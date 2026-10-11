# 「無法拍照／錄影、眼鏡觸控板沒反應、畫面有滑鼠指標閃爍」— 遠端診斷與修正

- **日期**：2026-09-13 18:39–18:45
- **情境**：眼鏡**未接 adb**（依使用者描述，拔線後），只能經由**手機 App 的藍牙通道**遠端診斷
- **裝置**：眼鏡 `<DEVICE_SERIAL>`（Android 12）· 手機 `<PHONE_SERIAL>`

---

## 0. 三個症狀、三個不同的原因

| 症狀 | 實測原因 | 與 root 有關？ |
|------|----------|---------------|
| **無法拍照／錄影** | `CAPTURE_PHOTO failed: camera_not_ready`；`cameraBusy=false` → **眼鏡端 `CaptureService` 已被系統停掉**，`onDestroy` 把 `graph.camera = null`（與 `reports/aivela_ring_connection_failure_2026-09-13.md` §1.5 同一根因） | ❌ **無關**（相機不需要 root） |
| **眼鏡觸控板按了沒反應** | `ringEnabled=false`、`ringRunning=false` —— **眼鏡端手勢控制目前是關閉的**；且 `ringDevice=/dev/input/event4`（RGR06）**不是**眼鏡觸控板 `event1` | ❌ 無關 |
| **畫面滑鼠指標閃爍** | RGR06 戒指以 **HID 滑鼠／觸控板**身分連在眼鏡上（`event2` Keyboard／`event3` Consumer Control／`event4` 觸控面），持續回報座標 → Android 畫出指標 | ❌ 無關 |
| （附帶）「目前在 root 狀態」 | 實測 `rootAcquired=false`、`rootMode=MANUAL` → **目前其實沒有 root** | — |

**遠端取得的實測輸出**（手機端新增的 `DUMP_STATUS`）：

```
DebugReceiver: STATUS cameraBusy=false rootMode=MANUAL rootAcquired=false
               silent=true/true ringEnabled=false ringRunning=false
               ringDevice=/dev/input/event4 ringLast=null/null
DebugReceiver: CAPTURE_PHOTO failed: camera_not_ready
DebugReceiver: DEVICE_INFO={"selinux":"Permissive","uid":10053,
               "rootAcquired":false,"ghostlockEnabled":false,"rootMode":"MANUAL", ...}
```

---

## 1. 為什麼「觸控板按了沒反應」——一個**真 bug**

`RingControlManager.start()` 原本的邏輯是：

```kotlin
val dev = devicePath?.takeIf { it.isNotBlank() } ?: reader.autoDetect(namePattern)?.first
```

`devicePath` 一旦寫進 `ring_ctrl.device` 就會**一直被沿用、且不會檢查該路徑是否還存在**。
本次就是這樣踩到的：2026-09-13 17:14 為了測 RGR06，把它設成 `/dev/input/event4`；
之後使用者要按**眼鏡自己的觸控板**，程式卻仍然只讀 event4 ——
而且**沒有任何錯誤訊息**（`start()` 回 true、狀態顯示「執行中」），完全靜默。

**已修正**：
```kotlin
val storedOk = stored != null && reader.deviceExists(stored)   // 新增：核對裝置清單
if (stored != null && !storedOk) { Log.w(...); devicePath = null }   // 失效就回退自動偵測
val dev = (if (storedOk) stored else null) ?: reader.autoDetect(namePattern)?.first
```

## 2. 為什麼「不能拍照」——服務被系統停掉，不是 root 問題

- `CaptureService.onDestroy()` 會執行 `graph.camera = null`，之後 `CommandRouter.capturePhoto`
  一律回 `camera not ready`。
- 服務被停的原因先前已取證過：`ActivityManager: Stopping service due to app idle`
  （因為 Android 12 的背景啟動限制讓它無法 `startForeground()`，見 P15）。
- **`MainActivity.onCreate()` 會 `startForegroundService(CaptureService)`** ——
  所以**在眼鏡上打開我們的 App 就能把相機救回來**。

**已改進**：手機端把 `camera_not_ready` 從「相機未就緒」改成可行動指示
**「相機未就緒（請在眼鏡上開啟 App）」**，14 語系同步更新（`phone_keys.tsv` 203 keys 驗證通過）。
使用者不會再只看到一句無從下手的錯誤。

