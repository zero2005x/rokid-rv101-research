# 戒指操控拍照／攝影 — 來源授權與硬編碼資安稽核

- **日期**：2026-09-13
- **範圍**：`android/shared/.../ring/`（5 檔）、`android/app-glasses/.../ring/`（2 檔）、
  `android/app-phone/.../ring/`（2 檔）、`deploy/ring_input_daemon.c`、
  兩端 `AndroidManifest.xml`、`shared/protocol/Protocol.kt`、`net/WifiServer.kt`
- **問題**：(1) 戒指操控拍照／攝影是否需要**藍芽配對確認機制**？(2) 有無**硬編碼造成的資安疑慮**？
- **方法**：靜態逐檔閱讀 ＋ 本機／實機量測（`dumpsys package`、`dumpsys input`、
  `dumpsys bluetooth_manager`、`getevent -pl`、`ip addr`、`netstat`、root 讀取 App prefs、
  實際廣播投遞測試）。**未執行**任何破壞性操作，未新增任何照片／影片。

> **結論先講**：**需要**，而且目前**兩條路徑都沒有任何來源驗證**。
> 「誰按的」在程式碼裡完全不存在——手機端只看到 keycode，眼鏡端只用**裝置名稱子字串**挑裝置；
> 而兩端各有一個 **exported 且無 permission 的除錯廣播接收器**，讓「強制配對」「強制拍照」
> 「取得 root」變成任何已安裝 App 都能觸發的操作。另有一個**寫死在 APK 的 WiFi 密碼**
> 配上**零認證的 TCP 控制通道**。

---

## 1. 資料流與「來源」在何處遺失

### 路徑 A（手機端攔截，免 root）

```
戒指(BLE HID) ──┐
鍵盤/滑鼠      ──┼─→ Android InputDispatcher ─→ MainActivity.dispatchKeyEvent /
合成輸入(input)─┘                                dispatchTouchEvent / MediaSession
                                                        │  ⚠ 只傳 keyCode / button
                                                        ▼
                                            RingController.onKeyDown(code)
                                            RingController.onMouseButton(action, button)
                                                        │
                                            RingGestureDetector → RingAction
                                                        ▼
                                            Remote.capturePhoto() / startVideo() …
```

| 站點 | 檔:行 | 有沒有看「來源裝置」 |
|------|-------|---------------------|
| `dispatchKeyEvent` | `MainActivity.kt:120-126` | ❌ 只讀 `event.keyCode`／`event.action` |
| `dispatchTouchEvent`（滑鼠式戒指） | `MainActivity.kt:134-141` | ⚠ 只檢查 `event.source` 是否為 **MOUSE／TOUCHPAD 類**（不是「哪一台裝置」） |
| `dispatchGenericMotionEvent`（滾輪） | `MainActivity.kt:145-154` | ❌ 只讀 `axisValue` |
| `RingMediaSession.onMediaButtonEvent` | `RingMediaSession.kt:37-51` | ❌ 只讀 `e.keyCode`（**背景**路徑） |
| `RingController.onKeyDown` | `RingController.kt:160-177` | ❌ 簽章只有 `code: Int` |
| `RingController.onMouseButton` | `RingController.kt:199-225` | ❌ 簽章只有 `(action, button)` |

**`KeyEvent.getDeviceId()` / `MotionEvent.getDeviceId()` / `InputDevice` 從未被讀取。**
唯一的分類依據是「按鍵是否落在 `INTERESTING_KEYS`」與 prefs 中的自訂鍵碼表
（`RingController.kt:144-158`）。

### 路徑 B（眼鏡端 root 直讀 evdev）

```
root_daemon(root) ──setsid──> getevent -lt <device> >> /data/local/tmp/ring_events.log
     ▲                                                             │
     │ exec("tail -c +offset …") 每 400ms                          │
App(untrusted) ─ RingEvdevReader ─ RingControlManager ─ CameraController.takePhoto()
```

