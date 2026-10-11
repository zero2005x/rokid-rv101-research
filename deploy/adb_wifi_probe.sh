#!/usr/bin/env bash
# adb_wifi_probe.sh — read-only device-side probe of wireless adb feasibility (changes no state)
#
# Purpose: answer one key question — **where does this device's adbd actually listen?**
#   A) a UNIX local socket (/dev/socket/adbd) -> relay is feasible (TCP -> socket)
#   B) USB FunctionFS only (/dev/usb-ffs/adb/ep0) -> relay is not feasible; ship your own adbd
#
# Read-only throughout: getprop / ls / cat / netstat only; no property writes, no adbd restarts, no mounts.
#
# Usage:
#   source deploy/env.sh
# use the device from env.sh
#   SERIAL=xxx bash deploy/adb_wifi_probe.sh
set -u
source "$(dirname "$0")/env.sh"

D() { "$ADB" -s "$SERIAL" shell "$1" 2>&1; }

echo "===== 0. 裝置 ====="
D 'printf "model=%s\nserial=%s\nandroid=%s\nsdk=%s\nbuild=%s\n" \
  "$(getprop ro.product.model)" "$(getprop ro.serialno)" \
  "$(getprop ro.build.version.release)" "$(getprop ro.build.version.sdk)" \
  "$(getprop ro.build.version.incremental)"'

echo
echo "===== 1. adb 相關屬性（是否被 build 忽略） ====="
D 'for p in service.adb.tcp.port persist.adb.tcp.port service.adb.listen_addrs \
        service.adb.root persist.sys.usb.config sys.usb.config sys.usb.state \
        ro.debuggable ro.secure ro.adb.secure ro.boot.verifiedbootstate \
        persist.adb.wifi.guid persist.vendor.adb; do
     printf "%-28s = %s\n" "$p" "$(getprop $p)"
   done'

echo
echo "===== 2. adbd 行程與身分 ====="
D 'ps -A -o USER,PID,PPID,ARGS 2>/dev/null | grep -E "[a]dbd" || echo "(找不到 adbd)"'

echo
echo "===== 3. adbd 的 fd（關鍵：有沒有 socket listener） ====="
D 'P=$(pidof adbd 2>/dev/null || ps -A -o PID,ARGS | grep -E "[a]dbd" | awk "{print \$1}" | head -1)
   echo "adbd pid=$P"
   [ -n "$P" ] && ls -l /proc/$P/fd 2>&1 | head -30'

echo
echo "===== 4. UNIX socket 清單（找 adbd） ====="
D 'grep -i adb /proc/net/unix 2>/dev/null || echo "(/proc/net/unix 無 adb 條目)"
   echo "--- /dev/socket 內含 adb 者 ---"
   ls -laZ /dev/socket 2>/dev/null | grep -i adb || echo "(無)"
   echo "--- 直接試探 /dev/socket/adbd ---"
   ls -la /dev/socket/adbd 2>&1'

echo
echo "===== 5. TCP listener 現況 ====="
D 'echo "--- /proc/net/tcp (本地埠, hex) ---"
   cat /proc/net/tcp /proc/net/tcp6 2>/dev/null | awk "NR>1 && \$4==\"0A\" {print \$2, \$4}" | head -20
   echo "--- netstat ---"
   (netstat -tlnp 2>/dev/null || ss -tlnp 2>/dev/null) | head -20'

echo
echo "===== 6. USB FunctionFS（B 路線的前提） ====="
D 'ls -la /dev/usb-ffs/adb/ 2>&1 | head
   echo "--- configfs ---"
   ls /config/usb_gadget 2>&1 | head -3'

echo
echo "===== 7. 裝置端可用工具（relay 的替代方案） ====="
D 'for t in nc netcat toybox busybox socat openssl ssh iptables ip; do
     printf "%-10s %s\n" "$t" "$(command -v $t 2>/dev/null || echo -)"
   done
   echo "--- toybox applets ---"
   toybox 2>/dev/null | tr " " "\n" | grep -E "^(nc|netcat|microcom|wget)$" | tr "\n" " "; echo'

echo
echo "===== 8. adbd 二進位（能否自帶一份跑 TCP-only） ====="
D 'ls -la /system/bin/adbd 2>&1
   echo "--- strings 找 TCP/屬性關鍵字 ---"
   strings /system/bin/adbd 2>/dev/null | grep -E "service.adb.tcp.port|persist.adb.tcp.port|service.adb.listen_addrs|verity|listen" | sort -u | head -20'

echo
echo "===== 9. 授權金鑰（決定 relay 進來的 PC 要不要按「允許」） ====="
D 'ls -la /data/misc/adb/ 2>&1 | head
   echo "--- adb_keys 條數與前 16 字元（足以比對本機 adbkey，不需全文） ---"
   if [ -r /data/misc/adb/adb_keys ]; then
     echo "count=$(wc -l < /data/misc/adb/adb_keys)"
     cut -c1-16 /data/misc/adb/adb_keys 2>/dev/null | head -10
   else
     echo "(shell 讀不到 adb_keys — 需 root)"
   fi'

echo
echo "===== 10. 網路介面（relay 要 bind 在哪） ====="
D 'ip -o addr 2>/dev/null | awk "{print \$2, \$3, \$4}" | head -10
   echo "--- 熱點/STA ---"
   getprop | grep -iE "softap|wlan" | head -10'
