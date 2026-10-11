# Ring / Camera P0+P1 Fixes — Implementation and On-Device Test Record

> 中文版：[ring_p0_p1_fixes_test_2026-09-13.md](ring_p0_p1_fixes_test_2026-09-13.md)

- **Date**: 2026-09-13 16:3x–16:4x
- **Basis**: the recommendation list in `reports/aivela_ring_connection_failure_2026-09-13.md`
- **Artifacts**: `android/app-glasses/build/outputs/apk/debug/app-glasses-debug.apk`,
  `android/app-phone/build/outputs/apk/debug/app-phone-debug.apk` (both installed and tested on device)
- **Devices**: glasses `<DEVICE_SERIAL>` (Android 12/API 32) · phone `<PHONE_SERIAL>` (Android 13/API 33, MIUI)

> **Note on the test method**: the ring's physical buttons cannot be produced by automation, so the photo/video paths were exercised over the **glasses' own
> TCP:8555 channel** by sending exactly the same JSON frames as the app (`{"t":"CAPTURE_PHOTO","id":N}` and so on).
> This channel shares the same `CommandRouter`/`CameraController` as Bluetooth, and it **can produce concurrency precisely**
> (WiFi and Bluetooth submitted at the same time), making it a stricter equivalent test than pressing the ring by hand.

---

## 1. What Was Fixed

| # | Requirement | Implementation | File |
|---|------|------|------|
| P0-1 | Taking a photo while recording must return a new error, not `camera not ready` | Added `CameraErrors.BUSY_RECORDING = "busy_recording"`; when `imageCapture == null`, `takePhoto()` returns `busy_recording` or `camera_not_ready` depending on `isRecording`; the phone's `RingText.errorDetail()` maps these to localized strings (unknown codes are shown as-is) | `shared/protocol/CameraErrors.kt` (new), `CameraController.kt`, phone `ui/RingText.kt` |
| P0-2 | A 5-second watchdog for `startVideo` | `startVideo` waits for `VideoRecordEvent.Start` with `withTimeoutOrNull(5s)`; on timeout → mark `videoAborted`, `recording.stop()`, force `bindPhoto()`, return `record_start_timeout`; `onVideoEvent` wakes it when Start arrives; on Finalize, if aborted, **delete the leftover file** and do not `addMedia` | `CameraController.kt` |
| P0-3 | Photo Mutex + 800 ms minimum interval | `photoLock: Mutex` (a failed `tryLock` returns `busy` immediately; **no queueing**, to avoid pile-up) + `MIN_PHOTO_INTERVAL_MS = 800`, with the interval topped up inside the lock | `CameraController.kt` |
| P1-1 | Media keys must not trigger `VIDEO_TOGGLE` by default | `MEDIA_KEYS_PASS_THROUGH` (PLAY_PAUSE/PLAY/PAUSE/STOP/NEXT/PREVIOUS/FF/REW/HEADSETHOOK) removed from `INTERESTING_KEYS` → passed through to the system; plus a **one-time migration** that removes existing custom "media key → VIDEO_TOGGLE" mappings | phone `ring/RingController.kt` |
| P1-2 | "配對戒指" becomes a list of already-bonded HID devices | Added `RingBonder.bondedHidDevices()` (triple test on class/UUID/name) + `displayName()`; `SettingsFragment` becomes a list dialog, storing the selection in `ring_ctrl.approved_mac/approved_name`; "掃描新裝置…" is a secondary option and **does not filter by name** | `shared/ring/RingBonder.kt`, phone `ui/SettingsFragment.kt` |

i18n: **10 new keys** added (3 error strings + 7 pairing UI strings), all 14 locales filled in
(`phone_keys.tsv` 193→203; each `out/*.xml` block kept in sync; `strings.xml` 284→**294** entries).
`validate_translations.py` ✅, `apply_phone_strings.py` ✅ (14/14).

---

## 2. On-Device Test Results

### T1 Build / Install
```
./gradlew :app-glasses:assembleDebug :app-phone:assembleDebug --offline   → BUILD SUCCESSFUL
adb -s <DEVICE_SERIAL> install -r app-glasses-debug.apk → Success
adb -s <PHONE_SERIAL> install -r app-phone-debug.apk   → Success
```

