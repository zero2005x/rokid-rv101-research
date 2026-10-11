# Rokid Dual-Android Remote Capture System

> 中文版：[README.md](README.md)

A remote capture / album management system for our own Rokid glasses plus an Android phone, and at the same time a root security research project on the RV101 glasses.

> **This repo is a monorepo**: the main body is the `rokid_root` research project, while the Windows desktop tool lives in [`desktop/`](desktop/README.md).
>
> Release notes (2026-10-11):
> - [`EXCLUDED.md`](EXCLUDED.md) — files not placed under version control and the reasons (including SHA-256)
> - [`REDACTION_LOG.md`](REDACTION_LOG.md) — redaction record for personal data and leaked credentials (before/after hashes)
> - [`GITHUB_PUBLISH_LOG.md`](GITHUB_PUBLISH_LOG.md) — acceptance record for the GitHub publication
> - [`MANIFEST.md`](MANIFEST.md) — original workspace layout inventory and classification principles
>
> The complete workspace (including the toolchain, APK targets, decompilation output, build caches and the video production area, roughly 17 GB) is still kept locally under `~/rokid_root/`.

> **English-edition convention**: every document in this repo has an English edition named
> `NAME_EN.md`, stored beside the Chinese original, and the two link to each other just
> below their titles. Chinese comments in source code were instead **rewritten in place**
> in English (no separate file). So: for any Chinese document, append `_EN` to the
> filename to get its English edition.

- **Glasses-side app**: CameraX photo/video capture, album index, `ghostlock_rv101` (CVE-2026-43499) LPE privilege escalation,
  privacy LED suppression (ghostlock), **silent capture (suppressing the shutter/video camera tone)**, **fully transparent information dashboard**, WiFi media TCP server,
  **ring control (root reads evdev directly, can take photos/record video in the background)**,
  **privilege-escalation operations layer**: runtime engine probing `AutoGhostlockEngine`, root daemon keep-alive watchdog `RootDaemonKeeper` + root-side keep-alive loop, privilege-escalation metrics; after acquiring root, run `deploy/install_root_keepalive.sh` so the daemon lasts the whole session.
- **Phone-side app**: branded dual theme (dark navy background / light white background, switchable), four tabs (Remote / Album / Local / Settings;
  the Settings page groups are collapsible, the album uses 3 columns;
  the Remote page completes capture on a single screen and manages settings centrally), album search/sorting, local storage summary and batch deletion,
  Bluetooth remote control (camera / LED / root / ghostlock), album management and download,
  **WiFi media channel (automatic fallback to Bluetooth)**, remote configuration of what the glasses display, **root status and privilege-escalation metrics display (including "which script to run" guidance)**, 14-locale interface,
  **in-app language selection button (switches instantly, and localizes the operation log along with it)**, **ring control card (gesture interception + gesture learning)**.
- **Transport**: **both control commands and media prefer WiFi** (glasses SoftAP `192.168.43.1:8555`, the same TCP connection,
  the same `CommandRouter`); before a media operation it **automatically** establishes the WiFi channel (ask the glasses to enable the hotspot → join the AP → connect TCP),
  and only falls back to Bluetooth RFCOMM if that fails. Reason: on the glasses side, `BluetoothServer` and `WifiServer` both close the previous connection when they accept,
  so the two channels are **mutually exclusive** — if ordinary commands were switched to Bluetooth, they would kick out the WiFi socket and media would silently fall back to Bluetooth (measured: a 10.6-second video
  takes 3.8 s over WiFi vs. a 324 s timeout over Bluetooth). "WiFi transport" on the Settings page is a persisted preference (on by default).

> For use in security research and penetration testing on **owned/authorized devices** only.

## Document Index

