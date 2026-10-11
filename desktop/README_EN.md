# ZeroLightRokid Desktop — Native Windows LPE Helper Tool (GUI) v3.0

> 中文版：[README.md](README.md)

> A desktop tool written in pure Win32 API (C) that packages the GhostLock
> (CVE-2026-43499) root flow for Rokid glasses (RV101) into button-driven
> operation. **No bash / Python / any runtime required** — only Windows +
> `adb.exe`.
>
> ⚠️ For security research on **your own / authorized devices** only. The exploit
> succeeds roughly 50% of the time per boot, and a failure reboots the device
> (this is normal behavior). root exists only in memory and **disappears on reboot**.

---

## What's new in v3.0

The interface was rewritten from "stacked native Win32 controls" into a
**custom-drawn branded interface**, reusing the design vocabulary of `app-phone`
(dark `#0B1220` / primary `#22D3EE` / accent `#FFB300` / 14px rounded cards).

- **Four cards**: device / main / tools / log. Only 4 real child controls exist
  (adb path, serial number, root command, log); everything else is custom-drawn.
- **Switches replace pairs of buttons**: one switch each for OTA lock, silent
  capture, privacy LED suppression, and Shizuku. **The switch position itself
  shows the device's current state**, so you no longer have to press "Query"
  first to find out.
- **A single primary action**: the only visual focus on screen is the big cyan
  button; once rooted it turns into a status panel, and "Re-acquire root" is
  demoted to a secondary button.
- **Live status**: background polling every 30 seconds (only while idle), and one
  query 1.5 seconds after launch.
- **Dark / light themes**: follows the Windows `AppsUseLightTheme` setting, can be
  toggled manually from the title bar, and is remembered in `settings.ini`.
- **Severity-colored log**: collapsed by default, showing only the last result and
  the two most recent lines; **expands automatically on failure**.
- **Manual mode**: ① ② ③ are tucked into a "Manual mode" expander; day-to-day use
  needs only the one-click button.

### Boot loop protection (added 2026-09-13)

The App-side `ArmPolicy` only protects the App's own automatic arming path, and
**the desktop build goes through its own helper, bypassing it completely**.
One "one-click acquire root" performs at most 12 attempts, each with roughly a
50% chance of a panic reboot, so a single click can cause consecutive reboots.
This version adds:

- **Cross-launch circuit breaker**: 2 consecutive cases of "reboot within
  ≤10 minutes of arming" → abort and explain why (state stored in `settings.ini`)
- **Pre-flight loop check before one-click**: ≥2 reboots in the last 10 minutes →
  warn; **warn but still allow a manual override**
- **Attempt counter display**: every attempt prints "attempt N / 12"
- **Power warning**: reminds you when the cable is not plugged in (during a panic
  reboot you can neither observe nor intervene)

It also fixes **three real defects** found during validation:

1. **Warning dialogs could land off-screen** — when the main window is larger than
   the screen, `MessageBox` centers itself on the window, so the user cannot see
   the warning yet is still stuck behind the modal. Fixed by centering on the
   screen when the window center is not on screen.
2. **Orphaned helpers kept re-running the exploit** — after the tool was
   force-killed, the helper became an orphan yet kept retrying from `last.cmd`
   (measured: 515–996 leftover processes, with the device rebooting repeatedly).
   Fixed to **kill the helper first (cut the source) → then kill the exploit →
   finally clear the trigger file**.
3. **Leftovers after privilege escalation could not be killed** — after a
   successful exploit the processes belong to uid 0, and `adb shell` (uid 2000)
   gets `EPERM` from them. Fixed so that when leftovers remain and the root
   channel is available, it **escalates to root identity** and cleans up again.

See [`docs/desktop-loop-protection.md`](docs/desktop-loop-protection.md) for details.

### Real defects fixed in v3.0

