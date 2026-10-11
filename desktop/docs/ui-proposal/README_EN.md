# ZeroLightRokid Desktop v3.0 — Custom-Drawn Interface

> 中文版：[README.md](README.md)

v3.0 rewrites the UI from "stacked native Win32 controls" into a **custom-drawn
branded interface** that matches the phone build's design language. The functional
coordination layer (the adb flow) is completely untouched.

## Contents

| File | Content |
|---|---|
| `01_dark.png` / `02_light.png` / `03_running.png` | realistic mockups made before implementation began (for the proposal) |
| `impl/` | **on-device screenshots** (2880×1620 @200% and 1920×1080 @100%) |

## Architecture

| File | Lines | Responsibility |
|---|---|---|
| `src/theme.h` | 406 | dark / light palettes, system theme detection, GDI+ initialization, rounded corners / circles / lines / geometric icons |
| `src/ui.h` | 232 | component library: cards, buttons, switches, status panels, chips, progress bars |
| `src/ui_shell.h` | 761 | element definitions, layout computation, drawing, hit testing, Rich Edit log, action dispatch |
| `src/zero_light_desktop.c` | 2732 | everything else (adb coordination layer, i18n, worker threads, window procedure) |

There are only 4 real child controls (adb path / serial number / root command /
log); everything else is custom-drawn.

## Mapping to the Original Pain Points

| Pain point | Solution |
|---|---|
| buttons cannot be found | 20 equally weighted buttons → 1 primary + 6 secondary + 4 switches, split across four cards |
| status cannot be seen | the switch itself is the status; a 30-second background poll updates it live |
| information is hard to read | a wall of text → structured: connection status + serial number, model row, status chips |
| no idea what to do next | dynamic guidance text on the primary card (changes with the root state) |
| it is ugly | branded color scheme, anti-aliased rounded corners, custom-drawn geometric icons |

## Diagnostic Tool

The `ZL_FORCE_DPI=96` environment variable forces a specific DPI, which is used to
validate the layout for other DPIs on a machine with a fixed scaling ratio (for
example, validating the 100% layout on a 200% screen).

## Known Limitations

- When the log is expanded, the window tries to grow 240 logical pixels taller; if
  the workspace limit has already been reached (for example on a large 200%-scaled
  screen) it stays as it is, and the log then fills the remaining space and is
  scrollable.
- `LOG_DEV_NOT_FOUND` / `ST_DEV_NOT_FOUND` are **safety mechanisms** newly added in
  v3.0: when the device with the specified serial number is not online, the tool
  refuses to perform any operation on other devices.

## On-Device One-Click root Regression Test (2026-09-12)

At 2880×1620 @200% scaling, the full one-click flow was run with the v3.0 interface
on real glasses (RG-glasses, 1.22.009).

**Result: pass.** On-device verification:

| Check | Result |
|---|---|
| SELinux | `Permissive` |
| `root_daemon` processes | 3 |
| socket | `/data/local/tmp/root_daemon.sock` exists |
| `last.done` | `uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0` + `__GHOSTLOCK_PAYLOAD_DONE__` |
| running `id` through the daemon channel | `uid=0(root) gid=0(root) groups=0(root)` |

The interface reflects this in step: the primary card's large button automatically
becomes a **✓ Glasses rooted** status panel, "Re-acquire root" is demoted to a
secondary button; the device card's chip shows `Permissive`; and the status bar
shows "connected, root ✓ (resident daemon present)".

Screenshot: `impl/v3_regression_rooted.png`

### Failures During the Run and How They Were Read

The first two attempts failed, with the log entry `failed-done-timeout`: SELinux had
already become `Permissive` (the LPE succeeded) but the payload stage did not
finish, and `last.done` was never written. This is the exploit's known
**partial-success** mode, not something caused by the interface rewrite — the
device-side script-generating code on that path is unchanged since v2.0, and the
hashes of `assets/ghostlock_rv101` and `assets/root_daemon` still match
`SHA256SUMS.txt`. The third attempt succeeded within 140 seconds.
