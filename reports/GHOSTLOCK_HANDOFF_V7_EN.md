# GhostLock RV101 — Work Status Handoff v7 (seventh → eighth agent)

> 中文版：[GHOSTLOCK_HANDOFF_V7.md](GHOSTLOCK_HANDOFF_V7.md)

> 📌 **Basis of fact**: wherever this document touches facts such as device generation, kernel config, CVE status, GhostLock offsets, or product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (single authoritative fact table; in case of conflict, that table decides).


> Updated 2026-08-19. This round (the seventh agent) took over from v9.26 and went through 4 versions, v9.27→v9.30.
> **This round successfully localized and fixed the root cause of "W1 writes do not land" that had plagued the previous agents (two major address computation errors), but the first on-device measurement after the fix (run 51) was unexpectedly interrupted at the punch 1 stage.**
> Working directory: `C:\Users\<USER>\Documents\codebase\rokid_root`
> Source: `source_code\ghostlock_rv101\` (v9.30, binary md5 `478D10A064D21F70DD226C5E9E8BAD10`, already synced to the device)
> Target device: `<DEVICE_SERIAL>` (RV101 / RG_glasses)

## 1. Overall conclusion (one sentence)

**The previous agents' "W1 writes do not land" was because the symbol offset and the physical base address of `data_addr` in `target_rv101.h` were computed incorrectly (two fatal bugs in total); after this round fixed them via disassembly and `kallsyms` (v9.30), the write address now hits `selinux_state` precisely, but this immediately caused run 51 to kernel panic/reboot at the punch 1 stage, and it must be confirmed whether the 8-byte write primitive corrupted the flag fields following the `selinux_state` structure.**

---

## 2. Core result of this round: localizing and fixing two major address errors

### 2.1 Error one: the symbol offset had an extra 0x28000000 (fixed in v9.29)
*   **Symptom**: run49 (v9.27) got STRICT child_pc=OK, proving `rb_erase` executed, but `enforcing` was still 1.
*   **Root cause**: `SELINUX_STATE_OFF` in `target_rv101.h` was set to `0x2aca0140`, but `kallsyms` shows `ffffffc00aca0140 B selinux_state`. The correct offset should be `0x02ca0140` (that is, 0x0aca0140 - 0x08000000). Every symbol on the W1/W2 chain (init_task, init_cred, etc.) was wrong by `+0x28000000`.
*   **Fix**: v9.29 corrected four offsets according to `kallsyms`.

### 2.2 Error two: the physical memory base address had an extra 0x800000 (fixed in v9.30)
*   **Symptom**: after the offset fix in run50 (v9.29), enforce was still 1.
*   **Root cause**: `P0_KERNEL_PHYS_LOAD` was set to `0x80800000`. But after disassembling `kaslr_early_init`, parsing the ARM64 Image Header (finding `text_offset = 0x0`, because of CONFIG_RELOCATABLE), and reading the device's `/proc/zoneinfo` (`start_pfn=524288` → `memstart=0x80000000`), the kernel physical load base address was confirmed to be `0x80000000`. The old value shifted the write address by 8MB.
*   **Fix**: v9.30 changed `P0_KERNEL_PHYS_LOAD` back to `0x80000000`. At this point the W1 target finally pointed at the real `selinux_state` direct-map alias (`0xffffff8002ca0140`).

### 2.3 Other static verification
*   `selinux_init` disassembly confirms: `enforcing` really is at `selinux_state + 0`, and `memset(state, 0, 0x88)`. GKI_struct_selinux_state (`0xaca3628`) has zero references and is a dead ABI placeholder.
*   `rb_erase` case-A write geometry confirmed: it must write to `target+0`.

---

## 3. Current blocker: the mystery of run51's death

Running run51 under v9.30 (addresses fully correct) (W1 mode, 3 cycles), **all 3 cycles died at punch 1 (exit=255)**, with the log truncated at `consumer punch 1/5`.

### 3.1 Deduction of the cause of death (two main suspects)
1.  **Suspect A: the 8-byte write smear corrupts the structure (number one suspect)**
    *   The write primitive of `rb_erase` is an 8-byte pointer write.
    *   `enforcing` in `selinux_state` occupies only 1 byte (offset +0).
    *   Writing 8 bytes overwrites the fields from `state+1` to `state+7` with the garbage value of the pointer.
    *   According to the `selinux_init` disassembly, `state+1` is `checkreqprot`, and `state+2~+7` may be bool fields such as `initialized`, `disabled`, `enabled`.
    *   If `disabled` is written to a nonzero value, the next time the kernel accesses SELinux state (such as an avc check) it may trigger the runtime disable path or dereference a bad pointer, causing an immediate crash.
2.  **Suspect B: vmap purge flood race (a leftover of the old problem)**
    *   Earlier runs 39/41/45 also had the gate flood cause the leak slab to be reallocated as a kernel stack, and the walk wrote into a live stack and died.
    *   The 3 deaths in run51 could be a 1/3-probability flood-death happening three times in a row (probability about 4%).

### 3.2 Interpretation criteria
*   **Characteristics of a punch-1 death log**: it stops at `consumer punch 1/5 (+20000us) tid=T`, with no `returned` line, and adb disconnects.

---

## 4. To-do list left for the eighth agent (in priority order)

### 4.1 Task number one: distinguish the cause of run51's death
*   **Method**: run W1 mode, but use the `GL_TARGET_DELTA` environment variable to shift the write target to a "harmless" RAM location (for example the previously wrong address `0xffffff80034a0140`).
*   **Criterion**:
    *   If it still dies 3/3 → it is a flood-death (unrelated to the target address).
    *   If it dies 0/3 (completes cleanly but enforce does not flip) → it is **8-byte smear corrupting the structure**.

### 4.2 Task two: solve the 8-byte smear problem (if suspect A is confirmed)
*   `rb_erase` cannot write only 1 byte.
*   **Countermeasure A**: give up on W1 (SELinux) and advance directly to W2. W2's target is to overwrite the task's `cred` pointer with `init_cred`. The pointer itself is 8 bytes, so there is no smear problem.
*   **Countermeasure B**: modify `write_value` (currently `page_base + SAFE_OFF`) so that its upper bytes match legal values for the fields following `selinux_state`. But `page_base` is a kernel pointer (`0xffffff80...`), so bytes 4-7 must be `0xff` and cannot match bool fields that are all zero.

### 4.3 Task three: repair the W2 path
*   Currently the W2 flow in `main.c` uses `perf_find_task()` to locate the child task_struct.
*   **Problem**: the device has `perf_event_paranoid=3`, so perf is completely blocked (EACCES).
*   **Countermeasure**: switch to the configfs/FOPS route mentioned in the V1 handoff, or locate the task_struct by reading `/proc` files.

---

## 5. Environment and tools

| Item | Status/Path |
|---|---|
| **Current Binary** | v9.30 (md5 `478D10A064D21F70DD226C5E9E8BAD10`) |
| **Device state** | Needs a reboot to ensure a clean state (uptime > 150s) |
| **Disassembly tool** | `scripts/rv101_disasm.py <file_offset> <len>` (requires python3 + capstone) |
| **Batch run script** | `& scripts/ghostlock_reboot_run.ps1 -Cycles 3 -Env "GL_SKB_PAD=64 GL_DEBUG_DRAIN=1 GL_DUMMY_KILLS=180" -ExploitArgs "--write1" -Log runNN` |
| **KASLR Leak** | Verified working (VA slide >= 64GB, purely virtual, does not affect the data_addr alias) |

## 6. Summary of the historical run matrix

| Run | Version | Configuration/Characteristics | Result |
|---|---|---|---|
| 48 | v9.27 | selftest | STRICT signature passed (payload landed successfully). |
| 49 | v9.27 | W1 (wrong offset) | STRICT child_pc=OK, but enforce was still 1. Offset bug found. |
| 50 | v9.29 | W1 (offset fixed, phys wrong) | STRICT child_pc=OK, enforce still 1. phys bug found. |
| 51 | v9.30 | W1 (all fixed) | 3/3 died at punch-1. Suspected 8-byte smear crash or flood. |
