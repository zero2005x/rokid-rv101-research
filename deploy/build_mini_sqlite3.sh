#!/usr/bin/env bash
# build_mini_sqlite3.sh — 交叉編譯 mini_sqlite3（aarch64 Android）
# 用途：裝置端沒有 sqlite3 CLI 時，用來讀寫 LSPosed/Vector 的設定 DB（模組 scope）。
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
