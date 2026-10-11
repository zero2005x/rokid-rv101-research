# Rokid Dual-Android Remote Capture System — Architecture Design

> 中文版：[architecture.md](architecture.md)

> **Purpose statement**: This project is for security research and penetration testing on **owned/authorized Rokid glasses devices**. The glasses side uses the existing `ghostlock_rv101` LPE (local privilege escalation) to control the camera privacy LED as root, in order to validate the risk that "the privacy indicator LED can be bypassed". Do not use it on unauthorized devices or to violate the privacy of others.

## 1. System Overview

**Dual-channel design**: **Both control commands and media prefer WiFi TCP** (glasses SoftAP `192.168.43.1:8555`; the same connection,
the same `CommandRouter`), and only fall back to Bluetooth RFCOMM when WiFi is unavailable. Before a media operation, the WiFi channel is
established **automatically** (`ENSURE_WIFI_AP` → join AP → connect TCP), controlled by the persistent preference `auto_wifi_media` (default on).

> ⚠ **The two channels are mutually exclusive**: on the glasses side, both `BluetoothServer.accept()` and `WifiServer.accept()` first execute
> `graph.connection?.close()` (last one to connect wins). Therefore, if ordinary commands switch to Bluetooth, they kick out the WiFi socket and
> media silently falls back to Bluetooth — this is exactly the cause of "transport always on Bluetooth, 10-second video extremely slow" (see FACTS §7 P28).

```
┌──────────────────────────┐   (1) Control: WiFi TCP 8555 preferred, Bluetooth RFCOMM fallback (JSON over length-prefixed frames)
│  Phone App (app-phone)   │◄══════════════════════════════════════════════════════►
│  - Remote/album/local UI │   (2) Media: same WiFi TCP 8555 (glasses SoftAP 192.168.43.1)
│  - Transfer mode display │◄──────────────────────────────────────────────────────►
│  - Glasses display cfg   │                                                      │
│    (5 chips)             │        ┌─────────────────────────────────────────────┐
│  - 14 locales / log view │        │  Glasses App (app-glasses)                  │
└──────────────────────────┘        │  ┌───────────────┐  ┌──────────────┐        │
                                    │  │BluetoothServer│─►│ CommandRouter│        │
                                    │  └───────────────┘  │ (dispatch)   │        │
                                    │  ┌───────────────┐  └───┬───┬───┬──┘        │
                                    │  │  WifiServer   │─────►│   │   │           │
                                    │  │ (TCP 8555)    │      │   │   │           │
                                    │  └───────────────┘      ▼   ▼   ▼           │
                                    │   CommandRouter → GhostlockEngine / Camera / │
                                    │                   MediaRepo / WifiHotspot / │
                                    │                   DisplayConfigStore        │
                                    │        │ root payload (suppress LED / hotspot)│
                                    │        ▼                                    │
                                    │  /data/local/tmp/ghostlock_rv101            │
                                    └─────────────────────────────────────────────┘


```

- **Glasses side**: the persistent foreground service `CaptureService` (`LifecycleService`) holds the Bluetooth RFCOMM server,
  the **WiFi TCP server**, the CameraX camera, the album index and the Ghostlock engine. `MainActivity` is a
  **fully transparent bottom-banner information dashboard** (connection/root/camera/storage/indicator; does not obstruct the
  line of sight; its displayed content can be configured remotely from the phone).
- **Phone side**: `MainActivity` + several `Activity` classes (album/media/local/viewer); both control and media are routed by
  `Remote.link()` (`TcpClient` preferred, `BluetoothClient` fallback), the media channel is managed by `WifiLink`,
  and it is established automatically before media operations by `Remote.ensureMediaLink()`.

## 2. Module Layout (Gradle multi-module)

| Module | Type | Responsibility |
|------|------|------|
| `:shared` | Android Library | Protocol message models, `FrameCodec` (length-prefixed framing, shared by BT/TCP), `DisplayConfig`/`WifiApInfo` models, `Protocol` constants |
| `:app-glasses` | Android App | Glasses side: BT Server + WiFi Server (TCP 8555), CameraX photo/video, album index, GhostlockEngine (Shizuku/Bridge/Direct), SoftAP management, dashboard UI, locales, LED suppression payload |
| `:app-phone` | Android App | Phone side: `LinkClient` (shared BT/WiFi transport logic), remote/album UI, glasses display configuration, two-line log preview + long-press full window, locales, original-file download and local playback |

The shared protocol lives in `:shared`; both ends import the same code, avoiding inconsistent message fields.

