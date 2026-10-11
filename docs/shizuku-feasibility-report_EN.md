# Shizuku × Rokid RV101: Feasibility Report on Integrating GhostLock Privilege Escalation into the Glasses-Side App

> 中文版：[shizuku-feasibility-report.md](shizuku-feasibility-report.md)

> Date: 2026-09-09 (final)
> Applicable device: Rokid RG-glasses (RV101, serial <DEVICE_SERIAL>), Android 12, kernel 5.10.209-perf, firmware 1.22.009 (GhostLock vulnerable version)
> Purpose: security research on owned/authorised devices.
> ✅ **Validated on device**: every option in this report has been developed and passed real-device testing (E2E privilege escalation, fallback, phone integration);
> see `reports/shizuku_integration_test_report.md`.

## 0. TL;DR (conclusion)

**Suitable, but its role is an "App-side execution channel", not a "bootstrap/persistence solution".**

1. **The core chain is empirically feasible**: once the Shizuku server is started via adb shell, the App runs `ghostlock_rv101` through Shizuku (UserService, shell uid 2000 + `u:r:shell:s0`) with an identity **exactly equivalent to adb shell** → this satisfies the exploit's hard requirement of starting as shell; it cannot use the App uid (that causes a kernel panic).
2. **It cannot eliminate adb**: the Shizuku server itself needs one adb/shell launch per reboot (adb mode has no auto-start at boot on Android 12); only Magisk gives complete freedom from adb (§5, priority 1).
3. **Low integration cost**: the `GhostlockEngine` interface already exists, so adding a Shizuku engine is enough; api 13.1.5 uses `bindUserService` + custom AIDL (the aak pattern); pre-authorisation can use `pm grant moe.shizuku.manager.permission.API_V23` or write `shizuku.json` directly to skip the UI.
4. **OOM risk is manageable**: the server is a shell/root-class process, so the lmkd risk is lower than for an App; add oom_score_adj protection + a binder-dead listener.
5. **The urgent task is not Shizuku but locking down OTA**: upgrading to 1.25.012 irreversibly patches GhostLock (`deploy/lock_ota.sh` is ready).
6. Recommended path: **lock OTA → phone as adb host (first) → Shizuku engine integration → Magisk if necessary (ultimate)**.

## 1. Current architecture (baseline, already proven)

| Element | Current state |
|------|------|
| exploit execution identity | **shell (uid 2000)** only; running it directly from the App (untrusted_app uid 10053) causes a kernel panic (it needs shell privileges such as `perf_event_open`) |
| Trigger method | host adb starts the shell helper `/data/local/tmp/ghostlock_bridge_helper.sh` (deployed by `deploy/root_bridge.sh`, resident under setsid) |
| App↔helper communication | `/data/local/tmp/ghostlock_bridge/` (777): `last.cmd` (the App writes the payload) / `last.out` / `last.progress` |
| exploit behaviour | `ghostlock_rv101` (with env `GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 W2_ATTEMPTS=1`) writes `.ghostlock_root.sh` (containing the ready marker `dropping to root shell`) ~2s after start, and runs that script as root after 100–142s |
| payload | the helper atomically replaces `.ghostlock_root.sh` with `cp`; the payload (`LedPayloads.startRootDaemon`) turns SELinux off (`echo 0 > /sys/fs/selinux/enforce`) → starts `root_daemon` (UNIX socket 0777) → the App runs commands as root over the socket |
| Identity after escalation | `uid=0(root) gid=0(root) context=u:r:kernel:s0` + Permissive |
| Persistence | root/helper/daemon are all memory-resident and **are entirely lost on reboot**; crossing a reboot needs one host adb run of `root_bridge.sh` |
| Known pain points | the exploit causes a kernel panic→reboot ~50% of the time; the helper is occasionally killed by lmkd/OOM; the App cannot write to `/data/local/tmp` (0771) |
| Rejected paths | the symlink trick (`setup_direct.sh`) solves payload writing but not the exploit's starting identity; the install-recovery.sh hook is blocked by dm-verity; `persist.adb.tcp.port` has no effect on this build |

## 2. Shizuku's role on this device

