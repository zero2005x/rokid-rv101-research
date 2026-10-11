#!/usr/bin/env bash
# Run a command as root on the Rokid glasses via the root_daemon UNIX socket.
# Usage: ./run_root.sh '<command>'
source "$(dirname "$0")/env.sh"   # shared ADB/SERIAL (see deploy/env.sh)
set -u

if [ $# -lt 1 ]; then
  echo "usage: $0 '<command>'"
  exit 1
fi
printf '%s\n' "$1" | "$ADB" -s "$SERIAL" shell "nc -U /data/local/tmp/root_daemon.sock"
