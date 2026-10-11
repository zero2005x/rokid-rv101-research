# Extended-Path On-Device Test Report

> 中文版：[extended-path-tests.md](extended-path-tests.md)

> **This document contains two run records**: §1–§3 below are the original baseline
> from 2026-09-12 (**20 passed / 0 failed / 0 skipped**); the "2026-09-13 re-run" at
> the end of the document is the full re-run after a reboot (**21 passed / 0 failed /
> 0 skipped** — one item more, because `shizuku_server` was already running and
> section 1 no longer skipped it). That run also fixed an **intermittent false
> negative** in the script (root detection required the `/proc/net/unix` count to be
> exactly 1).

Date: 2026-09-12
Device: Rokid RG-glasses (`<DEVICE_SERIAL>`), firmware `1.22.009-20260710-150201`
Environment: Windows 11, 2880×1620 @200%; adb invoked directly from bash
Script: `tools/test_extended_paths.sh` (re-runnable; exit code 0 = all passed)

## Results Overview

| Path | Result | Checks |
|---|---|---|
| 1. Shizuku engine root path | ✅ Passed | 4 / 4 |
| 2. DebugReceiver broadcasts | ✅ Passed | 8 / 8 |
| 3. WiFi media channel (glasses side) | ✅ Passed | 6 / 6 |
| **Total** | **✅ 20 passed, 0 failed, 0 skipped** | |

## Clarifying Ownership First

These three items were previously listed as "paths the desktop tool has not tested",
but in fact only one of them belongs to the desktop tool:

| Item | Ownership | The desktop tool's role |
|---|---|---|
| Shizuku engine | **desktop tool feature** (`OP_SHIZUKU` → `do_shizuku()`) | primary actor |
| DebugReceiver | debug interface of the glasses app (`com.rokid.rc.glasses`) | can only relay the adb broadcast |
| WiFi media channel | between phone ↔ glasses | can only validate the glasses side |

## 1. Shizuku Engine root Path

The desktop tool's Shizuku switch does the following in order: confirm the manager
is installed → derive the `libshizuku.so` path → pre-authorize → kill the old server
→ start → verify.

| Check | Result |
|---|---|
| Shizuku manager installed | ✅ `/data/app/~~iPe…/base.apk` |
| `libshizuku.so` exists and is executable | ✅ (`do_shizuku()` launches directly through this path; the step most likely to fail) |
| `moe.shizuku.manager.permission.API_V23` granted to `com.rokid.rc.glasses` | ✅ |
| `shizuku_server` running | ✅ |

Actual log:

```
--- Starting Shizuku server ---
  server library: /data/app/~~iPe…/lib/arm64/libshizuku.so
  pre-authorizing moe.shizuku.manager.permission.API_V23 (skipping the authorization dialog on the glasses)…
  starting server (takes about 3–10 seconds)…
info: killing old process...
info: apk path is /data/app/~~iPe…/base.apk
info: starting server...
info: shizuku_server pid is 7510
info: shizuku_starter exit with 0
[+] Shizuku server is now resident (must be re-run after a shutdown / reboot).
```

## 2. DebugReceiver Broadcasts

The interface is taken from
`app-glasses/src/main/kotlin/com/rokid/rc/glasses/DebugReceiver.kt`:

- component `com.rokid.rc.glasses/.DebugReceiver`
- action `com.rokid.rc.glasses.DEBUG`
- `--es cmd <TOGGLE_PANEL|SET_DISPLAY|SET_LOCALE>`; `SET_DISPLAY` additionally carries `--ez conn/root/camera/storage/led`
- logcat tag `GlassesDebugReceiver`

| Check | Result |
|---|---|
| `com.rokid.rc.glasses` installed | ✅ |
| the manifest registers DebugReceiver | ✅ |
| `TOGGLE_PANEL` / `SET_DISPLAY` / `SET_LOCALE` broadcasts sent | ✅ ×3 (all `Broadcast completed`) |
| logcat confirms all three took effect on the device | ✅ ×3 |

**Restored after the test**: `SET_LOCALE` without `lang` (= follow the system),
`SET_DISPLAY` with everything `true` (= default).

## 3. WiFi Media Channel (Glasses Side)

### Important finding: this path can be fully validated without a phone present

The comment in `WifiHotspot.kt` states:

> Trigger channel: RootDaemonClient (root, after privilege escalation). **Measured:
> adb shell (uid 2000) can also run start-softap**, but when the app (untrusted_app)
> calls `cmd wifi` directly it is rejected for lack of permission.

