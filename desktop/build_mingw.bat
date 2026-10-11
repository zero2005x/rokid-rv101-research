@echo off
rem ============================================================
rem  build_mingw.bat — 以 MinGW-w64 編譯 ZeroLightRokid Desktop
rem
rem  需求：MinGW-w64（x86_64-w64-mingw32-gcc + windres）在 PATH 中
rem        MSYS2: pacman -S mingw-w64-x86_64-gcc
rem
rem  用法：直接雙擊，或在命令提示字元執行 build_mingw.bat
rem ============================================================
setlocal
cd /d "%~dp0"

set CC=x86_64-w64-mingw32-gcc
set WINDRES=x86_64-w64-mingw32-windres

where %CC% >nul 2>nul
if errorlevel 1 (
  echo [x] 找不到 %CC%，請先安裝 MinGW-w64 並確認在 PATH 中。
  exit /b 1
)

rem ---- 1) 語系字串表（需要 Python；沒有就沿用已產生的 src\i18n_strings.h）----
if exist src\i18n_strings.h (
  echo [1/3] 語系字串表已存在，跳過產生
) else (
  where python >nul 2>nul
  if errorlevel 1 (
    echo [x] 找不到 src\i18n_strings.h，且系統沒有 Python 可產生。
    echo     請安裝 Python 3 後執行：python tools\gen_i18n.py
    exit /b 1
  )
  echo [1/3] 產生語系字串表...
  python tools\gen_i18n.py || exit /b 1
)

rem ---- 2) 編譯資源（manifest + 版本資訊）----
echo [2/3] 編譯資源...
%WINDRES% -I src src\app.rc -O coff -o src\app.res || exit /b 1

rem ---- 3) 編譯主程式 ----
rem  注意 -mwindows 不可省略，否則會產生 console 子系統執行檔（雙擊會閃黑窗）
echo [3/3] 編譯主程式...
%CC% -O2 -std=c11 -municode -mwindows -DUNICODE -D_UNICODE ^
  -finput-charset=UTF-8 ^
  -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type ^
  -o zero_light_desktop.exe ^
  src\zero_light_desktop.c src\app.res ^
  -lgdi32 -luser32 -lcomctl32 -lshell32 -lole32 -lshlwapi ^
  -lcomdlg32 -lwinhttp -lbcrypt
if errorlevel 1 (
  echo [x] 編譯失敗
  exit /b 1
)

echo.
echo [OK] 完成：zero_light_desktop.exe
endlocal
