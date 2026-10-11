# GhostLock RV101 — Complete reclaim mechanism picture (fifth agent, 2026-08-18)

> 中文版：[GHOSTLOCK_V5_RECLAIM_MECHANISM.md](GHOSTLOCK_V5_RECLAIM_MECHANISM.md)

> 📌 **Basis of fact**: wherever this document touches facts such as device generation, kernel config, CVE status, GhostLock offsets, or product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (single authoritative fact table; in case of conflict, that table decides).


> This document is the detailed mechanism version of `GHOSTLOCK_HANDOFF_V5.md`. All conclusions are backed by on-device measurements
> (reports/ghostlock_runlogs/exploit_run{13..35}.log + mmfree_probe{1,2,3,5}).

## 1. mm_struct lifecycle on this device (QCOM 5.10.209-perf + Google lazy patch)

kallsyms evidence: `rcu_cblist_flush_enqueue`, `lazy_list`, `lazy_vunmap_enable`,
`__purge_vmap_area_lazy`, `rcu_nocb_gp_kthread`/`rcu_nocb_cb_kthread`,
`do_nocb_deferred_wakeup_timer`; `/proc/config.gz`: `CONFIG_RCU_NOCB_CPU=y`,
`CONFIG_VMAP_STACK=y`.

1. **mm_struct free goes through lazy call_rcu**: free_mm's kmem_cache_free is deferred to a batch flush of the lazy list
   (FIFO). Flush triggers: the lazy list length crosses ~100-224 (probe2-A: 128
   pending does not trigger, 224 triggers; probe5-P1: 200 does not trigger for 12s). Each flush drains
   ~150-200 callbacks (run19: 766 kills → ~480 released; run22: objs −449).
2. **The memfd pin (/proc/pid/mem) leaks**: close(memfd) does not release the mm. probe2-B / probe3:
   pinned mm all survive after 30s+ and a 4096-file burst. → It cannot be used as a reclaim trigger;
   instead, use it in reverse: pin one child so its slab never becomes empty (a guard).
3. **Kernel stacks go through vmap-lazy**: free_task → vfree_atomic → lazy vmap list. When the list exceeds
   ~32MB (lazy_max_pages) it is purged in one go (run26: phase-3 pushed past the threshold → a single wave of +1456
   order-2 blocks); there is also a small burst of ~20-47 blocks waking together with the drain (run23/27/28's
   gate-window +16/+20/+47).
4. **task_struct goes through ordinary RCU** (~0.3-0.5s grace), and its slab is order-3 (8 pages),
   invisible to o2 (order-2) — o2's waves come only from stacks and mm slabs.

## 2. The current exploit's reclaim sequence (v9.19)

"Killing child processes = the reclaim sequence": children's do_exit enqueues free_mm's lazy callbacks into the FIFO in kill order;
the drain executes in FIFO order = the equivalent of old v9.1's close-order design under lazy semantics.

- builders 288 (the 18 1-inuse slabs of nr_partial) → early flood 192 → remaining late flood
  240 → post 16 → purge squad 659 (pushes the vmap list past ~32MB) → settle (6-20s,
  o2+objs both quiet) → dummy trigger (0-450) → pre+leak killed last (tail of the queue) → a
  **−1 dip** in slabinfo active_slabs (the only signal of leak slab discard) → send at the instant of the dip.

## 3. Decision table (on-device measurements)

| Observation | Meaning |
|---|---|
| slabinfo active_slabs −1 (dip=1) | the leak slab was discarded by the slab_empty gate (into buddy) |
| dip=0 + a large drop in objs | the drain completed but the leak slab was not empty (nr_partial<5 → park) or the trigger was insufficient |
| gate-window o2 ±1 | clean (send takes the head) |
| gate-window o2 +16~+47 | the vmap purge small burst buried the leak slab |
| phase-3 o2 +1000~+1500 | purge big wave (dummies pushed the vmap threshold past) |
| pselect ret=0 | the walk returned benignly early (page contents are not the payload) |
| vehicle diffs (0x0E08+) | payload in place + walk writes (success! run 32/33) **(corrected 2026-09-13: V6 §1.2 proves the payload is off by 64 B and did not actually land; this only means the walk ran)** |

## 4. Success configuration

```
GL_DEBUG_DRAIN=1 GL_SETUP_ATTEMPTS=8 GL_DUMMY_KILLS=0 --selftest
```
run 32/33 both passed on attempt 1. Success path: when settle's drain stops at queue < threshold it leaves
~150-250 leftovers; in phase-3 the 17 pre+leak frees + leftovers ignite the flush themselves;
no new dummy stacks → no purge torrent; the leak slab's discard is the only −1 dip → send takes the head cleanly.

## 5. Known residuals (next steps)

- Success rate ~30-50%/attempt (luck at the leftovers-size boundary) + pinned mm accumulation after a long uptime
  (~5 slab/attempt) degrades later attempts → reboot before each attempt, or release the guards in cleanup.
- The ±64 pad's vehicle-diff position (0x0E08) awaits final calibration (GL_SKB_PAD scan).
- W1/W2 not reached (run 34/35 all failed): after the payload alignment fix it has not yet been verified as succeeding under mode-1.
