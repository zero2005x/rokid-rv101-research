# 通訊協定 — 手機 ↔ 眼鏡（藍牙 RFCOMM + WiFi TCP / JSON）

> 本文件描述**現行**協定（最後校對：2026-09-13）。歷史變更與研究過程見 `reports/`。

## 1. 傳輸層

- **兩條可用通道**：
  - 藍牙 Classic RFCOMM（SPP UUID `00001101-0000-1000-8000-00805F9B34FB`，service name `RokidCapture`）
  - WiFi TCP（眼鏡 SoftAP `RokidCam` → `192.168.43.1:8555`，眼鏡端 `WifiServer`）
- ⚠ **通道互斥（單一連線）**：眼鏡端 `AppGraph.connection` **只有一個**，`BluetoothServer.accept()` 與
  `WifiServer.accept()` 都會先 `graph.connection?.close()`（**後連上者勝**）。
  因此**任一條通道上的指令都會取代另一條**——手機端若用藍牙重連，就會踢掉 WiFi socket。
- **選路（手機端 `Remote.link()`）**：TCP 已連線時**控制與媒體都走 WiFi**；未連線時才用藍牙。
  `Remote.ensureMediaLink()` 會在媒體操作前**自動**建立 WiFi（`ENSURE_WIFI_AP` → 加入 AP → 連 TCP，
  受偏好 `auto_wifi_media` 控制，預設開），失敗才回退藍牙。
- 兩條通道使用**相同的 frame 協定與命令集**（眼鏡端 `ConnectionHandler` 同時服務 RFCOMM 與 TCP socket）。
- 指令與照片/影片傳輸都走同一串流（frame 協定不變）。
- 所有 JSON 一律 compact（無換行/多餘空白），UTF-8。

## 2. Frame 格式

每個 frame：

```
┌─────────────────────┬────────┬──────────────────────────┐
│ payloadLength (4B)  │ opcode │ payload                  │
│ 大端序 unsigned int │ 1 byte │ N bytes                  │
└─────────────────────┴────────┴──────────────────────────┘
```

- `payloadLength` = **1 + N**（即 opcode 與 payload 的總位元組數，不含自身 4 位元組）。
- `opcode`：
  - `0x01`：JSON 控制訊息（payload = UTF-8 JSON 字串）。
  - `0x02`：二進位資料塊（payload = raw bytes，屬前一個 `MEDIA_META` 宣告的傳輸）。

讀取方式：讀 4B 長度 `L` → 再讀 `L` 位元組 → 第 1 位元組為 opcode，其餘為 payload。

## 3. JSON 訊息信封

所有控制訊息皆為 JSON object：

```json
{ "t": "<type>", "id": 123, ... }
```

- `t`：訊息型別（下表）。
- `id`：請求序號（long，單調遞增）。回應以 `"replyTo"` 對應原請求 `id`。
- 其餘欄位依型別而定。

回應通用型別：

| `t` | 方向 | 欄位 | 說明 |
|-----|------|------|------|
| `ACK` | 眼鏡→手機 | `ok`(bool), `error`(str?, 失敗時), `data`(object?, 視命令) | 一般命令回應 |

## 4. 命令總表（手機 → 眼鏡）