| Severity | Problem | Fix |
|---|---|---|
| **critical** | `device_state()` always took the first device from `adb devices` and **overwrote** the configured serial → while the glasses were rebooting, it would silently switch the target to a phone connected at the same time, and every subsequent reboot / exploit went to the wrong device | When a serial is specified, **only that serial is accepted**; if it is not found, report it and **refuse to perform any operation on other devices** |
| high | Wide format strings misused `%s` (which is `char*` under MinGW) → `swprintf(v,24,L"v%s",L"3.0.0")` printed only `v3` | Switched to `%ls`, and added a fourth class of static checks to `check_logfmt.py` |
| high | The background poll's "silent mode" swallowed even errors → the user could never tell why pressing a button did nothing | It now **speaks up as soon as the state changes**, and stays quiet only when the state is unchanged |
| medium | Drawing and layout computation each had their own copy of the tool-card height → hit regions did not match what was on screen | Unified the two and added a note |
| medium | The layout was not recomputed when polling changed `g_dev.rooted` → switches were drawn outside the card | `WM_INFO` now always recomputes the layout |

## What's new in v2.0

### Multilingual support (14 locales, matching the phone-side App)

The interface supports **14 languages**, exactly matching the locale list of
`rokid_root/android/app-phone`, and terminology follows that App's existing
translations to stay consistent:

| | | | |
|---|---|---|---|
| 繁體中文 `zh-TW` | 简体中文 `zh-CN` | English `en` | 日本語 `ja` |
| 한국어 `ko` | Français `fr` | Deutsch `de` | Español `es` |
| Português `pt` | Русский `ru` | Українська `uk` | ไทย `th` |
| Tiếng Việt `vi` | العربية `ar` | | |

- **Follow system**: by default it selects automatically from the Windows display
  language; you can also choose manually from the UI dropdown, and the choice is
  stored in `settings.ini`.
- **Automatic RTL mirroring for Arabic** (`WS_EX_LAYOUTRTL`), with no manual layout
  adjustments needed.
- **Automatic font selection per locale**: CJK / Thai / Arabic each have their own
  fallback font chain, falling back automatically when a font is missing.
- The translation pipeline is reproducible: `tools/i18n_master.json` →
  `tools/gen_i18n.py` → `src/i18n_strings.h`, and the generation stage verifies
  that the printf format specifiers are exactly identical across all locales. See
  [`docs/i18n.md`](docs/i18n.md).

### Aligned with the latest research (`rokid_root`)

| New feature | Why it matters |
|---|---|
| **🔒 Lock OTA** | `pm suspend com.rokid.glass.ota`. **A single OTA permanently destroys the vulnerability on this device** (fixed in 1.25.012, and A/B anti-rollback makes downgrading impossible). This is the most important security feature in this version. |
| **OTA patch check** | Queries the Rokid OTA API directly (MD5 signature) and compares the latest version against the patch threshold. |
| **Firmware gate** | Automatically recognizes `1.22.009` (vulnerable) and `1.25.012+` (patched); when patched it refuses to run and says so instead of wasting time. |
| **Start Shizuku** | Automatically installs / pre-authorizes / starts `libshizuku.so` on every boot, for use by the Shizuku engine of the glasses App. |
| **Silent capture** | Overrides the camera tone with a bind mount (requires root; lost after reboot). |
| **Suppress privacy LED** | Binder notification + sysfs keep-alive loop (requires root; lost after reboot). |
| **Reboot detection now falls back to uptime** | Compares whether the boot seconds went backwards, which is more reliable than "device offline"; and before retrying it waits for `sys.boot_completed=1` with uptime > 150 seconds, avoiding the exploit auto-trigger / OOM window. |
| **Boot health gate** | Waits one more round when the thread count is > 2500, avoiding a trigger while the system is busy. |
| **Atomic `last.cmd` write** | Writes `.tmp` first and then `mv`s it, so the helper never reads a half-written payload. |
| **`pkill` character class** | Uses `ghostlock_rv10[1]` instead of `ghostlock_rv101` to avoid pkill matching itself. |
| **Parameters follow the official `get_root_now.sh`** | 6 boots × 2 trigger rounds each, 950 seconds per round. |

### Engineering quality

- **Fixed the v1.0 pipe deadlock**: v1.0 wrote all of stdin before reading stdout,
  so if the child process produced more output than the pipe buffer the two would
  wait on each other. v2.0 uses a separate reader thread.
- **Cancellable**: the one-click flow can run for up to 80 minutes, and v2.0's
  "Cancel" button aborts it at any time.
