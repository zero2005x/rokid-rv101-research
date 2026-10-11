# ZeroLightRokid Desktop — Windows 原生 LPE 輔助工具（GUI）v3.0

> English: [README_EN.md](README_EN.md)

> 純 Win32 API（C 語言）撰寫的桌面板工具，把 Rokid 眼鏡（RV101）的 GhostLock
> （CVE-2026-43499）root 流程包成按鈕操作。**無需 bash / Python / 任何 runtime**，
> 只要 Windows + `adb.exe`。
>
> ⚠️ 僅限**自有／授權設備**的安全研究使用。exploit 每次開機約 50% 成功率，
> 失敗會造成裝置重開機（屬正常現象）。root 僅存在於記憶體，**重開機即消失**。

---

## v3.0 新增內容

介面從「堆疊原生 Win32 控制項」改寫為**自繪的品牌介面**，沿用 `app-phone` 的
設計語彙（深色 `#0B1220` / 主色 `#22D3EE` / 輔色 `#FFB300` / 14px 圓角卡片）。

- **四張卡片**：裝置／主要／工具／紀錄。只有 4 個真正的子控制項
  （adb 路徑、序號、root 指令、紀錄），其餘全部自繪。
- **開關取代成對按鈕**：OTA 鎖定、靜音拍攝、抑制隱私燈、Shizuku 各一個開關，
  **開關位置本身就顯示裝置現況**，不必先按「查詢」才知道。
- **單一主要動作**：畫面上唯一的視覺重心是那顆青色大按鈕；已 root 時它
  變成狀態面板，「重新取得 root」降為次要按鈕。
- **即時狀態**：背景每 30 秒輪詢（僅閒置時），開機後 1.5 秒先查一次。
- **深／淺雙主題**：跟隨 Windows `AppsUseLightTheme`，標題列可手動切換，記憶於 `settings.ini`。
- **紀錄分級上色**：預設收合只顯示上次結果與最近兩行；**失敗自動展開**。
- **手動模式**：① ② ③ 收進「手動模式」展開列，日常只需一鍵。

### 開機迴圈保護（2026-09-13 新增）

App 側的 `ArmPolicy` 只保護 App 的自動武裝路徑，**桌面板走自己的 helper 完全繞過它**。
一次「一鍵取得 root」最多 12 次嘗試、每次約 50% 機率 panic 重開機，
所以一次點擊就可能造成連續重開。本版補上：

- **跨啟動斷路器**：連續 2 次「武裝後 ≤10 分鐘就重開」→ 中止並說明原因（狀態存 `settings.ini`）
- **一鍵前迴圈預檢**：最近 10 分鐘重開 ≥2 次 → 警告；**警告但允許手動覆寫**
- **嘗試次數顯示**：每次嘗試都印「第 N / 12 次」
- **供電警告**：沒插線時提醒（panic 重開時無法觀察也無法介入）

同時修掉驗證時抓到的**三個真缺陷**：

1. **警告對話框會跑到畫面外** —— 主視窗比螢幕大時，`MessageBox` 以視窗中心定位，
   使用者看不到警告卻仍被 modal 卡住。改為中心不在畫面內時以螢幕置中。
2. **孤兒 helper 會持續重跑 exploit** —— 工具被強制結束後 helper 變成孤兒仍依
   `last.cmd` 重試（實測留下 515～996 個程序、裝置反覆重開機）。
   改為**先殺 helper（斷源）→ 再殺 exploit → 最後清觸發檔**。
3. **提權後的殘留殺不掉** —— exploit 成功後行程屬於 uid 0，`adb shell`（uid 2000）
   對它回 `EPERM`。改為還有殘留且 root 通道可用時，**升級到 root 身分**再清一次。

詳見 [`docs/desktop-loop-protection.md`](docs/desktop-loop-protection.md)。

### v3.0 修正的實際缺陷

