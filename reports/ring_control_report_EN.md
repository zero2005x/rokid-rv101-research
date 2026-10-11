# Ring Control (Rokid Ring / Touch Ring) — Research Results and Implementation Report

> 中文版：[ring_control_report.md](ring_control_report.md)

- Date: 2026-09-12
- Goal: let the **ring** control the glasses' photo/video capture (preferably after acquiring root), with support on both the phone side and the glasses side
- Status: **Path A (phone-side interception) and Path B (glasses-side root direct evdev read) are both complete and validated on device**

## 1. Ring Identification (Research Results)

| Device | Address | Evidence | Purpose |
|------|------|------|------|
| **AIVELA_RP13EJ37** | `BB:C3:13:73:8A:32` | the phone's only GATT client connection (`com.smartring`); a `/proc`‑level input device with **keyboard + mouse + touchpad + wheel** (`REL_X/Y`, `REL_WHEEL/HWHEEL`, `ABS_X/Y 0–4095`, `BTN_TOUCH/BTN_MOUSE/BTN_RIGHT/BTN_MIDDLE`, `KEY_VOLUMEUP/DOWN`, `KEY_PLAYPAUSE`, `KEY_HOMEPAGE/BACK/FORWARD`) | health measurement + gesture control (the app has an "enable gesture recognition / triple-tap the touchpad to wake" page) |
| **RGR06** (WVX "Rokid smart glasses ring mouse") | `D1:03:CB:ED:C7:97` (random address) | BLE advertising: `name=RGR06`, **service 0x1812 (HOGP)**, **Appearance 0x03C2 = Mouse**, `flags=0x04` (LE only), connectable, RSSI −35…−65 | the "official Rokid ring" the user wants to use |

Identification method (reusable): the phone app has a built-in **BLE scanner** (`RingScanner`) that outputs the raw advertising data
(name / RSSI / service UUID / manufacturer ID / raw hex), and a **GATT probe** (`RingGatt`) that
connects and enumerates services. For devices where the system settings page only shows a MAC, these two steps are the only way to confirm identity.

### 1.1 Known Limitation: RGR06 Currently Rejects New Hosts

- Symptom: advertising is normal (connectable, RSSI −35), but **every GATT connection returns `status 133`**, and `createBond()`
  goes `BOND_BONDING → BOND_NONE` within 1.2 seconds.
- Diagnosis: the user confirmed that this ring **was previously paired with another device (which is now gone or powered off)**. The common behavior of a BLE peripheral while holding an old bond
  is to advertise only in a directed way toward the old host / refuse new central connections → this is the typical cause of 133.
- Solution: **reset / re-pair** the ring according to its manual (commonly: long-press the touch area for 5–10 seconds until the LED blinks rapidly; or trigger it by power cycling).
  After the reset, the app's `WATCH_RING` watcher will **complete pairing automatically** (`scanAndBond`).
- Other attempts: the phone's system settings page also cannot pair it; glasses-side BLE scanning gets **0 results** in a third-party app
  (the phone sees it at the same distance), and adding the location permission did not help → this RV101 build has platform restrictions on third-party BLE scanning.
  Therefore **on the glasses side we switched to the two paths "pair on the phone + forward from the phone" or "ring connects directly to the glasses (if pairing succeeds at the system level)"**.

## 2. Gesture Mapping (Defaults)

| Gesture | Action | Notes |
|------|------|------|
| Single tap | **photo** | press and release < 400ms |
| Double tap | **start/stop recording** | interval between the two taps < 450ms |
| Long press | **toggle silent capture** | hold ≥ 700ms (path B also actually mounts/restores via SilentMode) |
| Wheel / swipe | **cycle the glasses display info** | cycle: all → connection+storage → camera+indicator → all hidden |
| Triple tap | unspecified (reserved) | on some rings used to wake |

The timing parameters are adjustable (the `RingGestureDetector` constructor; the glasses-side `getevent` version likewise decides on 400/450/700ms).

## 3. Path A: Phone-Side Interception (No Root Needed)

