#!/usr/bin/env bash
# build_adb_relay.sh — cross-compile adb_tcp_relay (aarch64 Android) with the Windows NDK
set -eu
cd "$(dirname "$0")/.."
NDK_BIN="${NDK_BIN:-/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/ndk/android-ndk-r27d/toolchains/llvm/prebuilt/windows-x86_64/bin}"
API="${API:-24}"
WIN_DIR="${WIN_DIR:-/mnt/c/Users/<USER>}"
CC="$NDK_BIN/aarch64-linux-android${API}-clang"

[ -x "$CC" ] || { echo "找不到 NDK clang：$CC"; exit 1; }

cp deploy/adb_tcp_relay.c "$WIN_DIR/adb_tcp_relay.c"
( cd "$WIN_DIR" && "$CC" -O2 -Wall -o adb_tcp_relay adb_tcp_relay.c )
cp "$WIN_DIR/adb_tcp_relay" build-cache/adb_tcp_relay
chmod 755 build-cache/adb_tcp_relay
echo "產出: build-cache/adb_tcp_relay"
