# AIVELA 戒指「連接失敗」— log 取證與處置

> English: [aivela_ring_connection_failure_2026-09-13_EN.md](aivela_ring_connection_failure_2026-09-13_EN.md)


- **日期**：2026-09-13 15:5x–16:0x
- **裝置**：眼鏡 RV101 `<DEVICE_SERIAL>`（Android 12 / API 32）· 手機 `<PHONE_SERIAL>`（Android 13 / API 33, MIUI V816）
- **戒指**：`AIVELA_RP13EJ37` = `BB:C3:13:73:8A:32`（[ DUAL ]，已配對）
- **方法**：兩端 logcat、`dumpsys activity services` / `bluetooth_manager` / `input`、App prefs（root）、
  App 自己的 debug 廣播與 UI 截圖。**未**變更藍牙配對、**未**刪除任何檔案。

---

## 0. 一句話結論

**戒指沒有連線問題——它早就連上、而且正在驅動拍照**（15:56 連續拍成 4 張）。
使用者看到的「連接失敗」是三件事疊起來的結果，其中**沒有一件是「配對／連線」本身**：

| # | 事件 | 時間 | 後果 |
|---|------|------|------|
| 1 | 眼鏡 App 被 **lowmemorykiller 殺掉**（RSS 100 MB） | 15:56:38 | 相機與服務全斷 |
| 2 | 服務重建但 **`startForeground()` 被背景限制拒絕** | 15:56:41 | 服務只是普通背景服務，隨時會被回收 |
| 3 | 重建後收到 **`START_VIDEO`**（戒指按鍵對應到錄影切換） | 15:56:45 | `bindVideo()` 把 `imageCapture = null` → **之後拍照必定失敗** |
| 4 | 錄影卡住（`Recorder` 等不到 video keyframe、沒有產檔） | 15:56:45→15:58 | 相機回不到拍照模式 |
| 5 | Android 以 **app idle** 停掉 `CaptureService` | 15:58:09 | `onDestroy` → `server.stop()` / `wifiServer.stop()` → **藍牙通道關閉** |
| 6 | 手機 `CONNECT`／`CAPTURE_PHOTO` 全部失敗 | 15:59 | 顯示「未連線」＝使用者認知的「連接失敗」 |

另外，**「配對戒指」按鈕對 AIVELA 永遠不會成功**（見 §2），但那與實際連線無關。

---

## 1. 證據鏈（逐條原始 log）

### 1.1 戒指是連上的，而且真的在拍照

```
# 手機：AIVELA 在系統層是 HID 連線中（2 = STATE_CONNECTED）
$ dumpsys bluetooth_manager | sed -n '/mInputDevices/,/^$/p'
  mInputDevices:
    64:8F:DB:86:13:3E : 0        ← Xiaomi Buds（未連）
    BB:C3:13:73:8A:32 : 2        ← AIVELA：已連線

# 手機：AIVELA 註冊為輸入裝置（/dev/input/event7）
$ dumpsys input | grep -A3 AIVELA
  24: AIVELA_RP13EJ37
    Classes: KEYBOARD | ALPHAKEY | TOUCH | CURSOR | EXTERNAL
    Path: /dev/input/event7     Enabled: true

# 手機：戒指手勢確實被 App 收到並執行
09-13 15:56:27.418 RingController: 自訂鍵碼 KEYCODE_VOLUME_DOWN (25) → PHOTO（累計 173）
09-13 15:56:31.150 RingController: 結果：KEYCODE_VOLUME_DOWN (25) → 已拍照 IMG_1789286187697.jpg
09-13 15:56:32.228 RingController: 結果：TAP → 已拍照 IMG_1789286191153.jpg
09-13 15:56:32.769 RingController: 手勢 TAP → 動作 PHOTO（累計 175）
09-13 15:56:33.813 RingController: 結果：TAP → 已拍照 IMG_1789286192824.jpg

# 眼鏡：4 個檔案真的落地
$ ls -la .../files/media/albums/alb-3b6dfe09/
  IMG_1789286187697.jpg  IMG_1789286191153.jpg
  IMG_1789286192824.jpg  IMG_1789286196069.jpg
```

### 1.2 直接死因：LMK 把 App 殺掉

```
09-13 15:56:38.195 E/lowmemorykiller: Memory Load: [2] {pid:26943} {size:100 MB} {name:com.rokid.rc.glasses} {oom_score:691}
09-13 15:56:38.231 I/lowmemorykiller: Kill 'com.rokid.rc.glasses' (26943), uid 10053, oom_score_adj 0 to free 102868kB rss, 22872kb swap
09-13 15:56:38.294 I/Zygote: Process 26943 exited due to signal 9 (Killed)
09-13 15:56:38.535 W ActivityManager: Scheduling restart of crashed service com.rokid.rc.glasses/.CaptureService in 1000ms
```

觸發點是**連續 5 筆拍照**（`CommandRouter: recv: CAPTURE_PHOTO id=15,16,17,18,19`，15:56:27–37）。

### 1.3 重建後進不了前景服務

