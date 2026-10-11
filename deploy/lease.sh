#!/usr/bin/env bash
# lease.sh — shared lease primitive for the deploy path (closes the deploy-side lease gap).
#
# ## Why this exists
#
# The three paths of the exploit itself are **hardcoded inside the `ghostlock_rv101` binary**
# and cannot be split in two the way the helper and the mailbox are:
#
#   /data/local/tmp/.ghostlock_root.sh   (payload; the other side replaces it)
#   /data/local/tmp/gl_run.log           (the other side truncates it)
#   /data/local/tmp/ghostlock_rv101      (the other side pkills it)
#
# So "who is running the exploit" must be serialised globally. There are **three** possible
# operators:
#
#   | operator                       | owner=    | where the lease is taken                      |
#   |--------------------------------|-----------|-----------------------------------------------|
#   | ZeroLightRokidDesktop          | `desktop` | `zero_light_desktop.c` (renews on the 30 s poll) |
#   | glasses App `RootDaemonKeeper` | `app`     | `RootDaemonKeeper.kt` (released in `finally`)  |
#   | deploy scripts (this file)     | `deploy`  | background renewal + `EXIT` trap release       |
#
# ## Contract (three places must stay in sync; change them together)
#
#   file:    `/data/local/tmp/ghostlock_op.lease`
#   content: `owner=<desktop|app|deploy> pid=<pid> ts=<unix seconds>`
#   TTL:     900 seconds; the holder must keep renewing while it works
#
# ## Why adding `owner=deploy` needs no change on the other two sides
#
# Both sides decide **deny-by-default** — only an owner equal to "itself" counts as its own:
#
#   desktop: `mine = strstr(txt, "owner=desktop")`
#   app    : `mine = txt.contains("owner=app")`
#
# So any unknown owner (including `deploy`) is treated as "held by someone else" and yields
# correctly. This was verified on the device (see `lease_selftest` below).
#
# ## Usage
#
#   source "$(dirname "$0")/lease.sh"
#   lease_take deploy || exit 2      # yield if the lease cannot be taken
#   lease_autorenew_start deploy     # background renewal (required for long flows)
#   trap 'lease_cleanup deploy' EXIT INT TERM
#   ...
#   lease_is_mine deploy || abort    # periodically confirm it was not stolen
#
# Requires `ADB` / `SERIAL` from `env.sh` (this file sources it; sourcing twice is harmless).
set -u

_LEASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
[ -n "${ADB:-}" ] && [ -n "${SERIAL:-}" ] || source "$_LEASE_DIR/env.sh"

# Lease file path. **All three sites must match exactly** (deploy/lease.sh, RootDaemonKeeper.kt,
# zero_light_desktop.c); `deploy/test_lease.sh` has a cross-file consistency check that blocks drift.
#
# ## Why it lives in the `ghostlock_bridge/` subdirectory and not in `/data/local/tmp` itself
#
# `/data/local/tmp` is `drwxrwx--x shell shell` -> the glasses App (uid 10053) is "other" and
# has only `--x` (traverse) with **no directory write permission**, so it was measured to be
# **unable to create or delete** files there. That broke the App-side lease for good:
# `takeLease()` always fails when the file is missing (-> the App runs the exploit without a
# lease) and `dropLease()` never works (-> a leftover lease blocks everyone else).
#
# `ghostlock_bridge/` is **777**, and the App measured `CREATE_OK` / `RM_OK` inside it.
# Moving the lease here **removes the permission trap at the root** and drops the reliance on
# the "reset only, never delete" convention.
LEASE_DIR="/data/local/tmp/ghostlock_bridge"
# The path is **deliberately written as a full literal** (not concatenated from "$LEASE_DIR/..."):
# this is a three-way contract and T9 in `deploy/test_lease.sh` compares the **literal value** in
# all three files to stop drift. Concatenating a variable would break that comparison and lose
# the protection.
LEASE_PATH="/data/local/tmp/ghostlock_bridge/ghostlock_op.lease"
# legacy path (under /data/local/tmp): used only for migration cleanup
LEASE_PATH_LEGACY="/data/local/tmp/ghostlock_op.lease"
LEASE_TTL_SEC="${LEASE_TTL_SEC:-900}"      # same as desktop / app; do not change casually
LEASE_RENEW_S="${LEASE_RENEW_S:-30}"       # renewal interval (desktop uses 30 s)

