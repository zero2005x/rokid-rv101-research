# Desktop Tool: Boot Loop Protection (2026-09-13)

> 中文版：[desktop-loop-protection.md](desktop-loop-protection.md)

> **Why the desktop build needs this too**: the `ArmPolicy` on the glasses-App side only protects **the App's own automatic arming path**.
> The desktop tool uses its own `_desktop` helper to run the exploit **directly**, **completely bypassing** that protection.
> A single "one-click root" performs at most `MAX_BOOTS(6) × TRIGGERS_PER_BOOT(2) = 12` attempts,
> each with roughly a 50% chance of a kernel panic → reboot, so one click can cause 5–6 consecutive reboots.

## 1. The four protections and what was "already there"

| Item | App-side ArmPolicy | Desktop tool |
|---|---|---|
| Post-boot delay `uptime > 150s` | ✅ Yes | ✅ **Already there** (`MIN_UPTIME_S`) |
| Per-boot retry limit | ✅ Yes | ➖ Limit exists but is large (12 per one-click) |
| Exponential backoff | ✅ 5/15/60 min | ❌ None (but there is an implicit interval: each round waits for boot + uptime) |
| Auto-arm only while charging | ✅ Yes | ➖ **Not applicable** (purely manual tool) |
| **Boot loop circuit breaker** | ✅ Yes | ❌ **Was absent → added this time** |
| **Pre-flight loop check before one-click** | ➖ | ❌ **Was absent → added this time** |

The key point: the desktop build **is a purely manual tool** (only button presses cause action; the 30-second poll reads status only, never touches the exploit, never takes a lease),
so protections for **automatic paths** such as "auto-arm only while charging" are meaningless. What is genuinely missing is
the **memory of "repeated reboots within the same round"**.

## 2. What was implemented

### P0-a Cross-launch circuit breaker

- Criterion: when a new boot is detected, if the "last arming" was ≤ **10 minutes** before that boot, one suspicious event is recorded;
  two **2** consecutive events trip the breaker. A long enough interval (a reboot under normal use) resets the counter to zero.
- After tripping, **the current run is aborted** (`LOG_BREAKER_STOP`).
- Boot identity uses `now - uptime` (= the boot time point). **Not uptime itself**:
  uptime only increases within the same round and cannot be compared across launches.
- **State is stored in the `[state]` section of `settings.ini`** and persists across launches —
  if it were kept only in memory, the user could bypass it by closing and reopening the tool.

```
[state]
boot_wall=1789292955     ; last observed boot time point (unix seconds)
arm_wall=0               ; time point when the last attempt started
suspicious=0             ; number of consecutive suspicious reboots
suspended=0              ; 1 = circuit breaker tripped
boot_hist=               ; the most recent 8 boot time points (comma-separated), used by the pre-flight check
```

### P0-b Pre-flight loop check before one-click

If there were ≥ 2 reboots within the last 10 minutes, **or** the circuit breaker has tripped → show a warning dialog box asking for confirmation.

**Per your decision: warn but allow manual override.** Pressing "Yes" logs
`LOG_LOOP_OVERRIDE` and resets the circuit breaker to zero (it can still accumulate again afterwards); pressing "No" logs
`LOG_LOOP_ABORTED` and returns immediately, **executing no exploit at all**.

The dialog box uses `MB_DEFBUTTON2`, so pressing Enter directly also lands on "No".

### P1 Attempt counter display

Every attempt prints "attempt N / 12", so the user knows how many 50% bets they are taking.

### P2 Power warning

Warn when USB/AC power is not detected: if the cable is unplugged during a panic reboot, it can neither be observed nor intervened in.
When the status cannot be read, **do not warn** (to avoid false alarms).

## 3. New i18n

13 new strings × 14 locales = 182 translations, all completed (`gen_i18n.py` requires every key to have all
14 locales, otherwise validation fails). `verify_exe.py` confirms that **all 3,374 entries are correctly compiled into the exe**.

