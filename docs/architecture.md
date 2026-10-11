# Rokid 雙 Android 遙控拍攝系統 — 架構設計

> English: [architecture_EN.md](architecture_EN.md)

> **用途聲明**：本專案用於**自有/已授權 Rokid 眼鏡設備**的安全研究與滲透測試。眼鏡端透過既有的 `ghostlock_rv101` LPE（本機提權）在 root 下控制相機隱私 LED，以驗證「隱私指示燈可被繞過」的風險。請勿用於未授權設備或侵害他人隱私。

## 1. 系統總覽

**雙通道設計**：**控制指令與媒體都優先走 WiFi TCP**（眼鏡 SoftAP `192.168.43.1:8555`；同一條連線、
同一個 `CommandRouter`），WiFi 不可用時才回退藍牙 RFCOMM。媒體操作前會**自動**建立 WiFi 通道
（`ENSURE_WIFI_AP` → 加入 AP → 連 TCP），受持久化偏好 `auto_wifi_media`（預設開）控制。

> ⚠ **兩條通道互斥**：眼鏡端 `BluetoothServer.accept()` 與 `WifiServer.accept()` 都會先執行
> `graph.connection?.close()`（後連上者勝）。因此若一般指令改走藍牙，會把 WiFi socket 踢掉、
> 媒體便靜默退回藍牙——這正是「傳輸一直在藍牙、10 秒影片極慢」的成因（見 FACTS §7 P28）。

```
┌──────────────────────────┐   (1) 控制：WiFi TCP 8555 優先，藍牙 RFCOMM 回退（JSON over length-prefixed frames）
│  手機端 App (app-phone)  │◄══════════════════════════════════════════════════════►
│  - 遙控/相簿/本機 UI      │   (2) 媒體：同一條 WiFi TCP 8555（眼鏡 SoftAP 192.168.43.1）
│  - 傳輸模式顯示/切換      │◄──────────────────────────────────────────────────────►
│  - 眼鏡顯示配置（5 chip） │                                                       │
│  - 多語系（14）/log 視窗  │        ┌─────────────────────────────────────────────┐
└──────────────────────────┘        │  眼鏡端 App (app-glasses)                    │
                                    │  ┌───────────────┐  ┌──────────────┐        │
                                    │  │BluetoothServer│─►│ CommandRouter│        │
                                    │  └───────────────┘  │ (分派指令)    │        │
                                    │  ┌───────────────┐  └───┬───┬───┬──┘        │
                                    │  │  WifiServer   │─────►│   │   │           │
                                    │  │ (TCP 8555)    │      │   │   │           │
                                    │  └───────────────┘      ▼   ▼   ▼           │
                                    │   CommandRouter → GhostlockEngine / Camera / │
                                    │                   MediaRepo / WifiHotspot /  │
                                    │                   DisplayConfigStore         │
                                    │        │ root payload（壓制 LED / 熱點）      │
                                    │        ▼                                     │
                                    │  /data/local/tmp/ghostlock_rv101             │
                                    └─────────────────────────────────────────────┘
```

- **眼鏡端**：常駐前台服務 `CaptureService`（`LifecycleService`）持有藍牙 RFCOMM Server、
  **WiFi TCP Server**、CameraX 相機、相簿索引與 Ghostlock 引擎。`MainActivity` 為
  **全透明底部橫幅資訊儀表板**（連線/Root/相機/儲存/指示燈，不遮視線；顯示內容可由
  手機遠端設定）。
- **手機端**：`MainActivity` + 數個 `Activity`（相簿/媒體/本地/檢視）；控制與媒體都由
  `Remote.link()` 選路（`TcpClient` 優先、`BluetoothClient` 回退），媒體通道由 `WifiLink` 管理，
  並由 `Remote.ensureMediaLink()` 在媒體操作前自動建立。

## 2. 模組劃分（Gradle 多模組）

