#!/usr/bin/env bash
# usbdebug_hider_ctl.sh — control script for the USB-debug anti-detection module (a homegrown LSPosed/Vector module)
#
# Companion report: reports/usbdebug_antidetect_module_2026-09-30.md
# Module source: android/usbdebug-hider/ (Java hook + native PLT/GOT hook)
# Control-group detector: android/debugdetectprobe/
#
# One-time prerequisites (install the framework, two reboots needed; done on 2026-09-30 on this Redmi Note 11 Pro+ 5G):
#   1) apd module install NeoZygisk-v2.4-289-release.zip   → reboot
#   2) apd module install Vector-v2.2-3080-Release.zip      → reboot
#   framework zips live in tools/framework/
#
# Usage:
#   ./deploy/usbdebug_hider_ctl.sh <command> [args]
#
#   build              compile the native layer + both APKs
#   install            install the module and probe APKs, push the config, enable the module
#   scope-all          set scope to all third-party apps (excluding the module itself and the APatch manager)
#   scope-set PKG...   inject only the given apps (allowlist)
#   scope-from-file F  set scope from a list file (e.g. deploy/usbdebug_hider_scope.txt)
#   scope-list         list the current scope
#   conf               push deploy/usbdebug_hider.conf to the device
#   enable / disable   enable / disable the module
#   status             framework, module, scope, config and the device's real state
#   verify             run the control-group detector and list masked and real values
#   panic              rescue: disable every APatch module (for a boot loop)
#   uninstall          remove the module and probe APKs (framework kept)
#   remove-all         full removal: apps + framework modules (NeoZygisk/Vector) + temp files, then reboot to verify
set -u

source "$(dirname "$0")/env.sh"   # ADB / WIN_ADB_DIR / SERIAL (glasses)

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
MODULE_PKG="com.rokid.rc.usbdebughider"
PROBE_PKG="com.rokid.rc.debugdetectprobe"
CLI="/data/adb/lspd/cli"
CONF="/data/local/tmp/usbdebug_hider.conf"
STAGE="${STAGE:-/mnt/c/Users/<USER>/rokid_stage}"
HIDER_APK="$ROOT_DIR/android/usbdebug-hider/build/outputs/apk/debug/usbdebug-hider-debug.apk"
PROBE_APK="$ROOT_DIR/android/debugdetectprobe/build/outputs/apk/debug/debugdetectprobe-debug.apk"

# phone serial: prefer PHONE_SERIAL, otherwise pick a device that is "not the glasses"
pick_device() {
  if [ -n "${PHONE_SERIAL:-}" ]; then
    echo "$PHONE_SERIAL"
    return
  fi
  local d model
  for d in $("$ADB" devices 2>/dev/null | tr -d '\r' | awk 'NR>1 && $2=="device" {print $1}'); do
    model="$("$ADB" -s "$d" shell getprop ro.product.model 2>/dev/null | tr -d '\r')"
    # exclude Rokid glasses (models like RG-glasses / RV101 ...)
    case "$model" in
      *[Gg]lasses*|*Rokid*|*ROKID*|*RV101*|*RG-*) continue ;;
    esac
    echo "$d"
    return
  done
  # Never fall back to "any device": on 2026-09-30 that sent commands to the glasses by
  # mistake (plus one extra reboot). If no phone is found, return an empty string and let
  # need_device fail loudly.
  echo ""
}

SERIAL_PHONE="$(pick_device)"
A() { "$ADB" -s "$SERIAL_PHONE" "$@"; }
SU() { A shell "su -c '$*'"; }
CLI_() { SU "$CLI $*"; }