## 4. Validation results (on-device + real exe)

| Test item | Method | Result |
|---|---|---|
| Observe boot while idle | Start the tool, check `settings.ini` | ✅ `[state] boot_wall` written |
| Suspicious reboot counting | Inject an "armed then rebooted 60 seconds later" state | ✅ `suspicious=1`, boot recorded in `boot_hist` |
| **Circuit breaker trips** | Inject the same state again | ✅ **`suspicious=2`, `suspended=1`** |
| State is not cleared by the startup flow | Inject `suspended=1`, then only start the tool without clicking, observe for 60 seconds | ✅ `suspended=1` retained (no writes at all) |
| **Pre-flight dialog appears and persists** | **Point `serial` at a nonexistent device** (ensuring the exploit cannot possibly run), press one-click while tripped | ✅ Main window `enabled=False` for 13+ consecutive seconds |
| Abort does not overwrite the circuit breaker | Same as above; check `settings.ini` while the dialog is present | ✅ `suspended` stays `1` |
| **Root escalation cleanup** | A real `uid=0 ppid=1` orphan on the device (`adb shell kill` returns EPERM) → press one-click | ✅ **Orphan was cleared (match count 0)** |
| All locale strings | `verify_exe.py` | ✅ All 3,374 entries compiled in |
| Leftover cleanup command | Create a fake helper + exploit + trigger file on the device, run the same command | ✅ Killed the helper and exploit, cleared `last.cmd`/`payload.staging` |

> ⚠ **Why the "pre-flight dialog" had to be tested with a nonexistent serial**: when performing coordinate clicks on a real device,
> the main card layout changes with "rooted / not rooted" (see §6.1),
> so reusing fixed coordinates clicks the status panel instead of the button, and the conclusion drawn is a misjudgment.
> With a nonexistent serial, **no path can actually run the exploit**,
> so the dialog behavior can be validated repeatedly in safety. Click testing on a real device, by contrast, requires a screenshot first to confirm the layout.

## 5. Two **real defects** found and fixed during validation

### A. The warning dialog box moves off-screen (**High**)

`MessageBox` positions itself by default at the **center of the owner window**. This tool's main window can be larger than the screen
(measured: desktop 1440×810, main window 2386×1523 @ x=266 → the window center x≈1459 is already off-screen),
so the dialog box was placed off-screen — **the user cannot see the warning, yet is still blocked by the modal**,
which is worse than having no warning at all.

Fix: add `confirm_dialog()`; if the owner window center is not within the virtual screen, use an ownerless
`MessageBox` instead, letting Windows center it relative to the active screen. The existing "close confirmation" had the same problem
and was switched over as well. At the same time, that dialog box's default button was changed to "No" (`MB_DEFBUTTON2`).

### B. Orphaned helpers keep re-running the exploit (**High**)