## 3. 滑鼠指標閃爍

RGR06 是 **HOGP 滑鼠類**周邊（Appearance `0x03C2` Mouse），配對後眼鏡的 HID host 會註冊
三個輸入裝置；只要它回報座標，Android 就顯示指標（游標閃爍通常代表它持續／斷續回報）。
**與拍照功能無關**，但若不想看到：解除 RGR06 配對，或把戒指移開／關機。

---

## 4. 處置

### 4.1 你現在就能做（不需要 adb）

1. **在眼鏡上打開我們的 App** → `CaptureService` 回到前景、相機重新 bind → 拍照／錄影恢復。
2. 手機 App「設定 → 戒指控制 → **眼鏡端手勢**」打開開關。
   （注意：`ringDevice` 目前仍是 `/dev/input/event4`，這要插線才能改；但即使指向戒指，
   開啟後戒指本身的手勢也能觸發拍照。）
3. 想擺脫閃爍的滑鼠指標：手機「藍牙設定」把 `RGR06` 解除配對。

### 4.2 插回 USB 後我可以一次做完

```bash
ADB="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe"
S=<DEVICE_SERIAL>
"$ADB" -s $S install -r dist/app-glasses-v1.0-safe.apk          # 裝含回退修正的版本
"$ADB" -s $S shell 'am start -n com.rokid.rc.glasses/.MainActivity'   # 讓相機回到 ready
# 把眼鏡端手勢切回「眼鏡自己的觸控板」
"$ADB" -s $S shell 'am broadcast -n com.rokid.rc.glasses/.DebugReceiver \
    -a com.rokid.rc.glasses.DEBUG --es cmd RING_STOP'
"$ADB" -s $S shell 'am broadcast -n com.rokid.rc.glasses/.DebugReceiver \
    -a com.rokid.rc.glasses.DEBUG --es cmd RING_START --es name TP --es device /dev/input/event1'
"$ADB" -s $S shell 'am broadcast -n com.rokid.rc.glasses/.DebugReceiver \
    -a com.rokid.rc.glasses.DEBUG --es cmd ARM_POLICY_STATUS'   # 確認安全政策狀態
```
（或直接把手機 App 的「眼鏡端手勢」開關打開，並用眼鏡端 UI 選裝置。）

---

## 5. 本次一起交付的改動

| 改動 | 檔案 |
|------|------|
| **戒指裝置路徑失效 → 自動回退偵測**（修掉靜默失效的真 bug） | `RingControlManager.kt`、`RingEvdevReader.kt`（新增 `deviceExists()`） |
| `camera_not_ready` → 可行動訊息，14 語系 | `RingText.kt`、`phone_keys.tsv`、`out/*.xml`、14 個 `strings.xml` |
| 手機除錯能力：失敗原因＋`DUMP_STATUS`（全狀態欄位） | phone `DebugReceiver.kt` |
| 兩版眼鏡 APK ＋ `dist/README.md` | `dist/` |

## 6. 未驗證／限制

- **眼鏡目前不在 adb 上**，上述修正**尚未安裝到眼鏡**（手機端已安裝並驗證訊息）。
- `ringEnabled=false` 是誰關掉的未追查（可能是手機端開關或先前的 `SET_RING`）；
  這次只確認「目前是關的」。
- 滑鼠指標成因由 HID 裝置清單推得（**High**），未實拍眼鏡畫面。

---

## 7. 追加（19:40–19:45，兩台都接回 adb 後的處置與結果）

### 7.1 找到「相機反覆消失」的機制：**螢幕睡著 → 活動無法進前景 → FGS 被拒**

```
$ dumpsys power | grep mWakefulness      → mWakefulness=Asleep
$ dumpsys window | grep mCurrentFocus    → Window{… MockWindow}       ← 不是我們的 Activity
$ logcat | grep 'startForeground'
  W ActivityManager: Background start not allowed: service Intent
      { cmp=com.rokid.rc.glasses/.CaptureService } … startFg?=true
```

`RingControlManager.ensureCaptureService()` 會嘗試 `startForegroundService`；但**螢幕睡著時
Activity 上不到前景**，Android 12 便以「背景啟動」拒絕，服務起不來 → `graph.camera` 永遠是 null
→ 所有拍照請求回 `camera not ready`。