- **Complete telemetry panel**: serial / model / firmware / security patch level /
  Android / SELinux / identity / root / Shizuku / uptime / firmware status, all
  retrieved with a single `adb shell`.
- **Multi-device safety**: every adb call carries `-s <serial>`; correctly
  distinguishes `device` / `unauthorized` / `offline`.
- **Per-Monitor V2 high DPI** + v6 common controls (`src/app.manifest`), so 4K
  screens are not blurry.
- **The log can be saved / copied / cleared**, with a progress bar as well.
- Compiles with zero warnings (`-Wall -Wextra`), and builds for both x64 and x86.

### Code review fixes (before v2.0 was finalized)

After v2.0 was completed, an independent adversarial code review was carried out
(since there was no Windows / Wine available for real testing, static review was
the only means of verification). The following real defects were found and fixed:

| Severity | Problem | Fix |
|---|---|---|
| **critical** | `g_busy` was never reset after the worker finished → **the whole program could only perform one operation, and the window could never be closed** (neither X nor Alt+F4 worked) | `set_busy_ui()` became the single reset point; closing the window while busy now cancels first and really closes once cleanup finishes |
| **critical** | `WM_BUSY(1)` was never sent → while busy, "save / copy / clear log" could still be clicked, and they read `g_logbuf` while the worker was `realloc`ing it (use-after-free) | `start_op()` now sends `WM_BUSY(1)` itself; and `g_logbuf` is now **maintained only by the UI thread** |
| **high** | `do_custom()` read window controls from the worker thread, with an uninitialized buffer → if the control had already been destroyed, it **sent stack garbage to the device as the root command** | The UI thread now fetches the command first and then hands it to the worker |
| **high** | `run_process()` could return and free the buffer while the reader thread was still alive (dangling pointer + double free) | After closing the read end, it now **waits indefinitely** for the thread to actually finish before returning |
| medium | The translated confirmation-dialog strings were unused, so silent capture / privacy LED suppression had **no second confirmation** | Wired up `DLG_SILENT_CONFIRM` / `DLG_LED_CONFIRM` |
| medium | An "unknown" firmware neither blocked nor asked, and the exploit simply ran | An unknown version now requires confirmation (`DLG_FW_UNKNOWN_*`); a patched one is blocked outright |
| medium | The layout was computed in "client-area" coordinates while the window was created with "window" dimensions → **the status bar was cut off and right-side controls overflowed** | Converted with `AdjustWindowRectEx()` |
| medium | The Shizuku APK-selection path was dead code (no control had that ID) | Now pressing "Start Shizuku" opens the file picker if no APK has been specified yet |
| low | WinHTTP headers did not end with CRLF, an empty signature was still sent when MD5 failed, `snprintf` truncation was unchecked, `xstrdup` failures were not caught, the dropdown destroyed itself inside its own notification, and strings leaked when `PostMessage` failed | All fixed |

### On-device execution test fixes (2026-09-11)

After the program was actually run on **Windows 11 (build 26200) + real Rokid
glasses**, two more defects were caught that **only show up during real
execution** (neither static review nor compilation could find them):

| Severity | Problem | Symptom | Fix |
|---|---|---|---|
| **critical** | `run_process()`'s stdin pipe was **wired backwards**: it kept the write end but closed it before writing, then called `WriteFile` on the "read end" | `adb shell` immediately received EOF, and **not a single byte of stdin could be sent** → ② installing the helper wrote an empty script, and custom root commands did nothing at all. The UI only showed "no output", making it hard to notice | Close the parent's read end, write to the write end instead, and close it after writing to send EOF |
| **critical** | The device-info panel's format string had **14 `%ls` but only 11 arguments** | After pressing "Refresh" and reading the device, `vswprintf` read variadic arguments that do not exist → **APPCRASH in `msvcrt.dll` (c0000005)** | The format string was changed to 11 `%ls` |

The reason the second defect escaped `-Wall`: GCC does not treat MinGW functions
such as `__mingw_swprintf` as printf prototypes, so `-Wformat` does not check them.
`tools/check_logfmt.py` has therefore been extended to **also scan printf-family
calls with literal format strings** (currently covering 144 localized calls +
28 literal-format calls).

