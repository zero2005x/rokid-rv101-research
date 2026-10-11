#!/usr/bin/env bash
# proc_util.sh — the "safe process killing" helper shared by the deploy scripts.
#
# ## Why `pkill -f` cannot be used
#
# Two measured pitfalls:
#
#  1. **Self-matching**: the `pkill -f <pattern>` pattern also matches **the shell running
#     it** (the cmdline of `sh -c "pkill -f ghostlock_rv101"` contains that string) -> the
#     shell kills itself, exit code 137, cleanup aborted. (see `docs/lease-and-keeper.md`)
#
#  2. **A too-narrow pattern misses processes**: measured on-device with `grep -E` (2026-09-13):
#
#       pattern="ghostlock_bridge_helper.sh"    → matches the App helper, **does not match** the _desktop variant
#       pattern="ghostlock_bridge_helper.s[h]"  → matches the App helper, **does not match** the _desktop variant
#       pattern="ghostlock_bridge_helpe[r]"     → matches both
#
#     Because `helper.sh` and `helper_desktop.sh` diverge after "helper".
#     That miss let the desktop helper survive the whole run; after the device rebooted on
#     its own it was restarted, saw the leftover `last.cmd` and triggered the exploit again
#     -> **994 child processes, load 75.8**.
#
# ## What this file does
#
# **Filter on the host, send only PIDs to the device to kill**:
#
#   ps (device) → grep/awk (host) → kill -9 <PID> (device)
#
# The only command run on the device is `ps -A -o PID,ARGS`, so **the pattern never appears
# in a device cmdline** and self-matching is structurally impossible. This pairs with the
# project's existing lesson: take PIDs from `ps`, then kill them in batches.
set -u

_PROC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
[ -n "${ADB:-}" ] && [ -n "${SERIAL:-}" ] || source "$_PROC_DIR/env.sh"

# Common prefix of the two helper variants: ending at `helpe` covers both
#   ghostlock_bridge_helper.sh
#   ghostlock_bridge_helper_desktop.sh
HELPER_PAT='ghostlock_bridge_helpe'
EXPLOIT_PAT='ghostlock_rv101'
ROOT_SOCK='/data/local/tmp/root_daemon.sock'

_padb() { "$ADB" -s "$SERIAL" "$@"; }

# ---- root channel ----
# After a successful escalation the exploit's processes are **owned by root**; adb shell
# (uid 2000) cannot kill them (`kill` returns EPERM). So leftover-exploit cleanup must have a
# root channel. Same reasoning as `root_restore_delayed.sh`: only root can clean up root.
# NOTE: "$ADB" must be called directly, **not** as `timeout 25 _padb ...` — `timeout` can only
# run external programs; handing it a shell function fails to exec and **silently** returns an
# empty string, so root_ok is misread as false and the whole escalation cleanup is a no-op
# (measured).
root_exec() {
  printf '%s\n' "${1:?root_exec <cmd>}" | timeout 25 "$ADB" -s "$SERIAL" shell "nc -U $ROOT_SOCK" 2>/dev/null | tr -d '\r'
}
root_ok() { root_exec 'id' | grep -q 'uid=0'; }

# list device PIDs matching regex (host-side filter; device cmdline untouched)
dev_pids_matching() {
  local pat="${1:?dev_pids_matching <regex>}"
  _padb shell "ps -A -o PID,ARGS" 2>/dev/null | tr -d '\r' \
    | grep -E "$pat" | grep -v '[g]rep' | awk '{print $1}' | grep -E '^[0-9]+$'
}