**處置（實測有效）**：`input keyevent KEYCODE_WAKEUP` → `am force-stop` → `am start MainActivity`
（**必須 force-stop**：Activity 還活著時 `am start` 只會 resume，`onCreate` 不會重跑，
也就不會再叫一次 `startForegroundService`）。

### 7.2 修掉我昨天新增的回退邏輯的一個缺陷

`RingEvdevReader.deviceExists()` 用 `listDevices()` 判斷，而 `listDevices()` 走 root daemon
（`getevent -pl`）；**沒有 root 時它一律回空清單** → 被誤判成「裝置不存在」→
把使用者設定的 `/dev/input/event1` **清掉**（實測 19:40 真的發生）。
已修：清單為空時回 `true`（無法確認就不動它）。

### 7.3 完成後的實測結果

| 項目 | 結果 |
|------|------|
| `CaptureService` | `isForeground=true`、`camera ready` ✅ |
| **拍照** | `CAPTURE_PHOTO ok IMG_1789299767856.jpg` ✅ |
| **錄影** | `START_VIDEO ok` → `STOP_VIDEO ok VID_1789299754052.mp4` ✅ |
| 戒指控制設定 | `pattern=TP`（眼鏡觸控板）、`enabled=true`、已移除失效的 `device` 路徑 ✅ |
| 安全政策 | `safeArming=on up=3611s charging=true arms=0/2 suspicious=0 suspended=false` ✅ |
| **眼鏡觸控板手勢** | `RING_STATUS running=false device=null last=no_device/TP` —— **缺 root**，`getevent` 跑不起來 ⚠ |
| RGR06 | 已配對但**目前未連線**（`mInputDevices` 空）→ 滑鼠指標目前不會出現 |

### 7.4 結論

- **「不能拍照／錄影」已修復**（相機與服務都回來了，照片與影片都實測成功）。
- **「眼鏡觸控板按了沒反應」只差 root**：路徑 B 走 root 直讀 evdev，沒有 root 就不可能運作。
  目前 `root_mode=MANUAL`、斷路器未跳脫 → 需要**手動**取得 root
  （手機 App 的「取得 root」按鈕，或 `deploy/root_bridge.sh`）。
- **滑鼠指標**：RGR06 未連線時不會出現；要根治就解除它的配對。

---

## 8. 追加（19:55–20:00）：取得 root 並完成全部恢復

使用者明確要求取得 root。**刻意避開 `deploy/get_root_now.sh` 的外層 `adb reboot` 重試迴圈**
（它失敗時最多重開機 6 次），改用**單次受控流程**（`build-cache/one_shot_root.sh`）：

1. `SERIAL=<DEVICE_SERIAL> bash deploy/root_bridge.sh` → 建立 bridge ＋ 常駐 helper（pid 4137）
2. 把 `deploy/trigger_root_daemon.cmd` 寫進 `ghostlock_bridge/last.cmd`
3. 每 10 秒監看 `last.progress` / `last.out` / `/proc/net/unix` / **uptime 是否倒退**

### 結果

```
[13] up=4219s prog=done out=– daemon=1     ← 約 130 秒取得 root
[29] 裝置離線（可能 reboot）                 ← 實為 USB 暫時斷線
```

**沒有 reboot**：uptime 由 4056s 一路遞增到 4546s（連續），`ro.boot.bootreason` 未變。
（boot history 另顯示 18:14:09 與 18:44:55 兩次重開，那是**本次處置之前**發生的，
正好對應使用者回報的當機時間，也解釋了為何 19:40 接手時 App 沒在跑、root 也沒了。）

### 完成後的狀態（全部實測）

| 項目 | 結果 |
|------|------|
| **root** | `uid=0(root) … context=u:r:kernel:s0`；`getenforce=Permissive`；`root_daemon`(21868) ＋ `root_daemon_keepalive.sh`(21879) 常駐 ✅ |
| **拍照** | `CAPTURE_PHOTO ok IMG_1789300777925.jpg` ✅ |
| **錄影** | `START_VIDEO ok` → `STOP_VIDEO ok VID_1789300796405.mp4` ✅ |
| **眼鏡觸控板手勢** | `RING_STATUS enabled=true running=true device=/dev/input/event1 last=running//dev/input/event1` ✅ |
| `CaptureService` | `isForeground=true` ✅ |
| 安全政策 | 未跳脫（本次是**手動**提權，不算自動武裝，故不觸發計數） |

