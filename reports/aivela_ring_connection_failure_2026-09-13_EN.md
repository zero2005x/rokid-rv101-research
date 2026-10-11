# AIVELA Ring "Connection Failure" — Log Forensics and Remediation

> 中文版：[aivela_ring_connection_failure_2026-09-13.md](aivela_ring_connection_failure_2026-09-13.md)

- **Date**: 2026-09-13 15:5x–16:0x
- **Devices**: glasses RV101 `<DEVICE_SERIAL>` (Android 12 / API 32) · phone `<PHONE_SERIAL>` (Android 13 / API 33, MIUI V816)
- **Ring**: `AIVELA_RP13EJ37` = `BB:C3:13:73:8A:32` ([ DUAL ], already paired)
- **Method**: logcat on both ends, `dumpsys activity services` / `bluetooth_manager` / `input`, App prefs (root),
  the App's own debug broadcast and UI screenshots. **No** Bluetooth pairing was changed, **no** file was deleted.

---

## 0. One-sentence conclusion

**The ring has no connection problem — it was connected long ago and was driving photo capture** (4 shots taken in a row at 15:56).
The "connection failure" the user saw is the result of three things stacking up, and **not one of them is "pairing/connection" itself**:

| # | Event | Time | Consequence |
|---|------|------|------|
| 1 | The glasses App was **killed by lowmemorykiller** (RSS 100 MB) | 15:56:38 | camera and services all cut off |
| 2 | The service was rebuilt but **`startForeground()` was rejected by background restrictions** | 15:56:41 | the service is only an ordinary background service and can be reclaimed at any time |
| 3 | After the rebuild it received **`START_VIDEO`** (the ring button maps to video toggle) | 15:56:45 | `bindVideo()` sets `imageCapture = null` → **every later photo capture must fail** |
| 4 | Video recording got stuck (`Recorder` never got a video keyframe, no file produced) | 15:56:45→15:58 | the camera could not return to photo mode |
| 5 | Android stopped `CaptureService` for **app idle** | 15:58:09 | `onDestroy` → `server.stop()` / `wifiServer.stop()` → **Bluetooth channel closed** |
| 6 | All phone-side `CONNECT`/`CAPTURE_PHOTO` attempts failed | 15:59 | it showed "not connected" = what the user perceived as a "connection failure" |

Separately, **the "pair ring" button will never succeed for AIVELA** (see §2), but that is unrelated to the actual connection.

---

## 1. Evidence chain (raw logs, item by item)

### 1.1 The ring is connected, and it really was taking photos

```
# Phone: at the system level AIVELA is HID-connected (2 = STATE_CONNECTED)
$ dumpsys bluetooth_manager | sed -n '/mInputDevices/,/^$/p'
  mInputDevices:
    64:8F:DB:86:13:3E : 0        ← Xiaomi Buds (not connected)
    BB:C3:13:73:8A:32 : 2        ← AIVELA: connected

# Phone: AIVELA is registered as an input device (/dev/input/event7)
$ dumpsys input | grep -A3 AIVELA
  24: AIVELA_RP13EJ37
    Classes: KEYBOARD | ALPHAKEY | TOUCH | CURSOR | EXTERNAL
    Path: /dev/input/event7     Enabled: true

# Phone: the ring gestures really were received and executed by the App
09-13 15:56:27.418 RingController: 自訂鍵碼 KEYCODE_VOLUME_DOWN (25) → PHOTO（累計 173）
09-13 15:56:31.150 RingController: 結果：KEYCODE_VOLUME_DOWN (25) → 已拍照 IMG_1789286187697.jpg
09-13 15:56:32.228 RingController: 結果：TAP → 已拍照 IMG_1789286191153.jpg
09-13 15:56:32.769 RingController: 手勢 TAP → 動作 PHOTO（累計 175）
09-13 15:56:33.813 RingController: 結果：TAP → 已拍照 IMG_1789286192824.jpg

# Glasses: 4 files really landed
$ ls -la .../files/media/albums/alb-3b6dfe09/
  IMG_1789286187697.jpg  IMG_1789286191153.jpg
  IMG_1789286192824.jpg  IMG_1789286196069.jpg
```

### 1.2 Direct cause of death: LMK killed the App

```
09-13 15:56:38.195 E/lowmemorykiller: Memory Load: [2] {pid:26943} {size:100 MB} {name:com.rokid.rc.glasses} {oom_score:691}
09-13 15:56:38.231 I/lowmemorykiller: Kill 'com.rokid.rc.glasses' (26943), uid 10053, oom_score_adj 0 to free 102868kB rss, 22872kb swap
09-13 15:56:38.294 I/Zygote: Process 26943 exited due to signal 9 (Killed)
09-13 15:56:38.535 W ActivityManager: Scheduling restart of crashed service com.rokid.rc.glasses/.CaptureService in 1000ms
```