- After the ring connects to the phone as BLE HID, Android delivers its key/mouse events to the **foreground window**.
- `MainActivity.dispatchKeyEvent` / `dispatchGenericMotionEvent` (**dispatch layer**, not `onKeyDown`)
  intercept them and hand them to `RingController`; `RingGestureDetector` recognizes the gesture → `perform()` executes the action
  (`Remote.capturePhoto()` / `startVideo()` / `stopVideo()` / `setSilent()` / `setDisplayConfig()`).
- UI: the **"Ring Control" card on the "Settings" tab** (since 2026-09-13; the remote-control tab keeps only capture-related items) — enable switch, gesture description, status (cumulative count + latest result),
  **gesture learning** (press a ring key → display `KEYCODE_xxx (code)` → **choose the action to bind from a menu**).
- **Learnable keycode mappings**: a learned keycode can be bound to photo / recording / silent / show next set / show previous set / no action,
  stored in the `ring_ctrl` prefs (`map_<code>`), and **custom mappings take priority over gesture defaults**; mouse buttons are represented as
  `0x1000 + button` (to avoid colliding with KeyEvent numbers). Long-pressing the "gesture learning" button clears all custom mappings.
- Important design points:
  - Intercept at the dispatch layer: `onKeyDown` is only called when "no View consumes the event", and when the ring sends `ENTER` it gets
    eaten by the focused button on screen (measured).
  - **Clicks from mouse-style rings go through `dispatchTouchEvent`** (not `dispatchGenericMotionEvent` — the latter only carries
    hover/scroll): for a ring like RGR06 with Appearance=Mouse, its "tap" is an ACTION_DOWN/UP with `source=SOURCE_MOUSE`.
    Therefore `dispatchTouchEvent` additionally intercepts MOUSE/TOUCHPAD events based on `event.source`,
    and **finger touch (touchscreen) is not intercepted at all** (measured: `input mouse tap`
    → `TAP → 已拍照 …`; `input tap` (finger) → 0 RingController log entries).
  - **No up-front connection check**: `Remote.request()` internally calls `ensureConnected()` to reconnect automatically; blocking
    "glasses not connected" up front would make gestures fail directly after an idle disconnect (hit during measurement).

## 4. Path B: Glasses-Side Root Direct evdev Read (Works in the Background Too)

```
root_daemon(root) ──setsid──> getevent -lt /dev/input/eventN >> /data/local/tmp/ring_events.log
        ▲                                                              │
        │ exec("tail -c +offset …") every 400ms                       │
   App(untrusted) ── RingEvdevReader ── RingControlManager ── CameraController.takePhoto()
```

- Why `getevent` instead of a self-built native binary: the app (untrusted_app) cannot read dev/input,
  but the root daemon can; `getevent` is the device's built-in evdev reader, which avoids shipping an aarch64 binary
  with the APK and its build chain (the local WSL has no aarch64 linker, and the NDK is the Windows version). The fallback implementation is kept in
  `deploy/ring_input_daemon.c` (not adopted).
- Event latency ≈ the polling interval (400ms); the device is selected automatically by a `--name` substring: **the program default is `RGR`** (the ring),
  while the glasses touchpad is set to `TP` through the `ring_ctrl.pattern` prefs (the current local value); where the document says "default TP" it means the device prefs, not the program default.
- The triggered capture / recording / silent / display switching **all complete locally** (without going through the phone), so it also works when the glasses are worn alone.
- **Automatic startup**: after successful privilege escalation, `CaptureService` automatically starts ring control according to `ring_ctrl.enabled`;
  **it is also restored when the app process starts (`App.onCreate`)**, with `skipExisting` so reading begins at the end of the file,
  avoiding replay of old gestures from while it was disabled. The device-name pattern is stored in `ring_ctrl.pattern` (default `RGR`).
- **Watchdog**: every 30 seconds it checks `pgrep getevent` and the polling loop, and restarts automatically if they fail (Rokid aggressively reclaims background
  apps, and without a watchdog background control would silently stop working).
- **Background measurement**: after switching the glasses to the home screen (Launcher in the foreground, our app in the background), a single tap was injected
  → `TAP → 已拍照 IMG_1789151522171.jpg` ✅.
- **Known boundary**: if Rokid **kills the entire app process** while in the background, gestures during that period will not be consumed
  (getevent still records them on the root side, but the consumer is absent); the process recovers automatically the next time any component wakes it.
  Supporting even that case would require moving "gesture → trigger" to the root side as well (waking the app with `am broadcast`),
  which is an optional follow-up enhancement.

