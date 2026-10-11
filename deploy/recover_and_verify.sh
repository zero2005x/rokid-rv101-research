#!/usr/bin/env bash
# recover_and_verify.sh — one-shot regression flow once the glasses' USB adb recovers.
#
# Background: restarting adbd on this build loses USB enumeration (caused by the old
# ENABLE_WIFI_ADB). Once the glasses recover (replug USB or reboot), run this script:
#   1) install the non-destructive glasses APK (ENABLE_WIFI_ADB no longer restarts adbd)
#   2) rerun the escalation (Shizuku E2E, with automatic retry after a panic)
#   3) verify WIFI_ADB only reports unsupported and USB adb stays connected
source "$(dirname "$0")/env.sh"   # shared ADB/SERIAL (see deploy/env.sh)
set -u

# phone serial (optional): supplied via the PHONE environment variable, no longer hardcoded
PHONE="${PHONE:-}"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

adb_s() { "$ADB" -s "$SERIAL" "$@"; }
adb_p() { "$ADB" -s "$PHONE" "$@"; }

echo "[0/4] 檢查眼鏡 USB adb"
if ! adb_s get-state 2>/dev/null | grep -q device; then
  echo "  [!] 眼鏡 adb 未恢復。請先：拔插 USB 線，或長按電源鍵重啟眼鏡。"
  exit 1
fi
echo "  [+] 眼鏡 adb 已恢復"

echo "[1/4] 安裝非破壞性版眼鏡 APK"
"$ADB" -s "$SERIAL" install -r "$(wslpath -w "$ROOT_DIR/android/app-glasses/build/outputs/apk/debug/app-glasses-debug.apk" 2>/dev/null || echo "$ROOT_DIR/android/app-glasses/build/outputs/apk/debug/app-glasses-debug.apk")" 2>&1 | tr -d '\r' | tail -1

echo "[2/4] 重跑提權（Shizuku E2E，自動重試）"
ADB="$ADB" SERIAL="$SERIAL" bash "$ROOT_DIR/deploy/start_shizuku.sh" >/dev/null 2>&1
ADB="$ADB" SERIAL="$SERIAL" bash "$ROOT_DIR/deploy/test_shizuku_e2e.sh" 2>&1 | tr -d '\r' | tail -3

echo "[3/4] 手機 WIFI_ADB 驗證（應回報 not supported、USB 不斷）"
adb_p shell "am force-stop com.rokid.rc.phone; am start -n com.rokid.rc.phone/.MainActivity" >/dev/null 2>&1
sleep 10
adb_p shell "am broadcast -n com.rokid.rc.phone/.DebugReceiver -a com.rokid.rc.phone.DEBUG --es cmd WIFI_ADB" >/dev/null 2>&1
sleep 25
echo "  [phone] $(adb_p shell 'logcat -d -s DebugReceiver:* 2>/dev/null | grep WIFI_ADB | tail -1' | tr -d '\r')"

echo "[4/4] USB adb 存活驗證（不應斷線）"
if adb_s get-state 2>/dev/null | grep -q device; then
  echo "  [+] USB adb 仍連線（非破壞性驗證通過）"
  echo "  完成。"
else
  echo "  [x] USB adb 又斷了——請檢查 build 是否為最新、並回報現象。"
  exit 1
fi
