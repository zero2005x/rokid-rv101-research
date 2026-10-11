> ⚠ This file is a debugging note (historical record). Compile-time switches such as
> `USE_BRIDGE` were replaced on 2026-09-13 by runtime engine probing (see `docs/deploy.md`
> §9.3 and §P1); the related statements in this document reflect the state at that time only.

# On-Device Debug Notes (Rokid RG-glasses)

> 中文版：[device-debug-notes.md](device-debug-notes.md)

> This document is a historical debugging record (it captures the state at the time of
> writing). **Latest architecture**: Shizuku mode (default) + automatic bridge fallback;
> see `docs/deploy.md` §9 and `reports/shizuku_integration_test_report.md`.

On-device debugging results for 2026-08-31 (serial number `<DEVICE_SERIAL>`).

## Device

| Item | Value |
|------|-----|
| Model | Rokid RG-glasses |
| Android | 12 (SDK 32) |
| SELinux | **Enforcing** (boot) → **Permissive** (after a successful exploit; the payload writes `/sys/fs/selinux/enforce`) |
| Camera | Qualcomm CHI, camera id 0, Back, `maxNumOutputStreams=3` |
| Phone | Redmi 21091116UG (Android 13, SDK 33) |

## exploit (`ghostlock_rv101`) contract

- `/data/local/tmp` = `drwxrwx--x shell shell` (0771); `ghostlock_rv101` = `0755`.
- In the directory chain `/data`(0771)→`/data/local`(0751)→`/data/local/tmp`(0771), others have `x` only.
- **Conclusion**: the App (`u:r:untrusted_app`) can traverse and read/exec the binary, but **cannot write `.ghostlock_root.sh`** (the directory gives others no `w`).
- The default script `.ghostlock_root.sh` (written by the exploit) contains the ready marker `echo '[+] dropping to root shell'`, and runs `cp ghostlock_rv101 su_gl`, `exec /system/bin/sh`.
- **Timing** (measured on device): the exploit writes the default script (ready marker) **~2s** after it starts; it then runs ~100–142s of kernel exploitation; finally it spawns `.ghostlock_root.sh` as root. → the replacement window is about 100s, which is ample.
- **Flaky**: the FOPS install often fails (`FOPS install failed after 3 attempts`), and roughly half the time it reboots the kernel. Retries within the same boot are needed (the orchestrator uses 3) plus a reboot retry.

## Trigger-layer conclusions

- **The direct version is unusable**: the App cannot write to `/data/local/tmp`.
- **The bridge version works and has been proven on device**: the shell helper from `BridgeGhostlockEngine` + `deploy/root_bridge.sh` can complete "run exploit → detect ready → replace payload → wait for DONE". The payload was measured running successfully as `uid=0(root) context=u:r:kernel:s0`.

## Bridge helper fixes

