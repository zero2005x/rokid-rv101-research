#!/usr/bin/env bash
# install_root_keepalive.sh — install the root daemon keepalive and OOM protection.
#
# Why a separate script: escalation can happen through **two paths** —
#   (a) the in-App engine (payload produced by LedPayloads.startRootDaemon, keepalive included)
#   (b) deploy/get_root_now.sh (via the bridge helper, with the exploit's default 13-line payload)
# Path (b) installs no keepalive -> once the daemon dies there is no root. This script fills
# that gap and **can be run whenever root is already held** (commands go through the root daemon
# socket, no need to rerun the exploit).
#
# NOTE: if the keepalive file was created by the App as root, shell cannot overwrite it
#      (Permission denied) -> delete it with the root daemon first, write it as shell, then
#      start it with the root daemon.
#
# Usage: SERIAL=<serial> bash deploy/install_root_keepalive.sh
set -u
source "$(dirname "$0")/env.sh"   # shared ADB/SERIAL (see deploy/env.sh)

SOCK=/data/local/tmp/root_daemon.sock
KA=/data/local/tmp/root_daemon_keepalive.sh
PID=/data/local/tmp/root_daemon.pid
KAPID=/data/local/tmp/root_daemon_keepalive.pid
START=/data/local/tmp/ka_start.sh

adbs() { "$ADB" -s "$SERIAL" "$@"; }
root_exec() { adbs shell "printf '%s\n' \"$1\" | timeout 8 nc -U $SOCK" 2>/dev/null; }

echo "[0/4] 檢查 root"
if ! root_exec 'id' | grep -q 'uid=0'; then
  echo "✗ root daemon 不可用（先跑 deploy/get_root_now.sh 或 root_bridge.sh）"
  exit 1
fi

echo "[1/4] 清除舊檔（由 root 執行；shell 無法覆寫 root 檔案）"
root_exec "rm -f $KA $KAPID $START"
root_exec "test -f $PID || rm -f $PID"

echo "[2/4] 寫入 keepalive 腳本"
adbs shell "cat > $KA" <<'KEEPALIVE'
#!/system/bin/sh
# root daemon keepalive: rebuild the daemon automatically when it crashes or is reclaimed
# (no adb and no exploit rerun needed), and keep pinning the daemon's and its own
# oom_score_adj to -1000 (least likely to be reclaimed by lmkd).
PID=/data/local/tmp/root_daemon.pid
SOCK=/data/local/tmp/root_daemon.sock
BIN=/data/local/tmp/root_daemon
LOG=/data/local/tmp/root_daemon.log
while true; do
  if ! grep -q 'root_daemon.sock' /proc/net/unix 2>/dev/null; then
    rm -f "$SOCK"
    setsid "$BIN" >>"$LOG" 2>&1 &
    echo $! > "$PID"
    echo "$(date '+%F %T') keepalive: daemon restarted pid=$(cat $PID 2>/dev/null)" >> "$LOG"
  fi
  # the pid file may hold a stale value -> look the real PID up with ps (keepalive runs as root and sees every process)
  ACTUAL=$(ps -A -o PID,ARGS 2>/dev/null | grep '[r]oot_daemon$' | awk '{print $1}' | head -1)
  [ -n "$ACTUAL" ] && echo "$ACTUAL" > "$PID"
  for p in $(cat "$PID" 2>/dev/null) $$; do
    echo -1000 > /proc/$p/oom_score_adj 2>/dev/null
  done
  sleep 20
done
KEEPALIVE

echo "[3/4] 寫入啟動器並以 root 執行（同時修掉 pid 檔為實際值）"
adbs shell "cat > $START" <<'STARTEOF'
#!/system/bin/sh
# Run by the root daemon: verify/start the keepalive and reset the pid files and OOM scores
if ! grep -q 'root_daemon.sock' /proc/net/unix 2>/dev/null; then
  rm -f /data/local/tmp/root_daemon.sock
  setsid /data/local/tmp/root_daemon >>/data/local/tmp/root_daemon.log 2>&1 &
  echo $! > /data/local/tmp/root_daemon.pid
fi
if ! ps -A -o ARGS 2>/dev/null | grep -q '[r]oot_daemon_keepalive.sh'; then
  setsid sh /data/local/tmp/root_daemon_keepalive.sh >/dev/null 2>&1 &
  echo $! > /data/local/tmp/root_daemon_keepalive.pid
fi
ACTUAL=$(ps -A -o PID,ARGS 2>/dev/null | grep '[r]oot_daemon$' | awk '{print $1}' | head -1)
[ -n "$ACTUAL" ] && echo "$ACTUAL" > /data/local/tmp/root_daemon.pid
for p in $(cat /data/local/tmp/root_daemon.pid 2>/dev/null) $(cat /data/local/tmp/root_daemon_keepalive.pid 2>/dev/null); do
  echo -1000 > /proc/$p/oom_score_adj 2>/dev/null
done
echo "started daemon=$(cat /data/local/tmp/root_daemon.pid 2>/dev/null) keepalive=$(cat /data/local/tmp/root_daemon_keepalive.pid 2>/dev/null)"
STARTEOF

adbs shell "chmod 755 $KA $START"
root_exec "sh $START"

sleep 3
echo "[4/4] 驗證"
adbs shell "ps -A -o PID,ARGS 2>/dev/null | grep -E '[r]oot_daemon|[k]eepalive' | head -3"
adbs shell "for p in \$(cat $PID 2>/dev/null) \$(cat $KAPID 2>/dev/null); do printf 'oom_score_adj(%s)=' \$p; cat /proc/\$p/oom_score_adj 2>/dev/null || echo '?'; done"
echo "完成。daemon 崩潰時 keepalive 會在 20 秒內自動重建（可用 kill -9 驗證）。"