## 3. Glasses Side (app-glasses)

### 3.1 Components

**`CaptureService` (foreground service, LifecycleService)**
- Creates `BluetoothServer` (RFCOMM, SPP UUID) and `WifiServer` (TCP 8555).
- Creates `CameraController`, binding CameraX to the service lifecycle.
- Holds the singleton `AppGraph` (`GhostlockEngine`, `MediaRepository`, `CameraController`,
  `WifiHotspot`, `DisplayConfigStore`).
- With `rootMode=AUTO`, automatically ensures the SoftAP (media channel) after successful privilege escalation; the foreground notification keeps it from being reclaimed.

**`BluetoothServer` / `WifiServer` / `ConnectionHandler`**
- The two servers are symmetric: after accept, they wrap the socket stream in a `ConnectionHandler`, and **share the same
  frame protocol and `CommandRouter`** (transport medium is transparent).
- Single connection (a new connection replaces the old one); `AppGraph.btConnected` / `wifiConnected` feed the dashboard display.

**`CommandRouter`**
- Dispatches on the message `type` to the corresponding handler and produces a response (see `protocol.md`).
- Covers: status/album/media, ghostlock/root, LED, `ENSURE_WIFI_AP`, `SET_DISPLAY_CONFIG`,
  `ENABLE_WIFI_ADB`.

**`CameraController`**
- Binds CameraX `ImageCapture` (photo) + `VideoCapture<Recorder>` (video).
- To accommodate the single-stream limit of the Rokid CHI HAL, it uses a "single use case" approach: photo binds only `ImageCapture`,
  and video rebinds to `VideoCapture` (the rebind happens on the main thread); Preview is never bound.
- When ghostlock is on, it **suppresses the LED first and then starts the stream** (to avoid a flash at the instant the stream starts), and restores afterwards.

**`GhostlockEngine` (LPE invocation layer, three implementations)**
- Interface contract: `isEnabled`, `rootAcquired`, `onProgress`, `suspend exec(payload)`, `suspend acquireRoot()`.
- `ShizukuGhostlockEngine` (**default**): runs the exploit directly through a Shizuku UserService **as shell (uid 2000)**
  (`CommandService` + custom AIDL); built-in early-death detection of the exploit + internal retry ×3;
  automatically falls back to `BridgeGhostlockEngine` when Shizuku is unavailable (see `docs/shizuku-feasibility-report.md`).
- `BridgeGhostlockEngine` (fallback): delegates to the shell helper kept resident by `deploy/root_bridge.sh` to run the exploit
  + replace the payload; the App communicates via `last.cmd/last.out`.
- `ShellGhostlockEngine` (Direct, research only): the App runs it directly; measured to kernel panic as `untrusted_app`, unusable.
- `LedPayloads` / `LedSuppressor` / `RootDaemonClient`: LED suppression and restore. After privilege escalation, a resident
  `root_daemon` runs (UNIX socket, see §5.1); LED commands **prefer the socket (millisecond range)**.

**`WifiHotspot` (SoftAP management)**
- After root, runs `cmd wifi start-softap RokidCam wpa2 <pass>` through `root_daemon` (**dispatched in the background**,
  to avoid blocking during the wifi service transition); detects whether the AP is ready via `NetworkInterface("wlan2")` (`192.168.43.1`).
- On this device's firmware, **STA and SoftAP are mutually exclusive**: turning on the hotspot kicks out the existing WiFi STA, and the STA recovers automatically after the hotspot is stopped.

**`DisplayConfigStore` (dashboard display configuration)**
- Five switch categories (connection/root/camera/storage/LED) persisted in SharedPreferences; the phone can configure them remotely
  (`SET_DISPLAY_CONFIG`); change notifications are always dispatched on the main thread; when everything is hidden the dashboard automatically collapses into a folded bar.

**`MainActivity` (fully transparent information dashboard)**
- Bottom banner, **no panel background color** (text floats directly over the line of sight without obscuring the view); five-zone status coloring; refreshed every 2 seconds.
- Tapping the banner (touchpad tap or DPAD center key/Enter) toggles expanded/collapsed.
- `DebugReceiver` (`com.rokid.rc.glasses.DEBUG`) provides `TOGGLE_PANEL`/`SET_DISPLAY`/`SET_LOCALE`
  automation commands (**to avoid input injection** — measured to carry a risk of full-device reboot).