# WSL path -> Windows path (adb.exe cannot read the Linux form of /home/... or /mnt/c/...)
winpath() {
  case "$1" in
    /mnt/[a-z]/*) echo "$(echo "${1#/mnt/}" | cut -c1 | tr 'a-z' 'A-Z'):$(echo "${1#/mnt/?}" | tr '/' '\\')" ;;
    *) echo "$1" ;;
  esac
}

need_device() {
  if [ -z "$SERIAL_PHONE" ]; then
    echo "找不到手機裝置 —— 本腳本只對手機作用，不會回退到眼鏡。" >&2
    echo "請接上手機，或用 PHONE_SERIAL=<serial> 明確指定。目前 adb devices：" >&2
    "$ADB" devices | tr -d '\r' >&2
    exit 1
  fi
  # safety check: refuse to run if the serial is the glasses'
  if [ "$SERIAL_PHONE" = "${SERIAL:-}" ]; then
    echo "拒絕執行：挑到的序號與眼鏡（$SERIAL）相同。" >&2
    exit 1
  fi
  # safety check: that serial must really be online with state device
  local st
  st="$("$ADB" -s "$SERIAL_PHONE" get-state 2>/dev/null | tr -d '\r')"
  if [ "$st" != "device" ]; then
    echo "拒絕執行：裝置 $SERIAL_PHONE 不在線（state='${st:-none}'）。" >&2
    "$ADB" devices | tr -d '\r' >&2
    exit 1
  fi
}

require_apks() {
  [ -f "$HIDER_APK" ] || { echo "缺 $HIDER_APK，請先跑：$0 build" >&2; exit 1; }
  [ -f "$PROBE_APK" ] || { echo "缺 $PROBE_APK，請先跑：$0 build" >&2; exit 1; }
}

cmd_build() {
  "$ROOT_DIR/deploy/build_debugmask_so.sh"
  ( cd "$ROOT_DIR/android" \
    && JAVA_HOME=/home/kali/rokid_root/tools/jdk/jdk-17.0.20.1+1 \
       GRADLE_USER_HOME="$ROOT_DIR/gradle_home" \
       ./gradlew :usbdebug-hider:assembleDebug :debugdetectprobe:assembleDebug )
  ls -la "$HIDER_APK" "$PROBE_APK"
}

cmd_install() {
  need_device; require_apks
  mkdir -p "$STAGE"
  cp "$HIDER_APK" "$PROBE_APK" "$ROOT_DIR/deploy/usbdebug_hider.conf" "$STAGE/"
  echo "[1/4] 安裝模組 APK"
  A install -r "$(winpath "$STAGE/usbdebug-hider-debug.apk")" | tail -1
  echo "[2/4] 安裝對照組探針 APK"
  A install -r "$(winpath "$STAGE/debugdetectprobe-debug.apk")" | tail -1
  echo "[3/4] 推送設定檔"
  A push "$(winpath "$STAGE/usbdebug_hider.conf")" "$CONF" | tail -1
  SU "chmod 644 $CONF"
  echo "[4/4] 啟用模組"
  CLI_ "modules enable $MODULE_PKG"
  CLI_ "modules ls"
}

cmd_scope_all() {
  need_device
  local pkgs
  pkgs="$(A shell "pm list packages -3" | sed 's/package://' | tr -d '\r' \
    | grep -vE "^($MODULE_PKG|me\.bmax\.apatch)$" | sed 's|$|/0|' | tr '\n' ' ')"
  local n
  n="$(echo "$pkgs" | wc -w)"
  echo "設定 scope：$n 個第三方 App（已排除模組自己與 APatch 管理器）"
  # shellcheck disable=SC2086
  CLI_ "scope set $MODULE_PKG $pkgs"
}

cmd_scope_set() {
  need_device
  [ $# -ge 1 ] || { echo "用法: $0 scope-set PKG [PKG...]" >&2; exit 1; }
  local args=""
  local p
  for p in "$@"; do args="$args $p/0"; done
  # shellcheck disable=SC2086
  CLI_ "scope set $MODULE_PKG$args"
}

cmd_scope_from_file() {
  need_device
  local f="${1:-}"
  [ -f "$f" ] || { echo "用法: $0 scope-from-file <清單檔>（見 deploy/usbdebug_hider_scope.txt）" >&2; exit 1; }
  local pkgs="" line p
  while IFS= read -r line; do
    p="${line%%#*}"
    p="$(echo "$p" | tr -d '[:space:]')"
    [ -n "$p" ] && pkgs="$pkgs $p/0"
  done < "$f"
  local n
  n="$(echo "$pkgs" | wc -w)"
  echo "由 $f 設定 scope：$n 個 App"
  # shellcheck disable=SC2086
  CLI_ "scope set $MODULE_PKG$pkgs"
}

cmd_scope_list() {
  need_device
  CLI_ "scope ls $MODULE_PKG"
}

cmd_conf() {
  need_device
  mkdir -p "$STAGE"
  cp "$ROOT_DIR/deploy/usbdebug_hider.conf" "$STAGE/"
  A push "$(winpath "$STAGE/usbdebug_hider.conf")" "$CONF" | tail -1
  SU "chmod 644 $CONF"
  echo "已推送；模組 2 秒內自動熱重載（必要時重啟目標 App）"
}

cmd_enable()  { need_device; CLI_ "modules enable $MODULE_PKG"; }
cmd_disable() { need_device; CLI_ "modules disable $MODULE_PKG"; }

cmd_status() {
  need_device
  echo "=== 裝置 ==="
  A shell "getprop ro.product.model; getprop ro.build.version.release; getprop ro.build.version.sdk" | tr -d '\r' | paste -sd' / ' -
  echo "=== 框架 ==="
  CLI_ "status"
  echo "=== 模組 ==="
  CLI_ "modules ls"
  echo "=== scope 筆數 ==="
  CLI_ "scope ls $MODULE_PKG" | tail -n +3 | grep -c . || true
  echo "=== 設定檔（$CONF）==="
  A shell "cat $CONF 2>/dev/null | grep -vE '^#|^$'" || echo "(未推送，使用 APK 內建預設)"
  echo "=== 裝置真實狀態（shell uid=2000，不該被遮）==="
  printf '  adb_enabled=%s  development_settings_enabled=%s  sys.usb.config=%s  init.svc.adbd=%s\n' \
    "$(A shell 'settings get global adb_enabled' | tr -d '\r')" \
    "$(A shell 'settings get global development_settings_enabled' | tr -d '\r')" \
    "$(A shell 'getprop sys.usb.config' | tr -d '\r')" \
    "$(A shell 'getprop init.svc.adbd' | tr -d '\r')"
}

cmd_verify() {
  need_device
  echo "--- 真實值（shell/root 視角）---"
  printf '  adb_enabled=%s  sys.usb.config=%s  init.svc.adbd=%s\n' \
    "$(A shell 'settings get global adb_enabled' | tr -d '\r')" \
    "$(A shell 'getprop sys.usb.config' | tr -d '\r')" \
    "$(A shell 'getprop init.svc.adbd' | tr -d '\r')" 
  echo "--- 被注入的探針視角（應為假值）---"
  A shell "logcat -c"
  A shell "am force-stop $PROBE_PKG"
  A shell "am start -n $PROBE_PKG/.DetectActivity" >/dev/null 2>&1
  sleep 7
  A shell "logcat -d -s DebugDetectProbe:I" | sed 's/^.*DebugDetectProbe: //' \
    | grep -E 'settings.Global.getInt:adb_enabled|settings.Global.getInt:development_settings_enabled|query.single:adb_enabled|prop.java:sys.usb.config|prop.java:init.svc.adbd|prop.native:sys.usb.config|prop.native:init.svc.adbd'
  echo "--- 模組注入紀錄 ---"
  SU "$CLI log cat" | grep -E "UsbDebugMask" | tail -4
}

cmd_panic() {
  need_device
  echo "停用所有 APatch 模組（救援用）…"
  local ids
  ids="$(SU "/data/adb/apd module list" | grep '"id"' | sed 's/.*: *"//;s/".*//')"
  local id
  for id in $ids; do
    echo "  disable $id"
    SU "/data/adb/apd module disable $id"
  done
  echo "完成。請重開機；若仍卡開機，改用 APatch 安全模式（開機時按音量鍵）。"
}

