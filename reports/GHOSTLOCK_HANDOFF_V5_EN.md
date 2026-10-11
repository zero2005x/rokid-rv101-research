# GhostLock RV101 — Work Status Handoff v5 (fifth → sixth agent)

> 中文版：[GHOSTLOCK_HANDOFF_V5.md](GHOSTLOCK_HANDOFF_V5.md)

> 📌 **Basis of fact**: wherever this document touches facts such as device generation, kernel config, CVE status, GhostLock offsets, or product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (single authoritative fact table; in case of conflict, that table decides).


> Updated in the early morning of 2026-08-18. This round (the fifth agent) took over from v9.3 and went through 17 versions,
> v9.3→v9.19, solving the entire reclaim-mechanism puzzle, **and in run 32/33 observed payload-walk writes for the first time
> (selftest passed)**. The "v9 reclaim design" handed off by v4 rested on three wrong premises (see §2), all of which were overturned by
> this round's on-device measurements.
>
> Working directory: `C:\Users\<USER>\Documents\codebase\rokid_root`
> Source: `source_code\ghostlock_rv101\` (binary md5 in §4)
> Target device: `<DEVICE_SERIAL>` (RV101 / RG_glasses; do not touch the Redmi `<PHONE_SERIAL>`)

---

## 1. Milestone (the only hard indicator this round)

**A diff has appeared in the vehicle forensics — payload in place + walk writes observed for the first time.**

- run 32: `SELFTEST: write into sprayed page verified: 1 vehicle(s) carried kernel
  writes (attempt 1)`, diffs in vehicle[5] (offset 0x0E08+, leftmost=w0, owner=ftask|1),
  gate window −1 (clean).
- run 33: same, attempt 1 passed (vehicle[4]). **Reproducible (2/2, both attempt 1).**
- Criterion environment variables: `GL_DUMMY_KILLS=0` (leftovers-trigger, see §3.3) plus the rest as below.

Not done: W1/W2 (run 34's W1 failed all 24 attempts after a long uptime — see the residual issues in §5).

---

## 2. v4 premises overturned this round (do not step on them again)

1. **The memfd pin leaks** (proven by probe3): on this device, close(`/proc/pid/mem`) **does not release the mm**.
   After 28 seconds + 4096 file callbacks the mm is still 561/561. v9's "pin + ordered close = reclaim sequence"
   chain is dead end to end.
2. **The mm_struct free is lazy-RCU** (proven by probe2): even an exit-time mm free without a pin requires
   ~10s of batch flushing (threshold ~100-224 lazy callbacks, executed FIFO). v9's "close releases synchronously"
   does not exist.
3. **The SKB payload has a ±NET_SKB_PAD misalignment**: send()'s data starts at slab+64 (skb_reserve's
   NET_SKB_PAD), whereas v4's `SKB_DATA_DELTA=0` assumed the data was at block+0, so the whole payload was off by 64
   bytes. The reference implementations (oneplus/aresin/emerald/m3q) all use `SKB_DATA_DELTA (-0xe80)`. Fixed
   (`GL_SKB_PAD`, default 64; payload and forensic extraction shifted by −64 together).

## 2b. The complete mechanism picture solved this round (on-device measurements)

| Mechanism | Evidence | Countermeasure |
|---|---|---|
| memfd pin leaks | probe2-B / probe3: pinned mm not released for 30s+ | Abandon pin-based reclaim (keep a small number of pins as "slab never empties" guards) |
| mm free is lazy-RCU, batch flushed | probe2-A: exit-time mm free only released after ~10.7s | Killing child processes = reclaim sequence; flush triggered by threshold |
| Flush threshold ~200-300 callbacks, each drain ~150-200 | probe5 / run19/22 | builders 288 + rest 448 to create volume; pre+leak placed inside the drain window |
| Task callbacks of killed child processes wake the drain | run19: 766 kills → ~480 released; 145 kills → 0 | Source of volume |
| Kernel stacks are freed via vmap-lazy, big purge at ~32MB threshold | run26: only +1456 at phase-3 (threshold pushed past by dummies) | purge squad 700 pushes past the threshold in phase-1, purge completes within settle |
| The ~20-47 block small burst of the purge wakes together with the drain | gate-window +16/+20/+47 in run23/27/28 | Put pre+leak at the tail of the queue + no dummy trigger (GL_DUMMY_KILLS=0) |
| Panic mode of the garbage-page walk | multiple times in runs 12-27 | Disappears naturally after success |

---

## 3. Current v9.19 design (prepare_good_kernel_page)

1. **Clone**: flood 765 (early 204 + late 561) + pre 16 + leak child (kernelsnitch then
   **pause alive**) + post 17 + dummy 450 + purge 700. About ~1950 threads total (< ulimit 6608).
2. **Guard pins**: 1 pin per slab for the early flood (12), 1 pin for post, 1 pin per slab for dummy/purge
   — the pin leak conveniently guarantees these slabs never become empty (no discard, no burying of the leak slab).
3. **phase-1 kills**: builders (288 siblings of the 18 slabs before the late flood) → early flood
   (192) → remaining late flood (240) → post (16) → purge squad (659). All frees enter the lazy
   queue; the builders' slabs become 1-inuse and go onto node partial (the source of nr_partial ≥ 5);
   the purge squad's stacks push the vmap lazy list past ~32MB and the purge completes within settle.
4. **settle**: a fixed ≥6s soak + 6 consecutive rounds (3s) of double quiet on both o2 and mm-objs (cap 20s).
5. **phase-3**: dummy trigger (`GL_DUMMY_KILLS`, 0=pure leftovers) → **pre+leak killed last**
   (the leak slab's 17 frees land at the tail of the queue, inside the last drain window) → an
   active_slabs **−1 dip** in slabinfo (the only signal of leak slab discard) → **send immediately at the dip**.
6. send vehicle[0] grabs the buddy LIFO head (the leak slab) → the remaining 767 → route/punch (unchanged).

## 3b. Key insights (for the next agent to cite directly)

- **dip criterion**: when early flood/post slabs become empty, nr_partial < 5 → park (no discard,
  no drop in the count); only the leak slab (last in FIFO + nr_partial ~33) is discarded → a **−1** in active_slabs
  is the leak slab's exclusive signal.
- **leftovers-trigger**: the settle drain stops at queue < threshold (~0-250 left); in phase-3 the
  pre+leak 17 + leftovers ≥ threshold makes the flush ignite itself (no new stacks, no purge torrent) —
  the successful path of run 32/33. With `GL_DUMMY_KILLS=0` the success rate is ~30-50%/attempt (luck at the
  leftovers-size boundary).
- **±64 pad**: `GL_SKB_PAD` (default 64) now applies to both payload writes and forensic extraction.
  The observed position of the vehicle diffs is 0x0E08+ in the recv buffer (≈ slab 0xE08+64, awaiting final calibration
  once aligned with the walk writes).

---

## 4. Artifacts / files

| Location | Contents |
|---|---|
| `source_code/ghostlock_rv101/` | v9.19 source (binary md5 `3CCAAAB61135B81844DEBA7CFC432AD4`; already synced to the device) |
| `reports/ghostlock_runlogs/exploit_run{13..35}*.log` | 23 device logs from this round (run 32/33 are the success samples) |
| `source_code/ghostlock_rv101/src/mmfree_probe{1,2,3,5}.c` | On-device measurement probes (pin leak / lazy free / trigger) |
| `reports/GHOSTLOCK_V5_RECLAIM_MECHANISM.md` | The complete mechanism picture (detailed version of §2b of this document) |

Build (as before):
```
$env:ANDROID_NDK_HOME='C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\android-ndk-r27d';
cd source_code\ghostlock_rv101; mingw32-make -B
```
After pushing, always compare Get-FileHash against the device md5sum (the PowerShell pipeline swallows failure codes).

How to run (success parameters):
```
adb -s <DEVICE_SERIAL> shell "GL_DEBUG_DRAIN=1 GL_SETUP_ATTEMPTS=8 GL_DUMMY_KILLS=0 /data/local/tmp/ghostlock_rv101 --selftest"
```

---

## 5. Residual issues (the next agent's to-do list, in priority order)

1. **W1/W2 not reached**: run 34's W1 failed all 24 attempts after a long uptime (~30 min, mm cache accumulated to 192 slabs);
   the run 32/33 successes were all in the **first few attempts after a fresh boot**. Suspects:
   (a) each attempt leaks ~80 pinned mm (~5 slabs) and the accumulation interferes with settle/drain;
   (b) luck at the leftovers-size boundary (the dip is sometimes there, sometimes not);
   (c) whether W1's mode-1 walk differs from mode-3 (the payload fix ±64 has not been verified in W1).
   → Candidate countermeasures: `adb reboot` before each W1 attempt (fresh machine state); or release the pinned guard
   slabs during cleanup (right now the pin leak is permanent).
2. **Engineering the success rate**: make the leftovers size observable (rate of change of objs at the end of settle) and top it
   up to ≥ threshold before phase-3 (a small dummy trigger of 50-150, trading off the small purge burst).
3. **Final ±64 calibration**: the 0x0E08 position of the vehicle diffs suggests the pad needs re-verification (±0/±64/±128
   scan, `GL_SKB_PAD` adjustable); the forensic 5-field extraction (child_pc/scratch/usage) is still not fully
   aligned (run 32 shows child_pc=0, usage=0x100, versus the expected (target-8)|1 and 0x101).
4. After selftest passes: W1 (`--write1` → getenforce=Permissive) → W2 (cred=init_cred).
   Operating discipline as before (each panic auto-reboots in 2-5 minutes, KASLR re-leaks automatically, dmesg is useless).

---

## 6. Environment notes (supplementing v4)

- Each attempt now takes ~12-20s (settle 6-20s); GL_SETUP_ATTEMPTS is adjustable (1-12).
- The device hard-resets on every panic; it often reboots from OOM after a run (pinned mm accumulation), so both before and after a
  planned reboot, verify uptime ≥ 5 min before running.
- `GL_DUMMY_KILLS=0` is the only trigger configuration observed to succeed; the configuration that kills all 450 dummies (the default)
  pushes the vmap threshold past in phase-3, and the gate window necessarily shows a +16~+47 purge burst (proven by run 28/29).
