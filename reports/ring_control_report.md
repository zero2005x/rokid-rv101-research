# 戒指控制（Rokid 戒指／觸控戒指）— 研究成果與實作報告

- 日期：2026-09-12
- 目標：讓**戒指**能操控眼鏡拍照／錄影（取得 root 後尤佳），手機端與眼鏡端都要支援
- 狀態：**路徑 A（手機端攔截）與路徑 B（眼鏡端 root 直讀 evdev）皆完成並實機驗證**

## 1. 戒指辨識（研究結果）

| 裝置 | 位址 | 證據 | 用途 |
|------|------|------|------|
| **AIVELA_RP13EJ37** | `BB:C3:13:73:8A:32` | 手機唯一 GATT client 連線（`com.smartring`）；`/proc`‑level input device 具 **鍵盤＋滑鼠＋觸控板＋滾輪**（`REL_X/Y`、`REL_WHEEL/HWHEEL`、`ABS_X/Y 0–4095`、`BTN_TOUCH/BTN_MOUSE/BTN_RIGHT/BTN_MIDDLE`、`KEY_VOLUMEUP/DOWN`、`KEY_PLAYPAUSE`、`KEY_HOMEPAGE/BACK/FORWARD`） | 健康量測＋手勢控制（App 內有「啟用手勢辨識／觸控板三擊喚醒」頁） |
| **RGR06**（WVX「Rokid 智能眼鏡指環滑鼠」） | `D1:03:CB:ED:C7:97`（random address） | BLE 廣告：`name=RGR06`、**服務 0x1812（HOGP）**、**Appearance 0x03C2 = Mouse**、`flags=0x04`（LE only）、connectable、RSSI −35…−65 | 使用者要用的「官方 Rokid 戒指」 |

辨識方法（可重複使用）：手機 App 內建 **BLE 掃描器**（`RingScanner`）輸出原始廣告資料
（名稱／RSSI／service UUID／manufacturer ID／raw hex），並以 **GATT 探測**（`RingGatt`）
連線列舉服務。系統設定頁只看得到 MAC 的裝置，靠這兩步才能確認身分。

### 1.1 已知限制：RGR06 目前拒絕新 host

- 症狀：廣告正常（可連線、RSSI −35），但 **GATT 連線一律 `status 133`**、`createBond()`
  在 1.2 秒內 `BOND_BONDING → BOND_NONE`。
- 診斷：使用者確認此戒指**曾配對過另一台裝置（該裝置已不在/關機）**。BLE 周邊在持有舊綁定時
  常見行為是：只對舊 host 定向廣播／拒絕新 central 連線 → 這正是 133 的典型成因。
- 解法：依戒指說明書**重置／重新配對**（常見為長按觸控區 5–10 秒直到 LED 快閃；或上電觸發）。
  重置後 App 的 `WATCH_RING` 監看器會**自動完成配對**（`scanAndBond`）。
- 其他嘗試：手機系統設定頁也配不上；眼鏡端 BLE 掃描在第三方 App 取得 **0 筆結果**
  （手機同距離看得到），已加 location 權限仍無效 → RV101 這版對第三方 BLE 掃描有平台限制。
  因此**眼鏡端改走「手機配對＋手機轉發」或「戒指直連眼鏡（若系統層配對成功）」**兩條路。

## 2. 手勢對應（預設）

| 手勢 | 動作 | 說明 |
|------|------|------|
| 單擊 | **拍照** | 按下放開 < 400ms |
| 雙擊 | **錄影開始／停止** | 兩次點擊間隔 < 450ms |
| 長按 | **靜音拍攝切換** | 按住 ≥ 700ms（路徑 B 亦會走 SilentMode 實際掛載/還原） |
| 滾輪／滑動 | **切換眼鏡顯示資訊** | 循環：全部 → 連線+儲存 → 相機+指示燈 → 全部隱藏 |
| 三擊 | 未指定（保留） | 部分戒指用於喚醒 |

時間參數可調（`RingGestureDetector` 建構子；眼鏡端 `getevent` 版同樣以 400/450/700ms 判定）。

## 3. 路徑 A：手機端攔截（免 root）

- 戒指以 BLE HID 連到手機後，Android 會把它的按鍵／滑鼠事件送到**前景視窗**。
- `MainActivity.dispatchKeyEvent` / `dispatchGenericMotionEvent`（**dispatch 層**，不是 `onKeyDown`）
  攔截後交給 `RingController`；`RingGestureDetector` 辨識手勢 → `perform()` 執行動作
  （`Remote.capturePhoto()`／`startVideo()`／`stopVideo()`／`setSilent()`／`setDisplayConfig()`）。
