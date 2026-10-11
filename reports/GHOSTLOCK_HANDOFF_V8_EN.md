# GhostLock RV101 — Work Status Handoff v8 (eighth → ninth agent)

> 中文版：[GHOSTLOCK_HANDOFF_V8.md](GHOSTLOCK_HANDOFF_V8.md)

> Updated 2026-08-20. This round (the eighth agent) took over from v9.30; the main outputs are: **run51's cause of death settled**,
> **all symbols/offsets for the W2 FOPS route verified**, and **discovery of a third address error + the critical kCFI obstacle**.
> Source: `source_code\ghostlock_rv101\` (still v9.30 logic; the v9.31 W2 constants have been added to `target_rv101.h`).
> Target device: `<DEVICE_SERIAL>` (RV101 / RG_glasses).

## 0. The single most important sentence (read this line first)

**The device is currently offline (no adb device, no USB, 192.168.1.110 unreachable) — no on-device measurement is possible; but run51's cause of death can already be settled from the existing logs, so there is no need to run the "harmless target test" again.**

---

## 1. run51's cause of death: settled as "8-byte smear" (suspect A), not flood (suspect B)

### 1.1 Basis for the verdict: run50 was in fact the "harmless target test"

The "harmless target test" designed by the previous agent was: use v9.30 + `GL_TARGET_DELTA=+0x800000` to move the write target to harmless RAM.
**The target written in run50 (v9.29, wrong phys base) `0xffffff80034a0140` is exactly the harmless address `selinux_state + 0x800000`** — that is, this test **has already been run**:

| Run | Version | Write target | Result |
|---|---|---|---|
| 50 | v9.29 | `0xffffff80034a0140` (selinux_state **+0x800000**, harmless) | all 5/5 punches `returned`, STRICT child_pc=OK, **0 deaths**, enforce still 1 |
| 51 | v9.30 | `0xffffff8002ca0140` (**real selinux_state**) | dies at punch 1, log truncated at `consumer punch 1/5` |

The flood/reclaim/punch machinery was completely identical in the two runs; the only variable was the "target address". run50 hitting a harmless address did not blow up,
while run51 hitting selinux_state did — **this rules out the flood race (suspect B) and confirms the 8-byte smear (suspect A)**.
(If the flood were the cause, run50 would have died too.)

### 1.2 Byte-level anatomy of the smear

W1's write value is `page_base + SAFE_OFF` (for example `0xffffff800b07d500` in run51), and rb_erase case-A writes it
as 8 bytes to `selinux_state + 0`, giving (little-endian):

```
byte0=0x00  byte1=0xd5  byte2=0x07  byte3=0x0b  byte4=0x80  byte5..7=0xff
```

- byte0 = `0x00` → `enforcing = 0` (the expected permissive, **but this very step is killed by the corruption that follows**)
- byte1 = `0xd5` → `checkreqprot` nonzero
- byte2 = `0x07` → `initialized` nonzero
- byte3..7 → `policycap[]` (or `disabled`/`enabled`, depending on config) all smeared to garbage

`rb_erase` can only write 8 bytes, and the written value is a kernel pointer (the upper 4 bytes must be `0xff`), so **W1 cannot write only 1 byte**;
therefore the path of "write selinux_state to turn off SELinux" is **a dead end** under this primitive. The previous agent's "countermeasure B" (change write_value's upper
bytes to match legal fields) is not feasible, for the same reason given by the previous agent.

### 1.3 Conclusion

**Give up on W1 (SELinux) and push W2 directly (cred = init_cred / patch cred).** W2's target is an 8-byte pointer field
and the write value is also an 8-byte pointer, so there is no smear problem — consistent with the previous agent's "countermeasure A".

---

## 2. Core result of this round: all symbols and offsets for the W2 FOPS route verified

Everything below is taken directly from `research/ghostlock_refs/rv101_kallsyms.txt` (UTF-16LE, needs `iconv` first) and
`binaries/extracted_1.22/kernel` (raw Image, file offset == link offset); none of it is estimated.

### 2.1 Symbols (offset = VA − `0xffffffc008000000`)

| Symbol | VA | offset |
|---|---|---|
| `init_task` | `ffffffc00a1eb840` | `0x021eb840` |
| `init_cred` | `ffffffc00a202058` | `0x02202058` |
| `selinux_state` | `ffffffc00aca0140` | `0x02ca0140` |
| `commit_creds` | `ffffffc00818b328` | `0x18b328` |
| `prepare_kernel_cred` | `ffffffc00818ba9c` | `0x18ba9c` |
| `noop_llseek` | `ffffffc0083d1930` | `0x3d1930` |
| `ashmem_fops` | `ffffffc009e977a8` | `0x1e977a8` |
| `ashmem_misc` | `ffffffc00a9bcbb0` | `0x29bcbb0` |
| `configfs_file_operations` | `ffffffc009d46840` | `0x1d46840` |
| `configfs_bin_file_operations` | `ffffffc009d46960` | `0x1d46960` |
| `configfs_read_bin_file` | `ffffffc00849ec40` | `0x49ec40` |
| `configfs_write_bin_file` | `ffffffc00849ee00` | `0x49ee00` |
| `configfs_read_file` | `ffffffc00849e8e0` | `0x49e8e0` |
| `configfs_write_file` | `ffffffc00849ea48` | `0x49ea48` |
| `ashmem_ioctl / mmap / open / release / show_fdinfo` | see v3 handoff §5 | consistent with v3 |

### 2.2 task_struct offsets (measured by disassembly this round, RV101 5.10.209)

| Field | offset | Source |
|---|---|---|
| `tasks` (list_head) | **`0x4c8`** | init_task walk in `__arm64_sys_kill` (`add x8,x23,#0x4c8; sub x22,x25,#0x4c8`) |
| `pid` | **`0x5c8`** | `copy_process`: `p->pid = pid_nr(pid)` (`str w8,[x20,#0x5c8]`) |
| `tgid` | **`0x5cc`** | `copy_process` CLONE_THREAD branch: `p->tgid = current->tgid` (`ldr w8,[x19,#0x5cc]; str w8,[x20,#0x5cc]`) |
| `group_leader` | `0x608` | same as above (`str x20/x8,[x20,#0x608]`) |
| `real_cred` | `0x778` | pre-existing (commit_creds disassembly) |
| `cred` | `0x780` | pre-existing; reconfirmed in `__arm64_sys_kill` this round (`ldr x8,[x21,#0x780]`) |
| `comm` | **`0x790`** | `__get_task_comm`/`__set_task_comm` (`add x0,x20,#0x790`) |

