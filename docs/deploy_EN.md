# Build and Deployment Guide

> 中文版：[deploy.md](deploy.md)

## 0. Prerequisites

- Development machine: Android Studio (Windows/WSL both fine), JDK 17 (local path see §1).
- Rokid glasses with ADB enabled (visible to `adb devices`; example serial number `<DEVICE_SERIAL>`).
- `ghostlock_rv101` and `root_daemon` already placed on the glasses at `/data/local/tmp/` (if not, see §4).
- **Shizuku manager APK installed** (the default trigger path, see §9.1) + the glasses App pre-authorized.
- Phone and glasses already **paired** in the system Bluetooth settings (control channel).
- (Optional) WiFi media transfer: the phone needs to join the glasses SoftAP `RokidCam` (§9.4).

## 1. Build

Open the `android/` directory in Android Studio (it contains `settings.gradle.kts`); after syncing:

- Glasses App: module `:app-glasses` → `assembleDebug`
- Phone App: module `:app-phone` → `assembleDebug`

Command line:

```bash
cd android
# If there is no wrapper, have Android Studio generate one first, or use the local gradle
./gradlew :app-glasses:assembleDebug :app-phone:assembleDebug
```

> Local (WSL/Kali) build note: the system has no JDK, so use the Temurin 17 bundled in the workspace:
> ```bash
> export JAVA_HOME=/home/kali/rokid_root/tools/jdk/jdk-17.0.20.1+1
> export GRADLE_USER_HOME=/home/kali/rokid_root/gradle_home   # optional (shared dependency cache)
> ```

Outputs:
- `android/app-glasses/build/outputs/apk/debug/app-glasses-debug.apk`
- `android/app-phone/build/outputs/apk/debug/app-phone-debug.apk`

## 2. Deploy the Glasses Side

```bash
cd deploy
# Adjust the variables at the top of deploy.sh to your adb path/serial number
./deploy.sh
```

What the script does:
1. Confirms `ghostlock_rv101` exists in `/data/local/tmp/` (if not, prompts for a manual push).
2. Installs the glasses APK with `adb install -r`.
3. Starts the glasses App (`am start -n <pkg>/.MainActivity`).
   ⚠ Do **not** use `am start-foreground-service` — the Android 12 background start restriction returns
   `app is in background`; starting the FGS from an Activity in the foreground is the viable path.
4. Prompts you to complete Bluetooth pairing first (control goes over RFCOMM; media can use a separate WiFi channel, §9.4).

## 3. Deploy the Phone Side

- Install the phone APK directly (`adb install` or transfer-and-install).
- Pair the phone and glasses in the system Bluetooth settings first, then open the App → it connects automatically or you select the paired glasses.

## 4. Deploy ghostlock_rv101 (one-time)

```bash
adb push /path/to/ghostlock_rv101 /data/local/tmp/ghostlock_rv101
adb shell chmod 755 /data/local/tmp/ghostlock_rv101
```

> The App-side trigger layer is already implemented (see `architecture.md §5`): by default it uses the **Shizuku engine**
> (§9, running the exploit directly with shell identity), and automatically falls back to the root bridge (§7) when Shizuku
> is unavailable. Before on-device validation, you can first use the existing
> `scripts/run_led_orchestrator.sh` to confirm the exploit drops root correctly.

## 5. Connection and Testing

1. Start the App on the glasses (`am start -n com.rokid.rc.glasses/.MainActivity`, which also brings up CaptureService).
2. Open the App on the phone → Bluetooth connection → select the paired glasses (shows "connected" + transport mode).
3. Basics: toggle ghostlock → photo/video → browse the album → download the original file.
4. On the glasses locally: the bottom dashboard shows five status zones; tapping the banner collapses it (fully transparent, does not obscure the view).
5. Glasses display configuration: phone "Advanced" card → "Glasses display" 5 chips / show all / hide all (persisted on the glasses side).
6. WiFi media: on the phone tap "Enable WiFi transfer" → the system dialog asks to join `RokidCam` → the transport mode shows WiFi.
7. Operation log: a two-line preview; long-press opens the full window (scrollable / select-copy / clear).

## 6. Troubleshooting

| Symptom | Possible cause | Handling |
|------|---------|------|
| Cannot connect | Bluetooth off / not paired | Confirm Bluetooth is on at both ends and paired in the system |
| Photo does nothing | Camera lens facing setting | Adjust `LENS_FACING` / camera id in `CameraController` |
| ghostlock ineffective | Shizuku not ready and the helper not resident | Confirm the §9.2 `start_shizuku.sh` or the §7 `root_bridge.sh` has been run each boot (the engine **probes automatically at runtime**, no code change needed) |
| Album empty | Index not refreshed | Restarting the service on the glasses side rescans the folder |

## 7. root Bridge daemon (fallback path)

When Shizuku is unavailable (server not started/not authorized), `ShizukuGhostlockEngine` **automatically falls back** to this bridge mode:

```bash
cd deploy
./root_bridge.sh
```

