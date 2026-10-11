# ZeroLightRokid Desktop（Windows GUI）實機測試報告

> English: [zero_light_desktop_test_report_EN.md](zero_light_desktop_test_report_EN.md)


> 日期：2026-09-11 / 09-12 · 設備：Rokid RG-glasses（RV101，serial <DEVICE_SERIAL>，fw 1.22.009-20260710-150201，Android 12）
> 測試環境：Windows 11 build 26200（2880×1620 @ 200% 縮放），經 WSL2 → Windows `adb.exe` / PowerShell interop 驅動
> 受測版本：`ZeroLightRokidDesktop` v2.0.0（Win32 C，單一 exe，14 語系）
> 測試人：主代理（自動化點擊 + 裝置端狀態回讀，非人工目視）

## 0. 總覽

| # | 測試項 | 結果 |
|---|--------|------|
| 1 | 交叉編譯（x64 / x86，MinGW-w64）與可重現性 | ✅ PASS（0 warning；同原始碼產生相同位元組） |
| 2 | 部署至 Windows 並與來源逐檔 SHA256 比對 | ✅ PASS（31 檔完全相同） |
| 3 | GUI 啟動、200% DPI 版面、內嵌 manifest | ✅ PASS（修正客戶區短少 41 px 後） |
| 4 | 14 語系渲染（含阿拉伯文 RTL、各語系字型鏈） | ✅ PASS（連續 14 次介面重建不崩潰、紀錄無重複） |
| 5 | 重新整理：裝置遙測（序號/型號/韌體/SPL/`id`/SELinux/root/Shizuku/uptime） | ✅ PASS |
| 6 | ① 推送 exploit（`ghostlock_rv101` + `root_daemon`） | ✅ PASS |
| 7 | ② 安裝 helper（bridge 目錄 + 常駐迴圈 + PID 檔） | ✅ PASS（腳本 1957 bytes） |
| 8 | ③ 觸發提權（payload → 關 SELinux + root_daemon） | ✅ PASS（**attempt 1**，約 139 秒） |
| 9 | 🚀 一鍵取得 root（6 開機 × 2 觸發、uptime 閘門、重開機偵測） | ✅ PASS（**兩次實測均成功**，約 6–8 分鐘） |
| 10 | ✓ 驗證 root（root_daemon socket） | ✅ PASS（`uid=0(root)` / Permissive） |
| 11 | 自訂 root 指令（`id; getenforce`） | ✅ PASS |
| 12 | 查 exploit log（`gl_run.log` 尾端 60 行） | ✅ PASS |
| 13 | OTA 修補檢查（官方 API + MD5 簽名） | ✅ PASS（取得目前版本；伺服器查詢需網路） |
| 14 | 靜音拍攝 / 還原拍攝音 | ✅ PASS（6 個 bind mount ↔ 0） |
| 15 | 抑制隱私燈 / 還原隱私燈 | ✅ PASS（`led=off hold=1` ↔ `led=on hold=0`） |
| 16 | 鎖定 OTA / 解除 OTA 鎖定 | ✅ PASS（`suspended=true` ↔ `false`） |
| 17 | 重啟裝置（含確認對話框） | ✅ PASS |
| 18 | 重開機後狀態矩陣（root 消失 / OTA 鎖定持久） | ✅ PASS（見 §5） |
| 19 | 韌體閘門（1.22.009 判定為「漏洞版（可提權）」） | ✅ PASS |
| 20 | 錯誤回報正確性（無假警報、狀態列不殘留） | ✅ PASS（修正後） |

**測試期間發現並修正 8 個缺陷**（§7）。其中 2 個為 only-by-execution 才顯現的嚴重缺陷：
按「重新整理」會 APPCRASH、以及 stdin 管線方向錯誤導致 helper 安裝寫出空腳本。

## 1. 測試方法

GUI 無法用一般自動化框架驅動（無 UIA 測試框架可用），改用 Win32 訊息層驅動：

