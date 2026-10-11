#!/system/bin/sh
# root_restore_delayed.sh — 延遲 N 秒後把 root daemon 與 keepalive 重新拉起。
#
# ## 為什麼需要
#
# 有些測試必須讓「root 對 App 而言不可用」（例如驗證 RootDaemonKeeper 的租約守衛：
# 守衛程式碼在 `if (client.isRoot()) return` **之後**才會被執行）。
#
# 但 root daemon 一旦被殺，**只有 root 身分才能把它重建回來** —— 也就是說，
# 少了這支腳本，測試完就只能重跑一次 exploit（慢、且對核心狀態敏感）。
#
# 作法：在殺掉 daemon **之前**，先經 root socket 以 root 身分啟動本腳本。
# 它是一個獨立的 detach 行程，daemon 死了它還活著，時間到就把 daemon 與 keepalive 帶回來。
#
# ## 用法（必須經 root socket 啟動，否則沒有重建 daemon 的權限）
#
#   printf 'setsid sh /data/local/tmp/root_restore_delayed.sh 150 >/dev/null 2>&1 &\n' \
#     | adb shell "nc -U /data/local/tmp/root_daemon.sock"
#
# 第一個參數＝延遲秒數（預設 90）。請留足夠時間給測試，並記得先確認它真的在跑
# （`ps -A -o ARGS | grep '[r]oot_restore_delayed'`）再殺 daemon。
set -u

DELAY="${1:-90}"
DAEMON=/data/local/tmp/root_daemon
KA=/data/local/tmp/root_daemon_keepalive.sh
SOCK=/data/local/tmp/root_daemon.sock
LOG=/data/local/tmp/root_daemon.log

echo "$(date '+%F %T') restorer: armed (delay=${DELAY}s pid=$$)" >> "$LOG"
sleep "$DELAY"
echo "$(date '+%F %T') restorer: waking up" >> "$LOG"

# 只在真的沒有 daemon 時才重建——避免蓋掉測試期間已由其他路徑恢復的實例
if ! grep -q 'root_daemon.sock' /proc/net/unix 2>/dev/null; then
  rm -f "$SOCK"
  setsid "$DAEMON" >>"$LOG" 2>&1 &
  echo "$(date '+%F %T') restorer: daemon restarted pid=$!" >> "$LOG"
else
  echo "$(date '+%F %T') restorer: daemon already present, skip" >> "$LOG"
fi

sleep 3

# keepalive 若不在，重新拉起（同樣必須是 root 身分，才能持續重建 daemon）
if ! ps -A -o ARGS 2>/dev/null | grep -q '[r]oot_daemon_keepalive.sh'; then
  setsid sh "$KA" >>"$LOG" 2>&1 &
  echo "$(date '+%F %T') restorer: keepalive restarted" >> "$LOG"
fi

echo "$(date '+%F %T') restorer: done" >> "$LOG"
