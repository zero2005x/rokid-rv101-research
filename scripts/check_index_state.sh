#!/usr/bin/env bash
# check_index_state.sh — check the glasses media index state and repair traces an interrupted
# verification may have left.
#
# Background: step A of `verify_index_resilience.sh` **deliberately** removes `albums[0].name`
# (to test per-entry fault tolerance). If adb drops mid-run, the modified index can stay on the
# device. Because the app's `reconcileWithDisk()` **rebuilds** albums and media from disk on the
# next start (only adding, never removing), no data is lost, but **the album name becomes the
# directory id** (`alb-3b6dfe09` instead of `Camera`). This script checks for that and restores the
# name.
#
# Usage: bash scripts/check_index_state.sh [--fix]
#   without --fix: check and print the state only (read-only)
#   with --fix: if an abnormal album name is detected, write the correct name back and restart
#   the app to verify
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../deploy/env.sh"
SERIAL="${GLASSES_SERIAL:-<DEVICE_SERIAL>}"
PHONE_SERIAL="${PHONE_SERIAL:-<PHONE_SERIAL>}"
export SERIAL

FIX=0
[ "${1:-}" = "--fix" ] && FIX=1

IDX=/sdcard/Android/data/com.rokid.rc.glasses/files/media/index.json
ALB_DIR=/sdcard/Android/data/com.rokid.rc.glasses/files/media/albums
DEFAULT_NAME="Camera"
OUT="$HERE/../reports/index_state_check_$(date +%Y%m%d_%H%M).txt"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
say() { echo "$@" | tee -a "$OUT"; }

sh_() { "$ADB" -s "$SERIAL" shell "$@" 2>&1 | tr -d '\r'; }
ph_() { "$ADB" -s "$PHONE_SERIAL" shell "$@" 2>&1 | tr -d '\r'; }

say "=== 索引狀態檢查（$(date '+%F %T')）==="
"$ADB" -s "$SERIAL" get-state >/dev/null 2>&1 || { say "❌ 眼鏡不在 adb 上"; exit 2; }

"$ADB" -s "$SERIAL" exec-out cat "$IDX" > "$TMP/idx.json" 2>/dev/null
if ! python3 -c "import json;json.load(open('$TMP/idx.json'))" 2>/dev/null; then
  say "⚠ 索引無法解析（可能是被中斷的半截檔）——啟動 App 會由 reconcileWithDisk() 從磁碟重建"
  IDX_OK=0
else
  IDX_OK=1
fi

if [ "$IDX_OK" = "1" ]; then
  python3 - "$TMP/idx.json" "$TMP/report.txt" "$DEFAULT_NAME" <<'PY' | tee -a "$OUT"
import json,sys
d=json.load(open(sys.argv[1]))
out=[]
bad=[]
for a in d.get('albums',[]):
    out.append(f"  album id={a['id']} name={a.get('name','<缺 name>')}")
    if a.get('name') in (None,'',a['id']):
        bad.append(a['id'])
out.append(f"  media 筆數={len(d['media'])}")
open(sys.argv[2],'w').write(json.dumps(bad))
print("\n".join(out))
PY
  BAD="$(cat "$TMP/report.txt")"
  DISK="$(sh_ "ls $ALB_DIR/alb-3b6dfe09/ | wc -l" | tr -d ' ')"
  say "  磁碟檔數=$DISK"
  if [ "$BAD" != "[]" ] && [ "$BAD" != "" ]; then
    say "⚠ 偵測到專輯名稱異常（$BAD）＝ 驗證中斷留下的痕跡（名稱變成目錄 id）"
    if [ "$FIX" = "1" ]; then
      python3 - "$TMP/idx.json" "$TMP/idx_fixed.json" "$DEFAULT_NAME" <<'PY'
import json,sys
d=json.load(open(sys.argv[1]))
for a in d.get('albums',[]):
    if a.get('name') in (None,'',a['id']):
        a['name']=sys.argv[3]
json.dump(d, open(sys.argv[2],'w'), ensure_ascii=False)
PY
      "$ADB" -s "$SERIAL" shell "cat > /data/local/tmp/idx_fixed.json" < "$TMP/idx_fixed.json"
      sh_ "cp /data/local/tmp/idx_fixed.json $IDX; rm -f /data/local/tmp/idx_fixed.json" >/dev/null
      say "  ✅ 已把專輯名稱修回「$DEFAULT_NAME」"
    else
      say "  （未加 --fix，僅檢查）"
    fi
  else
    say "  ✅ 專輯名稱正常（未被驗證流程改動）"
  fi
fi

# restart the app so reconcile runs once, then confirm the three sides agree
sh_ "input keyevent KEYCODE_WAKEUP; am force-stop com.rokid.rc.glasses" >/dev/null
sleep 2
"$ADB" -s "$SERIAL" logcat -c >/dev/null 2>&1
sh_ "am start -n com.rokid.rc.glasses/.MainActivity" >/dev/null
sleep 9
LOG="$("$ADB" -s "$SERIAL" logcat -d 2>/dev/null | grep MediaRepository | tr -d '\r')"
[ -n "$LOG" ] && say "$LOG" | sed 's/^/  /'
"$ADB" -s "$SERIAL" exec-out cat "$IDX" > "$TMP/idx2.json" 2>/dev/null
IDXN="$(python3 -c "import json;print(len(json.load(open('$TMP/idx2.json'))['media']))" 2>/dev/null || echo '?')"
DISK="$(sh_ "ls $ALB_DIR/alb-3b6dfe09/ | wc -l" | tr -d ' ')"
ph_ "am broadcast -a com.rokid.rc.phone.DEBUG -n com.rokid.rc.phone/.DebugReceiver --es cmd CONNECT" >/dev/null
sleep 4
"$ADB" -s "$PHONE_SERIAL" logcat -c >/dev/null 2>&1
ph_ "am broadcast -a com.rokid.rc.phone.DEBUG -n com.rokid.rc.phone/.DebugReceiver --es cmd LIST_MEDIA_INFO" >/dev/null
sleep 6
PN="$("$ADB" -s "$PHONE_SERIAL" logcat -d -s DebugReceiver:I 2>/dev/null | grep -o 'total=[0-9]*' | tail -1 | cut -d= -f2)"
say ""
say "三方一致性：索引=$IDXN  磁碟=$DISK  手機=${PN:-取不到}"
if [ "$IDXN" = "$DISK" ] && { [ -z "$PN" ] || [ "$PN" = "$IDXN" ]; }; then
  say "✅ 一致"
else
  say "⚠ 不一致（請看上方 reconcile 訊息）"
fi
say "逐字稿：$OUT"