| 嚴重度 | 問題 | 修正 |
|---|---|---|
| **critical** | `device_state()` 一律取 `adb devices` 的第一台並**覆寫**設定檔序號 → 眼鏡重開機期間會安靜地把操作目標換成同時連線的手機，後續 reboot／exploit 全打到錯誤裝置 | 有指定序號時**只接受該序號**；找不到就回報並**拒絕對其他裝置做任何操作** |
| high | 寬格式字串誤用 `%s`（MinGW 下為 `char*`）→ `swprintf(v,24,L"v%s",L"3.0.0")` 只印出 `v3` | 改用 `%ls`，並在 `check_logfmt.py` 新增第四類靜態檢查 |
| high | 背景輪詢的「靜默模式」連錯誤都吞掉 → 使用者永遠不知道為何按了沒反應 | 狀態**一改變就發聲**，只有狀態不變時才安靜 |
| medium | 繪製與版面計算各寫一份工具卡高度 → 命中區域與畫面不一致 | 兩處統一並加註 |
| medium | `g_dev.rooted` 被輪詢改變時未重算版面 → 開關被畫到卡片外 | `WM_INFO` 一律重算版面 |

## v2.0 新增內容

### 多國語系（14 語系，比照手機端 App）

介面支援 **14 種語言**，與 `rokid_root/android/app-phone` 的語系清單完全一致，
且術語沿用該 App 的既有翻譯以保持一致：

| | | | |
|---|---|---|---|
| 繁體中文 `zh-TW` | 简体中文 `zh-CN` | English `en` | 日本語 `ja` |
| 한국어 `ko` | Français `fr` | Deutsch `de` | Español `es` |
| Português `pt` | Русский `ru` | Українська `uk` | ไทย `th` |
| Tiếng Việt `vi` | العربية `ar` | | |

- **跟隨系統**：預設依 Windows 顯示語言自動選擇，也可在 UI 下拉選單手動指定，
  選擇會存入 `settings.ini`。
- **阿拉伯文自動 RTL 鏡像**（`WS_EX_LAYOUTRTL`），無需手動調整版面。
- **依語系自動選字型**：CJK / 泰文 / 阿拉伯文各有後備字型鏈，找不到時自動回退。
- 翻譯流程可重現：`tools/i18n_master.json` → `tools/gen_i18n.py` → `src/i18n_strings.h`，
  產生階段即驗證所有語系的 printf 格式符完全一致。詳見 [`docs/i18n.md`](docs/i18n.md)。

### 對齊最新研究（`rokid_root`）

| 新功能 | 為什麼重要 |
|---|---|
| **🔒 鎖定 OTA** | `pm suspend com.rokid.glass.ota`。**一次 OTA 就會永久毀掉這台裝置的漏洞**（1.25.012 已修補，A/B 防回滾無法降級）。這是本版最重要的安全功能。 |
| **OTA 修補檢查** | 直接查 Rokid OTA API（MD5 簽名），比對最新版本是否已達修補門檻。 |
| **韌體閘門** | 自動辨識 `1.22.009`（漏洞版）與 `1.25.012+`（已修補）；已修補時直接拒跑並提示，不浪費時間。 |
| **啟動 Shizuku** | 每此開機自動安裝／預授權／啟動 `libshizuku.so`，供眼鏡 App 的 Shizuku 引擎使用。 |
| **靜音拍攝** | 以 bind mount 覆蓋相機提示音（需 root，重開機後失效）。 |
| **抑制隱私燈** | Binder 通知 + sysfs 保持迴圈（需 root，重開機後失效）。 |
| **重開機偵測改用 uptime 回退** | 比對開機秒數是否倒退，比「裝置離線」可靠；並在重試前等待 `sys.boot_completed=1` 且 uptime > 150 秒，避開 exploit 自動觸發／OOM 視窗。 |
| **開機健康閘門** | 執行緒數 > 2500 時多等一輪，避免在系統忙碌時觸發。 |
| **原子寫入 `last.cmd`** | 先寫 `.tmp` 再 `mv`，避免 helper 讀到半截 payload。 |
| **`pkill` 字元類別** | 用 `ghostlock_rv10[1]` 而非 `ghostlock_rv101`，避免 pkill 自我匹配。 |
| **依官方 `get_root_now.sh` 參數** | 6 次開機 × 每次 2 回觸發，單輪 950 秒。 |

### 工程品質

- **修正 v1.0 的管線死結**：v1.0 先寫完 stdin 才讀 stdout，若子程序先行輸出
  超過管線緩衝區就會互相等待。v2.0 改為獨立讀取執行緒。
- **可取消**：一鍵流程最長可跑 80 分鐘，v2.0 的「取消」鈕可隨時中止。
- **完整遙測面板**：序號／型號／韌體／安全修補層級／Android／SELinux／身分／
  root／Shizuku／開機時間／韌體狀態，一次 `adb shell` 取回。
- **多裝置安全**：所有 adb 呼叫皆帶 `-s <serial>`；正確區分
  `device` / `unauthorized` / `offline`。