| 模組 | 型態 | 職責 |
|------|------|------|
| `:shared` | Android Library | 通訊協定訊息模型、`FrameCodec`（長度前綴 framing，BT/TCP 共用）、`DisplayConfig`/`WifiApInfo` 模型、`Protocol` 常數 |
| `:app-glasses` | Android App | 眼鏡端：BT Server + WiFi Server(TCP 8555)、CameraX 拍照/錄影、相簿索引、GhostlockEngine（Shizuku/Bridge/Direct）、SoftAP 管理、儀表板 UI、多語系、LED 壓制 payload |
| `:app-phone` | Android App | 手機端：`LinkClient`（BT/WiFi 共用傳輸邏輯）、遙控/相簿 UI、眼鏡顯示配置、log 兩行預覽＋長按完整視窗、多語系、原檔下載與本地播放 |

共用協定放在 `:shared`，兩端匯入同一份程式碼，避免訊息欄位不一致。

## 3. 眼鏡端（app-glasses）

### 3.1 元件

**`CaptureService`（前台服務，LifecycleService）**
- 建立 `BluetoothServer`（RFCOMM，SPP UUID）與 `WifiServer`（TCP 8555）。
- 建立 `CameraController`，將 CameraX 綁定到服務的 lifecycle。
- 持有單例 `AppGraph`（`GhostlockEngine`、`MediaRepository`、`CameraController`、
  `WifiHotspot`、`DisplayConfigStore`）。
- `rootMode=AUTO` 時提權成功後自動確保 SoftAP（媒體通道）；前台通知避免被回收。

**`BluetoothServer` / `WifiServer` / `ConnectionHandler`**
- 兩個 server 對稱：accept 後把 socket 串流包成 `ConnectionHandler`，**共用同一套
  frame 協定與 `CommandRouter`**（傳輸介質透明）。
- 單一連線（新連線取代舊連線）；`AppGraph.btConnected` / `wifiConnected` 供儀表板顯示。

**`CommandRouter`**
- 依訊息 `type` 分派到對應 handler，產生回應（見 `protocol.md`）。
- 涵蓋：狀態/相簿/媒體、ghostlock/root、LED、`ENSURE_WIFI_AP`、`SET_DISPLAY_CONFIG`、
  `ENABLE_WIFI_ADB`。

**`CameraController`**
- CameraX 綁定 `ImageCapture`（拍照）+ `VideoCapture<Recorder>`（錄影）。
- 為相容 Rokid CHI HAL 的單流限制，採「單一 use case」：拍照只綁 `ImageCapture`、錄影
  rebind 為 `VideoCapture`（rebind 在主執行緒），不綁 Preview。
- ghostlock 開啟時**先壓制 LED 再啟動串流**（避免串流啟動瞬間閃一下），結束後還原。

**`GhostlockEngine`（LPE 呼叫層，三實作）**
- 介面契約：`isEnabled`、`rootAcquired`、`onProgress`、`suspend exec(payload)`、`suspend acquireRoot()`。
- `ShizukuGhostlockEngine`（**預設**）：經 Shizuku UserService 以 **shell（uid 2000）身份**
  直跑 exploit（`CommandService` + 自訂 AIDL）；內建 exploit 提前死亡偵測 + 內部重試×3；
  Shizuku 不可用時自動回退 `BridgeGhostlockEngine`（見 `docs/shizuku-feasibility-report.md`）。
- `BridgeGhostlockEngine`（備援）：委託 `deploy/root_bridge.sh` 常駐的 shell helper 跑 exploit
  + 置換 payload，App 用 `last.cmd/last.out` 通訊。
- `ShellGhostlockEngine`（Direct，僅研究）：App 直跑；實測 `untrusted_app` 會 kernel panic，不可用。
- `LedPayloads` / `LedSuppressor` / `RootDaemonClient`：LED 壓制與還原。提權後常駐
  `root_daemon`（UNIX socket，見 §5.1）；LED 指令**優先走 socket（毫秒級）**。

**`WifiHotspot`（SoftAP 管理）**
- root 後經 `root_daemon` 執行 `cmd wifi start-softap RokidCam wpa2 <pass>`（**背景化送出**，
  避免 wifi 服務轉換期阻塞）；以 `NetworkInterface("wlan2")` 偵測 AP 是否就緒（`192.168.43.1`）。
