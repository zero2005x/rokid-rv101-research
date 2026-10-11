#!/usr/bin/env bash
# 共用環境設定：**唯一**的 adb 路徑與裝置序號來源。
#
# 為什麼需要：原本 10 支腳本各自寫死 `/mnt/c/Users/<user>/AppData/.../adb.exe` 與
# 眼鏡序號 `<DEVICE_SERIAL>`，換電腦或換裝置就得逐檔修改（見 scripts/audit_hardcode.py）。
#
# 用法（在腳本開頭）：
#   source "$(dirname "$0")/env.sh"
# 可用環境變數覆寫：ADB、SERIAL、WIN_ADB_DIR
set -u

# Windows 端 adb（WSL）；可用 ADB 環境變數覆寫
WIN_ADB_DIR="${WIN_ADB_DIR:-/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools}"
ADB="${ADB:-$WIN_ADB_DIR/adb.exe}"

# 裝置序號：預設抓 adb devices 的第一台，可用 SERIAL 覆寫
if [ -z "${SERIAL:-}" ]; then
  SERIAL="$("$ADB" devices 2>/dev/null | awk 'NR>1 && $2=="device" {print $1; exit}')"
fi
SERIAL="${SERIAL:-<DEVICE_SERIAL>}"   # 最後手段：已知眼鏡序號

# 常用路徑
export ADB SERIAL WIN_ADB_DIR
