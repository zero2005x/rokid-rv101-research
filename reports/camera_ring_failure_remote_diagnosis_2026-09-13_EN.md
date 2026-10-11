# "Cannot Take Photos/Record Video, Glasses Touchpad Unresponsive, Mouse Pointer Flickering On Screen" — Remote Diagnosis and Fixes

> 中文版：[camera_ring_failure_remote_diagnosis_2026-09-13.md](camera_ring_failure_remote_diagnosis_2026-09-13.md)

- **Date**: 2026-09-13 18:39–18:45
- **Situation**: The glasses are **not connected to adb** (per the user's description, after unplugging), so remote diagnosis is possible only through the **phone app's Bluetooth channel**
- **Devices**: glasses `<DEVICE_SERIAL>` (Android 12) · phone `<PHONE_SERIAL>`

---

## 0. Three Symptoms, Three Different Causes

| Symptom | Measured cause | Related to root? |
|------|----------|---------------|
| **Cannot take photos/record video** | `CAPTURE_PHOTO failed: camera_not_ready`; `cameraBusy=false` → **the glasses-side `CaptureService` has been stopped by the system**, and `onDestroy` set `graph.camera = null` (the same root cause as `reports/aivela_ring_connection_failure_2026-09-13.md` §1.5) | ❌ **Unrelated** (the camera does not need root) |
| **Glasses touchpad does not respond when pressed** | `ringEnabled=false`, `ringRunning=false` —— **glasses-side gesture control is currently disabled**; and `ringDevice=/dev/input/event4` (RGR06) is **not** the glasses touchpad `event1` | ❌ Unrelated |
| **Mouse pointer flickering on screen** | The RGR06 ring is attached to the glasses as a **HID mouse/touchpad** (`event2` Keyboard / `event3` Consumer Control / `event4` touch surface) and keeps reporting coordinates → Android draws a pointer | ❌ Unrelated |
| (Incidental) "currently in root state" | Measured `rootAcquired=false`, `rootMode=MANUAL` → **there is actually no root right now** | — |

**Measured output obtained remotely** (the newly added phone-side `DUMP_STATUS`):

```
DebugReceiver: STATUS cameraBusy=false rootMode=MANUAL rootAcquired=false
               silent=true/true ringEnabled=false ringRunning=false
               ringDevice=/dev/input/event4 ringLast=null/null
DebugReceiver: CAPTURE_PHOTO failed: camera_not_ready
DebugReceiver: DEVICE_INFO={"selinux":"Permissive","uid":10053,
               "rootAcquired":false,"ghostlockEnabled":false,"rootMode":"MANUAL", ...}
```

---

## 1. Why "The Touchpad Does Not Respond When Pressed" —— A **Real Bug**

The original logic of `RingControlManager.start()` was:

```kotlin
val dev = devicePath?.takeIf { it.isNotBlank() } ?: reader.autoDetect(namePattern)?.first
```

Once `devicePath` is written into `ring_ctrl.device`, it is **reused forever and the path is never checked for existence**.
That is exactly what happened this time: at 2026-09-13 17:14, to test RGR06, it was set to `/dev/input/event4`;
afterwards, when the user wanted to press **the glasses' own touchpad**, the program still read only event4 ——
and there was **no error message at all** (`start()` returned true and the status showed "running"). Completely silent.

**Fixed**:
```kotlin
val storedOk = stored != null && reader.deviceExists(stored)   // added: verify against the device list
if (stored != null && !storedOk) { Log.w(...); devicePath = null }   // if invalid, fall back to auto-detection
val dev = (if (storedOk) stored else null) ?: reader.autoDetect(namePattern)?.first
```

## 2. Why "Cannot Take Photos" —— The Service Was Stopped by the System, Not a Root Problem

- `CaptureService.onDestroy()` executes `graph.camera = null`, after which `CommandRouter.capturePhoto`
  always returns `camera not ready`.
- The reason the service was stopped had already been documented earlier: `ActivityManager: Stopping service due to app idle`
  (because Android 12's background start restrictions prevent it from calling `startForeground()`, see P15).
- **`MainActivity.onCreate()` calls `startForegroundService(CaptureService)`** ——
  so **opening our app on the glasses brings the camera back**.

**Improved**: the phone side changed `camera_not_ready` from "camera not ready" into an actionable instruction,
**"Camera not ready (please open the app on the glasses)"**, synchronized across 14 locales (`phone_keys.tsv` verified with 203 keys).
The user will no longer see only an error they cannot act on.

## 3. Flickering Mouse Pointer

RGR06 is a **HOGP mouse-class** peripheral (Appearance `0x03C2` Mouse); after pairing, the glasses' HID host registers
three input devices; as long as it reports coordinates, Android displays a pointer (a flickering cursor usually means it is reporting continuously/intermittently).
**This is unrelated to the photo-capture function**, but if you do not want to see it: unpair RGR06, or move the ring away / power it off.

---

## 4. Disposition

### 4.1 What You Can Do Right Now (No adb Required)

1. **Open our app on the glasses** → `CaptureService` returns to the foreground and the camera is rebound → photo/video capture recovers.
2. In the phone app, turn on "Settings → Ring control → **Glasses-side gestures**".
   (Note: `ringDevice` is still `/dev/input/event4`, which requires a cable to change; but even if it points at the ring,
   once enabled the ring's own gestures can also trigger photo capture.)
3. To get rid of the flickering mouse pointer: unpair `RGR06` in the phone's "Bluetooth settings".

### 4.2 Once USB Is Plugged Back In, I Can Do It All in One Pass

```bash
ADB="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe"
S=<DEVICE_SERIAL>
"$ADB" -s $S install -r dist/app-glasses-v1.0-safe.apk          # install the build with the fallback fix
"$ADB" -s $S shell 'am start -n com.rokid.rc.glasses/.MainActivity'   # bring the camera back to ready
# switch glasses-side gestures back to "the glasses' own touchpad"
"$ADB" -s $S shell 'am broadcast -n com.rokid.rc.glasses/.DebugReceiver \
    -a com.rokid.rc.glasses.DEBUG --es cmd RING_STOP'
"$ADB" -s $S shell 'am broadcast -n com.rokid.rc.glasses/.DebugReceiver \
    -a com.rokid.rc.glasses.DEBUG --es cmd RING_START --es name TP --es device /dev/input/event1'
"$ADB" -s $S shell 'am broadcast -n com.rokid.rc.glasses/.DebugReceiver \
    -a com.rokid.rc.glasses.DEBUG --es cmd ARM_POLICY_STATUS'   # confirm the security policy state
```
(Or simply turn on the phone app's "Glasses-side gestures" switch and select the device in the glasses-side UI.)

---

## 5. Changes Delivered Along With This Session

| Change | File |
|------|------|
| **Ring device path invalid → automatic fallback detection** (fixes a real bug of silent failure) | `RingControlManager.kt`, `RingEvdevReader.kt` (added `deviceExists()`) |
| `camera_not_ready` → actionable message, 14 locales | `RingText.kt`, `phone_keys.tsv`, `out/*.xml`, 14 × `strings.xml` |
| Phone debugging capability: failure reason + `DUMP_STATUS` (all status fields) | phone `DebugReceiver.kt` |
| Two glasses APK builds + `dist/README.md` | `dist/` |

## 6. Not Verified / Limitations

- **The glasses are currently not on adb**, so the above fixes **have not yet been installed on the glasses** (the phone side is installed and the message has been verified).
- Who turned `ringEnabled=false` off was not investigated (possibly the phone-side switch or an earlier `SET_RING`);
  this session only confirmed "it is currently off".
- The cause of the mouse pointer was inferred from the HID device list (**High**); no actual photo of the glasses screen was taken.

---

## 7. Addendum (19:40–19:45, Disposition and Results After Both Devices Were Reconnected to adb)

### 7.1 Finding the Mechanism Behind "The Camera Keeps Disappearing": **Screen Asleep → Activity Cannot Reach the Foreground → FGS Denied**

```
$ dumpsys power | grep mWakefulness      → mWakefulness=Asleep
$ dumpsys window | grep mCurrentFocus    → Window{… MockWindow}       ← not our Activity
$ logcat | grep 'startForeground'
  W ActivityManager: Background start not allowed: service Intent
      { cmp=com.rokid.rc.glasses/.CaptureService } … startFg?=true
```

`RingControlManager.ensureCaptureService()` attempts `startForegroundService`; but **when the screen is asleep the
Activity cannot reach the foreground**, so Android 12 rejects it as a "background start", the service cannot start → `graph.camera` stays null forever
→ every photo request returns `camera not ready`.

**Disposition (measured effective)**: `input keyevent KEYCODE_WAKEUP` → `am force-stop` → `am start MainActivity`
(**force-stop is mandatory**: while the Activity is still alive, `am start` only resumes it, `onCreate` does not rerun,
and therefore `startForegroundService` is not called again).

### 7.2 Fixing a Defect in the Fallback Logic I Added Yesterday

`RingEvdevReader.deviceExists()` judges via `listDevices()`, and `listDevices()` goes through the root daemon
(`getevent -pl`); **without root it always returns an empty list** → this was misjudged as "the device does not exist" →
and it **cleared** the user-configured `/dev/input/event1` (measured: this really happened at 19:40).
Fixed: when the list is empty it returns `true` (if it cannot be confirmed, leave it alone).

### 7.3 Measured Results After Completion

| Item | Result |
|------|------|
| `CaptureService` | `isForeground=true`, `camera ready` ✅ |
| **Photo** | `CAPTURE_PHOTO ok IMG_1789299767856.jpg` ✅ |
| **Video** | `START_VIDEO ok` → `STOP_VIDEO ok VID_1789299754052.mp4` ✅ |
| Ring control settings | `pattern=TP` (glasses touchpad), `enabled=true`, the invalid `device` path removed ✅ |
| Security policy | `safeArming=on up=3611s charging=true arms=0/2 suspicious=0 suspended=false` ✅ |
| **Glasses touchpad gestures** | `RING_STATUS running=false device=null last=no_device/TP` —— **root missing**, `getevent` cannot run ⚠ |
| RGR06 | paired but **currently not connected** (`mInputDevices` empty) → the mouse pointer does not appear at the moment |

### 7.4 Conclusion

- **"Cannot take photos/record video" is fixed** (both the camera and the service are back, and both a photo and a video succeeded in measurement).
- **"Glasses touchpad does not respond when pressed" is only missing root**: path B reads evdev directly via root, so it cannot work without root.
  Currently `root_mode=MANUAL` and the circuit breaker has not tripped → root must be acquired **manually**
  (the phone app's "Acquire root" button, or `deploy/root_bridge.sh`).
- **Mouse pointer**: it does not appear while RGR06 is disconnected; to eliminate it for good, unpair it.

---

## 8. Addendum (19:55–20:00): Acquiring Root and Completing the Full Recovery

The user explicitly requested acquiring root. **The outer `adb reboot` retry loop of `deploy/get_root_now.sh` was deliberately avoided**
(it reboots up to 6 times on failure), and a **single controlled run** was used instead (`build-cache/one_shot_root.sh`):

1. `SERIAL=<DEVICE_SERIAL> bash deploy/root_bridge.sh` → establish the bridge + resident helper (pid 4137)
2. Write `deploy/trigger_root_daemon.cmd` into `ghostlock_bridge/last.cmd`
3. Every 10 seconds, watch `last.progress` / `last.out` / `/proc/net/unix` / **whether uptime goes backwards**

### Result

```
[13] up=4219s prog=done out=– daemon=1     ← root acquired in about 130 seconds
[29] device offline (possibly reboot)      ← actually a transient USB disconnect
```

**No reboot**: uptime increased continuously from 4056s to 4546s, and `ro.boot.bootreason` was unchanged.
(The boot history separately shows two reboots at 18:14:09 and 18:44:55, which happened **before this session's disposition**
and correspond exactly to the crash times the user reported; they also explain why, when taking over at 19:40, the app was not running and root was gone.)

### State After Completion (All Measured)

| Item | Result |
|------|------|
| **root** | `uid=0(root) … context=u:r:kernel:s0`; `getenforce=Permissive`; `root_daemon`(21868) + `root_daemon_keepalive.sh`(21879) resident ✅ |
| **Photo** | `CAPTURE_PHOTO ok IMG_1789300777925.jpg` ✅ |
| **Video** | `START_VIDEO ok` → `STOP_VIDEO ok VID_1789300796405.mp4` ✅ |
| **Glasses touchpad gestures** | `RING_STATUS enabled=true running=true device=/dev/input/event1 last=running//dev/input/event1` ✅ |
| `CaptureService` | `isForeground=true` ✅ |
| Security policy | not tripped (this was **manual** privilege escalation, not automatic arming, so the counter is not triggered) |

### Three Small Pitfalls Encountered (Recorded)

1. **The `RING_START` pattern gets overwritten by the phone-side `SET_RING`**: `CommandRouter.setRing` uses
   `RingIdentity.orDefault(null) = "RGR"` when `pattern == null`, so the moment the phone toggles the "glasses-side gestures" switch,
   the glasses' `pattern` reverts to `RGR`. → To pin the source, use **`RING_START --es device /dev/input/event1`**
   (explicitly specify the device rather than relying on the pattern).
2. **If the Activity is still alive, `am start` only resumes it**, `onCreate` does not rerun → `startForegroundService` is not called again;
   and when the screen is asleep it cannot even reach the foreground. The fix is `KEYCODE_WAKEUP` + **`am force-stop`** + `am start`.
3. **`DUMP_STATUS` can time out**: when the glasses are busy taking a photo (about 2–26 seconds each), a STATUS request returns `request timeout`;
   during diagnosis, avoid doing it while a capture is in progress.

---

## 9. Addendum (20:30): **Correction** —— The Touchpad Is Actually Fine, Plus Gesture Evidence Captured Before the Crash

### 9.1 Correcting My Earlier (20:2x) Wrong Judgment

I had previously tested with `( toybox timeout 90 getevent -lt /dev/input/event1 ) | head -60`, got "0 events",
and therefore inferred that the touchpad produces no events. **That inference was wrong** —— it was **a problem with the test method itself**:
the output went through a pipe + `timeout` terminated the process with SIGTERM, so **the contents of the stdio buffer were lost entirely**.

After switching to **writing to a file** (`getevent -lt … >> /data/local/tmp/tp_test.log`), events appeared immediately.
**The touchpad (`/dev/input/event1`, `ROKID,PSOC-TP-R`) is completely fine.**

> Lesson: when capturing `getevent` on Android, always **redirect to a file**, never through a pipe; otherwise you will misjudge it as "the device has no events".

### 9.2 The Keys the User Actually Pressed (Raw Capture)

```
[ 1102.396] EV_KEY KEY_ENTER      DOWN / UP      ← light tap
[ 1102.912] EV_KEY KEY_DASHBOARD  DOWN / UP      ← ★ opens Rokid Assistant
[ 1103.488] EV_KEY KEY_ENTER      DOWN / UP
[ 1106.645] EV_KEY KEY_DASHBOARD  DOWN / UP
[ 1107.165] EV_KEY KEY_PROG1      DOWN / UP      ← swipe
[ 1110.195] EV_KEY KEY_DASHBOARD  DOWN / UP
[ 1110.392] EV_KEY KEY_DASHBOARD  DOWN / UP
[ 1110.684] EV_KEY KEY_BACK       DOWN / UP
```

| Key sent by the touchpad | System behavior | How our glasses-side program handles it |
|----------------|----------|----------------------|
| `KEY_ENTER` | confirm/select | ✅ inside `RingEvdevReader.TAP_LIKE` → gesture `TAP` → **photo** |
| `KEY_DASHBOARD` | **opens Rokid Assistant** | ❌ not in `TAP_LIKE` → ignored (correct) |
| `KEY_PROG1` | swipe | ❌ ignored |
| `KEY_BACK` | back | ❌ ignored |

→ So when "opening the assistant with the touchpad", your **light tap simultaneously makes us take a photo** (`KEY_ENTER`); `KEY_DASHBOARD` is what opens the assistant.

### 9.3 Why `ring_events.log` Is 0 bytes (Not Because There Were No Events)

That file was recreated by `RingEvdevReader.start()` with `rm -f` at 19:58, and at the 20:11 hard reset (PS_HOLD)
**the page-cache contents were cut off before they hit disk**, so the file stayed at 0 bytes / mtime 19:58.
**It cannot be used to prove "there were no gestures"** —— I was wrong about that too earlier.

### 9.4 Cause of the Crash: "Delayed LPE panic" Is Still the Most Likely, but **Decisive Evidence Has Not Yet Been Obtained**

In support:
- `gl_run.log`: the exploit succeeded at 19:54 (`child is root!` → `exploit complete`).
- Reboot at 20:11:55: `ro.boot.bootreason=reboot`, and dmesg shows the previous shutdown was
  **PS_HOLD (MSM Controlled Shutdown, a software clean reset)**, with `/sys/fs/pstore` **empty**.
- Today's four reboots (17:49:23 / 18:14:09 / 18:44:55 / 20:11:55) **have completely consistent characteristics**,
  and the first three had nothing to do with the assistant.
- The project's own documentation: failure = kernel panic, and "success may also leave UAF damage that only blows up later when stepped on";
  starting the assistant creates new threads/locks/futexes, exactly the actions that would touch a corrupted rt_mutex structure
  → the assistant is more likely a **trigger point** than the cause.

Alternative hypotheses that **still cannot be fully ruled out** (currently without evidence):
- The assistant (`com.rokid.os.sprite.assistserver`) contending with our `CaptureService` for the **camera/microphone**;
  and the light tap simultaneously making us take a photo, which may create a timing conflict.
- Deciding this requires `/data/system/dropbox` (`SYSTEM_TOMBSTONE` / `system_server_wtf` / `SYSTEM_LAST_KMSG`)
  or `/data/misc/bootstat/bootstat.log`; **both require root**, and root is gone after the reboot.

### 9.5 Two Available Forensic Paths

1. **Acquire root again** (single controlled run, about 130 seconds) → read dropbox/bootstat/tombstones.
   ⚠ Each privilege escalation still carries a ~50% panic risk.
2. **Live reproduction**: with the glasses worn, I capture `logcat` simultaneously on both devices, and you operate the touchpad to open the assistant once more;
   if it really crashes, the log from the instant before the crash will be captured (this is the most direct approach).

---

## 10. Addendum (20:31–20:36): **Live Reproduction Experiment —— No Crash Reproduced**

Per the user's choice, "live reproduction" forensics were used: the glasses' **kernel log (`dmesg -w`)** and Android logcat
were streamed to **files on the PC** (`build-cache/repro_dmesg.log` / `repro_logcat.log` / `repro_phone.log`),
so that even if the glasses hard-reset, what was received before the crash stays on the PC.

### 10.1 Result: **No Crash**

- The glasses' uptime kept increasing from 1434s, with **no reboot**.
- `repro_dmesg.log` contains **no** panic/oops/RCU stall/hung task at all (all 7 keyword hits are boot-time
  cmdline/ramoops/watchdog initialization strings).
- The user did operate the touchpad during this period: the kernel log continuously shows
  `Rokid,PSOC-TP-R 1-0008: psoc_ts_interrupt` / `psoc_report_in work`.

→ **"Opening the assistant" by itself does not reliably cause a crash**; the 20:11 event was not a necessary causal relationship.

### 10.2 Two Meaningful Phenomena Captured

**(a) The assistant does notice that our camera is occupied**
```
09-13 20:31:56.578  2017  2348 W AssistServer_Log:
    CameraController -> CameraManager Camera Un Available: 0
```
While our `CaptureService` holds the camera open, the assistant's `CameraController` receives "Camera Un Available".
**It has not caused an error or crash so far**, but the two really do become aware of each other.

**(b) The camera workqueue is frequently delayed**
```
CAM_WARN: CAM-UTIL: cam_common_util_thread_switch_delay_detect: 158
    CRM workq schedule delay detected … diff 10: threshold 5
```
This appeared **49 times** during the whole capture period (mostly `CAM-SYNC`/`CRM` workq delays). It is warning-level and not fatal,
but it shows that the camera path really is delayed under load.

### 10.3 Discovered at the Same Time: **The Glasses Touchpad Path Is Currently Stopped (Root Missing)**

```
20:31:52 RingEvdevReader: start(/dev/input/event1) …
20:31:52 RingEvdevReader: start(/dev/input/event1) getevent 執行中=false   ← start failed
20:32:05 CommandRouter: recv: CAPTURE_PHOTO id=120                        ← this one was sent by the phone
```

`getevent` requires root, and root was gone after the 20:11 reboot → `running=false`.
So the `IMG_1789302725966.jpg` / `VID_1789302732678.mp4` produced during that period were **triggered by the phone**,
**not** by the touchpad.

### 10.4 Overall Assessment (Updated)

| Hypothesis | Current evidence |
|------|----------|
| **Delayed LPE panic** (primary cause) | The 20:11 reboot has the same characteristics as the first three today (PS_HOLD clean reset, empty pstore), and the first three had nothing to do with the assistant; yet the reproduction experiment did not crash → **probabilistic and correlated with the time of privilege escalation** |
| Assistant vs. camera conflict | The assistant does see `Camera Un Available`, but during reproduction it **did not** cause a crash → currently it can only count as a "secondary trigger factor" |
| Touchpad / our program | During reproduction the touchpad path never started at all (no root) → it can be **ruled out** as the cause of the 20:11 event |

→ To obtain decisive evidence (a panic trace), root is still required to **read `/data/system/dropbox`**,
or **a live stream must happen to be running when the crash occurs** (it is already set up, so the next real crash can be captured).

---

## 11. Addendum (20:38–20:45): The AR Screen "Gray Background" Problem

The user reported "no gray background, I want a transparent background". Verified item by item:

### 11.1 First, Ruled Out: Our Panel Itself Draws No Background Color

- The `panel` / `collapsedBar` / `expandedPanel` in `activity_main.xml` **have no `android:background`**
  (the only backgrounds are the two 8dp status dots `bg_dot_gray` / `bg_dot_green`).
- `MainActivity` only calls `setBackgroundResource()` on `dotConn`.
- **`#1F1F1F` (the actual color of the gray background in the user's screenshot, sampled with PIL as `(31,31,31)`) does not exist anywhere in the app**:
  the full color set is only `#00000000 / #000000 / #FFFFFF / #E8EDF5 / #F2F4F8 / #9AA5B8 / #8A94A6 / #5A6478 / #FFC24B / #4FC3F7 / #3DDC84 / #0B1220`.
- On-device `screencap` sampling: the panel area is **pure black `(0,0,0)`**, and only text pixels are non-black → **device rendering has no gray background**.

### 11.2 But a **Real Contradiction** Was Found (Fixed)

| Location | Original | Problem |
|------|------|------|
| `activity_main.xml` comment | "**Fully transparent (no background color, no panel background)** —— text floats directly on the line of sight without obscuring the view" | The design intent is transparency |
| `themes.xml` | `android:windowBackground = @android:color/black` (**opaque**) | The entire 480×640 window is covered by opaque black, **directly contradicting** the intent above |

**Fixed** (`themes.xml`):
```xml
<item name="android:windowBackground">@android:color/transparent</item>
<item name="android:colorBackground">@android:color/transparent</item>
<item name="android:windowIsTranslucent">true</item>
<item name="android:backgroundDimEnabled">false</item>
```
**Verification** (`dumpsys window windows`):
```
mAttrs={(0,0)(fillxfill) … fmt=TRANSPARENT …}        ← was OPAQUE before the fix
snapshot=TaskSnapshot{ … mIsTranslucent=true … }     ← ✅ transparency is in effect
```

### 11.3 Also Reducing Notification Interference

Android requires a foreground service to have a notification, but on AR glasses any pop-up card blocks the view. The original channel `capture`
was `IMPORTANCE_LOW`; it was changed to a new channel **`capture_quiet` = `IMPORTANCE_MIN` + silent**
(a channel's importance cannot be lowered by the program once created, so the id must be changed).
Measurement shows the ROM pulls the FGS notification's `mOriginalImp=1` back to `mImportance=2`, but it is already `groupKey=silent` and
**does not pop up**.

### 11.4 Still to Be Confirmed: That "**Rounded-Corner** Gray Bar" in the User's Screenshot

The gray bar in the screenshot has **rounded corners** and carries both our text "Rokid Capture" and "點按收起".
But our layout has **neither a background color nor rounded corners**, and no gray background can be measured in device rendering →
that rounded gray card is very likely **a border the system draws for our window** or **the mirroring tool's own UI**,
not our layout.

→ The only possibility attributable to us, "the window is opaque", has been fixed and reinstalled; please ask the user to take another screenshot to confirm:
if the gray background is still there, it is not drawn by this app and the investigation needs to move to the system / mirroring tool.

---

## 12. Addendum (20:45–21:00): The **True Root Cause** of the Gray Background Was Found —— Android's **Focus Highlight**

### 12.1 Root Cause

The `panel` in `activity_main.xml` has an `OnClickListener` set → it is **clickable**.
The glasses' primary input is a **touchpad (D-pad class) rather than a touchscreen**, so this clickable View **takes focus**,
and Android overlays a **default focus highlight** on it (semi-transparent white, **rounded corners**).

→ This is the "gray background" the user saw: **rounded corners, light gray, fitting the panel exactly**.

### 12.2 Three Decisive Experiments (None of Them Guesses)

| Experiment | Method | Result |
|------|------|------|
| **A. Change the window background color to pure green** | `windowBackground=#FF00FF00` | The gray area measures **`(31,255,31)`** = green with about **12% white** overlaid → the gray background is **a layer drawn on top of our window**, not our background color |
| **B. Change the panel's paddingBottom from 8dp to 44dp** | The panel gets taller | **The gray background moves to y=544..640** (previously 592..640) → the gray background's extent **exactly equals the `panel`'s bounds** |
| **C. Switch to the launcher (no focused View)** | HOME | The same area is **pure black `(0,0,0)`** → only our focusable panel has this layer |

(B rules out the "system-fixed 48px bar" hypothesis —— which is how I misjudged it at first.)

### 12.3 Fix

```xml
<LinearLayout
    android:id="@+id/panel"
    ...
    android:defaultFocusHighlightEnabled="false"   <!-- ← add this line -->
```
Verification (`screencap` sampling):
```
before fix y=560..636 → (31,31,31)   ← gray band
after fix  y=560..630 → (0,0,0)      ← only text pixels remain, pure black
```

### 12.4 Things Also Reverted and Clarified

- **Reverted `windowIsTranslucent=true`**: measurement shows it makes **the launcher icons/text behind show all the way through**, overlapping
  with the dashboard (the "all mixed together" the user reported). **The correct implementation of "transparent" on AR glasses is opaque black** —— black emits no light,
  so the wearer sees the real world while the app behind is blocked out. `windowBackground=@android:color/black` has been restored.
- **Full-screen immersive mode has no effect on this gray layer** (it was removed from the program, leaving only a comment explaining it).
- The foreground service notification remains `capture_quiet` (`IMPORTANCE_MIN` + silent).

---

## 13. Addendum (20:33–21:13): Results of the 40-Minute Live Stream —— **No Reboot During the Period**

`build-cache/repro_capture.sh` (glasses kernel log + two logcat streams to the PC) ran for a full 2400 seconds.

```
Glasses uptime          3504s (continuous, uninterrupted during the period)
Latest boot history entry  reboot,1789301515  ← still the 20:11:55 one; nothing new since
panic/oops/RCU stall hits in dmesg 3 → all are boot-time cmdline/ramoops strings, not real events
```

### 13.1 Meaningful Observation: **These 58 Minutes Without Root Were Completely Stable**

After the 20:11:55 reboot, `root_mode=MANUAL`, and root was no longer armed after the disconnect —— during the period:
- multiple touchpad operations, opening the assistant, photo/video capture, and several rebuild-reinstalls by us
- **there was no reboot at all**

Compared with today's four reboots (17:49 / 18:14 / 18:44 / 20:11), all of which happened during periods **with root armed** →
the "reboot ↔ privilege escalation" correlation is one notch stronger (but still not decisive proof, since it could also be chance).

### 13.2 Limitations of This Streaming Run (To Be Changed Next Time)

The `adb shell dmesg -w` connection **dropped at 20:33** (the file stopped growing), and logcat only went to 20:46.
The cause is that the adb connection was interrupted by subsequent commands / the device being busy, and the script does not reconnect.
→ If we want to "wait for it to really panic and then capture the scene", the three streams need an **automatic reconnect** loop.