- UI：**「設定」分頁「戒指控制」卡**（2026-09-13 起；遙控分頁只留拍攝相關）— 啟用開關、手勢說明、狀態（累計次數＋最近結果）、
  **手勢學習**（按下戒指按鍵→顯示 `KEYCODE_xxx (code)`→**選單選擇要綁定的動作**）。
- **可學習的鍵碼對應**：學習到的鍵碼可綁定 拍照／錄影／靜音／顯示下一組／顯示上一組／不動作，
  存於 `ring_ctrl` prefs（`map_<code>`），**自訂對應優先於手勢預設**；滑鼠鍵以
  `0x1000 + button` 表示（避免與 KeyEvent 撞號）。長按「手勢學習」鈕＝清除全部自訂對應。
- 重要設計：
  - 用 dispatch 層攔截：`onKeyDown` 只在「沒有 View 消費」時才被呼叫，戒指送 `ENTER` 時會被
    畫面上聚焦的按鈕吃掉（實測）。
  - **滑鼠式戒指的點擊走 `dispatchTouchEvent`**（不是 `dispatchGenericMotionEvent`——後者只有
    hover/scroll）：RGR06 這類 Appearance=Mouse 的戒指，其「點一下」是 `source=SOURCE_MOUSE`
    的 ACTION_DOWN/UP。因此另在 `dispatchTouchEvent` 依 `event.source` 攔截
    MOUSE/TOUCHPAD 事件，**手指觸控（touchscreen）完全不攔**（實測：`input mouse tap`
    → `TAP → 已拍照 …`；`input tap`（手指）→ RingController 日誌 0 筆）。
  - **不做事前連線檢查**：`Remote.request()` 內部會 `ensureConnected()` 自動重連；事前擋
    「眼鏡未連線」會讓閒置斷線後的手勢直接失敗（實測踩到）。

## 4. 路徑 B：眼鏡端 root 直讀 evdev（背景也能用）

```
root_daemon(root) ──setsid──> getevent -lt /dev/input/eventN >> /data/local/tmp/ring_events.log
        ▲                                                              │
        │ exec("tail -c +offset …") 每 400ms                          │
   App(untrusted) ── RingEvdevReader ── RingControlManager ── CameraController.takePhoto()
```

- 為什麼用 `getevent` 而非自製 native binary：App（untrusted_app）讀不到 dev/input，
  但 root daemon 可以；`getevent` 是裝置內建 evdev 讀取器，免去隨 APK 附帶 aarch64 二進位
  與其建置鏈（本機 WSL 無 aarch64 linker、NDK 為 Windows 版）。備援實作保留在
  `deploy/ring_input_daemon.c`（未採用）。
- 事件延遲 ≈ 輪詢間隔（400ms）；裝置以 `--name` 子字串自動挑選：**程式預設 `RGR`**（戒指），
  眼鏡觸控板則由 `ring_ctrl.pattern` prefs 設為 `TP`（本機現值）；文件寫「預設 TP」時指的是裝置 prefs 而非程式預設。
- 觸發的拍攝／錄影／靜音／顯示切換**全在本機完成**（不經手機），因此眼鏡單獨配戴也能用。
- **自動啟動**：提權成功後 `CaptureService` 會依 `ring_ctrl.enabled` 自動啟動戒指控制；
  **App 程序啟動時（`App.onCreate`）也會恢復**，並帶 `skipExisting` 從檔尾開始讀，
  避免重放停用期間的舊手勢。裝置名稱 pattern 存 `ring_ctrl.pattern`（預設 `RGR`）。
- **看門狗**：每 30 秒檢查 `pgrep getevent` 與輪詢迴圈，失效即自動重啟（Rokid 會積極回收背景
  App，沒有看門狗會讓背景控制悄悄失效）。
- **背景實測**：把眼鏡切到桌面（Launcher 在前景、我們的 App 在背景）後注入單擊
  → `TAP → 已拍照 IMG_1789151522171.jpg` ✅。
- **已知邊界**：若 Rokid 在背景期間**整個 App 程序殺掉**，該段時間內的手勢不會被消費
  （getevent 仍在 root 端記錄，但消費者不在）；程序下次被任何元件喚起時會自動恢復。
  若要連這種情況也支援，需把「手勢→觸發」也搬到 root 端（以 `am broadcast` 喚起 App），
  屬後續可選強化。

