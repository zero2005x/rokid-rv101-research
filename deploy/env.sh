#!/usr/bin/env bash
# Shared environment setup: the **single** source of the adb path and device serial.
#
# Why: previously 10 scripts each hardcoded `/mnt/c/Users/<user>/AppData/.../adb.exe` and
# the glasses serial `<DEVICE_SERIAL>`, so changing machine or device meant editing every
# file (see scripts/audit_hardcode.py).
#
# Usage (at the top of a script):
#   source "$(dirname "$0")/env.sh"
# Overridable environment variables: ADB, SERIAL, WIN_ADB_DIR
set -u

# Windows-side adb (WSL); overridable with the ADB environment variable
WIN_ADB_DIR="${WIN_ADB_DIR:-/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools}"
ADB="${ADB:-$WIN_ADB_DIR/adb.exe}"

# Device serial: defaults to the first device from adb devices; overridable with SERIAL
if [ -z "${SERIAL:-}" ]; then
  SERIAL="$("$ADB" devices 2>/dev/null | awk 'NR>1 && $2=="device" {print $1; exit}')"
fi
SERIAL="${SERIAL:-<DEVICE_SERIAL>}"   # last resort: the known glasses serial

# Common paths
export ADB SERIAL WIN_ADB_DIR
