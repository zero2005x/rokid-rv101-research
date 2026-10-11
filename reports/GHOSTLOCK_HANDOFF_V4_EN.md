# GhostLock RV101 — Work Status Handoff Document v4

> 中文版：[GHOSTLOCK_HANDOFF_V4.md](GHOSTLOCK_HANDOFF_V4.md)

> This document was updated at the end of the fourth agent's work period (early morning 2026-08-17).
> The v1/v2/v3 handoffs (GHOSTLOCK_HANDOVER_PROMPT.md / GHOSTLOCK_HANDOFF_V2.md /
> GHOSTLOCK_HANDOFF_V3.md) remain valid background, but two key assumptions of v3 were overturned this round (see §2).
> Working directory: `C:\Users\<USER>\Documents\codebase\rokid_root`

---

## 1. Overall conclusion (one sentence)

**v3's "walk kernel panics on the fake page" was actually a "garbage-page walk caused by reclaim failure": the payload was never
confirmed by vehicle forensics to be in place — v9's o2 evidence was only an inference, and a real payload-walk has never once been
observed to date. This round fixed the punch timing, the reclaim close ordering, and the buddy head-grab race one by one, and the gate
can now trigger sporadically (hard evidence in run 10: o2 2470→2471), but the final step of "payload in place + walk writes + forensics
visible" has not yet been achieved.**

---

## 2. Facts closed this round (do not redo)

### 2.1 Two v3 assumptions overturned
1. **dmesg cannot capture the panic**: on this device a panic is a hard reset, the ring buffer ends cleanly, and there is no oops output
   whatsoever (confirmed at line 565551 of `ghostlock_work/dmesg_run3.log`). v3's "parallel dmesg -w" plan is useless,
   **the panic PC is unavailable on this device**, and progress can only come from forensics (vehicle diff) + controlled-fault bisection.
2. **The "fake wait_lock spinlock debug check" hypothesis is dead**: the lock helpers have been disassembled
   (`rv101_disasm/spinlock_helpers.txt`) and are all vanilla qspinlock (`ldaxr/stxr` 4-byte cmpxchg), with
   no DEBUG_SPINLOCK magic/owner checks and no lockdep. 0x9409df0=trylock, 0x9409f70=lock,
   0x940a0c8=lock_irq, 0x940a208=unlock_irq, 0x940a2ac=unlock, 0x940a004=lock_irqsave.

### 2.2 Facts about trigger / walk
- **errno 35 = EDEADLK (not EAGAIN!)**: requeue stably returns -EDEADLK every time (already recorded in v1 §67,
  reconfirmed this round). The vendor futex_requeue EDEADLK fixup (0x235180) does
  `str xzr,[rt_waiter+0x38]` (waiter->lock=NULL) and then returns directly.
- **The walk really does reach the forged waiter**: GL_POISON=1 (PWS_LOCK=0xdead) panics at punch 1
  in both run 5 and run 9 (trylock [0xdead] fault) — note that poison only proves the walk read forged stack bytes,
  **it does not prove the payload is in place** (the fault is at 0xdead, unrelated to page contents).
- **The static path of the payload-walk is complete with no fault** (rt_mutex_adjust_prio_chain disassembly checked line by line):
  5 permanent writes: leftmost(0xE10)=forged, w0->left(0x1010)=forged, usage(0x1240)=0x101,
  scratch(0x1300)=write_value, child_pc(0x1500)=(target-8)|1; wait_lock goes 1 then 0 (net zero).
  The tail of the NOOWNER=1 path calls wake_up_process(init_task) (0x9228cf8 veneer→0x193854,
  w1=3=STATE_NORMAL, harmless, **not put_task_struct**, init_task usage is not consumed).
- Punch timing has been fixed: the original 5ms often landed before A entered select (the walk used a stale rt_waiter, because
  waiter->prio==task->prio returns benignly at 0x1cebbc). It is now multi-punch: +20/55/90/125/160ms,
  5 shots in total, alternating nice 19/18 (each shot is a real policy change and each triggers a PI walk), with
  "pselect: calling select now / select returned" markers to align the window.