1. Replacement now uses `cp` (keeping `last.cmd` for retries) rather than `mv`.
2. `rm -f .ghostlock_root.sh` before every exploit run (to avoid picking up a leftover ready marker from the previous run).
3. Added **3 retries** + fast detection of `FOPS install failed`.
4. The done poll was extended to 200s (the exploit's completion time varies).
5. Idempotent deployment: `pkill` the old helper, then start a single instance with `setsid`.
6. App `BridgeConfig.timeoutMs` → 500s (to cover the retries).

## Camera fixes

- The Rokid CHI HAL rejects binding multiple use cases at once (`No supported surface combination`, including CameraX's StreamSharing).
- Fix: **bind a single use case** — for photos bind only ImageCapture; for video rebind to VideoCapture and switch back when recording finishes.
- `bindToLifecycle`/`unbindAll` must run on the main thread → use `withContext(Dispatchers.Main)` for the rebind.

## Verified (historical: the early WiFi/TCP version, since moved to Bluetooth)

- (Historical) The TCP server listened on 8555; HELLO/STATUS/LIST_ALBUMS/LIST_MEDIA worked.
- Photo: `IMG_*.jpg` 4032×3024 (12MP), 5.4MB.
- Video: `VID_*.mp4` 1280×720, 2.7s, 2.3MB.
- Album index: the Camera album accumulates mediaCount correctly.
- Bridge root payload: `id` returns `uid=0(root)`.

## Operating model

- The bridge helper runs persistently under `setsid`, but **disappears after a device reboot**, so `deploy/root_bridge.sh` must be re-run.
- (Historical) Phone↔glasses originally went over WiFi; this has since changed to Bluetooth RFCOMM (see "Communications moved to Bluetooth" below).
- ~~The App currently has `AppGraph.USE_BRIDGE = true`~~ (**obsolete**: it is now probed at runtime by `AutoGhostlockEngine`; this switch does not exist).

## To be verified

- ✅ **LED suppression proven**: in a focused test, after the suppress payload ran as root, the readback was `suppressed brightness=0` + `__GHOSTLOCK_PAYLOAD_DONE__` — the LED brightness is definitively written as 0 (off).
- ✅ **ghostlock=ON capture proven**: a full `SET_GHOSTLOCK(true)` + `CAPTURE_PHOTO` received `PHOTO_TAKEN` (12MP JPEG).
- ⚠️ **restore is still flaky**: during the second exploit (restore) the helper is often killed by OOM/lmkd, and the LED stays in the suppressed state; a reboot recovers it.
- ✅ Resolved: switched to Bluetooth RFCOMM; full remote control from the phone App has been proven (see "Communications moved to Bluetooth" and "root daemon persistence validation" below).
- ⚠️ The exploit itself reboots the kernel ~50% of the time and needs retries (orchestrator style).

## Permission fix (important)

- The App wrote `last.cmd` with the default `0600`, which the helper (shell) could not read → **the app→helper bridge was originally broken**.
- Fix: `BridgeGhostlockEngine` sets 0666 when writing `last.cmd`; the helper does `chmod 666` after writing `last.out`.

## Communications moved to Bluetooth (2026-09)

- Both commands and photos/videos **go over Bluetooth Classic RFCOMM** (SPP UUID `00001101-...`); WiFi/TCP has been removed.
- Glasses side: `BluetoothServer` (RKFCOMM server); phone side: `BluetoothClient` (client + request/requestFile).
- On MIUI phones: shell `input` injection is blocked and implicit broadcasts are blocked → automation switched to an **explicit broadcast**
  `am broadcast -n com.rokid.rc.phone/.DebugReceiver --es cmd SET_LED ...`.

## root daemon persistence validation (2026-09-02, single privilege escalation → sub-second LED over socket)

### Goal and conclusion

Replace "trigger the LPE on every command" with "**a single privilege escalation → persistent root daemon → immediate execution over a UNIX socket**", falling back to bridge/LPE only when the socket disappears. **Validated on device.**

### Validation results (full cold-start flow)

| Item | Result |
|------|--------|
| Privilege escalation | Single, `uid=0(root) context=u:r:kernel:s0`, `getenforce Permissive` |
| daemon | `root_daemon` with root PID, PPid 1, `srwxrwxrwx` socket, listed in `/proc/net/unix` |
| Single-command socket latency | **39–48ms** (including nc + the daemon's `sh -c` spawn) |
| hold-loop (white-LED suppression background loop) | Round trip **306ms**; the daemon still responded normally afterwards |
| LED | Green on = 255, white off = 0, readback correct |
| LPE re-runs | **0**: every LED command went over the socket; no `ghostlock_rv101` re-trigger, no OOM/reboot |
| Phone-side integration | `recv: SET_LED` → `LedSuppressor` → `RootDaemonClient` → socket → LED, effective immediately |

### Bugs fixed (root_daemon.c and LedPayloads.kt)

1. **Single thread + `popen` pipe deadlock**: a command that backgrounds a child process (the hold-loop) inherits the write end of the popen pipe →
   `fgets` never sees EOF and the whole daemon stops accepting.
   → Fix: fork a handler per connection; redirect stdout/stderr to a **temporary file** (not a pipe); `waitpid` waits only for `sh -c`.
2. **The client socket was inherited by the background child**: the hold-loop held connection `c` → the App's `readText()` never returned.
   → Fix: have the sh -c child `close(fd 3..127)`; also `close(fd 3..127)` defensively at the start of `main()`.
3. **Cold-start stale-socket misjudgement**: after a reboot the `/data/local/tmp/root_daemon.sock` "file" remains but has no
   listener, so `startRootDaemon`'s `[ ! -S ... ]` test misjudged it and skipped startup.
   → Fix: use `grep -q root_daemon.sock /proc/net/unix` to decide whether a socket is "alive" (`/proc/net/unix`
   lists only sockets that have a listener); only `rm -f` and start when none is alive.

### Q5: persist.adb.tcp.port 5555 (feasibility of persistent adb-over-tcp)

- As root, `setprop persist.adb.tcp.port 5555` → **after a restart `getprop` still returns 5555** (persist properties do persist).
- However, **adbd did not bind TCP 5555 after the restart** (`/proc/net/tcp` only shows the vendor listener on 0.0.0.0:8341).
- Conclusion: adbd in this Rokid build does not honour `persist.adb.tcp.port`, so this "persistent adb-over-tcp" trick is **not feasible**
  (it would need `service.adb.tcp.port` + an adbd restart, and service.* is lost on restart). The leftover property was cleared after validation.

### Operating model (updated)

- After privilege escalation, the root daemon and helper live only in memory and **disappear after a device reboot**, so you must re-run:
  `deploy/root_bridge.sh` (restart the helper) → trigger `ACQUIRE_ROOT` once (or manually push
  `deploy/trigger_root_daemon.cmd` to `ghostlock_bridge/last.cmd`).
- Each successful escalation leaves behind an idle `ghostlock_rv101` root process (the exploit's own root child),
  which can be cleared with `kill -9 <pid>` over the socket.

## OTA version-check API: signature cracking and an end-to-end "check for a new version → download → compare SPL" pipeline (2026-09)

### Background

To determine whether new Rokid firmware patches GhostLock, the `post-security-patch-level` of the latest OTA is needed.
The version-check API `ota.rokid.com/v1/extended/ota/check` of the OTA app (`com.rokid.glass.ota`)
originally required a "device authentication signature"; calling it directly only returned `C100401 认证不通过`.

### Frida hook process (on device)

- frida-server 17.17.0 was deployed to the glasses (root) and connected through a WSL2→Windows relay + adb forward.
- Attached to `com.rokid.glass.ota` and called `CheckService.checkNewOtaVersion()` directly to trigger the check.
- Captured the request URL `https://ota.rokid.com/v1/extended/ota/check` and the request parameters in logcat:
  `version=1.0;time=<ts>;sign=<md5>;key=...;device_type_id=...;device_id=...;service=ota`.
- Discovered that the correct broadcast extra key is `normal_cmd` (not `cmd`/`NORMAL_CMD_PARAM`).
- **Important**: the glasses have **no network** (`UnknownHostException`), so the hook cannot get a response; the response has to be issued from the **host**.

### Signature cracking (decompiling CheckService with jadx)

`CheckService.generateSignature()`:
```java
sign = MD5("key="+key+"&device_type_id="+deviceTypeId+"&device_id="+deviceId
         +"&service="+service+"&version="+version+"&time="+time+"&secret="+secret)
Authorization = "version="+version+";time="+time+";sign="+sign
              +";key="+key+";device_type_id="+deviceTypeId+";device_id="+deviceId+";service="+service
Body = {"version": <systemVersion>, "osType": "", "cpuType": ""}
```
Parameter sources (`DeviceProp.init()` reads system props):
| Field | Source | Value |
|------|------|-----|
| key | `ro.boot.key` | `<REDACTED_OTA_KEY>` |
| secret | `ro.boot.secret` | `<REDACTED_OTA_SECRET>` |
| deviceTypeId | `ro.boot.devicetypeid` | `<REDACTED_OTA_DEVICETYPEID>` |
| deviceId | `ro.serialno` | `<DEVICE_SERIAL>` |
| version | hard-coded | `1.0` |
| service | hard-coded | `ota` |

Verified with the known `time=1788364983`: the computed MD5 = `99d904b1e9e94d1a85d2a3d3fda51611`, matching logcat ✅.

### Query result (2026-09-03)

- Device currently on: `1.22.009-20260710-150201` (SPL `2024-07-05`).
- **Latest version: `1.25.012-20260901-150201`** (full package ~1GB, MD5 `bec406d9e28ee779d8e876e57ba69f51`).
- Latest package metadata: `post-security-patch-level = 2024-07-05` — **exactly the same as the old version, unchanged**.

### Conclusion (the misjudgement from looking at the SPL first)

- **The SPL is unreliable**: the `post-security-patch-level` of 1.25.012 is still `2024-07-05` (the same as 1.22.009),
  which looks like "not patched". But the SPL only reflects the Android security bulletin level; **it does not mean the kernel
  has not backported an individual CVE fix**.
- Measured (see the next section): **the 1.25.012 kernel has backported the CVE-2026-43499 (GhostLock) fix**.

### Kernel-level verification: disassembling remove_waiter (important)

GhostLock itself is a UAF in `remove_waiter()` in `kernel/locking/rtmutex.c`. The official fix
(commit `3bfdc63`, "rtmutex: Use waiter::task instead of current in remove_waiter()")
changes `current` to `waiter->task`.

- Used `payload-dumper-go` to unpack `payload.bin` → `boot.img` → `Image`, `vmlinux-to-elf` to restore symbols,
  and `capstone` to disassemble `remove_waiter`.
- Decision criteria (ARM64):
  - `mrs x?, sp_el0` (accessing `current`) → **vulnerable version**.
  - `ldr x?, [waiter, #0x30]` (loading `waiter->task`) → **patched version**.
  - ⚠ **2026-09-16 correction**: you must not search for `#0x30` alone. Both versions have a
    `ldr x14, [x9, #0x30]` at `remove_waiter()+0x160` (base `x9`, unrelated to `waiter->task`) → looking only at that
    misjudges the build as "patched".
    The correct criterion is to see what the `x20` loaded at `+0x040` feeds (`+0x044` pi_lock / `+0x088` pi_blocked_on /
    `+0x210` task argument).
- **1.25.012 measured**: inside `remove_waiter`, `x20 = ldr [waiter,#0x30]` (= `waiter->task`),
  `raw_spin_lock(&x20->pi_lock)`, `str xzr,[x20,#0x888]` (`pi_blocked_on=NULL`),
  `rt_mutex_adjust_prio_chain(..., x5=x20)` — `waiter->task` throughout, no `mrs sp_el0` → **patched**.
- For comparison: the device's current 1.22.009 is the vulnerable version (`ghostlock_rv101` has escalated successfully
  many times). After upgrading to 1.25.012, `ghostlock_rv101` (the CVE-2026-43499 exploit) should stop working.

**Corrected conclusion: Rokid 1.25.012 (2026-09-01) patches GhostLock (CVE-2026-43499); the SPL is unreliable,
and confirming it requires disassembling `remove_waiter`.**

**2026-09-16 independent re-check (same toolchain for both versions, byte by byte)**: `remove_waiter()` is `0x28c`
(163 instructions) in both 1.22.009 and 1.25.012, and the machine code differs in **only one 4-byte instruction at `+0x040`**
(`mrs x20, sp_el0` → `ldr x20, [x23, #0x30]`); the other 162 instructions (including all call targets) are identical.
Tools: `scripts/ota_kernel_diff.py`; OTA hashes for the two versions: 1.22.009 SHA-256 `163ab205…c94e5` (consistent with the
retained list), 1.25.012 MD5 `bec406d9…f51` (consistent with the vendor OTA metadata).

### Tools

- `deploy/check_ota.sh` (default) = end-to-end: read the device + query the latest using the cracked signature → download → compare SPL.
- `deploy/ota_api.py`: standalone Python query tool (the environment variables `KEY/SECRET/DTID/DID/SYSTEM_VERSION` can override).
- `deploy/ghostlock_patch_check.py`: **check latest → unpack payload.bin → boot.img → Image → disassemble
  `remove_waiter` → compare against the official patch → fixed-format report** (`ghostlock_patch_report.txt`).
- Decompiled sources: `/tmp/rokid_ota/jadx_out/` (`CheckService.java`, `DeviceProp.java`, `MD5Utils.java`).

## Stability fixes for phone-driven glasses privilege escalation + "adb-free" feasibility conclusion (2026-09)

### Root cause of the escalation "crash" and the fix

The main reason "phone App → glasses App → escalation" crashed is that **every press of acquire-root re-ran the exploit
(ghostlock_rv101 reboots the kernel ~50% of the time)**, combined with the Rokid screen sleeping after 5 seconds and
killing the app/service.

Fixes (built and installed; **it is now a runtime auto-probing engine**, so this switch is not needed):

1. **Skip re-running the exploit when already root**: `RootDaemonClient.isRoot()` (connects to the socket and runs `id` to confirm uid=0);
   `BridgeGhostlockEngine.acquireRoot()` / `ShellGhostlockEngine.acquireRoot()` check first and return success directly if already root.
2. **Keep the screen on during escalation**: `CaptureService` adds `SCREEN_BRIGHT_WAKE_LOCK`, `MainActivity` adds
   `FLAG_KEEP_SCREEN_ON`.
3. **Persist root mode**: `AppGraph.rootMode` is stored in SharedPreferences (`setRootMode()`); setting AUTO on the phone
   survives a restart.
4. **Auto-start the app at boot**: added `BootReceiver` (`BOOT_COMPLETED`) to start `CaptureService` automatically.

### Operating model (unchanged)

- **adb-free within the same boot**: the helper is already persistent thanks to `deploy/root_bridge.sh` → phone connects to
  glasses → triggers `ACQUIRE_ROOT` (or AUTO) → glasses App writes `last.cmd` → helper runs the exploit → root. No desktop
  adb is needed at any point.
- **Across a reboot, one adb session is still required**: the helper lives in memory and disappears after a reboot, so
  `deploy/root_bridge.sh` must be re-run once (in addition to the glasses App already being installed and
  `ghostlock_rv101`/`root_daemon` already being in `/data/local/tmp`).

### Why "helper auto-start at boot / fully adb-free" is not achievable (measured)

1. **The traditional `install-recovery.sh` hook is blocked by dm-verity**: the only hook is
   `/vendor/etc/init/vendor_flash_recovery.rc` → `/vendor/bin/install-recovery.sh`, but `/vendor`
   is dm-verity read-only (`mount -o remount,rw` returns read-only). `/system`, `/product` and `/odm` are read-only as well.
2. **No Magisk**: `/data/adb/` is empty and there is no `service.d/`. `magisk_patched-30600_9C56p.img`
   is just a patched boot.img and was never actually flashed.
3. **The symlink trick (App connecting directly for escalation, without the helper) failed**: after pointing `.ghostlock_root.sh`
   at a symlink into a 777 directory, the App really could write the payload (`setup_direct.sh` measured the target becoming
   `u0_a53`, 830B), but **running the exploit as `untrusted_app` (uid 10053) causes a kernel panic and a reboot** — it needs
   `shell` (uid 2000) privileges (`perf_event_open` and others).

### The only path to "fully adb-free" (not adopted)

Flash Magisk (using the existing `magisk_patched-30600_9C56p.img`, `dd` to the boot partitions `mmcblk0p17/18`),
then place a boot script in `/data/adb/service.d/` to start the helper. This carries a bricking risk and would only be
done with the user's confirmation.

---

## 2026-09-10 / 09-11 follow-up measurement appendix

> The content from this section onward is measurement addenda from the App productisation phase; §1 through the end
> above is the historical record, preserved as it was.

### A. The Shizuku path became the default (validated on device)

- Shizuku server (started over adb) → UserService identity = `uid 2000` + `u:r:shell:s0` (equivalent to adb shell),
  which can run `ghostlock_rv101` directly. Measured `auto acquireRoot: success=true` (fastest run 9 seconds).
- See `docs/shizuku-feasibility-report.md` and `reports/shizuku_integration_test_report.md` for details.

### B. WiFi media channel (SoftAP + TCP 8555)

- After the glasses are escalated, `cmd wifi start-softap RokidCam wpa2 rokid1234` → `wlan2` = `192.168.43.1`;
  `WifiServer` listens on TCP 8555 and **shares the same `ConnectionHandler`/`CommandRouter`** as Bluetooth.
- Phone side: `TcpClient` (WiFi) plus `BluetoothClient` (fallback): measured thumbnail transfer about 1.2s and a 5.4MB
  photo downloaded successfully; Bluetooth fallback is about 63KB/s. See `reports/wifi_transfer_test_report.md` for details.
- On this device's firmware, **STA and SoftAP are mutually exclusive**: starting the hotspot kicks off STA, and STA
  recovers automatically after `cmd wifi stop-softap`.

### C. Permanent fix for the privacy LED (flashing during video recording)

- The light-up source is the camera-open event in `lights_ctrl` (Binder `sendEvent(2,2014)`); writing sysfs alone races
  with it → the LED flashes during recording. Suppression was changed to `cancelEvent(2,2014)` (at the Binder layer) and
  the suppress was moved before `bindVideo()`; a 20-second recording sampled every 100ms measured **200/200 all zero**.
  See `reports/RV101_CAMERA_LED_FLASH_MECHANISM.md` §9 for details.

### D. Wireless adb (adb-over-TCP) is **feasible** — this section's original conclusion was overturned on 2026-09-20

> ⚠ **Correction**: the original "not feasible" judgement below was a **bug in the detection method**, not a platform
> limitation. The glasses adbd **does bind TCP 5555, at the IPv6 wildcard `[::]`** (visible only in `/proc/net/tcp6`),
> while the old detection code only grepped the IPv4 `/proc/net/tcp` → it always counted 0.
> End-to-end test on 2026-09-20: after the PC joined the SoftAP, `adb connect 192.168.43.1:5555` → `device`,
> `adb shell id` → `uid=2000(shell) context=u:r:shell:s0`.
> Full evidence: [`reports/wireless_adb_feasibility_2026-09-20.md`](../reports/wireless_adb_feasibility_2026-09-20.md);
> document corrections: `docs/deploy.md` §9.8／§P3, `reports/FACTS.md` R7.

- Original record: after root, `service.adb.tcp.port 5555` + **restarting adbd** → TCP 5555 refused connections, and
  **USB enumeration was lost**. **The "lost USB enumeration" was a side effect of restarting adbd itself** (on this device
  adbd is started the vendor way and pulls up USB FunctionFS by itself), unrelated to the TCP binding; **the correct
  approach is not to restart adbd**.
- `ENABLE_WIFI_ADB` was changed to be **non-destructive** (it only sets the property and detects; it does not restart
  adbd) — that decision was right, but the **detection logic** must check IPv4 and IPv6 together (fixed in `1.0-safe.4`;
  the ACK additionally returns `boundOn`).
- **Cable-free root is no longer limited to Magisk**: wireless adb works, so after acquiring root, setting
  `persist.adb.tcp.port 5555` (this device was already 5555) makes it listen on TCP automatically on **every boot**, and
  re-arming escalation (`deploy/root_bridge.sh`) no longer needs a USB cable; the only limitation is **first-time
  authorisation of a new host**, which requires writing the public key into `/data/misc/adb/adb_keys` as root (the glasses
  have no screen for a popup).

### F. Suppressing the photo/video camera tone (silent capture, 2026-09-11)

- **Sound source**: `libcameraservice.so` plays `camera_click.ogg` (photo), `VideoRecord.ogg`/`VideoStop.ogg` (video) and
  `camera_focus.ogg` (focus) from `/product/media/audio/ui/` itself, plus Rokid's custom
  `rkd_record_start.ogg`/`rkd_record_end.ogg` (6 files in total).
  `strings /system/lib64/libcameraservice.so | grep -E 'camera_click|VideoRecord'` reproduces this;
  `getprop` on this unit does not set `ro.camera.sound.forced` (even if other production units set it to 1, the
  file-level override still works).
- **Suppression method**: `/product` is read-only (dm-verity) → as root, `mount --bind` a legitimate silent OGG
  (generated with ffmpeg, Ogg Vorbis mono 44.1kHz 0.3s / 3584 B) over those 6 files; when the playback side reads silent
  content there is no sound. `umount` restores it, and **a reboot clears it automatically**.
- **Measured**: after suppression, the md5 of `camera_click.ogg` went `a3fc812e…` → `d7d0ebd6…` (= the silent file), with
  6 mounts; in the silent state, photo/video/stop all worked normally (`result=true`) with no `MediaPlayer` errors; after
  umount the md5 was restored and 0 mounts remained.
- **App integration**: glasses-side `SilentMode`/`SilentPayloads` (applied automatically after escalation, enabled by
  default), the `SET_SILENT` protocol command, and an "Advanced" card toggle on the phone. See
  `reports/silent_capture_mechanism.md` for details.

### E. Other measurement-driven fixes

- `am start-foreground-service` is blocked by the background-start restriction on Android 12 (`app is in background`) →
  switched to `am start -n .MainActivity` (starting the FGS from the foreground).
- When `RootDaemonClient.exec` hit a stale socket after a reboot (`Connection refused`), the App used to go FATAL →
  changed to never throw (it returns an empty string on failure).
- On the glasses, `input` injection (tap/keyevent) is high-risk: it once caused a full device reboot in testing;
  automation now always uses `DebugReceiver` broadcasts (`com.rokid.rc.glasses.DEBUG`).
- 2026-09-11: the Shizuku manager once disappeared from the device (cause unknown; the rest of `/data` was intact); after
  re-running `adb install` the escalation flow worked normally again.
