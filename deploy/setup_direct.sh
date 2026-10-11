#!/usr/bin/env bash
# =============================================================================
# setup_direct.sh — one-time setup: let the glasses App escalate directly, without a shell helper.
#
# How (symlink trick):
#   untrusted_app cannot write /data/local/tmp/.ghostlock_root.sh (directory 0771, others
#   have no w). If .ghostlock_root.sh is a symlink to
#   /data/local/tmp/ghostlock_bridge/.ghostlock_root.sh (bridge directory chmod 777), then the
#   exploit and the App both follow the symlink into the 777 directory when reading and
#   writing .ghostlock_root.sh, so the App can do "wait ready -> swap payload -> wait done"
#   on its own.
#
# ⚠ 2026-09-13: DIRECT (ShellGhostlockEngine direct) is **no longer used** — untrusted_app
#   running the exploit was measured to kernel-panic, and the engine now probes at runtime
#   (there is no USE_BRIDGE switch any more). This script is kept for research only.
# The symlink survives a reboot (/data/local/tmp is a persistent filesystem), so after boot
# the App can escalate on its own without a desktop adb.
# =============================================================================
source "$(dirname "$0")/env.sh"   # shared ADB/SERIAL (see deploy/env.sh)
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