| Document | Contents |
|------|------|
| [`MANIFEST.md`](MANIFEST.md) | **Workspace layout inventory**: role of each directory, classification principles, items pending cleanup |
| [`dist/README.md`](dist/README.md) | **Installable APK list**: version comparison (glasses safe/safe.2/original, phone), install and rebuild commands |
| [`docs/deploy.md`](docs/deploy.md) | Build, deployment, Shizuku mode, WiFi media channel, localization, icons, wireless adb conclusions (§9) |
| [`docs/wireless-adb-solution-design.md`](docs/wireless-adb-solution-design.md) | **General wireless ADB solution design** (plug in the cable once → wireless forever; topologies T1/T2/T3, bootstrap, PC tooling; includes the DAC proof that "a pure APK relay is not viable") |
| [`docs/architecture.md`](docs/architecture.md) | System architecture (dual channel, trigger engine and **runtime probing**, dashboard, UI/locales, **privilege-escalation operations layer**) |
| [`docs/protocol.md`](docs/protocol.md) | Communication protocol (message master table, frame format, binary transport) |
| [`docs/device-debug-notes.md`](docs/device-debug-notes.md) | On-device debugging notes (historical) |
| [`docs/root-from-zero-guide.md`](docs/root-from-zero-guide.md) | Beginner tutorial: acquiring root from zero (bridge route) |
| [`docs/shizuku-feasibility-report.md`](docs/shizuku-feasibility-report.md) | Shizuku integration feasibility (validated on device) |
| [`reports/shizuku_integration_test_report.md`](reports/shizuku_integration_test_report.md) | Shizuku integration on-device test report |
| [`reports/wifi_transfer_test_report.md`](reports/wifi_transfer_test_report.md) | WiFi transport + blank-screen fix on-device test report |
| [`reports/RV101_CAMERA_LED_FLASH_MECHANISM.md`](reports/RV101_CAMERA_LED_FLASH_MECHANISM.md) | Privacy LED mechanism research + permanent fix for video flicker (§9) |
| [`reports/silent_capture_mechanism.md`](reports/silent_capture_mechanism.md) | Mechanism and validation for suppressing the photo/video camera tone (silent capture) |
| [`reports/phone_i18n_localization_report.md`](reports/phone_i18n_localization_report.md) | Full localization of the phone side into 14 locales + in-app language selection button (validated on device) |
| [`reports/ring_control_report.md`](reports/ring_control_report.md) | Ring control: ring identification, path A/B implementation, root reading evdev directly, on-device validation |
| [`reports/hardcode_audit.md`](reports/hardcode_audit.md) | Project-wide hard-coded value audit (strings / paths / layout / scripts) and fix record |
| [`reports/FACTS.md`](reports/FACTS.md) | **Single authoritative fact table**: device generation / kernel config / CVE status / GhostLock offsets / product structure (this prevails when there is a conflict) |
| [`reports/index_resilience_verification_2026-09-16.md`](reports/index_resilience_verification_2026-09-16.md) | **On-device catch-up validation of bidirectional media index self-repair / transport-channel push** (A–E all pass; includes a root-free adb tunnel validation method) |
| [`reports/wireless_adb_feasibility_2026-09-20.md`](reports/wireless_adb_feasibility_2026-09-20.md) | **Wireless adb (adb-over-TCP) feasibility measured: viable** — adbd binds to IPv6 `[::]:5555`, and after the PC joins the SoftAP, `adb connect 192.168.43.1:5555` succeeds; this **overturns** the earlier conclusion that "this build does not support it" (a bug where the detection code only grepped IPv4) |
| [`reports/docs_correctness_audit_2026-09-13.md`](reports/docs_correctness_audit_2026-09-13.md) | **Documentation correctness audit**: sharded review of 97 reports + 15 docs, 17 sets of cross-file contradictions, missing artifacts and fix record |
| [`reports/usbdebug_antidetect_module_2026-09-30.md`](reports/usbdebug_antidetect_module_2026-09-30.md) | **Phone-side USB debugging anti-detection (self-built LSPosed/Vector module)**: `android/usbdebug-hider/` hides at two layers, the Java layer plus native PLT/GOT, so that a designated app reads "USB debugging / developer options = off" while adb still works; includes APatch framework installation (NeoZygisk v2.4 + Vector v2.2), full adb automation (Vector CLI / mini_sqlite3), A/B comparison and hot-reload validation, and known limitations |
| [`reports/su_escalation_roi_assessment.md`](reports/su_escalation_roi_assessment.md) | Privilege-escalation code return-on-investment assessment: 84–112 s measured, statistics over 143 historical runs, failure classification and P0–P3 recommendations |
| [`reports/zero_light_desktop_test_report.md`](reports/zero_light_desktop_test_report.md) | **Windows desktop tool (ZeroLightRokid Desktop v2.0) on-device test report**: complete root flow + reboot matrix + GUI/14-locale acceptance |
| [`reports/zero_light_desktop_ui/README.md`](reports/zero_light_desktop_ui/README.md) | Desktop GUI acceptance screenshots (including Arabic RTL and per-locale fonts) |
| [`android/branding/README.md`](android/branding/README.md) | Brand icon design (SVG / adaptive icon) |
| [`RV101_Disclosure/00_README.md`](RV101_Disclosure/00_README.md) | Coordinated vulnerability disclosure package |