| 站點 | 檔:行 | 有沒有看「來源裝置」 |
|------|-------|---------------------|
| `autoDetect(pattern)` | `RingEvdevReader.kt:48-52` | ❌ **名稱子字串**比對（`RGR`，退化時 `RING`），**取第一台命中** |
| `RingControlManager.start()` | `RingControlManager.kt:84-108` | ❌ 只要 `ring_ctrl.device` 有值就**完全跳過偵測**，直接沿用 |
| 事件解析 | `RingControlManager.kt:237-258` | ❌ 只解析 `EV_KEY`／`EV_REL` 標籤，無裝置欄位 |

---

## 2. 發現（依嚴重度）

### R1 — 路徑 A 接受**任何** HID 來源（High）

沒有任何一處比對事件來源，因此下列全部都能觸發拍照／錄影／靜音／顯示切換：

- 藍牙鍵盤、藍牙滑鼠、USB 鍵盤（送 `ENTER`／媒體鍵／`F1`–`F12`／滑鼠鍵者）；
- 手機內建的 **`uinput-fpc`（指紋感測器 uinput 鍵盤）** 與 `mt6877-mt6359 USB_3_5 Jack`；
- **別的廠牌戒指**——實機 `dumpsys input` 顯示 **`AIVELA_RP13EJ37`（device 22）** 目前正以
  input device 身分連在手機上（`/proc` 級鍵盤＋滑鼠＋觸控板），它的事件同樣會被
  `RingController` 吃掉；
- **合成／注入輸入**：本專案自己的報告已記錄 `input keyevent 131`、
  `input mouse tap` → `TAP → 已拍照`（`reports/ring_control_report.md` §5.2）。
  合成事件沒有任何實體戒指參與，證明「是不是戒指」在程式裡**無法區分**。

> 實機佐證（本次）：手機 `ring_ctrl` prefs = `enabled=true`、`gesture_count=171`、
> 自訂對應 `map_4097 → PHOTO`（滑鼠主鍵 → 拍照）。也就是說**在我們的 App 前景時，
> 任何滑鼠的左鍵點擊都會拍一張照**（連點 UI 按鈕也不例外）。

### R2 — 路徑 B 用「裝置名稱」當身分，且目前指向的**不是戒指**（High）

- 選裝置＝`name.contains("RGR")`（大小寫無關）取第一台；`RING` 為退化選項。
- **實機現況**：眼鏡 `ring_ctrl` prefs = `pattern=RGR`、`device=/dev/input/event1`、
  `enabled=true`；而眼鏡的輸入裝置只有兩台——
  `event0 = qpnp_pon`（電源鍵）、**`event1 = ROKID,PSOC-TP-R`（眼鏡自己的觸控板）**。
  眼鏡的已配對藍牙清單只有一支手機與一台平板，**沒有任何戒指**。
- 因為 `device` 有值就跳過偵測，`pattern=RGR` 實際上是**死設定**：今天在眼鏡上驅動拍照／錄影的
  是**眼鏡本身的觸控板**。

### R3 — 「掃到名字像的就自動配對」，沒有使用者確認（High）

`RingBonder.scanAndBond()`（`RingBonder.kt:107-135`）：掃描 → 取**第一個**
廣告名稱含 `RGR` 且可連線的裝置 → **立刻 `createBond()`**。

- BLE 廣告名稱由周邊**自行宣告**，不是身分。攻擊者用一台廣告 `RGR06` 的 BLE 裝置
  即可被自動配對；配對後就能注入 HID 事件 → 觸發拍照／錄影（R1）。
- 呼叫點：`SettingsFragment.kt:204`（UI 按鈕）、`DebugReceiver.kt:145,164-175`（手機）、
  `DebugReceiver.kt:52-54`（眼鏡）。其中 `WATCH_RING` 預設 `autoBond=true`，
  會持續數十輪掃描並在第一台可連線時自動配對。
- 程式**沒有**任何「顯示候選裝置（名稱／MAC／RSSI）→ 使用者按確認」的步驟；
  唯一的關卡是作業系統 SMP 流程（Just Works 周邊可能不跳任何視窗）。
