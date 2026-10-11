# 桌面工具：開機迴圈保護（2026-09-13）

> English: [desktop-loop-protection_EN.md](desktop-loop-protection_EN.md)

> **為什麼桌面板也要做**：眼鏡 App 側的 `ArmPolicy` 只保護 **App 自己的自動武裝路徑**。
> 桌面工具走自己的 `_desktop` helper **直接**跑 exploit，**完全繞過**那道保護。
> 一次「一鍵取得 root」最多 `MAX_BOOTS(6) × TRIGGERS_PER_BOOT(2) = 12` 次嘗試，
> 每次約 50% 機率 kernel panic → 重開機，所以一次點擊就可能造成連續 5–6 次重開機。

## 1. 四項保護與「本來就有」的部分

| 項目 | App 側 ArmPolicy | 桌面工具 |
|---|---|---|
| 開機後延遲 `uptime > 150s` | ✅ 有 | ✅ **本來就有**（`MIN_UPTIME_S`） |
| 每次開機重試上限 | ✅ 有 | ➖ 有上限但很大（12 次／一鍵） |
| 指數退避 | ✅ 5/15/60 分 | ❌ 無（但有隱性間隔：每輪要等開機 + uptime） |
| 僅充電時自動武裝 | ✅ 有 | ➖ **不適用**（純手動工具） |
| **開機迴圈斷路器** | ✅ 有 | ❌ **原本沒有 → 本次補上** |
| **一鍵前迴圈預檢** | ➖ | ❌ **原本沒有 → 本次補上** |

關鍵：桌面板**是純手動工具**（只有按鈕會動作；30 秒輪詢只讀狀態、不碰 exploit、不取租約），
所以「僅充電時自動武裝」這類**自動路徑**的保護沒有意義。真正缺的是
**「同一輪之內反覆重開」的記憶**。

## 2. 實作內容

### P0-a 跨啟動斷路器

- 判定：偵測到新的一次開機時，若「上次武裝」距該次開機 ≤ **10 分鐘**，記一次可疑；
  連續 **2** 次即跳脫。間隔夠久（正常使用下的重開）會把計數歸零。
- 跳脫後**中止本次執行**（`LOG_BREAKER_STOP`）。
- 開機識別用 `now - uptime`（＝開機時間點）。**不用 uptime 本身**：
  uptime 只在同一輪內遞增，跨啟動無法比較。
- **狀態存 `settings.ini` 的 `[state]` 段**，跨啟動有效 ——
  只存記憶體的話，使用者關掉再開工具就繞過了。

```
[state]
boot_wall=1789292955     ; 上次看到的開機時間點（unix 秒）
arm_wall=0               ; 上次開始嘗試的時間點
suspicious=0             ; 連續可疑重開次數
suspended=0              ; 1 = 斷路器跳脫
boot_hist=               ; 最近 8 次開機時間點（逗號分隔），供預檢用
```

### P0-b 一鍵前迴圈預檢

最近 10 分鐘內重開 ≥ 2 次，**或**斷路器已跳脫 → 跳警告對話框要求確認。

**依你的決定：警告但允許手動覆寫。** 按「是」會記錄
`LOG_LOOP_OVERRIDE` 並把斷路器歸零（之後仍可重新累計）；按「否」記錄
`LOG_LOOP_ABORTED` 並直接返回，**不執行任何 exploit**。

對話框用 `MB_DEFBUTTON2`，所以直接按 Enter 也會落在「否」。

### P1 嘗試次數顯示

每次嘗試都印出「第 N / 12 次」，讓使用者知道自己正在賭幾次 50%。

### P2 供電警告

未偵測到 USB／AC 供電時警告：panic 重開時沒插線就無法觀察也無法介入。
讀不到狀態時**不警告**（避免誤報）。

## 3. 新增 i18n

13 個新字串 × 14 語系 = 182 筆翻譯，全部寫齊（`gen_i18n.py` 要求每個 key 都要有全部
14 個語系，否則驗證失敗）。`verify_exe.py` 確認 **3,374 筆全部正確編入 exe**。