**End-to-end testing of the full flow (including the exploit and reboots)** caught
three more problems:

| Severity | Problem | Symptom | Fix |
|---|---|---|---|
| medium | The status bar **stayed on "Running…"** after an operation ended | Operations such as push / helper / trigger / silent capture / privacy LED did not set a terminal state themselves, so the status bar stayed on "Running…" forever | When the worker finishes and that operation has not set a status, "Ready" is filled in; on cancel it shows "Cancelled" |
| medium | `pkill -9 -f 'ghostlock_bridge_helpe[r]'` **killed its own adb shell** | adb returned 137 (128+SIGKILL) and the step was misjudged as failed. The character class does not prevent self-matching in this toybox version | Switched to a **PID file**: write `$!` when starting the helper and use `kill -9 $(cat ...)` to stop it, so there is no matching problem |
| low | Commands with no stdout were falsely reported as "(no output, may have failed)" | Installing the helper produced 5 false alarms in a row (mkdir / chmod / pkill simply produce no output) | Switched to the process exit code; also found that "with no output at all, `out` is `NULL` rather than an empty string", so the condition never held |
| low | Helper-start detection was misjudged as failed on a loaded device | With many leftover processes on the device, the first `ps` query returned 0 | Changed to at most 5 rounds × 1.5 s polling |
| low | Dialog titles displayed the literal `v%ls` | `T(STR_APP_TITLE)` only looks up the table without formatting, so the version was never substituted (3 places) | Switched to `Tf(STR_APP_TITLE, ZL_VERSION)`; and added a static check that reports an error when `T()` is used on a string containing format specifiers |
| low | The device panel was not updated after a successful one-click | The panel still showed "root: not running", contradicting "root ✓" in the status bar | After a successful one-click, `do_refresh()` is called automatically to update the panel |

One more layout problem under high DPI was also fixed: the window was created with
"window dimensions" but the layout was computed in "client-area coordinates", and
on a screen at 200%% scaling the client area measured 41 px less than expected,
**cutting off the status bar**. This is now handled by a `WM_SIZE`-driven
`ui_layout()`: the log area's height is computed from the actual client area, and
the window size is clamped to the work area.

The review also **confirmed as correct**: the argument counts and types of all
133 localized formatting calls match; wide printf always uses `%ls` (under MinGW's
ANSI stdio `%s` would take a `char*`, and that trap has been avoided);
`run_process` has no duplicate handle closes; and `fw_classify`'s numeric
comparison is the right choice for zero-padded version numbers (`009`/`012`).

---

## Folder contents

```
ZeroLightRokidDesktop/
├── zero_light_desktop.exe     ← main program (can be run by double-clicking)
├── README.md                  ← this document
├── SHA256SUMS.txt             ← hashes of the delivered files (verifiable with sha256sum -c SHA256SUMS.txt)
├── .gitignore                 ← excludes build artifacts, settings.ini, __pycache__
├── src/
│   ├── zero_light_desktop.c   ← complete source code (Win32 API)
│   ├── i18n.h                 ← i18n runtime (detection / lookup / fonts / RTL)
│   ├── i18n_strings.h         ← [auto-generated] 14-locale string table, do not edit by hand
│   ├── theme.h                ← theme palette and drawing foundations (dark / light)
│   ├── ui.h                   ← custom-drawn widget library (cards / buttons / switches / status panel)
│   ├── ui_shell.h             ← custom-drawn shell (layout, events, dialogs)
│   ├── app.rc                 ← version information and manifest resources
│   └── app.manifest           ← high-DPI + v6 common controls declaration
├── assets/
│   ├── ghostlock_rv101        ← exploit binary (the tool pushes it to the glasses automatically)
│   ├── root_daemon            ← root daemon binary
│   └── silent_camera.ogg      ← silent camera tone used for silent capture
├── tools/
│   ├── i18n_master.json       ← master translation table (14 locales × 241 strings)
│   ├── i18n_glossary.md       ← phone App terminology table (translation baseline)
│   ├── gen_i18n.py            ← generates + validates i18n_strings.h
│   ├── i18n_apply.py          ← merge translation fragments / fill in gaps / check progress
│   ├── check_logfmt.py        ← checks the argument counts of formatting calls (a crash class the compiler cannot catch)
│   ├── verify_exe.py          ← verifies that the compiled exe really contains all translations
│   ├── test_extended_paths.sh ← validates the Shizuku / DebugReceiver / WiFi media channel
│   └── wintest/               ← GUI automation test helpers (ZlTest.cs: window operations / screenshots)
├── docs/
│   ├── desktop-loop-protection.md  ← boot loop protection (circuit breaker / pre-flight check / counting / power warning)
│   ├── lease-and-keeper.md         ← interaction between the operation lease and the App keeper
│   ├── extended-path-tests.md      ← extended-path on-device tests (Shizuku / broadcast / WiFi)
│   ├── i18n.md                     ← how to add / modify a language
│   ├── research-update.md          ← summary of the latest research reflected in v2.0
│   ├── macos-port-feasibility.md   ← macOS port feasibility study
│   ├── screenshots/                ← on-device screenshots (each locale, Arabic RTL)
│   └── ui-proposal/                ← v3.0 interface proposal and implementation screenshots
├── build_mingw.sh             ← Linux/macOS cross-compilation
├── build_mingw.bat            ← MinGW-w64 build (Windows)
└── build_msvc.bat             ← MSVC build (Windows)
```

