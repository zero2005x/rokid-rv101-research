#!/usr/bin/env bash
source "$(dirname "$0")/env.sh"   # shared ADB/SERIAL (see deploy/env.sh)
set -u
# same toolchain as build_root_daemon.sh (NDK r27d clang wrapper)
NDK_BIN="${NDK_BIN:-/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/ndk/android-ndk-r27d/toolchains/llvm/prebuilt/windows-x86_64/bin}"
API="${API:-24}"
CC="$NDK_BIN/aarch64-linux-android${API}-clang"
[ -x "$CC" ] || { echo "找不到 clang wrapper：$CC"; exit 1; }
WIN_DIR="${WIN_DIR:-/mnt/c/Users/<USER>}"
cp /home/kali/rokid_root/deploy/ring_input_daemon.c "$WIN_DIR/ring_input_daemon.c"
( cd "$WIN_DIR" && "$CC" -O2 -o ring_input_daemon ring_input_daemon.c ) || exit 1
cp "$WIN_DIR/ring_input_daemon" /home/kali/rokid_root/deploy/ring_input_daemon
echo "產出: /home/kali/rokid_root/deploy/ring_input_daemon"
ls -la /home/kali/rokid_root/deploy/ring_input_daemon
