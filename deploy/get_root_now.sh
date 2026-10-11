#!/usr/bin/env bash
# get_root_now.sh — 依 2026-09 驗證流程：bridge helper + ghostlock_rv101 提權 → root_daemon 常駐。
# 每 boot 一輪：helper 內部最多試 3 次；同 boot 可重新觸發；若 exploit 造成 kernel reboot 則等開機後重來。
source "$(dirname "$0")/env.sh"   # 共用 ADB/SERIAL（見 deploy/env.sh）
set -u
SERIAL="${SERIAL:?請設定 SERIAL 或讓 env.sh 自動偵測}"
ROOT_DIR="/home/kali/rokid_root"

# 租約：deploy 路徑原本**完全沒有**取租約，於是會與桌面板／App keeper 同時跑，
# 互搶 binary 內寫死的共用檔案（.ghostlock_root.sh、gl_run.log、ghostlock_rv101）。
# SERIAL 必須先確定（lease.sh 用它組旗標檔名），故 source 置於此行之後。
source "$(dirname "$0")/lease.sh"
source "$(dirname "$0")/proc_util.sh"   # kill_all_helpers／clear_trigger_mailboxes
LEASE_OWNER="deploy"
LEASE_WAIT_S="${LEASE_WAIT_S:-300}"   # 等對方讓開的上限（App keeper 單次重建上限 5 分鐘）
BRIDGE=/data/local/tmp/ghostlock_bridge
HELPER=/data/local/tmp/ghostlock_bridge_helper.sh
TRIGGER="$ROOT_DIR/deploy/trigger_root_daemon.cmd"
LOGFILE="$ROOT_DIR/deploy/get_root_now.log"

MAX_BOOTS=6            # 最多 reboot 輪數
TRIGGERS_PER_BOOT=2    # 同 boot 最多觸發次數
POLL_S=5
ROUND_TIMEOUT_S=950    # 每輪最長等待(helper 3 try 上限 ~ 640s + 餘裕)

adb_s() { "$ADB" -s "$SERIAL" "$@"; }
log()  { echo "[$(date '+%F %T')] $*" | tee -a "$LOGFILE"; }

# ---- 等待裝置完成開機並穩定 (boot_completed + 低負載) ----
wait_device() {
  for i in $(seq 1 60); do
    ST=$(adb_s get-state 2>/dev/null | tr -d '\r')
    if [ "$ST" = "device" ]; then
      BC=$(adb_s shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')
      if [ "$BC" = "1" ]; then
        # 等 uptime 穩定（> 150s，讓系統安定，也避開其他自動觸發的 exploit）
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

# ---- 確保 helper 常駐存在（先清掉任何舊實例再重建）----
ensure_helper() {
  # 舊寫法 `pkill -f 'ghostlock_bridge_helper.s[h]'` 實測**漏殺** `_desktop` 變體
  # → 桌面板 helper 整場存活，並在裝置重開機後被重新拉起而再觸發 exploit。
  # kill_all_helpers 以「host 端過濾 + 送 PID」殺掉**兩個**變體（見 proc_util.sh）。
  kill_all_helpers

  # 存在性檢查**只認 App helper**：它才是讀 `ghostlock_bridge/last.cmd` 的那一個。
  # 若這裡誤用廣義 pattern，會在只有桌面板 helper 時誤判為「已常駐」而不再重建，
  # 導致後續 trigger 永遠等不到結果。
  if ! adb_s shell "ps -A -o ARGS 2>/dev/null | grep -q '[g]hostlock_bridge_helper.sh'"; then
    log "[helper] 不存在，執行 root_bridge.sh 重建並啟動"
    bash "$ROOT_DIR/deploy/root_bridge.sh" >> "$LOGFILE" 2>&1
  fi
  sleep 2
  adb_s shell "ps -A -o PID,ARGS 2>/dev/null | grep '[g]hostlock_bridge_helpe'" >> "$LOGFILE" 2>&1
}

# ---- 清掉殘留的 exploit 行程 ----
# 改用 proc_util.kill_all_exploits：host 端過濾 + 分批送 PID，
# 結構上不可能自我匹配，且殺 500+ 個程序不會超過 adb 呼叫的等待上限。
cleanup_exploit() {
  kill_all_exploits
  adb_s shell "rm -f /data/local/tmp/.ghostlock_root.sh /data/local/tmp/gl_run.log" || return 1
  log "[cleanup] 殘留 ghostlock_rv101 行程已清除（殘留 $(exploit_count) 個）"
  return 0
}

# ---- 觸發一次提權（寫 last.cmd；helper 必須已在常駐）----
trigger() {
  adb_s shell "rm -f $BRIDGE/last.cmd $BRIDGE/last.out $BRIDGE/last.done $BRIDGE/last.progress $BRIDGE/payload.staging" || return 1
  adb_s push "$TRIGGER" "$BRIDGE/last.cmd" >/dev/null 2>&1 || return 1
  adb_s shell "chmod 666 $BRIDGE/last.cmd" || return 1
  log "[trigger] last.cmd 已寫入，等待 exploit..."
}

# ---- 單輪等待結果；回傳 0=OK 1=超時/ERR 2=裝置 reboot ----
wait_result() {
  local elapsed=0 PREV_PROG="" PREV_UP=0
  while [ $elapsed -lt $ROUND_TIMEOUT_S ]; do
    # 裝置離線或 uptime 倒退（重新開機）=> 視為 exploit 造成 reboot
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

# ---- 驗證 root（socket 直連 root_daemon）----
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

# ---- 取租約（拿不到就讓開；契約：看到新鮮租約必須讓）----
# EXIT trap 確保任何結束路徑都會釋放租約；INT/TERM 先轉成 exit 才會觸發 EXIT trap。
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

# DRY_RUN=1：只驗證租約取得／續約／釋放，**完全不碰裝置上的 exploit**。
# 用於在真正提權之前確認租約機制正常（尤其與桌面板／App 併行時），
# 避免為了測試租約而多跑一次 LPE。DRY_RUN_HOLD_S 可讓「持有中」的狀態被觀察到。
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

  # 續約若中斷超過 TTL，租約可能已被對方取走 → 立刻讓開，不要繼續改共用檔案
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
    if [ $RC -eq 2 ]; then BOOT_OK=2; break; fi   # reboot：此 boot 結束
    # RC=1：ERR/逾時，同一 boot 重新觸發（若還有額度）
  done

  if [ "$boot" -lt "$MAX_BOOTS" ]; then
    log "[boot] 第 $boot 輪未成功，adb reboot 進入下一輪"
    adb_s reboot 2>/dev/null
    sleep 15
  fi
done

log "===== 失敗：$MAX_BOOTS 輪皆未取得 root ====="
exit 1