### 4.1 Two Pitfalls Hit During Implementation (Noted in Code Comments)

1. **`pkill -f` self-matching suicide**: `pkill -f 'getevent -lt'` matches the shell command line that runs it
   (the same line contains `getevent -lt …` further on), so pkill kills itself first → none of the subsequent starts run.
   Fix: **split stop and start into two execs**, and use the `[g]etevent` bracket trick + a PID file.
2. **Polling loop and tick**: `while (pollJob?.isActive == true)` executes before `launch` returns → the loop ends
   immediately; and `detector.tick()` was only called when "there was an event" → the **single tap that needs to wait out the double-tap window would never succeed**.
   Fix: use a `@Volatile running` flag, and call `tick()` on every round.
3. **Read offset out of bounds (log file recreated)**: `start()` does `rm` and recreates the log, but the old offset is kept in the prefs;
   if the offset > the file size, `tail -c +offset` never reads anything (all gestures unresponsive, with no error either).
   Fix: `pollNew()` compares the file size on every round and resets to zero and re-reads when out of bounds.
4. **Gesture detection must use "event time", not "parse time"**: if the same batch contains multiple gestures (for example a single catch-up read after the app is woken),
   using parse time compresses the timing to 0ms → a single tap is misjudged as a double tap.
   Fix: pass the `[  sec.usec]` timestamp from `getevent -lt` into `RingGestureDetector`;
   and add protection to clamp "future timestamps" to now+1s (otherwise `lastTapAt` is in the future and `tick()` never succeeds).
5. **Gesture rescue after the app process is reclaimed**: `start()` now "looks at the log file mtime first" — if there was activity within 10 seconds it **continues reading from the last offset**
   (so a gesture from the moment just before waking is not lost); only after a long idle does it jump to the end of the file;
   the offset is persisted in `ring_ctrl.reader_offset`. In addition, a **root-side keep-alive** was added
   (`/data/local/tmp/ring_keepalive.sh`, which every 15 seconds checks whether the app is alive and, if not, pulls it back with root's
   `am start-foreground-service`; after `force-stop` it cannot be woken, which is an Android limitation).

## 5. On-Device Validation

### 5.1 Path B (Glasses Side; Root Direct evdev Read)

Triggered by writing synthetic evdev events into `ring_events.log` (`getevent` itself was already reading the device for real under root):

| Gesture | Result (glasses-side log) |
|------|-------------------|
| Single tap | `TAP → 已拍照 IMG_1789148949298.jpg` (subsequent regression rounds also passed, e.g. `IMG_1789152626561.jpg`) |
| Double tap | `DOUBLE_TAP → 開始錄影` → `DOUBLE_TAP → 已存影片 VID_1789148960604.mp4` |
| Long press | `LONG_PRESS → 靜音關閉` → `LONG_PRESS → 靜音開啟` (both directions succeeded) |
| Wheel | `SCROLL_UP → 顯示切換為 連線+儲存` |
| Double tap within a batch (150ms) | `DOUBLE_TAP → 開始錄影` (validates event-timestamp-based detection) |
| Long press (>700ms) | `LONG_PRESS → 靜音關閉` / `→ 靜音開啟` |

Device enumeration: `RING_DEVICE /dev/input/event1 ROKID,PSOC-TP-R`, `/dev/input/event0 qpnp_pon`.

### 5.2 Path A (Phone Side)

Simulating ring keys with `input keyevent` (`F1=131`: HID keyboard class; `MEDIA_PLAY_PAUSE=85` was also tested):