- 註：`RGR06` 為 **random address**（`D1:03:CB:ED:C7:97`）。配對後 Android 會回報
  identity address，因此**配對後的 MAC 可作為穩定識別**；但**配對前**的廣告位址會輪替，
  不能當身分。

### R4 — 兩端各有 **exported 且無 permission** 的除錯接收器（High）

`AndroidManifest.xml`（手機 `:43-49`、眼鏡 `:67-73`）：

```xml
<receiver android:name=".DebugReceiver" android:exported="true">
    <intent-filter><action android:name="com.rokid.rc.phone.DEBUG" /></intent-filter>
</receiver>
```

- **沒有 `android:permission`** → **任何已安裝 App** 都能送這個廣播。
- 實機確認（本次）：`dumpsys package` 的 receiver resolver 區塊只有 `Action:`，
  沒有 `Permission:`；且實際投遞成功（foreign uid 送出 → `DebugReceiver` 執行）。
- 可觸發的指令（`DebugReceiver.kt`）：

| 端 | 指令 | 效果 |
|----|------|------|
| 手機 | **`CAPTURE_PHOTO`／`START_VIDEO`／`STOP_VIDEO`** | 遠端拍照／錄影 |
| 手機 | **`ACQUIRE_ROOT`** | 觸發 LPE 提權 |
| 手機 | **`WIFI_ADB`**、`WIFI_CONNECT --es host <任意 IP>` | 開無線 adb／連任意主機 |
| 手機 | **`PAIR_RING`／`UNPAIR_RING`／`WATCH_RING`（autoBond）** | **強制配對攻擊者的戒指** |
| 手機 | `SET_LED`、`SET_GHOSTLOCK`、`SET_SILENT`、`SET_DISPLAY`、`DELETE_MEDIA <id>`、`SET_RING` | 改裝置狀態／刪媒體 |
| 眼鏡 | **`RING_START`／`RING_STOP`／`RING_DIAG`**、`PAIR_RING` | 控制／改綁戒指來源 |
| 眼鏡 | `SET_DISPLAY`、`TOGGLE_PANEL`、`SET_LOCALE` | 改顯示／介面 |

- 另外 `<service android:name=".CaptureService" android:exported="true" />`（眼鏡 `:53-56`）
  **也沒有 permission** → 任何 App 可拉起相機前景服務；若 `rootMode=AUTO`，該路徑會**順帶跑提權**
  （`CaptureService.kt:81-89`）。
- 手機端 `onReceive` **不檢查 `intent.action`**（只檢查有無 `cmd` extra）→ 連 action 都不必對。

### R5 — 媒體／控制通道零認證 ＋ AP 密碼寫死在 APK（High）

- `WifiServer.kt:30`：`ServerSocket(8555)` 綁**所有介面**，`accept()` 任何來源
  （只印 `remoteSocketAddress`），交給 `CommandRouter`——**沒有 token／握手驗證**
  （`CommandRouter.kt:32` 的 `HELLO` 只回 `HELLO_ACK`）。同一條通道可 `CAPTURE_PHOTO`、
  `DOWNLOAD_MEDIA`、`DELETE_MEDIA`、`ACQUIRE_ROOT`、`ENABLE_WIFI_ADB`。
- `Protocol.kt:14-15`：`AP_SSID = "RokidCam"`、**`AP_PASS = "rokid1234"`**（固定 WPA2 密碼，
  隨 APK 散布；`WifiHotspot.kt:24-25,52` 直接用它開 SoftAP，`status()` 還會回傳給手機）。
- 藍牙 RFCOMM 端 `BluetoothServer.kt:37-42`：`accept()` 後**只記錄** `remoteDevice.address`，
  **沒有允許清單** → 任何已配對裝置都能下指令。
