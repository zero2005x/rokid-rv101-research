# GhostLock RV101 — Work Status Handoff v9 (eighth → ninth agent, the W2 implementation round)

> 中文版：[GHOSTLOCK_HANDOFF_V9.md](GHOSTLOCK_HANDOFF_V9.md)

> Updated 2026-08-20. This round (the eighth agent, with the device reconnected) completed three major things:
> 1. **Localized and fixed the root cause of the run51/v9.30 kernel panic** (it is not the 8-byte smear!).
> 2. **Landed the complete code for the W2 FOPS route** (fake_fops + configfs arbitrary read + task walk + cred write).
> 3. Found that **RV101's ashmem uses strscpy (which zeroes the tail)**, so OnePlus's zero_at trick does not work on this machine, and switched to a "page-based configfs read".

## 0. The most critical conclusion (read first)

**run51's punch-1 death is not the 8-byte smear (suspect A), but `waiter->task = data_addr(INIT_TASK)` pointing at the "real init_task"; the `wake_q_add(head, init_task)` at the end of the PI walk corrupts init_task → panic.**

- Ironclad evidence: v9.30's **selftest (writing the sprayed page, harmless target) also panics 100% at punch 1**; run50 (v9.29, wrong phys, data_addr(INIT_TASK) pointing at +0x800000 garbage) does not blow up.
- Fix (selftest STRICT OK already verified): in `route.c`, change `PWS_TASK` (the fake waiter->task) from `data_addr(INIT_TASK)` to **`fake_task`** (a fake task inside the sprayed page, the same as OnePlus).
- This means **W1 (writing selinux_state to turn off SELinux) may not have been broken at all** — the previous agent's panic diagnosis just pointed in the wrong direction. But in on-device measurement, after the W1 write `enforce` was still 1 (STRICT OK but enforce does not flip), so **selinux_state's enforcing offset may also not be +0** (see §2.3, to be investigated).

## 1. Verified results

| Item | Status |
|---|---|
| v9.30 panic root cause + fix (`waiter->task = fake_task`) | ✅ Verified (selftest STRICT OK, no more panic) |
| Write primitive (rb_erase 8-byte write) | ✅ Verified (STRICT signature) |
| Build (WSL interop clang.exe) | ✅ `clang.exe --target=aarch64-linux-android31 ...` compiles, and the artifact's md5 matches device v9.30 |
| adb (Windows adb.exe via interop) | ✅ `$ADB -s <DEVICE_SERIAL> shell ...`; push needs `wslpath -w` to convert the path |

## 2. Offsets settled by disassembly (all already written into `target_rv101.h` "W2 FOPS route (v9.31 prep)")

### 2.1 task_struct
`tasks=0x4c8`, `pid=0x5c8`, `tgid=0x5cc`, `group_leader=0x608`, `real_cred=0x778`, `cred=0x780`, `comm=0x790`.

### 2.2 configfs_buffer (RV101 5.10, disassembly of configfs_read/write_bin_file)
`mutex=0x20`, `needs_read_fill=0x50`, `read_in_progress=0x54`, `write_in_progress=0x55`, `bin_buffer=0x58`, `bin_buffer_size=0x60`, `cb_max_size=0x64`, `item=0x68`, `page=0x10`, `count=0x00`.

### 2.3 Key symbols (kallsyms/Image dump)
ashmem_fops=0x1e977a8, ashmem_misc=0x29bcbb0 (**fops @ +0x10, not the +0x18 of the v3 handoff**), configfs_*_fops, all `.cfi_jt` jump-table addresses (`CFI_*_OFF`). **This kernel is Clang kCFI**: the fake fops table must be filled with `.cfi_jt` addresses, and must use the `.read`(0x10)/`.write`(0x18) slots (not iter).

