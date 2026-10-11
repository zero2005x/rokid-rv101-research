# Latest Research Compilation for v2.0

> 中文版：[research-update.md](research-update.md)

> Source: `/home/kali/rokid_root` (analysis date 2026-09-11)
> This document records only the conclusions **relevant to the desktop tool**, and
> how each one is implemented in v2.0.

## 1. Vulnerability and Firmware Status

| Item | Details |
|---|---|
| CVE | **CVE-2026-43499** (GhostLock), CWE-416 kernel stack use-after-free |
| Location | `remove_waiter()` of futex-PI / rt-mutex — it clears the wrong `pi_blocked_on`, leaving a dangling `rt_mutex_waiter` |
| Upstream patch | mainline `3bfdc63936dd` (rtmutex: Use waiter::task instead of current in remove_waiter()) |
| Affected range | 2.6.39 – 7.1 |
| **Vulnerable firmware** | `1.22.009-20260710-150201` (Android 12 / SDK 32, kernel 5.10.209) |
| **Patched firmware** | `1.25.012-20260901-150201` (pushed 2026-09-01) |
| Patch evidence | Disassembly confirms that `remove_waiter` contains `ldr x20,[waiter,#0x30]` (= `waiter->task`) and **no** `mrs sp_el0` |
| Success rate | **about 50% per boot**; failure = kernel panic → automatic reboot after 2–5 minutes, with the KASLR slide changed |
| root persistence | **exists only in memory**, and disappears on reboot (files in `/data/local/tmp` are preserved) |

### ⚠ Two Critical Pitfalls

1. **The security patch level (SPL) cannot be trusted.** The SPL of 1.25.012 is
   still `2024-07-05`, the same as the vulnerable version.
   **You cannot use the SPL to determine whether the device is patched.**
2. **Upgrading is irreversible.** A/B anti-rollback, no public downgrade package,
   and no firehose over EDL. Once you upgrade to 1.25.012 you permanently lose
   this vulnerability.

> **Implemented in v2.0**: `fw_classify()` decides based on the version number in
> `ro.build.version.incremental` (threshold `1.25.012`) rather than the SPL; when
> the device is judged patched, "one-click acquire root" refuses to run outright.
> `LOG_OTACHECK_SPL` also states plainly in the UI that "this field is unreliable".

---

## 2. Paths to Acquiring Root

| Path | Prerequisites | Reliability | Survives reboot | Needs a PC |
|---|---|---|---|---|
| **A. Bridge helper** (classic) | adb + exploit already pushed | typically 1–2 boots | ❌ | ✅ |
| **B. Shizuku** (new default engine inside the app) | manager APK v13.6.0+, `pm grant` | 2 min 6 s measured on device, succeeded on the 1st attempt | ❌ (must be re-run every boot) | ✅ (or phone OTG) |
| **C. Magisk** (usable but not enabled) | unlocked bootloader | 100% deterministic | ✅ | only needs flashing once |
| **D. Phone as adb host** | termux-adb + OTG | usable | ❌ | ❌ |

Ruled out: adb-over-TCP (this adbd ignores `persist.adb.tcp.port`, and restarting
adbd **loses USB enumeration**), `install-recovery.sh` (dm-verity), and running the
exploit directly from the app (guaranteed panic).

> **Implemented in v2.0**: paths A (the main flow) and **B (the "Start Shizuku"
> button)**. Following `deploy/start_shizuku.sh`: confirm / install the manager →
> `pm grant com.rokid.rc.glasses moe.shizuku.manager.permission.API_V23` → derive
> `lib/arm64/libshizuku.so` from `pm path` → `pkill` the old server → launch →
> verify with `ps -A -o ARGS | grep -c shizuku_serve[r]`.

---

## 3. Bridge / helper Mechanism (Identical to the v2.0 Implementation)

**On-device paths**

```
/data/local/tmp/ghostlock_rv101          0755  exploit
/data/local/tmp/root_daemon              0755  root resident service
/data/local/tmp/root_daemon.sock         0777  UNIX socket
/data/local/tmp/gl_run.log                     exploit output
/data/local/tmp/.ghostlock_root.sh             exploit default payload (ready flag)
/data/local/tmp/ghostlock_bridge_helper.sh     helper resident loop
/data/local/tmp/ghostlock_bridge/        0777  mailbox directory
    last.cmd  last.out  last.done  last.progress  payload.staging
```