- **實機現況（避免誇大）**：眼鏡 `netstat` 顯示 `[::]:8555 LISTEN`，但目前
  `wlan0 = NO-CARRIER/DOWN`、無 `wlan2`（SoftAP 未開）→ **此刻外部不可達**。
  一旦開 SoftAP（root 後）或眼鏡連上一般 WiFi（文件記錄過連上**開放網路 `iTaiwan`**），
  同網段任何主機都能用公開密碼／零認證連進來操作相機。

### R6 — `"RGR"` 這個「唯一的身分依據」被寫死 11 處（Medium）

| 位置 | 檔案:行 |
|------|---------|
| shared | `RingBonder.kt:109`、`RingWatcher.kt:31`、`RingEvdevReader.kt:48,51` |
| 手機 | `DebugReceiver.kt:145,162`、`SettingsFragment.kt:204` |
| 眼鏡 | `CommandRouter.kt:92`、`RingControlManager.kt:85`、`DebugReceiver.kt:53,102` |
| C（未採用） | `ring_input_daemon.c:13` |

它同時是**安全參數**（唯一選裝置依據）與**散落字面值**：任何一處改動就會讓
「配對的裝置」與「實際監聽的裝置」不一致，而失敗模式是**靜默的**（挑不到裝置只記 log）。
`scripts/audit_hardcode.py` 目前**不會**把它列為重複字面值。

### R7 — `chmod(..., 0777)` 的 UNIX socket（Medium，未採用實作）

`deploy/ring_input_daemon.c:189`：`chmod(sock_path, 0777)` → 任何 uid 都能連上並**接收手勢事件**
（等同旁路側錄使用者何時按了戒指）。該檔開頭已標明「**備援實作（未採用）**」，
但若日後啟用就會直接帶進這個權限。

### R8 — 稽核工具本身沒有涵蓋這一類問題（**已修**，且修完立刻又抓到兩項）

`scripts/audit_hardcode.py` 檢查：中文字面值、Kotlin 硬編碼路徑／IP／MAC／埠、版面 XML、
重複字面值、腳本序號／Windows 路徑。**未檢查**：

1. `android:exported="true"` 而無 `android:permission` 的元件；
2. 憑證型字面值（`pass`／`password`／`token`／`secret`／`key` 指派常數）；
3. native 端的 `chmod` 權限。

→ 這正是 **R4／R5 這種「最嚴重的一類」在先前 `reports/hardcode_audit.md` 只被記成
「開發工具訊息」**的原因。

**已於本次補上第 6／7／8 節**，補完立刻額外抓到 **R10** 與 **R11**（見下）。

### R9 — 未採用的 native 備援版建置腳本仍指向不可用的工具鏈（Low）

`deploy/build_ring_input_daemon.sh:5,9-12` 寫死 Windows NDK 路徑與
`/home/kali/rokid_root`。本機 WSL 對 `/mnt/c` 為唯讀、NDK 為 Windows 版 → **此腳本不可能成功**；
專案已改用 `deploy/build_probe_zig.sh`（zig 交叉編譯），此檔未同步。

### R10 — **root daemon 的 socket 是 0777，且 SELinux 為 Permissive → 任何 App 直接取得 root**（Critical）

由新增的第 8 節規則抓到（`deploy/root_daemon.c:132`），實機確認：

```
$ adb shell 'getenforce; ls -ld /data/local/tmp; ls -laZ /data/local/tmp/root_daemon.sock'
Permissive
drwxrwx--x 11 shell shell 20480 2026-09-13 15:44 /data/local/tmp          ← other 有 x（可穿越）
srwxrwxrwx  1 root  root  u:object_r:shell_data_file:s0  0 … root_daemon.sock  ← world rwx
```

- `/data/local/tmp` 為 `0771`：other **可穿越**（`x`），足以 `connect()` 到已知路徑。
- socket 為 **`0777`**：任何 uid 可讀寫。
- `getenforce` = **Permissive**（本專案提權後會設定）→ **SELinux 不會擋**。

