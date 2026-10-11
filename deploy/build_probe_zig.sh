#!/usr/bin/env bash
# build_probe_zig.sh — cross-compile a device-side probe with zig (the fallback when no local Linux NDK exists)
#
# Background: this workspace could only compile with the Windows NDK clang
# (deploy/build_root_daemon.sh), but /mnt/c is read-only in the sandbox -> no temporary files
# can be written there. Switch to ziglang (a pip package that ships its own cross-compiler):
#     .venv/bin/pip install ziglang
#     zig cc -target aarch64-linux-musl -static  -> a static ELF that runs on Android
#
# NOTE: zig needs a writable cache directory (the default ~/.cache/zig is read-only in the sandbox), so it always points inside the workspace.
#
# Usage: bash deploy/build_probe_zig.sh <source.c> [output-name]
set -eu
cd "$(dirname "$0")/.."
SRC="${1:?用法: build_probe_zig.sh <原始碼.c> [輸出檔名]}"
OUT="${2:-$(basename "${SRC%.c}")}"

export ZIG_GLOBAL_CACHE_DIR="$PWD/build-cache/zig-global"
export ZIG_LOCAL_CACHE_DIR="$PWD/build-cache/zig-local"
mkdir -p "$ZIG_GLOBAL_CACHE_DIR" "$ZIG_LOCAL_CACHE_DIR" build-cache

[ -x .venv/bin/python ] || { echo "找不到 .venv/bin/python（請先建立 venv）"; exit 1; }
.venv/bin/python -c "import ziglang" 2>/dev/null || {
  echo "安裝 ziglang…"; .venv/bin/pip install ziglang >/dev/null; }

.venv/bin/python -m ziglang cc -target aarch64-linux-musl -static -O2 "$SRC" -o "build-cache/$OUT"
echo "產出: build-cache/$OUT"
file "build-cache/$OUT" | cut -c1-90

# optional: push straight to the device
if [ "${PUSH:-0}" = "1" ]; then
  source "$(dirname "$0")/env.sh"
  "$ADB" -s "$SERIAL" push "build-cache/$OUT" /data/local/tmp/ >/dev/null
  "$ADB" -s "$SERIAL" shell "chmod 755 /data/local/tmp/$(basename "$OUT")"
  echo "已推送到裝置 /data/local/tmp/$(basename "$OUT")"
fi
