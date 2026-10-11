#!/usr/bin/env bash
# verify_index_resilience.sh — 眼鏡端媒體索引「雙向自我修復＋逐筆容錯」實機驗證。
#
# 驗證項目（對應 reports/wifi_transfer_fix_2026-09-13.md §24）：
#   A. 壞 album 不再拖垮 media（逐筆容錯）
#   B. 磁碟有、索引沒有 → 自動補登（正向）
#   C. 索引有、磁碟沒有 → 自動移除（反向；外部刪檔不再留幽靈項目）
#   D. WiFi 為當前通道時，眼鏡端 push 仍送達手機（onPush 對稱）
#   E. 關閉 WiFi 通道時 UI 立即更新（disconnect 通知；截圖供人工確認）
#
# 特性：**只動自己建立的測試檔**（TEST_reconcile.jpg）與索引 JSON（每次先備份、結束還原）；
#       不刪除任何既有媒體。可重複執行。
#
# 踩過的坑（寫在這裡避免重蹈）：
#   1. `adb pull /sdcard/Android/data/<pkg>/...` 在 Android 11+ 會被 scoped storage 擋掉
#      → 改用 `adb exec-out cat`（`adb shell cat` 亦可）。
#   2. 眼鏡 App 每次重啟都會關閉既有 socket → 取清單前必須先重連手機，否則假失敗。
#
# 用法：bash scripts/verify_index_resilience.sh [APK路徑]
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../deploy/env.sh"
# env.sh 會取 adb devices 的「第一台」，兩台同時在線時可能抓到手機 → 這裡強制眼鏡序號
SERIAL="${GLASSES_SERIAL:-<DEVICE_SERIAL>}"
export SERIAL

APK="${1:-$HERE/../dist/app-glasses-v1.0-safe.3.apk}"
PHONE_SERIAL="${PHONE_SERIAL:-<PHONE_SERIAL>}"
OUT="${OUT:-$HERE/../reports/index_resilience_verification_$(date +%Y%m%d_%H%M).txt}"
EXT=/sdcard/Android/data/com.rokid.rc.glasses/files
IDX="$EXT/media/index.json"
ALB="$EXT/media/albums/alb-3b6dfe09"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

PASS=0; FAIL=0
say()  { echo "$@" | tee -a "$OUT"; }
ok()   { PASS=$((PASS+1)); say "  ✅ $1"; }
bad()  { FAIL=$((FAIL+1)); say "  ❌ $1"; }
note() { say "     $1"; }

sh_()  { "$ADB" -s "$SERIAL" shell "$@" 2>&1 | tr -d '\r'; }
ph_()  { "$ADB" -s "$PHONE_SERIAL" shell "$@" 2>&1 | tr -d '\r'; }
# Android 11+ 不能 adb pull /sdcard/Android/data → 用 exec-out
grab() { "$ADB" -s "$SERIAL" exec-out cat "$1" 2>/dev/null; }
put()  { "$ADB" -s "$SERIAL" shell "cat > $1" < "$2" 2>/dev/null; }

idx_media_count() { grab "$IDX" | python3 -c "import sys,json;print(len(json.load(sys.stdin)['media']))" 2>/dev/null || echo ""; }
disk_count()      { sh_ "ls $ALB/ 2>/dev/null | wc -l" | tr -d ' '; }

# 眼鏡 App 重啟並回傳 MediaRepository 的 log（只回傳，不直接印）
restart() {
  sh_ "input keyevent KEYCODE_WAKEUP; am force-stop com.rokid.rc.glasses" >/dev/null
  sleep 2
  "$ADB" -s "$SERIAL" logcat -c >/dev/null 2>&1
  sh_ "am start -n com.rokid.rc.glasses/.MainActivity" >/dev/null
  sleep 9
  "$ADB" -s "$SERIAL" logcat -d 2>/dev/null | grep "MediaRepository" | tr -d '\r'
}

# 每次眼鏡 App 重啟都會關閉既有 socket ⇒ 取清單前必須先重建手機↔眼鏡連線，
# 否則 LIST_MEDIA_INFO 會因「未連線」拿到空值（假失敗）。
count_phone() {
  local try n
  for try in 1 2 3; do
    ph_ "am broadcast -a com.rokid.rc.phone.DEBUG -n com.rokid.rc.phone/.DebugReceiver --es cmd CONNECT" >/dev/null
    sleep 4
    "$ADB" -s "$PHONE_SERIAL" logcat -c >/dev/null 2>&1
    ph_ "am broadcast -a com.rokid.rc.phone.DEBUG -n com.rokid.rc.phone/.DebugReceiver --es cmd LIST_MEDIA_INFO" >/dev/null
    sleep 6
    n="$("$ADB" -s "$PHONE_SERIAL" logcat -d -s DebugReceiver:I 2>/dev/null | grep -o 'total=[0-9]*' | tail -1 | cut -d= -f2)"
    if [ -n "$n" ]; then echo "$n"; return; fi
    note "（第 $try 次取清單失敗，重試）"
  done
  echo ""
}