The trigger was **5 consecutive photo captures** (`CommandRouter: recv: CAPTURE_PHOTO id=15,16,17,18,19`, 15:56:27–37).

### 1.3 After the rebuild it could not enter foreground service

```
09-13 15:56:39.879 I/ActivityManager: Background started FGS: Allowed [... code:SHELL; tempAllowListReason:<shell,reasonCode:SHELL,duration:30000...>]
09-13 15:56:41.136 W/ActivityManager: Service.startForeground() not allowed due to bg restriction: service com.rokid.rc.glasses/.CaptureService
```

### 1.4 The camera was switched to video mode → later photo captures must fail

```
09-13 15:56:45.521 CommandRouter: recv: START_VIDEO id=20
09-13 15:56:45.577 ImageCapture: clearPipeline          ← bindVideo() has unbound the photo capture use case
09-13 15:56:45.618 Camera2CameraImpl: Use case VideoCapture-... INACTIVE
（for the next ~2 minutes）Recorder: Cached audio data while we wait for video keyframe before starting muxer
```

Code correspondence (`camera/CameraController.kt`):

```kotlin
private fun bindVideo() { ...; p.unbindAll(); p.bindToLifecycle(..., vc); recorder = rec; imageCapture = null }   // ← photo capability cleared
suspend fun takePhoto(ghost: Boolean): Result<MediaItem> {
    val ic = imageCapture ?: return Result.failure(IllegalStateException("camera not ready"))   // ← line 118: the message the user sees
```

- Therefore every `CAPTURE_PHOTO` after `15:56:45` (id=21,24,27,31–35) returned **`camera not ready`**,
  and the phone UI showed "**拍照失敗：camera not ready**".
- **This video recording produced no file at all** (the newest video in the album is still 09-12 02:56); `Recorder` kept waiting for a keyframe.

### 1.5 Finally the service was stopped and the channel closed

```
09-13 15:58:09.605 W/ActivityManager: Stopping service due to app idle: u0a53 -1h56m36s8ms com.rokid.rc.glasses/.CaptureService
09-13 15:58:09.637 E/CaptureService: wifi server error: Socket closed
09-13 15:58:26.791 D/BluetoothSocket: close() ... mSocketState: CLOSED      ← the RFCOMM server is closed too
09-13 15:59:50.394 DebugReceiver: CONNECT <DEVICE_ALIAS> result=Failure(java.io.IOException: read failed, socket might closed or timeout, read ret: -1)
```

`CaptureService.onDestroy()` performs `server.stop()` + `wifiServer.stop()` + `graph.camera = null` → this is where "not connected" comes from.

### 1.6 The actual error text on the UI (screenshot OCR)

```
✓/✗ 指示燈…            開啟隱私指示燈…  ✗ 指示燈設定失敗 Broken pipe   ← the service is already dead (Broken pipe)
手機端手勢（戒指連手機）：OFF → ON → OFF → ON
收到鍵碼 KEYCODE_MEDIA_PLAY_PAUSE (85)
已對應 KEYCODE_MEDIA_PLAY_PAUSE (85) → 錄影開始／停止
✗ 掃描不到名稱含「RGR」的裝置（共 7 台）
```

---

## 2. Why "pair ring" must fail for AIVELA (two independent reasons)

1. **It is already paired and connected → it no longer advertises.** Three scans (15:54:29, 15:55:55, 15:56:51) saw 7–8 devices respectively,
   and **AIVELA did not appear in any of them**. An already-paired BLE peripheral connects only to its original host in a directed way and no longer advertises.
2. **The button only matches names containing `"RGR"`.** `SettingsFragment.btnRingPair → RingBonder.scanAndBond(ctx, "RGR", 15_000)`
   → `RingScanner` takes those whose "advertised name contains RGR and which are connectable"; AIVELA's name is `AIVELA_RP13EJ37`, which does not match.
   The results of all three scans were **`name=-`** (those are unnamed Apple/Microsoft beacons), so even "find by name" cannot succeed.

> Incidental check: the phone BT stack had one `Permission denial: Need ACCESS_FINE_LOCATION permission to get scan results`,
> but that instance belonged to **`jp.naver.line.android` (PID 27733)**, not this App (this App already holds `BLUETOOTH_SCAN`/`BLUETOOTH_CONNECT`,
> and `ACCESS_*_LOCATION` is not requested on API 33 because the manifest sets `maxSdkVersion="32"`). **Unrelated to this issue**, so it is not listed as a cause.

---

## 3. Immediate remediation (**measured to work**)

```bash
ADB="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe"
# 1) Bring the glasses-side App to the foreground (key: only a foreground start allows FGS)
"$ADB" -s <DEVICE_SERIAL> shell 'am start -n com.rokid.rc.glasses/.MainActivity'
# 2) Reconnect on the phone side
"$ADB" -s <PHONE_SERIAL> shell 'am broadcast -n com.rokid.rc.phone/.DebugReceiver \
    -a com.rokid.rc.phone.DEBUG --es cmd CONNECT'
```