### 2.3 Facts about the reclaim mechanism (the core work of this round)
- **v9's two races (localized and fixed this round)**:
  1. node-partial pop race: v9's ordering put the leak slab at the head of the node partial chain on the earliest drain,
     and any concurrent mm allocation would pop it away → the gate never triggers. Fixed (v9.1 close reordering, see §5).
  2. buddy-LIFO theft: after leak-close and before the first vehicle send, a system forked kernel stack
     (an order-2 buddy allocation) steals the leak slab at the LIFO head; the o2 debug read also stretched the window to the ms scale.
     Fixed (v9.2: settle + zero reads before the first send, a µs-scale window).
- **The o2 check is unreliable** (a hard lesson this round): RCU return flow from flooded child-process kernel stacks causes ±hundreds of blocks
  of o2 noise (run 7 attempt 2 showed +312; run 12 attempt 1 showed +63). **The only reliable criterion = vehicle
  forensics (a diff ⟺ payload in place + the walk wrote to our page)**. do_one_write has been changed to forensics-
  driven retry (KERNEL_PAGE_SETUP_ATTEMPTS=12).
- **Hard evidence of gate triggering (run 10)**: tight probe `o2 2470 -> 2471` — +1 at the instant of leak-close,
  so the leak slab really was discarded back to buddy by the slab_empty gate. This proves the v9.1/v9.2 mechanism chain
  (pobjects 14>13 → drain → leak slab sinks to the bottom of node partial → gate) can be made to work.
- SLUB disassembly has been checked (`rv101_disasm/{put_cpu_partial_real,__slab_free_real,unfreeze_partials}.txt`):
  the drain condition `pobjects > cpu_partial(13)` ✓, the unfreeze_partials empty-page gate `inuse==0 &&
  nr_partial >= min_partial(5)` ✓, add_partial is add-at-head (a page at the chain head sinks to the bottom of the list =
  pop-proof) ✓.
- clone/memfd failure counters have been added (run 10/11/12 all fail=0): the slab arithmetic (45 flood slabs +
  leak slab 17 obj + post slab) is intact; it was not broken by failed clones.

---

## 3. New blocker: payload placement still not achieved (the only task)

### 3.1 Symptoms (runs 7/8/10/11/12)
- run 7 (v9.1+gate check): 4 attempts o2 -1 / +312 / -1 / +1 — "+1 but zero writes" (possibly
  buddy theft: after the gate the leak slab was taken by a system stack and vehicle[0] got another block).
- run 8 (v9.2): attempt 1 zero writes (benign garbage walk), attempt 2 panic (garbage fault).
- run 10 (v9.2+tight probe): gate +1 confirmed, but the tight read (~1ms) reopened the buddy theft window →
  punch 1 panic.
- run 11 (boot 0 minutes): punch 1 panic (hectic boot, garbage-page walk).
- run 12 (stable boot 5 minutes): attempt 1 full sequence o2 +63 (**an RCU noise torrent — a 150ms settle is clearly
  insufficient**) with zero writes; attempt 2 punch 2 panic.

### 3.2 Suspect ranking (the next agent should follow this directly)
1. **Insufficient settle** (number one): 150ms cannot flush the RCU stack return flow of 765 children (run 12's +63
   proves that a large batch of stacks still returns during the shutdown sequence, and can land at any moment inside the µs window
   between leak-close and the first send).
   → Switch to **adaptive settle**: poll o2 after kill (read once every 100ms) and only enter the close sequence
     once two consecutive reads are identical (quiet); or simply fix it at 1-2s.
2. **The gate's nr_partial margin**: each slab in the late-stage flood keeps 1 object (16/17 closed), which already gives ~33 slabs
   of buffer, theoretically enough; if it still misses sporadically, fine-tune V9_PREPARE_CNT/SPRAY_CNT (but ulimit -p 6608
   is close to the ceiling, so do not add too many).
3. **Residual probability of buddy theft**: after a µs-scale window + quiet settle it should be negligible; if it still happens,
   consider creating all 768 socketpairs before the first send (already done) and using sendmmsg to send the first shot in one call.