# renewal child pid and the "lease stolen" flag (the main process aborts on it)
LEASE_RENEW_PID=""
LEASE_LOST_FLAG="${TMPDIR:-/tmp}/ghostlock_lease_lost.${SERIAL:-none}"

_adb() { "$ADB" -s "$SERIAL" "$@"; }

# Read the lease. Returns 1 = **adb failed** (cannot tell), 0 = read (may be empty = no file).
# Sentinels distinguish "adb is down" from "the file does not exist" — the two mean completely
# different things for renewal: the former is transient (e.g. during a reboot), the latter
# means the lease was released or stolen.
lease_read() {
  local raw
  raw=$(_adb shell "echo __LB__; cat $LEASE_PATH 2>/dev/null; echo __LE__" 2>/dev/null | tr -d '\r')
  case "$raw" in
    *__LB__*__LE__*) printf '%s\n' "$raw" | sed -n '/__LB__/,/__LE__/p' | sed '1d;$d' ;;
    *) return 1 ;;
  esac
}

# current holder (empty string = nobody or adb failed; use lease_read to tell which)
lease_owner() { lease_read 2>/dev/null | sed -n 's/.*owner=\([A-Za-z][A-Za-z]*\).*/\1/p' | head -1; }

# lease age (seconds). -1 = cannot tell / invalid field (same semantics as desktop's lease_age_sec)
lease_age() {
  local ts
  ts=$(lease_read 2>/dev/null | sed -n 's/.*ts=\([0-9][0-9]*\).*/\1/p' | head -1)
  [ -n "${ts:-}" ] || { echo -1; return; }
  [ "$ts" -gt 1000000000 ] 2>/dev/null || { echo -1; return; }
  echo $(( $(date +%s) - ts ))
}

# 0 = the lease is fresh (someone holds it validly)
lease_fresh() {
  local age; age=$(lease_age)
  [ "$age" -ge 0 ] 2>/dev/null && [ "$age" -le "$LEASE_TTL_SEC" ]
}

# 0 = validly held by "someone else" (held by me / expired / nobody -> 1)
lease_held_by_other() {
  local me="${1:?lease_held_by_other <me>}" pid="${2:-}" owner
  lease_fresh || return 1
  owner=$(lease_owner)
  [ -n "$owner" ] || return 1
  lease_is_mine "$me" "$pid" && return 1
  return 0
}

# the lease's pid field
lease_pid() { lease_read 2>/dev/null | sed -n 's/.*pid=\([0-9][0-9]*\).*/\1/p' | head -1; }

# 0 = the lease is mine.
#
# **Identity = (owner, pid)**, not owner alone. Reason: deploy is the only operator where
# several instances of one owner can run at once — `get_root_now.sh` is a command-line script
# that can be started twice, both write `owner=deploy`, and comparing owner alone would make
# them treat each other as their own and **not exclude each other at all** (T3 in this
# project's test_lease.sh caught exactly that gap).
# desktop (a single GUI process, guarded by `g_busy`) and app (a single keeper, guarded by
# `rearming`) are singletons and exclude each other without the pid.
#
# When pid is omitted only the owner is compared — used by the cross-side checks that
# "simulate the desktop / app view".
lease_is_mine() {
  local me="${1:?lease_is_mine <me>}" pid="${2:-}" lpid
  [ "$(lease_owner 2>/dev/null)" = "$me" ] || return 1
  [ -z "$pid" ] && return 0
  lpid=$(lease_pid 2>/dev/null)
  [ "$lpid" = "$pid" ]
}