**helper loop**: on detecting `last.cmd` → clear out/done/staging →
`echo started > last.progress` → `cp last.cmd payload.staging` → up to 3 attempts:
- run the exploit (environment variables `GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 W2_ATTEMPTS=1`)
- wait up to **120 seconds** for the ready flag `dropping to root shell` (give up early on `FOPS install failed`)
- copy the payload over `.ghostlock_root.sh` → wait up to **200 seconds** for `__GHOSTLOCK_PAYLOAD_DONE__`
- on success write `OK` to `last.out`; on failure write `ERR:<reason>`
- worst case for a single boot is about **640 seconds**

**payload**: `echo 0 > /sys/fs/selinux/enforce`; if `/proc/net/unix` does not have
`root_daemon.sock`, then `setsid root_daemon &`; once the socket appears, write
`id` into `last.done`.

> ⚠ **You must use `/proc/net/unix` to determine whether the daemon is alive**, not
> whether the socket *file* exists: that file remains after a reboot, but there is
> no longer any listener behind it.

**socket protocol** (`deploy/root_daemon.c`): AF_UNIX SOCK_STREAM, `CMD_MAX 8192`,
**one connection = one command, read until EOF**, handed to `/system/bin/sh -c` for
execution, with stdout written back; each connection forks one handler, and the
child closes fd 3..127 (so that backgrounded commands do not wedge the daemon).

> **Implemented in v2.0**: the `HELPER_SCRIPT` and `TRIGGER_SCRIPT` constants
> correspond word for word to the scripts above.
> `nc -U /data/local/tmp/root_daemon.sock` with the command fed on stdin, the same
> as `deploy/run_root.sh`. The mailbox directory is 0777 and files are 0666;
> `last.cmd` now **writes `.tmp` first and then `mv`s it (atomic)**.
> `pkill` now uses character classes throughout (`ghostlock_rv10[1]`,
> `ghostlock_bridge_helpe[r]`, `shizuku_serve[r]`) to avoid `pkill -f` matching
> itself.

---

## 4. Verifying root

```bash
printf 'id; getenforce\n' | adb shell "nc -U /data/local/tmp/root_daemon.sock"
```

The expected output is **exactly**:

```
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
Permissive
```

Secondary evidence: `grep -c root_daemon.sock /proc/net/unix` = `1`; `last.done`
contains `uid=0(root)` and `__GHOSTLOCK_PAYLOAD_DONE__`.

> **Implemented in v2.0**: `do_verify()` first confirms the daemon is alive
> (`/proc/net/unix`) and only then sends the command, judging success or failure by
> whether the output contains `uid=0`.

---

## 5. Telemetry Fields

The phone app's `GET_DEVICE_INFO` only has `selinux / id / uid / rootAcquired /
ghostlockEnabled / rootMode / wifiAp`, and **does not include serial number / model
/ firmware**. The equivalent commands on the PC side are:

```
adb devices -l
getprop ro.serialno | ro.product.model | ro.build.version.incremental
        | ro.build.version.security_patch | ro.build.version.release
        | sys.boot_completed | ro.boot.glassesWithPanel
id; getenforce
grep -c root_daemon.sock /proc/net/unix
ps -A -o ARGS | grep -c shizuku_serve[r]
cut -d' ' -f1 /proc/uptime
```

> **Implemented in v2.0**: merged into **a single** `adb shell`, returning tagged
> `KEY=value` pairs that are then parsed, avoiding multiple round trips and also
> avoiding parsing ambiguity.

---

## 6. OTA Patch-Check Logic

**API**: `POST https://ota.rokid.com/v1/extended/ota/check`

```
Body:         {"version":"<ro.build.version.incremental>","osType":"","cpuType":""}
Content-Type: application/json;charset=utf-8
Authorization: version=1.0;time=<unix>;sign=<md5>;key=<K>;device_type_id=<T>;device_id=<D>;service=ota

sign = MD5("key=<K>&device_type_id=<T>&device_id=<D>&service=ota&version=1.0&time=<ts>&secret=<S>")
```

`K` / `S` / `T` come from the device properties `ro.boot.key` / `ro.boot.secret` /
`ro.boot.devicetypeid`, and `D` comes from `ro.serialno` (`deploy/ota_api.py`
contains hard-coded defaults). Response fields: `code` (`"OK"`), `version`,
`imageUrl`, `checksum`, `changelog`, `isForceUpdate`.

Two decision paths:

| Path | Method | Reliability |
|---|---|---|
| Cheap | Download the OTA zip → read `post-security-patch-level` in `META-INF/com/android/metadata` | **Low** (the SPL is known to lie) |
| Authoritative | Unpack `payload.bin` → dump `boot` → gunzip → `vmlinux-to-elf` → find `remove_waiter` with `nm` → capstone ARM64 disassembly, checking for `mrs sp_el0` / `ldr [x,#0x30]` | High |

> **Implemented in v2.0**: the **upper layer** of the cheap path is implemented
> (query the API directly to obtain the latest version number, then compare it with
> the threshold `1.25.012`), and the UI states plainly that the SPL is unreliable.
> The disassembly path is **deliberately not implemented** — it would require
> DEFLATE, ELF symbol recovery and an ARM64 decoder, which do not fit inside a
> single Win32 program. When an authoritative determination is needed, run
> `rokid_root/deploy/ghostlock_patch_check.py`.

---

## 7. Windows Automation Pitfalls (Handled in v2.0)

| Pitfall | How it is handled |
|---|---|
| the exploit can only run as uid 2000 | go through `adb shell` throughout (= uid 2000); do not trigger it from the app |
| about 50% panic → reboot (2–5 min) | before retrying, wait for `sys.boot_completed=1` **and** uptime > 150 seconds |
| reboot detection | **uptime going backwards** (`/proc/uptime` decreasing by > 5 seconds) is more reliable than "device offline" |
| the system is busy after boot | when the thread count (4th field of `/proc/loadavg`) > 2500, wait one more round |
| USB re-enumeration | poll `adb devices` with backoff; **never restart adbd** (this build loses USB enumeration) |
| multiple devices | every adb call carries `-s <serial>` |
| `pkill -f` self-matching | character classes: `ghostlock_rv10[1]`, etc. |
| reading a half-written payload | `last.cmd` is written atomically with `.tmp` + `mv` |
| the app cannot write into `/data/local/tmp` (0771) | mailbox directory 0777, files 0666 |
| leftover socket file | use only `/proc/net/unix` to judge liveness |
| cleanup is needed before every trigger | clear `last.*` / `payload.staging` / `.ghostlock_root.sh` / `gl_run.log` and kill leftover exploits |
| Android 12 background-start restrictions | `am start-foreground-service` fails; `MainActivity` must be used instead |
| lmkd killing the helper on a 1.8 GB device | if `last.progress` is stuck, re-run "② Install helper" |
| `pm disable-user` unsupported | use `pm suspend` instead (`pm disable-user` returns `Failure [not support for 0]`) |

---

## 8. Other Things That Can Be Done from the PC but That v2.0 Deliberately Does Not Do

| Capability | Why it was not done |
|---|---|
| WiFi media channel (SoftAP `RokidCam` / `192.168.43.1:8555`) | unrelated to the root flow; moreover, enabling the AP requires root, and STA/SoftAP are mutually exclusive |
| album / media pull (app-private directories) | requires root and is low priority |
| glasses display / locale broadcasts (`DebugReceiver`) | only `TOGGLE_PANEL` / `SET_DISPLAY` / `SET_LOCALE`, unrelated to root |
| OTA image disassembly determination | see §6; the engineering effort does not suit a single Win32 program |
| `input tap/keyevent` automation | ⚠ measured on the glasses, it causes the whole device to reboot; **never use it** |

---

## 9. Correspondence with `rokid_root`

| This tool | Corresponding project script |
|---|---|
| ① Push exploit | `adb push ghostlock_rv101` + `root_daemon` |
| ② Install helper | the helper install / start portion of `deploy/root_bridge.sh` |
| ③ Trigger privilege escalation | `deploy/trigger_root_daemon.cmd` → `last.cmd` |
| 🚀 One-click acquire root | `deploy/get_root_now.sh` (`MAX_BOOTS=6`, `TRIGGERS_PER_BOOT=2`, `ROUND_TIMEOUT_S=950`) |
| ✓ Verify root | `deploy/run_root.sh 'id; getenforce'` |
| 🔒 Lock OTA | `deploy/lock_ota.sh` |
| OTA patch check | `deploy/ota_api.py` (upper layer) / `deploy/ghostlock_patch_check.py` (authoritative version) |
| Start Shizuku | `deploy/start_shizuku.sh` |
| Silent capture | `reports/media/silent_camera.ogg` + bind mount |
| Privacy LED suppression | the Binder + sysfs approach in `reports/RV101_CAMERA_LED_FLASH_MECHANISM.md` |
