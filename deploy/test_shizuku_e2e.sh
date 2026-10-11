#!/usr/bin/env bash
# test_shizuku_e2e.sh — E2E 測試：Shizuku 路徑端到端提權（含 kernel-panic 重試）
#
# 流程：設定 root_mode=AUTO（run-as 寫 prefs，debug build 可用）→ 確保 Shizuku server
# 活著 → 清 logcat → 啟動 CaptureService → App 內 ShizukuGhostlockEngine 以 shell 身份
# 跑 ghostlock_rv101 → 等 root_daemon socket 回 uid=0。
# exploit ~50% 機率 kernel panic→reboot；裝置重啟後自動重啟 Shizuku server 並重試。
#
# 用法：ADB=... SERIAL=<DEVICE_SERIAL> bash deploy/test_shizuku_e2e.sh
source "$(dirname "$0")/env.sh"   # 共用 ADB/SERIAL（見 deploy/env.sh）
set -u

MAX_ATTEMPTS="${MAX_ATTEMPTS:-3}"
POLL_SECONDS="${POLL_SECONDS:-480}"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

adb_s() { "$ADB" -s "$SERIAL" "$@"; }

device_alive() { adb_s get-state 2>/dev/null | grep -q device; }

check_root() {
  local out
  out=$(adb_s shell "echo id | nc -U /data/local/tmp/root_daemon.sock 2>/dev/null" 2>/dev/null | tr -d '\r')
  case "$out" in *"uid=0"*) return 0 ;; *) return 1 ;; esac
}

wait_device() {
  for _ in $(seq 1 60); do
    if device_alive; then sleep 8; return 0; fi
    sleep 5
  done
  return 1
}

ensure_shizuku() {
  if ! adb_s shell "pidof shizuku_server" 2>/dev/null | grep -q .; then
    echo "  [shizuku] server 不在，重啟 bootstrap..."
    ADB="$ADB" SERIAL="$SERIAL" bash "$ROOT_DIR/deploy/start_shizuku.sh" >/dev/null 2>&1 || {
      echo "  [shizuku] bootstrap 失敗"; return 1; }
    sleep 3
  fi
  echo "  [shizuku] server pid=$(adb_s shell pidof shizuku_server 2>/dev/null | tr -d '\r')"
}

set_auto_mode() {
  adb_s shell am force-stop com.rokid.rc.glasses 2>/dev/null
  adb_s shell "run-as com.rokid.rc.glasses sh -c 'mkdir -p shared_prefs; cat > shared_prefs/rokid_glasses.xml'" <<'EOF' 2>/dev/null
<?xml version="1.0" encoding="utf-8" standalone="yes" ?>
<map>
    <string name="root_mode">AUTO</string>
</map>
EOF
  echo "  [prefs] $(adb_s shell run-as com.rokid.rc.glasses cat shared_prefs/rokid_glasses.xml 2>/dev/null | tr -d '\r' | grep -o 'AUTO')"
}

for attempt in $(seq 1 "$MAX_ATTEMPTS"); do
  echo "===== E2E attempt $attempt/$MAX_ATTEMPTS $(date +%H:%M:%S) ====="
  if ! device_alive; then
    echo "[!] 裝置離線，等待回來"
    wait_device || { echo "[x] 裝置 5 分鐘未回"; exit 1; }
  fi
  ensure_shizuku || exit 1
  set_auto_mode
  adb_s logcat -c 2>/dev/null
  # Android 12 背景啟動限制：shell 直接 am start-foreground-service 會被打回
  # （"app is in background"），改走 App 自身路徑：launch MainActivity →
  # onResume 前台啟動 CaptureService（rootMode=AUTO → 自動跑 acquireRoot）。
  adb_s shell "am start -n com.rokid.rc.glasses/.MainActivity" 2>&1 | sed 's/^/  /'
  sleep 3
  if ! adb_s shell "pidof com.rokid.rc.glasses" 2>/dev/null | grep -q .; then
    echo "  [fallback] 直接嘗試 FGS 啟動"
    adb_s shell "am start-foreground-service -n com.rokid.rc.glasses/.CaptureService" 2>&1 | sed 's/^/  /'
  fi
  echo "  [service] 已啟動，輪詢最多 ${POLL_SECONDS}s..."

  deadline=$(( $(date +%s) + POLL_SECONDS ))
  while [ "$(date +%s)" -lt "$deadline" ]; do
    if ! device_alive; then
      echo "  [panic?] 裝置離線（kernel panic → reboot 中），等待回來"
      wait_device && echo "  [device] 回來（uptime: $(adb_s shell uptime 2>/dev/null | tr -d '\r')）" \
        || { echo "  [x] 裝置 5 分鐘未回"; exit 1; }
      break
    fi
    if check_root; then
      echo "ROOT_OK attempt=$attempt at $(date +%H:%M:%S)"
      echo "  [verify] $(adb_s shell 'echo id | nc -U /data/local/tmp/root_daemon.sock; getenforce' 2>/dev/null | tr -d '\r' | tr '\n' ' ')"
      echo "  [logcat] $(adb_s logcat -d -s CaptureService:* ShizukuGhostlockEngine:* 2>/dev/null | tr -d '\r' | tail -6 | tr '\n' '|')"
      exit 0
    fi
    sleep 10
  done
  echo "  [attempt-end] $(adb_s shell 'getenforce; uptime' 2>/dev/null | tr -d '\r' | tr '\n' ' ')"
  echo "  [logcat] $(adb_s logcat -d -s CaptureService:* ShizukuGhostlockEngine:* 2>/dev/null | tr -d '\r' | tail -8 | tr '\n' '|')"
done
echo "ALL_ATTEMPTS_FAILED"
exit 1