---

## Before you start

1. Install **adb** on Windows (Android SDK Platform-Tools).
   - The tool searches the usual paths automatically, in order; if it cannot find
     adb, click "Browse…" next to "adb path" at the top to specify it, then click
     "Apply" (this is stored in `settings.ini`).
2. Connect the glasses over USB so that `adb devices` can see them.
3. The firmware must be **`1.22.009-20260710-150201`** (the GhostLock-vulnerable
   version); **`1.25.012` and later are patched, and this tool cannot escalate
   privileges** (the tool detects this automatically and refuses to run).

> 💡 **The first thing to do with a new device: press "🔒 Lock OTA".**
> The official App may upgrade the glasses to the patched 1.25.012 in the
> background, and the upgrade is **irreversible** (A/B anti-rollback, with no
> public downgrade package). Once locked, the state persists across reboots.

---

## Button reference

### Main flow

| Button | What it does |
|---|---|
| Refresh | Detects the device: serial / model / firmware / SPL / `id` / SELinux / root / Shizuku / uptime |
| ① Push exploit | Transfers `assets/ghostlock_rv101` and `assets/root_daemon` to `/data/local/tmp/` on the glasses |
| ② Install helper | Creates the bridge directory and writes the helper script so that it stays resident (native write, no bash needed) |
| ③ Trigger escalation | Sends the payload (turn off SELinux + start root_daemon) to the mailbox `last.cmd` (atomic write) |
| 🚀 One-click acquire root | ①+②+③ + automatic polling + automatic retry (at most 6 boots × 2 trigger rounds; on reboot it automatically waits for the device to come up and starts over) |
| ✓ Verify root | Runs `id; getenforce` over the root_daemon socket to confirm `uid=0(root)` + `Permissive` |
| View exploit log | Shows the last 60 lines of `gl_run.log` on the glasses |
| Reboot device | `adb reboot` (root disappears and must be acquired again) |
| Run command below | Runs a custom command as root (for example `ls /data`) |
| Cancel | Aborts the current operation (the one-click flow can be stopped at any time) |

### Advanced tools

| Button | What it does |
|---|---|
| 🔒 Lock OTA | `pm suspend com.rokid.glass.ota`, prevents upgrading to patched firmware (**strongly recommended as the first step**) |
| Unlock OTA | `pm unsuspend` + `pm enable`, restores OTA functionality |
| OTA patch check | Queries the Rokid OTA server and checks whether the latest version is patched (requires network) |
| Start Shizuku | Installs / pre-authorizes / starts the Shizuku server (must be re-run on every boot) |
| Silent capture | Overrides the camera tone with a bind mount (requires root) |
| Restore camera tone | Removes the bind mount described above |
| Suppress privacy LED | Disables the privacy indicator that lights up when the camera is turned on (requires root; authorized research only) |
| Restore privacy LED | Restores the privacy indicator |