| `t` | 欄位 | 說明 | 回應 |
|-----|------|------|------|
| `HELLO` | `protoVersion`(int) | 握手 | `HELLO_ACK` |
| `GET_STATUS` | — | 查詢狀態 | `STATUS` |
| `SET_GHOSTLOCK` | `enabled`(bool) | 開/關 ghostlock LPE | `ACK` |
| `SET_ROOT_MODE` | `mode`(str: `AUTO`\|`MANUAL`) | 設定 root 取得模式 | `ACK` |
| `ACQUIRE_ROOT` | — | 立即跑一次 LPE 取得 root | `ACK`(含 `rootAcquired`) |
| `SET_LED` | `on`(bool), `color`(str: `white`\|`green`\|`blue`\|`red`\|`all`，預設 `white`) | 開/關指定顏色指示燈 | `ACK` |
| `GET_DEVICE_INFO` | — | 查詢權限/SELinux/root/WiFi AP 狀態 | `DEVICE_INFO` |
| `ENSURE_WIFI_AP` | — | 確保眼鏡 SoftAP 已啟動（root 未就緒回 `ok:false`；AP 啟動慢，手機端輪詢） | `WIFI_AP_STATUS` |
| `SET_DISPLAY_CONFIG` | `display`(object: `conn`,`root`,`camera`,`storage`,`led` bool) | 設定眼鏡儀表板顯示內容（眼鏡端持久化） | `ACK`(含 `display`) |
| `SET_SILENT` | `enabled`(bool) | 切換靜音拍攝（強制壓制拍照/錄影提示音，bind-mount 靜音檔；需 root） | `ACK`(含 `enabled`/`applied`) |
| `SET_RING` | `enabled`(bool) | 開/關**眼鏡端**戒指控制（路徑 B：root 直讀 evdev；`enabled=false` 會停止 `getevent` 讀取迴圈） | `ACK`(含 `enabled`) |
| `ENABLE_WIFI_ADB` | — | 要求眼鏡開啟無線 adb：root 後設 `persist.adb.tcp.port`（跨開機）與 `service.adb.tcp.port`；本 build 的 adbd **不綁 TCP**，故回 `ok:false` + `error:"adb-over-tcp not supported on this build"`（非破壞性：不動 adbd） | `ACK`(含 `ip`/`port`/`persistPort`/`autoAfterReboot`) |
| `CAPTURE_PHOTO` | — | 拍一張照片 | `PHOTO_TAKEN` |
| `START_VIDEO` | — | 開始錄影 | `VIDEO_STARTED` |
| `STOP_VIDEO` | — | 停止錄影 | `VIDEO_STOPPED` |
| `LIST_ALBUMS` | — | 相簿列表 | `ALBUM_LIST` |
| `CREATE_ALBUM` | `name`(str) | 建立相簿 | `ACK`(含 `album`) |
| `RENAME_ALBUM` | `albumId`(str), `name`(str) | 更名 | `ACK` |
| `DELETE_ALBUM` | `albumId`(str) | 刪除相簿(含媒體) | `ACK` |
| `LIST_MEDIA` | `albumId`(str) | 相簿內媒體 | `MEDIA_LIST` |
| `GET_THUMBNAIL` | `mediaId`(str), `maxW`(int), `maxH`(int) | 縮圖 | `MEDIA_META` + chunks + `TRANSFER_END` |
| `DOWNLOAD_MEDIA` | `mediaId`(str) | 原檔下載 | `MEDIA_META` + chunks + `TRANSFER_END` |
| `DELETE_MEDIA` | `mediaId`(str) | 刪除媒體 | `ACK` |

## 5. 回應 / 推送（眼鏡 → 手機）

| `t` | 欄位 | 說明 |
|-----|------|------|
| `HELLO_ACK` | `protoVersion`, `deviceName`, `appVersion` | 握手確認 |
| `STATUS` | `ghostlockEnabled`(bool), `cameraBusy`(bool), `recording`(bool), `albumCount`(int), `mediaCount`(int), `storageFreeBytes`(long), `rootMode`(str), `rootAcquired`(bool), `ledOn`(bool), `silentEnabled`(bool), `silentApplied`(bool), `display`(object) | 狀態；`display`=眼鏡儀表板目前顯示配置（手機連線時讀回同步） |
| `PROGRESS` | `message`(str) | 長時操作（提權）進度推送 | 
| `DEVICE_INFO` | `info`(object: `selinux`, `id`, `uid`, `rootAcquired`, `ghostlockEnabled`, `rootMode`, `wifiAp`) | `GET_DEVICE_INFO` 查詢結果；`wifiAp` = `{up,ssid,pass,ip,port}` |
| `PHOTO_TAKEN` | `media`(MediaItem) | 拍照完成 |
| `VIDEO_STARTED` | — | 錄影開始 |
| `VIDEO_STOPPED` | `media`(MediaItem) | 錄影完成 |
| `ALBUM_LIST` | `albums`(Album[]) | 相簿 |
| `MEDIA_LIST` | `albumId`, `items`(MediaItem[]) | 媒體 |
| `MEDIA_META` | `mediaId`, `transferId`, `fileName`, `mime`, `size`(long) | 檔案傳輸開始 |
| `TRANSFER_END` | `mediaId`, `transferId`, `ok`(bool), `bytes`(long) | 檔案傳輸結束 |
| `WIFI_AP_STATUS` | `ok`(bool), `wifiAp`(object) | `ENSURE_WIFI_AP` 回應（`ok`=root 可用且已觸發；`wifiAp.up`=AP 已就緒） |
| `ERROR` | `message`(str) | 未配對的錯誤（`replyTo` 可選） |

## 6. 二進位檔案傳輸（眼鏡 → 手機）