- 本裝置韌體 **STA 與 SoftAP 互斥**：開熱點會踢掉既有 WiFi STA，停熱點後 STA 自動恢復。

**`DisplayConfigStore`（儀表板顯示配置）**
- 五類開關（連線/root/相機/儲存/LED）持久化於 SharedPreferences；手機可遠端設定
  （`SET_DISPLAY_CONFIG`），變更通知一律在主執行緒派發；全隱藏時儀表板自動收成摺疊條。

**`MainActivity`（全透明資訊儀表板）**
- 底部橫幅、**無面板底色**（文字直接浮在視線上不遮蔽視野）；五區狀態色顯示；每 2 秒刷新。
- 點按橫幅（觸控板 tap 或 DPAD 中心鍵/Enter）切換展開/摺疊。
- `DebugReceiver`（`com.rokid.rc.glasses.DEBUG`）提供 `TOGGLE_PANEL`/`SET_DISPLAY`/`SET_LOCALE`
  自動化指令（**避免 input 注入**——實測有整機重啟風險）。

**`SilentMode` / `SilentPayloads`（靜音拍攝）**
- 以 root 把合法靜音 OGG `bind-mount` 覆蓋 `/product/media/audio/ui/` 的 6 個相機/錄影提示音檔
  （`camera_click`/`camera_focus`/`VideoRecord`/`VideoStop`/`rkd_record_start`/`rkd_record_end`）；
  `umount` 還原、reboot 自動清除。預設開啟，提權後由 CaptureService 自動套用，手機可 `SET_SILENT` 切換。
- 靜音檔由 `scripts/gen_silent_payload.py` 產生並內嵌（見 `reports/silent_capture_mechanism.md`）。

**`MediaRepository`**
- 實體儲存：`context.getExternalFilesDir(null)/media/albums/<albumId>/<file>`。
- 索引：`media/index.json`；縮圖快取 `cache/thumbs/<mediaId>.jpg`。
- 提供 `createAlbum/renameAlbum/deleteAlbum/listAlbums/listMedia/addMedia/deleteMedia`。

### 3.2 拍攝流程

```
[手機] CAPTURE_PHOTO ──► [眼鏡 CommandRouter] ──► CameraController.takePhoto()
                                   │
                     ghostlock 關：直接 ImageCapture.takePicture()   (LED 依 HAL 正常亮起)
                     ghostlock 開：led.suppress()（取消 lights_ctrl camera-open 事件 +
                                   sysfs 壓 0 + hold-loop）→ takePicture → restore
```

錄影同理：`START_VIDEO` →（ghostlock 開時**先 suppress 再 bindVideo**）→ `recorder.start`；
`STOP_VIDEO` → `recording.stop()` → 還原 LED → 回報 `VIDEO_STOPPED`。

> **閃爍修正（2026-09-09）**：隱私燈點亮源是 `lights_ctrl` 的 camera-open 事件（Binder），
> 單純寫 sysfs 會與事件互搶造成錄影閃爍。壓制改為在 Binder 層 `cancelEvent(2,2014)`
> 根治，並調整 suppress/bind 順序；詳見 `reports/RV101_CAMERA_LED_FLASH_MECHANISM.md` §9。

### 3.3 WiFi 媒體通道（SoftAP + TCP 8555）

- 拓撲：眼鏡提權後開 SoftAP（`RokidCam` / 固定 `192.168.43.1`），`WifiServer` 監聽
  TCP 8555；手機加入 AP 後連 `192.168.43.1:8555`。
- 手機端加入 AP 走 `WifiNetworkSpecifier` 系統彈窗（Android 12+ 需
  `CHANGE_NETWORK_STATE`；一次性確認，之後自動重連）；失敗時 UI 給出手動加入指引。
