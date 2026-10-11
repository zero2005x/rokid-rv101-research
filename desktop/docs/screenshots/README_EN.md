# On-Device Test Screenshots

> 中文版：[README.md](README.md)

Screenshots of `zero_light_desktop.exe` running on 2026-09-11 under Windows 11
(build 26200, 2880x1620 @ 200% scaling) + a physical Rokid RV101 glasses unit
(serial number <DEVICE_SERIAL>).

| File | Content |
|---|---|
| `01_main_zh-TW.png` | Main screen (Traditional Chinese); the glasses have been detected and full telemetry is displayed |
| `02_arabic_rtl.png` | Arabic: the entire interface is mirrored (the close button moves to the left, buttons run right to left) |
| `03_ja_th_ru_ko.png` | Japanese / Thai / Russian / Korean: each fallback font chain selects Yu Gothic UI / Leelawadee UI / Segoe UI / Malgun Gothic |
| `04_zh-CN_fr_de_uk.png` | Simplified Chinese / French / German / Ukrainian: Simplified Chinese uses mainland terminology, French uses « », German uses „ " |
| `05_device_detected.png` | Device info panel: serial number / model / firmware / SPL / Android / SELinux / identity / root / Shizuku / uptime |
| `06_rooted_after_oneclick.png` | After "🚀 One-click acquire root" succeeds: the panel automatically updates to root: Running / SELinux: Permissive |

Items exercised on device: all 14 locales render correctly and can be switched
through consecutively (14 interface rebuilds without a crash, with no duplicated or
lost log entries); Refresh / Verify root / custom root command / exploit log / OTA
patch check / ② Install helper all work normally; the helper script that ② writes to
the device is 1957 bytes / 58 lines (it was 0 bytes before the stdin fix).

## Defects Caught by Running the Tests (Invisible to Static Review and Compilation)

1. **APPCRASH**: the device info panel's format string had 14 `%ls` but only 11
   arguments were supplied → pressing "Refresh" and reading the device crashed in
   `msvcrt.dll` (c0000005).
2. **The stdin pipe direction was reversed**: the parent process kept the write end
   but closed it before writing, then called `WriteFile` on the read end →
   `adb shell` saw EOF immediately, so ② Install helper wrote an empty script and
   the custom root command had no effect.
3. On a 200%-scaled screen the client area was 41 px smaller than expected → the
   status bar was cut off.
4. After an operation finished, the status bar stayed at "running…".
5. `pkill -f` killed its own adb shell (returning 137).
6. Commands with no stdout were falsely reported as failures.

## Environment Notes

At test time the device had about **1554 leftover `ghostlock_rv101` processes from
previous sessions**, driving the load average of the 8-core device to 70–102 and
causing adb to report `fork failed: Try again`. After clearing them it dropped to
load 1.44 with 731 MB of available memory, and both root and Shizuku stayed
resident. Before regular use, it is advisable to check the device load first.

## Full-Flow On-Device Test (2026-09-11/12)

### Phase 1 — Root-requiring features (tested in an existing root state)
| Feature | Result |
|---|---|
| Silent capture | pushed 3584 bytes + created 6 bind mounts; `changed=6 mounted=6` |
| Restore camera tone | 6 → 0 mounts; `changed=6 mounted=0` |
| Privacy LED suppression | `led=off hold=1`, keep-alive loop started |
| Restore privacy LED | `led=on hold=0`, keep-alive loop stopped |
| Lock OTA | `Package com.rokid.glass.ota new suspended state: true` |
| Unlock OTA | `new suspended state: false` / `new state: enabled` (re-locked after the test) |

### Phase 2 — Reboot
| Item | After reboot | Verdict |
|---|---|---|
| SELinux | **Enforcing** (previously Permissive) | ✅ root is gone (memory level) |
| root_daemon socket | **0** | ✅ matches the design |
| shizuku_server | **0** | ✅ must be re-run every boot |
| Silent capture bind mount | **0** | ✅ cleared by the reboot |
| **OTA lock** | **1 (still suspended)** | ✅ **survives a reboot — the most important security property** |
| `/data/local/tmp` files | all preserved | ✅ persistent at the file level |
| App status bar | `Status: connected, not rooted` | ✅ reflects it correctly |

### Phase 3 — Acquiring root from a clean state
| Step | Result |
|---|---|
| ① Push exploit | `ghostlock_rv101` 60272 bytes, `root_daemon` 9304 bytes |
| ② Install helper | script 1957 bytes, process resident, PID file written |
| ③ Trigger privilege escalation | **succeeded on the first attempt** (about 139 seconds), `uid=0(root)`, Permissive |
| 🚀 One-click acquire root | **succeeded in both on-device runs** (about 6–8 minutes, including the stabilization wait for uptime > 150 seconds) |

The one-click flow log shows the full mechanism: waiting for `uptime > 150`,
`Boot 1/6, trigger 1/2`, and progress going from `started` → `waiting-done` →
`Root achieved! Verifying`.

### Defects Newly Found and Fixed in This Round of On-Device Testing
1. **The dialog title displayed a literal `v%ls`**: `T(STR_APP_TITLE)` did not use
   `Tf()` to substitute the version (3 places). A static check was added: a string
   fetched with `T()` that contains a format specifier is now an error.
2. **The device panel was not updated after a successful one-click**: the panel
   still showed `root: Not running`, contradicting `root ✓` in the status bar.
   `do_refresh()` is now called automatically after success.
