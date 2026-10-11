# GhostLock RV101 — v3 "fake page panic" root cause analysis (fourth agent)

> 中文版：[GHOSTLOCK_V3_PANIC_ROOTCAUSE_ANALYSIS.md](GHOSTLOCK_V3_PANIC_ROOTCAUSE_ANALYSIS.md)

## Conclusion (one sentence)

v3's handoff claim that "the walk kernel panics on the fake page" is actually a **garbage-page walk caused by reclaim failure**:
the payload is simply not on the page the walk reads and writes (the v9 reclaim gate MISSes most of the time). A real
payload-walk has not yet been observed; v3's bisection conclusions (BLACK-root, NOOWNER, the lock/erase sections)
are all based on garbage-page panics and can only serve as evidence that "the walk dies on a garbage page".

## On-device evidence chain (this round's runs)

| Run | Contents | Result |
|---|---|---|
| 1 | v9 original binary | requeue EDEADLK ✓ (errno 35 = **EDEADLK**, recorded in v1 handoff §67); punch wedges the kernel (punch_done=0) → hard reset after ~20s (soft-lockup type) |
| 2 | v9 | punch succeeds, zero writes, no panic → the walk never touched our page |
| 3 | multi-punch (5 shots, inside the window) | all 5 shots succeed, zero writes → same as above |
| 4 | GL_POISON=1 | kernelsnitch false positive (ffff0000...) → failed early |
| 5 | GL_POISON=1 | **panic at punch 1** → the walk really does reach the forged waiter's trylock (PWS_LOCK=0xdead fault) |
| 6 | GL_DEBUG_DRAIN=1 (fresh boot) | o2 2096→2093 (**gate MISS**) → then panic (garbage-page walk = the v3 signature) |
| 7 | v9.1 (close reordering + gate check + retry) | 4 attempts: o2 -1 / +312 (RCU noise) / -1 / **+1 but zero writes** → the o2 check is unreliable |
| 8 | v9.2 (settle + first-send head-grab + forensic retry) | in progress |

## Key mechanism findings

1. **errno 35 = EDEADLK (not EAGAIN)**: the trigger succeeds every time (already recorded in v1, reconfirmed this round).
   This kernel's futex_requeue EDEADLK fixup (0x235180) does `str xzr,[rt_waiter+0x38]`
   (waiter->lock=NULL) and then directly returns -EDEADLK.
2. **dmesg cannot capture the panic**: on this device a panic is a hard reset and the ring buffer has no oops output
   (dmesg_run3.log line 565551 ends cleanly). v3's "parallel dmesg -w" plan is useless.
   The PC can only come from controlled-fault bisection + vehicle forensics.
3. **The lock helpers are vanilla qspinlock** (0x9409df0/0x940a2ac/0x940a208/0x940a0c8 have been disassembled):
   no DEBUG_SPINLOCK magic checks, no lockdep → v3's number one suspect is ruled out.
4. **Punch timing race**: the original 5ms delay often lands before A enters select (the walk uses a stale rt_waiter,
   because waiter->prio==task->prio returns benignly at 0x1cebbc, or with lock=NULL it passes the
   NULL==NULL check at 0x1ceb7c-0x1ceb84). Changed to multi-punch (+20/55/90/125/160ms,
   alternating nice 18/19 so that every punch triggers a PI walk).
5. **The two races in v9's reclaim** (root cause):
   - **node-partial pop race**: v9's ordering puts the leak slab at the **head** of node partial
     on the earliest drain, and any concurrent mm allocation pops it away → the gate never triggers.
     → v9.1: reorder the close sequence (post[0] → flood F0-F11 → **pre[0] triggers the drain itself**
     (pobjects 13→14)), the leak slab is at the chain head → after add_partial it sinks to the bottom of the list = pop-proof;
     the late-stage flood leaves 1 object per slab (16/17) → a deep nr_partial buffer of ~33.
   - **buddy LIFO race**: after leak-close and before the first vehicle send, any system-forked
     kernel stack (order-2) steals the leak slab at the buddy head; the o2 debug read also stretches the window
     to the ms scale. Run 7-attempt-4's "+1 but zero writes" is most likely this.
     → v9.2: settle 150ms after kill (flushing the RCU stack return flow) → close sequence →
     **send vehicle[0] immediately after leak-close** (a µs window) → the remaining 767.
6. **The o2 check is unreliable**: RCU return flow from flooded child-process kernel stacks (±hundreds of blocks) pollutes
   the o2_before/o2_after difference. **The only reliable criterion = vehicle forensics** (a diff ⟺
   payload in place + the walk wrote to our page). do_one_write has been changed to forensics-driven retry
   (KERNEL_PAGE_SETUP_ATTEMPTS=12).
7. **The walk's static path (with the payload in place) should run through completely**: the permanent writes of the NOOWNER=0 path =
   leftmost(0xE10)=forged, w0->left(0x1010)=forged, usage(0x1240)=0x101,
   scratch(0x1300)=write_value, child_pc(0x1500)=(target-8)|1; wait_lock goes 1 then 0
   (net zero). The tail of the NOOWNER=1 path calls wake_up_process(init_task) (0x9228cf8
   veneer → 0x193854, w1=3=STATE_NORMAL), which is harmless (not put_task_struct).

## Residual risk

- A garbage-page walk can still panic (hard reset, each run burns up to 12 attempts × 1 boot).
  v9.2 should make the gate nearly deterministic, but there is still a chance during the boot peak.
- The survival of pi_blocked_on is a race: run 5's poison proved it survives; it is currently assumed stable (v1's multiple
  successful poisons back this up).

## Files

- Changes: src/util.c (v9.1 close reordering + v9.2 settle/first-send + debug_pagetype_o2 +
  cleanup returning the diff count), src/main.c (do_one_write forensic retry),
  src/route.c (multi-punch + markers), src/common.h, target_rv101.h (delay 5→20ms).
- New disassembly (RideFlux/ghostlock_work/): spinlock_helpers.txt, fn_193854.txt,
  qspinlock_slowpath.txt, rt_mutex_adjust_pi.txt, rb_helper_77512c.txt,
  caller_9228cf8.txt.
- Logs (RideFlux/ghostlock_work/): exploit_run{2..8}_*.log, dmesg_run{2,3}.log.
