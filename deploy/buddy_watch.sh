#!/usr/bin/env bash
# buddy_watch.sh — sample /proc/buddyinfo and /proc/meminfo to judge whether the
# KernelSnitch mm_struct leak failure correlates with exhaustion of high-order
# pages (order>=3).
#
# Usage:
#   bash buddy_watch.sh <seconds> <interval-seconds> <output-file>
# One line per sample (easy to diff before/after the exploit run):
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