→ 在這台已提權的眼鏡上，**任何已安裝 App 都能 `nc -U /data/local/tmp/root_daemon.sock`
取得 uid 0 的 shell**，不需要 GhostLock、不需要任何權限。
這是本專案**自己引入**的介面（不是 Rokid 的），也是戒指路徑 B 的**信任基礎**
（`RingEvdevReader` 的所有 `exec` 都經它）。

> ⚠ 這比本報告其他所有項目都嚴重，但**不在原本的戒指範圍內**（是戒指稽核的工具規則順帶抓到的）。
> 修法需要設計決定（見 §4 P0'）：最小改動是 `0600` ＋ 由 App 端改走 binder／
> SELinux 允許的通道；若必須讓 untrusted_app 連，則需要**每次啟動的權杖**或專用 SELinux type，
> 而不是 0777。**未經確認不動手**。

### R11 — 硬編碼的 OTA API 憑證被寫進 scripts（High）

`scripts/rokid_ota_check.py:25-28`：

```python
KEY             = "E217806228724620BC1A6924DD542B5"
SECRET          = "<REDACTED_OTA_SECRET>"
DEVICE_TYPE_ID  = "<REDACTED_OTA_DEVICETYPEID>"
DEVICE_ID       = "<DEVICE_SERIAL>"
```

- 用途：對 `https://ota.rokid.com/v1/extended/ota/check` 產生 `sign`（MD5）的共用憑證。
- 來源：註解自承「from `adb shell getprop`」——實機確認其中兩項來自 **bootloader 屬性**
  （`ro.boot.secret`、`ro.boot.devicetypeid`），即**燒進裝置的 provisioning 值**。
- 風險：若 `KEY`／`SECRET` 為**整批產品共用**，等於把 OTA API 憑證公開在 repo；
  即使是逐機值，也不該寫死（應在執行時讀 `getprop`，或改由環境變數／本機設定檔提供）。
- 處置：改為執行時讀取＋允許環境變數覆寫；並在 repo 內註明該值來源。
  **未經確認不動手**（涉及既有研究腳本的可重現性）。


---

## 3. 回答：「是否需要藍芽配對的確認機制？」

**需要，而且是兩個不同的東西，目前兩個都沒有。**

| # | 需求 | 現況 | 為什麼必要 |
|---|------|------|-----------|
| **A. 來源身分綁定** | 只接受「被認可的那顆戒指」的事件 | ❌ 手機端完全不看來源；眼鏡端只看**名稱子字串** | 名稱與 keycode 都是可偽造／可共用的。沒有綁定＝任何 HID 裝置（含他人的戒指、鍵盤、`input` 注入）都能拍照錄影 |
| **B. 配對時的使用者確認** | 掃到候選裝置 → **顯示名稱／MAC／RSSI** → 使用者按下確認 → 才 `createBond()`；並把該裝置**持久化為唯一認可的戒指** | ❌ `scanAndBond()` 取第一台名字像的**直接配對** | BLE 廣告名稱由對方決定；沒有確認流程＝方圓數十公尺內任何人都能讓自己被配對 |

**建議的最小可行設計**（尚未實作，等確認）：

1. **持久化認可清單**：新增 `ring_ctrl.approved_mac`（＋`approved_name`），
   由使用者在配對時寫入；未設定時**不接受任何手勢**（fail-closed）。
2. **路徑 A 綁定**：`MainActivity` 改傳 `event.deviceId`；`RingController` 以
   `InputDevice.getDevice(deviceId)` 取 `name`（必要時再比對
   `BluetoothAdapter.bondedDevices` 的 identity address）→ 與認可清單不符即**放行不攔截**。
   （注：Android 12 的 `InputDevice.getDescriptor()` 是雜湊後的值、**不含 MAC**，
   所以要比對的是 `getName()` ＋已配對清單，而非 descriptor。）
3. **路徑 B 綁定**：`autoDetect()` 改為「必須命中認可裝置名稱」；並在 `start()` 時
   對 `device` 做一次同樣比對（現在 `device` 有值就跳過檢查）。若戒指是 HID，
   可再以 `EVIOCGUNIQ`／sysfs 取唯一序號補強（此代裝置未驗證，列為待驗）。
