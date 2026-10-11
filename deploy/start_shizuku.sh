#!/usr/bin/env bash
# start_shizuku.sh — 每 boot 一次的 Shizuku server bootstrap（Rokid RV101）。
#
# 做三件事（順序重要）：
#   1) 確認/安裝 Shizuku manager APK（moe.shizuku.privileged.api，v13.6.0+，server class 在內）
#   2) 預授權：pm grant 眼鏡 App 的 moe.shizuku.manager.permission.API_V23（dangerous 權限，
#      server 啟動時會依 runtime permission 狀態寫回授權 config → 免在眼鏡 UI 點允許）
#   3) 啟動 server：adb shell <nativeLibraryDir>/libshizuku.so（v13.6.0 起無 start.sh；
#      starter 要求執行身份為 root(0) 或 shell(2000)，adb shell = 2000 ✓）
#
# 用法：
#   bash deploy/start_shizuku.sh
#   可覆寫：ADB=/path/to/adb  SERIAL=<DEVICE_SERIAL>  MANAGER_APK=/path/to/shizuku-v13.x.x.apk
#
# 注意：Shizuku server 不跨 reboot；每次開機後（含 exploit kernel-panic 重啟後）都要重跑一次。
# 之後眼鏡 App 即可自行以 shell 身份跑 exploit（ShizukuGhostlockEngine），
# Shizuku 不可用時 App 會自動回退 bridge helper。
source "$(dirname "$0")/env.sh"   # 共用 ADB/SERIAL（見 deploy/env.sh）
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
# 授權 config 由 server 啟動時依 runtime permission 重建；此處 grant 即足。
echo "  （若顯示 Unknown permission 屬正常：manager 首次啟動後再重跑一次即可）"

echo "[3/4] 推導 libshizuku.so 並啟動 server"
LIB_SO="${APK_PATH%/base.apk}/lib/arm64/libshizuku.so"
# 安裝後路徑可能變動，重新取一次
APK_PATH=$(adb_s shell pm path "$MANAGER_PKG" 2>/dev/null | tr -d '\r' | sed -n 's/^package://p' | head -1)
LIB_SO="${APK_PATH%/base.apk}/lib/arm64/libshizuku.so"
echo "  lib: $LIB_SO"
if ! adb_s shell "[ -x $LIB_SO ]" 2>/dev/null; then
  echo "  [!] $LIB_SO 不存在（ABI 非 arm64？）。列出可用 lib："
  adb_s shell "ls \$(dirname $LIB_SO) 2>/dev/null"
  exit 1
fi
# 先殺舊 server（starter 也會做，雙保險）
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
