# GhostLock RV101 — Work Status Handoff Document v2

> 中文版：[GHOSTLOCK_HANDOFF_V2.md](GHOSTLOCK_HANDOFF_V2.md)

> 📌 **Basis of fact**: wherever this document touches facts such as device generation, kernel config, CVE status, GhostLock offsets, or product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (single authoritative fact table; in case of conflict, that table decides).


> This document was updated on 2026-08-16 (at the end of the second agent's work period).
> The first agent's handoff document (the full §0-§7) remains valid background; this document **records only what this round added or overturned**.
> Working directory: `C:\Users\<USER>\Documents\codebase\rokid_root`

---

## 1. This round's conclusion (one sentence)

**The root cause of the reclaim failure is no longer a guess such as "SLUB does not return the slab to buddy"; this round used on-device measurement + source code + disassembly to localize three real mechanisms, and designed a v3 reclaim scheme that has not yet been measured. At the same time, however, a more fundamental unresolved anomaly was found: on RV101 the mm_struct allocation/free counters for forked processes are abnormal (see §4), and this must be clarified first.**

---

## 2. Newly established facts (already verified, do not redo)

### 2.1 SLUB 5.10.209 behavior (source already downloaded: `research/ghostlock_refs/slub_5.10.209.c`)
- mm_struct cache: order-2 (16KB), objsize 0x3C0, 17 obj/slab, **min_partial=5**
  (`set_min_partial(s, ilog2(s->size)/2)` → ilog2(960)/2=4 → clamped to MIN_PARTIAL=5),
  **cpu_partial=13** (set_cpu_partial: size≥256 → 13; the unit is "free objects").
- **The only gate for an empty slab being discarded back to buddy** is `n->nr_partial >= s->min_partial` (consistent in three places):
  - `__slab_free()`: when the last object is freed and the slab is on the node list (before `goto slab_empty`);
  - `unfreeze_partials()`: when the per-CPU partial chain is drained, an empty page is discarded only if nr_partial≥5; otherwise
    **an empty page entering node partial gets stuck forever**;
  - `deactivate_slab()`: the M_FREE path, same gate.
- `put_cpu_partial()`: the per-CPU partial chain (LIFO, a chain of pages) is unfrozen as a whole when `drain && pobjects > 13`; a new page enters the chain only after the drain.
- **Taken by another CPU**: a page on node partial (including an empty page) will be frozen and taken away by an mm allocation on any CPU (`get_partial_node`); a page on a per-CPU partial chain (frozen) cannot be touched by other CPUs.

### 2.2 RV101's order-2 (16KB) buddy is polluted by "child-process kernel stacks" (critical!)
- ~~RV101 has **no VMAP_STACK**~~ → **Corrected 2026-09-13: this machine has `CONFIG_VMAP_STACK=y` (`research/kernel_configs/kernel_config_1.22.009.txt`); that premise was wrong, and the order-2 stack-pollution mechanism of §2.2 does not hold**: every forked child process's kernel stack is taken directly from buddy order-2 as 16KB.
  Measured: forking 544 child processes moved `/proc/pagetypeinfo` order-2 Unmovable from 1872 → 47 → 1 → 0.
- After a child process is killed, its stack returns to the **LIFO head** of buddy via **RCU-deferred free** (`put_task_struct_rcu_user`, tens of ms later, on another
  CPU) — this squeezes the "leak slab that is discarded last" out of the buddy head.
  Measured: after killing 800 child processes, order-2 gained +299 blocks out of nowhere (RCU return flow).
- ⇒ The key payload of the old exploit grabbed the "just-freed stack page" every time instead of the leak slab — this is one of the most likely explanations for the old reclaim's "silent absorption".

### 2.3 Measured behavior of the old flow (slabprobe v1/v2, device details in §6)
- Old flow (heavy pre/post/spray/prepare flooding, leak-last, close(memfd_leak)):
  - the kill flood really did cause many slabs to be discarded back to buddy (mm slabs 57→26);
  - but **after settle 1.5s, close(memfd_leak) did not discard the leak slab** (slabs 27→27, unchanged) —
    at the last free, `nr_partial < 5` (node partial had been consumed by forks on other CPUs in the system),
    so the leak slab became "an empty page stuck in node partial" → permanently absorbing writes.
- key + 4×SKB vehicles consumed only 5 order-2 blocks (none of them the leak slab) → reading back showed no write trace.
  This matches exactly the "writes silently absorbed" phenomenon of §3 of the original manual.

### 2.4 Disassembly verification (artifacts in `research/ghostlock_refs/rv101_disasm/`)
- `copy_mm` @0xffffffc0081508c0: every fork unconditionally does `kmem_cache_alloc(mm_cachep
  [0xffffffc00aa6e158], 0xcc0)` (0xcc0 = GFP_KERNEL, no __GFP_ACCOUNT) → memcpy 0x3b0 → mm_init.
  With no CLONE_VM it must allocate (`tbnz w0,#8` is the share path).
- `__mmdrop` @0xffffffc00814cd08: `kmem_cache_free(mm_cachep, mm)`
  (0xffffffc0083b6384) is normal, with no RCU deferral.
- `exit_mm` @0xffffffc008158f34: starts with `futex_exit_release(current)` (0xffffffc0082336b4) +
  `mm_release` (0xffffffc00814d978) + munmap loop + mmap_lock operations + `current->mm=NULL`.
  **The tail (where mmput is called) has not yet been disassembled**: `0xffffffc0081590cc..0xffffffc008159280`.

---

## 3. v3 reclaim design (fully derived, **not yet measured**, implement first)

Approach: **do no large flood at all**; get the leak slab into the position of "the only/latest empty page on the per-CPU partial chain",
then use an "anchored self re-exec" to trigger one drain, making the leak slab the **last page freed back to buddy** in that discard, and **immediately** spray 8 SKB vehicles (one of which must get it).

Steps (replacing the current implementation inside `prepare_good_kernel_page()`):
1. fork pre(16) + leak child (KernelSnitch, pipe back the mm) + post(17).
   → the leak slab is 17/17 full → post's allocation deactivates it onto the **node full list**.
2. kill pre(16) → the leak slab's 1st free → freeze → enters the **CPU0 per-CPU partial chain** (inuse=1).
3. `close(memfd_leak)` → the last free (frozen free) → **the leak slab is empty and sits at the head of the chain**.
   Other CPUs cannot touch it at all (the chain is per-CPU).
4. Start anchord: `ghostlock_rv101 --anchor 16 <pipefd>`, pinned to CPU0; each round forks 17 pause
   children and then re-execs itself (state kept in argv). **Each exec frees one "full" mm slab**
   (the slab holding anchord's old mm = 17/17 full, on the full list → 1st-free → enters the chain).
5. On the 15th exec, `pobjects > 13` triggers a drain: 14 E-slabs (inuse=16) → node partial
   (nr_partial ≥ 14 ≥ 5 ✓); **the only empty page on the chain = the leak slab → discard-collect → the discard loop
   frees it last → head of buddy**.
6. anchord writes to the pipe to signal done; the main program **immediately** (no printf/other syscall in between)
   does `socketpair(AF_UNIX, SOCK_DGRAM)` × 8, each sending a 15872B payload (skb head kmalloc =
   15872+320=16192 ≤ 16384 → kmalloc_large order-2).
   - If the leak slab merged with adjacent blocks: the 8 vehicles will split it and take it block by block (covering a merge of ≤64KB).
7. The payload fills 8 identical copies using the "**page-view layout**" (see below); `fake_lock = slab_base + LOCK_OFF`
   (no more +0x18 delta; KEY_PAYLOAD_DELTA does not apply to SKB — SKB data is at block+0).
8. After the route finishes, `recv` reads back the 8 sockets and compares against pristine: **the one with a diff is the
   vehicle that got the slab**; forensics as before (child_pc/w0_right/usage/wait_lock/scratch).
9. No longer use the key vehicle (key quota maxbytes=20000/uid → only 1 16KB key possible; SKB has no limit).
   Also drop the large prepare/spray floods (the source of stack RCU pollution).

Code modification points: `target_rv101.h` (SKB_DATA_DELTA=0, fake_* = base+OFF), `util.c`
(rewrite prepare_good_kernel_page + anchord mode + 8-socket management + all-vehicle comparison),
`main.c` (--anchor mode entry). slabprobe already contains a usable anchord skeleton (`source_code/slabprobe/`).

---

## 4. ⚠️ Unresolved anomaly: the mm_struct count for forks (**must be investigated first**)

Measured (RV101, after reboot, clean environment with load 0.2, tool `source_code/slabprobe/forktest.c`):
- fork 34 children (libc `pause()`, children alive): `mm_struct act` rises only **+17** each time (consistent 3 times:
  204→221, 221→238, 238→255).
- fork 34 children (**pure raw syscall busy-spin**, /proc confirms state=R alive): act **+0**!
- kill all children (waitpid completes): act **never decreases** (unchanged for 10s+ of observation); the system's own
  `sh -c "exit 0"` run 10 times in a row also does not move it.
- But on the same machine the task_struct / signal_cache / pid counters all move normally (during boot, fork tests showed +30/+28/+32).
- Disassembly proves copy_mm unconditionally does kmem_cache_alloc(mm_cachep) and __mmdrop unconditionally does kmem_cache_free.
- The v1 old boot (an environment that may have been damaged by the 0x80080000 alias experiment) instead showed the exact
  count "221 forks → +221, 34 → +34" — so the anomaly is **not** permanent.

Priority experiments (in order):
1. **Memory-sharing test (already written, already compiled at `build/forktest`, not pushed)**: a child writes a global variable and exits,
   and the parent reads it. If the parent sees the value written by the child → **the child is actually sharing the parent's mm (CLONE_VM in effect?!)**,
   which would explain everything (no new mm allocation, no free, order-2 entirely consumed by stacks); if not → a counter/
   accounting-path problem.
2. If the mm is shared: check the clone flag passing (`syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0)` argument order on aarch64)
   plus whether the same call in `clone_child()` in the exploit is affected.
3. If each has its own mm: run the slabprobe v3 flow directly (§3); if reclaim succeeds then the anomaly does not affect the exploit chain
   (it may just be a weird slabinfo count); if it fails, disassemble the exit_mm tail + the mm_cache_init flags.

---

## 5. Environment notes (newly found this round)

- **There are now two devices attached to adb at the same time**: `<DEVICE_SERIAL>` (RV101, the target) and
  `<PHONE_SERIAL>` (Redmi Note 11 Pro, kernel 4.14, **not the target**).
  Every command must use `adb -s <DEVICE_SERIAL> ...`!
- RV101 has just rebooted (uptime ~14min, clean state, mm_struct 187/187, 11 slabs) — a good moment to rerun §4.1.
- `/sys/kernel/slab/*/*` is all Permission denied (shell); `/proc/slabinfo` and `/proc/pagetypeinfo`
  are readable; dmesg is readable (dmesg_restrict=0).
- key quota: maxbytes=20000, maxkeys=200 (each uid can only hold 1 16KB key at a time).
- ~~The device has no VMAP_STACK (§2.2)~~ → **Correction: it does have VMAP_STACK (see above)**; `ulimit -p` 6608, nofiles 32768; the adb shell cgroup has no pids.max.

## 6. Files added this round

| File | Contents |
|---|---|
| `source_code/slabprobe/{slabprobe.c,forktest.c,Makefile}` | Diagnostic tools (mm slab lifecycle observation, fork counting test, anchord skeleton); on device at `/data/local/tmp/{slabprobe,forktest}` |
| `research/ghostlock_refs/slub_5.10.209.c` | Official 5.10.209 mm/slub.c (for comparison of the reclaim mechanism) |
| `research/ghostlock_refs/rv101_disasm/{copy_mm,mm_init,__mmdrop,exit_mm}.txt` | New disassembly from this round |

The exploit source (`source_code/ghostlock_rv101/`) was **not modified** this round.