1. 眼鏡收到 `GET_THUMBNAIL` / `DOWNLOAD_MEDIA` 後，先回一幀 `MEDIA_META`（opcode 0x01）。
2. 接著以 opcode `0x02` 分塊（每塊 ≤ 256 KiB）送出 raw bytes，**不附加其他欄位**；順序與 `MEDIA_META` 宣告一致。
3. 全部送出後，回一幀 `TRANSFER_END`（opcode 0x01）表示完成。
4. 手機以 `transferId` 對應組裝，寫入本地檔案，`TRANSFER_END` 校驗 `bytes`。

> 目前僅有眼鏡→手機的檔案傳輸；手機不上傳檔案到眼鏡。

## 7. 資料模型（JSON）

```jsonc
// Album
{ "id": "a1b2c3", "name": "旅行", "createdAt": 1720000000000, "mediaCount": 12 }

// MediaItem
{
  "id": "m9x8y7",
  "albumId": "a1b2c3",
  "type": "PHOTO",            // PHOTO | VIDEO
  "fileName": "IMG_20260824_193000.jpg",
  "mime": "image/jpeg",        // or video/mp4
  "sizeBytes": 2048000,
  "width": 1920,
  "height": 1080,
  "durationMs": 0,             // 僅 VIDEO
  "createdAt": 1720000000000,
  "thumbReady": true
}
```

## 8. 錯誤處理

- 命令失敗：回應 `ACK { ok:false, error:"..." }`，或 `ERROR { message }`。
- 未知 `t`：回應 `ACK { ok:false, error:"unknown_type" }`。
- 連線中斷：兩端各自清理；眼鏡端關閉該 connection 並回到 accept 狀態，相機/相簿狀態不變。
- 檔案傳輸中斷：手機丟棄不完整檔案；`TRANSFER_END.ok=false` 視為失敗。

## 9. 版本協商

- `HELLO.protoVersion` 與 `HELLO_ACK.protoVersion` 不一致時，手機提示版本不符並斷線。
- 目前 `PROTO_VERSION = 1`。

## 戒指事件（結構化）

`STATUS` 的戒指欄位**不傳翻譯文字**，只傳代碼與參數，由手機端組字（見 `app-phone` `ui/RingText.kt`）：

| 欄位 | 說明 |
|------|------|
| `ringEnabled` / `ringRunning` / `ringDevice` | 眼鏡端戒指控制開關、是否執行中、evdev 裝置路徑 |
| `ringLastKey` | 最近動作代碼：`photo_ok`／`photo_fail`／`video_start`／`video_saved`／`stop_fail`／`silent_on`／`silent_on_pending`／`silent_off`／`display_switched`／`camera_not_ready`／`no_action`／`no_device`／`start_failed`／`stopped`／`running` |
| `ringLastArg` | 參數：檔名、錯誤原因、手勢名、pattern，或顯示旗標清單（`conn,root,camera,storage,led`） |

## 進度事件（結構化）

`PROGRESS`（眼鏡 → 手機）不傳翻譯文字，只傳代碼與參數：

| 欄位 | 說明 |
|------|------|
| `code` | `GhostlockCodes` 之一，如 `gl_skip_lpe`、`gl_ready_timeout`、`gl_kernel_panic`、`gl_shizuku_unavailable` |
| `arg` | 參數（路徑／原因／次數），可為空 |
| `message` | `code\|arg`（保留給 logcat 與向後相容） |

手機端以 `ui/GhostlockText.kt` 對照到自己語系的字串。
失敗回應（ack error）同樣以 `code\|arg` 形式傳遞，由 `ProgressCodec.decode()` 解碼後顯示。

## root 狀態與提權度量（P0-b／P2）

`STATUS` 新增（皆為結構化代碼，手機端以自己語系顯示）：

| 欄位 | 說明 |
|------|------|
| `rootState` | `RootStates`：`root_ok`／`root_rearming`／`root_rearmed`／`root_lost_need_arm`／`root_lost_cooldown` |
| `rootHint` | 附加資訊：建議執行的腳本（多個以 `|` 分隔）或可觸發的引擎 |
| `suEngine` | 最近一次提權使用的引擎（`shizuku`／`bridge`／`none`） |
| `suLastOk`／`suLastMs` | 最近一次是否成功／耗時（毫秒） |
| `suAttempts`／`suSuccesses`／`suFirstTryOk` | 累計嘗試／成功／首次即成功（快速路徑不計入，避免美化） |
| `suKeeperChecks`／`suKeeperRearms` | daemon 看守檢查次數／自動重建成功次數（存 prefs，App 被殺不歸零） |

`PROGRESS` 新增引擎選擇代碼：`gl_engine_selected`（arg = 引擎名）、`gl_engine_shizuku_running`、
`gl_engine_bridge_resident`、`gl_engine_none`、`gl_engine_retry_other`。