| 動作 | 手法 |
|---|---|
| 按鈕點擊 | `PostMessage(hBtn, BM_CLICK)`（**必須**用 PostMessage：按鈕處理常式會開強制回應對話框，`SendMessage` 會卡在對話框訊息迴圈） |
| 對話框確認 | 列舉同行程的 `#32770` 視窗 → `GetDlgItem(dlg, IDYES=6)` → `BM_CLICK` |
| 讀取結果 | 對紀錄 EDIT（id 2005）與裝置面板 EDIT（id 2003）送 `WM_GETTEXT` |
| 裝置端驗證 | 直接以 `adb shell` 回讀 `/proc`、`/proc/net/unix`、`/proc/mounts`、`dumpsys` 等 |
| 截圖 | DPI-aware PowerShell + `CopyFromScreen`（**必須先 `SetProcessDpiAwarenessContext(-4)`**，否則座標被虛擬化，2126×1524 會被讀成 1063×762） |

> ⚠️ 方法論重點：不感知 DPI 的擷取工具會把 200% 縮放的視窗回報成一半尺寸，
> 一度讓「版面是否為 2×」難以判定。本報告所有幾何數據皆為 DPI-aware 量測。

## 2. 建置與部署

- `build_mingw.sh` 交叉編譯：x64 813 KB / x86 782 KB，**0 warning**（`-Wall -Wextra`）
- 連結：`gdi32 user32 comctl32 shell32 ole32 shlwapi comdlg32 winhttp bcrypt`
- `-Wl,--no-insert-timestamp` → **可重現建置**，`SHA256SUMS.txt` 可重複驗證
- 內嵌資源：`RT_MANIFEST`（PerMonitorV2 + v6 通用控制項 + longPathAware）、`VS_VERSION_INFO`
- 靜態檢查：`gen_i18n.py --check`（14 語系 × **228** 字串 = **3192** 筆；原 191/2674 為當時數字）、
  `check_logfmt.py`（**134** 個多語系格式化呼叫、**28** 個寬格式、**26** 個 `T()` 取用；原 149/44 為當時數字）、
  `verify_exe.py`（確認 **3192** 筆翻譯確實編入執行檔；原 2674 為當時數字）
- 部署至 `C:\Users\<USER>\Documents\codebase\ZeroLightRokidDesktop`，與來源**逐檔 SHA256 完全相同**

## 3. GUI 與多語系

- 啟動即依系統語言選 zh-TW；14 語系皆可即時切換，**連續 14 次介面重建不崩潰**
- **阿拉伯文全介面鏡像**：關閉鈕移至左側、標籤右對齊、按鈕由右至左；
  `adb` / `exploit` / `root` 等拉丁詞在阿拉伯文中正確嵌入
- **各語系字型鏈命中正確**：ja→Yu Gothic UI、th→Leelawadee UI、ko→Malgun Gothic、ar/ru→Segoe UI
- 切換語言後**紀錄內容無重複、無遺失**（驗證 drain/rebuild 邏輯）
- 截圖見 `reports/zero_light_desktop_ui/`

## 4. Phase 1 — 需要 root 的功能

| 功能 | 裝置端前 | 裝置端後 | 驗證方式 |
|---|---|---|---|
| 靜音拍攝 | `media/audio/ui` mount = 0 | **6** | `grep -c media/audio/ui /proc/mounts`；`mountinfo` 確認來源為 `/data/local/tmp/silent_camera.ogg`（**bind mount，非覆寫原檔**） |
| 還原拍攝音 | 6 | **0** | 同上 |
| 抑制隱私燈 | hold 檔 = 0 | **1** | `ls /data/local/tmp/zl_led_hold`；日誌 `led=off hold=1` |
| 還原隱私燈 | 1 | **0** | 日誌 `led=on hold=0` |
| 鎖定 OTA | `suspended=true` | `true` | `dumpsys package com.rokid.glass.ota` |
| 解除 OTA 鎖定 | `true` | **`false`** | `new state: enabled`（測後已重新鎖定） |

> 靜音拍攝採 **bind mount**，`/proc/self/mountinfo` 的 root 欄位可證明來源是
> `/data/local/tmp/silent_camera.ogg`，**未修改 `/product` 上的原始提示音檔**。

## 5. Phase 2 — 重開機矩陣

