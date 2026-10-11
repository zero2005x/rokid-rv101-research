#!/usr/bin/env bash
set -u

# ===================== 依你的環境調整 =====================
source "$(dirname "$0")/env.sh"   # 共用 ADB/SERIAL（見 deploy/env.sh）
GLASSES_SERIAL="${SERIAL:?請設定 SERIAL 或讓 env.sh 自動偵測}"   # 眼鏡設備序列號 (adb devices)
PHONE_SERIAL=""                    # 手機序列號（留空 = 用唯一已連線設備）
GLASSES_APK="/home/kali/rokid_root/android/app-glasses/build/outputs/apk/debug/app-glasses-debug.apk"
PHONE_APK="/home/kali/rokid_root/android/app-phone/build/outputs/apk/debug/app-phone-debug.apk"
GLASSES_PKG="com.rokid.rc.glasses"
# ==========================================================

adbg() { "$ADB" -s "$GLASSES_SERIAL" "$@"; }
adbp() {
  if [ -n "$PHONE_SERIAL" ]; then
    "$ADB" -s "$PHONE_SERIAL" "$@"
  else
    "$ADB" "$@"
  fi
}

echo "========== 眼鏡端佈署 =========="

echo "[1/5] 檢查 ghostlock_rv101"
if adbg shell '[ -x /data/local/tmp/ghostlock_rv101 ]' 2>/dev/null; then
  echo "  [+] /data/local/tmp/ghostlock_rv101 就緒"
else
  echo "  [!] /data/local/tmp/ghostlock_rv101 不存在或不可執行"
  echo "      請先手動推送："
  echo "        adb push <ghostlock_rv101> /data/local/tmp/ghostlock_rv101"
  echo "        adb shell chmod 755 /data/local/tmp/ghostlock_rv101"
fi

echo "[2/5] 安裝眼鏡 APK"
adbg install -r "$GLASSES_APK" || { echo "  [!] 安裝失敗"; exit 1; }

echo "[3/5] 啟動眼鏡 App（自動啟動 CaptureService）"
adbg shell am start -n "$GLASSES_PKG/.MainActivity" || { echo "  [!] 啟動失敗"; exit 1; }

echo "[4/5] 確認藍牙配對（傳輸為 RFCOMM，不需 WiFi IP）"
echo "  請先在系統藍牙設定中配對手機與眼鏡。"

echo ""
echo "========== 手機端佈署 =========="
if [ -f "$PHONE_APK" ]; then
  adbp install -r "$PHONE_APK" || echo "  (手機未連線或安裝失敗，可手動安裝 APK)"
else
  echo "  (未找到手機 APK，請先建置)"
fi

echo ""
echo "完成。開啟手機 App，選取已配對的眼鏡即可連線（藍牙 RFCOMM）。"