### Log

| Button | What it does |
|---|---|
| Save log | Saves the complete log as a UTF-8 text file |
| Copy log | Copies it to the clipboard |
| Clear log | Empties the display |
| About | Version and supported-firmware information |

### Language

The "Language:" dropdown at the top switches between the 14 languages, and the
first entry is "Follow system". The interface is rebuilt immediately after
switching (Arabic also switches to an RTL layout), and the choice is stored in
`settings.ini`.

---

## Typical flow (for newcomers)

```
1. Plug in the cable → press "Refresh"     → the info area shows the serial / firmware 1.22.009 / uid=2000 Enforcing
2. Press "🔒 Lock OTA"                     → avoid being upgraded to the patched 1.25.012
3. Press "🚀 One-click acquire root"       → the log below shows push / helper / trigger / progress in sequence
4. When you see "root achieved! verifying" + `uid=0(root) ... Permissive` = success 🎉
5. Any command afterwards: type it into the input box → press "Run command below"
```

> If the glasses reboot partway through the one-click flow: that is **normal**
> (a property of the exploit, roughly a 50% chance). The tool detects the reboot
> automatically, waits for the system to stabilize, and moves on to the next
> round. If all 6 boots fail, just press "One-click acquire root" once more.

---

## Building it yourself

| Environment | Command |
|---|---|
| Linux / macOS cross-compilation | `./build_mingw.sh` (32-bit: `./build_mingw.sh 32`) |
| Windows + MinGW-w64 | `build_mingw.bat` |
| Windows + MSVC | Run `build_msvc.bat` in a Developer Command Prompt |

Requirements: MinGW-w64 (`x86_64-w64-mingw32-gcc`) or Visual Studio;
regenerating the locale string table additionally requires Python 3
(`src/i18n_strings.h` is already shipped, so this is usually unnecessary).

Linked system libraries: `gdi32 user32 comctl32 shell32 ole32 shlwapi comdlg32 winhttp bcrypt gdiplus uuid`.

### Verification

```bash
python3 tools/gen_i18n.py --check      # translation table: completeness, cross-locale format specifier consistency, en baseline unpolluted
python3 tools/check_logfmt.py          # whether each localized formatting call's argument count matches its format string
python3 tools/verify_exe.py zero_light_desktop.exe
                                       # whether the compiled exe really contains all 3,374 translation entries
```

`gen_i18n.py` and `check_logfmt.py` are integrated into `build_mingw.sh` and run
automatically on every build.

`check_logfmt.py` checks a class of crashes **the compiler cannot catch**: the
format string comes from a runtime table lookup, and if the caller supplies one
argument too few, `vswprintf` reads stack contents that do not exist. All 146
calls currently match.

`build_mingw.sh` uses `-Wl,--no-insert-timestamp`, so **the same source code
produces a byte-identical executable**, and `SHA256SUMS.txt` can be verified
reproducibly.

---

## Known limitations

- **No code signing**: SmartScreen may warn about an "unknown publisher"; choose
  "Run anyway".
- Only one operation runs at a time (while busy the buttons are disabled, but
  "Cancel" stays available).
- No bash dependency, but **`adb.exe` is still required** (the tool does not bundle
  adb itself).
- **The OTA patch check only compares version numbers**; it does not download or
  disassemble the OTA image. A complete determination requires
  `payload-dumper-go` + `vmlinux-to-elf` + ARM64 disassembly (see
  `rokid_root/deploy/ghostlock_patch_check.py`), which this tool deliberately does
  not do.
- Silent capture and privacy LED suppression **do not survive a reboot** and must
  be re-applied.

---

## macOS port

The feasibility study is in
[`docs/macos-port-feasibility.md`](docs/macos-port-feasibility.md). Conclusion:
**a native AppKit port is feasible and not expensive** (roughly 10–15 working
days; a minimum viable version about 4 days), but **Wine is not viable** (the
compatibility layer cannot see adb devices on USB).

---

## Disclaimer

For security research and penetration testing on **your own / authorized devices**
only. This tool contains a privilege-escalation program that can cause a kernel
panic and reboot the device. Any consequences of using this tool are the user's
own responsibility.