### 4.1 實作踩到的兩個坑（已在程式碼註解標明）

1. **`pkill -f` 自我比對自殺**：`pkill -f 'getevent -lt'` 會比對到執行它的那行 shell 指令
   （同一行後面就寫著 `getevent -lt …`），pkill 先殺掉自己 → 後續啟動全部沒跑。
   修正：停止與啟動**拆成兩次 exec**，並用 `[g]etevent` 括號技巧 + PID 檔。
2. **輪詢迴圈與 tick**：`while (pollJob?.isActive == true)` 在 `launch` 回傳前就執行 → 迴圈立即
   結束；且 `detector.tick()` 只在「有事件」時呼叫 → 需要等雙擊視窗的**單擊永遠不會成立**。
   修正：改用 `@Volatile running` 旗標，且每輪都 `tick()`。
3. **讀取位移越界（log 檔被重建）**：`start()` 會 `rm` 重建 log，但舊的位移存在 prefs 裡；
   若位移 > 檔案大小，`tail -c +offset` 永遠讀不到東西（手勢全無反應、也無錯誤）。
   修正：每輪 `pollNew()` 比對檔案大小，越界即歸零重讀。
4. **手勢判定要用「事件時間」而非「解析時間」**：同一批次若含多個手勢（例如程式被喚起後
   一次補讀），用解析時間會把時序壓縮成 0ms → 單擊被誤判為雙擊。
   修正：`getevent -lt` 的 `[  sec.usec]` 時間戳傳入 `RingGestureDetector`；
   並加上防護把「未來時間戳」夾到 now+1s（否則 `lastTapAt` 在未來，`tick()` 永不成立）。
5. **App 程序被回收後的手勢救援**：`start()` 改為「先看 log 檔 mtime」——10 秒內有活動就從
   **上次位移續讀**（喚起前一刻的手勢不會丢），閒置太久才跳到檔尾；
   位移持久化於 `ring_ctrl.reader_offset`。另加 **root 端 keep-alive**
   （`/data/local/tmp/ring_keepalive.sh`，每 15 秒檢查 App 是否存活，不在就以 root 的
   `am start-foreground-service` 拉回；`force-stop` 後無法喚起屬 Android 限制）。

## 5. 實機驗證

### 5.1 路徑 B（眼鏡端；root 直讀 evdev）

以合成 evdev 事件寫入 `ring_events.log` 觸發（`getevent` 本身已在 root 下實際讀取裝置）：

| 手勢 | 結果（眼鏡端 log） |
|------|-------------------|
| 單擊 | `TAP → 已拍照 IMG_1789148949298.jpg`（後續多輪回歸亦通過，如 `IMG_1789152626561.jpg`） |
| 雙擊 | `DOUBLE_TAP → 開始錄影` → `DOUBLE_TAP → 已存影片 VID_1789148960604.mp4` |
| 長按 | `LONG_PRESS → 靜音關閉` → `LONG_PRESS → 靜音開啟`（來回皆成功） |
| 滾輪 | `SCROLL_UP → 顯示切換為 連線+儲存` |
| 批次內雙擊（150ms） | `DOUBLE_TAP → 開始錄影`（驗證事件時間戳判定） |
| 長按（>700ms） | `LONG_PRESS → 靜音關閉` / `→ 靜音開啟` |

裝置列舉：`RING_DEVICE /dev/input/event1 ROKID,PSOC-TP-R`、`/dev/input/event0 qpnp_pon`。

### 5.2 路徑 A（手機端）

以 `input keyevent` 模擬戒指按鍵（`F1=131`：HID 鍵盤類；亦測 `MEDIA_PLAY_PAUSE=85`）：

