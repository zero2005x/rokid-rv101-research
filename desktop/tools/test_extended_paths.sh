#!/usr/bin/env bash
# test_extended_paths.sh — 驗證三條先前未測的路徑
#
#   1. Shizuku 引擎 root 路徑   —— 桌面工具功能（OP_SHIZUKU → do_shizuku()）
#   2. DebugReceiver 廣播       —— 眼鏡 App 的除錯介面（桌面工具透過「root 指令」欄驅動）
#   3. WiFi 媒體通道（眼鏡端）  —— 手機↔眼鏡功能，桌面工具只能驗證眼鏡端狀態
#
# 用法：  ./test_extended_paths.sh [序號]
# 結束碼：0 = 全部通過，1 = 有項目失敗，2 = 裝置未連線
#
# 注意：adb 一定要直接執行。透過 PowerShell 包裝呼叫 adb 實測會逾時
#       （曾因此誤判裝置離線），故本腳本以 bash 直接呼叫 adb.exe。
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

# 所有 adb 呼叫都在這裡統一加上 -s 與逾時
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
# 注意：必須比對「輸出」而不是 sh_ 的退出碼 —— sh_ 是管線，退出碼幾乎永遠是 0，
# 用它會讓未 root 的裝置被誤判成已 root。
#
# 注意 2（2026-09-13 實測修正）：**不可要求剛好等於 1**。
# /proc/net/unix 對同一個 socket 會有「listening」與「已連線」兩種 entry，
# 只要有客戶端連著（眼鏡 App 的 RootDaemonKeeper 每 30 秒探一次、keepalive、
# 或 adb 的 nc），計數就會變成 2 → 舊寫法 `= "1"` 會把**已 root 的裝置誤判成未 root**。
# 實測 10 次取樣中就出現過一次 count=2，屬間歇性偽陰性。改判 `>= 1`。
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

  # libshizuku.so 必須存在且可執行，do_shizuku() 靠這個路徑直接啟動
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
# 讓眼鏡 App 進入「前景」。
#
# 為什麼不能只下 am start-service：
#   am start-service        → Error: app is in background ... idle change:cached
#   am start-foreground-service → Error: Not found; no service started.
# 兩者都會被 Android 的背景執行限制擋下。CaptureService 是在
# MainActivity.onResume() 裡才 startForegroundService 的（原始碼註解明寫
# 「activity 進入前景後才啟動 FGS，避免 Background start not allowed」），
# 所以正確做法是把 MainActivity 帶到前景，讓它自己拉起服務。
# ------------------------------------------------------------------
ensure_app_foreground() {
  if [ "$(sh_ "ps -A -o ARGS 2>/dev/null | grep -c '$PKG_GLASSES'")" -gt 0 ] &&
     sh_ "dumpsys activity activities 2>/dev/null | grep -q 'topResumedActivity.*$PKG_GLASSES'"; then
    return 0
  fi
  sh_ 'input keyevent KEYCODE_WAKEUP' >/dev/null 2>&1   # 頭盔螢幕可能睡著
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
  # 關鍵：眼鏡 App 若沒在執行，Android 會以「背景執行限制」拒收廣播 ——
  #   W BroadcastQueue: Background execution Third-Party APP not allowed:
  #     receiving Intent { act=com.rokid.rc.glasses.DEBUG ... }
  # 此時 am broadcast 仍回報 "Broadcast completed"，但接收器根本沒被叫起來，
  # logcat 自然什麼都沒有。所以必須先確保 App 程序活著。
  # ------------------------------------------------------------------
  ensure_app_foreground
  if [ "$(sh_ "ps -A -o ARGS 2>/dev/null | grep -c '$PKG_GLASSES'")" -gt 0 ]; then
    ok "眼鏡 App 程序在執行（廣播才收得到）"
  else
    no "眼鏡 App 拉不起來，DebugReceiver 廣播會被背景限制擋掉"
  fi

  # 用「明確指定 component」送廣播，避免被背景限制擋掉
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

  # 還原：SET_LOCALE 不帶 lang = 跟隨系統；SET_DISPLAY 全 true = 預設
  sh_ "$BCAST --es cmd SET_LOCALE"       >/dev/null 2>&1
  sh_ "$BCAST --es cmd SET_DISPLAY --ez conn true --ez root true --ez camera true --ez storage true --ez led true" >/dev/null 2>&1

  # logcat 緩衝有延遲，重試幾次再判定，避免偽陰性
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
    # panel toggled (activity=false) 代表 MainActivity 沒在前面，屬於預期行為
    printf '%s' "$LOGS" | grep -q 'activity=false' && \
      printf '      （activity=false：MainActivity 未前景化，屬預期）\n'
  else
    no "logcat 沒有 TOGGLE_PANEL 的紀錄"
  fi

  # 若仍無紀錄，檢查是否又是背景執行限制
  if ! printf '%s' "$LOGS" | grep -q 'display set'; then
    if sh_ "logcat -d 2>/dev/null | grep -q 'Background execution Third-Party APP not allowed.*rokid.rc.glasses'"; then
      printf '      ↳ 偵測到背景執行限制擋下廣播（App 未執行）\n'
    fi
  fi
fi

# ------------------------------------------------- 3. WiFi 媒體通道（眼鏡端）
head_ "3. WiFi 媒體通道（眼鏡端，可完整驗證）"
printf '  來源依據：WifiHotspot.kt 註解「實測 adb shell(uid 2000) 也可執行 start-softap」，\n'
printf '            CaptureService 需由 MainActivity 進前景後自行拉起（背景限制）。\n'
printf '            因此這條路徑不需要手機在場也能完整驗證。\n'

WIFI_DID=0
# 註：cmd wifi start-softap 由 adb shell(uid 2000) 執行即可，不需要 root
#     （見 WifiHotspot.kt 註解），所以本段不因未 root 而略過。
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

  # 3b. SoftAP（root；本韌體 STA 與 SoftAP 不可同時維持）
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

  # 3c. 功能驗證：實際連線，確認 server 接受
  if [ -n "$APIP" ] && [ "$(sh_ "netstat -tln 2>/dev/null | grep -c ':$WIFI_PORT '")" -gt 0 ]; then
    sh_ "logcat -c" >/dev/null 2>&1
    # 注意：Android 的 sh 是 mksh，不支援 bash 的 /dev/tcp；要用 toybox 的 nc。
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

  # 3d. 還原：SoftAP 不跨 reboot，且開著會踢掉 STA，測完關閉
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

# ------------------------------------------------------------------ 總結
head_ "結果"
printf '  通過 %d  失敗 %d  略過 %d\n' "$pass" "$fail" "$skip"
[ "$fail" -eq 0 ] && exit 0 || exit 1
