# 延伸路徑實機測試報告

> **本文件有兩次執行紀錄**：下方 §1–§3 是 2026-09-12 的原始基準（**20 通過 / 0 失敗 / 0 略過**）；
> 文件最後的「2026-09-13 複驗」是重開機後的完整重跑（**21 通過 / 0 失敗 / 0 略過**，
> 多一項是因為 `shizuku_server` 已在執行，第 1 段不再略過）。
> 該次也修掉了腳本裡一個**間歇性偽陰性**（root 偵測要求 `/proc/net/unix` 計數剛好等於 1）。

日期：2026-09-12
裝置：Rokid RG-glasses（`<DEVICE_SERIAL>`），韌體 `1.22.009-20260710-150201`
環境：Windows 11，2880×1620 @200%；adb 由 bash 直接呼叫
腳本：`tools/test_extended_paths.sh`（可重複執行，退出碼 0 = 全通過）

## 結果總覽

| 路徑 | 結果 | 檢查項 |
|---|---|---|
| 1. Shizuku 引擎 root 路徑 | ✅ 通過 | 4 / 4 |
| 2. DebugReceiver 廣播 | ✅ 通過 | 8 / 8 |
| 3. WiFi 媒體通道（眼鏡端） | ✅ 通過 | 6 / 6 |
| **合計** | **✅ 20 通過、0 失敗、0 略過** | |

## 先釐清歸屬

這三項先前被列為「桌面工具未測路徑」，實際上只有一項屬於桌面工具：

| 項目 | 歸屬 | 桌面工具的角色 |
|---|---|---|
| Shizuku 引擎 | **桌面工具功能**（`OP_SHIZUKU` → `do_shizuku()`） | 主要執行者 |
| DebugReceiver | 眼鏡 App（`com.rokid.rc.glasses`）的除錯介面 | 只能代送 adb 廣播 |
| WiFi 媒體通道 | 手機 ↔ 眼鏡之間 | 只能驗證眼鏡端 |

## 1. Shizuku 引擎 root 路徑

桌面工具的 Shizuku 開關會依序：確認 manager 已安裝 → 推導 `libshizuku.so` 路徑 →
預先授權 → 殺舊 server → 啟動 → 驗證。

| 檢查 | 結果 |
|---|---|
| Shizuku manager 已安裝 | ✅ `/data/app/~~iPe…/base.apk` |
| `libshizuku.so` 存在且可執行 | ✅（`do_shizuku()` 靠此路徑直接啟動，最易失敗的一步） |
| `moe.shizuku.manager.permission.API_V23` 已授權給 `com.rokid.rc.glasses` | ✅ |
| `shizuku_server` 執行中 | ✅ |

實際日誌：

```
--- 啟動 Shizuku server ---
  server 程式庫：/data/app/~~iPe…/lib/arm64/libshizuku.so
  預先授權 moe.shizuku.manager.permission.API_V23（略過眼鏡上的授權對話框）…
  啟動 server（約需 3～10 秒）…
info: killing old process...
info: apk path is /data/app/~~iPe…/base.apk
info: starting server...
info: shizuku_server pid is 7510
info: shizuku_starter exit with 0
[+] Shizuku server 已常駐（關機／重開機後需重跑一次）。
```

## 2. DebugReceiver 廣播

介面取自 `app-glasses/src/main/kotlin/com/rokid/rc/glasses/DebugReceiver.kt`：

- component `com.rokid.rc.glasses/.DebugReceiver`
- action `com.rokid.rc.glasses.DEBUG`
- `--es cmd <TOGGLE_PANEL|SET_DISPLAY|SET_LOCALE>`；`SET_DISPLAY` 另帶 `--ez conn/root/camera/storage/led`
- logcat tag `GlassesDebugReceiver`

| 檢查 | 結果 |
|---|---|
| `com.rokid.rc.glasses` 已安裝 | ✅ |
| manifest 註冊了 DebugReceiver | ✅ |
| `TOGGLE_PANEL` / `SET_DISPLAY` / `SET_LOCALE` 廣播送出 | ✅ ×3（皆 `Broadcast completed`） |
| logcat 確認三者在裝置端生效 | ✅ ×3 |

**測後已還原**：`SET_LOCALE` 不帶 `lang`（= 跟隨系統）、`SET_DISPLAY` 全部 `true`（= 預設）。

## 3. WiFi 媒體通道（眼鏡端）

### 重要發現：這條路徑不需要手機在場也能完整驗證

`WifiHotspot.kt` 的註解寫明：

> 觸發通道：RootDaemonClient（root，提權後）。**實測 adb shell(uid 2000) 也可執行
> start-softap**，但 App(untrusted_app) 直接呼叫 `cmd wifi` 會被權限拒絕。

且 `WifiServer`（TCP 8555）由 `CaptureService` 啟動，而 `CaptureService` 可用
`am start-foreground-service` 拉起。因此桌機端即可完整走完眼鏡側的鏈路。