- 協定與藍牙完全相同（`ConnectionHandler` 複用），故媒體傳輸邏輯零修改。
- 實測：縮圖約 1.2s、5.4MB 照片完整下載成功；藍牙回退約 63KB/s（見
  `reports/wifi_transfer_test_report.md`）。

## 4. 手機端（app-phone）

- **`LinkClient`（傳輸無關基類）**：request/response 配對（`id` ↔ `replyTo`）與二進位檔傳輸
  （`MEDIA_META` → chunks → `TRANSFER_END`）。
  - `BluetoothClient`（RFCOMM 子類）、`TcpClient`（WiFi TCP 子類）。
- **`WifiLink`**：記錄眼鏡回報的 AP 資訊、引導手機加入 AP（`WifiNetworkSpecifier`）、
  管理 TCP 連線。
- **通道選路**：`Remote.link()` = `tcp.connected ? tcp : client`（WiFi 優先、藍牙回退），
  `request()` 與 `requestFile()` 都走它；`Remote.ensureConnected()` 在 WiFi 已連線時**不**重撥藍牙
  （否則會關掉 WiFi socket）。
- **媒體前自動建立 WiFi**：`Remote.ensureMediaLink()` → `ensureWifiMedia(apTries=6)`（請眼鏡開熱點 →
  加入 AP → 連 TCP）；以 `Mutex` 去重、失敗後 60 秒冷卻、失敗才回退藍牙。
- **UI 狀態**：`Remote.wifiMode`／`wifiNegotiating`／`onTransportChanged` 驅動三段顯示
  （WiFi 已連線／WiFi 連線中…／藍牙）；`LinkClient.onConnected` 於連線建立時觸發。
- **UI**
  - 「連線」：選取已配對的眼鏡、連線/斷線、**傳輸方式顯示（WiFi 已連線／WiFi 連線中…／藍牙）**、
    「WiFi 傳輸」開關（持久化偏好，預設開；關閉時把藍牙控制鏈路接回）。
  - 「遙控」：ghostlock 開關、拍照、錄影開始/停止、LED 開關與顏色 chips、
    **進階卡（root 模式/立即取得 root/裝置資訊/眼鏡顯示 5 chip + 全顯示/全隱藏）**、
    **操作紀錄（兩行預覽，長按彈出完整可捲動視窗，可選取複製/清除）**。
  - 「相簿」：相簿列表（建立/更名/刪除）、媒體縮圖列表、下載原檔、刪除眼鏡端媒體。
  - 「本地」：已下載媒體檢視/播放。
- **多語系**：14 語系（繁中預設 + 英/簡中/日/法/西/泰/越/韓/阿(RTL)/葡/俄/烏/德）；
  `LocaleHelper`（AppCompat per-app locale）+ debug 廣播 `SET_LOCALE`。
  手機端另有 **App 內語言選擇按鈕**（遙控分頁，「跟隨系統」＋14 母語名稱單選，選後 `recreate()` 立即生效）；
  手機端介面／Toast／對話框／操作紀錄／狀態摘要**全部走字串資源**（**203 key × 14 語系**（`scripts/i18n/phone_keys.tsv`；68 為初版數字），
  翻譯來源 `scripts/i18n/`，見 `reports/phone_i18n_localization_report.md`）。
  WiFi 媒體通道失敗原因以 `WifiSetupException(code)` 型別化回傳，由 UI 對應在地化訊息
  （不再以例外訊息字串比對判斷）。
