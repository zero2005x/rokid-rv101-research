# 戒指／相機 P0＋P1 修正 — 實作與實機測試紀錄

- **日期**：2026-09-13 16:3x–16:4x
- **依據**：`reports/aivela_ring_connection_failure_2026-09-13.md` 的建議清單
- **產出**：`android/app-glasses/build/outputs/apk/debug/app-glasses-debug.apk`、
  `android/app-phone/build/outputs/apk/debug/app-phone-debug.apk`（皆已安裝並實測）
- **裝置**：眼鏡 `<DEVICE_SERIAL>`（Android 12/API 32）· 手機 `<PHONE_SERIAL>`（Android 13/API 33, MIUI）

> **測試方式說明**：戒指的實體按鍵無法由自動化產生，因此拍照／錄影路徑改用**眼鏡自身的
> TCP:8555 通道**送出與 App 完全相同的 JSON frame（`{"t":"CAPTURE_PHOTO","id":N}` 等），
> 這條通道與藍牙共用同一個 `CommandRouter`／`CameraController`，且**可以精確製造併發**
> （WiFi 與藍牙同時送入），是比手動按戒指更嚴謹的等價測試。

---

## 1. 修正內容

| # | 需求 | 實作 | 檔案 |
|---|------|------|------|
| P0-1 | 錄影中拍照要回新錯誤，不要 `camera not ready` | 新增 `CameraErrors.BUSY_RECORDING = "busy_recording"`；`takePhoto()` 在 `imageCapture == null` 時依 `isRecording` 回 `busy_recording` 或 `camera_not_ready`；手機 `RingText.errorDetail()` 對應到在地化字串（未知碼原樣顯示） | `shared/protocol/CameraErrors.kt`（新）、`CameraController.kt`、phone `ui/RingText.kt` |
| P0-2 | `startVideo` 5 秒看門狗 | `startVideo` 以 `withTimeoutOrNull(5s)` 等 `VideoRecordEvent.Start`；逾時→標記 `videoAborted`、`recording.stop()`、強制 `bindPhoto()`、回 `record_start_timeout`；`onVideoEvent` 收到 Start 時喚醒；Finalize 時若 aborted 則**刪除殘檔**且不 `addMedia` | `CameraController.kt` |
| P0-3 | 拍照 Mutex ＋ 800 ms 最小間隔 | `photoLock: Mutex`（`tryLock` 失敗即回 `busy`，**不排隊**以免堆積）＋ `MIN_PHOTO_INTERVAL_MS = 800`，鎖內補足間隔 | `CameraController.kt` |
| P1-1 | 媒體鍵不可預設觸發 `VIDEO_TOGGLE` | `MEDIA_KEYS_PASS_THROUGH`（PLAY_PAUSE／PLAY／PAUSE／STOP／NEXT／PREVIOUS／FF／REW／HEADSETHOOK）自 `INTERESTING_KEYS` 移除 → 放行給系統；並加**一次性遷移**移除既有的「媒體鍵 → VIDEO_TOGGLE」自訂對應 | phone `ring/RingController.kt` |
| P1-2 | 「配對戒指」改為已配對 HID 裝置清單 | 新增 `RingBonder.bondedHidDevices()`（class/UUID/名稱三重判定）＋ `displayName()`；`SettingsFragment` 改為清單對話框，選定後存入 `ring_ctrl.approved_mac/approved_name`；「掃描新裝置…」為次要選項且**不過濾名稱** | `shared/ring/RingBonder.kt`、phone `ui/SettingsFragment.kt` |

i18n：新增 **10 個 key**（3 個錯誤字＋7 個配對 UI），14 語系全部補齊
（`phone_keys.tsv` 193→203；`out/*.xml` 各區塊同步；`strings.xml` 284→**294** 條）。
`validate_translations.py` ✅、`apply_phone_strings.py` ✅（14/14）。

---

## 2. 實機測試結果

### T1 建置／安裝
```
./gradlew :app-glasses:assembleDebug :app-phone:assembleDebug --offline   → BUILD SUCCESSFUL
adb -s <DEVICE_SERIAL> install -r app-glasses-debug.apk → Success
adb -s <PHONE_SERIAL> install -r app-phone-debug.apk   → Success
```