# Kill processes matching regex, **shell first, then the root channel for survivors**.
# When the root channel is unavailable, report the leftover count (the caller decides
# whether to abort).
kill_matching_escalating() {
  local pat="${1:?}" label="${2:-$1}" pids left
  pids=$(dev_pids_matching "$pat")
  [ -n "$pids" ] || return 0
  _padb shell "kill -9 $pids" >/dev/null 2>&1
  sleep 1
  left=$(dev_pids_matching "$pat")
  if [ -n "$left" ] && root_ok; then
    local batch="" n=0 pid
    for pid in $left; do
      batch="$batch $pid"; n=$((n + 1))
      if [ "$n" -ge 200 ]; then
        root_exec "kill -9$batch" >/dev/null 2>&1; batch=""; n=0
      fi
    done
    [ -n "$batch" ] && root_exec "kill -9$batch" >/dev/null 2>&1
    sleep 1
    left=$(dev_pids_matching "$pat")
  fi
  local total; total=$(echo "$pids" | wc -w | tr -d ' ')
  if [ -n "$left" ]; then
    echo "  [kill] $label：送出 $total 個 kill，仍有 $(echo "$left" | wc -w | tr -d ' ') 個殘留（可能需要 root 通道）" >&2
  else
    echo "  [kill] $label：已清除 $total 個程序" >&2
  fi
  return 0
}

# Kill processes matching regex. Returns the number of kills sent.
#
# Perf note: never "one adb call per PID" — killing 515 processes measured over 60 s and
# timed out the caller (cleanup half done). Batch instead: collect PIDs on the host, then send
# one adb call with the whole batch of kills.
kill_matching() {
  local pat="${1:?kill_matching <regex>}" label="${2:-$1}" pids pid n=0 batch_n=0
  pids=$(dev_pids_matching "$pat")
  [ -n "$pids" ] || return 0

  local batch=""
  for pid in $pids; do
    batch="$batch $pid"
    batch_n=$((batch_n + 1))
    # send a batch every 200 to keep the command line short
    if [ "$batch_n" -ge 200 ]; then
      _padb shell "kill -9$batch" >/dev/null 2>&1
      batch=""; batch_n=0
    fi
  done
  [ -n "$batch" ] && _padb shell "kill -9$batch" >/dev/null 2>&1

  n=$(echo "$pids" | wc -w | tr -d ' ')
  sleep 1   # kill is asynchronous; wait a moment before the caller counts leftovers
  echo "  [kill] $label：送出 $n 個 kill -9" >&2
  return 0
}

# For convergence: kill the trigger source (helper) first, then the exploit children it spawned.
kill_all_helpers()  { kill_matching "$HELPER_PAT"  "bridge helper（含 _desktop）"; }
# after escalation the exploit is root-owned -> use the version that escalates to the root channel
kill_all_exploits() { kill_matching_escalating "$EXPLOIT_PAT" "ghostlock_rv101"; }

# Clear the trigger files of both mailboxes — **the root cause of the runaway respawn
# loop**: after a device reboot the helper is restarted, sees the leftover last.cmd, and
# immediately runs the exploit again.
clear_trigger_mailboxes() {
  _padb shell "rm -f \
    /data/local/tmp/ghostlock_bridge/last.cmd \
    /data/local/tmp/ghostlock_bridge_desktop/last.cmd \
    /data/local/tmp/ghostlock_bridge_desktop/last.progress" >/dev/null 2>&1
}

# current process counts (diagnostics)
helper_count()  { dev_pids_matching "$HELPER_PAT"  | wc -l | tr -d ' '; }
exploit_count() { dev_pids_matching "$EXPLOIT_PAT" | wc -l | tr -d ' '; }

# Run directly: show current state
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  case "${1:-status}" in
    status)
      echo "helpers=$(helper_count)  exploits=$(exploit_count)"
      for p in $(dev_pids_matching "$HELPER_PAT"); do
        echo "  helper pid=$p"
      done
      ;;
    kill)
      kill_all_helpers; kill_all_exploits; clear_trigger_mailboxes
      echo "after: helpers=$(helper_count)  exploits=$(exploit_count)"
      ;;
    *) echo "用法：bash deploy/proc_util.sh [status|kill]"; exit 64 ;;
  esac
fi
