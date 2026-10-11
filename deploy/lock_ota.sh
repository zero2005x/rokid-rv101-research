#!/usr/bin/env bash
# lock_ota.sh — 鎖定 OTA，防止眼鏡升到 1.25.012（已 backport 修補 GhostLock，不可逆）。
#
# 原理（見 research/shizuku/alternatives_and_ota.md §C）：
#   本 Rokid build 的 pm 不支援 disable-user（"Failure [not support for 0]"），
#   改採 pm suspend：暫停的 app 不可執行/不可收廣播，狀態持久化於 package settings，
#   實測跨 3 次 reboot 仍維持（見 reports/shizuku_integration_test_report.md §3）。
#   一般裝置則走 disable-user 兜底（寫入 package-restrictions.xml，reboot 後維持）。
#   眼鏡本身無網路，風險在官方手機 App 代理；關掉 OTA app 即斷掉裝置端入口。
#
# 用法（host 或手機 Termux 的 adb 皆可）：
#   bash deploy/lock_ota.sh
# 可覆寫：ADB=/path/to/adb SERIAL=<DEVICE_SERIAL>
#
# 還原（suspend 路徑）：
#   adb shell pm unsuspend com.rokid.glass.ota
# 還原（disable-user 兜底路徑）：
#   adb shell pm enable com.rokid.glass.ota
#   adb shell cmd package install-existing com.rokid.glass.ota
source "$(dirname "$0")/env.sh"   # 共用 ADB/SERIAL（見 deploy/env.sh）
set -u

OTA_PKG="com.rokid.glass.ota"
# 次要目標（中繼服務）。⚠ 可能影響其他手機連動功能，預設「不」關閉；
# 要關請把下一行註解拿掉。
# ASSIST_PKG="com.rokid.os.sprite.assistserver"

adb_s() { "$ADB" -s "$SERIAL" "$@"; }

echo "[0/3] 檢查裝置與韌體版本"
if ! adb_s get-state 2>/dev/null | grep -q device; then
  echo "  [!] 看不到裝置 $SERIAL，請先連線（或用手機 termux-adb）"
  exit 1
fi
FP=$(adb_s shell getprop ro.build.fingerprint 2>/dev/null | tr -d '\r')
echo "  fingerprint: $FP"
case "$FP" in
  *1.22.009*) echo "  [+] 仍是漏洞版 1.22.009，繼續" ;;
  *1.25.012*) echo "  [!!!] 已是 1.25.012（GhostLock 已修補）。停止！" ; exit 2 ;;
  *) echo "  [!] 非預期版本字串，請人工確認後再繼續（Ctrl-C 中止）" ;;
esac

echo "[1/3] 停用 OTA app：$OTA_PKG"
# 此 Rokid build 的 pm 不支援 disable-user（回 "Failure [not support for 0]"），
# 改用 pm suspend（暫停=不可執行/不可收廣播，狀態持久化於 package settings，reboot 後維持）。
DISABLED=0
adb_s shell "pm suspend $OTA_PKG" 2>&1 | sed 's/^/  /'
adb_s shell "pm list packages -s" 2>/dev/null | grep -q "$OTA_PKG" && DISABLED=1
if [ "$DISABLED" = "0" ]; then
  # 兜底：一般裝置走 disable-user
  adb_s shell "pm disable-user --user 0 $OTA_PKG" 2>&1 | sed 's/^/  /'
  adb_s shell "pm list packages -d" 2>/dev/null | grep -q "$OTA_PKG" && DISABLED=1
fi

echo "[2/3] 驗證（suspended 或 disabled 清單應出現 $OTA_PKG）"
if [ "$DISABLED" = "1" ]; then
  echo "  [+] $OTA_PKG 已停用（suspend/disable）"
else
  echo "  [!] 驗證失敗，請檢查上面輸出"
  exit 1
fi

if [ -n "${ASSIST_PKG:-}" ]; then
  echo "[2.5/3] 停用中繼服務：$ASSIST_PKG"
  adb_s shell "pm disable-user --user 0 $ASSIST_PKG" 2>&1
fi

echo "[3/3] 完成。注意："
echo "  - 研究期間建議眼鏡與官方 Rokid App 藍牙斷連 / 不登入帳號（雙保險）"
echo "  - 還原指令：adb shell pm unsuspend $OTA_PKG"