> `comm` is at `0x790` (not `cred+8`=0x788) — because **CONFIG_KEYS=y** inserts
> `cached_requested_key` (8 bytes) between cred and comm.

### 2.3 The "third address error" found this round + the kCFI obstacle (important!)

1. **`ashmem_misc.fops` is at `+0x10`, not the `+0x18` written in the v3 handoff.**
   raw Image dump of `ashmem_misc`: `+0x000`=minor(0xff), `+0x008`=name pointer, **`+0x010`=`&ashmem_fops`**.
   v3's `+0x18` is actually the `list` field; writing there would put fake_fops into the list head and break the whole W2 chain.

2. **This kernel is Clang kCFI + LTO** (all symbols carry a `$hash` suffix; there is a `.cfi_jt` jump table). Measured: the entries inside the `ashmem_fops`
   and `configfs_*_fops` tables are **`.cfi_jt` jump-table addresses, not the function bodies**:
   - `ashmem_fops` dump: `+0x008`=`ashmem_llseek.cfi_jt`, `+0x020`=`ashmem_read_iter.cfi_jt`,
     `+0x050`=`ashmem_ioctl.cfi_jt`, `+0x060`=`ashmem_mmap.cfi_jt`, `+0x070`=`ashmem_open.cfi_jt`,
     `+0x080`=`ashmem_release.cfi_jt`, `+0x0e0`=`ashmem_show_fdinfo.cfi_jt`.
   - `configfs_bin_file_operations` dump: `+0x010`=`configfs_read_bin_file.cfi_jt`,
     `+0x018`=`configfs_write_bin_file.cfi_jt`, `+0x070`=open, `+0x080`=release.

   **Implication**: the fake file_operations table **must be filled with the real `.cfi_jt` jump-table addresses** (already computed and written into
   `CFI_*_OFF` in `target_rv101.h`); it cannot be filled with function body addresses the way the OnePlus reference (6.12) does.

3. **configfs uses `.read`/`.write` (fops +0x10/+0x18), not `.read_iter`/`.write_iter` (+0x20/+0x28).**
   So the fake fops must put `configfs_read_bin_file.cfi_jt` / `configfs_write_bin_file.cfi_jt` into
   **`FOPS_READ_OFF`/`FOPS_WRITE_OFF`**, not into OnePlus's `READ_ITER`/`WRITE_ITER` slots.