**`SilentMode` / `SilentPayloads` (silent capture)**
- Uses root to `bind-mount` a legitimate silent OGG over the 6 camera/video tone files under `/product/media/audio/ui/`
  (`camera_click`/`camera_focus`/`VideoRecord`/`VideoStop`/`rkd_record_start`/`rkd_record_end`);
  `umount` restores them, and reboot clears them automatically. On by default; applied automatically by CaptureService after privilege escalation, and the phone can toggle it with `SET_SILENT`.
- The silent files are generated and embedded by `scripts/gen_silent_payload.py` (see `reports/silent_capture_mechanism.md`).

**`MediaRepository`**
- Physical storage: `context.getExternalFilesDir(null)/media/albums/<albumId>/<file>`.
- Index: `media/index.json`; thumbnail cache `cache/thumbs/<mediaId>.jpg`.
- Provides `createAlbum/renameAlbum/deleteAlbum/listAlbums/listMedia/addMedia/deleteMedia`.

### 3.2 Capture Flow

```
[phone] CAPTURE_PHOTO ──► [glasses CommandRouter] ──► CameraController.takePhoto()
                                   │
                     ghostlock off: directly ImageCapture.takePicture()   (LED lights up normally per HAL)
                     ghostlock on:  led.suppress() (cancel the lights_ctrl camera-open event +
                                   force sysfs to 0 + hold-loop) → takePicture → restore
```

Video works the same way: `START_VIDEO` → (when ghostlock is on, **suppress first, then bindVideo**) → `recorder.start`;
`STOP_VIDEO` → `recording.stop()` → restore LED → report `VIDEO_STOPPED`.

> **Flicker fix (2026-09-09)**: the source that lights the privacy LED is the `lights_ctrl` camera-open event (Binder);
> merely writing sysfs contends with the event and causes video flicker. Suppression was changed to `cancelEvent(2,2014)`
> at the Binder layer to fix it at the root, and the suppress/bind order was adjusted; see `reports/RV101_CAMERA_LED_FLASH_MECHANISM.md` §9 for details.

### 3.3 WiFi Media Channel (SoftAP + TCP 8555)

- Topology: after privilege escalation the glasses start a SoftAP (`RokidCam` / fixed `192.168.43.1`), and `WifiServer` listens on
  TCP 8555; the phone joins the AP and then connects to `192.168.43.1:8555`.
- On the phone side, joining the AP goes through the `WifiNetworkSpecifier` system dialog (Android 12+ needs
  `CHANGE_NETWORK_STATE`; a one-time confirmation, after which it reconnects automatically); on failure the UI gives manual join instructions.
- The protocol is exactly the same as Bluetooth (`ConnectionHandler` is reused), so the media transfer logic required zero changes.
- Measured: thumbnails about 1.2 s, a 5.4 MB photo downloaded in full successfully; Bluetooth fallback is about 63 KB/s (see
  `reports/wifi_transfer_test_report.md`).

## 4. Phone Side (app-phone)

- **`LinkClient` (transport-agnostic base class)**: request/response pairing (`id` ↔ `replyTo`) and binary file transfer
  (`MEDIA_META` → chunks → `TRANSFER_END`).
  - `BluetoothClient` (RFCOMM subclass), `TcpClient` (WiFi TCP subclass).
- **`WifiLink`**: records the AP information reported by the glasses, guides the phone into joining the AP (`WifiNetworkSpecifier`),
  and manages the TCP connection.
- **Channel routing**: `Remote.link()` = `tcp.connected ? tcp : client` (WiFi preferred, Bluetooth fallback);
  both `request()` and `requestFile()` go through it; `Remote.ensureConnected()` does **not** re-dial Bluetooth when WiFi is already connected
  (otherwise it would close the WiFi socket).
- **Automatically establish WiFi before media**: `Remote.ensureMediaLink()` → `ensureWifiMedia(apTries=6)` (ask the glasses to start the hotspot →
  join the AP → connect TCP); deduplicated with a `Mutex`, 60-second cooldown after failure, and Bluetooth fallback only after failure.
- **UI state**: `Remote.wifiMode`／`wifiNegotiating`／`onTransportChanged` drive a three-state display
  (WiFi connected / WiFi connecting… / Bluetooth); `LinkClient.onConnected` fires when the connection is established.