### T2 P0-3 Burst capture (5 requests submitted simultaneously, reproducing the original LMK scenario)
```
--- frame 1: {"t":"PHOTO_TAKEN","replyTo":1,"media":{…"fileName":"IMG_1789288908289.jpg","sizeBytes":3558694}}
--- frame 2: {"t":"PHOTO_TAKEN","replyTo":2,…"IMG_1789288910128.jpg","sizeBytes":3554351}
--- frame 3: {"t":"PHOTO_TAKEN","replyTo":3,…"IMG_1789288911208.jpg","sizeBytes":3550323}
--- frame 4: {"t":"PHOTO_TAKEN","replyTo":4,…"IMG_1789288912233.jpg","sizeBytes":3553062}
--- frame 5: {"t":"PHOTO_TAKEN","replyTo":5,…"IMG_1789288913209.jpg","sizeBytes":3550864}
```
- **All 5 succeeded**; the `createdAt` intervals were **1079 / 1021 / 974 / 958 ms** → the 800 ms minimum interval is in effect.
- **No LMK kill, no process restart, no error codes** (before the fix: 5 requests in a row → RSS 100 MB → killed by lowmemorykiller).
- ⚠ Note: when verifying "no LMK", you cannot use `logcat | grep -c "Kill .com.rokid.rc.glasses"` — that command line **itself** contains this string and gets counted (this project has hit the same class of self-match trap with `pkill -f` several times).

### T3 P0-1 Taking a photo while recording
```
=== START_VIDEO id=10 ===   -> {"t":"VIDEO_STARTED","replyTo":10}
=== CAPTURE_PHOTO id=11 === -> {"t":"ACK","replyTo":11,"ok":false,"error":"busy_recording"}
=== STOP_VIDEO id=12 ===    -> {"t":"VIDEO_STOPPED","replyTo":12,"media":{"fileName":"VID_1789288977676.mp4",
                               "sizeBytes":17608900,"width":1280,"height":720,"durationMs":8207,…}}
=== CAPTURE_PHOTO id=13 === -> {"t":"PHOTO_TAKEN","replyTo":13,…"IMG_1789288995662.jpg"}
```
- Taking a photo while recording returns **`busy_recording`** (it was `camera not ready` before the fix).
- **Incidental P0-2 validation**: `START_VIDEO` returned `VIDEO_STARTED` **immediately** → `VideoRecordEvent.Start` arrived within 5 seconds and the **watchdog did not misfire**.
- `STOP_VIDEO` produced a **valid file** (17.6 MB, 1280×720, 8.2 seconds) → the watchdog did not damage normal recording.
- Taking a photo after stopping returned `PHOTO_TAKEN` → **`bindPhoto()` did restore photo mode**.

### T4 P0-3 Concurrency protection (WiFi and Bluetooth submitted at the same time)
```
glasses WiFi channel  -> {"t":"ACK","replyTo":90,"ok":false,"error":"busy"}
phone Bluetooth broadcast   -> DebugReceiver: CAPTURE_PHOTO result=true
```
→ `Mutex.tryLock` works: overlapping requests are **rejected immediately** (`busy`) instead of piling up in a queue.

### T5 P1-1 Media-key migration
```
RingController: 媒體鍵遷移完成：移除 2 筆「媒體鍵→錄影切換」舊對應（媒體鍵已改為放行）
ring_ctrl prefs 前：map_4097=PHOTO, map_87=VIDEO_TOGGLE, map_25=PHOTO, map_85=VIDEO_TOGGLE
ring_ctrl prefs 後：map_4097=PHOTO, map_25=PHOTO, migrated_media_keys_v1=true
```
→ The video mappings for `MEDIA_NEXT(87)`/`MEDIA_PLAY_PAUSE(85)` have been removed; the custom mappings for mouse buttons and volume keys are retained.