- **Per-Monitor V2 高 DPI** + v6 通用控制項（`src/app.manifest`），4K 螢幕不模糊。
- **紀錄可儲存／複製／清除**，另附進度條。
- 零警告編譯（`-Wall -Wextra`），x64 與 x86 皆可建置。

### 程式碼審查修正（v2.0 定稿前）

v2.0 完成後做了一輪獨立的對抗式程式碼審查（因為沒有 Windows／Wine 可實測，
靜態審查是唯一的驗證手段）。以下為查出並已修正的真實缺陷：

| 嚴重度 | 問題 | 修正 |
|---|---|---|
| **critical** | `g_busy` 在 worker 結束後從未歸零 → **整個程式只能操作一次，且視窗永遠關不掉**（X／Alt+F4 都無效） | `set_busy_ui()` 成為唯一的歸零點；忙碌時關閉視窗會先取消再於收尾後真正關閉 |
| **critical** | `WM_BUSY(1)` 從未送出 → 忙碌時「存檔／複製／清除紀錄」仍可點，而它們會讀取 worker 正在 `realloc` 的 `g_logbuf`（use-after-free） | `start_op()` 主動送出 `WM_BUSY(1)`；並把 `g_logbuf` 改為**只由 UI 執行緒維護** |
| **high** | `do_custom()` 在 worker 執行緒讀取視窗控制項，且緩衝區未初始化 → 控制項若已被銷毀，**會把堆疊垃圾當成 root 指令送到裝置** | 改由 UI 執行緒先抓取指令再交給 worker |
| **high** | `run_process()` 可能在讀取執行緒仍存活時返回並釋放緩衝區（懸空指標 + 二次釋放） | 關閉讀取端後**無限等待**執行緒真正結束才返回 |
| medium | 已翻譯的確認對話框字串未被使用，靜音拍攝／隱私燈抑制**沒有二次確認** | 接上 `DLG_SILENT_CONFIRM`／`DLG_LED_CONFIRM` |
| medium | 韌體「未知」時不擋也不問，直接跑 exploit | 未知版本要求確認（`DLG_FW_UNKNOWN_*`）；已修補直接擋下 |
| medium | 版面以「客戶區」座標計算，視窗卻用「視窗」尺寸建立 → **狀態列被截掉、右側控制項超出** | 以 `AdjustWindowRectEx()` 換算 |
| medium | Shizuku 的 APK 選擇路徑是死碼（沒有任何控制項有該 ID） | 改為按下「啟動 Shizuku」時若尚未指定 APK 就開啟檔案選擇 |
| low | WinHTTP 標頭未以 CRLF 結尾、MD5 失敗照樣送出空簽名、`snprintf` 截斷未檢查、`xstrdup` 失敗未擋、下拉選單在自己的通知中摧毀自己、`PostMessage` 失敗時字串洩漏 | 全部修正 |

### 實機執行測試修正（2026-09-11）

程式在 **Windows 11（build 26200）+ 實機 Rokid 眼鏡**上實際執行測試後，
又抓到兩個**只有在真實執行時才會顯現**的缺陷（靜態審查與編譯都無法發現）：

| 嚴重度 | 問題 | 症狀 | 修正 |
|---|---|---|---|
| **critical** | `run_process()` 的 stdin 管線**方向弄反**：保留寫端卻在寫入前先關閉，然後對「讀端」做 `WriteFile` | `adb shell` 立刻收到 EOF，**stdin 一個位元組都送不出去** → ② 安裝 helper 會寫出空腳本、自訂 root 指令完全沒作用。畫面只顯示「無輸出」而不易察覺 | 關閉父行程的讀端、改寫入寫端，寫完再關閉以送出 EOF |
| **critical** | 裝置資訊面板的格式字串有 **14 個 `%ls` 卻只給 11 個引數** | 按「重新整理」讀到裝置後，`vswprintf` 讀取不存在的可變引數 → **APPCRASH 於 `msvcrt.dll`（c0000005）** | 格式字串改為 11 個 `%ls` |

第二個缺陷之所以逃過 `-Wall`：MinGW 的 `__mingw_swprintf` 等函式未被 GCC 視為 printf
原型，`-Wformat` 不會檢查。因此 `tools/check_logfmt.py` 已擴充為**同時掃描字面格式
字串的 printf 家族呼叫**（目前涵蓋 144 個多語系呼叫 + 28 個字面格式呼叫）。