---

## 3. Implementation plan for the W2 FOPS route (next step, needs the device)

Reference: `research/ghostlock_refs/extracted/oneplus/GhostLock-for-OnePlus-main/src/core/{fops.c,root.c}`.

1. **Add a fake fops table to the sprayed page** (inside `prepare_skb_payload`, e.g. `FOPS_OFF = 0x1600`), with contents = the
   `file_operations` assembled from the `CFI_*` jump-table addresses above:
   - `read` = `configfs_read_bin_file.cfi_jt`, `write` = `configfs_write_bin_file.cfi_jt`
   - `llseek` = `noop_llseek.cfi_jt`, `ioctl`/`compat_ioctl`/`mmap`/`open`/`release`/`show_fdinfo`
     = ashmem's original `.cfi_jt`
2. **Use the mode-2 write primitive to write `fake_fops` into `data_addr(ASHMEM_MISC)+0x10`** (not +0x18).
3. Open `/dev/ashmem`; its read/write/ioctl now go through configfs → follow the OnePlus `util.c`
   `configfs_read_once`/`configfs_write_once` (using the ashmem `SET_NAME` blob to control the configfs buffer's
   `bin_buffer` field) to obtain **arbitrary kernel read/write**.
4. Walk the task list from `data_addr(INIT_TASK + TASK_TASKS_OFF)` and find the child by comparing **`tgid` (offset `0x5cc`)**
   (the child's tgid == the `fork()` return value; more robust than comm). `TASK_PID_OFF=0x5c8` can also serve as a fallback.
5. `task->cred (0x780) ← data_addr(INIT_CRED)` (write with mode-2, or directly patch cred's
   uid/caps/sid with an arbitrary write). Child `setuid(0)` + `setenforce 0` → root shell (the `main.c` skeleton is already built in).

> **The configfs arbitrary read/write primitive has been compared line by line**, see `reports/GHOSTLOCK_W2_FOPS_CONFIGFS_RV101.md`
> (RV101 configfs_buffer offsets: `bin_buffer=0x58`, `bin_buffer_size=0x60`, `cb_max_size=0x64`,
> `needs_read_fill=0x50`; the ashmem prefix "dev/ashmem/" = 11 bytes; both read and write use `bin_buffer`, which is simpler than OnePlus's
> page/bin_buffer split).
>
> All the offsets above (including `CFI_*`, `FOPS_*`, `ASHMEM_MISC_FOPS_OFF=0x10`, `CFG_*_OFF`,
> `ASHMEM_NAME_PREFIX_LEN`, `TASK_TASKS_OFF`, `TASK_PID_OFF`, `TASK_TGID_OFF`, `TASK_COMM_OFF`)
> have been written into the "W2 FOPS route (v9.31 prep)" section of `source_code/ghostlock_rv101/target_rv101.h`,
> but **have not been wired into any code yet**; they are provided only for the next agent to implement with.

---

## 4. To-do list (in priority order)

1. **Reconnect the device** (USB; **the adbd in this build does not bind TCP, so `adb connect` is unavailable** — measured 2026-09-13). This is the only hard blocker.
2. Implement the W2 FOPS route per §3 and measure it on device (first `--selftest` to confirm reclaim is still stable, then run W2).
3. Additionally measure `TASK_PID_OFF`/`TASK_TGID_OFF` (disassemble `copy_process` or the tail of `do_task_stat`) so that the task
   walk can compare tgid instead (more robust than comm, because comm can be truncated/renamed).

## 5. Environment and tools (unchanged)

- Build: `$env:ANDROID_NDK_HOME='...\ndk\android-ndk-r27d'; cd source_code\ghostlock_rv101; mingw32-make -B`
- Batch run: `scripts/ghostlock_reboot_run.ps1 -Cycles 3 -Env "..." -ExploitArgs "..." -Log runNN`
- Disassembly: `python3 scripts/rv101_disasm.py <hex-offset> <len>` (capstone 5.0.7 installed this round, runs inside WSL)
- kallsyms: `iconv -f UTF-16LE -t UTF-8 research/ghostlock_refs/rv101_kallsyms.txt | grep <sym>`
- The device auto-reboots 2-5 minutes after each panic and the KASLR slide changes; make sure uptime > 150s before each run.
