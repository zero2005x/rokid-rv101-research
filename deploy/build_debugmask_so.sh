#!/usr/bin/env bash
# build_debugmask_so.sh — build the usbdebug-hider native layer (PLT/GOT hook)
# Output: android/usbdebug-hider/src/main/jniLibs/arm64-v8a/libdebugmask.so
set -euo pipefail
cd "$(dirname "$0")/.."

NDK_BIN="${NDK_BIN:-/home/kali/android-sdk/ndk/26.1.10909125/toolchains/llvm/prebuilt/linux-x86_64/bin}"
API="${API:-26}"
CC="$NDK_BIN/aarch64-linux-android${API}-clang"

[ -x "$CC" ] || { echo "找不到 NDK clang：$CC（可用 NDK_BIN 覆寫）" >&2; exit 1; }

OUT_DIR=android/usbdebug-hider/src/main/jniLibs/arm64-v8a
mkdir -p "$OUT_DIR"
"$CC" -O2 -Wall -fPIC -shared -o "$OUT_DIR/libdebugmask.so" android/usbdebug-hider/src/main/jni/debugmask.c -llog -ldl
file "$OUT_DIR/libdebugmask.so"
ls -la "$OUT_DIR/libdebugmask.so"
echo "產出: $OUT_DIR/libdebugmask.so"
