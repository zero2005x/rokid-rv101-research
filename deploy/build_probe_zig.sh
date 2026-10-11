#!/usr/bin/env bash
# build_probe_zig.sh — 用 zig 交叉編譯「裝置端探針」（本機無 Linux NDK 時的替代路徑）
#
# 背景：本工作區原本只能靠 Windows 版 NDK clang 編譯（deploy/build_root_daemon.sh），
# 但沙箱中 /mnt/c 為唯讀 → 無法在那裡寫暫存檔。改用 ziglang（pip 套件，自帶交叉編譯器）：
#     .venv/bin/pip install ziglang
#     zig cc -target aarch64-linux-musl -static  → 產出可在 Android 執行的靜態 ELF
#
# 注意：zig 需要可寫的快取目錄（預設 ~/.cache/zig 在沙箱中唯讀），故一律指定到工作區內。
#
# 用法：bash deploy/build_probe_zig.sh <原始碼.c> [輸出檔名]
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

# 可選：直接推上裝置
if [ "${PUSH:-0}" = "1" ]; then
  source "$(dirname "$0")/env.sh"
  "$ADB" -s "$SERIAL" push "build-cache/$OUT" /data/local/tmp/ >/dev/null
  "$ADB" -s "$SERIAL" shell "chmod 755 /data/local/tmp/$(basename "$OUT")"
  echo "已推送到裝置 /data/local/tmp/$(basename "$OUT")"
fi
