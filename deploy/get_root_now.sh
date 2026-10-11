#!/usr/bin/env bash
# get_root_now.sh — per the verified 2026-09 flow: bridge helper + ghostlock_rv101 escalation -> root_daemon resident.
# One round per boot: the helper tries up to 3 times internally; a boot may be retriggered; if the
# exploit causes a kernel reboot, wait for boot and start over.
source "$(dirname "$0")/env.sh"   # shared ADB/SERIAL (see deploy/env.sh)
set -u
SERIAL="${SERIAL:?請設定 SERIAL 或讓 env.sh 自動偵測}"
ROOT_DIR="/home/kali/rokid_root"

# Lease: the deploy path originally took **no** lease at all, so it ran alongside the desktop
# app / App keeper, fighting over the files hardcoded in the binary (.ghostlock_root.sh,
# gl_run.log, ghostlock_rv101).
# SERIAL must be resolved first (lease.sh builds flag filenames from it), hence the source below.
source "$(dirname "$0")/lease.sh"
source "$(dirname "$0")/proc_util.sh"   # kill_all_helpers / clear_trigger_mailboxes
LEASE_OWNER="deploy"
LEASE_WAIT_S="${LEASE_WAIT_S:-300}"   # cap on waiting for the other side to yield (App keeper rebuilds take up to 5 minutes)
BRIDGE=/data/local/tmp/ghostlock_bridge
HELPER=/data/local/tmp/ghostlock_bridge_helper.sh
TRIGGER="$ROOT_DIR/deploy/trigger_root_daemon.cmd"
LOGFILE="$ROOT_DIR/deploy/get_root_now.log"

MAX_BOOTS=6            # max reboot rounds
TRIGGERS_PER_BOOT=2    # max triggers per boot
POLL_S=5
ROUND_TIMEOUT_S=950    # longest wait per round (helper 3-try cap ~640 s + margin)

adb_s() { "$ADB" -s "$SERIAL" "$@"; }
log()  { echo "[$(date '+%F %T')] $*" | tee -a "$LOGFILE"; }

