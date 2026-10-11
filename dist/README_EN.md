# dist/ — Installable APKs

> 中文版：[README.md](README.md)

| File | Version | Description |
|------|------|------|
| `app-glasses-v1.0-safe.6.apk` | `1.0-safe.6` (code 8) | **glasses side · current version**. Adds **development mode** on top of `1.0-safe.5`: `net/WifiJoin` (A: `WifiNetworkSuggestion`/B: `cmd wifi connect-network`) + `net/DevModeController` (state machine `OFF→JOINING→ACTIVE→REVERTING→OFF`, automatic shutdown on idle timeout, fallback on failure with SoftAP restoration) + `DebugReceiver` commands `DEV_MODE_START/STOP/STATUS`, `WIFI_SET_CRED`. Sensitive commands carry an **origin guard** (allowing only this App / adb shell uid 2000 / same-signature permission) and declare `com.rokid.rc.glasses.permission.DEV_MODE` (signature) in the manifest. **Measured on device**: path B successfully made the glasses join `LouisaCoffee` (`wlan0=10.0.0.120/24`) → PC `adb connect` obtained `uid=2000(shell)`; path A failed on the headless glasses (the system notification cannot be tapped). See [`docs/wireless-adb-solution-design.md`](../docs/wireless-adb-solution-design.md) §4.1 |
| `app-glasses-v1.0-safe.5.apk` | `1.0-safe.5` (code 7) | Adds **`AdbWifiService`** (a relay from TCP:5555 → `/dev/socket/adbd`) and the `DebugReceiver` commands `ADB_WIFI_START/STOP/STATUS`. ⚠ **Measured conclusion: unusable without root** — `/dev/socket/adbd` is `0660 system:system`, and a third-party App (uid 10064) calling `connect()` gets **EACCES(errno 13)**; what blocks it is **DAC, not SELinux** (no `avc: denied` at all); the on-device log is `E AdbWifi: adbd socket 不可連：IOException: Permission denied`. It is therefore positioned as a **research build (usable with root)**. |
| `app-glasses-v1.0-safe.5.apk` | `1.0-safe.5` (code 7) | **glasses side · current version**. Adds **`AdbWifiService`** (a relay from TCP:5555 → `/dev/socket/adbd`) and the `DebugReceiver` commands `ADB_WIFI_START/STOP/STATUS` on top of `1.0-safe.4`. ⚠ **Measured conclusion: unusable without root** — `/dev/socket/adbd` is `0660 system:system`, and a third-party App (uid 10064) calling `connect()` gets **EACCES(errno 13)**; what blocks it is **DAC, not SELinux** (no `avc: denied` at all); the on-device log is `E AdbWifi: adbd socket 不可連：IOException: Permission denied`. It is therefore positioned as a **research build (usable with root)**; generic wireless adb follows the "one-time cable bootstrap + adbd native TCP" route, see [`docs/wireless-adb-solution-design.md`](../docs/wireless-adb-solution-design.md) |
| `app-glasses-v1.0-safe.4.apk` | `1.0-safe.4` (code 6) | Fixes a wireless adb detection bug: `enableWifiAdb()` originally grepped only the IPv4 `/proc/net/tcp` (`:15B3`), but on this machine adbd binds to **IPv6 `[::]:5555`** (`/proc/net/tcp6`), so it misjudged "actually bound" as `adb-over-tcp not supported on this build`. It now queries both tables and the ACK reports `boundOn` (`ipv4`/`ipv6`). |
| `app-glasses-v1.0-safe.3.apk` | `1.0-safe.3` (code 5) | On top of `1.0-safe.2`: the `albums` array is now also fault-tolerant per entry (previously one bad album would wipe out both album and media), the index is **reverse-corrected** (entries for files that no longer exist on disk are removed automatically, leaving no ghost items), and `getThumbnail()` no longer returns a stale cached thumbnail for a source file that has disappeared. |
| `app-glasses-v1.0-safe.2.apk` | `1.0-safe.2` (code 4) | Fixes **media index loss**: the `media` array is fault-tolerant per entry, self-repairs from disk files after loading (measured: recovered 6 vanished entries), and `persist()` writes atomically. |
| `app-glasses-v1.0-safe.apk` | `1.0-safe` (code 3) | Safe-arming build (first version): boot-loop circuit breaker / per-boot retry cap + exponential backoff / auto-arm only while charging / post-boot delay, plus "automatic fallback when the ring device path fails". |
| `app-glasses-v1.0-original.apk` | `1.0` (code 1) | **glasses side · original version**, the behavior before the safety policy was applied, for A/B comparison. |
| `app-phone-v1.3.apk` | `1.3` (code 4) | **phone side · current version**. On top of `1.2`: centralized registration in `Remote.onPush` (**fixing a regression where `PROGRESS`/LPE progress pushes were dropped while WiFi was the current channel**), `LinkClient.disconnect()` now notifies the UI (pressing "disconnect" updates the screen immediately), `lastAutoWifiFailAt` is marked `@Volatile`, the dead code `mediaClient()` is removed, and `request()` actually goes through `Remote.link()`. |
| `app-phone-v1.2.apk` | `1.2` (code 3) | Fixes **media always going over Bluetooth**: ordinary commands and media both prefer WiFi, media operations **automatically** establish a WiFi channel, the settings-page toggle becomes a persisted preference (on by default), the transfer indicator updates in real time, and `.part` remnants are cleaned up. Measured: the same 10.6-second video went from "Bluetooth timeout failure after 324 seconds" to "WiFi 3.0 seconds". |
| `app-phone-v1.1.apk` | `1.1` (code 2) | Older phone-side version (kept for comparison). Includes `DUMP_STATUS`, records the real reason when photo/video capture fails, and turns `camera_not_ready` into an actionable instruction. |
| `usbdebug-hider-v1.1.apk` | `1.1` (code 2) | **USB debugging anti-detection module (LSPosed/Vector, self-built)**. Hides `adb_enabled`/`adb_wifi_enabled`/`development_settings_enabled` and `sys.usb.*`/`init.svc.adbd` from scoped third-party Apps; two layers, Java plus native PLT/GOT, with adb itself unaffected. **v1.1 fixes the bug where `dlopen` interception broke the App linker namespace** (switched to bionic `__loader_*` + caller address; see report §3.6 for details). ⚠ Requires **NeoZygisk v2.4-289 + Vector v2.2-3080** installed first (`tools/framework/`). For installation and verification see [`reports/usbdebug_antidetect_module_2026-09-30.md`](../reports/usbdebug_antidetect_module_2026-09-30.md) |
| `debugdetectprobe-v1.0.apk` | `1.0` (code 1) | **Control-group detector**: actually reads through 4 kinds of USB-debugging detection paths (Settings getter / direct cursor read / Java property / native `__system_property_get`) and outputs the fixed format `CHECK <向量> = <值>`, used to diff before and after hiding. |