## 4. 驗證結果（實機 + 真實 exe）

| 測項 | 方法 | 結果 |
|---|---|---|
| 閒置時觀測開機 | 啟動工具，檢查 `settings.ini` | ✅ 寫入 `[state] boot_wall` |
| 可疑重開計數 | 植入「武裝後 60 秒重開」狀態 | ✅ `suspicious=1`、開機記入 `boot_hist` |
| **斷路器跳脫** | 再植入一次 | ✅ **`suspicious=2`、`suspended=1`** |
| 狀態不被啟動流程清掉 | 植入 `suspended=1` 後只啟動、不點擊，觀察 60 秒 | ✅ 維持 `suspended=1`（無任何寫入） |
| **預檢對話框會出現且持續** | **`serial` 指向不存在的裝置**（確保不可能跑 exploit），跳脫狀態下按一鍵 | ✅ 主視窗 `enabled=False` 連續 13 秒以上 |
| 中止不覆寫斷路器 | 同上，對話框存在期間檢查 `settings.ini` | ✅ `suspended` 維持 `1` |
| **root 升級清理** | 裝置上真實的 `uid=0 ppid=1` 孤兒（`adb shell kill` 回 EPERM）→ 按一鍵 | ✅ **孤兒被清除（相符數 0）** |
| 全部語系字串 | `verify_exe.py` | ✅ 3,374 筆全數編入 |
| 殘留清理指令 | 裝置上造出假 helper＋exploit＋觸發檔，跑同一條指令 | ✅ 殺掉 helper 與 exploit、清掉 `last.cmd`／`payload.staging` |

> ⚠ **為什麼「預檢對話框」要用不存在的 serial 測**：對真實裝置做座標點擊時，
> 主卡片版面會隨「已 root / 未 root」改變（見 §6.1），
> 沿用固定座標會點到狀態面板而不是按鈕，得到的結論是誤判。
> 改用不存在的 serial 後，**任何路徑都不可能真的執行 exploit**，
> 因此可以安全地反覆驗證對話框行為。真實裝置上的點擊測試則需先截圖確認版面。

## 5. 驗證過程抓到並修掉的兩個**真缺陷**

### A. 警告對話框會跑到畫面外（**高**）

`MessageBox` 預設以 **owner 視窗中心**定位。本工具的主視窗可以比螢幕還大
（實測：桌面 1440×810，主視窗 2386×1523 @ x=266 → 視窗中心 x≈1459 已在畫面外），
於是對話框被擺到畫面外 —— **使用者看不到警告，卻仍被 modal 卡住**，
比沒有警告更糟。

修法：新增 `confirm_dialog()`，owner 視窗中心若不在虛擬畫面內就改用無 owner 的
`MessageBox`，讓 Windows 以作用中螢幕為基準置中。既有的「關閉確認」也有同樣問題，
一併改用。同時把該對話框的預設按鈕改成「否」（`MB_DEFBUTTON2`）。

### B. 孤兒 helper 會持續重跑 exploit（**高**）

`cleanup_stray_exploits()` 原本**只殺 `ghostlock_rv101`**，不殺 helper。
若工具被強制結束／當掉（helper 的 PPID 變成 1）或一鍵跑到一半被取消，
**helper 會繼續依 `last.cmd` 重試** —— 清理永遠追不上（殺掉一批子程序，helper 又生一批）。

實測：強制結束工具後，裝置上留下 **515～996 個 exploit 程序、load 破百**，
裝置也因此反覆重開機。這與 App 側先前修掉的「重開機後 helper 被拉起就再跑一次」
是同一類問題。

修法：改為**先殺 helper（斷源）→ 再殺 exploit → 最後清觸發檔**
（`last.cmd`、`payload.staging`）。順序不能顛倒，否則清空信箱後 helper 可能又收到新觸發。

### C. 提權後的殘留殺不掉 → 改用 root 通道（**本次新增**）

