# Glasses Media Index / Transport Channel — Follow-up Verification (2026-09-16 07:51–08:03)

> 中文版：[index_resilience_verification_2026-09-16.md](index_resilience_verification_2026-09-16.md)

- **Trigger**: the to-do in `reports/wifi_transfer_fix_2026-09-13.md` §24 (glasses adb was disconnected, so it could not be completed that evening).
- **Targets**: glasses `1.0-safe.3` (versionCode 5), phone `1.3` (versionCode 4).
- **Script**: `scripts/verify_index_resilience.sh` (re-runnable; it touches only self-created test files and backs up the index first).
- **Result**: **A–E all pass (PASS=9 → 11/11 after D/E were filled in)**.

---

## 0. First fix two "verification method" pitfalls (not product problems)

| # | Pitfall | Symptom | Correct approach |
|---|----|------|------|
| 1 | `adb pull /sdcard/Android/data/<pkg>/…` | Android 11+ scoped storage blocks it outright → the first run produced `media=?`, `cp: bad ...` | Use `adb exec-out cat` (read) and `adb shell "cat > …"` (write) instead |
| 2 | The glasses App closes the existing socket on every restart | No reconnect to the phone before fetching the list → empty `total=`, a "false failure" | Send `CONNECT` and retry 3 times before fetching the list |
| 3 | Wrong logcat tag guessed | The glasses-side debug message tag is **`GlassesDebugReceiver`** (not `DebugReceiver`) → misdiagnosed as "the broadcast was not delivered" | Use the correct tag |

> Items 1 and 2 have been written into the script comments; the transcript of the first run is kept in
> `reports/index_resilience_verification_20260916_0751.txt` (PASS=4 / FAIL=3, all of them the script defects above).

## 1. Verification results (second run: `reports/index_resilience_verification_run2.txt`)

| Item | What was verified | Evidence | Result |
|----|----------|------|------|
| — | Install `1.0-safe.3` | `versionName=1.0-safe.3` | ✅ |
| — | Baseline is consistent | Index media=**14**, disk **14** files, **no** reconcile message at startup | ✅ |
| **A** | **Per-entry fault tolerance for a bad album** (removing `albums[0].name`) | `E MediaRepository: album entry #0 skipped: No value for name`, and the phone list is still **total=14** | ✅ |
| **B** | **Forward self-repair** (present on disk, absent from the index) | `W index reconciled: +1 re-registered / -0 removed`, phone **total=15** | ✅ |
| **C** | **Reverse self-repair** (present in the index, absent from disk) | `W index reconciled: +0 re-registered / -1 removed (file missing on disk)`, phone back to **total=14** | ✅ |
| — | Restore the index | `media=14` (same as baseline) | ✅ |

**What A means**: this is exactly the gap that adversarial verification caught (the first version protected only the `media` array, while the `albums` loop still shared the outer try).
After the fix: **one bad album skips only itself, and media is completely unaffected** — before the fix, both album and media would be wiped out.

## 2. D/E: push and indication over the transport channel

### 2.1 Obstacle: SoftAP needs root, and root had disappeared because the device shut itself down

D failed outright in the first run, and the glasses-side log gave a clear reason:

```
W WifiHotspot: root daemon 未就緒，無法啟動 SoftAP      ← several times in a row
phone: WIFI_CONNECT result=Failure(WifiSetupException: hotspot not ready)
$ ps -A | grep -c root_daemon → 0 ；getenforce → Enforcing
$ uptime → up 9 min ；ro.boot.bootreason → shutdown,leg_fold_timeout
```

⇒ Late at night on 2026-09-15 the glasses **shut themselves down** (`leg_fold_timeout` = automatic shutdown on fold / not-worn timeout).
This was not an adb/USB problem, and **not** a GhostLock panic (previous instances were `reboot,PS_HOLD`).
Shutting down loses root (SELinux returns to `Enforcing`), and **SoftAP needs root** ⇒ the WiFi channel cannot be established.
(Also note: the **leftover socket file** `/data/local/tmp/root_daemon.sock` is still there, which makes it easy to misjudge that root is still present — to confirm, check `ps`.)

> ⚠ Corrects the guess in `wifi_transfer_fix_2026-09-13.md` §23: it said "adba/USB connection problem", but the actual cause was **the device shutting itself down**.

### 2.2 A root-free alternative: use an adb tunnel to create a real TCP channel

What D needs to verify is "push over the **TCP channel**", which does not require actually starting SoftAP — the glasses' `WifiServer` binds `[::]:8555`,
so adb can be used to connect that port to the phone:

```bash
adb -s <glasses> forward tcp:8555 tcp:8555     # PC:8555 → glasses:8555
adb -s <phone> reverse tcp:8555 tcp:8555     # phone:8555 → PC:8555
adb -s <phone> shell am broadcast … --es cmd WIFI_CONNECT --es host 127.0.0.1 --es port 8555
```

The glasses side confirms it goes through the same code path: `WifiServer: wifi connection accepted from /127.0.0.1:47999`
(= `WifiServer` → `ConnectionHandler` → `CommandRouter`, exactly the same as real WiFi; only the underlying transport differs).

### 2.3 Results