> **Actually installed right now**: glasses `1.0-safe.6` (2026-09-20, including the wireless adb detection fix + AdbWifiService relay + development mode), phone `1.3`.
> Older versions are all kept for A/B comparison.

## Installation

```bash
ADB="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe"
# glasses
"$ADB" -s <DEVICE_SERIAL> install -r dist/app-glasses-v1.0-safe.6.apk
# phone
"$ADB" -s <PHONE_SERIAL> install -r dist/app-phone-v1.3.apk
```

## USB Debugging Anti-Detection Module (phone / any APatch·KernelSU device)

`usbdebug-hider` is an **LSPosed/Vector module** (not an ordinary App); it only works once the framework is in place:

```bash
# 1) Framework (reboot once after each): NeoZygisk → Vector
"$ADB" -s <serial> push tools/framework/NeoZygisk-v2.4-289-release.zip /data/local/tmp/
"$ADB" -s <serial> shell "su -c '/data/adb/apd module install /data/local/tmp/NeoZygisk-v2.4-289-release.zip'"
"$ADB" -s <serial> reboot
"$ADB" -s <serial> push tools/framework/Vector-v2.2-3080-Release.zip /data/local/tmp/
"$ADB" -s <serial> shell "su -c '/data/adb/apd module install /data/local/tmp/Vector-v2.2-3080-Release.zip'"
"$ADB" -s <serial> reboot

# 2) The module itself + scope (no reboot needed for this part)
./deploy/usbdebug_hider_ctl.sh install
./deploy/usbdebug_hider_ctl.sh scope-set com.example.bank   # or scope-all
./deploy/usbdebug_hider_ctl.sh verify
```

The two glasses APKs share the **same applicationId** and can overwrite each other
directly; use
`adb -s <DEVICE_SERIAL> shell dumpsys package com.rokid.rc.glasses | grep versionName`
to confirm which version is currently installed.

## Rebuilding

```bash
cd android
./gradlew :app-glasses:assembleDebug                      # safe (default)
./gradlew :app-glasses:assembleDebug -PsafeArming=false   # original-behavior version
./gradlew :app-phone:assembleDebug
```