**完整流程實測（含 exploit 與重開機）**又抓到三個問題：

| 嚴重度 | 問題 | 症狀 | 修正 |
|---|---|---|---|
| medium | 狀態列在操作結束後**停在「執行中…」** | push／helper／trigger／靜音／隱私燈等操作不會自行設定終態，狀態列永遠停在「執行中…」 | worker 結束時若該操作未設定狀態，補上「就緒」；取消時顯示「已取消」 |
| medium | `pkill -9 -f 'ghostlock_bridge_helpe[r]'` **會殺掉自己的 adb shell** | adb 回傳 137（128+SIGKILL），步驟被誤判為失敗。字元類別在這個 toybox 版本擋不住自我匹配 | 改用 **PID 檔**：啟動 helper 時寫入 `$!`，停止時 `kill -9 $(cat ...)`，無比對問題 |
| low | 沒有 stdout 的指令被誤報為「(無輸出，可能失敗)」 | 安裝 helper 時連續出現 5 行假警報（mkdir／chmod／pkill 本來就不輸出） | 改看行程 exit code；另發現「完全無輸出時 `out` 是 `NULL` 而非空字串」，條件式原本永遠不成立 |
| low | helper 啟動偵測在滿載裝置上會誤判失敗 | 裝置上有大量殘留行程時，第一次 `ps` 查詢回 0 | 改為最多 5 輪 × 1.5 秒輪詢 |
| low | 對話框標題顯示字面 `v%ls` | `T(STR_APP_TITLE)` 只查表不格式化，版本沒有代入（3 處） | 改用 `Tf(STR_APP_TITLE, ZL_VERSION)`；並新增靜態檢查：`T()` 取用含格式符的字串即報錯 |
| low | 一鍵成功後裝置面板未更新 | 面板仍顯示「root：未執行」，與狀態列的「root ✓」矛盾 | 一鍵成功後自動呼叫 `do_refresh()` 更新面板 |

另外修正一個在高 DPI 下的版面問題：視窗依「視窗尺寸」建立但版面用「客戶區座標」
計算，實測在 200%% 縮放的螢幕上客戶區比預期少 41 px，**狀態列被切掉**。現在改為
由 `WM_SIZE` 驅動的 `ui_layout()`：紀錄區高度依實際客戶區計算，並以工作區夾限
視窗大小。

審查同時**確認正確**的重點：133 個多語系格式化呼叫的引數個數與型別全部相符；
寬 printf 一律使用 `%ls`（在 MinGW 的 ANSI stdio 下 `%s` 會取 `char*`，
這個坑已避開）；`run_process` 沒有任何重複關閉控制代碼；
`fw_classify` 的數值比較對零填充版號（`009`/`012`）是正確的選擇。

---

## 資料夾內容

```
ZeroLightRokidDesktop/
├── zero_light_desktop.exe     ← 主程式（可直接雙擊執行）
├── README.md                  ← 本文件
├── SHA256SUMS.txt             ← 交付檔案雜湊（sha256sum -c SHA256SUMS.txt 可驗證）
├── .gitignore                 ← 排除建置產物、settings.ini、__pycache__
├── src/
│   ├── zero_light_desktop.c   ← 完整原始碼（Win32 API）
│   ├── i18n.h                 ← 多語系執行期（偵測／查表／字型／RTL）
│   ├── i18n_strings.h         ← 【自動產生】14 語系字串表，勿手改
│   ├── theme.h                ← 主題色票與繪製基礎（深／淺）
│   ├── ui.h                   ← 自繪元件庫（卡片／按鈕／開關／狀態面板）
│   ├── ui_shell.h             ← 自繪外殼（版面、事件、對話框）
│   ├── app.rc                 ← 版本資訊與 manifest 資源
│   └── app.manifest           ← 高 DPI + v6 控制項宣告
├── assets/
│   ├── ghostlock_rv101        ← exploit 二進位（工具會自動 push 到眼鏡）
│   ├── root_daemon            ← root 常駐服務二進位
│   └── silent_camera.ogg      ← 靜音拍攝用的無聲提示音
├── tools/
│   ├── i18n_master.json       ← 翻譯主表（14 語系 × 241 字串）
│   ├── i18n_glossary.md       ← 手機 App 術語對照表（翻譯基準）
│   ├── gen_i18n.py            ← 產生 + 驗證 i18n_strings.h
│   ├── i18n_apply.py          ← 併入翻譯片段 / 補齊缺漏 / 查看進度
│   ├── check_logfmt.py        ← 檢查格式化呼叫的引數個數（編譯器抓不到的崩潰類別）
│   ├── verify_exe.py          ← 驗證編譯後的 exe 真的含有全部翻譯
│   ├── test_extended_paths.sh ← 驗證 Shizuku／DebugReceiver／WiFi 媒體通道
│   └── wintest/               ← GUI 自動化測試輔助（ZlTest.cs：視窗操作／截圖）
├── docs/
│   ├── desktop-loop-protection.md  ← 開機迴圈保護（斷路器／預檢／計數／供電警告）
│   ├── lease-and-keeper.md         ← 操作租約與 App keeper 的互動
│   ├── extended-path-tests.md      ← 延伸路徑實機測試（Shizuku／廣播／WiFi）
│   ├── i18n.md                     ← 如何新增／修改語言
│   ├── research-update.md          ← v2.0 對應的最新研究整理
│   ├── macos-port-feasibility.md   ← macOS 移植可行性研究
│   ├── screenshots/                ← 實機截圖（各語系、阿拉伯文 RTL）
│   └── ui-proposal/                ← v3.0 介面提案與實作截圖
├── build_mingw.sh             ← Linux/macOS 交叉編譯
├── build_mingw.bat            ← MinGW-w64 編譯（Windows）
└── build_msvc.bat             ← MSVC 編譯（Windows）
```