It will: create `/data/local/tmp/ghostlock_bridge/` with `chmod 777`, write and keep resident `ghostlock_bridge_helper.sh` (a shell loop), and at `[2.5/4]` push `root_daemon` (see §8).

(Removed) In the past `AppGraph.USE_ENGINE` could be used to force a specific engine; since 2026-09-13 this became runtime automatic probing (see §9.3),
and `Engine.DIRECT` remains research-only, kernel panics, and is not in the automatic selection list.

Communication (all in `/data/local/tmp/ghostlock_bridge/`):
- `last.cmd`: the App writes the payload → the helper reads it away and substitutes it into `.ghostlock_root.sh`
- `last.done`: the payload (root) writes the DONE marker → the helper uses it to determine completion
- `last.out`: the helper writes the result back (`OK` / `ERR:...`)

helper log: `/data/local/tmp/ghostlock_bridge_helper.log`

## 8. Resident root daemon (second-level LED control after a single privilege escalation)

After a successful privilege escalation, the payload (`LedPayloads.startRootDaemon`) no longer "re-runs the LPE for every command",
but instead brings up a resident root daemon listening on a UNIX domain socket. Subsequent LED commands from the App go straight over
the socket (millisecond range), and only fall back to the §7 bridge/LPE path when the socket disappears.

### 8.1 Build

```bash
cd deploy
./build_root_daemon.sh
```

- Cross-compiles `root_daemon.c` → `root_daemon` with NDK r27d's `aarch64-linux-android24-clang`.
- The script first copies the source to a Windows-readable path (`/mnt/c/Users/<user>/`) before compiling, because
  Windows clang.exe cannot read WSL `/home` paths.

### 8.2 Deployment and Startup

`root_bridge.sh`'s `[2.5/4]` already pushes `root_daemon` to `/data/local/tmp/root_daemon` (0755).
It is **not** started directly; instead it waits for the privilege-escalation payload to run as root:

```sh
echo 0 > /sys/fs/selinux/enforce 2>/dev/null
if ! grep -q 'root_daemon.sock' /proc/net/unix 2>/dev/null; then
    rm -f /data/local/tmp/root_daemon.sock
    setsid /data/local/tmp/root_daemon >/data/local/tmp/root_daemon.log 2>&1 &
fi
```

> Why `/proc/net/unix` is used to judge liveness: **after a reboot `/data/local/tmp` retains a stale
> `root_daemon.sock` "file" but there is no listener**. Checking only `[ -S ]` would misjudge and skip startup,
> leaving the socket permanently unconnectable. `/proc/net/unix` lists only sockets that have a listener, so it distinguishes the two correctly.

### 8.3 socket Protocol

- Path: `/data/local/tmp/root_daemon.sock` (`chmod 0777`, so untrusted_app can connect).
- One connection = one command: the client connects → writes the command (plain text, may be multi-line/trailing newline) → `shutdownOutput`
  → the daemon reads EOF → runs `sh -c` → writes stdout back → closes the connection.
- App-side client: `RootDaemonClient` (`android.net.LocalSocket`).

### 8.4 Why the daemon must fork per connection (important)

The original daemon was "single-threaded + a `popen` pipe", with two bugs that could hang it permanently:

1. A command backgrounding a child process (the white-LED hold-loop) inherits the `popen` pipe write end → `fgets` never sees EOF.
2. A backgrounded child process inherits the client socket `c` → the client's `readText()` never receives EOF.

Fixes (already written into `root_daemon.c`):
- **Fork one handler per connection**: a single stuck command does not affect the accept loop.
- **Redirect stdout/stderr to a temporary file** (not a pipe): `sh -c` returns right after `... &`, and `waitpid` reaps immediately.
- **The sh -c child closes `fd 3..127`** (including the client socket): background children do not hold the connection.
- `main()` also closes `fd 3..127` at the start: defends against inheriting an old connection when started from a shell via `setsid ... &`.

## 9. Shizuku Mode (new default: the App runs the exploit directly with shell identity)

Principle (see `docs/shizuku-feasibility-report.md` for details): after the Shizuku server is started with adb shell,
the App executes `ghostlock_rv101` through a UserService (uid 2000 + `u:r:shell:s0`, equivalent to adb shell),
replacing the helper loop. **The Shizuku server must be restarted once per boot** (including after a restart caused by an exploit kernel panic).

### 9.1 One-Time Prerequisites

```bash
# 1) Install the Shizuku manager APK (v13.6.0+, download from https://shizuku.rikka.app/download/)
adb install -r shizuku-v13.x.x-rxxx.apk
# 2) Pre-authorize the glasses App (skips the authorization UI; dangerous permission, written back to the authorization config when the server starts)
adb shell pm grant com.rokid.rc.glasses moe.shizuku.manager.permission.API_V23
```

### 9.2 Per-Boot Bootstrap (one command)

```bash
cd deploy
./start_shizuku.sh    # = confirm manager + pm grant + adb shell <lib>/libshizuku.so
```