`cleanup_stray_exploits()` 第一段走 `adb shell`（uid 2000），
但 **exploit 提權成功後它的行程屬於 uid 0**，shell 對它無能為力
（實測 `kill: Operation not permitted`）。裝置上就留了一個
`uid=0 ppid=1` 的 `ghostlock_rv101` 孤兒，第一段怎麼清都清不掉。

修法：第一段之後若**還有殘留**且 root 通道活著，就用 root 身分再清一次
（`root_cmd()`；先以 `id` 確認真的是 `uid=0` 才動手）。
這與 deploy 側 `proc_util.sh` 的 `kill_matching_escalating` 是同一個策略 ——
**只有 root 能收拾 root**。

**實機驗證（真實孤兒，非模擬）**：

| 步驟 | 結果 |
|---|---|
| 取得 root 後，裝置上有一個 `uid=0 ppid=1` 的 `ghostlock_rv101` | ✅ 存在 |
| `adb shell kill -9 <pid>` | ❌ `Operation not permitted`（重現問題） |
| 按一鍵（`cleanup_stray_exploits` 先執行） | ✅ **孤兒被清除，相符數 0** |

另外新增 `count_procs_matching()`：以 **host 端過濾**計數，避免裝置端
`grep -c` 把「執行那條指令的 sh -c」也算進去（本專案踩過多次）。

## 6. 已知限制

- ~~root 擁有的孤兒殺不掉~~ → ✅ **已修**（見 §5 C）：root 通道可用時會升級清理。
  若 root 通道**已死**，uid 0 的殘留仍然清不掉（沒有其他 root 途徑）——
  只能靠重開機，或先重新取得 root。
- **對話框的「取消」路徑未以自動化 GUI 完整驗證**：
  主視窗比螢幕大、按鈕是自繪（沒有子控制項可 `BM_CLICK`），
  而 `ZlTest esc` 是把 Esc `PostMessage` 到**主視窗**，modal 對話框收不到，
  不是有效的取消方式。已驗證的是「對話框會出現且擋住執行」與「程式碼只在 IDYES 時繼續」。
  **建議手動驗證一次**：跳脫狀態下按一鍵 → 對話框應可見 → 按「否」→ 紀錄應出現
  `使用者於開機迴圈警告處取消`，且裝置上不應有 exploit 程序。
- **重試次數仍是 12 次／一鍵**：本次沒有調整 `MAX_BOOTS`，只加了斷路器與預檢。
  若要更保守可考慮調降（會改變成功率與時間的取捨）。

## 6.1 自動化 GUI 測試的一個陷阱（**值得記下來**）

用 `tools/wintest/ZlTest.exe click X Y` 對主視窗做座標點擊時，
**座標不能寫死**：主卡片的版面會隨裝置狀態改變 ——

| 裝置狀態 | 主卡片內容 |
|---|---|
| 尚未 root | 一顆大的青色「一鍵取得 root」按鈕 |
| **已 root** | **狀態面板（「眼鏡已 root / Permissive · 已鎖定 · 防升級」）＋「手動模式」** |

實測：在「已 root」狀態下沿用「未 root」時的座標，點到的是**狀態面板**而不是按鈕，
於是「按了一鍵卻沒出現警告對話框」的結論完全是誤判。
（事後用截圖比對才發現。）

教訓：**座標式 GUI 自動化必須先截圖確認當下的版面**，不能假設版面固定。
此外 `ZlTest esc` 是把 Esc `PostMessage` 到**主視窗**，
modal 對話框收不到 —— 那不是有效的取消方式。

## 7. 如何重置斷路器

- 在警告對話框按「是」（＝覆寫）會自動歸零；
- 或直接改 `settings.ini`：`suspended=0`、`suspicious=0`、`arm_wall=0`。

## 8. 建置

```bash
./build_mingw.sh          # 產出 zero_light_desktop.exe（PE32+ GUI）
python3 tools/verify_exe.py zero_light_desktop.exe   # 驗證 14 語系字串都編進去
sha256sum -c SHA256SUMS.txt
```

`-Wl,--no-insert-timestamp` 讓同樣原始碼產生相同位元組，`SHA256SUMS.txt` 已同步更新。