### 踩到的三個小坑（已記錄）

1. **`RING_START` 的 pattern 會被手機端的 `SET_RING` 覆寫**：`CommandRouter.setRing` 在
   `pattern == null` 時用 `RingIdentity.orDefault(null) = "RGR"`，所以手機一動「眼鏡端手勢」開關，
   眼鏡的 `pattern` 就變回 `RGR`。→ 要固定來源請用 **`RING_START --es device /dev/input/event1`**
   （明確指定裝置，不依賴 pattern）。
2. **`am start` 若 Activity 還活著只會 resume**，`onCreate` 不重跑 → 不會再叫 `startForegroundService`；
   螢幕睡著時更上不到前景。修法是 `KEYCODE_WAKEUP` ＋ **`am force-stop`** ＋ `am start`。
3. **`DUMP_STATUS` 會逾時**：眼鏡忙於拍照（單筆約 2–26 秒）時，STATUS 請求會 `request timeout`；
   診斷時要避開拍攝中。

---

## 9. 追加（20:30）：**更正**——觸控板其實正常，並取得崩潰前的手勢證據

### 9.1 更正我先前（20:2x）的錯誤判斷

我先前用 `( toybox timeout 90 getevent -lt /dev/input/event1 ) | head -60` 測試，得到「0 個事件」，
因而推論觸控板不產生事件。**這個推論是錯的**——那是**測試方法本身的問題**：
輸出經過 pipe + `timeout` 以 SIGTERM 結束行程，**stdio 緩衝內容整個遺失**。

改成**寫入檔案**（`getevent -lt … >> /data/local/tmp/tp_test.log`）後立刻看到事件。
**觸控板 (`/dev/input/event1`, `ROKID,PSOC-TP-R`) 完全正常。**

> 教訓：在 Android 上抓 `getevent` 一定要**導向檔案**，不要經過 pipe；否則會誤判成「裝置沒事件」。

### 9.2 使用者實際按下的鍵（原始捕捉）

```
[ 1102.396] EV_KEY KEY_ENTER      DOWN / UP      ← 輕觸
[ 1102.912] EV_KEY KEY_DASHBOARD  DOWN / UP      ← ★ 開啟 Rokid 助手
[ 1103.488] EV_KEY KEY_ENTER      DOWN / UP
[ 1106.645] EV_KEY KEY_DASHBOARD  DOWN / UP
[ 1107.165] EV_KEY KEY_PROG1      DOWN / UP      ← 滑動
[ 1110.195] EV_KEY KEY_DASHBOARD  DOWN / UP
[ 1110.392] EV_KEY KEY_DASHBOARD  DOWN / UP
[ 1110.684] EV_KEY KEY_BACK       DOWN / UP
```

| 觸控板送出的鍵 | 系統行為 | 我們眼鏡端程式的處理 |
|----------------|----------|----------------------|
| `KEY_ENTER` | 確認／選取 | ✅ 在 `RingEvdevReader.TAP_LIKE` 內 → 手勢 `TAP` → **拍照** |
| `KEY_DASHBOARD` | **開啟 Rokid 助手** | ❌ 不在 `TAP_LIKE` → 忽略（正確） |
| `KEY_PROG1` | 滑動 | ❌ 忽略 |
| `KEY_BACK` | 返回 | ❌ 忽略 |

→ 所以「用觸控板開助手」時，你的**輕觸同時會讓我們拍照**（`KEY_ENTER`），`KEY_DASHBOARD` 才是開助手。

### 9.3 `ring_events.log` 為何是 0 bytes（不是因為沒事件）

該檔在 19:58 由 `RingEvdevReader.start()` `rm -f` 重建，20:11 硬重置（PS_HOLD）時
**頁快取的內容還沒落盤就被切斷**，所以檔案留在 0 bytes / mtime 19:58。
**不能拿它證明「沒有手勢」**——這點我先前也說錯了。

