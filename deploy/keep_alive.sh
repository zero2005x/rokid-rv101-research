#!/usr/bin/env bash
source "$(dirname "$0")/env.sh"   # shared ADB/SERIAL (see deploy/env.sh)
set -u

# ============ adjust to your environment ============
SERIAL="${SERIAL:?請設定 SERIAL 或讓 env.sh 自動偵測}"
WAKE_INTERVAL=3          # wake the screen every 3 seconds
CHECK_EVERY=10           # check liveness every 10 iterations (~30 s)
# =======================================

echo "keep-alive 啟動：每 ${WAKE_INTERVAL}s 喚醒螢幕、每 $((WAKE_INTERVAL*CHECK_EVERY))s 檢查 app/helper 存活"

i=0
while true; do
  i=$((i+1))

  # wake the screen (keep Rokid's 5-second sleep from killing the app)
  "$ADB" -s "$SERIAL" shell 'input keyevent KEYCODE_WAKEUP' >/dev/null 2>&1

  # periodic check + restart
  if [ $((i % CHECK_EVERY)) -eq 0 ]; then
    if ! "$ADB" -s "$SERIAL" shell 'ps -A -o ARGS 2>/dev/null | grep -q com.rokid.rc.glasses' 2>/dev/null; then
      echo "[$(date +%T)] 眼鏡 app 死亡，重啟"
      "$ADB" -s "$SERIAL" shell 'am start -n com.rokid.rc.glasses/.MainActivity' >/dev/null 2>&1
    fi
    if ! "$ADB" -s "$SERIAL" shell 'ps -A -o ARGS 2>/dev/null | grep -q ghostlock_bridge_helper' 2>/dev/null; then
      echo "[$(date +%T)] helper 死亡，重啟"
      "$ADB" -s "$SERIAL" shell 'setsid sh /data/local/tmp/ghostlock_bridge_helper.sh >/data/local/tmp/ghostlock_bridge_helper.log 2>&1 </dev/null &' >/dev/null 2>&1
    fi
  fi

  sleep "$WAKE_INTERVAL"
done
