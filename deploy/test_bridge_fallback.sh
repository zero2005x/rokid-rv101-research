#!/usr/bin/env bash
# test_bridge_fallback.sh — 驗證 Shizuku 不可用時自動回退 bridge helper 提權。
#
# 前置：殺 Shizuku server（模擬不可用）+ 殺 root_daemon（逼 App 需要新 root）
#       → deploy/root_bridge.sh 佈署 helper → App(AUTO) 觸發 acquireRoot
#       → 引擎 preflight 失敗 → fallback → BridgeGhostlockEngine → helper 跑 exploit。
# 成功判定：root socket 回 uid=0，且 bridge last.out=OK（證明走 bridge 而非 Shizuku）。
source "$(dirname "$0")/env.sh"   # 共用 ADB/SERIAL（見 deploy/env.sh）
set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

adb_s() { "$ADB" -s "$SERIAL" "$@"; }
device_alive() { adb_s get-state 2>/dev/null | grep -q device; }
wait_device() { for _ in $(seq 1 60); do if device_alive; then sleep 8; return 0; fi; sleep 5; done; return 1; }
check_root() {
  local out
  out=$(adb_s shell "echo id | nc -U /data/local/tmp/root_daemon.sock 2>/dev/null" 2>/dev/null | tr -d '\r')
  case "$out" in *"uid=0"*) return 0 ;; *) return 1 ;; esac
}

echo "===== 前置：Shizuku down + root_daemon down + helper up（乾淨狀態）====="
# 注意：pkill -f 會自我匹配（adb shell 自身 cmdline 含 pattern），改用
# 字元類別 regex（"shizuku_serve[r]"）避免自殺，或 pidof+kill。
adb_s shell "pkill -9 -f 'shizuku_serve[r]' 2>/dev/null; true"
adb_s shell "for p in \$(pidof root_daemon); do kill -9 \$p 2>/dev/null; done; rm -f /data/local/tmp/root_daemon.sock; true"
adb_s shell "pkill -9 -f 'ghostlock_bridge_helpe[r]' 2>/dev/null; true"
adb_s shell "rm -f /data/local/tmp/ghostlock_bridge/last.cmd /data/local/tmp/ghostlock_bridge/last.out /data/local/tmp/ghostlock_bridge/last.progress /data/local/tmp/ghostlock_bridge/last.done 2>/dev/null; true"
sleep 2
echo "  shizuku_server: $(adb_s shell pidof shizuku_server 2>/dev/null | tr -d '\r' || echo '(none)')"
echo "  root_daemon:    $(adb_s shell pidof root_daemon 2>/dev/null | tr -d '\r' || echo '(none)')"

for attempt in 1 2 3; do
  echo "===== bridge fallback attempt $attempt ====="
  if ! device_alive; then wait_device || exit 1; fi
  # reboot 後 helper 會消失，重佈署
  bash "$ROOT_DIR/deploy/root_bridge.sh" 2>&1 | grep -E "^\[|完成" | tail -4
  adb_s shell am force-stop com.rokid.rc.glasses 2>/dev/null
  adb_s logcat -c 2>/dev/null
  adb_s shell "am start -n com.rokid.rc.glasses/.MainActivity" >/dev/null 2>&1
  echo "  [poll] 最多 420s..."
  deadline=$(( $(date +%s) + 420 ))
  while [ "$(date +%s)" -lt "$deadline" ]; do
    if ! device_alive; then
      echo "  [panic] reboot 中，等待回來"
      wait_device && echo "  [device] back" || exit 1
      break
    fi
    if check_root; then
      echo "BRIDGE_FALLBACK_OK attempt=$attempt"
      echo "  [bridge last.out] $(adb_s shell cat /data/local/tmp/ghostlock_bridge/last.out 2>/dev/null | tr -d '\r')"
      echo "  [bridge progress] $(adb_s shell cat /data/local/tmp/ghostlock_bridge/last.progress 2>/dev/null | tr -d '\r')"
      echo "  [verify] $(adb_s shell 'echo id | nc -U /data/local/tmp/root_daemon.sock; getenforce' 2>/dev/null | tr -d '\r' | tr '\n' ' ')"
      exit 0
    fi
    sleep 10
  done
  echo "  [attempt-end] $(adb_s shell 'uptime; getenforce' 2>/dev/null | tr -d '\r' | tr '\n' ' ')"
done
echo "BRIDGE_FALLBACK_ALL_FAILED"
exit 1