- **UI**
  - "Connection": select a paired glasses device, connect/disconnect, **transport display (WiFi connected / WiFi connecting… / Bluetooth)**,
    a "WiFi transfer" switch (persistent preference, on by default; turning it off reconnects the Bluetooth control link).
  - "Remote": ghostlock switch, photo, video start/stop, LED switch and color chips,
    an **advanced card (root mode / acquire root now / device info / glasses display 5 chips + show all/hide all)**,
    and an **operation log (two-line preview; long-press pops up a full scrollable window with select-copy/clear)**.
  - "Album": album list (create/rename/delete), media thumbnail list, download original files, delete media on the glasses.
  - "Local": view/playback of downloaded media.
- **Locales**: 14 locales (Traditional Chinese default + English/Simplified Chinese/Japanese/French/Spanish/Thai/Vietnamese/Korean/Arabic (RTL)/Portuguese/Russian/Ukrainian/German);
  `LocaleHelper` (AppCompat per-app locale) + the debug broadcast `SET_LOCALE`.
  The phone side additionally has an **in-app language selection button** (Remote tab: "follow system" + a single-choice list of 14 native language names; after selection `recreate()` takes effect immediately);
  the phone-side interface/Toasts/dialogs/operation log/status summary **all go through string resources** (**203 keys × 14 locales** (`scripts/i18n/phone_keys.tsv`; 68 was the initial number),
  translation sources in `scripts/i18n/`, see `reports/phone_i18n_localization_report.md`).
  WiFi media channel failure reasons are returned typed as `WifiSetupException(code)`, and the UI maps them to localized messages
  (no longer determined by comparing exception message strings).
- **Phone-side information architecture (2026-09-12 redesign)**: bottom navigation with four tabs: **Remote / Album / Local / Settings**.
  - **Remote page** (`fragment_remote.xml`): **does not use an outer ScrollView**; one screen is visible without scrolling —
    connection status card (`● device name` + select glasses/disconnect + `connected` + a single status line `transport · album · media · root · ghostlock`),
    capture (large photo/video buttons), quick switches (silent capture, privacy indicator LED), and an **operation log pinned at the bottom and scrollable**.
  - **Settings page** (`fragment_settings.xml` + `SettingsFragment`): connection (WiFi transfer/device info),
    capture and permissions (silent/ghostlock/AUTO root/acquire root now), privacy indicator LED color, glasses dashboard display content,
    ring control (two switches for phone side/glasses side + status + gesture learning/pair ring), appearance (light/dark), language.
  - **Ring events are structured**: the glasses side sends only a code plus arguments (`Status.ringLastKey` / `ringLastArg`, such as
    `photo_ok` + filename, `display_switched` + `conn,led`), and the phone's `ui/RingText.kt` composes the text in the **phone locale**
    (the glasses side no longer sends Chinese sentences, so the two languages can be independent).
  - The operation log is shared through `ui/LogStore`: actions on the settings page are also written into the same log as the remote page (single source).
  - **Album page**: toolbar = search (SearchView filters names live) / sort (name · item count · newest) / refresh;
    album cards have a square cover + an item-count badge at the top right + a brand gradient background; the cover **prefers a photo** (video thumbnails are often a black frame).
  - **Local page**: a `N albums downloaded · M items · X used` summary row; **long-press enters multi-select** (batch bar: N selected / select all / delete / cancel);
    batch delete removes only the phone copies; the toolbar has only refresh (search/sort do not apply).
  - **Settings page groups are collapsible cards**: tapping the title row expands/collapses (the arrow rotates 180°), and the state is stored in `settings_groups` prefs;
    by default only "Connection" and "Capture and permissions" are expanded, and the rest (privacy indicator LED/glasses display/ring control/appearance/language) are collapsed by default.
  - **Album/Local are a 3-column grid** (`GridLayoutManager(context, 3)`); card text at 13sp/11sp + shadows keep readability at small sizes.
  - **Theme (light/dark)**: `ThemeHelper` (prefs `app_theme/light_mode`, default **dark**) calls
    `AppCompatDelegate.setDefaultNightMode` before `MainActivity.onCreate` is created; it can be switched in "Settings → Appearance", and switching calls `recreate()` immediately.
    Two brand palettes: dark = `values-night/colors.xml` (dark blue background `#0B1220` + bright cyan `#22D3EE` + amber `#FFB300`);
    light = `values/colors.xml` (white background + cyan `#0E7490` + amber `#B45309`). The status bar icons switch with the mode (`windowLightStatusBar`).
  - Unified styles: `Widget.RokidPhone.Card` (14dp corner radius, 1dp stroke, no shadow, colors follow day/night mode),
    `Widget.RokidPhone.SectionTitle`, `Widget.RokidPhone.Hint`.
  - **Colors (brand colors)**: dark blue background `#0B1220`, card `#141D2F`, primary cyan `#22D3EE`, accent amber `#FFB300`,
    text `#E6EDF3` / secondary `#93A1B1`, stroke `#24304A`; consistent with the launcher icon and the glasses transparent dashboard,
    with one brand palette each for dark and light (`values/` light: background `#F4F6F9`, primary `#0E7490`, accent `#B45309`; `values-night/` dark: background `#0B1220`, primary `#22D3EE`, accent `#FFB300`).