### 3.3 If placement is achieved but the walk still panics → "safe branching" bisection without a PC (contingency)
When the payload is in place, the walk's fault point can be bisected by "changing some field to a safe value so the walk branches off
cleanly early" (one run per variant; panic = it did not branch at that point):
- Variant A: RTMUTEX_WAITERS_LM_OFF=0 (leftmost=0 → 0x1cec5c cbz branch, skipping the w0 read;
  if it no longer panics → the fault is in the w0-related read at 0x1cec60/[w0+0x40]).
- Variant B: GL_NOOWNER=1 (skips the fake-task section).
- Variant C: W0_OFF+WAITER_LOCK_OFF=0xdead… (moves the fault to the controllable read at 0x1cec60).
- The criterion is still the 5 forensic writes (see §2.2) — how far it can write shows how far the walk went.

---

## 4. Files changed this round (source, all built and pushed, do not roll back blindly)

| File | Contents |
|---|---|
| `src/route.c` | multi-punch (5 shots +20..160ms, alternating nice 18/19), markers before/after punch, PUNCH_COUNT/INTERVAL constants, adjustable GL_PUNCH_DELAY_USEC |
| `src/util.c` | v9.1 close reordering (post[0]→flood F0-F11→**pre[0] triggers drain**→pre[1..15]→post[1..16]→late-stage flood 16/17); v9.2 settle 150ms + zero reads before the first send; debug_pagetype_o2(); clone/memfd failure counters; cleanup_page_prepare_state returns the diff count |
| `src/main.c` | do_one_write changed to forensics-driven retry (no diff → retry, up to 12 times) |
| `src/common.h` | cleanup_page_prepare_state signature changed to int |
| `target_rv101.h` | PSELECT_ENTER_DELAY_USEC 5000→20000 |

Build (do not forget -B and the md5 comparison): `$env:ANDROID_NDK_HOME='C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\android-ndk-r27d';
cd source_code\ghostlock_rv101; mingw32-make -B`.
⚠️ The PowerShell pipeline swallows make's failure code (already recorded in v3 §7), so always compare Get-FileHash against the device md5sum.
End-of-round binary md5: `01bd9e97992f79d1a83f66589d011922` (already synced on the device).

---

## 5. Artifacts added this round

| Location | Contents |
|---|---|
| `reports/GHOSTLOCK_V3_PANIC_ROOTCAUSE_ANALYSIS.md` | Complete analysis notes for this round (including the evidence table for runs 1-12) |
| `reports/ghostlock_runlogs/exploit_run{2..12}*.log` | 11 on-device measurement logs |
| `research/ghostlock_refs/rv101_disasm/{spinlock_helpers,fn_193854,qspinlock_slowpath,rt_mutex_adjust_pi,rb_helper_77512c,caller_9228cf8,unfreeze_partials}.txt` | 7 new disassembly files |
| (workspace) `RideFlux/ghostlock_work/dmesg_run{2,3}.log` | Full dmesg streams (34/93MB each, proving the panic produces no output; line 565551 of run3 is the last line of the old boot) |

---

## 6. Environment notes (supplementing v3)

- Two devices: the target `<DEVICE_SERIAL>` (RV101); the Redmi `<PHONE_SERIAL>` went offline at the end of this round,
  and if it reappears, still do not touch it.
- Every panic → automatic reboot in 2-5 minutes and a changed KASLR slide (the exploit re-leaks automatically).
- **Neither dmesg nor pstore can obtain panic messages**: stop spending time there; the only progress probes are forensics and
  controlled-fault (poison or safe-branching variants).
- Before every selftest run, wait for the boot to stabilize (uptime ≥ 4-5 minutes) to significantly reduce noise (run 11 with boot 0 minutes
  blew up immediately).
- How to run (generating a log):
  `adb -s <DEVICE_SERIAL> shell "/data/local/tmp/ghostlock_rv101 --selftest" 2>&1 | Tee-Object exploit_runN.log`
  Diagnostic switches: GL_DEBUG_DRAIN=1 (full o2 table), GL_POISON/GL_POISON2, GL_NOOWNER, GL_VEHICLES,
  GL_PUNCH_DELAY_USEC, SELFTEST_RUNS.
- Selftest pass criteria (cleanup forensics are already printed): a vehicle diff line +
  `child_pc=(target-8)|1` + `scratch=0x…500` (SAFE_OFF) + `fake_task_usage=0x101`.