| Gesture | Result (phone-side log) | UI state |
|------|-------------------|---------|
| Single tap | `TAP → 已拍照 IMG_1789149429394.jpg` | `累計 14 次 · 最近：TAP → 已拍照 IMG_…jpg` |
| Double tap | `DOUBLE_TAP → 開始錄影` → `DOUBLE_TAP → 已存影片 VID_1789149438818.mp4` | same as above |
| Gesture learning | press `F1` → dialog title `選擇這個鍵碼的動作 / KEYCODE_F1 (131)` | 6 actions selectable |
| Custom mapping | after binding `F1 → 錄影`, pressing F1: `自訂鍵碼 KEYCODE_F1 (131) → VIDEO_TOGGLE` → `開始錄影` → `已存影片 VID_1789150240748.mp4` | overrides the default (F1 originally = photo) |
| Clear mapping | after long-pressing "gesture learning", pressing F1: `手勢 TAP → 動作 PHOTO` → `已拍照 IMG_1789150279362.jpg` | reverts to default |
| **Mouse-source** single tap | `input mouse tap` → `TAP → 已拍照 IMG_1789153194857.jpg` | simulates a mouse ring such as RGR06 |
| **Mouse-source** double tap | two taps 150ms apart → `DOUBLE_TAP → 開始錄影` → `已存影片 VID_1789153234558.mp4` | — |
| Finger touch (safety) | `input tap` → **0** RingController log entries | will not trigger by mistake |

## 5.2.1 Phone-Side Background Limitations (Measured)

- Foreground: `dispatchKeyEvent` / `dispatchTouchEvent` can intercept all ring events (verified).
- Background: Android only dispatches **media keys** to the "media button session", whose priority is determined by PlaybackState.
  Measured locally (`dumpsys media_session`): `Media button session is yt.morphe.noname.exe/YouTube playerlib`
  — even with YouTube Music paused, it still takes the media keys and our `RokidRingControl` session does not receive them
  (logcat shows `MediaSessionService: Sending KeyEvent … to yt.morphe.noname.exe`).
