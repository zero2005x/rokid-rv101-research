# Ring-Operated Photo/Video Capture — Source Authorization and Hard-Coding Security Audit

> 中文版：[ring_capture_security_audit_2026-09-13.md](ring_capture_security_audit_2026-09-13.md)

- **Date**: 2026-09-13
- **Scope**: `android/shared/.../ring/` (5 files), `android/app-glasses/.../ring/` (2 files),
  `android/app-phone/.../ring/` (2 files), `deploy/ring_input_daemon.c`,
  both sides' `AndroidManifest.xml`, `shared/protocol/Protocol.kt`, `net/WifiServer.kt`
- **Questions**: (1) Does ring-operated photo/video capture require a **Bluetooth pairing confirmation mechanism**? (2) Are there **security concerns caused by hard-coding**?
- **Method**: per-file static reading + host/on-device measurement (`dumpsys package`, `dumpsys input`,
  `dumpsys bluetooth_manager`, `getevent -pl`, `ip addr`, `netstat`, reading app prefs as root,
  and an actual broadcast delivery test). **No** destructive operation was performed, and no photo/video was added.

> **Conclusion first**: **it is required**, and at present **neither path has any source verification at all**.
> "Who pressed it" simply does not exist in the code —— the phone side sees only a keycode, and the glasses side selects a device purely by a **device-name substring**;
> on top of that, each side has an **exported debug broadcast receiver with no permission**, which turns "force pairing", "force capture"
> and "acquire root" into operations any installed app can trigger. There is also a **WiFi password hard-coded in the APK**
> combined with a **zero-authentication TCP control channel**.

---

## 1. Data Flow and Where the "Source" Is Lost

### Path A (phone-side interception, no root required)

```
Ring (BLE HID) ──┐
Keyboard/mouse ──┼─→ Android InputDispatcher ─→ MainActivity.dispatchKeyEvent /
Synthetic input ─┘                               dispatchTouchEvent / MediaSession
                                                        │  ⚠ only keyCode / button is passed
                                                        ▼
                                            RingController.onKeyDown(code)
                                            RingController.onMouseButton(action, button)
                                                        │
                                            RingGestureDetector → RingAction
                                                        ▼
                                            Remote.capturePhoto() / startVideo() …
```

| Site | File:line | Does it look at the "source device"? |
|------|-------|---------------------|
| `dispatchKeyEvent` | `MainActivity.kt:120-126` | ❌ reads only `event.keyCode` / `event.action` |
| `dispatchTouchEvent` (mouse-style ring) | `MainActivity.kt:134-141` | ⚠ only checks whether `event.source` is of **MOUSE / TOUCHPAD class** (not "which device") |
| `dispatchGenericMotionEvent` (wheel) | `MainActivity.kt:145-154` | ❌ reads only `axisValue` |
| `RingMediaSession.onMediaButtonEvent` | `RingMediaSession.kt:37-51` | ❌ reads only `e.keyCode` (the **background** path) |
| `RingController.onKeyDown` | `RingController.kt:160-177` | ❌ the signature is only `code: Int` |
| `RingController.onMouseButton` | `RingController.kt:199-225` | ❌ the signature is only `(action, button)` |

**`KeyEvent.getDeviceId()` / `MotionEvent.getDeviceId()` / `InputDevice` are never read.**
The only classification criteria are "does the key fall in `INTERESTING_KEYS`" and the custom keycode table in prefs
(`RingController.kt:144-158`).

### Path B (glasses-side root direct evdev read)

```
root_daemon(root) ──setsid──> getevent -lt <device> >> /data/local/tmp/ring_events.log
     ▲                                                             │
     │ exec("tail -c +offset …") every 400ms                       │
App(untrusted) ─ RingEvdevReader ─ RingControlManager ─ CameraController.takePhoto()
```

| Site | File:line | Does it look at the "source device"? |
|------|-------|---------------------|
| `autoDetect(pattern)` | `RingEvdevReader.kt:48-52` | ❌ **name substring** match (`RGR`, degraded to `RING`), **takes the first hit** |
| `RingControlManager.start()` | `RingControlManager.kt:84-108` | ❌ as long as `ring_ctrl.device` has a value it **skips detection entirely** and reuses it |
| Event parsing | `RingControlManager.kt:237-258` | ❌ parses only `EV_KEY` / `EV_REL` tags; no device field |