cmd_remove_all() {
  need_device
  echo "=== 完整移除：本專案 App → 框架模組 → 暫存檔 → 重開機 → 驗證 ==="

  echo "[1/6] 停用並移除本專案的 App"
  CLI_ "modules disable $MODULE_PKG" 2>/dev/null || true
  A uninstall "$MODULE_PKG" 2>/dev/null || echo "  （$MODULE_PKG 不在裝置上）"
  A uninstall "$PROBE_PKG" 2>/dev/null || echo "  （$PROBE_PKG 不在裝置上）"

  echo "[2/6] 移除框架模組（先 Vector，再 NeoZygisk）"
  SU "/data/adb/apd module disable zygisk_vector" 2>/dev/null || true
  SU "/data/adb/apd module disable zygisksu" 2>/dev/null || true
  SU "/data/adb/apd module uninstall zygisk_vector" 2>/dev/null || echo "  （zygisk_vector 不存在）"
  SU "/data/adb/apd module uninstall zygisksu" 2>/dev/null || echo "  （zygisksu 不存在）"

  echo "[3/6] 清除推到裝置的暫存檔"
  SU "rm -f $CONF"
  SU "rm -f /data/local/tmp/NeoZygisk-*.zip /data/local/tmp/Vector-*.zip"
  SU "rm -f /data/local/tmp/mini_sqlite3 /data/local/tmp/linker64 /data/local/tmp/daemon.apk"
  SU "rm -f /data/local/tmp/ui.xml /data/local/tmp/ui_lbl.xml /data/local/tmp/ui_lbl2.xml /data/local/tmp/ui3.xml"

  echo "[4/6] 重開機（讓框架卸載生效）"
  A reboot
  sleep 10
  A wait-for-device
  local i b
  for i in $(seq 1 90); do
    b="$(A shell 'getprop sys.boot_completed' 2>/dev/null | tr -d '\r')"
    [ "$b" = "1" ] && { echo "  開機完成（${i}x2s）"; break; }
    sleep 2
  done
  sleep 5

  echo "[5/6] 清除框架殘留目錄"
  SU "rm -rf /data/adb/lspd /data/adb/neozygisk"

  echo "[6/6] 驗證"
  echo "  -- APatch 模組 --"
  SU "/data/adb/apd module list" | head -5
  echo "  -- 本專案 App --"
  A shell "pm list packages | grep -E 'usbdebughider|debugdetectprobe' || echo '  (已無)'"
  echo "  -- 框架行程 --"
  A shell "ps -A -o NAME | grep -E 'vectord|zygisk' || echo '  (已無)'"
  echo "  -- 設定檔 --"
  A shell "[ -f $CONF ] && echo '  仍存在' || echo '  (已刪除)'"
  echo "  -- 裝置 adb 真實狀態（應維持原樣）--"
  printf '  adb_enabled=%s  sys.usb.config=%s  init.svc.adbd=%s\n' \
    "$(A shell 'settings get global adb_enabled' | tr -d '\r')" \
    "$(A shell 'getprop sys.usb.config' | tr -d '\r')" \
    "$(A shell 'getprop init.svc.adbd' | tr -d '\r')"
  echo "完成。（專案原始碼、報告、APK、框架 zip 仍在主機端，未刪除）"
}

cmd_uninstall() {
  need_device
  CLI_ "modules disable $MODULE_PKG"
  A uninstall "$MODULE_PKG" || true
  A uninstall "$PROBE_PKG" || true
  SU "rm -f $CONF"
  echo "已移除模組與探針 APK（框架 NeoZygisk/Vector 保留）"
}

case "${1:-}" in
  build)       cmd_build ;;
  install)     cmd_install ;;
  scope-all)   cmd_scope_all ;;
  scope-set)   shift; cmd_scope_set "$@" ;;
  scope-from-file) shift; cmd_scope_from_file "$@" ;;
  scope-list)  cmd_scope_list ;;
  conf)        cmd_conf ;;
  enable)      cmd_enable ;;
  disable)     cmd_disable ;;
  status)      cmd_status ;;
  verify)      cmd_verify ;;
  panic)       cmd_panic ;;
  uninstall)   cmd_uninstall ;;
  remove-all)  cmd_remove_all ;;
  *)
    sed -n '2,30p' "$0"
    exit 1
    ;;
esac