```
09-13 15:56:39.879 I/ActivityManager: Background started FGS: Allowed [... code:SHELL; tempAllowListReason:<shell,reasonCode:SHELL,duration:30000...>]
09-13 15:56:41.136 W/ActivityManager: Service.startForeground() not allowed due to bg restriction: service com.rokid.rc.glasses/.CaptureService
```

### 1.4 相機被切到錄影模式 → 之後拍照必失敗

```
09-13 15:56:45.521 CommandRouter: recv: START_VIDEO id=20
09-13 15:56:45.577 ImageCapture: clearPipeline          ← bindVideo() 已 unbind 拍照 use case
09-13 15:56:45.618 Camera2CameraImpl: Use case VideoCapture-... INACTIVE
（其後 ~2 分鐘）Recorder: Cached audio data while we wait for video keyframe before starting muxer
```

程式碼對應（`camera/CameraController.kt`）：

```kotlin
private fun bindVideo() { ...; p.unbindAll(); p.bindToLifecycle(..., vc); recorder = rec; imageCapture = null }   // ← 拍照能力被清空
suspend fun takePhoto(ghost: Boolean): Result<MediaItem> {
    val ic = imageCapture ?: return Result.failure(IllegalStateException("camera not ready"))   // ← line 118：使用者看到的訊息
```

- 因此 `15:56:45` 之後的每一筆 `CAPTURE_PHOTO`（id=21,24,27,31–35）都回 **`camera not ready`**，
  手機 UI 顯示「**拍照失敗：camera not ready**」。
- **本次錄影沒有產生任何檔案**（相簿內最新影片仍是 09-12 02:56）；`Recorder` 一直在等 keyframe。

### 1.5 最後服務被停、通道關閉

```
09-13 15:58:09.605 W/ActivityManager: Stopping service due to app idle: u0a53 -1h56m36s8ms com.rokid.rc.glasses/.CaptureService
09-13 15:58:09.637 E/CaptureService: wifi server error: Socket closed
09-13 15:58:26.791 D/BluetoothSocket: close() ... mSocketState: CLOSED      ← RFCOMM server 也關了
09-13 15:59:50.394 DebugReceiver: CONNECT <DEVICE_ALIAS> result=Failure(java.io.IOException: read failed, socket might closed or timeout, read ret: -1)
```

`CaptureService.onDestroy()` 會 `server.stop()` + `wifiServer.stop()` + `graph.camera = null` → 這就是「未連線」的由來。

### 1.6 UI 上的實際錯誤字（截圖 OCR）

```
✓/✗ 指示燈…            開啟隱私指示燈…  ✗ 指示燈設定失敗 Broken pipe   ← 服務已死（Broken pipe）
手機端手勢（戒指連手機）：OFF → ON → OFF → ON
收到鍵碼 KEYCODE_MEDIA_PLAY_PAUSE (85)
已對應 KEYCODE_MEDIA_PLAY_PAUSE (85) → 錄影開始／停止
✗ 掃描不到名稱含「RGR」的裝置（共 7 台）
```

---

## 2. 為什麼「配對戒指」對 AIVELA 一定失敗（兩個獨立原因）

1. **它已經配對且已連線 → 不再廣播。** 三次掃描（15:54:29、15:55:55、15:56:51）分別看到 7–8 台裝置，
   **沒有一次出現 AIVELA**。已配對的 BLE 周邊只對原 host 定向連線，不再對外廣告。
2. **按鈕只比對名稱含 `"RGR"`。** `SettingsFragment.btnRingPair → RingBonder.scanAndBond(ctx, "RGR", 15_000)`
   → `RingScanner` 取「廣告名稱含 RGR 且可連線」者；AIVELA 名稱是 `AIVELA_RP13EJ37`，不符。
   三次掃描的結果**全部 `name=-`**（那些是 Apple/微軟的無名信標），所以連「用名稱找」都不可能成功。

> 附帶查核：手機 BT stack 有一次 `Permission denial: Need ACCESS_FINE_LOCATION permission to get scan results`，
> 但該次屬於 **`jp.naver.line.android`（PID 27733）**，不是本 App（本 App 已持有 `BLUETOOTH_SCAN`/`BLUETOOTH_CONNECT`，
> 且 `ACCESS_*_LOCATION` 因 manifest `maxSdkVersion="32"` 在 API 33 不請求）。**與本問題無關**，不列為原因。

---

## 3. 立即處置（**已實測有效**）

```bash
ADB="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe"
# 1) 把眼鏡端 App 帶到前景（關鍵：前景啟動 FGS 才被允許）
"$ADB" -s <DEVICE_SERIAL> shell 'am start -n com.rokid.rc.glasses/.MainActivity'
# 2) 手機端重新連線
"$ADB" -s <PHONE_SERIAL> shell 'am broadcast -n com.rokid.rc.phone/.DebugReceiver \
    -a com.rokid.rc.phone.DEBUG --es cmd CONNECT'
```

驗證結果：