## Directory Structure

```
rokid_root/
├── android/              # Gradle multi-module project
│   ├── shared/           # Shared protocol module (FrameCodec / Protocol / models)
│   ├── app-glasses/      # Glasses-side app
│   ├── app-phone/        # Phone-side app
│   └── branding/         # Branding SVG and icon documentation
├── desktop/              # ★ Windows native desktop tool (ZeroLightRokid Desktop v3.0)
│   ├── src/              # Pure Win32 API (C language), no runtime dependencies
│   ├── assets/           # Runtime essentials (exploit / root_daemon / silent camera tone)
│   ├── tools/            # i18n generator and static checks (14 locales)
│   └── docs/             # Loop protection, i18n, extended path tests, macOS feasibility
├── deploy/               # ADB deployment and test scripts (root_bridge / start_shizuku / test_* / lock_ota ...)
├── docs/                 # Architecture, protocol, deployment, research reports
├── reports/              # Vulnerability research, on-device tests, debugging records
├── research/             # GhostLock / Shizuku reverse-engineering reference material
├── scripts/              # exploit development-time tools + i18n workflow (scripts/i18n/) + hard-coded value audit (audit_hardcode.py)
├── dist/                 # Installable APKs (glasses safe/safe.2/original, phone) + version comparison in dist/README.md
├── binaries/             # Non-rebuildable binaries rescued from the device (binaries/device_rescue/)
├── source_code/          # 250 probe/exploit source files (adsprpc/binder/kgsl/ashmem etc.)
├── frida/                # Frida hook scripts (OTA/intent exploration)
├── analysis/             # Packet captures, jadx decompilation output (not version-controlled, see EXCLUDED.md)
├── apks/ tools/          # APK targets, JDK/NDK and other tools (not version-controlled, rebuildable)
├── build-cache/ .zig-cache/  # Build/probe scratch; rebuildable (not version-controlled)
├── RV101_Disclosure/     # Coordinated vulnerability disclosure package (including frozen evidence)
├── HITCON_ZeroDay_RV101/ # Conference agenda material
├── EXCLUDED.md           # List of files not placed under version control and the reasons (including SHA-256)
└── REDACTION_LOG.md      # Personal data / credential redaction record (before/after hashes)
```

> For file-ownership principles see [`MANIFEST.md`](MANIFEST.md): **formal evidence → `reports/`**, **scratch/rebuildable → `build-cache/`**;
> `reports/media*/` is the canonical location for screenshots and raw measurement files.

## Quick Start

1. Build: `docs/deploy.md` §1 (JDK 17 required locally; a Temurin path is included)
2. Deployment and privilege escalation: `docs/deploy.md` §9 (arm once per boot over adb: `deploy/start_shizuku.sh` or `deploy/root_bridge.sh`);
   after acquiring root, it is recommended to run `deploy/install_root_keepalive.sh` to install the daemon keep-alive watchdog (see §Privilege-Escalation Operations P0–P3 in the same file)
3. WiFi media transport: `docs/deploy.md` §9.4
4. On-device validation scripts: `deploy/test_shizuku_e2e.sh` (privilege escalation), `deploy/recover_and_verify.sh` (regression after recovery)