### 9.4 崩潰原因：仍以「LPE 延遲 panic」最可能，但**尚未取得決定性證據**

支持：
- `gl_run.log`：19:54 exploit 成功（`child is root!` → `exploit complete`）。
- 20:11:55 重開：`ro.boot.bootreason=reboot`，dmesg 顯示前次關機為
  **PS_HOLD（MSM Controlled Shutdown，軟體式乾淨重置）**，`/sys/fs/pstore` **空**。
- 今天四次重開（17:49:23／18:14:09／18:44:55／20:11:55）**特徵完全一致**，
  而前三次都與助手無關。
- 專案自身文件：失敗 = kernel panic，且「成功也可能留下 UAF 損傷，等之後被踩到才爆」；
  助手啟動會建立新的執行緒／鎖／futex，正好是會碰到被破壞的 rt_mutex 結構的動作
  → 助手較可能是**觸發點**而非原因。

**仍不能完全排除**的替代假設（目前無證據）：
- 助手（`com.rokid.os.sprite.assistserver`）與我們的 `CaptureService` 爭用**相機／麥克風**；
  且輕觸同時讓我們拍照，可能造成時間上的衝突。
- 需要 `/data/system/dropbox`（`SYSTEM_TOMBSTONE`／`system_server_wtf`／`SYSTEM_LAST_KMSG`）
  或 `/data/misc/bootstat/bootstat.log` 才能判定；**兩者都需要 root**，而重開機後 root 已失效。

### 9.5 可用的兩條取證路徑

1. **再取得 root**（單次受控流程，約 130 秒）→ 讀 dropbox／bootstat／tombstones。
   ⚠ 每次提權仍有 ~50% panic 風險。
2. **即時重現**：戴著眼鏡、我同步 `logcat` 雙機擷取，你再操作一次觸控板開助手；
   若真的崩潰，崩潰前一瞬間的 log 會被我們抓到（這是最直接的）。

---

## 10. 追加（20:31–20:36）：**即時重現實驗——沒有重現崩潰**

依使用者選擇，改用「即時重現」取證：把眼鏡的 **kernel log（`dmesg -w`）** 與 Android logcat
串流到 **PC 端檔案**（`build-cache/repro_dmesg.log` / `repro_logcat.log` / `repro_phone.log`），
這樣即使眼鏡硬重置，崩潰前收到的內容仍會留在 PC。

### 10.1 結果：**沒有當機**

- 眼鏡 uptime 由 1434s 持續遞增，**沒有重開**。
- `repro_dmesg.log` 內 **沒有任何** panic／oops／RCU stall／hung task（7 個關鍵字命中全是開機時的
  cmdline／ramoops／watchdog 初始化字串）。
- 使用者在此期間確實操作了觸控板：kernel log 持續出現
  `Rokid,PSOC-TP-R 1-0008: psoc_ts_interrupt` / `psoc_report_in work`。

→ **「開助手」本身不會穩定造成當機**，20:11 那次不是必然的因果。

### 10.2 捕捉到的兩個有意義現象

**(a) 助手確實會注意到我們的相機被佔用**
```
09-13 20:31:56.578  2017  2348 W AssistServer_Log:
    CameraController -> CameraManager Camera Un Available: 0
```
我們的 `CaptureService` 開著相機時，助手的 `CameraController` 會收到「Camera Un Available」。
**目前沒有造成錯誤或崩潰**，但兩者確實會互相感知。

**(b) 相機工作佇列常被延遲**
```
CAM_WARN: CAM-UTIL: cam_common_util_thread_switch_delay_detect: 158
    CRM workq schedule delay detected … diff 10: threshold 5
```
整個捕捉期間出現 **49 次**（多為 `CAM-SYNC`／`CRM` workq 延遲）。屬於警告等級、不致命，
但顯示相機路徑在負載下確實會延遲。

### 10.3 同一時間發現：**眼鏡觸控板路徑目前是停的（缺 root）**

```
20:31:52 RingEvdevReader: start(/dev/input/event1) …
20:31:52 RingEvdevReader: start(/dev/input/event1) getevent 執行中=false   ← 啟動失敗
20:32:05 CommandRouter: recv: CAPTURE_PHOTO id=120                        ← 這張是手機端送的
```

