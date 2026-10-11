# Handoff Prompt — GhostLock RV101 (CVE-2026-43499), Third Agent

> 中文版：[NEXT_AGENT_PROMPT.md](NEXT_AGENT_PROMPT.md)

> ⚠ **2026-09-13 update**: the working directory has changed to `/home/kali/rokid_root` (WSL/Kali; the old
> `C:\Users\<USER>\Documents\codebase\rokid_root` is no longer used). **Runtime privilege escalation is complete and stable**
> (two paths, bridge/Shizuku, armed once per boot over adb); the current maintenance focus is the **operations layer**
> (keep-alive watchdog, engine auto-probing, privilege-escalation metrics), see `docs/deploy.md` §privilege-escalation operations P0–P3.


> Feed the full text below directly to the next AI agent. For background details, have it read `reports/GHOSTLOCK_HANDOFF_V2.md`
> and the original handoff document (the first GhostLock handoff file at the root of the working directory). Working directory:
> `C:\Users\<USER>\Documents\codebase\rokid_root`

---

You are taking over a **CVE-2026-43499 (GhostLock) local privilege escalation exploit** project targeting the
**Rokid RV101 (RG-glasses, Android 12, kernel 5.10.209-perf, Qualcomm 4-core)**.
GhostLock is a stack-UAF in the rt_mutex futex PI code: `remove_waiter()` mistakenly clears
`current->pi_blocked_on` (rather than the waiter's own); after a 3-futex PI chain and a FUTEX_CMP_REQUEUE_PI
-EDEADLK rollback is triggered, the waiter's `pi_blocked_on` dangles at a dead rt_mutex_waiter.
pselect is used to forge a waiter and sched_setattr to punch, achieving a controlled write (rb_erase's
`parent->rb_right = child`). The RV101 kernel is confirmed unpatched (`remove_waiter.disasm.txt`).

**Project status: the exploit is about 90% complete (KASLR leak, KernelSnitch mm leak, GhostLock trigger,
pselect forging, punch, and walk reaching the forged waiter — all proven by poison testing). The only blocker is
"reclaim of the freed mm slab"; once fixed, W1 can be verified (selinux_state.enforcing=0 →
Permissive), and then W2 completed (cred=init_cred → root shell).**

## Required Reading (finish these before starting work)

1. `reports/GHOSTLOCK_HANDOFF_V2.md` — the previous agent's work status (including new findings and the v3 reclaim design).
2. `source_code/ghostlock_rv101/target_rv101.h` — the single source of truth for all offsets/constants.
3. `source_code/ghostlock_rv101/src/{main.c,util.c,route.c}` — the exploit body.
4. `source_code/slabprobe/{slabprobe.c,forktest.c}` — diagnostic tools written by the previous agent.
5. `research/ghostlock_refs/slub_5.10.209.c` — source-level reference for the SLUB reclaim mechanism.

## Environment

- Device over adb: **serial `<DEVICE_SERIAL>`**. ⚠️ A second device is now attached at the same time
  (Redmi Note 11 Pro, `<PHONE_SERIAL>`, kernel 4.14, not the target) —
  **every adb command must include `-s <DEVICE_SERIAL>`**.
- shell uid=2000, SELinux Enforcing, kptr_restrict=2, dmesg readable, perf blocked (paranoid=3).
- The device has just rebooted (clean state). Each panic triggers an automatic restart (2-5 minutes) and the KASLR slide changes
  (the exploit re-leaks it automatically).
- Build (Windows + NDK r27d):
  ```powershell
  $env:ANDROID_NDK_HOME='C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\android-ndk-r27d'
  cd source_code\ghostlock_rv101
  mingw32-make -B
  adb -s <DEVICE_SERIAL> push build\ghostlock_rv101 /data/local/tmp/ghostlock_rv101
  adb -s <DEVICE_SERIAL> shell chmod 755 /data/local/tmp/ghostlock_rv101
  ```

## Task List (in order)

### A. First resolve the "fork mm_struct counting anomaly" (a conclusion should be reached within 1 hour)
The previous agent measured: forking 34 live child processes moved slabinfo's `mm_struct act` by only +17 or +0; after kill it never
drops; but task_struct/pid/signal_cache counts are normal; disassembly of copy_mm/__mmdrop confirms both are unconditional
kmem_cache_alloc/free(mm_cachep). See HANDOFF_V2 §4 for details and hypotheses.

1. First push and run the already-compiled but not-yet-pushed memory-sharing test (this is the most decisive experiment):
   ```powershell
   cd source_code\slabprobe
   mingw32-make -B build/forktest
   adb -s <DEVICE_SERIAL> push build\forktest /data/local/tmp/forktest
   adb -s <DEVICE_SERIAL> shell chmod 755 /data/local/tmp/forktest
   adb -s <DEVICE_SERIAL> shell /data/local/tmp/forktest
   ```
   Interpretation: if the parent reads the value written by the child (≠0x11111111) → the child is somehow sharing the parent's mm (clone flag problem)
   → fix the clone call (cross-check using libc fork()) and retest; if the parent still sees 0x11111111 →
   it is a counter/accounting anomaly, continue to step 2.
2. Disassemble the tail of `exit_mm` (after 0xffffffc0081590cc, look for the mmput@0xffffffc00814d2b0 call)
   and `mm_cache_init` (0xffffffc00a063a10, to check whether the cache flags include SLAB_TYPESAFE_BY_RCU
   or anything odd), using `scripts/ghostlock_analysis/disasm_func.py`.
3. If it is still unresolved: skip straight to B and measure reclaim empirically (the anomaly does not necessarily affect the exploit chain — as long as the leak slab can reach the buddy allocator).

### B. Implement and verify the v3 reclaim scheme (the core of this round)
The full derivation of the v3 design is in HANDOFF_V2 §3. Key points:
- Abandon the large flood (RCU-delayed freeing of child-process kernel stacks pollutes the buddy LIFO head — this device has no
  VMAP_STACK); instead use pre(16)+leak+post(17) to place the leak slab on the per-CPU partial list,
  then use anchord (`--anchor 16`, self re-exec 16 times, forking 17 paused children each round) to trigger
  one drain, making the leak slab the last page discarded back to the buddy allocator.
- Immediately (with zero printf / zero syscall in between) spray 8 AF_UNIX DGRAM sockets (each sending 15872B,
  skb head kmalloc=16192 → order-2), with identical payloads and a page-view layout
  (the fake structure at offsets such as block+LOCK_OFF, SKB data delta=0; fake_lock=slab_base+LOCK_OFF).
- After route, recv all 8 sockets and compare with pristine; any that differ means the slab was obtained.
- First reproduce it in slabprobe (modify slabprobe.c, or modify the exploit directly and use `--selftest`).
  Success criteria: the selftest forensics show `child_pc=(target-8)|1`, `scratch=<written value>`,
  `wait_lock=1`, `fake_task_usage=0x101` and other write traces (cross-reference the offsets in target_rv101.h).

### C. W1: SELinux Permissive
Once selftest consistently shows a diff: `adb -s <DEVICE_SERIAL> shell /data/local/tmp/ghostlock_rv101 --write1`
→ `getenforce` should return Permissive. If it has no effect and there is no panic: check the target alias
(data_addr(SELINUX_STATE)=0xffffff802b4a0140) and P0_KERNEL_PHYS_LOAD (0x80800000;
0x80080000 previously wrote to random RAM and panicked, do not use it).

### D. W2: cred=init_cred (FOPS route; the perf route is unusable because perf is blocked)
Follow the original handoff document §5: write a fake fops table into ashmem_miscs[0].fops → arbitrary read/write via configfs →
walk the task list from init_task.tasks to find the child → task->cred(0x780)=data_addr(INIT_CRED) →
have the child write enforce=0 and exec /data/local/tmp/.ghostlock_root.sh.
Grep the required symbols from `research/ghostlock_refs/rv101_kallsyms.txt`.

### E. Deliverable
- A single ELF `build/ghostlock_rv101` (reliably obtains a root shell under uid 2000) + usage instructions
  (README or report), and add this round's findings to `reports/GHOSTLOCK_HANDOFF_V2.md` or open a new v3.

## Acceptance Criteria
1. The write traces from §B appear in selftest → reclaim is fixed.
2. `getenforce` returns Permissive → W1 succeeded.
3. `id` returns uid=0 → W2 succeeded.
4. Stable = 3 consecutive full-pipeline successes with no panic.

## Cautions
- The poison tests (GL_POISON/GL_POISON2) deliberately panic; use them only when you need to bisect walk progress.
- pstore is empty, so you cannot debug via crash logs; the only probes are poison and the selftest diff.
- After changing target_rv101.h you must run `mingw32-make -B`.
- Do not touch the other Redmi device (every adb command carries `-s <DEVICE_SERIAL>`).
