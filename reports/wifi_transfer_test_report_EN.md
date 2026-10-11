# WiFi Transfer Feature + Blank-Screen Fix: On-Device Test Report

> 中文版：[wifi_transfer_test_report.md](wifi_transfer_test_report.md)

> ⚠ **Historical document (not current state)**: this write-up records the situation at the time; the project has changed substantially since (root acquired, engine switched to runtime probing, phone UI reworked, i18n 193 keys).
> For the current state see `README.md`, `MANIFEST.md`, `docs/deploy.md`.


> Date: 2026-09-09 · Devices: Rokid RG-glasses (RV101, fw 1.22.009, root acquired) + Redmi <PHONE_SERIAL>
> Test method: WSL2 → Windows adb.exe; glasses root commands go through the root_daemon socket

## 0. Overview

| # | Test item | Result |
|---|--------|------|
| 1 | **Fix for the blank screen when the phone app opens** (shownTag initialization bug) | ✅ PASS (fully rendered on first open; proven by a UI dump) |
| 2 | Glasses WiFi TCP server (:8555, reusing ConnectionHandler/CommandRouter) | ✅ PASS |
| 3 | Glasses SoftAP (triggered by root with `cmd wifi start-softap`, RokidCam/192.168.43.1) | ✅ PASS (including the status-detection fix) |
| 4 | Protocol extensions (ENSURE_WIFI_AP / WIFI_AP_STATUS / DEVICE_INFO.wifiAp) | ✅ PASS (returned in an actual BT test) |
| 5 | **WiFi media transfer E2E** (TcpClient → thumbnail → full photo) | ✅ PASS (measured over a direct LAN connection; thumbnail in 1.2 s, 5.4 MB photo completed) |
| 6 | **Automatic Bluetooth fallback** (media goes over RFCOMM after WiFi drops) | ✅ PASS (5.4 MB in about 85 s ≈ 63 KB/s, as expected) |
| 7 | Phone joining the AP (WifiNetworkSpecifier system dialog) | ⚠️ Reached the "system dialog awaiting confirmation" step (Android requires a user tap; cannot be automated; on timeout it correctly returns manual-join instructions) |
| 8 | Lint + build (both modules) | ✅ PASS (all green) |

## 1. Blank-Screen Bug (fixed)
- Root cause: `MainActivity.shownTag` is initialized to `"remote"`, which short-circuits against `show()`'s
  `if (tag == shownTag) return` → the first `show("remote")` in `onCreate` adds no Fragment at all.
- Fix: initialize it to `""` (with a comment).
- Validation: on first open, `uiautomator dump` shows the connection card /「傳輸：藍牙」/「啟用 WiFi 傳輸」/ capture card all rendered.

## 2. Implementation Architecture
- Glasses: `net/WifiServer.kt` (TCP 8555 → ConnectionHandler, same protocol as Bluetooth),
  `net/WifiHotspot.kt` (root runs `cmd wifi start-softap` through root_daemon, dispatched in the background;
  AP state detected with `NetworkInterface.getByName("wlan2")`); CaptureService starts WifiServer and
  opens the hotspot automatically after a successful AUTO escalation; CommandRouter gains `ENSURE_WIFI_AP`
  handling and the `DEVICE_INFO.wifiAp` field.
- Phone: `net/LinkClient.kt` (a transport-agnostic base class extracted from BluetoothClient: request/response +
  binary file transfer), `BluetoothClient` (RFCOMM subclass), `net/TcpClient.kt` (TCP subclass),
  `net/WifiLink.kt` (WifiNetworkSpecifier AP join + SSID detection), `Remote.mediaClient()`
  (WiFi first, Bluetooth fallback), a new transfer status row plus an「啟用 WiFi 傳輸」button in RemoteFragment,
  and `WIFI_CONNECT`/`WIFI_DISCONNECT` in DebugReceiver.
- shared: `Protocol.kt` (WIFI_PORT=8555 / AP_SSID=RokidCam / AP_PASS / AP_IP),
  `WifiApInfo.kt`, two new MsgType values.

## 3. Problems Found and Fixed During Testing (5)
1. The shell pipeline `awk '{print \\$2}'` in `WifiHotspot.status()` had an escaping bug (awk received `\$2`
   and raised a syntax error) → replaced with a pure-Java `NetworkInterface` approach with zero shell dependency.
2. `cmd wifi start-softap` could block the root socket for a long time while the wifi service transitions →
   changed to **background dispatch** plus a 30 s timeout in `RootDaemonClient.exec`.
3. AP startup time varies (5–90 seconds) → the glasses side "reports as soon as it triggers", and the phone side
   **polls ENSURE_WIFI_AP until `wifiAp.up`**.
4. WifiNetworkSpecifier requires the `CHANGE_NETWORK_STATE` permission (Android 12+) → added to the Manifest
   (COARSE_LOCATION as well, to satisfy lint).
5. `suspendCancellableCoroutine` registered the cancellation handler repeatedly and threw an exception → merged
   into a single handler.

## 4. Known Limitations
- This device's firmware makes **STA and SoftAP mutually exclusive**: turning on the hotspot kicks out the
  existing WiFi connection (STA recovers automatically once the hotspot is turned off).
- Joining the AP needs **one system-dialog confirmation** (an Android security design that no app can bypass);
  if it is not confirmed the app gives instructions to "manually join WiFi RokidCam with password rokid1234";
  after joining, you just press the button again.
- The hotspot shares root's lifetime (after a reboot you must re-escalate, and the hotspot is reopened automatically).

## 5. Deliverables
- Code: app-glasses (WifiServer/WifiHotspot/CaptureService/CommandRouter/AppGraph/
  RootDaemonClient), app-phone (LinkClient/TcpClient/WifiLink/Remote/RemoteFragment/
  DebugReceiver/MainActivity bug fix/Manifest/layout/strings), shared (Protocol/WifiApInfo)
- Docs: `docs/protocol.md` (dual-channel description + new messages), `docs/deploy.md` §9.4 (using the WiFi media channel)
- APKs: `app-glasses-debug.apk`, `app-phone-debug.apk` (both already installed on the device)