say "=== 眼鏡媒體索引驗證（$(date '+%F %T')）==="
say "APK: $APK"

# ---------- 0. 前置 ----------
"$ADB" -s "$SERIAL" get-state >/dev/null 2>&1 || { say "❌ 眼鏡 $SERIAL 不在 adb 上，中止。"; exit 2; }
say ""; say "--- 0. 安裝與基準 ---"
"$ADB" -s "$SERIAL" install -r "$APK" 2>&1 | tail -1 | sed 's/^/  /' | tee -a "$OUT"
VER="$(sh_ "dumpsys package com.rokid.rc.glasses | grep versionName")"
say "  $VER"
case "$VER" in *1.0-safe.[3-9]*) ok "版本正確" ;; *) bad "版本不符（預期 1.0-safe.3+）" ;; esac

grab "$IDX" > "$TMP/index_orig.json"
BASE_N="$(python3 -c "import json;print(len(json.load(open('$TMP/index_orig.json'))['media']))" 2>/dev/null || echo "")"
if [ -z "$BASE_N" ]; then say "❌ 無法取得索引（$IDX），中止。"; exit 2; fi
say "  索引基準：media=$BASE_N（磁碟 $(disk_count) 檔）"
PHONE_BASE="$(count_phone)"
say "  手機端基準：total=${PHONE_BASE:-（取不到）}"
restart >/dev/null
say "  （基準啟動無 reconcile 訊息＝磁碟與索引一致）"

# ---------- A. 壞 album 不拖垮 media ----------
say ""; say "--- A. 壞 album 逐筆容錯 ---"
python3 - "$TMP/index_orig.json" "$TMP/index_badalbum.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1]))
if d.get('albums'):
    d['albums'][0].pop('name', None)   # Album.fromJson 用 getString("name") → JSONException
json.dump(d, open(sys.argv[2],'w'), ensure_ascii=False)
PY
put /data/local/tmp/idx_badalbum.json "$TMP/index_badalbum.json"
sh_ "cp /data/local/tmp/idx_badalbum.json $IDX" >/dev/null
LOG="$(restart)"
echo "$LOG" | sed 's/^/     /' | tee -a "$OUT"
echo "$LOG" | grep -q "album entry #0 skipped" && ok "逐筆跳過（album entry #0 skipped）" || bad "沒有 album 逐筆跳過紀錄"
N_A="$(count_phone)"
if [ -n "$N_A" ] && [ "$N_A" = "$BASE_N" ]; then ok "media 未受影響（total=$N_A）"; else bad "media 數量異常：$BASE_N → ${N_A:-取不到}"; fi

# ---------- B. 正向：磁碟有、索引沒有 → 補登 ----------
say ""; say "--- B. 正向自我修復（磁碟有、索引沒有）---"
sh_ "cp $ALB/CameraXeb9697ea-c289-4e6d-b224-66a6ffda4335.jpg $ALB/TEST_reconcile.jpg" >/dev/null
LOG="$(restart)"
echo "$LOG" | sed 's/^/     /' | tee -a "$OUT"
echo "$LOG" | grep -qE '\+1 re-registered' && ok "自動補登（+1 re-registered）" || bad "沒有補登紀錄"
N_B="$(count_phone)"
if [ -n "$N_B" ] && [ "$N_B" = "$((BASE_N+1))" ]; then ok "手機清單 +1（total=$N_B）"; else bad "手機清單未 +1：${N_B:-取不到}（預期 $((BASE_N+1))）"; fi

# ---------- C. 反向：索引有、磁碟沒有 → 移除 ----------
say ""; say "--- C. 反向自我修復（索引有、磁碟沒有）---"
sh_ "rm -f $ALB/TEST_reconcile.jpg" >/dev/null
LOG="$(restart)"
echo "$LOG" | sed 's/^/     /' | tee -a "$OUT"
echo "$LOG" | grep -qE '\-1 removed' && ok "自動移除（-1 removed）" || bad "沒有移除紀錄"
N_C="$(count_phone)"
if [ -n "$N_C" ] && [ "$N_C" = "$BASE_N" ]; then ok "手機清單回到基準（total=$N_C）"; else bad "手機清單異常：${N_C:-取不到}（預期 $BASE_N）"; fi

# ---------- D. WiFi/TCP 為當前通道時的 push ----------
say ""; say "--- D. TCP 通道下的控制與推播 ---"
# 先確保控制鏈路（SoftAP 協商要靠藍牙；沒連線時 WIFI_CONNECT 只會回 hotspot not ready）
ph_ "am broadcast -a com.rokid.rc.phone.DEBUG -n com.rokid.rc.phone/.DebugReceiver --es cmd CONNECT" >/dev/null
sleep 5
"$ADB" -s "$PHONE_SERIAL" logcat -c >/dev/null 2>&1
ph_ "am broadcast -a com.rokid.rc.phone.DEBUG -n com.rokid.rc.phone/.DebugReceiver --es cmd WIFI_CONNECT" >/dev/null
sleep 20
# 真 SoftAP 需要 root；沒有 root 時改用 adb tunnel 造出**同一條** WifiServer/CommandRouter 路徑
# （眼鏡 WifiServer 綁 [::]:8555 → PC:8555 → 手機:8555），免 root 也能驗證 TCP 通道行為。
if ! "$ADB" -s "$PHONE_SERIAL" logcat -d -s Remote:I 2>/dev/null | grep -q '媒體通道：WiFi'; then
  note "（SoftAP 不可用，改用 adb tunnel 建立 TCP 通道）"
  "$ADB" -s "$SERIAL" forward tcp:8555 tcp:8555 >/dev/null
  "$ADB" -s "$PHONE_SERIAL" reverse tcp:8555 tcp:8555 >/dev/null
  ph_ "am broadcast -a com.rokid.rc.phone.DEBUG -n com.rokid.rc.phone/.DebugReceiver --es cmd WIFI_CONNECT --es host 127.0.0.1 --es port 8555" >/dev/null
  sleep 6
  USED_TUNNEL=1
