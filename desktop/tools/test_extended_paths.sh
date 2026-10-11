#!/usr/bin/env bash
# test_extended_paths.sh — verify three previously untested paths
#
#   1. Shizuku engine root path  -- desktop tool feature (OP_SHIZUKU -> do_shizuku())
#   2. DebugReceiver broadcast   -- the glasses app's debug interface (driven from the desktop
#                                   tool's "root command" field)
#   3. WiFi media channel (glasses side) -- a phone<->glasses feature; the desktop tool can
#                                   only verify the glasses-side state
#
# Usage:  ./test_extended_paths.sh [serial]
# Exit codes: 0 = all passed, 1 = some item failed, 2 = device not connected
#
# NOTE: adb must be executed directly. Calling adb through a PowerShell wrapper was measured
#       to time out (which once made the device look offline), so this script calls adb.exe
#       straight from bash.
set -u

SER="${1:-<DEVICE_SERIAL>}"
ADB="${ADB:-/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
SHIZUKU_APK="${SHIZUKU_APK:-/home/kali/rokid_root/research/shizuku/shizuku-manager-v13.6.0.apk}"
PKG_GLASSES="com.rokid.rc.glasses"
PKG_SHIZUKU="moe.shizuku.privileged.api"
PERM_SHIZUKU="moe.shizuku.manager.permission.API_V23"
WIFI_PORT=8555

pass=0; fail=0; skip=0
ok()   { printf '  \033[32m✓\033[0m %s\n' "$1"; pass=$((pass+1)); }
no()   { printf '  \033[31m✗\033[0m %s\n' "$1"; fail=$((fail+1)); }
sk()   { printf '  \033[33m–\033[0m %s\n' "$1"; skip=$((skip+1)); }
head_() { printf '\n\033[1m== %s ==\033[0m\n' "$1"; }

# every adb call goes through here to get -s and a timeout
sh_() { timeout 45 "$ADB" -s "$SER" shell "$1" 2>/dev/null | tr -d '\r'; }
adbx() { timeout 45 "$ADB" -s "$SER" "$@" 2>&1 | tr -d '\r'; }

head_ "0. 前置檢查"
if ! timeout 30 "$ADB" devices 2>/dev/null | grep -q "^${SER}[[:space:]]*device"; then
  if timeout 30 "$ADB" devices 2>/dev/null | grep -q "${SER}"; then
    no "裝置在清單中但不是 device 狀態（未授權／offline）"; exit 2
  fi
  no "裝置 $SER 未連線"; exit 2
fi
ok "裝置已連線：$SER"

SELINUX=$(sh_ 'getenforce')
ROOTED=0
# NOTE: compare the "output", not sh_'s exit code — sh_ is a pipeline, so its exit code is
# almost always 0, and using it would report a non-rooted device as rooted.
#
# NOTE 2 (measured fix, 2026-09-13): **do not require exactly 1**.
# /proc/net/unix has both a "listening" and a "connected" entry for the same socket, so as soon
# as a client is attached (the glasses app's RootDaemonKeeper probes every 30 s, keepalive, or
# adb's nc) the count becomes 2 -> the old `= "1"` reported a **rooted device as not rooted**.
# One count=2 appeared in 10 measured samples: an intermittent false negative. Test `>= 1`.
if [ "$(sh_ 'grep -c root_daemon.sock /proc/net/unix; true')" -ge 1 ] 2>/dev/null; then ROOTED=1; fi
if [ "$ROOTED" = 1 ] && [ "$SELINUX" = "Permissive" ]; then
  ok "已 root（SELinux=$SELINUX，root_daemon socket 存在）"
else
  no "尚未 root（SELinux=$SELINUX）—— 請先跑「一鍵取得 root」"
fi

FW=$(sh_ 'getprop ro.build.version.incremental')
printf '  韌體 %s\n' "$FW"

# ---------------------------------------------------------------- 1. Shizuku
head_ "1. Shizuku 引擎 root 路徑"
if [ "$ROOTED" != 1 ]; then
  sk "需要 root，略過"