- **手機端資訊架構（2026-09-12 重設計）**：底部導覽四分頁 **遙控／相簿／本機／設定**。
  - **遙控頁**（`fragment_remote.xml`）：**不使用外層 ScrollView**，一屏可見不需滾動 —
    連線狀態卡（`● 裝置名`＋選擇眼鏡／斷線＋`已連線`＋單一狀態列 `傳輸 · 相簿 · 媒體 · root · ghostlock`）、
    拍攝（拍照／錄影大按鈕）、快速開關（靜音拍攝、隱私指示燈）、**固定在下方且可捲動的操作紀錄**。
  - **設定頁**（`fragment_settings.xml`＋`SettingsFragment`）：連線（WiFi 傳輸／裝置資訊）、
    拍攝與權限（靜音／ghostlock／AUTO root／立即取得 root）、隱私指示燈顏色、眼鏡儀表板顯示內容、
    戒指控制（手機端／眼鏡端兩個開關＋狀態＋手勢學習／配對戒指）、外觀（淺色/深色）、語言。
  - **戒指事件為結構化**：眼鏡端只送代碼＋參數（`Status.ringLastKey` / `ringLastArg`，如
    `photo_ok` + 檔名、`display_switched` + `conn,led`），由手機 `ui/RingText.kt` 以**手機語系**組字
    （眼鏡端不再送中文句子，兩端語言可獨立）。
  - 操作紀錄由 `ui/LogStore` 共用：設定頁的動作也會寫進遙控頁那份紀錄（單一來源）。
  - **相簿頁**：工具列＝搜尋（SearchView 即時過濾名稱）／排序（名稱・項數・最新）／重新整理；
    相簿卡方形封面＋右上項數徽章＋品牌漸層底圖；封面**優先取照片**（影片縮圖常為黑畫面）。
  - **本機頁**：`已下載 N 本 · M 項 · 佔用 X` 摘要列；**長按進入多選**（批次列：已選 N 本／全選／刪除／取消）
    批次刪除只刪手機副本；工具列只有重新整理（搜尋/排序不適用）。
  - **設定頁分組為可折疊卡片**：點標題列展開/收合（箭頭旋轉 180°），狀態存 `settings_groups` prefs；
    預設只展開「連線」「拍攝與權限」，其餘（隱私指示燈／眼鏡顯示／戒指控制／外觀／語言）預設收合。
  - **相簿／本機為 3 欄方格**（`GridLayoutManager(context, 3)`）；卡片文字 13sp/11sp＋陰影維持小尺寸可讀性。
  - **主題（淺色／深色）**：`ThemeHelper`（prefs `app_theme/light_mode`，預設**深色**）於
    `MainActivity.onCreate` 建立前 `AppCompatDelegate.setDefaultNightMode`；「設定 → 外觀」可切換，切換即 `recreate()`。
    兩套品牌配色：深色＝`values-night/colors.xml`（深藍底 `#0B1220`＋亮青 `#22D3EE`＋琥珀 `#FFB300`）；
    淺色＝`values/colors.xml`（白底＋青 `#0E7490`＋琥珀 `#B45309`）。狀態列圖示隨模式切換（`windowLightStatusBar`）。
  - 統一樣式：`Widget.RokidPhone.Card`（14dp 圓角 1dp 描邊、無陰影，顏色隨日夜模式）、
    `Widget.RokidPhone.SectionTitle`、`Widget.RokidPhone.Hint`。
  - **配色（品牌色）**：深藍底 `#0B1220`、卡片 `#141D2F`、primary 青 `#22D3EE`、accent 琥珀 `#FFB300`、
    文字 `#E6EDF3`／次要 `#93A1B1`、描邊 `#24304A`；與 launcher icon／眼鏡透明儀表板一致，
    深淺色各自一套品牌色（`values/` 淺色：底 `#F4F6F9`、primary `#0E7490`、accent `#B45309`；`values-night/` 深色：底 `#0B1220`、primary `#22D3EE`、accent `#FFB300`）。

- **顯示格式**：`util/Format` 僅負責數字/時間格式；型別標籤改回傳字串資源 id（`typeLabelRes()`），
  由呼叫端 `getString()` 取在地化文字。
- **戒指控制**：兩條路徑 —（A）手機端 `RingController` 在 dispatch 層攔截戒指 HID
  （key／mouse／scroll）→ 手勢辨識 → 走既有 BT/WiFi 控制通道命令眼鏡；
  （B）眼鏡端 `RingControlManager` 由 **root daemon** 背景執行 `getevent` 直讀 evdev，
  輪詢增量後直接在本機觸發 `CameraController`（背景可用）。BLE 探索／配對／GATT 探測
  放在 `shared/ring/` 供兩端共用。
  詳見 `reports/ring_control_report.md`。