`getevent` 需要 root，而 20:11 重開後 root 已失效 → `running=false`。
所以該期間產生的 `IMG_1789302725966.jpg`／`VID_1789302732678.mp4` 是**手機端觸發**的，
**不是**觸控板。

### 10.4 綜合判斷（更新）

| 假設 | 目前證據 |
|------|----------|
| **LPE 延遲 panic**（主因） | 20:11 重開與今天前三次特徵一致（PS_HOLD 乾淨重置、pstore 空），前三次與助手無關；重現實驗卻不當機 → **機率性、與提權時間相關** |
| 助手與相機衝突 | 助手確實會看到 `Camera Un Available`，但重現期間**沒有**造成崩潰 → 目前只能算「次要觸發因子」 |
| 觸控板／我們的程式 | 重現期間觸控板路徑根本沒啟動（無 root）→ 可**排除**是它造成 20:11 那次 |

→ 要取得決定性證據（panic trace）仍需 **root 讀 `/data/system/dropbox`**，
或**在崩潰發生時剛好有即時串流**（目前已架好，下次真的當機就能抓到）。

---

## 11. 追加（20:38–20:45）：AR 畫面「灰底」問題

使用者回報「不要灰底、要透明底色」。逐項查證：

### 11.1 先排除：我們的面板本身沒有畫任何底色

- `activity_main.xml` 的 `panel` / `collapsedBar` / `expandedPanel` **都沒有 `android:background`**
  （唯一的背景是 `bg_dot_gray`／`bg_dot_green` 兩顆 8dp 狀態圓點）。
- `MainActivity` 只在 `dotConn` 上 `setBackgroundResource()`。
- **`#1F1F1F`（使用者截圖中灰底的實際顏色，以 PIL 取樣為 `(31,31,31)`）在整個 App 中不存在**：
  全部顏色只有 `#00000000 / #000000 / #FFFFFF / #E8EDF5 / #F2F4F8 / #9AA5B8 / #8A94A6 / #5A6478 / #FFC24B / #4FC3F7 / #3DDC84 / #0B1220`。
- 裝置端 `screencap` 取樣：面板區域是 **純黑 `(0,0,0)`**，只有文字像素非黑 → **裝置渲染沒有灰底**。

### 11.2 但找到一個**真實的矛盾**（已修）

| 位置 | 原本 | 問題 |
|------|------|------|
| `activity_main.xml` 註解 | 「**全透明（無底色、無面板背景）**——文字直接浮在視線上，不遮蔽視野」 | 設計意圖是透明 |
| `themes.xml` | `android:windowBackground = @android:color/black`（**不透明**） | 整個 480×640 視窗被不透明黑蓋住，與上面的意圖**直接矛盾** |

**已修**（`themes.xml`）：
```xml
<item name="android:windowBackground">@android:color/transparent</item>
<item name="android:colorBackground">@android:color/transparent</item>
<item name="android:windowIsTranslucent">true</item>
<item name="android:backgroundDimEnabled">false</item>
```
**驗證**（`dumpsys window windows`）：
```
mAttrs={(0,0)(fillxfill) … fmt=TRANSPARENT …}        ← 修正前為 OPAQUE
snapshot=TaskSnapshot{ … mIsTranslucent=true … }     ← ✅ 透明生效
```

### 11.3 一併降低通知的干擾

Android 規定前景服務必須有通知，但 AR 眼鏡上任何彈出卡片都會擋視線。原本通道 `capture`
為 `IMPORTANCE_LOW`；改為新通道 **`capture_quiet`＝`IMPORTANCE_MIN`＋靜音**
（通道 importance 建立後無法由程式調降，故必須換 id）。
實測 ROM 會把 FGS 通知的 `mOriginalImp=1` 拉回 `mImportance=2`，但已是 `groupKey=silent`、
**不會彈出**。

### 11.4 尚待確認：使用者截圖中那個「**圓角**灰底條」

截圖裡的灰底條是 **圓角**、上面同時有我們的字「Rokid Capture」與「點按收起」。
但我們的版面**既沒有底色、也沒有圓角**，裝置渲染也量不到灰底 →
那個圓角灰卡很可能是 **系統替我們的視窗畫的外框** 或 **鏡像工具自己的 UI**，
不是我們的 layout。