| 手勢 | 結果（手機端 log） | UI 狀態 |
|------|-------------------|---------|
| 單擊 | `TAP → 已拍照 IMG_1789149429394.jpg` | `累計 14 次 · 最近：TAP → 已拍照 IMG_…jpg` |
| 雙擊 | `DOUBLE_TAP → 開始錄影` → `DOUBLE_TAP → 已存影片 VID_1789149438818.mp4` | 同上 |
| 手勢學習 | 按下 `F1` → 對話框標題 `選擇這個鍵碼的動作 / KEYCODE_F1 (131)` | 6 個動作可選 |
| 自訂對應 | 綁定 `F1 → 錄影` 後按 F1：`自訂鍵碼 KEYCODE_F1 (131) → VIDEO_TOGGLE` → `開始錄影` → `已存影片 VID_1789150240748.mp4` | 覆寫預設（原本 F1 = 拍照） |
| 清除對應 | 長按「手勢學習」後按 F1：`手勢 TAP → 動作 PHOTO` → `已拍照 IMG_1789150279362.jpg` | 回復預設 |
| **滑鼠來源**單擊 | `input mouse tap` → `TAP → 已拍照 IMG_1789153194857.jpg` | 模擬 RGR06 這類滑鼠戒指 |
| **滑鼠來源**雙擊 | 兩次間隔 150ms → `DOUBLE_TAP → 開始錄影` → `已存影片 VID_1789153234558.mp4` | — |
| 手指觸控（安全） | `input tap` → RingController 日誌 **0 筆** | 不會誤觸發 |

## 5.2.1 手機端背景限制（實測）

- 前景：`dispatchKeyEvent`／`dispatchTouchEvent` 可攔到戒指全部事件（已驗證）。
- 背景：Android 只把**媒體鍵**派送給「media button session」，其優先序由 PlaybackState 決定。
  本機實測（`dumpsys media_session`）：`Media button session is yt.morphe.noname.exe/YouTube playerlib`
  ——即使 YouTube Music 處於暫停，媒體鍵仍被它取走，我們的 `RokidRingControl` session 收不到
  （logcat 可見 `MediaSessionService: Sending KeyEvent … to yt.morphe.noname.exe`）。
- 本專案**刻意不搶佔**（假裝 STATE_PLAYING 會蓋掉使用者的音樂控制）；`RingMediaSession` 僅為
  「沒有其他媒體工作階段時」的盡力而為。
- 因此：**手機在背景時的可靠控制解是眼鏡端路徑 B**（root 直讀 evdev，不依賴手機前景與媒體鍵），
  這也正是本專案同時實作兩條路徑的原因。
- 另修一個副作用：原先啟用戒指控制會攔下**所有**按鍵（BACK 也被吃掉），現改為只攔
  戒指 HID 描述元常見的鍵（ENTER/DPAD_CENTER/媒體鍵/音量鍵/首頁鍵/F1–F12）＋自訂對應，
  其餘放行（實測：BACK 可正常返回、RingController 日誌 0 筆）。

## 5.3 對其他 BLE 裝置的影響（實測，重要）

- 觀察：長時間連續 `WATCH_RING` 掃描（LOW_LATENCY、每輪 6 秒、數十輪）期間，**AIVELA 健康戒指
  的 GATT/HID 連線中斷**（`mInputDevices: BB:C3:…:0`、`com.smartring Connections: 0`）；
  停止掃描後該裝置回到可重連狀態。
- 原因：手機單天線 BLE 在「持續主動掃描」時會與既有 BLE 連線搶佔射頻/排程，導致連線品質劣化
  甚至中斷（Android 端 `ScanSettings.SCAN_MODE_LOW_LATENCY` 尤其明顯）。
- 因此本專案的設計原則：**只在需要配對時做短掃描**（App 內「配對戒指」= 單次 15 秒），
  `WATCH_RING` 僅供開發/除錯且應縮短輪數；平日執行戒指控制**不需要掃描**（配對後的
  HID 事件由系統直接派送、眼鏡端則走 evdev），所以不影響 AIVELA 同步。

## 5.4 眼鏡端戒指控制的遠端開關（協定）

- 新增 `SET_RING`（手機→眼鏡，`{enabled, pattern?}`）：眼鏡端 `CommandRouter.setRing()` 會設定
  `ringControl.enabled` 並 start/stop，回 `{ringEnabled, ringRunning, ringDevice}`。
- `STATUS` 新增 `ringEnabled / ringRunning / ringDevice / ringLast`，手機端「戒指控制」卡即時顯示
  （**2026-09-13 更新：`ringLast` 已改為結構化 `ringLastKey` + `ringLastArg`**，見下方「結構化事件」）
  （例：`眼鏡端：執行中 · /dev/input/event1 · 已啟動（/dev/input/event1）`）並提供開關。
- 實測：手機 `SET_RING enabled=false` → 眼鏡 `RING_STATUS enabled=false running=false last=stopped/…`（眼鏡送代碼，手機顯示「已停止」）；
  `enabled=true` → `enabled=true running=true 已啟動` ✅

