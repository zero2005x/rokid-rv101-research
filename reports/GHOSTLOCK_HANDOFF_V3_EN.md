# GhostLock RV101 — Work Status Handoff Document v3

> 中文版：[GHOSTLOCK_HANDOFF_V3.md](GHOSTLOCK_HANDOFF_V3.md)

> 📌 **Basis of fact**: wherever this document touches facts such as device generation, kernel config, CVE status, GhostLock offsets and product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (a single authoritative fact table; that table decides any conflict).


> This document was updated at the end of the third agent's work (late night 2026-08-16, session continued).
> The v1/v2 handoffs (GHOSTLOCK_HANDOFF_V2.md, GHOSTLOCK_HANDOVER_PROMPT.md) remain valid background.
> Working directory: `C:\Users\<USER>\Documents\codebase\rokid_root`

---

## 1. Overall conclusion (one sentence)

**"Freed mm slab reclaim" has been solved by the v9 scheme (measured on device: the leak slab is synchronously discarded back to buddy through the
slab_empty gate at the instant of close(memfd), and 768 SKB vehicles really do take it away); but the first time the walk truly ran on "our fake page"
it began to kernel panic — this is a section inherited from v2 that was **never validated** (v2's poison proof only went as far as "reaching the forged
waiter", because the old reclaim kept failing and the fake page contents had never been touched by the walk). The reclaim problem is closed; the new
blocker is the walk's panic on the fake page.**

---

## 2. Facts resolved in this round (do not redo)

### 2.1 Task A (fork mm_struct count anomaly) — closed
- forktest discrimination experiment: the child writes a global then exits, and the parent reads 0x11111111 → **the child has an independent mm**
  (COW works normally), ruling out the CLONE_VM hypothesis. The aarch64 argument order of `syscall(SYS_clone, SIGCHLD, ...)`
  is not a problem.
- Disassembly of the tail of `exit_mm` is complete: **it really does call mmput** (0xffffffc0081590ec: `mov x0,x20; bl
  0xffffffc00814d2b0`, unconditional path), so v2's doubt is resolved.
- Explanation of the count anomaly: SLUB per-CPU invisibility (the cpu_slab page + the per-CPU partial chain are not counted in
  /proc/slabinfo) + **phantom slab** (this kernel's allocation slow path discards a full CPU page via
  DEACTIVATE_BYPASS into a phantom page with "inuse=17, no list at all" that never enters the full list and is completely invisible
  to slabinfo; it is only frozen into the chain at 1st-free).
- There is also a batched-free phenomenon (see §4.3), but it does not affect the v9 design (v9 frees synchronously via memfd close).

### 2.2 Task B (reclaim) — solved: the v9 scheme (validated on device)
Measured evidence (pagetypeinfo inside the exploit with GL_DEBUG_DRAIN=1):
```
[after-flood-closes] Unmovable ... o2=2344
[after-leak-close]   Unmovable ... o2=2345   ← close(memfd_leak): +1 order-2 block instantaneously
[after-vehicles]     Unmovable ... o2=1584   ← 768 SKB vehicles take away -761 blocks
```
o2 +1 appears at **order-2** (not a higher order) → the leak slab was not merged by buddy → **the 1st vehicle
(kmalloc-16k eating the buddy head directly) is the one that gets the leak slab, and the payload is at slab base+0**.

**The v9 flow (util.c `prepare_good_kernel_page()`, current code)**:
1. fork flood(544+221=765) + pre(16) + leak + post(17).
2. **open `/proc/pid/mem` for every child** (memfd pins all the mms).
3. The leak child runs kernelsnitch → writes the mm back → exits (memfd holds a ref, so the mm is not released).
4. **kill all children** (pinned by memfd → the mm is not released on exit, so there is no "batched/delayed free" problem).
5. Pre-create 768 AF_UNIX DGRAM socketpairs.
6. Close the memfds in order: pre → post → flood → **immediately close(memfd_leak) (do not settle in between!)**.
   On the final free the leak slab is "an empty slab on the node partial with nr_partial ≥ 5" →
   the slab_empty gate of `__slab_free` **synchronously** discards it to buddy inside the close() system call.
7. Immediately send 768 × 15872B (identical payload, page-view layout, SKB data at block+0).
8. Return base (= page_base); after route finishes, recv all vehicles and compare against pristine.

**Why v3/v4/v5/v6/v7/v8 all failed and v9 succeeded** (hard-won lessons; do not step in them again):
- v3/v4/v5 relied on draining the per-CPU chain when pobjects>13: **pobjects drain is empirically never triggered on this kernel**
  (960 keepers ≈ 56 pobjects, 2000 ≈ 117 pobjects, and slabinfo never moves throughout; see §4.1).
- v6/v7 relied on the free after kill: child mm free on this kernel has **batched/delayed** characteristics (§4.3),
  and the timing is uncontrollable.
- The key to v9: **memfd pins it → kill does not release → only close() frees**, so the timing is fully controllable;
  and v2 had already demonstrated that "the flood flow can push the leak slab onto the node partial", so v9 only fixed v2's
  single error of "settling 1.5s lets the system eat up the node partial" (changed to closing immediately).
- Key constants (measured): mm_struct objsize=960, 17 obj/slab, order-2; min_partial=5;
  cpu_partial=13 (the 2/6/13/30 table of set_cpu_partial is confirmed in the kmem_cache_open disassembly).

---

## 3. New blocker: punch's walk kernel-panics on the fake page (to be solved)

### 3.1 Symptom
- Old versions (reclaim failed, slab A is garbage): after the walk reaches fake_lock (garbage contents) it **terminates benignly with no
  panic** (the garbage contents happen to pass the bail conditions).
- v9 (reclaim succeeded, a vehicle holds the payload): **punch's walk panics directly and the device reboots**
  (output stops at "pselect route ...", with no "pselect returned").
- Every panic → automatic reboot in 2-5 minutes, KASLR slide changes (the exploit re-leaks automatically).

### 3.2 Bisection done so far (results)
| Variant | Result |
|---|---|
| GL_POISON=1 (PWS_LOCK=0xdead…) | panic (expected: after the walk reaches the forged waiter, lock 0xdead → fault) |
| Normal fake page | panic |
| GL_NOOWNER=1 (owner=0, bypassing the fake-task section) | panic (→ the crash is in the erase/insert/lock section, not the fake-task section) |
| **BLACK-root fix** (W0's parent_color 0→1) | still panics |

**BLACK-root fix (applied, retained)**: the payload originally set W0's `parent_color` to 0 (and the comment even
said "BLACK (0)" — wrong). Linux rbtree `RB_BLACK=1`. pc=0 means "RED with no parent", and
rb_insert_color (0x7749e4), seeing a RED parent, takes the rotation path and reads the grandparent: `ldr x10,[x8,#8]`
with x8=0 → **NULL deref → panic**. Changed to `put64p(..., 1)` (there is a comment in util.c).

### 3.3 Full disassembly reading of the walk (complete; can be cited directly)
`rt_mutex_adjust_prio_chain` @ **0xffffffc0081ceaf4** (punch path:
rt_mutex_adjust_pi @ 0x1ce9fc → `rt_mutex_adjust_prio_chain(task, chwalk=0,
orig_lock=NULL, next_lock=x20=waiter->lock, orig_waiter=NULL, top_task=task)`).
Key flow (x19=task=A, x25=A->pi_blocked_on=forged waiter):
1. 0x1ceb7c: `next_lock` must == waiter->lock (both from the same source, always true).
2. 0x1cec00: x24 = waiter->lock = fake_lock → 0x1cec08 trylock (`bl 0x9409df0`,
   proceeds only if it returns 0) → **writes [fake_lock+0]=1**.
3. 0x1cec3c: `(owner & ~1)` vs top_task(A); owner=fake_task|1 ≠ A → continue.
4. 0x1cec54: x0 = lock->waiters.rb_leftmost (=w0); 0x1cec60 reads [w0+0x38]=w0->lock,
   which must == fake_lock (otherwise 0x1cf380 `brk #0x800` → panic).
5. 0x1cecb0: `[forged] != forged` and `w0 != forged` → 0x1ceccc **rb_erase
   (&forged, &lock->waiters)** (rb_erase @ 0x774b38) → **the controlled write primitive**.
6. 0x1cecdc-0x1ced68: reinsert into the tree (searching w0's prio=130 decides left/right), `[forged]=w0` (pc),
   `[w0+child]=forged`, rb_insert_color(&forged, root) (root=x1=x24+8 is correct).
7. 0x1cedd4: owner ≤ 1 → 0x1cf2d0 unlock and return; otherwise x19=fake_task → usage++ (0x101),
   fake pi_lock(0x1A5C) spin, pi_blocked_on=0 → 0x1cf238 clean finish.
8. Two brk traps: 0x1cf380 (w0->lock≠fake_lock), 0x1cf384 (after insertion leftmost->lock
   ≠fake_lock).

`rb_erase` @ **0x774b38** (case-1: left==NULL): parent=pc&~3=target-8;
parent->left==node ? parent+0x10 : parent+8 ← child → **8-byte store of write_value at
target**; child->pc=pc (writes SAFE_OFF). Confirmed.
`rb_insert_color` @ **0x7749e4**: parent BLACK → return directly; parent RED → rotation path
(NULL deref when pc=0, see §3.2 BLACK fix).

### 3.4 Suggested next steps (in order)
1. **Capture the exact location of the panic**: `dmesg` is readable (dmesg_restrict=0)! On the next selftest,
   **open another adb shell in parallel and run `dmesg -w`**; the panic message (BUG/oops/PC) will be printed
   before the reboot — this is the only way to obtain the panic scene on this device (pstore is always empty, no ramoops).
2. Prime suspect: **the trylock helper (0x9409df0) and unlock (0x940a2ac)** — if the kernel enables
   CONFIG_DEBUG_SPINLOCK / PROVE_LOCKING, the fake wait_lock (pure 4-byte zero) may trip the
   debug checks (bad magic / unregistered lockdep_map). Disassemble 0xffffffc009409df0,
   0xffffffc00940a2ac, 0xffffffc00940a208, 0xffffffc00940a0c8.
3. If the lock is not the problem, use "controllable panic" to bisect the later part of the walk:
   - change rb_leftmost to 0xdead… (payload or the PWS path) → if the panic is at the [0xdead+0x38] read
     (0x1cec60) → confirms it reaches that point;
   - change PWS_TREE_LEFT to non-zero → takes rb_erase case-2/3 (vendor version at 0x774b48/0x774bb8);
   - point erase's child at an illegal address (write_value=0xdead) to see whether the store fault occurs as expected.
4. Selftest pass criteria (the cleanup forensic, already printed by the current code):
   `child_pc=(target-8)|1`, `scratch=<written value>`, `fake_task_usage=0x101`, and a vehicle
   showing a diff line.
5. Once selftest is stable → W1 (--write1, writing selinux_state.enforcing=0 → getenforce
   Permissive) → W2 (FOPS route, see §5).

---

## 4. Measured anomaly log (not fully explained, but v9 already bypasses it)

### 4.1 pobjects drain is never triggered (though the disassembly looks vanilla)
- put_cpu_partial @ **0x3b9fd4** disassembly: it unfreezes only if `pobjects > cpu_partial([kmem_cache+0x2c])`
  — vanilla logic; set_cpu_partial (inside kmem_cache_open @ 0x3b8a6c) writes 2/6/13/30
  to +0x2c; mm_struct(size 960) → **13**.
- But measured: microtest forks 238 (=14 slabs), kills them all → 14 1st-frees → pobjects should be 14 > 13
  → yet mm_struct slabs in slabinfo **never move at all** (25→25→25, sampled every 50ms for 2 seconds); 960/2000
  keepers behave the same. This is presumed related to the batched free of §4.3 (the free may land on another CPU's chain),
  so **do not design anything else using pobjects drain**.
- `unfreeze_partials` @ 0x3bac58: vanilla (discards only when the page is empty and nr_partial>=min_partial(+0x10),
  otherwise it goes to the node partial).
- `__slab_free` @ 0x3b93bc: 1st-free (prior==NULL) → freeze → put_cpu_partial(page,1),
  vanilla.
- `rcu_free_slab` @ 0x3bcab4 / `__free_slab` @ 0x3bcae4: for a cache without
  SLAB_TYPESAFE_BY_RCU, discard goes through __free_slab straight back to buddy (mm_struct does not need to wait for RCU).
- `___slab_alloc` @ 0x3b7264: the slow path pops the chain head (when an empty page is popped, get_freelist
  fakes inuse=17 — **by design, no mm allocation may happen after the leak slab goes on the chain**).

### 4.2 slabinfo invisibility of fork/kill
microtest: fork 238 (14 new slabs) → slabinfo does not change at all; it does not change after kill either; o2 instead drops by
-14. Explanation: the new slabs all become phantom (DEACTIVATE_BYPASS), and the 1st-free enters the per-CPU chain (invisible).
⇒ Always use pagetypeinfo's order-2 as the primary observation method.

### 4.3 Batched/delayed child mm free
The v6 flood trace (850 kills + sampling after each kill): mm slabs 62→62(200 kills)→57→43→29→28,
**in batches of -5/-14/-14**. It is presumed that mm release after exit has a batched path (worker/RCU style), so
v6/v7's timing of "close immediately after kill" is unreliable; v9 bypasses it entirely with memfd.

---

## 5. W2 prerequisite data (FOPS route; perf is blocked with paranoid=3)

OnePlus reference implementation: `research/ghostlock_refs/extracted/oneplus/.../src/core/fops.c`.
RV101 kallsyms is ready (offset = addr - 0xffffffc008000000):
- `ashmem_fops` @ 0x1e977a8 (d), `ashmem_misc` @ 0x29bcbb0 (miscdevice, fops at +0x18),
  ashmem_ioctl/mmap/open/release/show_fdinfo @ 0xc67e2c/0xc687e0/0xc68a04/0xc68a90/0xc68bb4.
- `configfs_file_operations` @ 0x1d46840, `configfs_bin_file_operations` @ 0x1d46960,
  configfs_write_bin_file$… @ 0x49ee00, configfs_read_bin_file$… @ 0x49ec40.
- `init_task` @ 0x2a1eb840, `init_cred` @ 0x2a202058, `selinux_state` @ 0x2aca0140
  (consistent with target_rv101.h), `commit_creds` @ 0x18b328.
Design (v1 handoff): W2 uses a mode-2 write to write `fake_fops` (a file_operations table inside the fake page)
into ashmem_miscs[0].fops → read/write on an opened /dev/ashmem becomes arbitrary configfs read/write →
walk the task list from init_task.tasks to find the child → use a mode-2 write for child->cred(0x780)=
data_addr(INIT_CRED). One can also first rely only on arbitrary configfs reads to find the child task (the substitute when perf is unavailable).

---

## 6. Files changed in this round

| File | Content |
|---|---|
| `source_code/ghostlock_rv101/src/util.c` | **v9 reclaim** (memfd flood + immediate close + 768 SKB vehicles); recv-diff forensic (prints only vehicles with a diff); GL_NOOWNER switch; **BLACK-root fix**; poll_mm_slabs() (reads mm_struct active slabs from slabinfo); debug_pagetype() (GL_DEBUG_DRAIN=1) |
| `source_code/ghostlock_rv101/target_rv101.h` | SKB_DATA_DELTA=0, SKB_RECLAIM_SENDS=768, v9 comments |
| `source_code/slabprobe/slabprobe.c` | v4 flow (keeper pool + sparse kill + per-phase sampling) |
| `source_code/slabprobe/microtest.c` | **new**: a discrimination tool for fork 238/kill/50ms sampling (mm slabs + o2) |
| `research/ghostlock_refs/rv101_disasm/{slab_free_path,put_cpu_partial_real,__slab_free_real,exit_mm_tail,rt_mutex_adjust_prio_chain,rb_erase,rb_erase_tail,rb_insert_tail,kmem_cache_alloc}.txt` | **new**: this round's disassembly artifacts (addresses in §3.3/§4) |

Exploit build (do not forget -B): `$env:ANDROID_NDK_HOME='C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\android-ndk-r27d'; cd source_code\ghostlock_rv101; mingw32-make -B`.
⚠️ PowerShell pitfall: `mingw32-make -B 2>$null | Select-Object -Last 1` swallows make's
failure code ($LASTEXITCODE becomes 0 from the last stage of the pipeline) — this round pushed a stale binary once because of it.
Always: after building, compare `Get-FileHash build\ghostlock_rv101 -Algorithm MD5` with the device's
`adb shell md5sum /data/local/tmp/ghostlock_rv101` (current value: 237c80dae80a6e970a2f4cac1aa3c86a).

---

## 7. Environment notes

- Two devices: target `<DEVICE_SERIAL>` (RV101); `<PHONE_SERIAL>` is a Redmi, do not touch it.
  Every adb command carries `-s <DEVICE_SERIAL>`.
- Every panic → automatic reboot (2-5 minutes), the KASLR slide changes (the exploit re-leaks automatically).
  After a reboot, confirm the binary is still there (/data is preserved; if in doubt, compare md5).
- shell uid=2000, SELinux Enforcing, kptr_restrict=2, dmesg readable (dmesg_restrict=0),
  pstore always empty, perf blocked, /sys/kernel/slab all Permission denied.
- Key readable probes: /proc/slabinfo (mm_struct active slabs), /proc/pagetypeinfo (order-2
  Unmovable = number of 16KB blocks), dmesg -w (capture the panic message in parallel).
- The poison tests (GL_POISON/GL_POISON2) can still be used to bisect walk progress; each one costs a reboot.
- key quota: maxbytes=20000 (v9 no longer uses key; everything is SKB). ulimit -p 6608: kernelsnitch's
  4096 threads + flood 765 child processes ≈ 5000, close to the limit, so do not increase the flood further.
- The GL_VEHICLES env can adjust the vehicle count (default 768); GL_NOOWNER=1 bypasses the fake-task section (for bisection).