```
# 眼鏡
ServiceRecord{... com.rokid.rc.glasses/.CaptureService}  isForeground=true     ← 修好
CaptureService: WiFi media server ready (:8555)
CaptureService: camera ready
# 手機
DebugReceiver: CONNECT <DEVICE_ALIAS> result=Success(kotlin.Unit)
DebugReceiver: CAPTURE_PHOTO result=true
# 眼鏡相簿
IMG_1789286455006.jpg  2026-09-13 16:00      ← 新檔案
```

**日常操作建議**：眼鏡端**不要用 `am start-foreground-service` 從背景拉服務**（會被 bg restriction 拒絕，
之後必被 app idle 停掉）；要用 **`am start` 把 MainActivity 帶到前景**，由 App 自己 `startForeground()`。

---

## 4. 根治建議（程式修改，尚未實作）

| 優先 | 問題 | 建議改動 | 檔案 |
|------|------|----------|------|
| **P0** | 錄影中拍照只回 `camera not ready`，誤導成「連線失敗」 | `imageCapture == null` 時若 `isRecording` 為真 → 回新錯誤 `busy_recording`；手機 `RingText` 加對應字串「錄影中，無法拍照」 | `CameraController.kt:118`、`CommandRouter.kt:288`、`RingText.kt` |
| **P0** | 錄影會卡死（等不到 keyframe）且永遠回不到拍照模式 | `startVideo` 後加看門狗：N 秒（建議 5 s）內未收到 `VideoRecordEvent.Start` 就 `stop()` + `bindPhoto()` 並回報錯誤 | `CameraController.kt` |
| **P0** | 連續 5 次拍照就把 App 打爆（LMK） | `capturePhoto` 加 `Mutex` + 最小間隔（建議 800 ms），同時只允許一筆；眼鏡端回 `busy` | `CommandRouter.kt`、`RingControlManager.kt` |
| **P1** | 服務被 bg restriction 拒絕後必被 app idle 停掉 | 啟動流程改為「先確保有前景 Activity，再啟服務」；`CaptureService` 無法 `startForeground()` 時要有補救（例如通知使用者/自動 `am start` MainActivity） | `CaptureService.kt`、`RootDaemonKeeper`、`ring_keepalive.sh` |
| **P1** | AIVELA 的 `KEY_PLAYPAUSE`/`MEDIA_NEXT` 被綁成 **錄影切換**，滑一下就誤觸錄影 → 相機卡死 | 預設對應移除媒體鍵的 `VIDEO_TOGGLE`（改 `NONE`），或要求長按才切錄影 | `RingController.actionFor` / prefs `map_85`、`map_87` |
| **P1** | 「配對戒指」寫死 `RGR`，且對已配對裝置做掃描 | 改成**已配對 HID 裝置清單**（`BluetoothAdapter.bondedDevices` 過濾 HID/鍵鼠）＋選單選擇；掃描僅用於未配對的新戒指 | `SettingsFragment`、`RingBonder` |
| **P2** | UI 顯示「未連線」但不知道是服務死了 | 眼鏡 `STATUS` 增 `serviceForeground` / `cameraMode`(`photo`/`video`/`none`) 欄位，手機直接顯示「眼鏡服務未啟動」 | `Models.kt`、`CommandRouter`、`RingText` |

---

## 5. 信心

| 陳述 | 標記 | 信心 | 依據 |
|------|------|------|------|
| AIVELA 已配對 + HID 連線中，且手勢可驅動拍照 | FACT | **Very High** | `mInputDevices : 2`、`dumpsys input` device 24、4 張檔案 + `RingController` log |
| 「配對戒指」因名稱不符（且已配對不廣播）而必失敗 | FACT | **Very High** | 三次掃描無 AIVELA；`scanAndBond("RGR")`；UI 字串「掃描不到名稱含「RGR」的裝置（共 7 台）」 |
| App 被 LMK 殺 | FACT | **Very High** | `lowmemorykiller: Kill 'com.rokid.rc.glasses'` |
| 服務重建後未取得前景 → 最後被 app idle 停掉 | FACT | **Very High** | `startForeground() not allowed due to bg restriction`、`Stopping service due to app idle` |
| 錄影中拍照必回 `camera not ready` | FACT | **Very High** | `bindVideo()` 設 `imageCapture = null`；`takePhoto()` line 118 |
| 本次錄影沒有產檔 | FACT | **High** | 相簿最新影片為 09-12；`Recorder` 持續等 keyframe；encoder 15:58:10 RELEASED |
| 立即處置可恢復 | FACT | **Very High** | `isForeground=true`、`camera ready`、`CONNECT Success`、`CAPTURE_PHOTO result=true`、新檔案 |
| 手機 BT stack 的 location 權限拒絕與本問題無關 | FACT | **High** | 該行屬於 `jp.naver.line.android`（PID 27733） |

## 6. 未執行

未變更任何藍牙配對、未刪除任何檔案、未執行 `force-stop`（僅用 `am start` 恢復）。
新增的唯一檔案是驗證用的 `IMG_1789286455006.jpg`（16:00，來自 `CAPTURE_PHOTO` 測試）。