→ 已把「視窗不透明」這個唯一屬於我們的可能性修掉並重新安裝；請使用者再截一次圖確認：
若灰底仍在，就不是本 App 畫的，需要往系統／鏡像工具查。

---

## 12. 追加（20:45–21:00）：灰底**真正根因**找到了 —— 是 Android 的**焦點高亮**

### 12.1 根因

`activity_main.xml` 的 `panel` 設了 `OnClickListener` → 它是 **clickable**。
眼鏡的主力輸入是**觸控板（D-pad 類）而不是觸控螢幕**，所以這個可點擊的 View 會**取得焦點**，
Android 便在其上疊一層 **default focus highlight**（半透明白、**圓角**）。

→ 這就是使用者看到的「灰底」：**圓角、淺灰、剛好貼著面板**。

### 12.2 三個決定性實驗（都不是猜的）

| 實驗 | 做法 | 結果 |
|------|------|------|
| **A. 把視窗底色改成純綠** | `windowBackground=#FF00FF00` | 灰底區量到 **`(31,255,31)`** = 綠色被疊上約 **12% 白** → 灰底是**畫在我們視窗之上**的一層，不是我們的底色 |
| **B. 把面板 paddingBottom 由 8dp 改成 44dp** | 面板變高 | **灰底跟著變成 y=544..640**（原本 592..640）→ 灰底的範圍**完全等於 `panel` 的 bounds** |
| **C. 切到啟動器（無聚焦 View）** | HOME | 同一區域 **純黑 `(0,0,0)`** → 只有我們的可聚焦面板才有這層 |

（B 排除了「系統固定 48px bar」的假設——我一開始就是這樣誤判的。）

### 12.3 修正

```xml
<LinearLayout
    android:id="@+id/panel"
    ...
    android:defaultFocusHighlightEnabled="false"   <!-- ← 加入這行 -->
```
驗證（`screencap` 取樣）：
```
修正前 y=560..636 → (31,31,31)   ← 灰帶
修正後 y=560..630 → (0,0,0)      ← 只剩文字像素，純黑
```

### 12.4 同時回退與釐清的事

- **回退 `windowIsTranslucent=true`**：實測會讓**背後的啟動器圖示／文字整個透出來**，與儀表板疊在
  一起（使用者回報的「混在一起」）。**AR 眼鏡上「透明」的正確實作是不透明黑**——黑色不發光，
  配戴者看到的是真實世界，同時擋掉背後 App。已還原 `windowBackground=@android:color/black`。
- **全螢幕沉浸模式對這層灰底無效**（已從程式移除，只留註解說明）。
- 前景服務通知維持 `capture_quiet`（`IMPORTANCE_MIN`＋靜音）。

---

## 13. 追加（20:33–21:13）：40 分鐘即時串流結果 —— **期間沒有重開機**

`build-cache/repro_capture.sh`（眼鏡 kernel log ＋ 兩台 logcat 串流到 PC）跑完 2400 秒。

```
眼鏡 uptime          3504s（連續，期間未中斷）
boot history 最新一筆  reboot,1789301515  ← 仍是 20:11:55 那次；之後沒有新的
dmesg 內 panic/oops/RCU stall 命中 3 筆 → 全部是開機時的 cmdline／ramoops 字串，非真事件
```

### 13.1 有意義的觀察：**沒有 root 的這 58 分鐘完全穩定**

20:11:55 那次重開之後，`root_mode=MANUAL`、斷線後 root 也不再武裝 —— 期間：
- 多次觸控板操作、開助手、拍照／錄影、我們改版重裝多次
- **完全沒有再重開機**

對照今天四次重開（17:49／18:14／18:44／20:11）都發生在**有 root 武裝**的期間 →
「重開機 ↔ 提權」的相關性又更強了一分（但仍非決定性證明，因為也可能是機率）。

### 13.2 這次串流的限制（下次要改）

`adb shell dmesg -w` 的連線在 **20:33 就斷了**（檔案不再成長），logcat 也只到 20:46。
原因是 adb 連線被後續指令／裝置忙碌打斷，而腳本不會重連。
→ 若之後要「等它真的 panic 再抓現場」，需要把三個串流加上**自動重連**的迴圈。
