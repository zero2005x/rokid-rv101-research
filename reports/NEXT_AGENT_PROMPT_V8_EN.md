# DeepSeek Harness Prompt for GhostLock RV101 (Ninth Agent)

> 中文版：[NEXT_AGENT_PROMPT_V8.md](NEXT_AGENT_PROMPT_V8.md)

> ⚠ **Historical document (not the current state)**: this text records the situation at the time; the project has since changed significantly (root acquired, engine switched to runtime probing, phone UI redone, i18n 193 keys).
> For the current state see `README.md`, `MANIFEST.md`, `docs/deploy.md`.


You are the ninth agent to take over the GhostLock project. Goal: complete the root privilege escalation chain on the RV101 device.

## First Prerequisite: Device Connection State

When the ninth agent took over, the device **had been reconnected to the Windows host over USB** (`adb devices` → `<DEVICE_SERIAL>  device`).
Note: **adb inside WSL cannot see this device** (WSL2 has no USB passthrough). Choose one:
- Use `usbipd` (on the Windows side `usbipd bind --busid <id>` → inside WSL `usbipd attach`) so that WSL adb can connect directly; or
- Just run in **PowerShell** (all the existing scripts / `mingw32-make` are Windows tools to begin with, and all previous runs were done this way).
- Serial: `<DEVICE_SERIAL>`.

## Settled Conclusions (do not redo these)

1. **The cause of the run51 death = 8-byte smear (suspect A), already settled from the existing logs**: run50 hit a harmless address (selinux_state+0x800000)
   and all 5/5 punches completed cleanly; run51 hitting the real selinux_state died at punch 1; the only variable is the target address, so flood is ruled out.
2. **W1 (writing selinux_state to turn off SELinux) is a dead end**: rb_erase can only write 8 bytes, and the written value is a kernel pointer (high bytes=0xff);
   smearing `checkreqprot/initialized/policycap` is guaranteed to blow up. **Go straight to W2.**
3. Details and the full symbol table are in `reports/GHOSTLOCK_HANDOFF_V8.md`.

## Your Task: Implement and On-Device Test the W2 FOPS Route

W2's goal: write the child's `task->cred` (offset `0x780`) as `data_addr(INIT_CRED)` (an 8-byte pointer, no smearing).
Current blocker: `perf_find_task()` is completely blocked by `perf_event_paranoid=3`, so the child's task_struct address cannot be obtained.
Solution = FOPS arbitrary read/write (see `research/ghostlock_refs/extracted/oneplus/.../src/core/{fops.c,root.c}`).

**Implementation steps (`source_code/ghostlock_rv101/`):**

1. In `prepare_skb_payload()` in `util.c`, add a fake `file_operations` table (for example `FOPS_OFF=0x1600`),
   filled with **`.cfi_jt` jump-table addresses** (**not the function bodies**, see `CFI_*_OFF` in `target_rv101.h`):
   - `read`(0x10) = `configfs_read_bin_file.cfi_jt`, `write`(0x18) = `configfs_write_bin_file.cfi_jt`
   - `llseek`=noop, `ioctl/compat/mmap/open/release/show_fdinfo` = the original ashmem `.cfi_jt`
   - ⚠️ configfs uses `.read/.write` (+0x10/+0x18), **not** read_iter/write_iter.
2. Use the mode-2 write primitive to write `fake_fops` into **`data_addr(ASHMEM_MISC)+0x10`** (**not +0x18**; the v3 handoff got this wrong).
3. Open `/dev/ashmem` and implement `configfs_read_once`/`configfs_write_once` (using an ashmem `SET_NAME` blob to control
   the configfs buffer's `bin_buffer`) → arbitrary kernel read/write.
4. Walk the task list from `data_addr(INIT_TASK + TASK_TASKS_OFF)` (tasks @ `0x4c8`) and locate the child by comparing
   **`tgid` @ `0x5cc`** (the child's tgid == the `fork()` return value, more reliable than comm; `pid` @ `0x5c8` as a fallback).
5. `task->cred(0x780) ← data_addr(INIT_CRED)` → child does `setuid(0)` + `setenforce 0` → root shell.

All the offsets needed are already written into the "W2 FOPS route (v9.31 prep)" section of `target_rv101.h` (including `CFI_*_OFF`, `FOPS_*_OFF`,
`CFG_*_OFF`, `ASHMEM_MISC_FOPS_OFF=0x10`, `ASHMEM_NAME_PREFIX_LEN=11`, `TASK_TASKS_OFF=0x4c8`,
`TASK_PID_OFF=0x5c8`, `TASK_TGID_OFF=0x5cc`, `TASK_COMM_OFF=0x790`).

**The configfs arbitrary read/write primitive has already been checked line by line**, so be sure to read `reports/GHOSTLOCK_W2_FOPS_CONFIGFS_RV101.md` first
(on RV101 both reads and writes use `bin_buffer@0x58`; the fake fops is filled with `.cfi_jt` and uses the `.read/.write` slots; SET_NAME writes NUL in stages).

## Environment Discipline

- Device serial `<DEVICE_SERIAL>`; before each run uptime > 150s; after a panic it restarts automatically in 2-5 minutes and the slide changes.
- Build: `$env:ANDROID_NDK_HOME='C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\android-ndk-r27d'; cd source_code\ghostlock_rv101; mingw32-make -B`
- After building, always compare `Get-FileHash` against the device `md5sum` (v3 handoff warning: the PowerShell pipeline swallows make's failure code).
- Batch runs: `& scripts/ghostlock_reboot_run.ps1 -Cycles 3 -Env "GL_SKB_PAD=64 GL_DEBUG_DRAIN=1 GL_DUMMY_KILLS=180" -ExploitArgs "--selftest" -Log runNN`
- Run `--selftest` first to confirm the reclaim/write primitive is still stable (STRICT signature), then move on to W2.

First read `reports/GHOSTLOCK_HANDOFF_V8.md` and `source_code/ghostlock_rv101/target_rv101.h`, then start implementing.
