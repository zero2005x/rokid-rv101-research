#!/usr/bin/env bash
source "$(dirname "$0")/env.sh"   # 共用 ADB/SERIAL（見 deploy/env.sh）
set -u

# ============ 依你的環境調整 ============
SERIAL="${SERIAL:?請設定 SERIAL 或讓 env.sh 自動偵測}"
WAKE_INTERVAL=3          # 每 3 秒喚醒一次螢幕
CHECK_EVERY=10           # 每 10 次(約 30 秒)檢查存活
# =======================================

echo "keep-alive 啟動：每 ${WAKE_INTERVAL}s 喚醒螢幕、每 $((WAKE_INTERVAL*CHECK_EVERY))s 檢查 app/helper 存活"

i=0
while true; do
  i=$((i+1))

  # 喚醒螢幕（防止 Rokid 5 秒休眠殺 app）
  "$ADB" -s "$SERIAL" shell 'input keyevent KEYCODE_WAKEUP' >/dev/null 2>&1

  # 定期檢查 + 重啟
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