---

## 使用前準備

1. Windows 上裝好 **adb**（Android SDK Platform-Tools）。
   - 工具會自動依序尋找常見路徑；找不到時請在上方「adb 路徑」按「瀏覽…」指定，
     再按「套用」（會存 `settings.ini`）。
2. 眼鏡接 USB、`adb devices` 看得到。
3. 韌體必須是 **`1.22.009-20260710-150201`**（GhostLock 漏洞版）；
   **`1.25.012` 以上已修補，本工具無法提權**（工具會自動辨識並拒跑）。

> 💡 **拿到裝置的第一件事：按「🔒 鎖定 OTA」。**
> 官方 App 可能在背景把眼鏡升級到已修補的 1.25.012，而升級是**不可逆**的
> （A/B 防回滾，沒有公開降級包）。鎖定後狀態會跨重開機維持。

---

## 按鈕說明

### 主要流程

| 按鈕 | 做什麼 |
|---|---|
| 重新整理 | 偵測裝置：序號／型號／韌體／SPL／`id`／SELinux／root／Shizuku／開機時間 |
| ① 推送 exploit | 把 `assets/ghostlock_rv101` 與 `assets/root_daemon` 傳到眼鏡 `/data/local/tmp/` |
| ② 安裝 helper | 建立 bridge 目錄、寫入並常駐 helper 腳本（原生寫入，不需要 bash） |
| ③ 觸發提權 | 送出 payload（關 SELinux + 啟動 root_daemon）到信箱 `last.cmd`（原子寫入） |
| 🚀 一鍵取得 root | ①+②+③＋自動輪詢＋自動重試（最多 6 次開機 × 2 回觸發；重開機會自動等開機重來） |
| ✓ 驗證 root | 經 root_daemon socket 執行 `id; getenforce`，確認 `uid=0(root)` + `Permissive` |
| 查 exploit log | 顯示眼鏡上 `gl_run.log` 尾端 60 行 |
| 重啟裝置 | `adb reboot`（root 會消失，需重新取得） |
| 執行下方指令 | 自訂指令以 root 執行（例如 `ls /data`） |
| 取消 | 中止目前操作（一鍵流程可隨時停） |

### 進階工具

| 按鈕 | 做什麼 |
|---|---|
| 🔒 鎖定 OTA | `pm suspend com.rokid.glass.ota`，防止升級到已修補韌體（**強烈建議先按**） |
| 解除 OTA 鎖定 | `pm unsuspend` + `pm enable`，還原 OTA 功能 |
| OTA 修補檢查 | 查詢 Rokid OTA 伺服器，比對最新版本是否已修補（需要網路） |
| 啟動 Shizuku | 安裝／預授權／啟動 Shizuku server（每此開機需重跑一次） |
| 靜音拍攝 | 以 bind mount 覆蓋相機提示音（需 root） |
| 還原拍攝音 | 解除上述 bind mount |
| 抑制隱私燈 | 停用相機開啟時的隱私指示燈（需 root，僅限授權研究） |
| 還原隱私燈 | 還原隱私指示燈 |