else
  APKPATH=$(sh_ "pm path $PKG_SHIZUKU")
  if [ -n "$APKPATH" ]; then
    ok "Shizuku manager 已安裝：${APKPATH#package:}"
  else
    sk "Shizuku manager 未安裝（do_shizuku() 會嘗試以 settings.ini 的 apk 安裝）"
    printf '      APK：%s\n' "$SHIZUKU_APK"
    [ -f "$SHIZUKU_APK" ] && ok "本機找得到 APK" || no "本機找不到 APK"
  fi

  # libshizuku.so must exist and be executable; do_shizuku() launches it from this path
  if [ -n "$APKPATH" ]; then
    LIB=$(printf '%s' "${APKPATH#package:}" | sed 's#base\.apk$#lib/arm64/libshizuku.so#')
    if [ "$(sh_ "[ -x '$LIB' ] && echo yes || echo no")" = "yes" ]; then
      ok "libshizuku.so 存在且可執行"
    else
      no "libshizuku.so 不存在或不可執行：$LIB"
    fi
    G=$(sh_ "dumpsys package $PKG_GLASSES 2>/dev/null | grep -c '$PERM_SHIZUKU'")
    [ "${G:-0}" -gt 0 ] && ok "已授權 $PERM_SHIZUKU 給 $PKG_GLASSES" \
                        || sk "尚未授權 $PERM_SHIZUKU（按「啟動」時才會授權）"
  fi

  N=$(sh_ 'ps -A -o ARGS 2>/dev/null | grep -c shizuku_serve[r]')
  if [ "${N:-0}" -gt 0 ]; then
    ok "shizuku_server 已在執行（$N 個程序）"
  else
    sk "shizuku_server 未執行 —— 請在工具卡按 Shizuku 的「啟動」"
  fi
fi

# ------------------------------------------------------------------
# Bring the glasses app to the foreground.
#
# Why am start-service alone is not enough:
#   am start-service        → Error: app is in background ... idle change:cached
#   am start-foreground-service → Error: Not found; no service started.
# Both are blocked by Android's background execution limits. CaptureService is only started with
# startForegroundService from MainActivity.onResume() (the source comment says outright "start
# the FGS only after the activity reaches the foreground, to avoid Background start not
# allowed"), so the right approach is to bring MainActivity to the foreground and let it start
# the service itself.
# ------------------------------------------------------------------
ensure_app_foreground() {
  if [ "$(sh_ "ps -A -o ARGS 2>/dev/null | grep -c '$PKG_GLASSES'")" -gt 0 ] &&
     sh_ "dumpsys activity activities 2>/dev/null | grep -q 'topResumedActivity.*$PKG_GLASSES'"; then
    return 0
  fi
  sh_ 'input keyevent KEYCODE_WAKEUP' >/dev/null 2>&1   # the headset screen may be asleep
  sleep 1
  sh_ "am start -W -n $PKG_GLASSES/.MainActivity" >/dev/null 2>&1
  for _ in 1 2 3 4 5 6 7 8; do
    sleep 3
    sh_ "dumpsys activity activities 2>/dev/null | grep -q 'topResumedActivity.*$PKG_GLASSES'" && break
  done
  return 0
}

# ------------------------------------------------------- 2. DebugReceiver
head_ "2. DebugReceiver 廣播（眼鏡端 $PKG_GLASSES）"
INST=$(sh_ "pm list packages 2>/dev/null | grep -c '^package:$PKG_GLASSES\$'")
if [ "${INST:-0}" -eq 0 ]; then
  no "$PKG_GLASSES 未安裝 —— 無法測試 DebugReceiver"
