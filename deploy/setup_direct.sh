#!/usr/bin/env bash
# =============================================================================
# setup_direct.sh — 一次性設定：讓眼鏡 App「直連提權」，不再需要 shell helper。
#
# 原理（symlink trick）：
#   untrusted_app 原本寫不了 /data/local/tmp/.ghostlock_root.sh（目錄 0771，others 無 w）。
#   若把 .ghostlock_root.sh 設成 symlink → /data/local/tmp/ghostlock_bridge/.ghostlock_root.sh
#   （bridge 目錄 chmod 777），則 exploit 與 App 讀寫 .ghostlock_root.sh 時都會 follow symlink
#   寫入 777 目錄，App 就能自行完成「等 ready → 置換 payload → 等 done」。
#
# ⚠ 2026-09-13：DIRECT（ShellGhostlockEngine 直連）**已不使用**——實測 untrusted_app 跑 exploit
#   會 kernel panic，且引擎改為執行期自動探測（不再有 USE_BRIDGE 開關）。本腳本僅保留供研究。
# 之後 reboot 後 symlink 仍在（/data/local/tmp 是持久檔案系統），App 開機即可自行提權，
# 不需要桌機 adb。
# =============================================================================
source "$(dirname "$0")/env.sh"   # 共用 ADB/SERIAL（見 deploy/env.sh）
set -u

BRIDGE="/data/local/tmp/ghostlock_bridge"
ROOT_SCRIPT="/data/local/tmp/.ghostlock_root.sh"

adb_s() { "$ADB" -s "$SERIAL" "$@"; }

echo "[1/4] 建立 777 bridge 目錄"
adb_s shell "mkdir -p $BRIDGE && chmod 777 $BRIDGE" || exit 1

echo "[2/4] 建立 .ghostlock_root.sh symlink → bridge 目錄"
adb_s shell "rm -f $ROOT_SCRIPT && ln -s $BRIDGE/.ghostlock_root.sh $ROOT_SCRIPT" || exit 1

echo "[3/4] 確認 exploit 與 root_daemon 就緒"
if ! adb_s shell "[ -x /data/local/tmp/ghostlock_rv101 ]" 2>/dev/null; then
    echo "  [!] /data/local/tmp/ghostlock_rv101 不存在，請先 push"
    exit 1
fi
if [ -f "/home/kali/rokid_root/deploy/root_daemon" ]; then
    adb_s push /home/kali/rokid_root/deploy/root_daemon /data/local/tmp/root_daemon >/dev/null 2>&1
    adb_s shell "chmod 755 /data/local/tmp/root_daemon"
fi

echo "[4/4] 驗證 symlink"
adb_s shell "ls -la $ROOT_SCRIPT; ls -la $BRIDGE/"

echo ""
echo "完成（研究用途）。注意：DIRECT 路徑會 kernel panic，正式流程請用 root_bridge.sh 或 start_shizuku.sh。"
echo "之後重開機，App 會由 BootReceiver 啟動並自行提權（若 rootMode=AUTO）。"