### 紀錄

| 按鈕 | 做什麼 |
|---|---|
| 儲存紀錄 | 把完整紀錄存成 UTF-8 文字檔 |
| 複製紀錄 | 複製到剪貼簿 |
| 清除紀錄 | 清空顯示 |
| 關於 | 版本與適用韌體說明 |

### 語言

上方「語言：」下拉選單可切換 14 種語言，第一項為「跟隨系統」。
切換後介面立即重建（阿拉伯文同時切換為 RTL 版面），選擇存入 `settings.ini`。

---

## 典型流程（新手）

```
1. 接線 → 按「重新整理」        → 資訊區出現序號／韌體 1.22.009／uid=2000 Enforcing
2. 按「🔒 鎖定 OTA」            → 避免被升級到已修補的 1.25.012
3. 按「🚀 一鍵取得 root」        → 下方紀錄依序顯示 push / helper / trigger / progress
4. 看到「root 已達成！驗證中」+ `uid=0(root) ... Permissive` = 成功 🎉
5. 之後任何指令：輸入框打指令 → 按「執行下方指令」
```

> 若一鍵流程中途眼鏡重開機：**正常**（exploit 特性，約 50% 機率），
> 工具會自動偵測重開機、等待系統穩定後換下一輪。
> 全部 6 次開機失敗再按一次「一鍵取得 root」即可。

---

## 自行編譯

| 環境 | 指令 |
|---|---|
| Linux / macOS 交叉編譯 | `./build_mingw.sh`（32 位元：`./build_mingw.sh 32`） |
| Windows + MinGW-w64 | `build_mingw.bat` |
| Windows + MSVC | 在 Developer Command Prompt 執行 `build_msvc.bat` |

需求：MinGW-w64（`x86_64-w64-mingw32-gcc`）或 Visual Studio；
若要重新產生語系字串表則另需 Python 3（`src/i18n_strings.h` 已隨附，通常不需要）。

連結的系統程式庫：`gdi32 user32 comctl32 shell32 ole32 shlwapi comdlg32 winhttp bcrypt gdiplus uuid`。

### 驗證

```bash
python3 tools/gen_i18n.py --check      # 翻譯表：完整性、跨語系格式符一致、en 基準未被污染
python3 tools/check_logfmt.py          # 每個多語系格式化呼叫的引數個數是否與格式字串相符
python3 tools/verify_exe.py zero_light_desktop.exe
                                       # 編譯後的 exe 是否真的含全部 3,374 筆翻譯
```

`gen_i18n.py` 與 `check_logfmt.py` 已整合進 `build_mingw.sh`，每次建置都會自動執行。

`check_logfmt.py` 檢查的是一類**編譯器抓不到**的崩潰：格式字串來自執行期查表，
若呼叫端少給一個引數，`vswprintf` 會讀取不存在的堆疊內容。目前 146 個呼叫全數相符。

`build_mingw.sh` 使用 `-Wl,--no-insert-timestamp`，因此**同樣的原始碼會建出位元組
完全相同的執行檔**，`SHA256SUMS.txt` 可重現驗證。

---

## 已知限制

- **未做程式碼簽章**：SmartScreen 可能警告「未知發行者」，選「仍要執行」即可。
- 一次只執行一個操作（忙碌時按鈕會停用，「取消」維持可用）。
- 無 bash 依賴，但**仍需要 `adb.exe`**（工具本身不內建 adb）。
- **OTA 修補檢查只做版號比對**，不下載、不反組譯 OTA 映像。完整判定需要
  `payload-dumper-go` + `vmlinux-to-elf` + ARM64 反組譯（見
  `rokid_root/deploy/ghostlock_patch_check.py`），本工具刻意不做。
- 靜音拍攝與隱私燈抑制**重開機後失效**，需重新套用。

---

## macOS 移植

可行性研究見 [`docs/macos-port-feasibility.md`](docs/macos-port-feasibility.md)。
結論：**原生 AppKit 移植可行且成本不高**（約 10–15 個工作日；最小可行版本約 4 天），
但 **Wine 不可行**（相容層看不到 USB 上的 adb 裝置）。

---

## 責任聲明

僅供**自有／授權設備**的安全研究與滲透測試使用。
本工具內含可造成核心 panic 與裝置重開機的提權程式。
因使用本工具造成的任何影響由使用者自行負責。