4. **配對確認 UI**：`scanAndBond` 拆成 `scan()` 與 `bond(device)` 兩步，
   中間插入候選清單對話框（名稱／MAC／RSSI／是否 HID 0x1812）；移除
   `WATCH_RING` 的 `autoBond=true` 預設。
5. **每次手勢的回饋**：拍照／錄影屬「有實體副作用」的動作，建議除既有 logcat 外，
   在眼鏡顯示器上給一次性提示（目前 `RingOutcome` 已可承載，屬產品決策）。

---

## 4. 建議修復清單

### 4.1 本次已套用（「零風險清理」範圍，已驗證）

| 項目 | 對應 | 改動 | 驗證 |
|------|------|------|------|
| ✅ `"RGR"` 集中為單一常數 | R6 | 新增 `android/shared/.../ring/RingIdentity.kt`（`DEFAULT_NAME_PATTERN`／`FALLBACK_NAME_PATTERN`／`TOUCHPAD_NAME_PATTERN`／`orDefault()`），改寫 11 處引用（shared 3、手機 3、眼鏡 5），補 3 個 import | `./gradlew :app-glasses:compileDebugKotlin :app-phone:compileDebugKotlin --offline` → **BUILD SUCCESSFUL**；`grep '"RGR"'` 只剩常數檔 |
| ✅ `chmod(0777)` → `0600` | R7 | `deploy/ring_input_daemon.c`（含說明：0600 會讓 untrusted_app 連不上，若真要採用需**存取控制設計**而非放回 0777） | zig 交叉編譯通過（aarch64 ELF） |
| ✅ 稽核工具補 3 條規則 | R8 | `scripts/audit_hardcode.py` 新增第 6 節（exported 且無 permission；含 launcher／受保護廣播的降噪）、第 7 節（憑證型字面值）、第 8 節（world-accessible `chmod`） | 實跑即抓到 R4/R5/R10/R11；已修正 `lastKey` 偽陽性與 `BootReceiver` 系統廣播降噪 |

> 這三項**不改變任何行為**（`RGR` 值相同、C 檔本來就未採用、稽核腳本唯讀）。
> 也**尚未重新建置／安裝 APK 到裝置**——上面只做到 Kotlin 編譯通過。

### 4.2 待確認（會改變行為或 UX，**尚未動手**）

| 優先 | 項目 | 對應 | 改動 | 影響面 |
|------|------|------|------|--------|
| **P0′** | **root daemon socket 收斂**（⚠ 比本報告其他項目都嚴重） | R10 | 現況 `0777` ＋ SELinux Permissive ⇒ 任何 App 可取得 root shell。最小改動 `0600`，但 App 端（untrusted_app）就連不上 → 需改成 **binder／權杖／專用 SELinux type** 的設計 | 會動到眼鏡端**所有** root 功能（提權、戒指、靜音、LED），需完整回歸 |
| **P0** | 除錯接收器加權限 | R4 | 兩端 manifest 加 `android:permission="android.permission.DUMP"` | 已在**兩台實機確認 `shell` 持有 `DUMP`** → `adb shell am broadcast` 自動化**不會壞**，但第三方 App 被擋（已由 `reports/ring_reachability_test_2026-09-13.txt` 證實目前擋不住） |
| **P0** | `CaptureService` 收斂 | R4 | 改 `exported="false"` | 同 App 內啟動不受影響；`checkComponentPermission` 對 **uid 0／system** 一律放行，故 **root daemon（uid 0）的 `am start-foreground-service` 仍可用**；但 **`adb shell`（uid 2000）驅動的啟動會被拒** → 需回歸一次（`deploy/test_shizuku_e2e.sh:72` 會受影響） |
| **P1** | 戒指來源綁定（A） | R1,R2 | 見 §3 步驟 1–3 | 需改 3 檔＋新增 prefs |
| **P1** | 配對確認 UI（B） | R3 | 見 §3 步驟 4 | 需改 UI |
| **P1** | OTA 憑證改為執行時讀取 | R11 | `scripts/rokid_ota_check.py` 改讀 `getprop`／環境變數，不寫死 | 影響該腳本可重現性（需一併更新文件） |
| **P1** | AP 密碼改為每台隨機 | R5 | 首次啟動產生隨機 `AP_PASS` 存 prefs，經配對通道告知手機 | 需改 `Protocol`／`WifiHotspot`／手機端加入 AP 流程 |
| **P3** | 媒體通道加權杖 | R5 | `HELLO` 帶一次性權杖（由藍牙配對通道交換） | 較大改動 |
| **P3** | 備援建置腳本改用 zig | R9 | `build_ring_input_daemon.sh` 改呼叫 `build_probe_zig.sh` | 未採用路徑 |