else
  ok "$PKG_GLASSES 已安裝"
  RCV=$(sh_ "dumpsys package $PKG_GLASSES 2>/dev/null | grep -c 'DebugReceiver'")
  [ "${RCV:-0}" -gt 0 ] && ok "manifest 中註冊了 DebugReceiver" \
                        || no "manifest 中找不到 DebugReceiver"

  # ------------------------------------------------------------------
  # Key point: if the glasses app is not running, Android rejects the broadcast under its
  # "background execution limits" —
  #   W BroadcastQueue: Background execution Third-Party APP not allowed:
  #     receiving Intent { act=com.rokid.rc.glasses.DEBUG ... }
  # am broadcast still reports "Broadcast completed", but the receiver is never invoked, so
  # logcat naturally shows nothing. The app process must therefore be alive first.
  # ------------------------------------------------------------------
  ensure_app_foreground
  if [ "$(sh_ "ps -A -o ARGS 2>/dev/null | grep -c '$PKG_GLASSES'")" -gt 0 ]; then
    ok "眼鏡 App 程序在執行（廣播才收得到）"
  else
    no "眼鏡 App 拉不起來，DebugReceiver 廣播會被背景限制擋掉"
  fi

  # send the broadcast with an explicit component so background limits cannot drop it
  BCAST="am broadcast -n $PKG_GLASSES/.DebugReceiver -a $PKG_GLASSES.DEBUG"
  sh_ "logcat -c" >/dev/null 2>&1

  for CMD in TOGGLE_PANEL SET_DISPLAY SET_LOCALE; do
    OUT=$(sh_ "$BCAST --es cmd $CMD")
    if printf '%s' "$OUT" | grep -qi 'Broadcast completed'; then
      ok "cmd=$CMD 廣播已送出"
    else
      no "cmd=$CMD 廣播失敗：$(printf '%s' "$OUT" | head -1)"
    fi
  done

  # restore: SET_LOCALE without lang = follow the system; SET_DISPLAY all true = defaults
  sh_ "$BCAST --es cmd SET_LOCALE"       >/dev/null 2>&1
  sh_ "$BCAST --es cmd SET_DISPLAY --ez conn true --ez root true --ez camera true --ez storage true --ez led true" >/dev/null 2>&1

  # the logcat buffer lags; retry a few times before judging, to avoid false negatives
  LOGS=""
  for _ in 1 2 3 4 5; do
    sleep 3
    LOGS=$(sh_ "logcat -d -s GlassesDebugReceiver 2>/dev/null | tail -20")
    printf '%s' "$LOGS" | grep -q 'display set' && break
  done
  if printf '%s' "$LOGS" | grep -q 'locale set'; then ok "logcat 確認 SET_LOCALE 生效"; else no "logcat 沒有 SET_LOCALE 的紀錄"; fi
  if printf '%s' "$LOGS" | grep -q 'display set'; then ok "logcat 確認 SET_DISPLAY 生效"; else no "logcat 沒有 SET_DISPLAY 的紀錄"; fi
  if printf '%s' "$LOGS" | grep -q 'panel toggled'; then
    ok "logcat 確認 TOGGLE_PANEL 生效"
    # panel toggled (activity=false) means MainActivity was not in front; that is expected
    printf '%s' "$LOGS" | grep -q 'activity=false' && \
      printf '      （activity=false：MainActivity 未前景化，屬預期）\n'
  else
    no "logcat 沒有 TOGGLE_PANEL 的紀錄"
  fi

  # if there is still no record, check whether background limits blocked it again
  if ! printf '%s' "$LOGS" | grep -q 'display set'; then
    if sh_ "logcat -d 2>/dev/null | grep -q 'Background execution Third-Party APP not allowed.*rokid.rc.glasses'"; then
      printf '      ↳ 偵測到背景執行限制擋下廣播（App 未執行）\n'
    fi
  fi
fi

# ------------------------------------------------- 3. WiFi media channel (glasses side)
head_ "3. WiFi 媒體通道（眼鏡端，可完整驗證）"
printf '  來源依據：WifiHotspot.kt 註解「實測 adb shell(uid 2000) 也可執行 start-softap」，\n'
printf '            CaptureService 需由 MainActivity 進前景後自行拉起（背景限制）。\n'
printf '            因此這條路徑不需要手機在場也能完整驗證。\n'