- This project **deliberately does not contend** for them (pretending STATE_PLAYING would override the user's music controls); `RingMediaSession` is only a
  best effort "when there is no other media session".
- Therefore: **the reliable control solution while the phone is in the background is glasses-side path B** (root direct evdev read, independent of the phone's foreground state and media keys),
  which is exactly why this project implements both paths.
- One side effect was also fixed: enabling ring control originally intercepted **all** keys (BACK was eaten too); it now intercepts only
  the keys common in ring HID descriptors (ENTER/DPAD_CENTER/media keys/volume keys/home key/F1–F12) plus custom mappings,
  and lets the rest through (measured: BACK returns normally, 0 RingController log entries).

## 5.3 Impact on Other BLE Devices (Measured, Important)

- Observation: during long continuous `WATCH_RING` scanning (LOW_LATENCY, 6 seconds per round, dozens of rounds), the **AIVELA health ring's
  GATT/HID connection dropped** (`mInputDevices: BB:C3:…:0`, `com.smartring Connections: 0`);
  after scanning stopped, that device returned to a reconnectable state.
- Cause: the phone's single-antenna BLE contends for radio/scheduling with existing BLE connections during "continuous active scanning", degrading connection quality
  or even dropping the link (especially noticeable with Android's `ScanSettings.SCAN_MODE_LOW_LATENCY`).
- Therefore this project's design principle: **scan only briefly when pairing is needed** (the in-app "pair ring" = a single 15-second scan),
  `WATCH_RING` is for development/debugging only and its round count should be reduced; running ring control day to day **requires no scanning** (after pairing,
  HID events are delivered directly by the system, and the glasses side uses evdev), so it does not affect AIVELA synchronization.

## 5.4 Remote Switch for Glasses-Side Ring Control (Protocol)

- Added `SET_RING` (phone→glasses, `{enabled, pattern?}`): on the glasses side `CommandRouter.setRing()` sets
  `ringControl.enabled` and starts/stops it, returning `{ringEnabled, ringRunning, ringDevice}`.
- `STATUS` gains `ringEnabled / ringRunning / ringDevice / ringLast`, shown live in the phone's "Ring Control" card
  (**2026-09-13 update: `ringLast` has been changed to structured `ringLastKey` + `ringLastArg`**, see "Structured events" below)
  (e.g. `眼鏡端：執行中 · /dev/input/event1 · 已啟動（/dev/input/event1）`) and provides a switch.
- Measured: phone `SET_RING enabled=false` → glasses `RING_STATUS enabled=false running=false last=stopped/…` (the glasses send a code and the phone displays "已停止");
  `enabled=true` → `enabled=true running=true 已啟動` ✅

## 6. How to Operate (Debug Commands)

```bash
ADB=/mnt/c/.../platform-tools/adb.exe

# phone side (path A)
# the ADB below is provided by deploy/env.sh (source deploy/env.sh then use $ADB/$SERIAL)
$ADB -s <phone> shell am broadcast -n com.rokid.rc.phone/.DebugReceiver -a com.rokid.rc.phone.DEBUG --es cmd CONNECT
$ADB -s <phone> shell am broadcast -n com.rokid.rc.phone/.DebugReceiver -a com.rokid.rc.phone.DEBUG --es cmd SCAN_RING  --ei secs 15
$ADB -s <phone> shell am broadcast -n com.rokid.rc.phone/.DebugReceiver -a com.rokid.rc.phone.DEBUG --es cmd PAIR_RING  --es pattern RGR
$ADB -s <phone> shell am broadcast -n com.rokid.rc.phone/.DebugReceiver -a com.rokid.rc.phone.DEBUG --es cmd PROBE_RING --es mac <MAC>
$ADB -s <phone> shell am broadcast -n com.rokid.rc.phone/.DebugReceiver -a com.rokid.rc.phone.DEBUG --es cmd WATCH_RING --ei rounds 60 --es pattern RGR
$ADB -s <phone> shell am broadcast -n com.rokid.rc.phone/.DebugReceiver -a com.rokid.rc.phone.DEBUG --es cmd SET_RING --ez enabled false   # remotely disable glasses-side ring control

# glasses side (path B)
$ADB -s <glasses> shell am broadcast -n com.rokid.rc.glasses/.DebugReceiver -a com.rokid.rc.glasses.DEBUG --es cmd RING_DEVICES
$ADB -s <glasses> shell am broadcast -n com.rokid.rc.glasses/.DebugReceiver -a com.rokid.rc.glasses.DEBUG --es cmd RING_START --es name RGR   # for glasses touchpad testing use --es name TP
$ADB -s <glasses> shell am broadcast -n com.rokid.rc.glasses/.DebugReceiver -a com.rokid.rc.glasses.DEBUG --es cmd RING_STATUS
$ADB -s <glasses> shell am broadcast -n com.rokid.rc.glasses/.DebugReceiver -a com.rokid.rc.glasses.DEBUG --es cmd RING_STOP
```

> Glasses-side FGS limitation: while the display is asleep, CaptureService cannot be started from the background, so `RING_START` detects
> `graph.camera == null` and **starts CaptureService with root instead**.

## 7. To Do

1. **RGR06 pairing**: the user needs to reset the ring according to the manual (see §1.1); after the reset, `WATCH_RING` will pair automatically,
   and path A can then be operated with the real ring (path A has currently been validated with simulated keys).
2. AIVELA ring: its gesture module sleeps by default (per the app documentation: **triple-tap the touchpad to wake**); once woken, the same
   path A can intercept it; monitoring of `/dev/input/event7` is ready.
3. ~~Advanced mapping~~: **completed** (see the learnable keycode mapping in §5.2).

---

## Structured Events (2026-09-13)

The glasses side used to compose results into **Chinese sentences** (`已拍照 IMG_….jpg`, `顯示切換為 連線+指示燈`) and send them to the phone →
when the phone was set to English the locales were mixed, and the glasses side only had zh-TW resources so it could not localize. It now works as follows:

| Layer | Change |
|----|------|
| Glasses `RingControlManager` | actions return `RingOutcome(key, arg)` (such as `photo_ok` + filename); display configuration parameters become machine-readable flags `conn,led`; all Chinese result strings removed (logcat now English) |
| `shared` `Status` | `ringLast` → `ringLastKey` + `ringLastArg` |
| Phone | `ui/RingText.kt` maps the codes to phone-locale strings (reusing the existing `ring_*` resources + 5 new keys); `DebugReceiver` gains `TEST_RING_TEXT`, which lists the text of all codes in the current locale |

Measured (same batch of codes, switching the phone language): `photo_ok` → 「已拍照 IMG_1.jpg」/「Photo captured: IMG_1.jpg」;
`display_switched` (arg `conn,led`) → 「顯示切換為 連線+指示燈」/「Display switched to Connection+Indicator」.