## 6. 操作方式（除錯指令）

```bash
ADB=/mnt/c/.../platform-tools/adb.exe

# 手機端（路徑 A）
# 以下 ADB 由 deploy/env.sh 提供（source deploy/env.sh 後使用 $ADB/$SERIAL）
$ADB -s <phone> shell am broadcast -n com.rokid.rc.phone/.DebugReceiver -a com.rokid.rc.phone.DEBUG --es cmd CONNECT
$ADB -s <phone> shell am broadcast -n com.rokid.rc.phone/.DebugReceiver -a com.rokid.rc.phone.DEBUG --es cmd SCAN_RING  --ei secs 15
$ADB -s <phone> shell am broadcast -n com.rokid.rc.phone/.DebugReceiver -a com.rokid.rc.phone.DEBUG --es cmd PAIR_RING  --es pattern RGR
$ADB -s <phone> shell am broadcast -n com.rokid.rc.phone/.DebugReceiver -a com.rokid.rc.phone.DEBUG --es cmd PROBE_RING --es mac <MAC>
$ADB -s <phone> shell am broadcast -n com.rokid.rc.phone/.DebugReceiver -a com.rokid.rc.phone.DEBUG --es cmd WATCH_RING --ei rounds 60 --es pattern RGR
$ADB -s <phone> shell am broadcast -n com.rokid.rc.phone/.DebugReceiver -a com.rokid.rc.phone.DEBUG --es cmd SET_RING --ez enabled false   # 遠端關閉眼鏡端戒指控制

# 眼鏡端（路徑 B）
$ADB -s <glasses> shell am broadcast -n com.rokid.rc.glasses/.DebugReceiver -a com.rokid.rc.glasses.DEBUG --es cmd RING_DEVICES
$ADB -s <glasses> shell am broadcast -n com.rokid.rc.glasses/.DebugReceiver -a com.rokid.rc.glasses.DEBUG --es cmd RING_START --es name RGR   # 眼鏡觸控板測試用 --es name TP
$ADB -s <glasses> shell am broadcast -n com.rokid.rc.glasses/.DebugReceiver -a com.rokid.rc.glasses.DEBUG --es cmd RING_STATUS
$ADB -s <glasses> shell am broadcast -n com.rokid.rc.glasses/.DebugReceiver -a com.rokid.rc.glasses.DEBUG --es cmd RING_STOP
```

> 眼鏡端 FGS 限制：顯示器休眠時 CaptureService 無法由背景啟動，`RING_START` 會偵測
> `graph.camera == null` 並**改用 root 啟動** CaptureService。

## 7. 待完成

1. **RGR06 配對**：需使用者依說明書重置戒指（見 §1.1）；重置後 `WATCH_RING` 會自動配對，
   路徑 A 即可用真正的戒指操作（目前路徑 A 已用模擬按鍵驗證）。
2. AIVELA 戒指：其手勢模組預設休眠（App 說明：**觸控板三擊喚醒**），喚醒後可用同一套
   路徑 A 攔截；`/dev/input/event7` 監看已就緒。
3. ~~進階對應~~：**已完成**（見 §5.2 可學習的鍵碼對應）。

---

## 結構化事件（2026-09-13）

眼鏡端原本把結果組成**中文句子**（`已拍照 IMG_….jpg`、`顯示切換為 連線+指示燈`）送給手機 →
手機設英文時會混語系，且眼鏡端只有 zh-TW 資源無法在地化。現改為：

| 層 | 變更 |
|----|------|
| 眼鏡 `RingControlManager` | 動作回傳 `RingOutcome(key, arg)`（如 `photo_ok` + 檔名）；顯示配置參數改為機器可讀旗標 `conn,led`；中文結果字串全部移除（logcat 改英文） |
| `shared` `Status` | `ringLast` → `ringLastKey` + `ringLastArg` |
| 手機 | `ui/RingText.kt` 把代碼對到手機語系字串（沿用既有 `ring_*` 資源＋5 個新 key）；`DebugReceiver` 新增 `TEST_RING_TEXT` 可列出全部代碼在當前語系的文字 |

實測（同一批代碼、切換手機語言）：`photo_ok` → 「已拍照 IMG_1.jpg」／「Photo captured: IMG_1.jpg」；
`display_switched`（arg `conn,led`）→ 「顯示切換為 連線+指示燈」／「Display switched to Connection+Indicator」。