Moreover, `WifiServer` (TCP 8555) is started by `CaptureService`, and
`CaptureService` can be brought up with `am start-foreground-service`. The glasses
side of the chain can therefore be walked through completely from the desktop.

| Check | Result |
|---|---|
| glasses app process running | ✅ |
| TCP 8555 listening after `CaptureService` is brought up | ✅ |
| SoftAP started, glasses address `192.168.43.1` | ✅ |
| **Functional check: an actual TCP connection succeeded** | ✅ |
| `WifiServer` logcat confirms the connection was accepted | ✅ |
| restored after the test: SoftAP off | ✅ |

On-device log from the functional check:

```
I WifiServer: wifi connection accepted from /192.168.43.1:58226
```

### Known limitations (from the research document)

On this firmware, **STA and SoftAP cannot be maintained at the same time**; turning
on the hotspot kicks off the existing WiFi STA, and the STA recovers automatically
after the hotspot is turned off (`cmd wifi stop-softap`). SoftAP does not survive a
reboot.

## Interface Defect Fixed Along the Way: the Shizuku Switch Popped Up an APK Chooser Every Time

**Problem**: `ui_invoke(EL_SHIZUKU)` popped up a file chooser whenever `g_apk` was
empty — even when the Shizuku manager had long been installed and no APK was needed
at all. The user was interrupted every time they pressed Shizuku.

**Fix**: "whether the Shizuku manager is installed" was folded into the existing
30-second background poll (a `Z=` field was added to the probe command,
`pm path moe.shizuku.privileged.api | wc -l`), and the dialog is popped up only when
`g_dev.shizuku_installed != 1`.

The reason for taking this route: **to avoid calling adb on the UI thread** (which
would compete with the worker for adb and freeze the screen); the poll was already
running anyway, so adding one `pm path` costs almost nothing and is far faster than
`dumpsys package`.

| Value | Meaning | Behavior |
|---|---|---|
| `1` | installed | start directly, **no dialog** |
| `0` | not installed | pop up the dialog so the user can pick an APK |
| `-1` | not probed yet (the app just started / the device is offline) | pop up the dialog (covering the brief gap; better to ask once too often than to leave the user unable to install) |

**Verification**:

- `settings.ini` **deliberately contains no `apk=`** (exactly the condition under
  which the old version popped up the dialog every time); pressing the Shizuku
  switch → `NO_DIALOG ✓`, log `info: shizuku_server pid is 17978` /
  `shizuku_starter exit with 0`
- the detection mechanism verified in both directions: a nonexistent package gives
  `Z=0`, an installed package gives `Z=1`

**Follow-up hardening**: a **Shizuku APK field + "Browse…" button** was added to the
settings overlay, so even when Shizuku is not installed the APK can be specified
directly in the settings without relying on the pop-up dialog. The behavior now is
therefore: installed → start directly; not installed → install using the APK from
the settings (only if the settings are also empty does pressing the switch pop up
the file chooser once).

**Lesson from the process**: on the first attempt I edited the wrong file —
`case EL_SHIZUKU` lives in `ui_shell.h`, but I did the string replacement on
`zero_light_desktop.c`. Python's `str.replace` **silently skips** when it cannot find
the target, and the project still compiled successfully, so it looked like "changed
but not taking effect". All later changes were guarded with `assert old in s`.

## Three Substantive Defects in the Test Script (Found in the Second Round)

When this round was re-run, sections 2 and 3 suddenly failed; on investigation,
**all three turned out to be problems with the test script itself**, not with the
device or the app:

### 1. The `ROOTED` check was wrong — it used the pipeline exit code, not the on-device result

```bash
ROOTED=0
sh_ 'grep -q root_daemon.sock /proc/net/unix' && ROOTED=1   # wrong
```

`sh_` is a `timeout ... adb shell ... | tr` pipeline, and **its exit code is almost
always 0**, so a device that was not rooted was also judged to be rooted, and the
root-requiring items that followed then failed. Changed to compare the output:

```bash
if [ "$(sh_ 'grep -c root_daemon.sock /proc/net/unix; true')" = "1" ]; then ROOTED=1; fi
```

### 2. DebugReceiver broadcasts require the app to be **running**

When the app is not running, `am broadcast` still reports
`Broadcast completed: result=0`, but the system log simultaneously shows:

```
W BroadcastQueue: Background execution Third-Party APP not allowed:
  receiving Intent { act=com.rokid.rc.glasses.DEBUG ... } to .../.DebugReceiver
```

**The broadcast was dropped by the background-execution restriction and the receiver
was never woken up at all**, so naturally logcat showed nothing. This is a very
insidious false success: the command says it succeeded while nothing actually
happened. The script now confirms the app process is alive first, and proactively
surfaces this warning when it still fails.

