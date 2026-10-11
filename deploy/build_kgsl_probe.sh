#!/usr/bin/env bash
# build_kgsl_probe.sh — build the KGSL ioctl regression probe (same NDK toolchain as root_daemon)
source "$(dirname "$0")/env.sh"   # shared ADB/SERIAL (see deploy/env.sh)
set -u

NDK_BIN="${NDK_BIN:-/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/ndk/android-ndk-r27d/toolchains/llvm/prebuilt/windows-x86_64/bin}"
API="${API:-24}"
CC="$NDK_BIN/aarch64-linux-android${API}-clang"
WIN_DIR="${WIN_DIR:-/mnt/c/Users/<USER>}"

[ -x "$CC" ] || { echo "找不到 clang wrapper：$CC"; exit 1; }
echo "使用編譯器: $CC"

# Windows clang cannot read WSL /home -> copy somewhere Windows can reach first
cp "$(dirname "$0")/kgsl_ioctl_probe.c" "$WIN_DIR/kgsl_ioctl_probe.c"
( cd "$WIN_DIR" && "$CC" -O2 -Wall -o kgsl_ioctl_probe kgsl_ioctl_probe.c )
cp "$WIN_DIR/kgsl_ioctl_probe" "$(dirname "$0")/kgsl_ioctl_probe"
echo "產出: $(dirname "$0")/kgsl_ioctl_probe"
ls -la "$(dirname "$0")/kgsl_ioctl_probe"
