# ZeroLightRokid Desktop (Windows GUI) On-Device Test Report

> 中文版：[zero_light_desktop_test_report.md](zero_light_desktop_test_report.md)

> Date: 2026-09-11 / 09-12 · Device: Rokid RG-glasses (RV101, serial <DEVICE_SERIAL>, fw 1.22.009-20260710-150201, Android 12)
> Test environment: Windows 11 build 26200 (2880×1620 @ 200% scaling), driven through WSL2 → Windows `adb.exe` / PowerShell interop
> Version under test: `ZeroLightRokidDesktop` v2.0.0 (Win32 C, single exe, 14 locales)
> Tester: main agent (automated clicking + device-side state read-back, not human visual inspection)

## 0. Overview

| # | Test item | Result |
|---|--------|------|
| 1 | Cross-compilation (x64 / x86, MinGW-w64) and reproducibility | ✅ PASS (0 warning; identical bytes from the same source) |
| 2 | Deploy to Windows and compare SHA256 with the source file by file | ✅ PASS (all 31 files identical) |
| 3 | GUI startup, 200% DPI layout, embedded manifest | ✅ PASS (after fixing a 41 px client-area shortfall) |
| 4 | 14-locale rendering (including Arabic RTL and each locale's font chain) | ✅ PASS (14 consecutive interface rebuilds without a crash, no duplicated log entries) |
| 5 | Refresh: device telemetry (serial/model/firmware/SPL/`id`/SELinux/root/Shizuku/uptime) | ✅ PASS |
| 6 | ① Push exploit (`ghostlock_rv101` + `root_daemon`) | ✅ PASS |
| 7 | ② Install helper (bridge directory + resident loop + PID file) | ✅ PASS (script 1957 bytes) |
| 8 | ③ Trigger escalation (payload → disable SELinux + root_daemon) | ✅ PASS (**attempt 1**, about 139 seconds) |
| 9 | 🚀 One-click root acquisition (6 boots × 2 triggers, uptime gate, reboot detection) | ✅ PASS (**both measurements succeeded**, about 6–8 minutes) |
| 10 | ✓ Verify root (root_daemon socket) | ✅ PASS (`uid=0(root)` / Permissive) |
| 11 | Custom root command (`id; getenforce`) | ✅ PASS |
| 12 | Check exploit log (last 60 lines of `gl_run.log`) | ✅ PASS |
| 13 | OTA patch check (official API + MD5 signature) | ✅ PASS (current version obtained; server query needs network) |
| 14 | Silent capture / restore capture sound | ✅ PASS (6 bind mounts ↔ 0) |
| 15 | Suppress privacy LED / restore privacy LED | ✅ PASS (`led=off hold=1` ↔ `led=on hold=0`) |
| 16 | Lock OTA / unlock OTA | ✅ PASS (`suspended=true` ↔ `false`) |
| 17 | Reboot device (including confirmation dialog) | ✅ PASS |
| 18 | Post-reboot state matrix (root gone / OTA lock persists) | ✅ PASS (see §5) |
| 19 | Firmware gate (1.22.009 judged "vulnerable build (escalatable)") | ✅ PASS |
| 20 | Error-reporting correctness (no false alarms, status bar not left stale) | ✅ PASS (after fixes) |

**8 defects were found and fixed during testing** (§7). Two of them were serious defects that only appeared by execution:
pressing "Refresh" caused an APPCRASH, and a reversed stdin pipe direction made the helper installation write an empty script.

## 1. Test method

The GUI cannot be driven by an ordinary automation framework (no UIA test framework was available), so the Win32 message layer was used instead:

| Action | Technique |
|---|---|
| Button click | `PostMessage(hBtn, BM_CLICK)` (**must** use PostMessage: the button handler opens a modal dialog, and `SendMessage` gets stuck in the dialog's message loop) |
| Dialog confirmation | Enumerate `#32770` windows in the same process → `GetDlgItem(dlg, IDYES=6)` → `BM_CLICK` |
| Read results | Send `WM_GETTEXT` to the log EDIT (id 2005) and the device panel EDIT (id 2003) |
| Device-side verification | Read back `/proc`, `/proc/net/unix`, `/proc/mounts`, `dumpsys`, etc. directly with `adb shell` |
| Screenshot | DPI-aware PowerShell + `CopyFromScreen` (**must call `SetProcessDpiAwarenessContext(-4)` first**, otherwise coordinates are virtualized and 2126×1524 is read as 1063×762) |

> ⚠️ Methodological point: a DPI-unaware capture tool reports a 200%-scaled window at half size,
> which at one point made it hard to decide "whether the layout is 2×". All geometric data in this report is a DPI-aware measurement.

## 2. Build and deployment

- `build_mingw.sh` cross-compilation: x64 813 KB / x86 782 KB, **0 warning** (`-Wall -Wextra`)
- Linking: `gdi32 user32 comctl32 shell32 ole32 shlwapi comdlg32 winhttp bcrypt`
- `-Wl,--no-insert-timestamp` → **reproducible build**, `SHA256SUMS.txt` can be verified repeatedly
- Embedded resources: `RT_MANIFEST` (PerMonitorV2 + v6 common controls + longPathAware), `VS_VERSION_INFO`
- Static checks: `gen_i18n.py --check` (14 locales × **228** strings = **3192** entries; the original 191/2674 were the figures at the time),
  `check_logfmt.py` (**134** multilingual formatting calls, **28** wide formats, **26** `T()` accesses; the original 149/44 were the figures at the time),
  `verify_exe.py` (confirms the **3192** translations are actually compiled into the executable; the original 2674 was the figure at the time)
- Deployed to `C:\Users\<USER>\Documents\codebase\ZeroLightRokidDesktop`, **file-by-file SHA256 identical** to the source

## 3. GUI and multilingual support

- On startup it selects zh-TW according to the system language; all 14 locales can be switched live, with **14 consecutive interface rebuilds without a crash**
- **Full Arabic interface mirroring**: the close button moves to the left, labels are right-aligned, buttons go right-to-left;
  Latin words such as `adb` / `exploit` / `root` are correctly embedded in Arabic
- **Each locale's font chain hits correctly**: ja→Yu Gothic UI, th→Leelawadee UI, ko→Malgun Gothic, ar/ru→Segoe UI
- After switching languages the **log contents have no duplicates and no losses** (validating the drain/rebuild logic)
- Screenshots in `reports/zero_light_desktop_ui/`

## 4. Phase 1 — Features that require root

| Feature | Device-side before | Device-side after | Verification method |
|---|---|---|---|
| Silent capture | `media/audio/ui` mount = 0 | **6** | `grep -c media/audio/ui /proc/mounts`; `mountinfo` confirms the source is `/data/local/tmp/silent_camera.ogg` (**bind mount, not overwriting the original file**) |
| Restore capture sound | 6 | **0** | same as above |
| Suppress privacy LED | hold file = 0 | **1** | `ls /data/local/tmp/zl_led_hold`; log `led=off hold=1` |
| Restore privacy LED | 1 | **0** | log `led=on hold=0` |
| Lock OTA | `suspended=true` | `true` | `dumpsys package com.rokid.glass.ota` |
| Unlock OTA | `true` | **`false`** | `new state: enabled` (re-locked after the test) |

> Silent capture uses a **bind mount**; the root field of `/proc/self/mountinfo` proves the source is
> `/data/local/tmp/silent_camera.ogg`, and **the original camera-tone file on `/product` was not modified**.

## 5. Phase 2 — Reboot matrix

| Item | Before reboot | After reboot | Verdict |
|---|---|---|---|
| SELinux | Permissive | **Enforcing** | ✅ root disappears (memory level, as designed) |
| `root_daemon.sock` (`/proc/net/unix`) | 1 | **0** | ✅ |
| `shizuku_server` | 1 | **0** | ✅ must be rerun on every boot |
| Silent-capture bind mount | 6 | **0** | ✅ cleared by reboot |
| **OTA lock (`suspended`)** | 1 | **1** | ✅ **persists across reboots — the most important security property** |
| `/data/local/tmp/*` files | present | **all retained** | ✅ file-level persistence |
| App status bar | `root OK` | `Status: connected, not rooted` | ✅ correctly reflected |
| Device info panel | root: Running | root: **Not running** | ✅ correctly reflected |

The App's "one-click" flow also handles reboots correctly: the measured log shows
`Wait for the system to settle (uptime 146 s, need > 150 s)`,
which is the stability wait designed to avoid the automatic restart window after a kernel panic.

## 6. Phase 3 — Acquiring root from a clean state

| Step | Observation |
|---|---|
| ① Push exploit | `ghostlock_rv101` 60272 bytes, `root_daemon` 9304 bytes |
| ② Install helper | script **1957 bytes / 58 lines**, resident process, bridge directory `drwxrwxrwx`, PID file written |
| ③ Trigger escalation | mailbox `last.cmd` → `payload.staging` (helper has picked it up) → progress `started` → `waiting-done` → **`last.out = OK`** (about 139 seconds) |
| 🚀 One-click root acquisition | `Boot 1/6, trigger 1/2` → progress `started` → `waiting-done` → `Root achieved! Verifying` (about 6–8 minutes, **both times succeeded on attempt 1**) |
| ✓ Verify root | `uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0` + `Permissive`; `last.done` contains `__GHOSTLOCK_PAYLOAD_DONE__` |

## 7. Defects found and fixed in this round

| # | Severity | Defect | Symptom | Fix |
|---|---|---|---|---|
| 1 | **critical** | The device-info panel format string had **14 `%ls` but only 11 arguments** | Pressing "Refresh" and reading the device **APPCRASHed in `msvcrt.dll` (c0000005)** | Changed the format string to 11 `%ls`; also extended the static check to cover **the printf family with literal format strings** (GCC's `-Wformat` does not take effect for MinGW's `__mingw_swprintf`, hence the miss) |
| 2 | **critical** | The stdin pipe of `run_process()` was **reversed** (the write end was kept but closed before writing, then `WriteFile` was called on the read end) | `adb shell` received EOF immediately → **② installing the helper wrote a 0-byte empty script, and custom root commands did nothing at all**, while the screen showed only "no output" | Close the parent's read end and write to the write end instead, closing it after writing to send EOF (this defect **has existed since v1.0**) |
| 3 | medium | The window was created with "window size" but the layout used "client-area coordinates" | At 200% scaling the client area was **41 px** smaller than expected and **the status bar was cut off** | Switched to a `WM_SIZE`-driven `ui_layout()`, and clamped the window to the work area |
| 4 | medium | After an operation finished the status bar **stayed at "running…"** | push/helper/trigger/silent/privacy LED, etc. did not set a terminal state themselves | Set "ready" when the worker finishes; show "cancelled" on cancellation |
| 5 | medium | `pkill -9 -f 'ghostlock_bridge_helpe[r]'` **killed its own adb shell** | adb returned **137** (128+SIGKILL) and the step was misjudged as failed. The character class does not prevent self-matching in this toybox version | Switched to a **PID file**: write `$!` at startup and `kill -9 $(cat …)` at stop |
| 6 | low | Commands with no stdout were falsely reported as "(no output, may have failed)" | Installing the helper produced 5 lines of false alarms (mkdir/chmod simply produce no output) | Switched to looking at the exit code; also fixed the issue where "with no output `out` is `NULL` rather than an empty string", which made the conditional fail |
| 7 | low | The dialog title displayed a **literal `v%ls`** | `T(STR_APP_TITLE)` only looked up the table without formatting (3 places) | Switched to `Tf(STR_APP_TITLE, ZL_VERSION)`; added a static check: using `T()` on a string containing format specifiers is an error |
| 8 | low | After a successful one-click the **device panel was not updated** | The panel still showed "root: not running", contradicting the status bar's "root ✓" | Automatically call `do_refresh()` after a successful one-click |

## 8. Pre-existing device-side problems (not caused by this tool)

Early in testing adb produced `error: fork failed: Try again` (exit 126), and investigation found about
**1554 `ghostlock_rv101` processes left over from previous sessions** on the device:

| Metric | Before cleanup | After cleanup |
|---|---|---|
| `ghostlock_rv101` process count | ~1554 | 1 |
| load average (**4 cores**; the device is 4×Cortex-A55) | **69.98 / 102.58 / 55.42** | **0.58 / 0.80 / 0.45** |
| MemFree | 338 MB / 1.8 GB | 731 MB |

Cleared with `ps -A -o PID,ARGS | grep ghostlock_rv10[1] | awk '{print $1}' | xargs -r kill -9`
(**not `pkill -f`**, for the reason in §7-5). After cleanup both root and Shizuku remained resident.

> Recommendation: the cleanup step of `deploy/*` should ensure leftover exploit processes are cleared at the end of every session;
> this tool has added `pkill -9 -f ghostlock_rv10[1]` to the helper loop, but that is relative to
> the helper's own flow, and leftovers from manual sessions still have to be cleaned separately.

## 9. Known limitations and untested items

- **The OTA patch check only compares version numbers**; it does not download or disassemble the OTA image. An authoritative verdict still requires
  `deploy/ghostlock_patch_check.py` (`payload-dumper-go` + `vmlinux-to-elf` + capstone).
- **Not tested**: the OTA image download and disassembly path, the WiFi media channel, the glasses-side App broadcast (`DebugReceiver`),
  the Shizuku engine path (③ in this session took the bridge helper route).
- **Not tested**: malicious/corrupted input, switching when multiple devices are connected at once, long (>1 hour) continuous operation.
- The exploit success rate is higher than expected (all 3 escalations in this session succeeded on attempt 1), but it is still probabilistic;
  the ~50% in the documentation is a conservative estimate.

## 10. Artifact locations

| Item | Path |
|---|---|
| Tool source and executable | `C:\Users\<USER>\Documents\codebase\ZeroLightRokidDesktop` (31 files) |
| Linux-side development copy | `~/ZeroLightRokidDesktop` |
| Screenshots | `reports/zero_light_desktop_ui/` |
| Tool documentation | the `README.md` in that directory; for i18n maintenance see `docs/i18n.md`; for the macOS port assessment see `docs/macos-port-feasibility.md` |

## 11. Correspondence (this tool ↔ existing rokid_root scripts)

| GUI button | Corresponding project script |
|---|---|
| ① Push exploit | `adb push ghostlock_rv101` + `root_daemon` |
| ② Install helper | the helper install/start section of `deploy/root_bridge.sh` |
| ③ Trigger escalation | `deploy/trigger_root_daemon.cmd` → `last.cmd` |
| 🚀 One-click root acquisition | `deploy/get_root_now.sh` (`MAX_BOOTS=6`, `TRIGGERS_PER_BOOT=2`, `ROUND_TIMEOUT_S=950`) |
| ✓ Verify root | `deploy/run_root.sh 'id; getenforce'` |
| 🔒 Lock OTA | `deploy/lock_ota.sh` |
| OTA patch check | `deploy/ota_api.py` (upper-layer version comparison) |
| Silent capture | `reports/media/silent_camera.ogg` + bind mount |
| Suppress privacy LED | the Binder + sysfs approach of `reports/RV101_CAMERA_LED_FLASH_MECHANISM.md` |
