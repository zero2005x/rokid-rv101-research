#!/usr/bin/env bash
source "$(dirname "$0")/env.sh"   # shared ADB/SERIAL (see deploy/env.sh)
set -u

# ============ adjust to your environment ============
# NDK r27d clang wrapper script (a bash script that calls clang.exe and adds --target)
NDK_BIN="${NDK_BIN:-/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/ndk/android-ndk-r27d/toolchains/llvm/prebuilt/windows-x86_64/bin}"
API="${API:-24}"
# =======================================

CC="$NDK_BIN/aarch64-linux-android${API}-clang"

echo "使用編譯器: $CC"
[ -x "$CC" ] || { echo "找不到 clang wrapper：$CC"; exit 1; }

# Windows clang.exe cannot read WSL /home; copy the source somewhere Windows can
# reach first
WIN_DIR="${WIN_DIR:-/mnt/c/Users/<USER>}"
cp /home/kali/rokid_root/deploy/root_daemon.c "$WIN_DIR/root_daemon.c"

(
  cd "$WIN_DIR"
  "$CC" -O2 -o root_daemon root_daemon.c
)

cp "$WIN_DIR/root_daemon" /home/kali/rokid_root/deploy/root_daemon
echo "產出: /home/kali/rokid_root/deploy/root_daemon"
ls -la /home/kali/rokid_root/deploy/root_daemon