| 檢查 | 結果 |
|---|---|
| 眼鏡 App 程序在執行 | ✅ |
| `CaptureService` 拉起後 TCP 8555 監聽 | ✅ |
| SoftAP 啟動、眼鏡位址 `192.168.43.1` | ✅ |
| **功能驗證：實際 TCP 連線成功** | ✅ |
| `WifiServer` logcat 確認接受連線 | ✅ |
| 測後還原：SoftAP 關閉 | ✅ |

功能驗證的裝置端日誌：

```
I WifiServer: wifi connection accepted from /192.168.43.1:58226
```

### 已知限制（來自研究文件）

本韌體 **STA 與 SoftAP 不可同時維持**，開熱點會把既有的 WiFi STA 踢掉；關閉熱點後
STA 自動恢復（`cmd wifi stop-softap`）。SoftAP 不跨 reboot。

## 順帶修掉的介面缺陷：Shizuku 開關每次跳 APK 選擇框

**問題**：`ui_invoke(EL_SHIZUKU)` 只要 `g_apk` 是空的就跳檔案選擇框 —— 即使 Shizuku
manager 早就裝好、根本不需要 APK。使用者每次按 Shizuku 都被打斷。

**修法**：把「Shizuku manager 是否已安裝」納入既有的 30 秒背景輪詢
（探測指令新增 `Z=` 欄位，`pm path moe.shizuku.privileged.api | wc -l`），
只在 `g_dev.shizuku_installed != 1` 時才跳框。

選擇這條路的原因：**避免在 UI 執行緒上呼叫 adb**（會與 worker 搶 adb 並卡住畫面），
而輪詢本來就在跑，加一個 `pm path` 幾乎不增加成本，也比 `dumpsys package` 快得多。

| 值 | 意義 | 行為 |
|---|---|---|
| `1` | 已安裝 | 直接啟動，**不跳框** |
| `0` | 未安裝 | 跳框讓使用者挑 APK |
| `-1` | 尚未探到（App 剛啟動／裝置離線） | 跳框（涵蓋短暫空窗，寧可多問也不要讓人無法安裝） |

**驗證**：

- `settings.ini` **刻意不含 `apk=`**（正是舊版每次跳框的條件），按下 Shizuku 開關 →
  `NO_DIALOG ✓`，日誌 `info: shizuku_server pid is 17978` / `shizuku_starter exit with 0`
- 偵測機制雙向驗證：不存在的套件 `Z=0`、已安裝的套件 `Z=1`

**後續補強**：設定浮層新增了 **Shizuku APK 欄位＋「瀏覽…」鈕**，
所以即使 Shizuku 未安裝，也能直接在設定裡指定 APK，不必依賴彈出式對話框。
因此現在的行為是：已安裝 → 直接啟動；未安裝 → 用設定裡的 APK 安裝
（若設定也是空的，才在按下開關時跳一次檔案選擇框）。

**過程中的教訓**：第一次修改時我改錯了檔案 —— `case EL_SHIZUKU` 位於 `ui_shell.h`，
我卻對 `zero_light_desktop.c` 做字串取代。Python 的 `str.replace` 在找不到目標時
**靜默跳過**，而專案照樣編譯成功，因此看起來像「改了但沒生效」。後續改動都加上
`assert old in s` 把關。

## 測試腳本的三個實質缺陷（第二輪發現）

這一輪重跑時第 2、3 段突然失敗，追查後發現**三個都是測試腳本自己的問題**，
不是裝置或 App：

### 1. `ROOTED` 判斷錯誤 —— 用的是管線退出碼，不是裝置端結果

```bash
ROOTED=0
sh_ 'grep -q root_daemon.sock /proc/net/unix' && ROOTED=1   # 錯
```

`sh_` 是 `timeout ... adb shell ... | tr` 管線，**退出碼幾乎永遠是 0**，
所以未 root 的裝置也被判定成已 root，接著去跑需要 root 的項目而失敗。
改成比對輸出：

```bash
if [ "$(sh_ 'grep -c root_daemon.sock /proc/net/unix; true')" = "1" ]; then ROOTED=1; fi
```

### 2. DebugReceiver 廣播要求 App **正在執行**

App 沒在跑時，`am broadcast` 仍回報 `Broadcast completed: result=0`，
但系統日誌同時出現：

```
W BroadcastQueue: Background execution Third-Party APP not allowed:
  receiving Intent { act=com.rokid.rc.glasses.DEBUG ... } to .../.DebugReceiver
```

**廣播被背景執行限制丟掉，接收器根本沒被叫起來**，logcat 自然什麼都沒有。
這是很陰險的假成功：指令說成功、實際上什麼都沒發生。
腳本現在會先確認 App 程序活著，並在仍失敗時主動提示這個警告。

