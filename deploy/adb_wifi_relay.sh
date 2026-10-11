#!/usr/bin/env bash
# adb_wifi_relay.sh — 在裝置上開一個「無線 adb 入口」：TCP → adbd local socket
#
# 原理：裝置的 adbd 聽在 UNIX socket /dev/socket/adbd（USB 只是它的另一個 transport）。
#       本腳本在裝置端跑 deploy/adb_tcp_relay（TCP listener），把每條 TCP 連線原封不動
#       轉給 adbd → PC/手機/筆電即可用標準 `adb connect <ip>:5555` 連上，
#       認證/shell/sync/forward 全部由原廠 adbd 處理。
#
# 為什麼需要 root：/dev/socket/adbd 只允許 adbd 自身 domain 與 root 連（SELinux）；
#       shell(2000) 連會被拒（實測 Permission denied）。取得 root 後（GhostLock）即可。
#
# 用法：
#   source deploy/env.sh
#   bash deploy/adb_wifi_relay.sh start [PORT]     # 預設 5555
#   bash deploy/adb_wifi_relay.sh stop
#   bash deploy/adb_wifi_relay.sh status
#   bash deploy/adb_wifi_relay.sh probe            # 只做唯讀可行性檢查
set -u
source "$(dirname "$0")/env.sh"

ACTION="${1:-status}"
PORT="${2:-5555}"
ROOT=/data/local/tmp
BIN="$ROOT/adb_tcp_relay"
LOG="$ROOT/adb_relay.log"
PIDF="$ROOT/adb_relay.pid"
SOCK=/data/local/tmp/root_daemon.sock

adbs() { "$ADB" -s "$SERIAL" "$@"; }

# 以 root 執行（走既有 root_daemon socket；沒有就退回 su -c）
root_exec() {
  local cmd="$1"
  if adbs shell "grep -q root_daemon.sock /proc/net/unix 2>/dev/null" | grep -q .; then
    printf '%s\n' "$cmd" | adbs shell "timeout 15 nc -U $SOCK" 2>/dev/null
  else
    adbs shell "su -c \"$cmd\"" 2>/dev/null
  fi
}

has_root() { root_exec 'id' | grep -q 'uid=0'; }

port_hex() { printf ':%04X' "$1"; }

case "$ACTION" in
probe)
  echo "=== 唯讀可行性檢查（SERIAL=$SERIAL） ==="
  adbs shell 'printf "android=%s model=%s\n" "$(getprop ro.build.version.release)" "$(getprop ro.product.model)"
    echo "--- /dev/socket/adbd ---"; ls -la /dev/socket/adbd 2>&1
    echo "--- /proc/net/unix 中的 adbd ---"; grep -i adbd /proc/net/unix 2>/dev/null || echo "(無)"
    echo "--- adbd 行程 ---"; ps -A -o USER,PID,ARGS 2>/dev/null | grep "[a]dbd" | head -3
    echo "--- adb 屬性 ---"; for p in service.adb.tcp.port persist.adb.tcp.port ro.debuggable ro.adb.secure; do printf "%-24s=%s\n" "$p" "$(getprop $p)"; done'
  echo "--- root 狀態 ---"; has_root && echo "root: OK" || echo "root: 不可用（先跑 deploy/root_bridge.sh 或 get_root_now.sh）"
  ;;

start)
  echo "[1/4] 檢查 root"
  has_root || { echo "✗ 需要 root（/dev/socket/adbd 對 shell 是 Permission denied）"; exit 1; }
  echo "  root OK"

  echo "[2/4] 確認 adbd local socket 存在"
  adbs shell 'ls -la /dev/socket/adbd 2>&1; grep -ci adbd /proc/net/unix 2>/dev/null' | tail -2

  echo "[3/4] 推入 relay 二進位"
  [ -f build-cache/adb_tcp_relay ] || {
    echo "  建置中…"; bash "$(dirname "$0")/build_adb_relay.sh"; }
  adbs push build-cache/adb_tcp_relay "$BIN" >/dev/null 2>&1 || {
    echo "✗ push 失敗"; exit 1; }
  adbs shell "chmod 755 $BIN"

  echo "[4/4] 以 root 啟動 relay :$PORT"
  root_exec "pkill -f adb_tcp_relay 2>/dev/null; sleep 0.3; setsid $BIN -p $PORT -l $LOG </dev/null >/dev/null 2>&1 &"
  sleep 2
  root_exec "cat $LOG 2>/dev/null | tail -3"
  if adbs shell "cat /proc/net/tcp /proc/net/tcp6 2>/dev/null | awk '{print \$2}' | grep -qi '$(port_hex "$PORT")'" | grep -q .; then
    IP="$(adbs shell 'ip -4 -o addr show 2>/dev/null | grep -oE "inet 192\.168\.[0-9]+\.[0-9]+" | head -1 | cut -d" " -f2')"
    echo "✅ relay 已監聽 TCP :$PORT"
    echo "   眼鏡 IP（目前介面）：${IP:-<無 192.168.x.x 介面>}"
    echo "   PC 連線：adb connect ${IP:-<眼鏡IP>}:$PORT"
  else
    echo "✗ 沒有 listener；看 $LOG"; root_exec "cat $LOG 2>/dev/null | tail -5"
    exit 1
  fi
  ;;

stop)
  root_exec 'pkill -f adb_tcp_relay; echo stopped'
  ;;

status)
  echo "=== relay 狀態（SERIAL=$SERIAL） ==="
  adbs shell 'ps -A -o PID,USER,ARGS 2>/dev/null | grep "[a]db_tcp_relay" | head -3 || echo "(未執行)"
    echo "--- 監聽埠 ---"
    cat /proc/net/tcp /proc/net/tcp6 2>/dev/null | awk "\$4==\"0A\" {print \$2}" | head -10
    echo "--- log ---"; cat /data/local/tmp/adb_relay.log 2>/dev/null | tail -5'
  ;;
*)
  echo "用法: $0 {start|stop|status|probe} [PORT]"; exit 2;;
esac
