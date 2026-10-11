#!/usr/bin/env bash
# build_kgsl_probe.sh — 編譯 KGSL ioctl 回測探針（與 root_daemon 相同 NDK 工具鏈）
source "$(dirname "$0")/env.sh"   # 共用 ADB/SERIAL（見 deploy/env.sh）
set -u

NDK_BIN="${NDK_BIN:-/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/ndk/android-ndk-r27d/toolchains/llvm/prebuilt/windows-x86_64/bin}"
API="${API:-24}"
CC="$NDK_BIN/aarch64-linux-android${API}-clang"
WIN_DIR="${WIN_DIR:-/mnt/c/Users/<USER>}"

[ -x "$CC" ] || { echo "找不到 clang wrapper：$CC"; exit 1; }
echo "使用編譯器: $CC"

# Windows clang 讀不到 WSL 的 /home → 先複製到 Windows 可存取位置
cp "$(dirname "$0")/kgsl_ioctl_probe.c" "$WIN_DIR/kgsl_ioctl_probe.c"
( cd "$WIN_DIR" && "$CC" -O2 -Wall -o kgsl_ioctl_probe kgsl_ioctl_probe.c )
cp "$WIN_DIR/kgsl_ioctl_probe" "$(dirname "$0")/kgsl_ioctl_probe"
echo "產出: $(dirname "$0")/kgsl_ioctl_probe"
ls -la "$(dirname "$0")/kgsl_ioctl_probe"
