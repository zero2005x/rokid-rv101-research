@echo off
rem ============================================================
rem  build_msvc.bat — 以 MSVC 編譯 ZeroLightRokid Desktop
rem
rem  用法：在「Developer Command Prompt for VS」中執行 build_msvc.bat
rem        （一般命令提示字元沒有 cl.exe）
rem
rem  注意：/utf-8 不可省略。原始碼與產生的字串表都是 UTF-8，
rem        少了它 MSVC 會以系統代碼頁解讀，14 個語系會全部變亂碼。
rem ============================================================
setlocal
cd /d "%~dp0"

rem ---- 1) 語系字串表 ----
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
rc /nologo /I src /fo src\app.res src\app.rc || exit /b 1

rem ---- 3) 編譯主程式 ----
rem  /DUNICODE /D_UNICODE : 使用寬字元 API
rem  /utf-8               : 原始碼與執行期字元集皆為 UTF-8（務必保留）
rem  /SUBSYSTEM:WINDOWS   : GUI 子系統（不要 console 視窗）
echo [3/3] 編譯主程式...
cl /nologo /O2 /utf-8 /DUNICODE /D_UNICODE /W3 ^
  /D_CRT_SECURE_NO_WARNINGS ^
  src\zero_light_desktop.c src\app.res ^
  /link /SUBSYSTEM:WINDOWS ^
  user32.lib gdi32.lib comctl32.lib shell32.lib ole32.lib shlwapi.lib ^
  comdlg32.lib winhttp.lib bcrypt.lib ^
  /OUT:zero_light_desktop.exe
if errorlevel 1 (
  echo [x] 編譯失敗
  exit /b 1
)

rem 清掉 MSVC 產生的中間檔
del /q *.obj 2>nul

echo.
echo [OK] 完成：zero_light_desktop.exe
endlocal
