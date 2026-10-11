#!/usr/bin/env bash
# build_mingw.sh — 在 Linux / macOS 上以 MinGW-w64 交叉編譯 ZeroLightRokid Desktop
#
# 用法：
#   ./build_mingw.sh            # 編譯 64 位元（預設）
#   ./build_mingw.sh 32         # 編譯 32 位元
#   ./build_mingw.sh clean      # 清除產物
#
# 需求：x86_64-w64-mingw32-gcc（Debian/Ubuntu: gcc-mingw-w64-x86-64；
#       macOS: brew install mingw-w64）
#
# 注意：-mwindows 不可省略。少了它會產生 console 子系統執行檔，
#       雙擊時會先閃出一個黑色命令提示字元視窗。
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
# 格式字串來自執行期查表，編譯器無法檢查引數是否相符；
# 少給引數會讓 vswprintf 讀取不存在的堆疊內容而崩潰。
python3 tools/check_logfmt.py

echo "== 3/4 編譯資源（manifest + 版本資訊） =="
"$WINDRES" -I src src/app.rc -O coff -o src/app.res

echo "== 4/4 編譯主程式（$TRIPLE） =="
# -finput-charset=UTF-8  : 原始碼為 UTF-8
# -fwide-exec-charset     : Windows target 預設即 UTF-16LE，L"..." 直接正確
# -municode              : 使用 wWinMain 進入點
# -mwindows              : GUI 子系統（不要 console 視窗）
CFLAGS=(
  -O2 -std=c11 -municode -mwindows -DUNICODE -D_UNICODE
  -finput-charset=UTF-8
  -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type
  -ffile-prefix-map="$PWD"=. -fdebug-prefix-map="$PWD"=.
)
# --no-insert-timestamp：連結器預設會把建置時間寫進 PE 標頭，
#   導致同樣的原始碼每次建出不同位元組、SHA256SUMS.txt 永遠對不上。
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