WIFI_DID=0
# NOTE: cmd wifi start-softap works from adb shell (uid 2000) and needs no root (see the
#     WifiHotspot.kt comment), so this section is not skipped when unrooted.
  ensure_app_foreground
  APPUP=$(sh_ "ps -A -o ARGS 2>/dev/null | grep -c '$PKG_GLASSES'")
  [ "${APPUP:-0}" -gt 0 ] && ok "眼鏡 App 已在前景執行" || sk "眼鏡 App 拉不起來"

  # 3a. CaptureService → WifiServer(TCP 8555)
  if [ "$(sh_ "netstat -tln 2>/dev/null | grep -c ':$WIFI_PORT '")" -gt 0 ]; then
    ok "TCP $WIFI_PORT 已在監聽"
  else
    ensure_app_foreground
    for _ in 1 2 3 4 5 6; do
      sleep 5
      [ "$(sh_ "netstat -tln 2>/dev/null | grep -c ':$WIFI_PORT '")" -gt 0 ] && break
    done
    if [ "$(sh_ "netstat -tln 2>/dev/null | grep -c ':$WIFI_PORT '")" -gt 0 ]; then
      ok "TCP $WIFI_PORT 已啟動（App 進前景後由 CaptureService 拉起）"
    else
      no "TCP $WIFI_PORT 仍未監聽"
    fi
  fi

  # 3b. SoftAP (root; this firmware cannot keep STA and SoftAP up at the same time)
  APIP=$(sh_ 'ip -o addr show 2>/dev/null | grep -o "192\.168\.43\.[0-9]*" | head -1')
  if [ -z "$APIP" ]; then
    sh_ 'cmd wifi start-softap RokidCam wpa2 rokid1234 >/dev/null 2>&1 &' >/dev/null 2>&1
    WIFI_DID=1
    for _ in $(seq 1 9); do
      sleep 10
      APIP=$(sh_ 'ip -o addr show 2>/dev/null | grep -o "192\.168\.43\.[0-9]*" | head -1')
      [ -n "$APIP" ] && break
    done
  fi
  if [ -n "$APIP" ]; then
    ok "SoftAP 已啟動，眼鏡位址 $APIP（應為 192.168.43.1）"
  else
    no "SoftAP 90 秒內未起來"
  fi

  # 3c. functional check: actually connect and confirm the server accepts
  if [ -n "$APIP" ] && [ "$(sh_ "netstat -tln 2>/dev/null | grep -c ':$WIFI_PORT '")" -gt 0 ]; then
    sh_ "logcat -c" >/dev/null 2>&1
    # NOTE: Android's sh is mksh and does not support bash's /dev/tcp; use toybox's nc.
    C=$(sh_ "echo '' | timeout 5 nc -w 3 $APIP $WIFI_PORT >/dev/null 2>&1 && echo OK || echo FAIL")
    sleep 3
    if printf '%s' "$C" | grep -q OK; then
      ok "TCP 連線成功：$APIP:$WIFI_PORT"
    else
      no "TCP 連線失敗：$APIP:$WIFI_PORT"
    fi
    if sh_ "logcat -d -s WifiServer 2>/dev/null | grep -q 'connection accepted'"; then
      ok "WifiServer logcat 確認接受連線"
    else
      sk "logcat 未見 'connection accepted'（可能已輪替）"
    fi
  fi

  # 3d. restore: SoftAP does not survive a reboot and drops STA while up, so turn it off after
  # the test
  if [ "$WIFI_DID" = 1 ] || [ -n "$APIP" ]; then
    sh_ 'cmd wifi stop-softap >/dev/null 2>&1; true' >/dev/null 2>&1
    sleep 3
    if [ -z "$(sh_ 'ip -o addr show 2>/dev/null | grep -o "192\.168\.43\.[0-9]*" | head -1')" ]; then
      ok "已還原：SoftAP 關閉"
    else
      sk "SoftAP 仍在（可手動 cmd wifi stop-softap 關閉）"
    fi
  fi
  printf '  已知限制：本韌體 STA 與 SoftAP 不可同時維持，開熱點會踢掉既有 WiFi STA。\n'

# ------------------------------------------------------------------ summary
head_ "結果"
printf '  通過 %d  失敗 %d  略過 %d\n' "$pass" "$fail" "$skip"
[ "$fail" -eq 0 ] && exit 0 || exit 1