### T6 Phone-side error strings (i18n)
```
TEST_RING_TEXT photo_fail busy_recording        -> 拍照失敗：錄影中，無法拍照
TEST_RING_TEXT photo_fail busy                  -> 拍照失敗：操作太頻繁，請稍候
TEST_RING_TEXT photo_fail record_start_timeout  -> 拍照失敗：錄影啟動逾時，已切回拍照
TEST_RING_TEXT photo_fail camera_not_ready      -> 拍照失敗：相機未就緒
TEST_RING_TEXT photo_fail weird_code            -> 拍照失敗：weird_code     ← unknown code shown as-is
```

### T7 P1-2 Device list (**completed, with screenshot**)
Actual screenshot of the dialog after tapping "配對戒指":

```
選擇戒指裝置
✓ AIVELA_RP13EJ37
BB:C3:13:73:8A:32
✓ Xiaomi Buds 5 Pro
64:8F:DB:86:13:3E
掃描新裝置…
                                          取消
```

- **AIVELA_RP13EJ37 appears in the list** (screenshot `build-cache/shots/picker.png`).
- **There is no longer any `RGR` name filtering**, and **no BLE scan is performed for already-bonded devices** (the list comes directly from `bondedDevices`).
- A further filter was added afterwards: devices in the `Major.AUDIO_VIDEO` class that have **no** HID UUID/name evidence are excluded.
  ⚠ But **Xiaomi Buds 5 Pro still appears** — because it **really is a HID device**
  (`mInputDevices` in `dumpsys bluetooth_manager` lists both `64:8F:DB:86:13:3E` and AIVELA),
  i.e. it has HID UUID evidence, so the filter **deliberately** does not exclude it.
- **Incidental finding**: the earbuds are also HID → per §2 R1 of `reports/ring_capture_security_audit_2026-09-13.md`,
  their media/volume keys **are likewise treated as a gesture source by `RingController`**. This is another instance of "no source binding";
  this list merely honestly lists the same set of devices, and the real fix remains source binding (that report's §3).

---

## 3. Not Yet Covered / Known Limitations

| Item | Status |
|------|------|
| ~~Screenshot validation of the P1-2 list dialog~~ | ✅ **completed** (T7, with screenshot) |
| End-to-end gestures from the ring's **physical buttons** (single tap/double tap/long press) | Not performed (physical HID events cannot be produced by automation); the code path was not changed, and T3–T5 already cover its downstream |
| Actually hanging the recording to trigger the 5-second watchdog | The hang could not be reproduced artificially; **no misfire** was verified (Start arrives normally) plus the code path of the timeout branch |
| `DebugReceiver` (exported with no permission) | Still to be fixed per the earlier audit (untouched here; see §7 P9) |
| The glasses-side `CaptureService` being stopped by app idle | Untouched here (see the P1 recommendation in §7 P15); during testing it was kept in the foreground with `am start` |

## 4. Confidence

| Statement | Label | Confidence | Basis |
|------|------|------|------|
| Taking a photo while recording returns `busy_recording` | FACT | **Very High** | the verbatim ACK from T3 |
| Video start/stop work normally and produce a valid file | FACT | **Very High** | T3 `VIDEO_STOPPED` + 17.6 MB / 1280×720 / 8207 ms |
| Burst capture is throttled by the 800 ms interval and is no longer killed by LMK | FACT | **High** | T2's five `createdAt` intervals of 958–1079 ms; no kill record |
| Concurrent requests are rejected as `busy` by the Mutex | FACT | **Very High** | the verbatim ACK from the WiFi side in T4 |
| Media keys no longer trigger recording by default | FACT | **Very High** | the T5 migration record + media keys removed from `INTERESTING_KEYS` |
| Phone-side error strings are localized correctly | FACT | **Very High** | T6's five cases (including an unknown code) |
| AIVELA appears in the pairing list | FACT | **Very High** | the T7 screenshot; the list comes from `bondedDevices` with no name filtering |
| Xiaomi Buds appears because it really is a HID device | FACT | **High** | `mInputDevices` lists both the Buds and AIVELA |
| The watchdog's timeout branch itself | Not tested on device | — | only non-misfire was verified; a timeout requires artificially hanging the Recorder |