- **檔案下載**：收 `MEDIA_META` 後組裝 binary chunk 到本地檔，`TRANSFER_END` 確認；
  WiFi 通道下同樣流程、速度提升。

## 5. Ghostlock LPE 觸發層

`ShellGhostlockEngine` 已把 orchestrator 的完整觸發流程實作進 App 內：

```
launch（ProcessBuilder 執行 ghostlock_rv101，帶 env，輸出重導 app cache）
  → ready（輪詢 .ghostlock_root.sh 出現 "dropping to root shell"）
  → install（寫 .ghostlock_root.sh.new 後 rename 原子取代）
  → done（輪詢 payload log 出現 __GHOSTLOCK_PAYLOAD_DONE__）
  → cleanup（destroyForcibly 回收 exploit 行程）
```

參數集中在 `ExploitConfig`（`doneTimeoutMs` 實測調為 240s，涵蓋 exploit 100–142s 核心階段）。

**執行身份硬前提**：exploit 需以 `shell`(uid 2000) 執行；`untrusted_app` 直跑會 kernel panic
（需 `perf_event_open` 等 shell 權限）。因此三條觸發路徑：

1. **Shizuku**：Shizuku server 由 adb 啟動 → UserService 身份 = server 身份
   = `u:r:shell:s0`，等同 adb shell（詳見 `research/shizuku/shizuku_internals.md`）。
2. **Bridge**：`deploy/root_bridge.sh` 常駐 shell helper 迴圈讀 `last.cmd`。
3. **Direct**：App 直跑（**已不使用**，會 kernel panic，僅保留研究）。

**引擎選擇為執行期探測（2026-09-13 起，`AutoGhostlockEngine`）**：每次提權時檢查
`rikka.shizuku.Shizuku.pingBinder()`（Shizuku 優先）與 helper 心跳檔
`/data/local/tmp/ghostlock_bridge/helper.alive`（每秒更新，容忍 120 秒），選不到時回報
`gl_shizuku_not_running` 之類的結構化代碼。**不再有 `USE_ENGINE` 編譯期開關**，換環境不需重建 APK。
⚠ 探測**不可用 `ps`**：Android 的 App 看不到其他 process（實測 helper 在跑卻回 false）。
payload 的 DONE 標記路徑由 `engine.doneLog()` 決定，`LedSuppressor` 無需感知差異。

payload 內容不變：`LedPayloads` 以 marker 檔 + 背景迴圈壓制 LED，`restore` 還原；
SELinux 在 exploit 成功後為 `u:r:kernel:s0` + Permissive，payload 直接以 root 執行。

### 5.1 常駐 root daemon（單次提權 → socket 秒級 LED）

- 提權 payload（`LedPayloads.startRootDaemon`）以 root 執行：`echo 0 > /sys/fs/selinux/enforce`、
  確認 `/proc/net/unix` 無存活 socket 後 `setsid /data/local/tmp/root_daemon &`，並等 socket 就緒；
  同時寫 pid 檔、安裝 **keep-alive 迴圈**並把 daemon 與 keep-alive 的 `oom_score_adj` 設為 `-1000`（P0）。
- `root_daemon`（`deploy/root_daemon.c`）監聽 `/data/local/tmp/root_daemon.sock`（0777），
  一條連線 = 一個指令：`sh -c` 執行、stdout 寫回。fork-per-connection + 暫存檔輸出。
- `RootDaemonClient`（`LocalSocket`）執行指令；**永不拋出**（reboot 後 stale socket 檔會
  `Connection refused`，一律回傳空字串，避免 App 崩潰）。
- 好處：LED 指令毫秒級（實測 39–48ms）、0 次重跑 LPE。
- 注意：`root_daemon` 與 helper 都是記憶體常駐，**reboot 後消失**，需重新提權（adb 武裝 → `deploy/root_bridge.sh`
  或 `deploy/start_shizuku.sh`）。reboot 後手機端會顯示「root：已失效，需重新武裝」與要跑的腳本（P0-b）。