### 3. `CaptureService` only starts once MainActivity **comes to the foreground**

Both ways of invoking the service directly fail:

| Command | Result |
|---|---|
| `am start-service -n .../.CaptureService` | `Error: app is in background uid ... idle change:cached` |
| `am start-foreground-service -n .../.CaptureService` | `Error: Not found; no service started.` |

The reason is in `MainActivity.onResume()` (the source comment states it very
clearly):

```kotlin
// start the FGS only after the activity comes to the foreground, to avoid Background start not allowed
startForegroundService(Intent(this, CaptureService::class.java))
```

So the correct approach is `input keyevent KEYCODE_WAKEUP` +
`am start -W -n com.rokid.rc.glasses/.MainActivity`, letting the Activity bring the
service up itself. Measured on device, only then does TCP 8555 start listening.

> Incidental conclusion: **both the WiFi media channel and the automatic application
> of silent capture require the glasses app to be running in the foreground**. This
> is the app's own lifecycle design, and the desktop tool does not intervene.

## Two Other Test-Script Defects Fixed During Testing

1. **Misuse of bash-specific `/dev/tcp`** — Android's `sh` is mksh and does not
   support `/dev/tcp`, producing a **false negative** for "TCP connection failed"
   (while logcat at the same moment showed the connection being accepted). Switched
   to toybox's `nc` (measured to work).
2. **shellcheck SC1087** — `"^$SER[[:space:]]*device"` was missing braces.

## Test Side Effects and Restoration

When `CaptureService` was started, by design it **automatically applied silent
capture** (6 bind mounts) — a side effect of the test, not a user setting. It was
restored through the desktop tool's "Silent capture" switch (log:
`changed=6 mounted=0`, `[+] camera tone restored.`).

## Final Device State

| Item | State |
|---|---|
| SELinux | `Permissive` (rooted) |
| `root_daemon` socket | present |
| `shizuku_server` | running |
| SoftAP | off (restored) |
| TCP 8555 | listening (`CaptureService` running) |
| Silent capture | restored to off (0 mounts) |
| OTA lock | locked |

## Reproducibility

```bash
./tools/test_extended_paths.sh            # default serial number
./tools/test_extended_paths.sh <serial>   # specify the serial number
```

The script decides automatically: when the device is not rooted, the Shizuku and
SoftAP sections are marked "skipped" and it prompts you to run one-click root first,
rather than falsely reporting failure. Exit codes: 0 = all passed, 1 = there were
failures, 2 = the device is not connected.

> adb must be invoked directly. Calling adb through a PowerShell wrapper was measured
> to time out, which once led to a false determination that the device was offline.

---

# 2026-09-13 Re-run (Full Run After a Reboot)

The device was rebooted first → root acquired with `deploy/get_root_now.sh`
(**boot 1 / trigger 1**) → Shizuku started with `deploy/start_shizuku.sh` → this test
was run.

| Section | Result |
|---|---|
| 0. Pre-flight checks | ✅ connected, rooted (`Permissive`) |
| 1. Shizuku engine root path | ✅ 4 / 4 |
| 2. DebugReceiver broadcasts | ✅ 8 / 8 |
| 3. WiFi media channel (glasses side) | ✅ 6 / 6 |
| **Total** | **✅ 21 passed, 0 failed, 0 skipped** (`EXIT_CODE=0`) |

One item more than the original baseline (20/0/0), because `shizuku_server` was
already running this time, so section 1 no longer skipped the "server already
running" item.

## A **Test-Script False Negative** Fixed This Time (Important)

The source originally judged root like this:

```bash
if [ "$(sh_ 'grep -c root_daemon.sock /proc/net/unix; true')" = "1" ]; then ROOTED=1; fi
```

**Requiring exactly 1 is wrong.** For the same socket, `/proc/net/unix` has two
kinds of entry, "listening" and "connected"; as soon as any client is connected it
becomes 2:

- the glasses app's `RootDaemonKeeper` probes once every 30 seconds (`exec("id")`)
- the root-side keepalive
- adb's `nc -U`

In 10 sampling runs measured on device (every 3 seconds), `count=2` occurred once. A
rooted device was therefore **falsely judged "not yet rooted"**, the whole of
section 1 was skipped, and failure was reported — this kind of intermittent false
negative is the hardest to track down, because simply re-running it works.

It has been changed to `-ge 1`, with the reason recorded in a comment.

> The same script already carried a warning earlier that "you must compare the
> output, not the exit code"; this is another instance of the same theme: **when
> judging "whether there is any", do not write it as "exactly equal to some
> number"**.
