#!/usr/bin/env bash
# build_mini_sqlite3.sh — cross-compile mini_sqlite3 (aarch64 Android)
# Purpose: read/write the LSPosed/Vector config DB (module scope) when the device
# has no sqlite3 CLI.
set -euo pipefail
cd "$(dirname "$0")/.."

NDK_BIN="${NDK_BIN:-/home/kali/android-sdk/ndk/26.1.10909125/toolchains/llvm/prebuilt/linux-x86_64/bin}"
API="${API:-26}"
CC="$NDK_BIN/aarch64-linux-android${API}-clang"

[ -x "$CC" ] || { echo "找不到 NDK clang：$CC（可用 NDK_BIN 覆寫）" >&2; exit 1; }

mkdir -p build-cache
"$CC" -O2 -Wall -o build-cache/mini_sqlite3 deploy/mini_sqlite3.c -ldl
chmod 755 build-cache/mini_sqlite3
file build-cache/mini_sqlite3
echo "產出: build-cache/mini_sqlite3"
