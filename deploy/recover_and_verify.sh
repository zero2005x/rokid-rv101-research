#!/usr/bin/env bash
# recover_and_verify.sh — 眼鏡 USB adb 恢復後的一鍵回歸流程。
#
# 背景：此 build 的 adbd 重啟會讓 USB 列舉丟失（ENABLE_WIFI_ADB 舊版所致）。
# 眼鏡恢復（拔插 USB 或重啟）後跑本腳本：
#   1) 安裝非破壞性版眼鏡 APK（ENABLE_WIFI_ADB 不再重啟 adbd）
#   2) 重跑提權（Shizuku E2E，含 panic 自動重試）
#   3) 驗證 WIFI_ADB 只回報不支援、且 USB adb 保持連線
source "$(dirname "$0")/env.sh"   # 共用 ADB/SERIAL（見 deploy/env.sh）
set -u

# 手機序號（選用）：可由環境變數 PHONE 提供，不再寫死於腳本
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