Verification: `adb shell ps -A | grep shizuku_server` should be resident. After that, when the glasses App boots (BootReceiver
automatically starts CaptureService) → trigger ACQUIRE_ROOT (or automatically with rootMode=AUTO) → the Shizuku engine
runs the exploit with shell identity → after privilege escalation the payload starts root_daemon (§8).

### 9.3 Modes and Fallback

> **2026-09-13 update (P1)**: the engine changed to **runtime automatic probing**, and the `AppGraph.USE_ENGINE` compile-time switch was removed.
> On every privilege escalation `AutoGhostlockEngine` decides: Shizuku binder available → use Shizuku (which still falls back to bridge internally);
> otherwise, if the bridge helper heartbeat (`ghostlock_bridge/helper.alive`) is fresh → use bridge; if neither → select the Shizuku engine
> and report "server/helper must be started first". **Changing environments requires no APK rebuild.**
- `Engine.SHIZUKU` (formerly the default, now one branch of the automatic selection): automatically falls back to the §7 bridge helper when Shizuku is unavailable (server not started / not authorized /
  binding failed).
- `Engine.BRIDGE`: the helper loop (`deploy/root_bridge.sh` kept resident).
- `Engine.DIRECT`: the App connects directly (symlink trick; `untrusted_app` running the exploit kernel panics, **no longer used**,
  `ShellGhostlockEngine` is kept for research only and is also not in the automatic selection list).
- Note: if the Shizuku server was started as **root** (the child process becomes uid 0), the engine tries to
  drop back to shell with `/data/local/tmp/run_shell` before running the exploit; without run_shell it reports an explicit error.
  Therefore bootstrap always goes through the adb/shell startup in §9.2.

### 9.4 WiFi Transfer (both control and media prefer WiFi, Bluetooth is the fallback)

- Topology: the glasses **automatically start a SoftAP after privilege escalation** (SSID `RokidCam`, password `rokid1234`,
  fixed IP `192.168.43.1`), and the glasses-side `WifiServer` listens on **TCP 8555** (sharing the same
  `ConnectionHandler`/`CommandRouter`/frame protocol with Bluetooth) ⇒ **once WiFi is connected, ordinary commands also go over WiFi**.
- **No manual enabling needed**: before a media operation (thumbnail/download), `Remote.ensureMediaLink()` automatically
  asks the glasses to start the hotspot (`ENSURE_WIFI_AP`, polling until the AP is ready) → joins the AP → establishes TCP;
  controlled by the persistent preference `auto_wifi_media` (**on by default**, the "WiFi transfer" switch on the settings page).
  The first time you join the AP, the system shows a "Join RokidCam" confirmation dialog — **tap allow** (it does not appear when the phone is already on that AP).
- ⚠ **The two channels are mutually exclusive (most important)**: on the glasses side, both `BluetoothServer.accept()` and `WifiServer.accept()`
  first call `graph.connection?.close()` (last one to connect wins). Therefore **any command sent over Bluetooth kicks out the WiFi socket**,
  and media silently falls back to Bluetooth — this is the root cause of "it is clearly fast, yet it shows Bluetooth" and "a 10-second video is extremely slow"
  (fixed: `Remote.link()` prefers WiFi + `ensureConnected()` does not re-dial Bluetooth while WiFi is connected).
- Display: the remote page shows three states (WiFi connected / **WiFi connecting…** / Bluetooth), updated in real time by
  `Remote.onTransportChanged` (`LinkClient.onConnected`/`onDisconnected`).
- Fallback: automatically falls back to Bluetooth when WiFi is not connected or is interrupted. **Measured speeds**: the same 10.6-second / 16,736,320-byte video —
  **WiFi 3.0–3.8 seconds (4.4–5.6 MB/s)**; **Bluetooth 324 seconds and timed out as a failure (51.7 KB/s)**, a difference of about 85×.