### 3. `CaptureService` 必須由 MainActivity **進到前景**才會啟動

兩種直呼服務的写法都會失败：

| 指令 | 結果 |
|---|---|
| `am start-service -n .../.CaptureService` | `Error: app is in background uid ... idle change:cached` |
| `am start-foreground-service -n .../.CaptureService` | `Error: Not found; no service started.` |

原因在 `MainActivity.onResume()`（原始碼註解寫得很清楚）：

```kotlin
// activity 進入前景後才啟動 FGS，避免 Background start not allowed
startForegroundService(Intent(this, CaptureService::class.java))
```

所以正確做法是 `input keyevent KEYCODE_WAKEUP` ＋
`am start -W -n com.rokid.rc.glasses/.MainActivity`，讓 Activity 自己把服務拉起來。
實測如此一來 TCP 8555 才會開始監聽。

> 附帶結論：**WiFi 媒體通道與靜音拍攝的自動套用都需要眼鏡 App 在前景執行**。
> 這是 App 自身的生命週期設計，桌面工具不介入。

## 測試過程修掉的兩個測試腳本缺陷

1. **誤用 bash 專屬的 `/dev/tcp`** —— Android 的 `sh` 是 mksh，不支援 `/dev/tcp`，
   導致「TCP 連線失敗」的**偽陰性**（同一時間 logcat 卻顯示連線被接受）。
   改用 toybox 的 `nc`（已實測可用）。
2. **shellcheck SC1087** —— `"^$SER[[:space:]]*device"` 未加花括號。

## 測試副作用與還原

啟動 `CaptureService` 時，它依設計**自動套用了靜音拍攝**（6 個 bind mount）——
這是測試的副作用，非使用者設定。已透過桌面工具的「靜音拍攝」開關還原
（日誌：`changed=6 mounted=0`、`[+] 已還原相機提示音。`）。

## 最終裝置狀態

| 項目 | 狀態 |
|---|---|
| SELinux | `Permissive`（已 root） |
| `root_daemon` socket | 存在 |
| `shizuku_server` | 執行中 |
| SoftAP | 已關閉（已還原） |
| TCP 8555 | 監聽中（`CaptureService` 執行中） |
| 靜音拍攝 | 已還原為未開啟（0 個 mount） |
| OTA 鎖定 | 已鎖定 |

## 可重現性

```bash
./tools/test_extended_paths.sh            # 預設序號
./tools/test_extended_paths.sh <serial>   # 指定序號
```

腳本會自動判斷：未 root 時 Shizuku 與 SoftAP 段落標為「略過」並提示先跑一鍵 root，
不會誤報失敗。退出碼：0 = 全通過、1 = 有失敗、2 = 裝置未連線。

> adb 一定要直接執行。透過 PowerShell 包裝呼叫 adb 實測會逾時，曾因此誤判裝置離線。

---

# 2026-09-13 複驗（重開機後完整跑）

裝置先重新開機 → `deploy/get_root_now.sh` 取得 root（**boot 1 / trigger 1**）
→ `deploy/start_shizuku.sh` 啟動 Shizuku → 跑本測試。

| 段落 | 結果 |
|---|---|
| 0. 前置檢查 | ✅ 已連線、已 root（`Permissive`） |
| 1. Shizuku 引擎 root 路徑 | ✅ 4 / 4 |
| 2. DebugReceiver 廣播 | ✅ 8 / 8 |
| 3. WiFi 媒體通道（眼鏡端） | ✅ 6 / 6 |
| **合計** | **✅ 21 通過、0 失敗、0 略過**（`EXIT_CODE=0`）|

比原始基準（20/0/0）多一項，是因為這次 `shizuku_server` 已在執行，
第 1 段不再略過「server 已在執行」那一項。

## 本次修掉的一個**測試腳本偽陰性**（重要）

原始碼原本這樣判斷 root：

```bash
if [ "$(sh_ 'grep -c root_daemon.sock /proc/net/unix; true')" = "1" ]; then ROOTED=1; fi
```

**要求剛好等於 1 是錯的。** `/proc/net/unix` 對同一個 socket 會有
「listening」與「已連線」兩種 entry；只要有客戶端連著就會變成 2：

- 眼鏡 App 的 `RootDaemonKeeper` 每 30 秒探一次（`exec("id")`）
- root 側 keepalive
- adb 的 `nc -U`

實測 10 次取樣（每 3 秒）就出現過一次 `count=2`。於是**已 root 的裝置會被誤判成
「尚未 root」**，第 1 段整段被略過並回報失敗 —— 這種間歇性偽陰性最難查，
因為重跑一次就好了。

已改為 `-ge 1`，並在註解記錄原因。

> 同一支腳本在更早處已有「必須比對輸出而不是退出碼」的警告，
> 這次是同一個主題的另一個實例：**判斷「有沒有」時不要寫成「剛好等於某個數」**。