### T2 P0-3 連拍（5 筆同時送入，重現原本的 LMK 情境）
```
--- frame 1: {"t":"PHOTO_TAKEN","replyTo":1,"media":{…"fileName":"IMG_1789288908289.jpg","sizeBytes":3558694}}
--- frame 2: {"t":"PHOTO_TAKEN","replyTo":2,…"IMG_1789288910128.jpg","sizeBytes":3554351}
--- frame 3: {"t":"PHOTO_TAKEN","replyTo":3,…"IMG_1789288911208.jpg","sizeBytes":3550323}
--- frame 4: {"t":"PHOTO_TAKEN","replyTo":4,…"IMG_1789288912233.jpg","sizeBytes":3553062}
--- frame 5: {"t":"PHOTO_TAKEN","replyTo":5,…"IMG_1789288913209.jpg","sizeBytes":3550864}
```
- **5 張全部成功**；`createdAt` 間隔 **1079 / 1021 / 974 / 958 ms** → 800 ms 最小間隔生效。
- **無 LMK kill、無程序重啟、無錯誤碼**（修正前：連 5 筆 → RSS 100 MB → 被 lowmemorykiller 殺掉）。
- ⚠ 註：驗證「無 LMK」時不能用 `logcat | grep -c "Kill .com.rokid.rc.glasses"`——該指令列**自身**含此字串會被算進去（本專案已多次踩到 `pkill -f` 自比對的同類坑）。

### T3 P0-1 錄影中拍照
```
=== START_VIDEO id=10 ===   -> {"t":"VIDEO_STARTED","replyTo":10}
=== CAPTURE_PHOTO id=11 === -> {"t":"ACK","replyTo":11,"ok":false,"error":"busy_recording"}
=== STOP_VIDEO id=12 ===    -> {"t":"VIDEO_STOPPED","replyTo":12,"media":{"fileName":"VID_1789288977676.mp4",
                               "sizeBytes":17608900,"width":1280,"height":720,"durationMs":8207,…}}
=== CAPTURE_PHOTO id=13 === -> {"t":"PHOTO_TAKEN","replyTo":13,…"IMG_1789288995662.jpg"}
```
- 錄影中拍照回 **`busy_recording`**（修正前是 `camera not ready`）。
- **P0-2 附帶驗證**：`START_VIDEO` **立刻**回 `VIDEO_STARTED` → `VideoRecordEvent.Start` 在 5 秒內到達，**看門狗沒有誤判**。
- `STOP_VIDEO` 產生**有效檔案**（17.6 MB、1280×720、8.2 秒）→ 看門狗沒有破壞正常錄影。
- 停止後拍照回 `PHOTO_TAKEN` → **`bindPhoto()` 有把拍照模式還原**。

### T4 P0-3 併發保護（WiFi 與藍牙同時送）
```
眼鏡 WiFi 通道  -> {"t":"ACK","replyTo":90,"ok":false,"error":"busy"}
手機 藍牙廣播   -> DebugReceiver: CAPTURE_PHOTO result=true
```
→ `Mutex.tryLock` 生效：重疊的請求被**立即拒絕**（`busy`）而非排隊堆積。

### T5 P1-1 媒體鍵遷移
```
RingController: 媒體鍵遷移完成：移除 2 筆「媒體鍵→錄影切換」舊對應（媒體鍵已改為放行）
ring_ctrl prefs 前：map_4097=PHOTO, map_87=VIDEO_TOGGLE, map_25=PHOTO, map_85=VIDEO_TOGGLE
ring_ctrl prefs 後：map_4097=PHOTO, map_25=PHOTO, migrated_media_keys_v1=true
```
→ `MEDIA_NEXT(87)`／`MEDIA_PLAY_PAUSE(85)` 的錄影對應已移除；滑鼠鍵與音量鍵的自訂對應保留。