- **Display formatting**: `util/Format` is responsible only for number/time formatting; type labels now return a string resource id instead (`typeLabelRes()`),
  and the caller obtains the localized text with `getString()`.
- **Ring control**: two paths — (A) on the phone side, `RingController` intercepts ring HID at the dispatch layer
  (key/mouse/scroll) → gesture recognition → commands the glasses over the existing BT/WiFi control channel;
  (B) on the glasses side, `RingControlManager` runs `getevent` in the background via the **root daemon** to read evdev directly,
  and after polling for increments triggers `CameraController` locally (usable in the background). BLE discovery/pairing/GATT probing
  live in `shared/ring/` for both ends to share.
  See `reports/ring_control_report.md` for details.

- **File download**: after receiving `MEDIA_META`, binary chunks are assembled into a local file and confirmed by `TRANSFER_END`;
  the same flow applies on the WiFi channel, at higher speed.

## 5. Ghostlock LPE Trigger Layer

`ShellGhostlockEngine` has implemented the orchestrator's complete trigger flow inside the App:

```
launch (ProcessBuilder runs ghostlock_rv101, with env, output redirected to app cache)
  → ready (poll .ghostlock_root.sh for "dropping to root shell")
  → install (write .ghostlock_root.sh.new then rename for atomic replacement)
  → done (poll the payload log for __GHOSTLOCK_PAYLOAD_DONE__)
  → cleanup (destroyForcibly reclaims the exploit process)
```