`cleanup_stray_exploits()` originally **killed only `ghostlock_rv101`**, not the helper.
If the tool is force-terminated/crashes (the helper's PPID becomes 1) or a one-click run is cancelled halfway,
**the helper keeps retrying according to `last.cmd`** — cleanup can never catch up (a batch of child processes is killed, and the helper spawns another batch).

Measured: after force-terminating the tool, **515–996 exploit processes** were left on the device**, load exceeded 100**,
and the device consequently rebooted repeatedly. This is the same class of problem as the App side's previously fixed "after a reboot the helper is launched and runs once more".

Fix: change to **kill the helper first (cut the source) → then kill the exploits → finally clear the trigger files**
(`last.cmd`, `payload.staging`). The order must not be reversed, otherwise the helper may receive a new trigger after the mailbox is emptied.

### C. Leftovers after privilege escalation cannot be killed → switch to the root channel (**new this time**)

The first stage of `cleanup_stray_exploits()` goes through `adb shell` (uid 2000),
but **after the exploit escalates successfully its process belongs to uid 0**, and the shell can do nothing about it
(measured: `kill: Operation not permitted`). That left a
`uid=0 ppid=1` `ghostlock_rv101` orphan on the device that the first stage could not clear no matter what.

Fix: after the first stage, if **leftovers remain** and the root channel is alive, clean once more with root privileges
(`root_cmd()`; first confirm with `id` that it really is `uid=0` before acting).
This is the same strategy as `kill_matching_escalating` in the deploy-side `proc_util.sh` —
**only root can clean up after root**.

**On-device validation (real orphan, not simulated)**:

| Step | Result |
|---|---|
| After acquiring root, a `uid=0 ppid=1` `ghostlock_rv101` exists on the device | ✅ Present |
| `adb shell kill -9 <pid>` | ❌ `Operation not permitted` (reproduces the problem) |
| Press one-click (`cleanup_stray_exploits` runs first) | ✅ **Orphan cleared, match count 0** |

In addition, `count_procs_matching()` was added: it counts with **host-side filtering**, avoiding the device-side
`grep -c` also counting the "sh -c that runs that command" (this project has hit that several times).

## 6. Known limitations

- ~~Orphans owned by root cannot be killed~~ → ✅ **Fixed** (see §5 C): cleanup is escalated when the root channel is available.
  If the root channel is **dead**, uid 0 leftovers still cannot be cleared (there is no other root path) —
  the only options are a reboot, or re-acquiring root first.
- **The dialog's "Cancel" path has not been fully validated with automated GUI**:
  the main window is larger than the screen, the buttons are owner-drawn (no child controls to `BM_CLICK`),
  and `ZlTest esc` posts Esc to the **main window**, which a modal dialog box does not receive,
  so it is not a valid way to cancel. What has been validated is "the dialog appears and blocks execution" and "the code only continues on IDYES".
  **Manual validation once is recommended**: press one-click while tripped → the dialog should be visible → press "No" → the log should show
  `使用者於開機迴圈警告處取消`, and there should be no exploit processes on the device.
- **The retry count is still 12 per one-click**: `MAX_BOOTS` was not adjusted this time; only the circuit breaker and the pre-flight check were added.
  For a more conservative setting, lowering it could be considered (this changes the trade-off between success rate and time).

## 6.1 A pitfall of automated GUI testing (**worth writing down**)

When using `tools/wintest/ZlTest.exe click X Y` to perform coordinate clicks on the main window,
**the coordinates must not be hard-coded**: the main card layout changes with device state —

| Device state | Main card content |
|---|---|
| Not yet rooted | One large cyan "one-click root" button |
| **Rooted** | **Status panel ("glasses rooted / Permissive · locked · anti-upgrade") + "manual mode"** |

Measured: in the "rooted" state, reusing the coordinates from the "not rooted" state clicked the **status panel** instead of the button,
so the conclusion "the one-click was pressed but no warning dialog appeared" was a complete misjudgment.
(Only discovered afterwards by comparing screenshots.)

Lesson: **coordinate-based GUI automation must take a screenshot first to confirm the current layout**; the layout must not be assumed fixed.
In addition, `ZlTest esc` posts Esc to the **main window**,
which a modal dialog box does not receive — that is not a valid way to cancel.

## 7. How to reset the circuit breaker

- Pressing "Yes" (= override) in the warning dialog resets it to zero automatically;
- or edit `settings.ini` directly: `suspended=0`, `suspicious=0`, `arm_wall=0`.

## 8. Build

```bash
./build_mingw.sh          # produces zero_light_desktop.exe (PE32+ GUI)
python3 tools/verify_exe.py zero_light_desktop.exe   # verifies all 14 locale strings are compiled in
sha256sum -c SHA256SUMS.txt
```

`-Wl,--no-insert-timestamp` makes the same source produce identical bytes, and `SHA256SUMS.txt` has been updated in sync.