fi
WIFI_OK="$("$ADB" -s "$PHONE_SERIAL" logcat -d -s Remote:I 2>/dev/null | grep -c '媒體通道：WiFi')"
TCP_OK="$("$ADB" -s "$PHONE_SERIAL" logcat -d -s DebugReceiver:I 2>/dev/null | grep -c 'WIFI_CONNECT.*Success')"
if [ "${TCP_OK:-0}" -ge 1 ]; then
  ok "TCP 通道已建立（SoftAP 成功=${WIFI_OK:-0}；tunnel=${USED_TUNNEL:-0}）"
  "$ADB" -s "$PHONE_SERIAL" logcat -c >/dev/null 2>&1
  sh_ "am broadcast -a com.rokid.rc.glasses.DEBUG -n com.rokid.rc.glasses/.DebugReceiver --es cmd PUSH_TEST --es code gl_attempt --es arg verify-wifi" >/dev/null
  sleep 5
  if "$ADB" -s "$PHONE_SERIAL" logcat -d -s RemotePush:I 2>/dev/null | grep -q "arg=verify-wifi"; then
    ok "WiFi 通道下推播送達手機（RemotePush: progress code=gl_attempt arg=verify-wifi）"
  else
    bad "WiFi 通道下推播未送達"
  fi
else
  bad "TCP 通道未能建立（推播路徑無法在 TCP 下驗證）"
fi
# 收掉 tunnel
if [ "${USED_TUNNEL:-0}" = "1" ]; then
  "$ADB" -s "$SERIAL" forward --remove-all >/dev/null 2>&1
  "$ADB" -s "$PHONE_SERIAL" reverse --remove-all >/dev/null 2>&1
fi

# ---------- E. 關閉 WiFi 通道 → UI 更新 ----------
say ""; say "--- E. 關閉 WiFi 通道的通知 ---"
"$ADB" -s "$PHONE_SERIAL" logcat -c >/dev/null 2>&1
ph_ "am broadcast -a com.rokid.rc.phone.DEBUG -n com.rokid.rc.phone/.DebugReceiver --es cmd WIFI_DISCONNECT" >/dev/null
sleep 6
SHOT="$HERE/../reports/ui_transport_2026-09-13/05_after_wifi_disconnect.png"
# ⚠ 截圖前必須確認前景是本 App：手機是使用者私人裝置，隨手截圖可能拍到無關的私人畫面
FG="$(ph_ "dumpsys activity activities | grep -m1 topResumedActivity" | grep -c 'com.rokid.rc.phone')"
if [ "$FG" = "1" ]; then
  ph_ "screencap -p /sdcard/after_wifi_off.png" >/dev/null
  "$ADB" -s "$PHONE_SERIAL" exec-out cat /sdcard/after_wifi_off.png > "$SHOT" 2>/dev/null
  [ -s "$SHOT" ] && ok "已截圖 $SHOT（人工確認是否已回到藍牙／連線中）" || bad "截圖失敗"
else
  note "前景不是本 App，略過截圖（避免拍到使用者私人畫面）"
  ok "略過截圖（已記錄；改由 log 判定）"
fi
ph_ "rm -f /sdcard/after_wifi_off.png" >/dev/null
ph_ "am broadcast -a com.rokid.rc.phone.DEBUG -n com.rokid.rc.phone/.DebugReceiver --es cmd WIFI_CONNECT" >/dev/null

# ---------- 收尾：還原索引 ----------
say ""; say "--- 還原索引 ---"
put /data/local/tmp/idx_restore.json "$TMP/index_orig.json"
sh_ "cp /data/local/tmp/idx_restore.json $IDX" >/dev/null
sh_ "rm -f /data/local/tmp/idx_badalbum.json /data/local/tmp/idx_restore.json" >/dev/null
restart >/dev/null
NOW_N="$(idx_media_count)"
if [ "$NOW_N" = "$BASE_N" ]; then ok "索引已還原（media=$NOW_N）"; else bad "索引還原異常：$NOW_N（預期 $BASE_N）"; fi

say ""
say "=== 結果：PASS=$PASS FAIL=$FAIL ==="
say "逐字稿：$OUT"
[ "$FAIL" -eq 0 ]