# Write the lease. **Write a temp file first, then mv**: mv within one directory is atomic and
# keeps readers from seeing half the content (a truncated `ts=` would be misread as "expired"
# and let the other side grab it).
# chmod 666 is required: the App runs as a different uid and the default umask may hide the file.
_lease_write() {
  local me="$1" pid="$2"
  _adb shell "mkdir -p $LEASE_DIR 2>/dev/null; chmod 777 $LEASE_DIR 2>/dev/null; \
    printf 'owner=$me pid=$pid ts=%s\n' \"\$(date +%s)\" > $LEASE_PATH.tmp \
    && mv $LEASE_PATH.tmp $LEASE_PATH && chmod 666 $LEASE_PATH" >/dev/null 2>&1
}

# Take the lease. 0 = taken; 1 = validly held by someone else (including **another instance of the same owner**).
lease_take() {
  local me="${1:?lease_take <me>}" pid="${2:-$$}"
  lease_held_by_other "$me" "$pid" && return 1
  _lease_write "$me" "$pid" || return 1
  # It can still be stolen after the pre-check (TOCTOU) -> read back to confirm it is really mine
  # (owner and pid must both match)
  lease_is_mine "$me" "$pid"
}

# Wait for the lease to free up (at most timeout seconds). 0 = taken; 1 = still held at timeout.
lease_wait_free() {
  local me="${1:?}" timeout="${2:-300}" waited=0 owner
  while [ "$waited" -lt "$timeout" ]; do
    lease_take "$me" && return 0
    owner=$(lease_owner 2>/dev/null)
    echo "[lease] 等待中：${waited}s/${timeout}s（目前持有者=${owner:-?}，age=$(lease_age)s）" >&2
    sleep 10; waited=$((waited + 10))
  done
  return 1
}

# Renew. **Only renew while I still hold it; never steal someone else's.**
# Returns 0 = renewed; 1 = the lease is no longer mine (stolen / deleted); 2 = transient
# failure (adb unavailable, e.g. during a reboot)
#
# NOTE: the `case` match must see **both** the same owner and the same pid before renewing —
# otherwise, after another `owner=deploy` instance takes over, this instance would renew it
# back to itself (mutual stealing).
lease_renew() {
  local me="${1:?lease_renew <me>}" pid="${2:-$$}" txt
  txt=$(lease_read) || return 2                 # adb failed -> transient, do not misread as stolen
  case "$txt" in
    "") return 1 ;;                             # file does not exist -> treat as lost
  esac
  case "$txt" in
    *"owner=$me"*"pid=$pid"*) _lease_write "$me" "$pid" && return 0 || return 2 ;;
  esac
  # same owner but a different pid -> another instance of the same kind, not mine
  return 1
}

# Reset to the "released" state: **write an expired marker, do not delete the file**.
#
# Why still "no delete" (even though the new path is writable):
#   1. **Consistency**: all three sides share one convention instead of each deciding whether it
#      has delete permission.
#   2. **No gap**: between delete and recreate a reader would see "file does not exist" = no
#      lease = up for grabs, exactly the TOCTOU breeding ground; writing a marker is a single
#      atomic `mv` with no gap.
#   3. The legacy path (`/data/local/tmp`) could never be deleted anyway, so keeping this
#      convention makes behaviour identical before and after the migration.
#   Released = write `owner=none pid=0 ts=0` (all three sides treat it as invalid).
lease_reset() {
  _adb shell "mkdir -p $LEASE_DIR 2>/dev/null; chmod 777 $LEASE_DIR 2>/dev/null; \
    printf 'owner=none pid=0 ts=0\n' > $LEASE_PATH 2>/dev/null; \
    chmod 666 $LEASE_PATH 2>/dev/null; true" >/dev/null 2>&1
}

# Ensure the lease directory and file exist with correct permissions (directory 777, file 666).
# The directory must be 777 so the App can create the lease file itself (otherwise the
# permission trap is back). If it already exists only the permissions are fixed, content
# untouched.
lease_ensure_file() {
  _adb shell "mkdir -p $LEASE_DIR 2>/dev/null; chmod 777 $LEASE_DIR 2>/dev/null; \
    if [ ! -e $LEASE_PATH ]; then \
      printf 'owner=none pid=0 ts=0\n' > $LEASE_PATH 2>/dev/null; \
    fi; chmod 666 $LEASE_PATH 2>/dev/null; true" >/dev/null 2>&1
}

