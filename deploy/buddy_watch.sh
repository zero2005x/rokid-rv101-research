#!/usr/bin/env bash
# buddy_watch.sh — 取樣 /proc/buddyinfo 與 /proc/meminfo，用於判斷 KernelSnitch
# mm_struct leak 失敗是否與「高階頁面（order>=3）枯竭」相關。
#
# 用法：
#   bash buddy_watch.sh <秒數> <間隔秒> <輸出檔>
# 每個樣本一行（好比對 exploit 執行前後的變化）：
#   <unixtime> <uptime> <order0..10 free blocks> MemFree Slab SUnreclaim
source "$(dirname "$0")/env.sh"
set -u
SERIAL="${SERIAL:?}"
DURATION="${1:-180}"
INTERVAL="${2:-10}"
OUT="${3:-/home/kali/rokid_root/deploy/buddy_watch.log}"

adb_s() { "$ADB" -s "$SERIAL" "$@"; }

echo "# ts uptime ord0..ord10 MemFree Slab SUnreclaim" > "$OUT"
END=$(( $(date +%s) + DURATION ))
while [ "$(date +%s)" -lt "$END" ]; do
  SNAP=$(adb_s shell "awk '{printf \"%s\", \$2}' /proc/uptime; printf ' '; \
    awk '/zone/{for(i=4;i<=NF;i++) printf \"%s \", \$i}' /proc/buddyinfo; printf ' '; \
    awk '/MemFree|^Slab|SUnreclaim/{printf \"%s \", \$2}' /proc/meminfo" 2>/dev/null | tr -d '\r')
  [ -n "$SNAP" ] && echo "$(date +%s) $SNAP" >> "$OUT"
  sleep "$INTERVAL"
done