| 項目 | 重開機前 | 重開機後 | 判定 |
|---|---|---|---|
| SELinux | Permissive | **Enforcing** | ✅ root 消失（記憶體層級，符合設計） |
| `root_daemon.sock`（`/proc/net/unix`） | 1 | **0** | ✅ |
| `shizuku_server` | 1 | **0** | ✅ 每次開機需重跑 |
| 靜音拍攝 bind mount | 6 | **0** | ✅ 重開機即清除 |
| **OTA 鎖定（`suspended`）** | 1 | **1** | ✅ **跨重開機持久 —— 最重要的安全特性** |
| `/data/local/tmp/*` 檔案 | 存在 | **全部保留** | ✅ 檔案層持久 |
| App 狀態列 | `root OK` | `Status: connected, not rooted` | ✅ 正確反映 |
| 裝置資訊面板 | root: Running | root: **Not running** | ✅ 正確反映 |

App 的「一鍵」流程也正確處理了重開機：實測日誌可見
`Wait for the system to settle (uptime 146 s, need > 150 s)`，
即為避開 kernel panic 後自動重啟視窗所設計的穩定等待。

## 6. Phase 3 — 從乾淨狀態取得 root

| 步驟 | 觀察 |
|---|---|
| ① 推送 exploit | `ghostlock_rv101` 60272 bytes、`root_daemon` 9304 bytes |
| ② 安裝 helper | 腳本 **1957 bytes / 58 行**、行程常駐、bridge 目錄 `drwxrwxrwx`、PID 檔寫入 |
| ③ 觸發提權 | 信箱 `last.cmd` → `payload.staging`（helper 已取件）→ progress `started` → `waiting-done` → **`last.out = OK`**（約 139 秒） |
| 🚀 一鍵取得 root | `Boot 1/6, trigger 1/2` → progress `started` → `waiting-done` → `Root achieved! Verifying`（約 6–8 分鐘，**兩次皆 attempt 1 成功**） |
| ✓ 驗證 root | `uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0` + `Permissive`；`last.done` 含 `__GHOSTLOCK_PAYLOAD_DONE__` |

## 7. 本輪發現並修正的缺陷

| # | 嚴重度 | 缺陷 | 症狀 | 修正 |
|---|---|---|---|---|
| 1 | **critical** | 裝置資訊面板格式字串有 **14 個 `%ls` 卻只給 11 個引數** | 按「重新整理」讀到裝置後 **APPCRASH 於 `msvcrt.dll`（c0000005）** | 格式字串改為 11 個 `%ls`；並擴充靜態檢查涵蓋**字面格式字串的 printf 家族**（GCC 的 `-Wformat` 對 MinGW 的 `__mingw_swprintf` 不生效，因此漏檢） |
| 2 | **critical** | `run_process()` 的 stdin 管線**方向弄反**（保留寫端卻在寫入前關閉，再對讀端 `WriteFile`） | `adb shell` 立刻收到 EOF → **② 安裝 helper 寫出 0 bytes 空腳本、自訂 root 指令完全無作用**，畫面僅顯示「無輸出」 | 關閉父行程讀端、改寫入寫端，寫完關閉以送 EOF（此缺陷**自 v1.0 即存在**） |
| 3 | medium | 視窗以「視窗尺寸」建立但版面用「客戶區座標」 | 200% 縮放下客戶區比預期少 **41 px**，**狀態列被截掉** | 改由 `WM_SIZE` 驅動的 `ui_layout()`，並以工作區夾限視窗 |
| 4 | medium | 操作結束後狀態列**停在「執行中…」** | push／helper／trigger／靜音／隱私燈等未自行設定終態 | worker 結束時補「就緒」；取消時顯示「已取消」 |
| 5 | medium | `pkill -9 -f 'ghostlock_bridge_helpe[r]'` **殺掉自己的 adb shell** | adb 回傳 **137**（128+SIGKILL），步驟被誤判失敗。字元類別在此 toybox 版本擋不住自我匹配 | 改用 **PID 檔**：啟動時寫 `$!`，停止時 `kill -9 $(cat …)` |
| 6 | low | 無 stdout 的指令被誤報「(無輸出，可能失敗)」 | 安裝 helper 時出現 5 行假警報（mkdir/chmod 本就不輸出） | 改看 exit code；另修正「無輸出時 `out` 為 `NULL` 而非空字串」使條件式失效的問題 |
| 7 | low | 對話框標題顯示**字面 `v%ls`** | `T(STR_APP_TITLE)` 只查表不格式化（3 處） | 改用 `Tf(STR_APP_TITLE, ZL_VERSION)`；新增靜態檢查：`T()` 取用含格式符的字串即報錯 |
| 8 | low | 一鍵成功後**裝置面板未更新** | 面板仍顯示「root：未執行」，與狀態列「root ✓」矛盾 | 一鍵成功後自動呼叫 `do_refresh()` |