Shizuku = a framework that "lets an ordinary App use **adb/root-level identity** through binder". Its
value for this project is as a **channel** to run the exploit as shell — replacing the helper loop (a
shell script + file mailbox) with direct Shizuku API calls inside the App. It is not the exploit
itself, and it **does not produce root by itself**; the Shizuku server still has to be started with
adb or root.

## 3. Feasibility analysis

### 3.1 Platform compatibility (confirmed)
- Android 12 / SDK 32 ✓ (Shizuku supports API 23+); aarch64 ✓; no Google Play → sideload the manager APK.
- Latest stable versions on Maven Central: `dev.rikka.shizuku:api:13.1.5`, `dev.rikka.shizuku:provider:13.1.5`.
- app-glasses: minSdk 26 / targetSdk 34 / Kotlin+coroutines; just add the dependencies.

### 3.2 Execution identity (decisive; confirmed from the source, see `research/shizuku/shizuku_internals.md` §2)
- The server's identity = the launcher's identity (`OsUtils.getuid()` + `SELinux.getContext()` cached).
- The child processes of `newProcess` (server side `Service.java:306-323` = `Runtime.exec`) and of rish (native fork+exec) **inherit the server's uid + SELinux context, with no setuid/setcon**.
- UserService is likewise started by `Runtime.exec("sh")` inside the server process with no setuid (`--uid` only counts as a userId) — **it is also the server's identity**.

| Launch path | server identity | Child process (running the exploit) identity |
|---------|------------|------------------------|
| **Started via adb shell** | uid 2000, `u:r:shell:s0` | **uid 2000, `u:r:shell:s0` = exactly equivalent to adb shell ✅** |
| Started as root (this device) | uid 0, `u:r:kernel:s0` | uid 0, kernel context ❌ (the exploit must start unprivileged) |

- **Conclusion**: to run `ghostlock_rv101`, the Shizuku server **must be started via adb/shell**; a server started as root is suitable for running root commands but not for running the exploit.

### 3.3 Server launch path (v13.6.0, confirmed)
- **There is no start.sh from v13.6.0 onwards** (older documentation is out of date): the launcher is the native `libshizuku.so` inside the manager APK.
  The command = `adb shell <nativeLibraryDir>/libshizuku.so` (starter.cpp checks that the uid must be 0 or 2000; the root version adds `--apk=`).
  nativeLibraryDir can be derived with `pm path moe.shizuku.privileged.api`.
- The server is a bare `app_process --nice-name=shizuku_server` and **does not survive a reboot**; if killed there is no watchdog, and without adb/root it cannot heal itself.
- Automatic restart at boot: in root mode `su -c` can run automatically on BOOT_COMPLETED; automatic restart in adb mode only exists on Android 13+ and requires WRITE_SECURE_SETTINGS → **on this device (Android 12), adb mode needs a manual launch after every reboot**.
- The manager APK must be installed (the server class is packed inside it, `moe.shizuku.privileged.api`, minSdk 24, no GMS dependency, sideloading is enough).

### 3.4 Authorisation UX and pre-authorisation (confirmed, highly feasible)
- Authorisation in v13 is not SQLite and **not a binder token**: it is persisted in **`/data/user_de/0/com.android.shell/shizuku.json`** (`ShizukuConfigManager.java:42`):
  `{"version":2,"packages":[{"uid":10053,"flags":2,"packages":["com.rokid.rc.glasses"]}]}` (FLAG_ALLOWED=2).
  A server restart does not invalidate it; the client authorisation is rebuilt from the config.
