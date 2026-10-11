#!/usr/bin/env bash
# lock_ota.sh — lock OTA so the glasses cannot upgrade to 1.25.012 (GhostLock patched there, irreversible).
#
# Rationale (see research/shizuku/alternatives_and_ota.md §C):
#   This Rokid build's pm does not support disable-user ("Failure [not support for 0]"), so
#   use pm suspend: a suspended app cannot run or receive broadcasts, its state persists in
#   package settings and was measured to survive 3 reboots (see
#   reports/shizuku_integration_test_report.md §3). Other devices fall back to disable-user
#   (written to package-restrictions.xml, survives reboot).
#   The glasses have no network themselves; the risk is the official phone App proxying, and
#   disabling the OTA app cuts the device-side entry point.
#
# Usage (host adb or phone Termux adb both work):
#   bash deploy/lock_ota.sh
# Overridable: ADB=/path/to/adb SERIAL=<DEVICE_SERIAL>
#
# Restore (suspend path):
#   adb shell pm unsuspend com.rokid.glass.ota
# Restore (disable-user fallback path):
#   adb shell pm enable com.rokid.glass.ota
#   adb shell cmd package install-existing com.rokid.glass.ota
source "$(dirname "$0")/env.sh"   # shared ADB/SERIAL (see deploy/env.sh)
set -u

OTA_PKG="com.rokid.glass.ota"
# Secondary target (relay service). ⚠ may affect other phone-tethered features, so it is
# "not" disabled by default; uncomment the next line to disable it.
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
# This Rokid build's pm does not support disable-user (it returns "Failure [not support for
# 0]"), so use pm suspend (suspended = cannot run or receive broadcasts, state persists in
# package settings and survives reboot).
DISABLED=0
adb_s shell "pm suspend $OTA_PKG" 2>&1 | sed 's/^/  /'
adb_s shell "pm list packages -s" 2>/dev/null | grep -q "$OTA_PKG" && DISABLED=1
if [ "$DISABLED" = "0" ]; then
  # fallback: other devices use disable-user
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