- **存活強化（P0）**：root 側 `root_daemon_keepalive.sh`（20 秒一輪，daemon 崩潰自動重建；實測 9–18 秒救回）
  ＋ App 側 `RootDaemonKeeper`（30 秒一輪，失效時清 stale socket、判斷可觸發性後自動重建，冷卻 5 分鐘）。
  兩者互補：root 還在時由 keep-alive 負責（不需 adb、不需重跑 exploit）；root 整體失效時由看守負責或回報需介入。

## 6. 執行緒 / 併發模型

- 眼鏡端：IO dispatcher 跑 BT/WiFi accept 與 socket 讀寫；`CameraController` 用 main executor 回呼；
  root payload 用 `ProcessBuilder`/Shizuku UserService + coroutine 等待；顯示配置變更通知在主執行緒。
- 手機端：`LinkClient` 在 IO dispatcher；UI 用 coroutine + callback；下載進度回報。

## 7. 安全與合規

- 藍牙 RFCOMM 明文；WiFi 通道為 WPA2 SoftAP + 明文 TCP（研究用途，未加應用層加密）。
  ⚠ WiFi 通道承載**控制與媒體**（見 §1），且 `WifiServer` 綁所有介面、`accept()` 任何來源、`CommandRouter` 無權杖、AP 密碼寫死於 APK（`Protocol.AP_PASS`）——詳見 FACTS §7 P10。
- 眼鏡端對連線**不驗證身份**：研究用途假設已配對可信。正式使用需加應用層配對/授權。
- 除錯廣播（`DEBUG`/`SET_LOCALE`/`TOGGLE_PANEL` 等）為研究期便利措施，正式版應移除或加權限。
- ghostlock 開關與隱蔽拍攝僅供授權研究；不實作任何針對第三方設備的規避/散佈能力。
- 原檔與縮圖傳輸採 binary chunk，避免 base64 33% 膨脹。

## 8. 版本選擇

- Kotlin 1.9.24、AGP 8.5.2、Gradle 8.7；`kotlin.compiler.execution.strategy=in-process`（本機 WSL 無 JDK daemon 權限）
- compileSdk/targetSdk 34、minSdk 26
- Shizuku API/Provider `dev.rikka.shizuku:api|provider:13.1.5`
- CameraX 1.3.4、Coroutines 1.8.1、AndroidX core-ktx/appcompat/material/recyclerview/constraintlayout
- 序列化：org.json（Android 內建，零額外 plugin，降低建置風險）
- 圖示：adaptive icon（vector + monochrome），見 `android/branding/README.md`

## 提權維運層（2026-09-13 新增）

| 元件 | 角色 |
|------|------|
| `ghost/RootDaemonKeeper` | root daemon 看守（30 秒）：stale socket 清理、探針判斷可觸發性、自動重建（5 分鐘冷卻）、狀態代碼回報 |
| `ghost/AutoGhostlockEngine` | 執行期引擎探測與委派（Shizuku ↔ bridge），含失敗換線重試 |
| `GhostlockCodes.ENGINE_*` | 引擎選擇原因代碼（結構化，手機端以自己語系顯示） |
| `shared/RootStates`、`shared/RootMetrics` | root 狀態代碼與提權度量（隨 `STATUS` 回報） |
| `app-phone/ui/RootText` | 狀態／提示／度量 → 手機語系文字 |
| `/data/local/tmp/ghostlock_op.lease` | **跨工具共用租約**（App 看守／`deploy/lease.sh`／ZeroLightRokid Desktop）：取得租約者才能動 exploit，避免同時改 `.ghostlock_root.sh` |
| payload `root_daemon_keepalive.sh` | root 側存活迴圈（daemon 崩潰自動重建）＋ `oom_score_adj=-1000`；由 App payload 或 `deploy/install_root_keepalive.sh` 安裝 |
| `ghostlock_bridge/helper.alive` | helper 每秒心跳（App 無法用 `ps` 看別的程序，改讀此檔判斷觸發管線是否常駐） |