### 2.4 The three major address errors (two fixed by the previous two agents + one newly found this round)
1. symbol offset +0x28000000 (fixed in v9.29).
2. phys load base 0x80800000→0x80000000 (fixed in v9.30).
3. **ashmem_misc.fops @ +0x10** (the v3 handoff's +0x18 is wrong; already fixed in the header).

## 3. Current blocker: configfs arbitrary read is not yet working (two layers of problems)

### 3.1 strscpy zeroes the tail (key new finding)
RV101's `ashmem set_name` uses **strscpy** (`add x0,x19,#0xb; bl 0x7777b4=strscpy`), which **zeroes** the bytes "within the same 8-byte word after the NUL". OnePlus 6.12's ashmem uses strlcpy (which preserves the tail), so OnePlus's `try_set_ashmem_name_blob` zero_at trick **does not work** on this machine — measured: after SET_NAME the name is all 0.

### 3.2 Switched to a "page-based configfs read" (theoretically workable, not yet measured as passing)
`configfs_read_file` uses `page@0x10` (not bin_buffer@0x58). page is at the **beginning** of the SET_NAME blob (blob[5..12]), far from needs_read_fill (blob[69]), so it can be achieved with a single SET_NAME:
```c
memset(name, 0x41, 256);
memcpy(name+5, &target, 8);   // page = target
name[21] = 0;                  // NUL -> mutex(0x20)=0, needs_read_fill(0x50)=zalloc 0
ioctl(fd, ASHMEM_SET_NAME, name);
pread(fd, data, len, 0);       // configfs_read_file reads page=target
```
`fake_fops->read` has been changed to `configfs_read_file.cfi_jt`. **But no successful probe has been observed yet** (see §3.3).

### 3.3 Current obstacle: mode-4 writes (FOPS installation) show "STRICT MISS" in recent rounds and the device panics
- In one early round a mode-4 write was STRICT OK (diff@0x1600=(target-8)|1), proving the write geometry is correct.
- After adding "verify write" (the STRICT check of `do_one_write(mode 4)`), several consecutive rounds show **child_pc@FOPS_OFF=0 (MISS)**, and the device panics.
- Suspect directions: (a) reclaim fluctuation, (b) do_one_write's retry loop leaving a stale fake_fops pointer when write#1 lands but STRICT misses it, (c) a regression introduced by some change. **To be investigated next round**.

## 4. Next round's to-do list (in priority order)

1. **Debug the mode-4 write**: first use `--selftest` to confirm reclaim is stable; do a single mode-4 write (no verify+install double write, no retry loop), then clean up manually and see whether child_pc@FOPS_OFF lands.
2. **Verify the page-based configfs read**: once mode-4 lands, probing `kernel_read64(fd, data_addr(INIT_TASK+TASK_TASKS_OFF))` should return a legal pointer (nonzero).
3. **cred write**: after finding child_task, use `do_cred_write()` (rb_erase mode-2 writes init_cred to cred/real_cred, single shot without verification, verified via the child uid).
4. (Secondary) Find selinux_state's real enforcing offset (W1 can serve as a fallback path).

## 5. Build/run quick reference

```bash
# Build (WSL interop)
NDKBIN="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/ndk/android-ndk-r27d/toolchains/llvm/prebuilt/windows-x86_64/bin"
cd /mnt/c/Users/<USER>/Documents/codebase/rokid_root/source_code/ghostlock_rv101
"$NDKBIN/clang.exe" --target=aarch64-linux-android31 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
  -fno-stack-protector -fPIE -Isrc -include target_rv101.h -o build/ghostlock_rv101 \
  src/main.c src/util.c src/route.c -pie -static-libgcc -pthread

# push (wslpath -w to convert the path) + run
ADB="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe"
"$ADB" -s <DEVICE_SERIAL> push "$(wslpath -w .../build/ghostlock_rv101)" /data/local/tmp/ghostlock_rv101
"$ADB" -s <DEVICE_SERIAL> shell "chmod 755 /data/local/tmp/ghostlock_rv101"
"$ADB" -s <DEVICE_SERIAL> shell "GL_SKB_PAD=64 GL_DEBUG_DRAIN=1 GL_DUMMY_KILLS=180 GL_SETUP_ATTEMPTS=3 /data/local/tmp/ghostlock_rv101 --selftest"
```
Disassembly: `python3 scripts/rv101_disasm.py <hex-offset> <len>` (capstone already installed).

---

## Appendix A: New findings from the ninth agent's implementation round (round 1)

1. **The correct value of `GL_SKB_PAD` is `0`, not the `64` written in the handoff.**
   - `--selftest` (mode 3) with `GL_SKB_PAD=64` gives "0 diffs" (the write does not land in the recv vehicle); switching to **`GL_SKB_PAD=0`** gives `STRICT child_pc=OK` (the write lands normally).
   - Compare the v9.20 comment in `util.c`: "SKB data starts at block+0; pad=0 re-aligns" — **the `GL_SKB_PAD=64` in the handoff and in the ps1 script is wrong**. Use `GL_SKB_PAD=0` from now on.

2. **Installing fake_fops (mode-4 write to ashmem_misc.fops) makes the device panic at punch 1.**
   - Added `install_fake_fops` debugging: `route start` → `pselect route target=...mode=4` → disconnect after `consumer punch 1/5` (it dies at punch 1, i.e. at the moment rb_erase writes `fake_fops → ashmem_misc.fops`).
   - selftest (mode 3, writing the sprayed page) is normal → the problem is specific to "writing the global .data pointer ashmem_misc.fops".
   - Most likely cause: the Android system uses `/dev/ashmem` intensively, and once fops is swapped for fake_fops, **concurrent processes** opening/reading/writing ashmem hit configfs's read/write (or a NULL slot) and crash.
   - Compare OnePlus: after installing fake_fops it "immediately" performs the configfs operation and "immediately" restores the original fops (`configfs_write_once(fd, misc_fops, &original_fops, ...)`). This implementation currently **does not restore**, and the configfs **write** path (bin_buffer@0x58) still does not work because of strscpy.

3. **Next round's priority**: make the fake_fops installation "safe" — (a) restore the original fops (note that a mode-4 restore would smear `ashmem_fops.owner` into garbage, so switch to a configfs write or accept a built-in driver with a NULL owner), (b) or find a read primitive that does not touch ashmem's global fops.

**Current code status**: everything has been synced to Windows; `--selftest` (GL_SKB_PAD=0) passes; the W2 main flow (install fops → configfs read → task walk → mode-2 cred write) has landed but is stuck on the fake_fops installation panic.

---

## Appendix B: Ninth agent's round 2 progress (on-device measurement)

1. **The fake_fops installation panic comes from the "configfs write", not the "configfs read".**
   - `fake_fops->write = configfs_write_bin_file` makes concurrent processes writing ashmem crash at installation time (dies at punch 1).
   - After setting `write` to **NULL** (keeping `read` as `configfs_read_file`), installation no longer reliably panics (the route can run to completion).
   - Conclusion: **the cred write uses rb_erase mode-2 (`do_cred_write`) and configfs only does reads**, so `write=NULL` is the correct answer.

2. **The buffer fields for the configfs read (page-based) are verified correct**: the GET_NAME dump shows `page@0x10 = target`,
   `needs_read_fill@0x50 = 0`, `mutex@0x20 = 0` (the single-NUL SET_NAME write is correct). But `pread` still returns 0.
   To investigate: `simple_read_from_buffer`'s `available = buffer->count@0x00 = "dev/ashm"` (asmem_open uses
   `str x11,[asma]` to store the prefix and `stur w10,[asma,#7]` to store the "mem/" tail → `count` is a huge positive number), so logically it should not return 0.

3. **The biggest uncertainty is that reclaim is flaky**: `--selftest`(GL_SKB_PAD=0) is sometimes STRICT OK, sometimes 0 diffs,
   sometimes a panic; mode-4 writes gave 0 diffs 6 times in a row. This is the old problem of v9.x heap feng shui (the handoff's run45-48 "zero diffs").
   Next round: (a) use `GL_SETUP_ATTEMPTS=12` to try more, (b) or roll back and tune V9_PURGE_CNT/settle timing.

**Current code**: already synced to Windows. `fake_fops->read=configfs_read_file`, `write=NULL`; `install_fake_fops`
keeps the page uncleaned (the correct answer); the configfs read carries GET_NAME debugging.
