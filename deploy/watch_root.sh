#!/usr/bin/env bash
# watch_root.sh — passively watch the in-flight exploit attempt; verify as soon as root appears.
source "$(dirname "$0")/env.sh"   # shared ADB/SERIAL (see deploy/env.sh)
set -u
SERIAL="${SERIAL:?請設定 SERIAL 或讓 env.sh 自動偵測}"
BRIDGE=/data/local/tmp/ghostlock_bridge
LOG=/home/kali/rokid_root/deploy/watch_root.log

adb_s() { "$ADB" -s "$SERIAL" "$@"; }
log() { echo "[$(date '+%F %T')] $*" >> "$LOG"; }

: > "$LOG"
log "watcher start"
PREV_UP=0; SEEN_OK=0
for i in $(seq 1 240); do   # at most ~40 minutes
  UP=$(adb_s shell cat /proc/uptime 2>/dev/null | awk '{print int($1)}' | tr -d '\r')
  if [ -z "$UP" ]; then
    log "offline (waiting reboot)"; sleep 15; continue
  fi
  # check the root_daemon socket listener
  N=$(adb_s shell "grep -c 'root_daemon.sock' /proc/net/unix 2>/dev/null" | tr -d '\r')
  if [ "$N" = "1" ] 2>/dev/null; then
    OUT=$(printf 'id; getenforce; ls -ld /data/root_proof 2>/dev/null\n' | adb_s shell "nc -U /data/local/tmp/root_daemon.sock" 2>/dev/null)
    log "SOCKET-ROOT: $OUT"
    if echo "$OUT" | grep -q 'uid=0(root)'; then
      log "===== ROOT VERIFIED via root_daemon socket ====="
      echo "===== ROOT VERIFIED ====="
      exit 0
    fi
  fi
  DONE=$(adb_s shell "cat $BRIDGE/last.done 2>/dev/null" | tr -d '\r')
  if echo "$DONE" | grep -q 'uid=0(root)'; then
    log "last.done has root: $DONE"
    log "===== ROOT VERIFIED via last.done ====="
    echo "===== ROOT VERIFIED ====="
    exit 0
  fi
  # device reboot detection (uptime goes backwards)
  if [ "$PREV_UP" -gt 0 ] && [ "$UP" -lt $((PREV_UP - 5)) ]; then
    log "REBOOT detected (${PREV_UP}s -> ${UP}s)"
  fi
  PREV_UP=$UP
  sleep 10
done
log "watcher timeout"
exit 2