## 8. 裝置端既有問題（非本工具造成）

測試初期 adb 出現 `error: fork failed: Try again`（exit 126），追查發現裝置上有約
**1554 個先前 session 殘留的 `ghostlock_rv101` 行程**：

| 指標 | 清理前 | 清理後 |
|---|---|---|
| `ghostlock_rv101` 行程數 | ~1554 | 1 |
| load average（**4 核**；裝置為 4×Cortex-A55） | **69.98 / 102.58 / 55.42** | **0.58 / 0.80 / 0.45** |
| MemFree | 338 MB / 1.8 GB | 731 MB |

以 `ps -A -o PID,ARGS | grep ghostlock_rv10[1] | awk '{print $1}' | xargs -r kill -9`
清除（**不用 `pkill -f`**，理由見 §7-5）。清除後 root 與 Shizuku 均保持常駐。

> 建議：`deploy/*` 的收尾步驟應確保每次 session 結束都清掉殘留 exploit 行程；
> 本工具已在 helper 迴圈中加入 `pkill -9 -f ghostlock_rv10[1]`，但那是相對於
> helper 自身流程，手動 session 的殘留仍需另行清理。

## 9. 已知限制與未測項

- **OTA 修補檢查只做版號比對**，不下載、不反組譯 OTA 映像。權威判定仍需
  `deploy/ghostlock_patch_check.py`（`payload-dumper-go` + `vmlinux-to-elf` + capstone）。
- **未測**：OTA 映像下載與反組譯路徑、WiFi 媒體通道、眼鏡端 App 廣播（`DebugReceiver`）、
  Shizuku 引擎路徑（本次 ③ 走 bridge helper 路線）。
- **未測**：惡意／損毀輸入、多裝置同時連線時的切換、長時間（>1 小時）連續操作。
- exploit 成功率高於預期（本次 3 次提權皆 attempt 1 成功），但仍為機率性；
  文件中的 ~50% 為保守估計。

## 10. 產出物位置

| 項目 | 路徑 |
|---|---|
| 工具原始碼與可執行檔 | `C:\Users\<USER>\Documents\codebase\ZeroLightRokidDesktop`（31 檔） |
| Linux 端開發副本 | `~/ZeroLightRokidDesktop` |
| 截圖 | `reports/zero_light_desktop_ui/` |
| 工具說明 | 該目錄的 `README.md`；i18n 維護見 `docs/i18n.md`；macOS 移植評估見 `docs/macos-port-feasibility.md` |

## 11. 對照關係（本工具 ↔ rokid_root 既有腳本）

| GUI 按鈕 | 對應專案腳本 |
|---|---|
| ① 推送 exploit | `adb push ghostlock_rv101` + `root_daemon` |
| ② 安裝 helper | `deploy/root_bridge.sh` 的 helper 安裝／啟動段 |
| ③ 觸發提權 | `deploy/trigger_root_daemon.cmd` → `last.cmd` |
| 🚀 一鍵取得 root | `deploy/get_root_now.sh`（`MAX_BOOTS=6`、`TRIGGERS_PER_BOOT=2`、`ROUND_TIMEOUT_S=950`） |
| ✓ 驗證 root | `deploy/run_root.sh 'id; getenforce'` |
| 🔒 鎖定 OTA | `deploy/lock_ota.sh` |
| OTA 修補檢查 | `deploy/ota_api.py`（上層版號比對） |
| 靜音拍攝 | `reports/media/silent_camera.ogg` + bind mount |
| 抑制隱私燈 | `reports/RV101_CAMERA_LED_FLASH_MECHANISM.md` 的 Binder + sysfs 做法 |