- Glasses-side debug command: `am broadcast -n com.rokid.rc.glasses/.DebugReceiver --es cmd PUSH_TEST [--es code <code> --es arg <arg>]`
  —— actively pushes one structured progress event, used to verify "the glasses-side push goes over the current connection → the phone receives it on both channels"
  (before the fix only Bluetooth's `onPush` was registered, so pushes were dropped when WiFi was the current channel). When the phone receives it, it prints `RemotePush: progress code=… arg=…` to logcat.
- Phone-side debug commands: `am broadcast -n com.rokid.rc.phone/.DebugReceiver --es cmd WIFI_CONNECT`
  (optionally with `--es host <ip>` for a direct connection test), `WIFI_DISCONNECT` to close.
- Note: on this device's firmware, **STA and SoftAP cannot be maintained simultaneously** (turning on the hotspot kicks out the existing WiFi STA);
  after the hotspot is turned off, the STA recovers automatically (`cmd wifi stop-softap`, root).

### 9.5 Glasses Dashboard and Remote Display Configuration

- The glasses-side MainActivity is a **bottom-banner information dashboard** (five zones: connection/Root/camera/storage/indicator),
  **fully transparent (no panel background color, no background fill)** — the text floats directly over the line of sight and does not obscure the view;
  it is primarily for display, with operation driven by the phone; tapping the banner (touchpad tap or DPAD center key/Enter) toggles expanded/collapsed.
- The phone-side "Advanced" card contains a "**Glasses display**" section: 5 chips push `SET_DISPLAY_CONFIG` in real time,
  plus "show all / hide all" shortcuts; the configuration is persisted on the glasses side (survives reboot) and is automatically read back and synchronized when the phone connects.
  When all five categories are hidden, the glasses automatically collapse into a folded bar.
- Debug broadcast (to avoid input injection against the glasses — measured to carry a risk of full-device reboot):
  `am broadcast -n com.rokid.rc.glasses/.DebugReceiver -a com.rokid.rc.glasses.DEBUG --es cmd <TOGGLE_PANEL|SET_DISPLAY|SET_LOCALE>` (SET_DISPLAY takes `--ez conn/root/camera/storage/led true|false`).

### 9.6 Multilingual Support (14 locales) and In-App Language Selection

- Both Apps support: Traditional Chinese (default), English, Simplified Chinese, Japanese, French, Spanish, Thai, Vietnamese, Korean, Arabic (RTL), Portuguese, Russian, Ukrainian, German.
- **Phone-side in-app switching**: Remote tab → "Language: <current language>" button → single-choice dialog (follow system + 14 native language names);
  after selection it takes effect immediately via `AppCompatDelegate.setApplicationLocales` + `recreate()`, and is written to the `app_locale`
  SharedPreferences, retained after an App restart (`LocaleHelper.apply()` is applied at startup).
- By default it follows the system language; the per-app override is also available via broadcast: phone `DebugReceiver SET_LOCALE --es lang <tag>`,
  glasses `Glasses DebugReceiver SET_LOCALE` (`lang` omitted = restore following the system; does not affect the system language).
- **Localization coverage**: the phone-side interface, Toasts, dialogs, the **operation log** and the status summary all go through string resources
  (**203 keys × 14 locales**; 68 was the initial number); only the `DebugReceiver` logcat/`TEST_LOG` test messages remain in Chinese (developer tools).
  Translation sources: `scripts/i18n/phone_keys.tsv` (zh-TW/en) + `scripts/i18n/out/*.xml` (the other 12 locales);
  flow: `normalize_locale_terms.py` (aligns terminology with each locale's established usage, e.g. ja `Wi-Fi`/`メガネ`, de `WLAN`,
  ar `واي فاي`) → `validate_translations.py` (coverage/placeholder/escape checks) →
  `apply_phone_strings.py` (merges into `values*/strings.xml`; all three are re-runnable).
- Implementation and verification details: `reports/phone_i18n_localization_report.md` (including on-device evidence for en/ja/ar/th and RTL screenshots).

### 9.7 Operation Log Scrolling and Brand Icon

- The phone-side "operation log" is a **two-line preview** (always showing the latest two lines); **long-press** pops up the "operation log (full)"
  window: scrollable, long-press to select and copy, with built-in clear, synchronized live with the preview; the "clear" button empties it in one tap.
- Debug injection: `am broadcast -n com.rokid.rc.phone/.DebugReceiver -a com.rokid.rc.phone.DEBUG --es cmd TEST_LOG --ei n 25`
- Brand icons (SVG): `android/branding/logo.svg` (full color), `logo_monochrome.svg` (monochrome);
  both App launchers use adaptive icons (`mipmap-anydpi-v26` + foreground/monochrome vector + dark blue background
  `#0B1220`, with themed icon support on Android 13+); the glasses foreground service notification uses `ic_notification`.
  Design concept: AR glasses × camera aperture (cyan) × privacy shutter (amber).

### 9.8 Wireless adb (adb-over-TCP) — ✅ **Feasible, end-to-end verified on 2026-09-20**

> ⚠ **This section's original conclusion ("this build does not support adb-over-TCP") has been overturned**. Full evidence:
> [`reports/wireless_adb_feasibility_2026-09-20.md`](../reports/wireless_adb_feasibility_2026-09-20.md).

- **The truth**: the glasses adbd **binds TCP all along**, at the **IPv6 wildcard `[::]:5555`**
  (`/proc/net/tcp6`, uid 2000; `netstat` shows `tcp6 [::]:5555 LISTEN`).
  AOSP logic: `service.adb.listen_addrs` → `service.adb.tcp.port` → **`persist.adb.tcp.port`**
  are consulted in that order, so `persist.adb.tcp.port=5555` makes adbd **automatically open TCP on every start** (across reboots, no cable needed).
- **Root cause of the original misjudgment**: the detection code only grepped the IPv4 `/proc/net/tcp` (`:15B3`) → it could not see the IPv6-only listener
  → it wrongly reported `adb-over-tcp not supported on this build` (**fixed**: `CommandRouter.enableWifiAdb()`
  now checks both `/proc/net/tcp` and `/proc/net/tcp6`, and reports `boundOn`).
- **Measured (PC joined the glasses SoftAP `RokidCam`)**:
  ```
  adb connect 192.168.43.1:5555     → connected
  adb devices -l                    → 192.168.43.1:5555   device
  adb -s 192.168.43.1:5555 shell id → uid=2000(shell) context=u:r:shell:s0
  ```
  `adb devices -l                    → 192.168.43.1:5555   device`
  The same `adb devices` showed **USB glasses + USB phone + WiFi glasses** three transports coexisting;
  the same wireless run completed 4 consecutive sessions (`SESSION_1..4`, `uid=2000`) all successfully → concurrent multiple connections hold.
  One-command verification: `SERIAL=<serial> bash deploy/adb_wifi_verify.sh` (**no need to switch the PC network**;
  it only attempts a connection when the PC is already on the glasses' subnet).
- **⚠ Do not restart adbd**: the documented "restarting adbd drops USB enumeration" is a side effect of **the restart itself**
  (vendor startup method + USB FunctionFS), **unrelated to the TCP binding**; to open TCP you only need to set
  `persist.adb.tcp.port 5555` and then **reboot normally once**.
- **Security**: this amounts to exposing the debug port to the network (`ro.adb.secure=1` still requires a key, but the local
  `/data/misc/adb/adb_keys` is preloaded with 6 keys, and the SoftAP password `rokid1234` is hard-coded in the APK).
  When not needed: `setprop persist.adb.tcp.port ""` + reboot, or turn off the SoftAP.
- **Fallback route**: if a future OTA breaks adbd, you can use `deploy/adb_tcp_relay.c`
  (TCP → `/dev/socket/adbd`; **requires root**, because shell connecting to that socket gets `Permission denied`).
- **Current state of "root without a cable"**: wireless adb works → **privilege escalation can be re-armed without USB**
  (`deploy/root_bridge.sh` can be changed to go over `adb connect 192.168.43.1:5555`),
  but **the first connection still requires writing that host's public key into `adb_keys`** (the glasses have no screen to pop an authorization dialog, so it needs root to write).
  Flashing Magisk remains the only route to "persistent root from boot", but it is no longer a prerequisite for "cable-free".

### 9.9 Silent Capture (force-suppress photo/video tones)

- Mechanism: `libcameraservice` plays `/product/media/audio/ui/{camera_click,camera_focus,VideoRecord,VideoStop}.ogg`
  and the Rokid-custom `rkd_record_{start,end}.ogg`; `/product` is read-only, but root can **bind-mount legitimate silent OGGs**
  over these 6 files → playback is silent; `umount` restores them; **reboot clears them automatically**.
- Usage: after privilege escalation the App **applies it automatically** (on by default); the phone's "Advanced" card can toggle it live; the `SET_SILENT` broadcast allows automation.
- Verification: after suppression md5 == the silent file, mount count 6; photo/video/stop all work normally with no MediaPlayer errors;
  after restore the md5 is back to the original and no mounts remain. See `reports/silent_capture_mechanism.md` for details.
- Limitations: requires root; must be re-applied every boot (same lifecycle as root); affects only camera/video tones.

### 9.11 Ring Control (photo/video)

The ring (BLE HID) can control the glasses to take photos/record video, and both paths are implemented and verified on device; **path B starts
automatically after a successful privilege escalation** (it is also restored when the App restarts; the device pattern is stored in `ring_ctrl.pattern`, default `RGR`,
and can be specified with `RING_START --es name`, for example the glasses touchpad `TP`):

| Path | Location | Description |
|------|------|------|
| **A: phone-side interception** | `app-phone` `ring/RingController`+`RingGestureDetector` | The ring is paired to the phone; `MainActivity.dispatchKeyEvent`/`dispatchGenericMotionEvent` intercept HID events → gesture → commands the glasses to capture over the existing BT/WiFi channel (root functions on the glasses side work as usual) |
| **B: glasses-side root direct read** | `app-glasses` `ring/RingEvdevReader`+`RingControlManager` | The root daemon runs `getevent -lt /dev/input/eventN` in the background, writing to `/data/local/tmp/ring_events.log`; the App fetches increments every 400 ms with `tail -c +offset` → gesture → **local** capture (usable in the background, without going through the phone) |

- Default gestures: **single tap = photo, double tap = start/stop recording, long press = toggle silent, scroll wheel = switch the glasses display information**.
- Phone-side UI: the "ring control" card on the Remote tab — path A enable switch, gesture description, status, **gesture learning** and **pair ring**;
  plus the **glasses-side (path B) switch and status** (`SET_RING` + `STATUS.ring*`).
- Interception points: keyboard/media keys go through `dispatchKeyEvent`; **clicks from a mouse-style ring go through `dispatchTouchEvent`**
  (only MOUSE/TOUCHPAD are intercepted based on `event.source`, so finger touch is unaffected); the scroll wheel goes through `dispatchGenericMotionEvent`.
- Only keys "the ring might send" are intercepted (ENTER/DPAD_CENTER/media keys/volume keys/home key/F1–F12) plus custom mappings; **everything else passes through**
  (otherwise system keys such as BACK would be swallowed).
- **Phone-side background limitation**: Android media keys are dispatched to the media button session; when a music App is present (measured:
  `Media button session is yt.morphe.noname.exe`) our `RingMediaSession` does not receive them,
  and this project does not contend for them. **The reliable solution for background control = glasses-side path B**.
  **Learnable key mappings**: learned key codes can be bound to actions (photo/record/silent/next display group/previous display group/no action),
  stored in prefs and taking precedence over the default gestures; long-pressing "gesture learning" clears all custom mappings.
- Ring discovery/pairing (shared `shared/ring/`): `RingScanner` (BLE scan including raw AD/manufacturer ID),
  `RingGatt` (connect, enumerate services and read the HID Report Map), `RingBonder` (pair with the live `BluetoothDevice` after scanning;
  **must not reconstruct from a MAC string, random addresses yield GATT 133**), `RingWatcher` (continuous watching + automatic pairing).
- ⚠️ **BLE scanning affects existing BLE connections**: continuous scanning (`WATCH_RING`) was measured to interrupt the GATT/HID
  connection of an AIVELA health ring; everyday ring control does not need scanning, and scanning is used only during pairing via the in-app "pair ring" (a single 15-second pass).
- Debug commands: phone `SCAN_RING`/`PAIR_RING`/`PROBE_RING`/`WATCH_RING`/`CONNECT`;
  glasses `RING_DEVICES`/`RING_START`/`RING_STOP`/`RING_STATUS` (see
  `reports/ring_control_report.md` §6 for details).
- Notes (implementation points, all of which were hit):
  1. When the display is asleep the FGS cannot start in the background → when `RING_START` detects the camera is not ready it **starts CaptureService via root instead**.
  2. `getevent`'s **stop and start must not be written on the same line** (`pkill -f` matches itself and commits suicide), and the
     `[g]etevent` bracket trick + a PID file are used.
  3. **The read offset must be guarded against overrun**: after the log file is recreated, the old offset makes `tail` read nothing forever → compare the file size each round and reset to zero.
  4. **Gesture determination uses event timestamps** (the `[sec.usec]` of `getevent -lt`), otherwise batch catch-up reads compress the timing to 0 ms.
  5. Recovery after the program is reclaimed: if the log file has activity within 10 seconds → continue reading from the last offset (no missed gestures); a root-side
     keep-alive is also provided (checks every 15 seconds and pulls the App back; not being able to wake it after `force-stop` is an Android limitation).

### 9.10 Debug Commands and Script Index

**Broadcast commands (for automated verification; command format: `am broadcast -n <component> -a <action> --es cmd <CMD> [extras]`)**

| Target | Component / action | Command | Purpose |
|------|---------------|------|------|
| Phone | `.DebugReceiver` / `com.rokid.rc.phone.DEBUG` | `SET_LED` (`--ez on`, `--es color`) | Toggle LED |
| Phone | same as above | `CAPTURE_PHOTO` / `START_VIDEO` / `STOP_VIDEO` | Photo and video |
| Phone | same as above | `SET_GHOSTLOCK --ez enabled` | ghostlock toggle |
| Phone | same as above | `ACQUIRE_ROOT` | Trigger privilege escalation |
| Phone | same as above | `GET_DEVICE_INFO` / `LIST_MEDIA_INFO` / `TEST_CHAIN` | Query status / media chain test |
| Phone | same as above | `SET_DISPLAY --ez conn/root/camera/storage/led` | Set the glasses display content |
| Phone | same as above | `WIFI_CONNECT` (optionally with `--es host <ip>`) / `WIFI_DISCONNECT` | WiFi media channel |
| Phone | same as above | `WIFI_ADB` | Ask the glasses to enable wireless adb (**verified feasible on 2026-09-20**; the glasses return `ip`/`port`/`boundOn`, and the PC connects with `adb connect <ip>:5555`) |
| Phone | same as above | `SET_SILENT --ez enabled` | Silent capture (suppress shutter/video tones; requires root) |
| Phone | same as above | `SET_LOCALE --es lang <tag>` | per-app language |
| Phone | same as above | `TEST_LOG --ei n 25` | Inject log lines (verify the two-line preview/long-press window) |
| Glasses | `.DebugReceiver` / `com.rokid.rc.glasses.DEBUG` | `TOGGLE_PANEL` | Collapse/expand the dashboard |
| Glasses | same as above | `SET_DISPLAY --ez …` | Set the display configuration locally |
| Glasses | same as above | `SET_LOCALE --es lang <tag>` | per-app language |

> ⚠ On the glasses side, do **not** use `input tap/keyevent` for automation (measured to have caused a full-device reboot).

**Script Index (`deploy/`)**

| Script | Purpose |
|------|------|
| `deploy.sh` | Install the glasses APK and start the App |
| `start_shizuku.sh` | Once per boot: install/confirm the manager, pre-authorize, start the Shizuku server |
| `test_shizuku_e2e.sh` | Shizuku path end-to-end privilege-escalation test (with automatic retry after a panic) |
| `test_bridge_fallback.sh` | Test of the fallback path when Shizuku is unavailable |
| `recover_and_verify.sh` | One-command regression after glasses adb recovery (install + privilege escalation + non-destructive WIFI_ADB verification) |
| `root_bridge.sh` / `run_root.sh` / `get_root_now.sh` | bridge helper deployment / run commands as root / fully automatic privilege escalation |
| `lock_ota.sh` | Lock OTA (`pm suspend`, persists across reboot) |
| `check_ota.sh` / `ota_api.py` / `ghostlock_patch_check.py` | OTA version queries and GhostLock patch determination |
| `env.sh` | **The single source of `ADB`/`SERIAL` shared by all scripts** (can be overridden with environment variables) |
| `install_root_keepalive.sh` | Install keep-alive + OOM protection after acquiring root (runnable in any already-rooted state) |
| `lease.sh` / `test_lease.sh` | The cross-tool **shared lease** primitive and its test suite (works with the App keeper and desktop tools; the test needs no root and does not trigger the exploit) |
| `proc_util.sh` | Safe process killing (avoids `pkill -f` self-matching / killing `*_desktop` variants by mistake) |
| `test_keeper_lease_guard.sh` | End-to-end test of the keeper lease guard (creates root failure; the pass criterion is the log, and recovery uses `root_restore_delayed.sh` to avoid re-running the exploit)|
| `root_restore_delayed.sh` | Delayed recovery process: started through the root socket, it brings the daemon + keepalive back when the time comes |
| `buddy_watch.sh` | `/proc/buddyinfo`, `/proc/meminfo` sampling (diagnosing mm leak failures) |
| `keep_alive.sh` / `watch_root.sh` / `build_root_daemon.sh` / `build_ring_input_daemon.sh` / `setup_direct.sh` / `trigger_root_daemon.cmd` | Auxiliary/research use (`setup_direct.sh`'s DIRECT path kernel panics; do not use it in a production flow) |

## Privilege-Escalation Operations (P0–P3, 2026-09-13)

> Background and investment judgment in `reports/su_escalation_roi_assessment.md` (measured 84–112 seconds to acquire root,
> 143 historical runs, failure classification). The conclusion is **not to rewrite the exploit**, but to strengthen the operations layer.

### P0: root daemon liveness hardening

| Mechanism | Location | Behavior |
|------|------|------|
| root-side keep-alive | payload `LedPayloads.startRootDaemon()` / `deploy/install_root_keepalive.sh` installing `/data/local/tmp/root_daemon_keepalive.sh` | Checks the listener every 20 seconds and `setsid`s a rebuild if the daemon dies (**no adb needed, no exploit re-run**); measured **9 seconds** to recover |
| OOM protection | same as above | Both the daemon and the keep-alive are set to `oom_score_adj=-1000` (to avoid being reclaimed by lmkd) |
| pid files | `/data/local/tmp/root_daemon.pid`, `root_daemon_keepalive.pid` | Precise targeting, easy cleanup |
| App-side keeper | `RootDaemonKeeper` (one round every 30 seconds, started by `App.kt`/`CaptureService`) | On failure: clear the stale socket → judge "can anyone still trigger privilege escalation" → automatically rebuild (5-minute cooldown) or report that intervention is needed |
| **Shared lease (avoid two tool sets stepping on each other)** | `RootDaemonKeeper` (App side) + `deploy/lease.sh` (deploy side) + desktop tools | Fixed contract file `/data/local/tmp/ghostlock_op.lease` (contents `owner=… pid=… ts=…`). The App keeper checks the lease **before clearing a stale socket**; only the lease holder may use the exploit, preventing the desktop side and the App from modifying `.ghostlock_root.sh` at the same time; there is also a **TOCTOU pre-check** before re-arming |
| **helper variants** | `root_bridge.sh` = `ghostlock_bridge_helper.sh`; desktop tools = `ghostlock_bridge_helper_desktop.sh` | ⚠ The App's heartbeat probe recognizes only the former and `ghostlock_bridge/helper.alive`; if **only the desktop helper is running**, the keeper reports "needs re-arming" (acceptable: desktop tools usually hold the lease at the same time) |
| **Safe process killing** | `deploy/proc_util.sh` | Replaces `pkill -f` (which self-matches and also kills similarly named `*_desktop` variants by mistake) |
| **Memory sampling** | `deploy/buddy_watch.sh` | Samples `/proc/buddyinfo`, `/proc/meminfo`, used to judge whether KernelSnitch mm leak failures correlate with high-order page exhaustion |
| Probe fix | `AutoGhostlockEngine` | ⚠ **Must not use `ps` for the decision**: Android apps cannot see other processes (measured: the helper was running yet it returned false). It now reads the `ghostlock_bridge/helper.alive` heartbeat file written by the helper every second; for Shizuku it uses the official `Shizuku.pingBinder()` |

### P0-b: Failure visualization

Phone "Settings → Capture and permissions → root status" shows a status line + hint (tap for operating instructions):

| Status code | Phone text (zh-TW) |
|----------|------------------|
| `root_ok` | root：可用 |
| `root_rearming` | root：失效，自動重建中… |
| `root_rearmed` | root：已自動重建 |
| `root_lost_need_arm` | root：已失效，需重新武裝 ＋「請執行：deploy/root_bridge.sh 或 deploy/start_shizuku.sh」 |
| `root_lost_cooldown` | root：已失效（冷卻中，稍後自動重試） |

Measured (after a real reboot): `root：已失效，需重新武裝` / `請執行：deploy/root_bridge.sh  或  deploy/start_shizuku.sh` / `看守 2 次 · 自動重建 0 次`。

### P1: Runtime engine probing

`AppGraph` no longer has the `USE_ENGINE` compile-time constant; instead `AutoGhostlockEngine` probes on every call:
Shizuku running → Shizuku engine (with bridge fallback inside); otherwise, if the bridge helper heartbeat is fresh → bridge engine;
neither → select the Shizuku engine and report `gl_shizuku_not_running` (hinting which script to run).
When the preferred engine fails, it automatically tries once more if the other pipeline is available.

### P2: Privilege-escalation metrics

`AppGraph.acquireRootMetered()` is the single entry point for all privilege-escalation calls, recording
`lastEngine`/`lastOk`/`lastMs`/`attempts`/`successes`/`firstTryOk` (**stored in prefs, not reset to zero when the App is killed**;
the fast path is not counted toward "first-try success", to avoid flattering the numbers), and reports them to the phone with `STATUS` along with the keeper counts.

### P3: Cable-free arming (`persist.adb.tcp.port`) — ✅ **Feasible (original conclusion overturned on 2026-09-20)**

The `WIFI_ADB` command sets `persist.adb.tcp.port=5555` (the official Android 11+ wireless debugging property;
setting only `service.*` is valid only for the current boot). It is already implemented in code, and the ack reports `autoAfterReboot`/`boundOn`.

**What was wrong with the original "not feasible" conclusion (the 2026-09-13 measurement method)**: when deciding "is it bound", it only grepped
the **IPv4** `/proc/net/tcp` (`:15B3`); the local adbd binds the **IPv6 wildcard**, which appears
only in `/proc/net/tcp6` → the count was always 0, so it was misjudged as "adbd ignores this property".

**2026-09-20 measurement (steady state, without any modification)**:

| Check | Result |
|------|------|
| `getprop persist.adb.tcp.port` | ✅ `5555` (retained across reboots) |
| `grep -c ':15B3 ' /proc/net/tcp` (IPv4) | ❌ 0 (**the old criterion only saw this**) |
| `grep -c ':15B3 ' /proc/net/tcp6` (IPv6) | ✅ **2** (LISTEN + connection) |
| `netstat -tlnp` | ✅ `tcp6 [::]:5555 LISTEN` |
| `nc 127.0.0.1 5555` on device | ✅ `TCP connect OK` |
| **PC joins SoftAP → `adb connect 192.168.43.1:5555`** | ✅ `device` |
| **`adb -s 192.168.43.1:5555 shell id`** | ✅ `uid=2000(shell) context=u:r:shell:s0` |

→ **Conclusion: this build supports adb-over-TCP**; cable-free re-arming **is now feasible** (after `adb connect`, just run
`deploy/root_bridge.sh`; no USB needed anymore). The only remaining limitation is **first-time authorization from a new host**:
the glasses have no screen to show a dialog, so that host's public key must first be written into `/data/misc/adb/adb_keys` as root.

⚠ Do not restart adbd again (it drops USB enumeration) — `persist.adb.tcp.port` only needs **one normal reboot** to take effect,
and it is long since in effect on this device; it works right now.

See [`reports/wireless_adb_feasibility_2026-09-20.md`](../reports/wireless_adb_feasibility_2026-09-20.md) for details.

## Appendix: Cross-Compiling Device-Side Probes (added 2026-09-13)

Originally there was only one route, the Windows-version NDK clang (`deploy/build_root_daemon.sh`), but inside the sandbox `/mnt/c` is **read-only**
→ temporary files could not be placed there, leaving "the local machine cannot compile new probes". The switch was made to **ziglang** (a pip package that bundles its own cross-compiler):

```bash
.venv/bin/pip install ziglang          # once is enough
PUSH=1 SERIAL=$SERIAL bash deploy/build_probe_zig.sh deploy/kgsl_micronode_probe.c kgsl_micronode_probe
```

- Target: `aarch64-linux-musl` **static** ELF (can run directly on Android; measured that `/dev/kgsl-3d0` can be opened).
- ⚠ zig's cache directory must be inside the workspace (`ZIG_GLOBAL_CACHE_DIR`/`ZIG_LOCAL_CACHE_DIR` → `build-cache/`),
  otherwise the default `~/.cache/zig` fails in the read-only sandbox (`CacheCheckFailed`).
- Artifacts go in `build-cache/` (already listed as a `.gitignore`-style scratch area), and sources stay in `deploy/*.c`.