---

## 2. Findings (by Severity)

### R1 — Path A accepts **any** HID source (High)

Nothing anywhere compares the event source, so all of the following can trigger photo/video/silent/display toggling:

- Bluetooth keyboards, Bluetooth mice, USB keyboards (those that send `ENTER` / media keys / `F1`–`F12` / mouse buttons);
- The phone's built-in **`uinput-fpc` (fingerprint sensor uinput keyboard)** and `mt6877-mt6359 USB_3_5 Jack`;
- **A ring from another vendor** —— on-device `dumpsys input` shows **`AIVELA_RP13EJ37` (device 22)** currently attached
  to the phone as an input device (`/proc`-level keyboard + mouse + touchpad), and its events are likewise consumed by
  `RingController`;
- **Synthetic/injected input**: this project's own report has already recorded `input keyevent 131`,
  `input mouse tap` → `TAP → 已拍照` (`reports/ring_control_report.md` §5.2).
  Synthetic events involve no physical ring at all, proving that "is it the ring?" **cannot be distinguished** in the program.

> On-device corroboration (this session): phone `ring_ctrl` prefs = `enabled=true`, `gesture_count=171`,
> custom mapping `map_4097 → PHOTO` (mouse primary button → photo). That is, **while our app is in the foreground,
> a left click from any mouse takes a photo** (including clicking a UI button in quick succession).

### R2 — Path B uses "device name" as identity, and what it currently points at is **not the ring** (High)