Parameters are centralized in `ExploitConfig` (`doneTimeoutMs` measured and tuned to 240s, covering the exploit's 100–142s core phase).

**Hard precondition on execution identity**: the exploit must run as `shell` (uid 2000); running it directly as `untrusted_app` kernel panics
(requires `perf_event_open` and other shell privileges). Hence the three trigger paths:

1. **Shizuku**: the Shizuku server is started by adb → the UserService identity = the server identity
   = `u:r:shell:s0`, equivalent to adb shell (see `research/shizuku/shizuku_internals.md` for details).
2. **Bridge**: `deploy/root_bridge.sh` runs a resident shell helper loop reading `last.cmd`.
3. **Direct**: the App runs it directly (**no longer used**, kernel panics; kept for research only).

**Engine selection is a runtime probe (since 2026-09-13, `AutoGhostlockEngine`)**: on every privilege escalation it checks
`rikka.shizuku.Shizuku.pingBinder()` (Shizuku preferred) and the helper heartbeat file
`/data/local/tmp/ghostlock_bridge/helper.alive` (updated every second, tolerating 120 seconds); when neither can be selected it reports
structured codes such as `gl_shizuku_not_running`. **There is no longer a `USE_ENGINE` compile-time switch**, so changing environments requires no APK rebuild.
⚠ The probe **must not use `ps`**: Android apps cannot see other processes (measured: the helper was running yet it returned false).
The DONE marker path for the payload is determined by `engine.doneLog()`, so `LedSuppressor` does not need to be aware of the difference.

The payload content is unchanged: `LedPayloads` suppresses the LED with a marker file + a background loop, and `restore` restores it;
after a successful exploit SELinux is `u:r:kernel:s0` + Permissive, and the payload executes directly as root.

### 5.1 Resident root daemon (one privilege escalation → socket-level LED in seconds)

- The privilege-escalation payload (`LedPayloads.startRootDaemon`) runs as root: `echo 0 > /sys/fs/selinux/enforce`,
  confirms there is no surviving socket in `/proc/net/unix`, then `setsid /data/local/tmp/root_daemon &`, and waits for the socket to be ready;
  at the same time it writes the pid file, installs a **keep-alive loop**, and sets the `oom_score_adj` of both the daemon and the keep-alive to `-1000` (P0).
- `root_daemon` (`deploy/root_daemon.c`) listens on `/data/local/tmp/root_daemon.sock` (0777);
  one connection = one command: run with `sh -c`, write stdout back. Fork-per-connection + temporary-file output.
- `RootDaemonClient` (`LocalSocket`) executes commands; it **never throws** (after a reboot a stale socket file gives
  `Connection refused`, and an empty string is always returned to avoid an App crash).
- Benefit: LED commands in the millisecond range (39–48 ms measured), 0 LPE re-runs.
- Note: both `root_daemon` and the helper are memory-resident and **disappear after a reboot**, requiring a new privilege escalation (arm via adb → `deploy/root_bridge.sh`
  or `deploy/start_shizuku.sh`). After a reboot the phone side shows "root: expired, needs re-arming" together with the script to run (P0-b).
- **Liveness hardening (P0)**: on the root side, `root_daemon_keepalive.sh` (one round every 20 seconds; the daemon is automatically recreated if it crashes; measured 9–18 seconds to recover)
  plus, on the App side, `RootDaemonKeeper` (one round every 30 seconds; on failure it clears the stale socket, judges triggerability, and automatically recreates; 5-minute cooldown).
  The two are complementary: while root is still alive the keep-alive handles it (no adb needed, no exploit re-run); when root is entirely gone the keeper handles it or reports that intervention is needed.

## 6. Threading / Concurrency Model

- Glasses side: the IO dispatcher runs BT/WiFi accept and socket reads/writes; `CameraController` calls back on the main executor;
  root payloads use `ProcessBuilder`/Shizuku UserService + coroutines to wait; display configuration change notifications are on the main thread.
- Phone side: `LinkClient` runs on the IO dispatcher; the UI uses coroutines + callbacks; download progress is reported.

## 7. Security and Compliance

- Bluetooth RFCOMM is plaintext; the WiFi channel is a WPA2 SoftAP + plaintext TCP (research use, no application-layer encryption).
  ⚠ The WiFi channel carries **control and media** (see §1), and `WifiServer` binds all interfaces, `accept()`s any source, `CommandRouter` has no token, and the AP password is hard-coded in the APK (`Protocol.AP_PASS`) — see FACTS §7 P10 for details.
- The glasses side **does not verify identity** for connections: research use assumes paired and trusted. Production use requires adding application-layer pairing/authorization.
- Debug broadcasts (`DEBUG`/`SET_LOCALE`/`TOGGLE_PANEL` etc.) are conveniences for the research period and should be removed or permission-gated in a production version.
- The ghostlock switch and covert capture are for authorized research only; no capability to evade or distribute against third-party devices is implemented.
- Original files and thumbnails are transferred as binary chunks, avoiding the 33% bloat of base64.

## 8. Version Choices

- Kotlin 1.9.24, AGP 8.5.2, Gradle 8.7; `kotlin.compiler.execution.strategy=in-process` (the local WSL has no JDK daemon permission)
- compileSdk/targetSdk 34, minSdk 26
- Shizuku API/Provider `dev.rikka.shizuku:api|provider:13.1.5`
- CameraX 1.3.4, Coroutines 1.8.1, AndroidX core-ktx/appcompat/material/recyclerview/constraintlayout
- Serialization: org.json (built into Android, zero extra plugins, lowering build risk)
- Icons: adaptive icon (vector + monochrome), see `android/branding/README.md`

## Privilege-Escalation Operations Layer (added 2026-09-13)

| Component | Role |
|------|------|
| `ghost/RootDaemonKeeper` | root daemon keeper (30 seconds): stale socket cleanup, probe-based triggerability judgment, automatic rebuild (5-minute cooldown), status code reporting |
| `ghost/AutoGhostlockEngine` | Runtime engine probing and delegation (Shizuku ↔ bridge), including failover retry on the other path |
| `GhostlockCodes.ENGINE_*` | Engine-selection reason codes (structured; displayed by the phone in its own locale) |
| `shared/RootStates`, `shared/RootMetrics` | Root state codes and privilege-escalation metrics (reported with `STATUS`) |
| `app-phone/ui/RootText` | Status/hint/metrics → phone-locale text |
| `/data/local/tmp/ghostlock_op.lease` | **Cross-tool shared lease** (App keeper/`deploy/lease.sh`/ZeroLightRokid Desktop): only the lease holder may touch the exploit, preventing simultaneous modification of `.ghostlock_root.sh` |
| payload `root_daemon_keepalive.sh` | Root-side liveness loop (daemon automatically recreated on crash) + `oom_score_adj=-1000`; installed by the App payload or `deploy/install_root_keepalive.sh` |
| `ghostlock_bridge/helper.alive` | Helper heartbeat every second (the App cannot use `ps` to see other processes, so it reads this file to judge whether the trigger pipeline is resident) |
