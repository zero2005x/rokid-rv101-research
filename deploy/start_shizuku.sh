#!/usr/bin/env bash
# start_shizuku.sh — per-boot Shizuku server bootstrap (Rokid RV101).
#
# Three things (order matters):
#   1) Verify/install the Shizuku manager APK (moe.shizuku.privileged.api, v13.6.0+, which
#      bundles the server class)
#   2) Pre-authorise: pm grant the glasses App moe.shizuku.manager.permission.API_V23 (a
#      dangerous permission; on startup the server writes the grant config back from the
#      runtime permission state -> no need to tap Allow in the glasses UI)
#   3) Start the server: adb shell <nativeLibraryDir>/libshizuku.so (no start.sh since
#      v13.6.0; the starter requires the caller to be root(0) or shell(2000), adb shell = 2000 ✓)
#
# Usage:
#   bash deploy/start_shizuku.sh
#   Overridable: ADB=/path/to/adb  SERIAL=<DEVICE_SERIAL>  MANAGER_APK=/path/to/shizuku-v13.x.x.apk
#
# NOTE: the Shizuku server does not survive a reboot; rerun this after every boot (including
# restarts caused by an exploit kernel panic).
# The glasses App can then run the exploit itself as shell (ShizukuGhostlockEngine), and falls
# back to the bridge helper automatically when Shizuku is unavailable.
source "$(dirname "$0")/env.sh"   # shared ADB/SERIAL (see deploy/env.sh)
set -u

MANAGER_APK="${MANAGER_APK:-}"
MANAGER_PKG="moe.shizuku.privileged.api"
APP_PKG="com.rokid.rc.glasses"
PERM="moe.shizuku.manager.permission.API_V23"

adb_s() { "$ADB" -s "$SERIAL" "$@"; }

echo "[0/4] 檢查裝置"
if ! adb_s get-state 2>/dev/null | grep -q device; then
  echo "  [!] 看不到裝置 $SERIAL（可用手機 termux-adb 替代 PC）"
  exit 1
fi

echo "[1/4] 確認 Shizuku manager（$MANAGER_PKG）"
APK_PATH=$(adb_s shell pm path "$MANAGER_PKG" 2>/dev/null | tr -d '\r' | sed -n 's/^package://p' | head -1)
if [ -n "$APK_PATH" ]; then
  echo "  [+] manager 已安裝：$APK_PATH"
else
  if [ -z "$MANAGER_APK" ]; then
    echo "  [!] manager 未安裝，且未給 MANAGER_APK 路徑。"
    echo "      請從 https://shizuku.rikka.app/download/ 下載 APK 後："
    echo "      MANAGER_APK=/path/to/shizuku.apk bash deploy/start_shizuku.sh"
    exit 1
  fi
  echo "  [..] 安裝 $MANAGER_APK"
  adb_s install -r "$MANAGER_APK" || { echo "  [x] 安裝失敗"; exit 1; }
fi

echo "[2/4] 預授權眼鏡 App（跳過授權 UI）"
adb_s shell "pm grant $APP_PKG $PERM" 2>&1 | sed 's/^/  /'
# The grant config is rebuilt from the runtime permission state when the server starts; the grant here is enough.
echo "  （若顯示 Unknown permission 屬正常：manager 首次啟動後再重跑一次即可）"

echo "[3/4] 推導 libshizuku.so 並啟動 server"
LIB_SO="${APK_PATH%/base.apk}/lib/arm64/libshizuku.so"
# the path may change after install; fetch it again
APK_PATH=$(adb_s shell pm path "$MANAGER_PKG" 2>/dev/null | tr -d '\r' | sed -n 's/^package://p' | head -1)
LIB_SO="${APK_PATH%/base.apk}/lib/arm64/libshizuku.so"
echo "  lib: $LIB_SO"
if ! adb_s shell "[ -x $LIB_SO ]" 2>/dev/null; then
  echo "  [!] $LIB_SO 不存在（ABI 非 arm64？）。列出可用 lib："
  adb_s shell "ls \$(dirname $LIB_SO) 2>/dev/null"
  exit 1
fi
# kill the old server first (the starter does this too; belt and braces)
adb_s shell "pkill -9 -f shizuku_server 2>/dev/null; true"
adb_s shell "$LIB_SO" 2>&1 | sed 's/^/  /'
sleep 3

echo "[4/4] 驗證"
if adb_s shell "ps -A -o PID,NAME,ARGS 2>/dev/null | grep -v grep | grep shizuku_server"; then
  echo "  [+] Shizuku server 已常駐（uid 2000/shell 身份，等同 adb shell）"
  echo "  完成。眼鏡 App 會走 Shizuku 引擎跑 exploit；不可用時自動回退 bridge helper。"
else
  echo "  [!] 未看到 shizuku_server 進程，請手動檢查（logcat -s ShizukuServer）"
  exit 1
fi