# Migration: remove the lease file left at the legacy path
# (`/data/local/tmp/ghostlock_op.lease`) so a still-fresh old file cannot make it look like two
# leases exist at once.
lease_migrate_legacy() {
  _adb shell "rm -f $LEASE_PATH_LEGACY 2>/dev/null; true" >/dev/null 2>&1
}

# Release (reset only while still holding it, so someone else's lease is not cleared)
lease_drop() {
  local me="${1:?lease_drop <me>}" pid="${2:-$$}"
  lease_is_mine "$me" "$pid" || return 0
  lease_reset
}

# Background renewal. Writes a flag file when the lease is lost; the main process checks it with lease_lost.
lease_autorenew_start() {
  local me="${1:?lease_autorenew_start <me>}" pid="${2:-$$}" interval="${3:-$LEASE_RENEW_S}"
  rm -f "$LEASE_LOST_FLAG"
  (
    while true; do
      sleep "$interval"
      lease_renew "$me" "$pid"; rc=$?
      if [ "$rc" -eq 1 ]; then
        printf 'lost at %s (owner=%s)\n' "$(date '+%F %T')" "$(lease_owner 2>/dev/null)" > "$LEASE_LOST_FLAG"
        exit 0
      fi
      # rc=2 (transient adb failure) -> keep retrying, do not write the flag
    done
  ) &
  LEASE_RENEW_PID=$!
}

lease_autorenew_stop() {
  [ -n "${LEASE_RENEW_PID:-}" ] && kill "$LEASE_RENEW_PID" 2>/dev/null
  LEASE_RENEW_PID=""
}

# Called periodically by the main process in its loop: 0 = still safely held
lease_lost() { [ -f "$LEASE_LOST_FLAG" ]; }

# one-line status (for logs)
lease_status() {
  local owner age; owner=$(lease_owner 2>/dev/null); age=$(lease_age)
  echo "owner=${owner:-（無）} age=${age}s ttl=${LEASE_TTL_SEC}s"
}

# Teardown: stop renewal -> release -> clear the flag. Safe to call repeatedly from an EXIT trap.
lease_cleanup() {
  local me="${1:?lease_cleanup <me>}"
  lease_autorenew_stop
  lease_drop "$me"
  rm -f "$LEASE_LOST_FLAG"
}

# Self-test: verify the deny-by-default contract (both sides treat an unknown owner as someone else)
#   lease_selftest deploy
lease_selftest() {
  local me="${1:-deploy}" rc=0
  echo "[selftest] 目前租約：$(lease_status)"
  if lease_held_by_other "$me"; then
    echo "[selftest] 別的有效持有中 → 正確判定為『需讓開』"
  else
    echo "[selftest] 無人持有（或已過期）→ 正確判定為『可取得』"
  fi
  lease_take "$me"       || { echo "[selftest] ✗ 取租約失敗"; rc=1; }
  lease_is_mine "$me"    || { echo "[selftest] ✗ 取得後回讀不是自己"; rc=1; }
  lease_held_by_other "$me" && { echo "[selftest] ✗ 自己持有卻被判為別人"; rc=1; }
  # Take a different owner name to simulate "the other side's view": it must see deploy as someone else
  if lease_held_by_other desktop; then
    echo "[selftest] ✓ 以 desktop 視角看 deploy → 判定為『別人持有』（deny-by-default 成立）"
  else
    echo "[selftest] ✗ 以 desktop 視角竟看不到 deploy 的租約"; rc=1
  fi
  lease_drop "$me"
  echo "[selftest] 結束（rc=$rc）"
  return "$rc"
}

# Run the self-test when this file is executed directly: bash deploy/lease.sh [owner]
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  case "${1:-selftest}" in
    selftest) lease_selftest "${2:-deploy}" ;;
    status)   echo "租約狀態：$(lease_status)" ;;
    *) echo "用法：bash deploy/lease.sh [selftest|status] [owner]"; exit 64 ;;
  esac
fi