- **Two routes to pre-authorise without the UI (both doable with root_daemon)**:
  1. `pm grant com.rokid.rc.glasses moe.shizuku.manager.permission.API_V23` (this permission has protectionLevel=dangerous, so `pm grant` works; when the server starts it writes the config back according to the runtime permission state);
  2. write shizuku.json directly (**write the file before starting the server**, to avoid the server's delayed 10s write overwriting it).
- The normal UI flow (the manager pops up a confirmation box and you tap allow) also works; if the glasses UI is operable, it only hurts once.

### 3.5 OOM/lmkd risk (confirmed)
- The server runs as shell/root and is not in an app's oom_adj class → **the risk of being hunted down by lmkd is lower than for an ordinary App, but not zero**; when started as root the starter switches cgroup (`switch_cgroup()`), and when started via adb it does not.
- There is no foreground-service mechanism (bare app_process). Mitigations: as root, `echo -1000 > /proc/<pid>/oom_score_adj`; the App registers a binder-dead listener + re-pulls; a long-lived shell ELF must not be tied to the App's binder lifetime (when the owner binder dies, `RemoteProcessHolder` destroys the child process).
- It is officially known to disappear after a reboot (issue #363), and some OEMs kill the UserService (issue #2048) — the same class of problem as our helper being killed by lmkd, with no new risk.

### 3.6 Assessment of reusing the ghostlock-apk reference implementation (completed; see `research/shizuku/ghostlock_apk_analysis.md`)
- **`oopnv70-lab/ghostlock-apk` itself is unusable**: AI-generated; its STRUCTURAL_NOTICE admits that all 12 CI runs failed; it has been archived; there is no exploit binary, no offsets and no LICENSE (all rights reserved by default, so it cannot be copied); the Shizuku layer is missing the `provider` dependency and the manifest does not declare `ShizukuProvider` → it cannot obtain a binder.
- **The core chain is confirmed feasible**: when Shizuku is started via adb, the UserService runs inside the server process and executes `sh -c` as **shell uid 2000** — exactly the identity ghostlock_rv101 needs.
- **The best reference = the same author's `ghostlock-aak-apk` (GPL-3.0)**: correct ShizukuProvider configuration, `writeFile(byte[])` (writing a file + chmod as shell), `runCommandStream` (a oneway streaming callback with two threads reading stdout/stderr in parallel to prevent deadlock). The local copy is in `research/shizuku/`.
- **Differences from RV101 (8 places to change)**: swap in the ghostlock_rv101 ELF, add the provider dependency, add env passing (`GL_SETUP_ATTEMPTS`), streaming output + long timeouts (100–150s), read stdout/stderr in parallel, **keep the "wait for ready → replace `.ghostlock_root.sh`" order** (⚠ aak's "write the payload first" does not apply: our exploit writes its own default script ~2s after start, so writing first would be overwritten), add kernel-panic/reboot detection (compare boot timestamps), and retry on error.
- **Licensing**: aak is GPL-3.0 → merging and distributing it would require the whole package to be GPL; the recommendation is to use aak as a skeleton and **rewrite** the execution layer yourself (it needs major changes anyway), and to credit oopnv70-lab. For internal research only, without distribution, copyleft obligations are usually not triggered.

## 4. Integration design (final)

### 4.1 Gradle / Manifest (version facts verified)
- `implementation("dev.rikka.shizuku:api:13.1.5")` + `implementation("dev.rikka.shizuku:provider:13.1.5")` (Maven Central; api = MIT, Shizuku itself = Apache-2.0, so it can be embedded).
- Manifest: `<provider android:name="rikka.shizuku.ShizukuProvider" android:authorities="${applicationId}.shizuku" android:exported="true" android:multiprocess="false" android:permission="android.permission.INTERACT_ACROSS_USERS_FULL"/>` + `<uses-permission android:name="moe.shizuku.manager.permission.API_V23"/>`; `ShizukuProvider.enableMultiProcessSupport(true)`.
- ⚠ **In api 13.1.5, `Shizuku.newProcess` is already private+deprecated** (verified from the Maven sources jar); the public channel is `Shizuku.bindUserService(UserServiceArgs, ServiceConnection)` → **use UserService + custom AIDL**.

### 4.2 Execution-chain design (aak pattern, rewritten in-house)
- Declare a UserService in the App (custom AIDL: `runCommandStream(cmd, env, oneway callback)` + `writeFile(path, byte[])`), with `bindUserService(UserServiceArgs(component).daemon(true).version(1))`.
- The UserService calls `Runtime.exec(["sh","-c",cmd], env)` inside the **shell uid 2000 (adb-started server)** process: two threads read stdout/stderr in parallel, a oneway streaming callback (to prevent deadlock), and no hard timeout that would cut the process off.
- The state machine reuses the existing Bridge/Shell engine (PREFLIGHT/LAUNCH/READY/INSTALL/DONE):
  LAUNCH (the UserService runs `GL_SETUP_ATTEMPTS=1 … ghostlock_rv101`) → READY (poll for the ready marker) → INSTALL (replace via `cat > .ghostlock_root.sh` through the UserService, or symlink direct write) → DONE.
  ⚠ The order must not be reversed (the exploit writes its own default script ~2s after start).
- Added: record the boot timestamp before execution (`/proc/uptime` or a sys.boot_completed comparison) → compare afterwards to detect a kernel-panic reboot; retry on failure; **never exec the ELF directly from the App uid** at any point.
- AppGraph: ~~`USE_ENGINE` (Engine enum: SHIZUKU / BRIDGE / DIRECT) with three states~~ **(2026-09-13 update: changed to runtime auto-probing with `AutoGhostlockEngine`; Shizuku is detected with `pingBinder()` and the bridge with a helper heartbeat file; there is no longer a compile-time switch)**. `acquireRoot()` checks `RootDaemonClient.isRoot()` first.
- Keep-alive: the existing CaptureService foreground service + WakeLock is sufficient; add `Shizuku.addBinderDeadListener` to detect the server's death.
- **Implemented and validated on device**: `android/app-glasses/src/main/kotlin/com/rokid/rc/glasses/ghost/ShizukuGhostlockEngine.kt` + `CommandService.kt` + `aidl/` (historical design draft: `research/shizuku/ShizukuGhostlockEngine.draft.kt`, superseded by the production version).
- Four fixes from on-device testing (processNameSuffix is mandatory; OOM fast-fail + internal retry ×3; Android 12 needs the FGS started from the foreground; doneTimeout 240s) — see the test report §5.1.

### 4.3 Server launch and bootstrap strategy (corresponding to §5)
- **Path 1 (adb/phone adb)**: once per reboot, the host or the phone (termux-adb) runs `adb shell <lib>/libshizuku.so` → the App is autonomous from then on.
- **Path 2 (root launch)**: started by root_daemon after the first successful LPE — but the server then has the root identity, so **before running the exploit it must drop back to shell via run_shell** (uid 2000; the SELinux context remains kernel:s0 + Permissive; inferred to be feasible but not measured, so path 1 is preferred).
- **Path 3 (Magisk)**: fully automatic across reboots (see §5B); Shizuku can auto-start at boot via the su root mode.

## 5. Comparison matrix of alternatives (completed; see `research/shizuku/alternatives_and_ota.md`)

| Option | Degree of adb-freedom | Across reboot | Bricking risk | Implementation difficulty | Recommended priority |
|------|------------|-----------|---------|---------|---------|
| **B. Magisk dd flash** (service.d auto-starts root_daemon) | ✅ completely free (fully automatic at boot) | ✅ fully persistent | Medium (only touches boot, recoverable via fastboot; EDL lacks firehose) | Medium-high | **1 (if the flashing risk is accepted)** |
| **A. Phone as adb host** (termux-adb + adb_keys injection) | ⚠️ PC-free; one tap on the phone per reboot | ❌ helper restarts every boot | Low (does not touch partitions) | Medium (MIUI OTG pitfalls + one-off key injection) | **2 (low-risk first choice)** |
| Baseline: PC adb + shell helper (current) | ❌ needs a PC | ❌ | Low | Completed | 3 (fallback) |
| Shizuku engine | Bound to how the server is started (see §3.3) | Needs Magisk to be adb-free | None | Low-medium | Serves as the App-side execution channel; can be layered on A/B |
| ~~install-recovery.sh hook~~ | — | — | — | — | ❌ blocked dead by dm-verity (measured) |
| ~~symlink trick, App direct connection~~ | — | — | — | — | ❌ needs shell privileges; running directly from the App kernel panics (measured) |

**Key facts (sub-agent C)**:
- **Option A's chicken-and-egg authorisation problem is solved**: the phone's termux-adb generates `adbkey.pub` (adb's own mincrypt format, not OpenSSH) → through the existing root_daemon socket, as root, `printf >> /data/misc/adb/adb_keys` + `chown system:shell` + `chmod 640` + `restorecon` → adbd re-reads it when the AUTH request arrives, which usually takes effect immediately and avoids the fingerprint UI.
- **Option B**: as root, `dd` `magisk_patched-30600_9C56p.img` to `mmcblk0p17` (active slot A). The bootloader is already unlocked → AVB lets it through, with no need to touch vbmeta/dm-verity (Magisk is systemless and does not touch read-only partitions). After it succeeds, a boot script in `/data/adb/service.d/` starts root_daemon directly → the flaky ghostlock chain can be retired and Shizuku can be started as root. **Before flashing you must dd a backup of the stock boot_a outside the device and verify its MD5** (the EDL script can only wake the device; without firehose there is no way to flash over the wire).
- Recommended path: **A first, then B**; once B is done, write the OTA lock into service.d.

## 6. OTA protection (completed; see `research/shizuku/alternatives_and_ota.md` §C)
- Current state: firmware 1.22.009 (vulnerable version); 1.25.012 has backported the CVE-2026-43499 fix (an identical SPL is misleading; identifying it requires disassembling remove_waiter); `isForceUpdate=false`, with no silent forced upgrade.
- The glasses themselves have no network → "check and download by themselves" is inherently broken; the real risk = **the official phone Rokid App acting as a proxy / the `com.rokid.os.sprite.assistserver` relay** (a local sideload could not be confirmed, but defences should assume there is one).
- **First choice (do immediately)**: `adb shell pm disable-user --user 0 com.rokid.glass.ota` (persists across reboot, stored in package-restrictions.xml); as a secondary measure, `com.rokid.os.sprite.assistserver` can be evaluated (it may affect phone integration; take inventory first).
- **Do not** disable `com.android.providers.downloads` (the general-purpose downloader; it would break the album).
- During the research period: keep the glasses disconnected from the official Rokid App over Bluetooth and do not sign in.
- **Upgrading to 1.25.012 is considered irreversible** (A/B anti-rollback plus no public downgrade/flash package) → blocking OTA is the precondition for keeping ghostlock_rv101.

## 7. Conclusions and recommendations

**Answering the original question**: "Is Shizuku suitable for installation in the glasses-side APP, to then run adb shell + the ghostlock exploit?"
→ **It is suitable, and it is the cleanest in-App shell execution channel among the available options**; but "running adb shell" must be understood as "obtaining the same shell identity as adb shell to run the exploit", not literally starting adbd on the device. Shizuku does not solve "zero external intervention after a reboot" — that falls within Magisk's scope.

**Recommended order of execution**:
1. **Immediate (can be prepared without the device)**: run `deploy/lock_ota.sh` (disable `com.rokid.glass.ota`); keep the glasses away from the official Rokid App during the research period.
2. **Low-risk modification (recommended first)**: phone termux-adb + `adb_keys` injection (§5A) → removes the PC dependency, with one tap on the phone per reboot to restart the helper/Shizuku server.
3. **App integration (the main body of this report)**: add the Shizuku dependency + `ShizukuGhostlockEngine` (UserService/AIDL pattern) to app-glasses, falling back to Bridge; pre-authorise via `pm grant`.
4. **Ultimate (optional; confirm risk acceptance first)**: dd a backup of the stock boot_a → flash `magisk_patched-30600_9C56p.img` → a `service.d` boot script auto-starts root_daemon (the ghostlock chain can be retired) + OTA locked + Shizuku auto-starting as root.

**Main risk warning**: the exploit's ~50% panic rate is unchanged in nature (Shizuku is only a channel); a kernel panic takes the Shizuku server down with it → the App must detect the reboot and redo the bootstrap from §5; upgrading to 1.25.012 is irreversible.

## Appendix
- Research details: `research/shizuku/shizuku_internals.md` (including errata), `research/shizuku/ghostlock_apk_analysis.md`, `research/shizuku/alternatives_and_ota.md`
- Design artefacts: `research/shizuku/ShizukuGhostlockEngine.draft.kt`, `research/shizuku/aar/` (evidence from the 13.1.5 aar/sources), `research/shizuku/shizuku-src/` (Shizuku source)
- Deliverables: `deploy/lock_ota.sh` (one-key OTA lock)