# ---- wait for the device to finish booting and settle (boot_completed + low load) ----
wait_device() {
  for i in $(seq 1 60); do
    ST=$(adb_s get-state 2>/dev/null | tr -d '\r')
    if [ "$ST" = "device" ]; then
      BC=$(adb_s shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')
      if [ "$BC" = "1" ]; then
        # wait for uptime to settle (> 150 s: let the system calm down and dodge other auto-triggered exploits)
        for j in $(seq 1 36); do
          UP=$(adb_s shell cat /proc/uptime 2>/dev/null | awk '{print int($1)}' | tr -d '\r')
          if [ -n "$UP" ] && [ "$UP" -gt 150 ] 2>/dev/null; then return 0; fi
          sleep 10
        done
        return 0
      fi
    fi
    sleep 5
  done
  return 1
}

# ---- make sure the helper is resident (clear any old instance, then rebuild) ----
ensure_helper() {
  # The old `pkill -f 'ghostlock_bridge_helper.s[h]'` was measured to **miss** the `_desktop`
  # variant -> the desktop helper survived the whole run and was restarted after a device
  # reboot, triggering the exploit again.
  # kill_all_helpers kills **both** variants (host-side filter + send PIDs; see proc_util.sh).
  kill_all_helpers

  # The existence check accepts the **App helper only**: it is the one that reads
  # `ghostlock_bridge/last.cmd`. A broad pattern here would report "resident" when only the
  # desktop helper is alive, skip the rebuild, and later triggers would never return.
  if ! adb_s shell "ps -A -o ARGS 2>/dev/null | grep -q '[g]hostlock_bridge_helper.sh'"; then
    log "[helper] 不存在，執行 root_bridge.sh 重建並啟動"
    bash "$ROOT_DIR/deploy/root_bridge.sh" >> "$LOGFILE" 2>&1
  fi
  sleep 2
  adb_s shell "ps -A -o PID,ARGS 2>/dev/null | grep '[g]hostlock_bridge_helpe'" >> "$LOGFILE" 2>&1
}

# ---- clear leftover exploit processes ----
# Uses proc_util.kill_all_exploits: host-side filtering + batched PID sends, so
# self-matching is structurally impossible and killing 500+ processes stays under the
# adb call timeout.
cleanup_exploit() {
  kill_all_exploits
  adb_s shell "rm -f /data/local/tmp/.ghostlock_root.sh /data/local/tmp/gl_run.log" || return 1
  log "[cleanup] 殘留 ghostlock_rv101 行程已清除（殘留 $(exploit_count) 個）"
  return 0
}

# ---- trigger one escalation (write last.cmd; the helper must already be resident) ----
trigger() {
  adb_s shell "rm -f $BRIDGE/last.cmd $BRIDGE/last.out $BRIDGE/last.done $BRIDGE/last.progress $BRIDGE/payload.staging" || return 1
  adb_s push "$TRIGGER" "$BRIDGE/last.cmd" >/dev/null 2>&1 || return 1
  adb_s shell "chmod 666 $BRIDGE/last.cmd" || return 1
  log "[trigger] last.cmd 已寫入，等待 exploit..."
}

# ---- wait for one round's result; returns 0=OK 1=timeout/ERR 2=device reboot ----
wait_result() {
  local elapsed=0 PREV_PROG="" PREV_UP=0
  while [ $elapsed -lt $ROUND_TIMEOUT_S ]; do
    # device offline or uptime went backwards (reboot) => treat as a reboot caused by the exploit
    UP=$(adb_s shell cat /proc/uptime 2>/dev/null | awk '{print int($1)}' | tr -d '\r')
    if [ -z "$UP" ]; then
      log "[wait] 裝置離線，視為 reboot"
      return 2
    fi
    if [ "$PREV_UP" -gt 0 ] && [ "$UP" -lt $((PREV_UP - 5)) ]; then
      log "[wait] uptime 倒退 ${PREV_UP}s -> ${UP}s → 裝置已 reboot"
      return 2
    fi
    PREV_UP=$UP
    OUT=$(adb_s shell "cat $BRIDGE/last.out 2>/dev/null" | tr -d '\r')
    if [ "$OUT" = "OK" ]; then
      log "[wait] last.out = OK（payload 已以 root 執行完畢）"
      return 0
    fi
    case "$OUT" in
      ERR:*) log "[wait] last.out = $OUT"; return 1 ;;
    esac
    PROG=$(adb_s shell "cat $BRIDGE/last.progress 2>/dev/null" | tr -d '\r')
    if [ "$PROG" != "$PREV_PROG" ]; then
      log "[wait] progress: ${PROG:-(-)}"
      PREV_PROG="$PROG"
    fi
    sleep $POLL_S
    elapsed=$((elapsed + POLL_S))
  done
  log "[wait] 輪詢逾時 ($ROUND_TIMEOUT_S s)"
  return 1
}

# ---- verify root (direct socket to root_daemon) ----
verify_root() {
  log "[verify] last.done 內容："
  adb_s shell "cat $BRIDGE/last.done 2>/dev/null" | tee -a "$LOGFILE"
  log "[verify] 經 root_daemon socket 執行 id/getenforce："
  printf 'id; getenforce; ls -ld /data/root_proof 2>/dev/null; ps -A -o USER,PID,ARGS | grep [r]oot_daemon\n' \
    | adb_s shell "nc -U /data/local/tmp/root_daemon.sock" 2>&1 | tee -a "$LOGFILE"
  if adb_s shell "grep -q 'uid=0(root)' $BRIDGE/last.done 2>/dev/null"; then
    return 0
  fi
  return 1
}

: > "$LOGFILE"
log "===== get_root_now 開始（serial=$SERIAL）====="