---

## 5. 本次實際執行的量測（可重現）

```
ADB="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe"

# 1) exported 接收器（兩端）：resolver 區塊無 Permission:
"$ADB" -s <PHONE_SERIAL> shell 'dumpsys package com.rokid.rc.phone'   | grep -A3 'com.rokid.rc.phone.DEBUG'
"$ADB" -s <DEVICE_SERIAL> shell 'dumpsys package com.rokid.rc.glasses' | grep -A3 'com.rokid.rc.glasses.DEBUG'

# 2) 廣播確實可被 foreign uid 投遞（無副作用指令：只做字串對照）
"$ADB" -s <PHONE_SERIAL> shell 'logcat -c; am broadcast -n com.rokid.rc.phone/.DebugReceiver \
    -a com.rokid.rc.phone.DEBUG --es cmd TEST_RING_TEXT; sleep 3; logcat -d -s DebugReceiver:*'
#   → DebugReceiver: onReceive cmd=TEST_RING_TEXT connected=true …（15 行對照輸出）

# 3) 手機目前的 input 裝置（AIVELA 戒指在線；指紋 uinput；USB jack）
"$ADB" -s <PHONE_SERIAL> shell 'dumpsys input' | grep -E '^ *[0-9]+: |Descriptor' | head -20
#   → 22: AIVELA_RP13EJ37 / 1: uinput-fpc / 5: mt6877-mt6359 USB_3_5 Jack …

# 4) 眼鏡的輸入裝置（只有電源鍵與自己的觸控板 → 沒有戒指）
"$ADB" -s <DEVICE_SERIAL> shell 'for f in /dev/input/event*; do echo -n "$f: "; getevent -i $f | grep -m1 name:; done'
#   → event0 qpnp_pon / event1 ROKID,PSOC-TP-R

# 5) 已配對裝置
"$ADB" -s <PHONE_SERIAL> shell 'dumpsys bluetooth_manager | grep -A8 "Bonded devices"'
#   → 有 AIVELA_RP13EJ37 / <DEVICE_ALIAS> …；無 RGR06
"$ADB" -s <DEVICE_SERIAL> shell 'dumpsys bluetooth_manager | grep -A4 "Bonded devices"'
#   → 只有 Redmi Note 11 Pro+ 5G / Redmi Pad SE

# 6) 戒指 prefs（眼鏡需 root）
printf 'cat /data/data/com.rokid.rc.glasses/shared_prefs/ring_ctrl.xml\n' | nc -U /data/local/tmp/root_daemon.sock
#   → pattern=RGR / device=/dev/input/event1 / enabled=true
"$ADB" -s <PHONE_SERIAL> shell 'su -c "cat /data/data/com.rokid.rc.phone/shared_prefs/ring_ctrl.xml"'
#   → map_4097=PHOTO / gesture_count=171 / enabled=true

# 7) 媒體通道
"$ADB" -s <DEVICE_SERIAL> shell 'netstat -tlnp | grep 8555; ip addr | grep -E "^[0-9]+: |inet "'
#   → tcp6 [::]:8555 LISTEN；wlan0 NO-CARRIER/DOWN、無 wlan2（此刻外部不可達）

# 8) shell 是否持有 DUMP（決定 P0 修法是否保留 adb 自動化）
"$ADB" -s <each> shell 'dumpsys package com.android.shell | grep "android.permission.DUMP"'
#   → granted=true（兩台皆是）
```

