#!/usr/bin/env bash
# install_root_keepalive.sh — 安裝 root daemon 存活看守（keepalive）＋ OOM 保護。
#
# 為什麼需要獨立一支：提權可能由**兩條路徑**完成——
#   (a) App 內引擎（payload 由 LedPayloads.startRootDaemon 產生，已自帶 keepalive）
#   (b) deploy/get_root_now.sh（走 bridge helper，payload 是 exploit 預設那 13 行）
# 路徑 (b) 不會安裝 keepalive → daemon 掛掉就沒有 root。本腳本補上，
# 且**任何時候已取得 root 都能執行**（用 root daemon socket 下指令，不需重跑 exploit）。
#
# 注意：若 keepalive 檔已由 App 以 root 建立，shell 無法覆寫（Permission denied）
#      → 先用 root daemon 刪除，再以 shell 寫入，最後用 root daemon 啟動。
#
# 用法：SERIAL=<序號> bash deploy/install_root_keepalive.sh
set -u
source "$(dirname "$0")/env.sh"   # 共用 ADB/SERIAL（見 deploy/env.sh）

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
# root daemon 存活看守：daemon 崩潰／被回收時自動重建（不需 adb、不需重跑 exploit），
# 並持續把 daemon 與自己的 oom_score_adj 壓到 -1000（最不易被 lmkd 回收）。
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
  # pid 檔可能殘留舊值 → 由 ps 反查真實 PID（keepalive 以 root 執行，看得到全部程序）
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
# 由 root daemon 執行：確認/補啟動 keepalive，並重設 pid 檔與 OOM 分數
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
