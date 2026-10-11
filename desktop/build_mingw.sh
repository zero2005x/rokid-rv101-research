#!/usr/bin/env bash
# build_mingw.sh — cross-compile ZeroLightRokid Desktop with MinGW-w64 on Linux / macOS
#
# Usage:
#   ./build_mingw.sh            # build 64-bit (default)
#   ./build_mingw.sh 32         # build 32-bit
#   ./build_mingw.sh clean      # remove the build artefacts
#
# Requires: x86_64-w64-mingw32-gcc (Debian/Ubuntu: gcc-mingw-w64-x86-64;
#           macOS: brew install mingw-w64)
#
# NOTE: -mwindows must not be omitted. Without it the executable uses the console subsystem
#       and double-clicking it flashes a black command-prompt window first.
set -euo pipefail

cd "$(dirname "$0")"

ARCH="${1:-64}"
case "$ARCH" in
  clean)
    rm -f src/*.o src/*.res zero_light_desktop.exe zero_light_desktop32.exe
    echo "已清除建置產物"
    exit 0
    ;;
  32)  TRIPLE=i686-w64-mingw32;    OUT=zero_light_desktop32.exe ;;
  64|*) TRIPLE=x86_64-w64-mingw32; OUT=zero_light_desktop.exe ;;
esac

CC="$TRIPLE-gcc"
WINDRES="$TRIPLE-windres"

command -v "$CC" >/dev/null 2>&1 || {
  echo "錯誤：找不到 $CC，請先安裝 MinGW-w64 工具鏈" >&2
  exit 1
}

echo "== 1/4 產生並驗證語系字串表 =="
python3 tools/gen_i18n.py

echo "== 2/4 檢查多語系格式化呼叫的引數個數 =="
# The format strings come from a runtime lookup, so the compiler cannot check the argument
# counts; too few arguments make vswprintf read non-existent stack contents and crash.
python3 tools/check_logfmt.py

echo "== 3/4 編譯資源（manifest + 版本資訊） =="
"$WINDRES" -I src src/app.rc -O coff -o src/app.res

echo "== 4/4 編譯主程式（$TRIPLE） =="
# -finput-charset=UTF-8  : the source is UTF-8
# -fwide-exec-charset    : the Windows target already defaults to UTF-16LE, so L"..." is correct
# -municode              : use the wWinMain entry point
# -mwindows              : GUI subsystem (no console window)
CFLAGS=(
  -O2 -std=c11 -municode -mwindows -DUNICODE -D_UNICODE
  -finput-charset=UTF-8
  -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type
  -ffile-prefix-map="$PWD"=. -fdebug-prefix-map="$PWD"=.
)
# --no-insert-timestamp: by default the linker writes the build time into the PE header, so the
#   same source produces different bytes each time and SHA256SUMS.txt never matches.
LDFLAGS=( -Wl,--no-insert-timestamp )

"$CC" "${CFLAGS[@]}" "${LDFLAGS[@]}" \
  -o "$OUT" \
  src/zero_light_desktop.c src/app.res \
  -lgdi32 -luser32 -lcomctl32 -lshell32 -lole32 -lshlwapi \
  -lcomdlg32 -lwinhttp -lbcrypt -lgdiplus -luuid

echo
SIZE=$(stat -c%s "$OUT" 2>/dev/null || stat -f%z "$OUT")
echo "完成：$OUT（$SIZE bytes）"
"$TRIPLE-objdump" -p "$OUT" 2>/dev/null | grep -iE '^Subsystem|Magic' | head -3 || true