## 6. 信心與未驗證項

| 陳述 | 標記 | 信心 | 依據 |
|------|------|------|------|
| 兩條路徑都沒有來源裝置驗證 | FACT | **Very High** | 逐行閱讀；`RingController` 的簽章只有 `(code)`／`(action,button)`，無 `deviceId` |
| 任何 HID 來源都能觸發動作 | FACT | **Very High** | 無來源過濾 ＋ 專案既有實測（`input keyevent`／`input mouse tap` → 拍照） |
| AIVELA 戒指目前在手機上以 input device 身分在線 | FACT | **Very High** | `dumpsys input` device 22 |
| 眼鏡目前監聽的是自己的觸控板而非戒指 | FACT | **Very High** | prefs `device=/dev/input/event1` ＋ `getevent` 名稱 `ROKID,PSOC-TP-R` ＋ 已配對清單無戒指 |
| 兩個 DebugReceiver 為 exported 且無 permission | FACT | **Very High** | manifest ＋ `dumpsys package` resolver 無 `Permission:` ＋ 實際投遞成功 |
| 「任何第三方 App」可投遞（而非只有 shell） | ✅ **FACT（已實測）** | **Very High** | **端到端實證**：自製最小 App `com.ringaudit.reachtest`（**uid 10346**／眼鏡 **uid 10064**，皆非 shell 2000）投遞後由 `DebugReceiver`／`GlassesDebugReceiver` 實際執行；原始輸出 `reports/ring_reachability_test_2026-09-13.txt` |
| `scanAndBond` 會自動配對名稱相符的任意裝置 | FACT | **Very High** | `RingBonder.kt:107-135`；`WATCH_RING` 預設 `autoBond=true` |
| SoftAP 密碼寫死 | FACT | **Very High** | `Protocol.kt:15` |
| :8555 零認證 | FACT | **Very High** | `WifiServer.kt:30-45`、`CommandRouter.kt:32`；實機 LISTEN |
| 目前 :8555 對外**不可達** | FACT | **High** | 無任何 WiFi 介面 up；仍受情境影響（開 AP／連上網路即改變） |
| TEE／SELinux 是否另外擋住第三方 App 的廣播 | ✅ **已排除** | **Very High** | 眼鏡的 SELinux 為 Permissive、手機端實測成功；兩台皆無額外阻擋 |

**未執行（刻意）**：未發送 `CAPTURE_PHOTO`／`START_VIDEO`／`ACQUIRE_ROOT`／`PAIR_RING`／
`WIFI_ADB` 等任何有副作用的廣播；未對 `:8555` 送任何指令；未動任何藍牙配對；
未對 `root_daemon.sock` 以非 root uid 實際連線（R10 為權限位元＋`getenforce` 的靜態判定）。

**已執行的實機測試**：最小測試 App `com.ringaudit.reachtest`
（uid 10346／10064，**非 shell**）→ 兩端 `DebugReceiver` 均**實際執行**指令，
原始輸出見 `reports/ring_reachability_test_2026-09-13.txt`；測試 App 已自兩台裝置移除。

---

## 7. 與既有文件的關係

- `reports/ring_control_report.md` 描述的是**功能**實作與實測（拍照／錄影／手勢學習皆正常），
  與本報告不衝突；本報告補的是**授權面**。
- `reports/hardcode_audit.md` 把 `DebugReceiver` 的行動字串歸類為「開發工具」而未評估其
  **可達性**（exported／permission），本報告 R4／R8 更正此盲點。
- 權威事實已折入 `reports/FACTS.md` §7（P7–P10）與 §9（新未解項）；**若兩者不一致以 FACTS.md 為準**。
