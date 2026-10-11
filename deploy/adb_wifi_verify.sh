#!/usr/bin/env bash
# adb_wifi_verify.sh — 無線 adb 一鍵驗證（**不需要切換 PC 網路**）
#
# 檢查眼鏡端「該開的都有開」，並印出 PC 端要下的指令；若 PC 已經在眼鏡網段，
# 會直接嘗試 adb connect 並回報結果。
#
# 用法：
#   source deploy/env.sh
#   bash deploy/adb_wifi_verify.sh          # 用 env.sh 的 SERIAL
#   SERIAL=<DEVICE_SERIAL> bash deploy/adb_wifi_verify.sh
set -u
source "$(dirname "$0")/env.sh"

PORT=5555
HEX=":$(printf '%04X' "$PORT")"          # 5555 → :15B3

D() { "$ADB" -s "$SERIAL" shell "$1" 2>&1; }

echo "===== 1. 無線 adb listener（**兩張表都要看**） ====="
D "v4=\$(grep -c '$HEX ' /proc/net/tcp 2>/dev/null); v6=\$(grep -c '$HEX ' /proc/net/tcp6 2>/dev/null)
   echo \"  IPv4 (/proc/net/tcp)  : \$v4\"
   echo \"  IPv6 (/proc/net/tcp6) : \$v6   ← 本機 adbd 綁這裡（[::] dual-stack）\"
   if [ \"\$v4\" = 0 ] && [ \"\$v6\" = 0 ]; then
     echo '  ✗ 沒有 listener：需 root 設 persist.adb.tcp.port=$PORT 後正常重開機（勿重啟 adbd）'
   else
     echo '  ✓ 有 listener'
   fi"

echo
echo "===== 2. 屬性（persist = 跨開機自動生效） ====="
D 'printf "  persist.adb.tcp.port = %s\n  service.adb.tcp.port = %s\n  ro.adb.secure        = %s\n" \
     "$(getprop persist.adb.tcp.port)" "$(getprop service.adb.tcp.port)" "$(getprop ro.adb.secure)"'

echo
echo "===== 3. 對外入口 IP（SoftAP 固定 192.168.43.1） ====="
D 'ip -4 -o addr 2>/dev/null | grep -v " lo " | awk "{printf \"  %s  %s\n\", \$2, \$4}"
   ip -o addr 2>/dev/null | grep -q wlan2 || echo "  ⚠ wlan2 不在 → AP 未開，可下：cmd wifi start-softap RokidCam wpa2 rokid1234"'

echo
echo "===== 4. 授權金鑰（新主機首次連線需授權；眼鏡無螢幕可彈窗） ====="
D 'if [ -r /data/misc/adb/adb_keys ]; then echo "  adb_keys 條數 = $(wc -l < /data/misc/adb/adb_keys)"; else echo "  (shell 讀不到，需 root)"; fi'

echo
echo "===== 5. PC 端（若已在眼鏡網段會直接嘗試） ====="
IP="$(D 'ip -4 -o addr show wlan2 2>/dev/null | grep -oE "inet [0-9.]+" | cut -d" " -f2')"
IP="${IP:-192.168.43.1}"
echo "  adb connect ${IP}:${PORT}"
if timeout 8 "$ADB" connect "${IP}:${PORT}" >/dev/null 2>&1 &&
   [ "$(timeout 8 "$ADB" -s "${IP}:${PORT}" get-state 2>/dev/null | tr -d '\r')" = "device" ]; then
  echo "  ✓ 連線成功：$("$ADB" -s "${IP}:${PORT}" shell id 2>/dev/null | head -1)"
else
  echo "  （PC 目前不在眼鏡網段，或尚未授權 → transport 非 device）"
  echo "  → 加入 SoftAP RokidCam（密碼 rokid1234）後再執行同一行指令"
  "$ADB" disconnect "${IP}:${PORT}" >/dev/null 2>&1
fi