| Item | What was verified | Evidence | Result |
|----|----------|------|------|
| **D-1** | **Control commands go over TCP** (deliberately not sending `CONNECT`, letting `Remote.link()` choose) | phone `LIST_MEDIA_INFO … total=14`; glasses `CommandRouter: recv: LIST_ALBUMS id=11` / `recv: LIST_MEDIA id=12` | ✅ |
| **D-2** | **Push goes over TCP** (the core of the regression fix) | glasses `PUSH_TEST sent code=gl_attempt arg=verify-tcp **wifi=true bt=false**`; phone `RemotePush: progress code=gl_attempt arg=verify-tcp` | ✅ |
| **D-3** | The push really does show up in the UI | the phone's operation log shows "`[進度] 跑 exploit 第 verify 次嘗試`" | ✅ |
| **D-4** | Bluetooth channel control case | glasses `PUSH_TEST … wifi=false bt=true` → the phone likewise receives `RemotePush … arg=verify-bt` | ✅ |
| **E** | **Closing the WiFi channel → the UI updates immediately** | before: `傳輸：WiFi 192.168.43.1:8555`; after: `傳輸：藍牙` (without manually switching tabs) | ✅ |

Screenshots (all taken only after **confirming this App was in the foreground**):
`reports/ui_transport_2026-09-13/06_before_wifi_disconnect.png` (WiFi),
`05_after_wifi_disconnect.png` (Bluetooth).

> **The "foreground check" is a required step**: when the screenshot for E was first taken, the phone was being used for another App,
> so an **unrelated private screen** was captured; those two images were deleted immediately, and thereafter every screenshot was
> taken only after confirming `topResumedActivity` was this App. Please keep using this guard for automated screenshots in the future.

## 3. Device state (after verification)

```
phone: CONNECT <DEVICE_ALIAS> result=Success (the Bluetooth control link has been restored)
glasses: rootMode=MANUAL rootAcquired=false  silent=true/false
      ringEnabled=true ringRunning=false  ringLast=start_failed//dev/input/event1
index: media=14 (consistent with disk)
```

- **root is not present** (because the device shut itself down) ⇒ currently: (a) the WiFi media channel is unavailable (SoftAP needs root), so
  media automatically falls back to Bluetooth; (b) ring evdev control cannot start (needs root); (c) silent capture does not take effect.
  Restoring these features requires re-arming root once (`deploy/root_bridge.sh` or `build-cache/one_shot_root.sh`).
- The exploit was not re-run in this round (ArmPolicy is MANUAL; arming is a risky operation, left to the user to time).

## 4. Confidence

| Statement | Label | Confidence | Basis |
|------|------|------|------|
| A bad album no longer drags down media | FACT | **Very High** | log + phone list 14 |
| Bidirectional index self-repair works | FACT | **Very High** | `+1 re-registered` / `-1 removed` + phone list 15→14 |
| Control commands really do go over TCP | FACT | **Very High** | the command succeeded without `CONNECT`, and the glasses side received it |
| Push over the TCP channel is fixed | FACT | **Very High** | the phone receives `RemotePush` when `wifi=true bt=false` |
| The UI updates immediately when the channel is closed | FACT | **High** | before/after screenshots (single case) |
| Push over the real SoftAP path (not a tunnel) | Inferred | High | the tunnel and SoftAP use the same set of `WifiServer`/`ConnectionHandler`/`CommandRouter`; only the underlying transport differs |
| The device shut itself down = `leg_fold_timeout` | FACT | **Very High** | `ro.boot.bootreason` + `uptime` |

---

## 5. Third run (08:03): adb disconnected midway — handling and residual-state notes

After fixing the script (CONNECT-first, tunnel fallback, foreground guard) it was re-run once, and **both devices disappeared from
`adb devices` right at the start of step A** (`08:04`; still 0 devices after `adb kill-server` / restarting the server — suspected cable/hub unplug, or the user was using the device).
That run's transcript, `reports/index_resilience_verification_run3.txt`, stops at "--- A. 壞 album 逐筆容錯 ---".

**Possible residual state**: step A **deliberately** removes the index's `albums[0].name`, so if the interruption happened after the copy,
the index on the device will be left in an "album is missing its name" state. **This does not cause data loss** — the next time the App starts,
`reconcileWithDisk()` rebuilds albums and media from disk (add-only), and the only visible difference is that
**album names become directory ids** (`alb-3b6dfe09` rather than `Camera`).

**Handling already prepared**:
- `scripts/check_index_state.sh [--fix]`: checks the index (including whether album names are anomalous and whether the index / disk / phone entry counts agree);
  `--fix` repairs the name back to `Camera` and restarts the App to verify. **The read-only mode is safe to run first.**
- `check_index_state.sh --fix` has already been **triggered automatically** when the glasses returned to adb.

> Note: the full A–E verification **was completed in the second run plus manual completion of D/E** (§1, §2); the third run was merely "run it once more with the improved script",
> and its result does not affect the conclusions above.

## 6. Script improvements (learned this time, already written into `scripts/verify_index_resilience.sh`)

| Improvement | Reason |
|------|------|
| Step D changed to **`CONNECT` first, then `WIFI_CONNECT`** | SoftAP negotiation goes over the Bluetooth control link; without a connection you only get `hotspot not ready` (the false failure in the first run) |
| **Automatically switch to an adb tunnel** when WiFi is unavailable (`forward` + `reverse` on port 8555) | A real SoftAP needs root; the tunnel uses the same set of `WifiServer`/`CommandRouter`, so **TCP channel behavior can be verified without root** |
| Step E **checks `topResumedActivity` before taking a screenshot** | The phone is the user's private device; the first attempt mistakenly captured unrelated private screens (those two images were deleted). If the foreground is not this App, only log it and do not screenshot |
| Always use `exec-out` / `shell cat >` to read and write `/sdcard/Android/data` | `adb pull` is blocked by scoped storage on Android 11+ |
| `CONNECT` first and retry 3 times before fetching the list | The glasses App closes the socket on every restart, otherwise you get an empty value and a false failure |
