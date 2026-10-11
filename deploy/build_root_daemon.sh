#!/usr/bin/env bash
source "$(dirname "$0")/env.sh"   # 共用 ADB/SERIAL（見 deploy/env.sh）
set -u

# ============ 依你的環境調整 ============
# NDK r27d 的 clang 包裝腳本（bash script，會自動呼叫 clang.exe 並加 --target）
NDK_BIN="${NDK_BIN:-/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/ndk/android-ndk-r27d/toolchains/llvm/prebuilt/windows-x86_64/bin}"
API="${API:-24}"
# =======================================

CC="$NDK_BIN/aarch64-linux-android${API}-clang"

echo "使用編譯器: $CC"
[ -x "$CC" ] || { echo "找不到 clang wrapper：$CC"; exit 1; }

# Windows clang.exe 讀不到 WSL 的 /home，先複製原始碼到 Windows 可存取位置
WIN_DIR="${WIN_DIR:-/mnt/c/Users/<USER>}"
cp /home/kali/rokid_root/deploy/root_daemon.c "$WIN_DIR/root_daemon.c"

(
  cd "$WIN_DIR"
  "$CC" -O2 -o root_daemon root_daemon.c
)

cp "$WIN_DIR/root_daemon" /home/kali/rokid_root/deploy/root_daemon
echo "產出: /home/kali/rokid_root/deploy/root_daemon"
ls -la /home/kali/rokid_root/deploy/root_daemon