- Device selection = `name.contains("RGR")` (case-insensitive), taking the first; `RING` is the degraded option.
- **Current on-device state**: glasses `ring_ctrl` prefs = `pattern=RGR`, `device=/dev/input/event1`,
  `enabled=true`; yet the glasses have only two input devices ——
  `event0 = qpnp_pon` (power key), **`event1 = ROKID,PSOC-TP-R` (the glasses' own touchpad)**.
  The glasses' bonded Bluetooth list contains only one phone and one tablet, **no ring at all**.
- Because detection is skipped when `device` has a value, `pattern=RGR` is effectively a **dead setting**: what is actually driving
  photo/video capture on the glasses today is **the glasses' own touchpad**.

### R3 — "Auto-pair with anything whose name looks right", with no user confirmation (High)

`RingBonder.scanAndBond()` (`RingBonder.kt:107-135`): scan → take the **first**
connectable device whose advertising name contains `RGR` → **immediately `createBond()`**.

- The BLE advertising name is **self-declared** by the peripheral; it is not identity. An attacker with a BLE device
  advertising `RGR06` can be paired automatically; after pairing they can inject HID events → trigger photo/video capture (R1).
- Call sites: `SettingsFragment.kt:204` (UI button), `DebugReceiver.kt:145,164-175` (phone),
  `DebugReceiver.kt:52-54` (glasses). Among them `WATCH_RING` defaults to `autoBond=true`,
  which keeps scanning for dozens of rounds and pairs automatically with the first connectable device.
- The program has **no** step of "show candidate devices (name / MAC / RSSI) → user presses confirm";
  the only gate is the OS SMP flow (a Just Works peripheral may not show any dialog).
- Note: `RGR06` is a **random address** (`D1:03:CB:ED:C7:97`). After pairing, Android reports the
  identity address, so **the post-pairing MAC can serve as a stable identifier**; but **pre-pairing** advertising addresses rotate
  and cannot be used as identity.

### R4 — Each Side Has an **Exported, Permissionless** Debug Receiver (High)

`AndroidManifest.xml` (phone `:43-49`, glasses `:67-73`):

```xml
<receiver android:name=".DebugReceiver" android:exported="true">
    <intent-filter><action android:name="com.rokid.rc.phone.DEBUG" /></intent-filter>
</receiver>
```

- **There is no `android:permission`** → **any installed app** can send this broadcast.
- Confirmed on device (this session): the receiver resolver block of `dumpsys package` has only `Action:`,
  with no `Permission:`; and delivery actually succeeded (sent from a foreign uid → `DebugReceiver` executed).
- Triggerable commands (`DebugReceiver.kt`):

| Side | Command | Effect |
|----|------|------|
| Phone | **`CAPTURE_PHOTO` / `START_VIDEO` / `STOP_VIDEO`** | Remote photo/video capture |
| Phone | **`ACQUIRE_ROOT`** | Triggers LPE privilege escalation |
| Phone | **`WIFI_ADB`**, `WIFI_CONNECT --es host <arbitrary IP>` | Enable wireless adb / connect to an arbitrary host |
| Phone | **`PAIR_RING` / `UNPAIR_RING` / `WATCH_RING` (autoBond)** | **Force-pair the attacker's ring** |
| Phone | `SET_LED`, `SET_GHOSTLOCK`, `SET_SILENT`, `SET_DISPLAY`, `DELETE_MEDIA <id>`, `SET_RING` | Change device state / delete media |
| Glasses | **`RING_START` / `RING_STOP` / `RING_DIAG`**, `PAIR_RING` | Control / rebind the ring source |
| Glasses | `SET_DISPLAY`, `TOGGLE_PANEL`, `SET_LOCALE` | Change display / interface |

- In addition, `<service android:name=".CaptureService" android:exported="true" />` (glasses `:53-56`)
  **also has no permission** → any app can bring up the camera foreground service; if `rootMode=AUTO`, that path **also runs privilege escalation along the way**
  (`CaptureService.kt:81-89`).
- The phone-side `onReceive` **does not check `intent.action`** (it only checks for the presence of a `cmd` extra) → not even the action has to be right.

### R5 — Media/Control Channel Has Zero Authentication + the AP Password Is Hard-Coded in the APK (High)

- `WifiServer.kt:30`: `ServerSocket(8555)` binds **all interfaces** and `accept()`s any source
  (it only prints `remoteSocketAddress`), handing off to `CommandRouter` —— **no token / handshake verification**
  (`HELLO` at `CommandRouter.kt:32` only replies `HELLO_ACK`). The same channel can `CAPTURE_PHOTO`,
  `DOWNLOAD_MEDIA`, `DELETE_MEDIA`, `ACQUIRE_ROOT`, `ENABLE_WIFI_ADB`.
- `Protocol.kt:14-15`: `AP_SSID = "RokidCam"`, **`AP_PASS = "rokid1234"`** (a fixed WPA2 password,
  distributed with the APK; `WifiHotspot.kt:24-25,52` uses it directly to start the SoftAP, and `status()` even returns it to the phone).
- Bluetooth RFCOMM side, `BluetoothServer.kt:37-42`: after `accept()` it **only records** `remoteDevice.address`,
  with **no allowlist** → any bonded device can issue commands.
- **Current on-device state (avoiding exaggeration)**: the glasses' `netstat` shows `[::]:8555 LISTEN`, but currently
  `wlan0 = NO-CARRIER/DOWN` and there is no `wlan2` (SoftAP not started) → **externally unreachable at this moment**.
  Once the SoftAP is started (after root) or the glasses join an ordinary WiFi (the documentation records a connection to the **open network `iTaiwan`**),
  any host on the same segment can connect with the public password / zero authentication and operate the camera.

### R6 — `"RGR"`, the "single basis of identity", Is Hard-Coded in 11 Places (Medium)

| Location | File:line |
|------|---------|
| shared | `RingBonder.kt:109`, `RingWatcher.kt:31`, `RingEvdevReader.kt:48,51` |
| Phone | `DebugReceiver.kt:145,162`, `SettingsFragment.kt:204` |
| Glasses | `CommandRouter.kt:92`, `RingControlManager.kt:85`, `DebugReceiver.kt:53,102` |
| C (not adopted) | `ring_input_daemon.c:13` |

It is simultaneously a **security parameter** (the sole basis for device selection) and a **scattered literal**: changing it in any one place makes
"the device that was paired" and "the device actually being monitored" inconsistent, and the failure mode is **silent** (failing to pick a device only writes a log).
`scripts/audit_hardcode.py` currently does **not** flag it as a duplicate literal.

### R7 — UNIX Socket with `chmod(..., 0777)` (Medium, implementation not adopted)

`deploy/ring_input_daemon.c:189`: `chmod(sock_path, 0777)` → any uid can connect and **receive gesture events**
(equivalent to side-channel recording of when the user pressed the ring). That file's header already marks it "**fallback implementation (not adopted)**",
but if it is enabled later it will introduce this permission directly.

### R8 — The Audit Tool Itself Did Not Cover This Class of Problem (**Fixed**, and Fixing It Immediately Caught Two More)

`scripts/audit_hardcode.py` checks: Chinese literals, Kotlin hard-coded paths/IPs/MACs/ports, layout XML,
duplicate literals, script serial numbers / Windows paths. It did **not** check:

1. Components with `android:exported="true"` and no `android:permission`;
2. Credential-type literals (`pass` / `password` / `token` / `secret` / `key` assignment constants);
3. `chmod` permissions on the native side.

→ This is exactly why **R4/R5, "the most severe class", were previously recorded only as
"developer tool messages" in `reports/hardcode_audit.md`**.

**Sections 6/7/8 were added this time**, and immediately after being added they caught **R10** and **R11** (see below).

### R9 — The Build Script of the Unadopted Native Fallback Still Points at an Unusable Toolchain (Low)

`deploy/build_ring_input_daemon.sh:5,9-12` hard-codes a Windows NDK path and
`/home/kali/rokid_root`. On this machine WSL has `/mnt/c` read-only and the NDK is the Windows build → **this script cannot possibly succeed**;
the project has switched to `deploy/build_probe_zig.sh` (zig cross-compilation), and this file was not updated in step.

### R10 — **The root daemon socket is 0777 and SELinux is Permissive → any app obtains root directly** (Critical)

Caught by the newly added section 8 rule (`deploy/root_daemon.c:132`), confirmed on device:

```
$ adb shell 'getenforce; ls -ld /data/local/tmp; ls -laZ /data/local/tmp/root_daemon.sock'
Permissive
drwxrwx--x 11 shell shell 20480 2026-09-13 15:44 /data/local/tmp          ← other has x (traversable)
srwxrwxrwx  1 root  root  u:object_r:shell_data_file:s0  0 … root_daemon.sock  ← world rwx
```

- `/data/local/tmp` is `0771`: other **can traverse** (`x`), which is enough to `connect()` to a known path.
- The socket is **`0777`**: any uid can read and write.
- `getenforce` = **Permissive** (set after this project's privilege escalation) → **SELinux will not block it**.

→ On this already-escalated glasses device, **any installed app can `nc -U /data/local/tmp/root_daemon.sock`
and obtain a uid 0 shell**, with no GhostLock and no permission of any kind.
This is an interface **this project introduced itself** (not Rokid's), and it is also the **trust foundation** of ring path B
(all of `RingEvdevReader`'s `exec` calls go through it).

> ⚠ This is more severe than every other item in this report, but it is **outside the original ring scope** (it was caught incidentally by the ring audit's tooling rules).
> The fix requires a design decision (see §4 P0'): the minimal change is `0600` plus moving the app side to binder or a
> SELinux-allowed channel; if untrusted_app must be allowed to connect, what is needed is a **per-launch token** or a dedicated SELinux type,
> not 0777. **Nothing will be touched without confirmation**.

### R11 — Hard-Coded OTA API Credentials Written into scripts (High)

`scripts/rokid_ota_check.py:25-28`:

```python
KEY             = "E217806228724620BC1A6924DD542B5"
SECRET          = "<REDACTED_OTA_SECRET>"
DEVICE_TYPE_ID  = "<REDACTED_OTA_DEVICETYPEID>"
DEVICE_ID       = "<DEVICE_SERIAL>"
```

- Purpose: shared credentials for generating the `sign` (MD5) for `https://ota.rokid.com/v1/extended/ota/check`.
- Origin: the comment itself admits "from `adb shell getprop`" —— on device, two of them were confirmed to come from **bootloader properties**
  (`ro.boot.secret`, `ro.boot.devicetypeid`), i.e. **provisioning values burned into the device**.
- Risk: if `KEY`/`SECRET` are **shared across the whole product batch**, this amounts to publishing the OTA API credentials in the repo;
  even if they are per-unit values, they should not be hard-coded (they should be read from `getprop` at runtime, or supplied via environment variables / a local config file).
- Disposition: change to runtime reading plus allowing an environment-variable override; and note the value's origin inside the repo.
  **Nothing will be touched without confirmation** (it affects the reproducibility of an existing research script).


---

## 3. Answer: "Is a Bluetooth Pairing Confirmation Mechanism Needed?"

**Yes, and they are two different things; at present neither exists.**

| # | Requirement | Current state | Why it is necessary |
|---|------|------|-----------|
| **A. Source identity binding** | Accept events only from "the one approved ring" | ❌ The phone side does not look at the source at all; the glasses side looks only at a **name substring** | Both the name and the keycode are forgeable/shareable. Without binding, any HID device (someone else's ring, a keyboard, `input` injection) can take photos and record video |
| **B. User confirmation at pairing time** | Scan for candidates → **show name / MAC / RSSI** → user presses confirm → only then `createBond()`; and **persist that device as the only approved ring** | ❌ `scanAndBond()` takes the first similarly-named device and **pairs directly** | The BLE advertising name is chosen by the other party; without a confirmation flow, anyone within a few dozen meters can get themselves paired |

**Recommended minimal viable design** (not yet implemented; awaiting confirmation):

1. **Persisted allowlist**: add `ring_ctrl.approved_mac` (+ `approved_name`),
   written by the user at pairing time; when unset, **accept no gestures at all** (fail-closed).
2. **Path A binding**: `MainActivity` passes `event.deviceId`; `RingController` uses
   `InputDevice.getDevice(deviceId)` to obtain `name` (and where necessary also compares against
   `BluetoothAdapter.bondedDevices`' identity address) → if it does not match the allowlist, **let it through without intercepting**.
   (Note: Android 12's `InputDevice.getDescriptor()` is a hashed value and **does not contain the MAC**,
   so what must be compared is `getName()` plus the bonded list, not the descriptor.)
3. **Path B binding**: change `autoDetect()` to "must match an approved device name"; and in `start()`
   perform the same comparison on `device` (currently the check is skipped when `device` has a value). If the ring is HID,
   it can be further reinforced with a unique serial number via `EVIOCGUNIQ` / sysfs (not verified on this device generation; listed as to-be-verified).
4. **Pairing confirmation UI**: split `scanAndBond` into `scan()` and `bond(device)`,
   inserting a candidate-list dialog in between (name / MAC / RSSI / whether HID 0x1812); remove
   `WATCH_RING`'s `autoBond=true` default.
5. **Feedback for every gesture**: photo/video capture are actions with "physical side effects", so in addition to the existing logcat it is recommended
   to give a one-shot indication on the glasses display (currently `RingOutcome` can already carry it; this is a product decision).

---

## 4. Recommended Fix List

### 4.1 Applied This Time (the "zero-risk cleanup" scope; verified)

| Item | Corresponds to | Change | Verification |
|------|------|------|------|
| ✅ `"RGR"` consolidated into a single constant | R6 | Added `android/shared/.../ring/RingIdentity.kt` (`DEFAULT_NAME_PATTERN` / `FALLBACK_NAME_PATTERN` / `TOUCHPAD_NAME_PATTERN` / `orDefault()`), rewrote 11 references (shared 3, phone 3, glasses 5), added 3 imports | `./gradlew :app-glasses:compileDebugKotlin :app-phone:compileDebugKotlin --offline` → **BUILD SUCCESSFUL**; `grep '"RGR"'` matches only the constants file |
| ✅ `chmod(0777)` → `0600` | R7 | `deploy/ring_input_daemon.c` (with an explanation: 0600 makes it unreachable for untrusted_app, so actually adopting it requires an **access-control design** rather than putting 0777 back) | zig cross-compilation passes (aarch64 ELF) |
| ✅ Audit tool gained 3 rules | R8 | `scripts/audit_hardcode.py` gained section 6 (exported with no permission; including noise reduction for launcher / protected broadcasts), section 7 (credential-type literals), section 8 (world-accessible `chmod`) | Running it immediately caught R4/R5/R10/R11; the `lastKey` false positive and `BootReceiver` system-broadcast noise were also fixed |

> These three items **change no behavior** (the `RGR` value is identical, the C file was never adopted, and the audit script is read-only).
> Also, **no APK has been rebuilt/installed on the device yet** —— the above only went as far as Kotlin compiling successfully.

### 4.2 Awaiting Confirmation (Would Change Behavior or UX; **Not Yet Touched**)

| Priority | Item | Corresponds to | Change | Blast radius |
|------|------|------|------|--------|
| **P0′** | **Tighten the root daemon socket** (⚠ more severe than every other item in this report) | R10 | Current state `0777` + SELinux Permissive ⇒ any app can obtain a root shell. The minimal change is `0600`, but then the app side (untrusted_app) cannot connect → it needs to become a **binder / token / dedicated SELinux type** design | Touches **all** glasses-side root functionality (privilege escalation, ring, silent, LED); a full regression is required |
| **P0** | Add permission to the debug receiver | R4 | Add `android:permission="android.permission.DUMP"` to both manifests | It has been confirmed on **both devices that `shell` holds `DUMP`** → `adb shell am broadcast` automation **will not break**, but third-party apps are blocked (already proven not to be blocked today by `reports/ring_reachability_test_2026-09-13.txt`) |
| **P0** | Tighten `CaptureService` | R4 | Change to `exported="false"` | Same-app starts are unaffected; `checkComponentPermission` always allows **uid 0/system**, so **the root daemon's (uid 0) `am start-foreground-service` still works**; but **starts driven by `adb shell` (uid 2000) will be rejected** → one regression run is needed (`deploy/test_shizuku_e2e.sh:72` is affected) |
| **P1** | Ring source binding (A) | R1,R2 | See §3 steps 1–3 | Requires changing 3 files + adding prefs |
| **P1** | Pairing confirmation UI (B) | R3 | See §3 step 4 | Requires UI changes |
| **P1** | Read OTA credentials at runtime | R11 | Change `scripts/rokid_ota_check.py` to read `getprop` / environment variables instead of hard-coding | Affects that script's reproducibility (documentation must be updated too) |
| **P1** | Randomize the AP password per device | R5 | Generate a random `AP_PASS` on first launch, store it in prefs, and tell the phone over the paired channel | Requires changes to `Protocol` / `WifiHotspot` / the phone-side AP join flow |
| **P3** | Add a token to the media channel | R5 | `HELLO` carries a one-time token (exchanged over the Bluetooth pairing channel) | A larger change |
| **P3** | Switch the fallback build script to zig | R9 | Change `build_ring_input_daemon.sh` to call `build_probe_zig.sh` | An unadopted path |

---

## 5. Measurements Actually Performed This Time (Reproducible)

```
ADB="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe"

# 1) exported receivers (both sides): no Permission: in the resolver block
"$ADB" -s <PHONE_SERIAL> shell 'dumpsys package com.rokid.rc.phone'   | grep -A3 'com.rokid.rc.phone.DEBUG'
"$ADB" -s <DEVICE_SERIAL> shell 'dumpsys package com.rokid.rc.glasses' | grep -A3 'com.rokid.rc.glasses.DEBUG'

# 2) the broadcast really is deliverable from a foreign uid (side-effect-free command: string comparison only)
"$ADB" -s <PHONE_SERIAL> shell 'logcat -c; am broadcast -n com.rokid.rc.phone/.DebugReceiver \
    -a com.rokid.rc.phone.DEBUG --es cmd TEST_RING_TEXT; sleep 3; logcat -d -s DebugReceiver:*'
#   → DebugReceiver: onReceive cmd=TEST_RING_TEXT connected=true … (15 lines of comparison output)

# 3) the phone's current input devices (AIVELA ring online; fingerprint uinput; USB jack)
"$ADB" -s <PHONE_SERIAL> shell 'dumpsys input' | grep -E '^ *[0-9]+: |Descriptor' | head -20
#   → 22: AIVELA_RP13EJ37 / 1: uinput-fpc / 5: mt6877-mt6359 USB_3_5 Jack …

# 4) the glasses' input devices (only the power key and their own touchpad → no ring)
"$ADB" -s <DEVICE_SERIAL> shell 'for f in /dev/input/event*; do echo -n "$f: "; getevent -i $f | grep -m1 name:; done'
#   → event0 qpnp_pon / event1 ROKID,PSOC-TP-R

# 5) bonded devices
"$ADB" -s <PHONE_SERIAL> shell 'dumpsys bluetooth_manager | grep -A8 "Bonded devices"'
#   → has AIVELA_RP13EJ37 / <DEVICE_ALIAS> …; no RGR06
"$ADB" -s <DEVICE_SERIAL> shell 'dumpsys bluetooth_manager | grep -A4 "Bonded devices"'
#   → only Redmi Note 11 Pro+ 5G / Redmi Pad SE

# 6) ring prefs (glasses require root)
printf 'cat /data/data/com.rokid.rc.glasses/shared_prefs/ring_ctrl.xml\n' | nc -U /data/local/tmp/root_daemon.sock
#   → pattern=RGR / device=/dev/input/event1 / enabled=true
"$ADB" -s <PHONE_SERIAL> shell 'su -c "cat /data/data/com.rokid.rc.phone/shared_prefs/ring_ctrl.xml"'
#   → map_4097=PHOTO / gesture_count=171 / enabled=true

# 7) media channel
"$ADB" -s <DEVICE_SERIAL> shell 'netstat -tlnp | grep 8555; ip addr | grep -E "^[0-9]+: |inet "'
#   → tcp6 [::]:8555 LISTEN; wlan0 NO-CARRIER/DOWN, no wlan2 (externally unreachable at this moment)

# 8) whether shell holds DUMP (determines whether the P0 fix preserves adb automation)
"$ADB" -s <each> shell 'dumpsys package com.android.shell | grep "android.permission.DUMP"'
#   → granted=true (both devices)
```

## 6. Confidence and Unverified Items

| Statement | Label | Confidence | Basis |
|------|------|------|------|
| Neither path verifies the source device | FACT | **Very High** | Line-by-line reading; `RingController`'s signatures are only `(code)` / `(action,button)`, with no `deviceId` |
| Any HID source can trigger actions | FACT | **Very High** | No source filtering + the project's existing on-device measurements (`input keyevent` / `input mouse tap` → photo) |
| The AIVELA ring is currently online on the phone as an input device | FACT | **Very High** | `dumpsys input` device 22 |
| The glasses are currently monitoring their own touchpad rather than a ring | FACT | **Very High** | prefs `device=/dev/input/event1` + `getevent` name `ROKID,PSOC-TP-R` + no ring in the bonded list |
| Both DebugReceivers are exported and permissionless | FACT | **Very High** | manifest + no `Permission:` in the `dumpsys package` resolver + actual delivery succeeded |
| "Any third-party app" can deliver (not just shell) | ✅ **FACT (measured)** | **Very High** | **End-to-end proof**: a self-made minimal app `com.ringaudit.reachtest` (**uid 10346** / glasses **uid 10064**, neither being shell 2000) delivered, and `DebugReceiver` / `GlassesDebugReceiver` actually executed it; raw output in `reports/ring_reachability_test_2026-09-13.txt` |
| `scanAndBond` auto-pairs any device with a matching name | FACT | **Very High** | `RingBonder.kt:107-135`; `WATCH_RING` defaults to `autoBond=true` |
| The SoftAP password is hard-coded | FACT | **Very High** | `Protocol.kt:15` |
| :8555 has zero authentication | FACT | **Very High** | `WifiServer.kt:30-45`, `CommandRouter.kt:32`; LISTEN measured on device |
| :8555 is currently **unreachable** externally | FACT | **High** | No WiFi interface is up; still situation-dependent (starting the AP / joining a network changes it) |
| Whether TEE/SELinux separately blocks third-party apps' broadcasts | ✅ **Ruled out** | **Very High** | The glasses' SELinux is Permissive and the phone-side test succeeded; neither device has an additional block |

**Not performed (deliberately)**: no broadcast with side effects was sent, such as `CAPTURE_PHOTO` / `START_VIDEO` / `ACQUIRE_ROOT` / `PAIR_RING` /
`WIFI_ADB`; no command was sent to `:8555`; no Bluetooth pairing was touched;
and no actual connection to `root_daemon.sock` was made from a non-root uid (R10 is a static determination from permission bits + `getenforce`).

**On-device tests that were performed**: the minimal test app `com.ringaudit.reachtest`
(uid 10346/10064, **not shell**) → both sides' `DebugReceiver` **actually executed** the commands;
raw output in `reports/ring_reachability_test_2026-09-13.txt`; the test app has been removed from both devices.

---

## 7. Relationship to Existing Documents

- `reports/ring_control_report.md` describes the **functional** implementation and on-device measurement (photo/video/gesture learning all working normally),
  and does not conflict with this report; what this report adds is the **authorization** dimension.
- `reports/hardcode_audit.md` classified `DebugReceiver`'s action strings as "developer tooling" without assessing their
  **reachability** (exported/permission); R4/R8 in this report correct that blind spot.
- The authoritative facts have been folded into `reports/FACTS.md` §7 (P7–P10) and §9 (new open items); **if the two disagree, FACTS.md prevails**.