Verification results:

```
# Glasses
ServiceRecord{... com.rokid.rc.glasses/.CaptureService}  isForeground=true     ← fixed
CaptureService: WiFi media server ready (:8555)
CaptureService: camera ready
# Phone
DebugReceiver: CONNECT <DEVICE_ALIAS> result=Success(kotlin.Unit)
DebugReceiver: CAPTURE_PHOTO result=true
# Glasses album
IMG_1789286455006.jpg  2026-09-13 16:00      ← new file
```

**Everyday operational advice**: on the glasses side, **do not use `am start-foreground-service` to pull the service up from the background** (it will be rejected by the bg restriction and afterwards will inevitably be stopped by app idle); instead use **`am start` to bring MainActivity to the foreground** and let the App call `startForeground()` itself.

---

## 4. Root-cause recommendations (code changes, not yet implemented)

| Priority | Problem | Suggested change | File |
|------|------|----------|------|
| **P0** | Taking a photo while recording only returns `camera not ready`, misleadingly read as a "connection failure" | When `imageCapture == null` and `isRecording` is true → return a new error `busy_recording`; add the corresponding string to the phone's `RingText`, "錄影中，無法拍照" | `CameraController.kt:118`, `CommandRouter.kt:288`, `RingText.kt` |
| **P0** | Recording can hang (never gets a keyframe) and can never return to photo mode | Add a watchdog after `startVideo`: if `VideoRecordEvent.Start` is not received within N seconds (5 s suggested), then `stop()` + `bindPhoto()` and report an error | `CameraController.kt` |
| **P0** | 5 consecutive photo captures blow up the App (LMK) | Add a `Mutex` + minimum interval (800 ms suggested) to `capturePhoto`, allowing only one at a time; the glasses side returns `busy` | `CommandRouter.kt`, `RingControlManager.kt` |
| **P1** | After the service is rejected by the bg restriction it is bound to be stopped by app idle | Change the startup flow to "first ensure there is a foreground Activity, then start the service"; when `CaptureService` cannot `startForeground()`, have a fallback (e.g. notify the user / automatically `am start` MainActivity) | `CaptureService.kt`, `RootDaemonKeeper`, `ring_keepalive.sh` |
| **P1** | AIVELA's `KEY_PLAYPAUSE`/`MEDIA_NEXT` are bound to **video toggle**, so a swipe mis-triggers recording → the camera hangs | By default remove `VIDEO_TOGGLE` from the media keys (change to `NONE`), or require a long press to toggle recording | `RingController.actionFor` / prefs `map_85`, `map_87` |
| **P1** | "Pair ring" hard-codes `RGR` and scans for already-paired devices | Change to a **list of paired HID devices** (`BluetoothAdapter.bondedDevices` filtered to HID/keyboard-mouse) + menu selection; use scanning only for new unpaired rings | `SettingsFragment`, `RingBonder` |
| **P2** | The UI shows "not connected" without knowing the service is dead | Add `serviceForeground` / `cameraMode` (`photo`/`video`/`none`) fields to the glasses `STATUS`, so the phone can directly show "glasses service not started" | `Models.kt`, `CommandRouter`, `RingText` |

---

## 5. Confidence

| Statement | Label | Confidence | Basis |
|------|------|------|------|
| AIVELA is paired + HID-connected, and gestures can drive photo capture | FACT | **Very High** | `mInputDevices : 2`, `dumpsys input` device 24, 4 files + `RingController` log |
| "Pair ring" must fail because the name does not match (and a paired device does not advertise) | FACT | **Very High** | three scans with no AIVELA; `scanAndBond("RGR")`; the UI string "掃描不到名稱含「RGR」的裝置（共 7 台）" |
| The App was killed by LMK | FACT | **Very High** | `lowmemorykiller: Kill 'com.rokid.rc.glasses'` |
| After the rebuild the service did not obtain foreground → it was finally stopped by app idle | FACT | **Very High** | `startForeground() not allowed due to bg restriction`, `Stopping service due to app idle` |
| Taking a photo during recording necessarily returns `camera not ready` | FACT | **Very High** | `bindVideo()` sets `imageCapture = null`; `takePhoto()` line 118 |
| This recording produced no file | FACT | **High** | the newest video in the album is 09-12; `Recorder` kept waiting for a keyframe; encoder 15:58:10 RELEASED |
| Immediate remediation can restore it | FACT | **Very High** | `isForeground=true`, `camera ready`, `CONNECT Success`, `CAPTURE_PHOTO result=true`, new file |
| The phone BT stack's location-permission denial is unrelated to this issue | FACT | **High** | that line belongs to `jp.naver.line.android` (PID 27733) |

## 6. Not performed

No Bluetooth pairing was changed, no file was deleted, and no `force-stop` was executed (only `am start` was used to recover).
The only new file is `IMG_1789286455006.jpg`, used for verification (16:00, from the `CAPTURE_PHOTO` test).