### T6 手機端錯誤字（i18n）
```
TEST_RING_TEXT photo_fail busy_recording        -> 拍照失敗：錄影中，無法拍照
TEST_RING_TEXT photo_fail busy                  -> 拍照失敗：操作太頻繁，請稍候
TEST_RING_TEXT photo_fail record_start_timeout  -> 拍照失敗：錄影啟動逾時，已切回拍照
TEST_RING_TEXT photo_fail camera_not_ready      -> 拍照失敗：相機未就緒
TEST_RING_TEXT photo_fail weird_code            -> 拍照失敗：weird_code     ← 未知碼原樣顯示
```

### T7 P1-2 裝置清單（**已完成，含畫面**）
點「配對戒指」後的對話框實拍：

```
選擇戒指裝置
✓ AIVELA_RP13EJ37
BB:C3:13:73:8A:32
✓ Xiaomi Buds 5 Pro
64:8F:DB:86:13:3E
掃描新裝置…
                                          取消
```

- **AIVELA_RP13EJ37 出現在清單中**（截圖 `build-cache/shots/picker.png`）。
- **不再有任何 `RGR` 名稱過濾**、**不對已配對裝置做 BLE 掃描**（清單直接來自 `bondedDevices`）。
- 後續追加一道過濾：`Major.AUDIO_VIDEO` 類別且**沒有** HID UUID／名稱證據者排除。
  ⚠ 但 **Xiaomi Buds 5 Pro 仍會入列**——因為它**真的是 HID 裝置**
  （`dumpsys bluetooth_manager` 的 `mInputDevices` 同時列出 `64:8F:DB:86:13:3E` 與 AIVELA），
  即它具有 HID UUID 證據，過濾條件**刻意**不排除它。
- **附帶發現**：耳機也是 HID → 依 `reports/ring_capture_security_audit_2026-09-13.md` §2 R1，
  它的媒體／音量鍵**同樣會被 `RingController` 當成手勢來源**。這是「未做來源綁定」的另一個實例；
  本次清單只是把同一批裝置誠實列出來，真正的解法仍是來源綁定（該報告 §3）。

---

## 3. 尚未涵蓋／已知限制

| 項目 | 狀態 |
|------|------|
| ~~P1-2 清單對話框的畫面驗證~~ | ✅ **已完成**（T7，含截圖） |
| 戒指**實體按鍵**的端到端手勢（單擊/雙擊/長按） | 未執行（無法以自動化產生實體 HID 事件）；程式路徑未改動，且 T3–T5 已覆蓋其下游 |
| 實際把錄影「卡死」以觸發 5 秒看門狗 | 未能人為重現卡死；已驗證**不誤判**（Start 正常到達）＋逾時分支的程式碼路徑 |
| `DebugReceiver`（exported 無 permission） | 依先前稽核仍待修（本次未動，見 §7 P9） |
| 眼鏡端 `CaptureService` 被 app idle 停掉 | 本次未動（見 §7 P15 的 P1 建議）；測試期間以 `am start` 維持前景 |

## 4. 信心

| 陳述 | 標記 | 信心 | 依據 |
|------|------|------|------|
| 錄影中拍照回 `busy_recording` | FACT | **Very High** | T3 線上 ACK 原文 |
| 錄影啟動／停止正常且產出有效檔案 | FACT | **Very High** | T3 `VIDEO_STOPPED` + 17.6 MB / 1280×720 / 8207 ms |
| 連拍被 800 ms 間隔節流且不再被 LMK 殺 | FACT | **High** | T2 五次 `createdAt` 間隔 958–1079 ms；無 kill 紀錄 |
| 併發請求被 Mutex 拒絕為 `busy` | FACT | **Very High** | T4 WiFi 端 ACK 原文 |
| 媒體鍵不再預設觸發錄影 | FACT | **Very High** | T5 遷移紀錄 + `INTERESTING_KEYS` 已移除媒體鍵 |
| 手機端錯誤字在地化正確 | FACT | **Very High** | T6 五個案例（含未知碼） |
| AIVELA 會出現在配對清單 | FACT | **Very High** | T7 畫面實拍；清單來自 `bondedDevices`，無名稱過濾 |
| Xiaomi Buds 入列是因它真的是 HID 裝置 | FACT | **High** | `mInputDevices` 同時列出 Buds 與 AIVELA |
| 看門狗逾時分支本身 | 未實測 | — | 僅驗證不誤判；逾時需人為製造 Recorder 卡死 |