# ---- take the lease (yield if unavailable; contract: a fresh lease must be honoured) ----
# The EXIT trap releases the lease on every exit path; INT/TERM become exits so the EXIT trap runs.
trap 'lease_cleanup "$LEASE_OWNER"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if ! lease_take "$LEASE_OWNER" 2>/dev/null; then
  log "[lease] 已被他人有效持有（$(lease_status)）→ 等待最多 ${LEASE_WAIT_S}s 讓對方做完"
  if ! lease_wait_free "$LEASE_OWNER" "$LEASE_WAIT_S" 2>>"$LOGFILE"; then
    log "[lease] ✗ 等不到租約（$(lease_status)）→ 放棄；避免與對方互搶共用檔案"
    exit 2
  fi
fi
log "[lease] ✓ 已取得租約（$(lease_status)）"
lease_autorenew_start "$LEASE_OWNER"
log "[lease] 背景續約已啟動（每 ${LEASE_RENEW_S}s 一次；TTL ${LEASE_TTL_SEC}s）"

# DRY_RUN=1: verify lease take/renew/release only, **without touching the device's exploit at
# all**. Use it to confirm the lease works before a real escalation (especially alongside the
# desktop app), avoiding an extra LPE run just to test the lease. DRY_RUN_HOLD_S keeps the
# "held" state observable.
if [ "${DRY_RUN:-0}" = "1" ]; then
  log "[dry-run] 租約已取得，不觸發 exploit（DRY_RUN=1）"
  if [ "${DRY_RUN_HOLD_S:-0}" -gt 0 ]; then
    log "[dry-run] 持有租約 ${DRY_RUN_HOLD_S}s 供外部觀察（$(lease_status)）"
    sleep "$DRY_RUN_HOLD_S"
  fi
  log "[dry-run] 結束；EXIT trap 將釋放租約"
  exit 0
fi

for boot in $(seq 1 "$MAX_BOOTS"); do
  log "===== BOOT ROUND $boot/$MAX_BOOTS ====="

  # if renewal lapsed past the TTL the lease may have been taken -> yield at once and stop touching shared files
  if lease_lost; then
    log "[lease] ✗ 租約已被他人取走（$(cat "$LEASE_LOST_FLAG" 2>/dev/null)）→ 中止"
    exit 2
  fi
  if ! wait_device; then
    log "[boot] 裝置等不到上線"
    if [ "$boot" -lt "$MAX_BOOTS" ]; then log "[boot] 呼叫 adb reboot 再試"; adb_s reboot 2>/dev/null; fi
    continue
  fi
  log "[boot] 裝置已上線，uptime=$(adb_s shell cat /proc/uptime | awk '{print int($1)}')s"
  cleanup_exploit
  ensure_helper

  BOOT_OK=0
  for t in $(seq 1 "$TRIGGERS_PER_BOOT"); do
    log "--- trigger $t/$TRIGGERS_PER_BOOT (boot $boot) ---"
    if ! trigger; then log "[trigger] 寫入失敗"; continue; fi
    wait_result; RC=$?
    if [ $RC -eq 0 ]; then
      if verify_root; then
        log "===== ROOT 已達成並驗證（boot $boot, trigger $t）====="
        exit 0
      else
        log "[verify] last.done 無 uid=0 → 再等一下 socket 驗證"
        sleep 5
        printf 'id\n' | adb_s shell "nc -U /data/local/tmp/root_daemon.sock" 2>&1 | tee -a "$LOGFILE"
        if adb_s shell "grep -q 'root_daemon.sock' /proc/net/unix 2>/dev/null"; then
          log "===== ROOT 已達成（socket 存活；daemon 即 root）====="
          exit 0
        fi
      fi
    fi
    if [ $RC -eq 2 ]; then BOOT_OK=2; break; fi   # reboot: this boot is over
    # RC=1: ERR/timeout, retrigger within the same boot (if budget remains)
  done

  if [ "$boot" -lt "$MAX_BOOTS" ]; then
    log "[boot] 第 $boot 輪未成功，adb reboot 進入下一輪"
    adb_s reboot 2>/dev/null
    sleep 15
  fi
done

log "===== 失敗：$MAX_BOOTS 輪皆未取得 root ====="
exit 1
