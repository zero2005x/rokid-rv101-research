# GhostLock RV101 — Work Status Handoff v6 (sixth → seventh holder)

> 中文版：[GHOSTLOCK_HANDOFF_V6.md](GHOSTLOCK_HANDOFF_V6.md)

> Updated late at night on 2026-08-18. This round (the sixth holder) took over from v9.19 and went through 7 revisions, v9.20→v9.26.
> This round **solved the three open points left by the v5 handoff mainly through static analysis** (the truth about the ±64 pad,
> the write geometry of rb_erase, and the full static picture of the W1 that never landed), and pushed the reclaim engineering
> to the point of a "stable dip on attempt 1 of every fresh boot" —
> **but payload landing (vehicle diff) and the W1 enforcing flip are still not achieved**. The four successes of v9.19
> (run 32/33/36/37) **could not be reproduced even once** in this round's 9 on-device trials; the only newly added failure mode
> is "punch 1 immediately resets" (run 39/41/45), which this round has attributed (see §3).
>
> Working directory: `C:\Users\<USER>\Documents\codebase\rokid_root`
> Source: `source_code\ghostlock_rv101\` (v9.26, binary md5 `B6DCFBFC3F9F5B415F79621D4019CEC6`, synced to the device)
> Target device: `<DEVICE_SERIAL>` (RV101 / RG_glasses; do not touch the Redmi `<PHONE_SERIAL>`)

---

## 1. This round's most important static conclusions (cite directly, do not redo)

All verified line by line from `research/ghostlock_refs/rv101_disasm/*.txt` plus the home-grown tool `scripts/rv101_disasm.py`
(python3 + capstone 5.0.7, `python scripts/rv101_disasm.py <off> <len>`, offset = offset within the Image file).
**The device kernel = `binaries/extracted_1.22/kernel` (47MB raw Image).**

### 1.1 The write geometry of rb_erase (0x774b38, fully solved)

- Linux rb_node layout: `__rb_parent_color` @ +0x00, `rb_right` @ +0x08, `rb_left` @ +0x10.
- For our forged waiter's tree_entry: `tree_pc=(target-8)|1`, `tree_right=write_value`,
  `tree_left=0` → rb_erase takes **case A** (left==NULL):
  - `0x774b9c: ldr x12,[parent+0x10]!` reads `[target+8]` and compares it with node (almost certainly not equal to the stack addr)
    → `0x774bac: str x8,[x11]` writes **write_value to `target+0` (the rb_right slot)**;
  - `0x774d84: str x9,[x8]` writes `(target-8)|1` to `write_value` (= page_base+SAFE_OFF,
    i.e. the forensic child_pc signal).
- **When W1's target=data_addr(SELINUX_STATE), the 8-byte write_value lands at selinux_state+0,
  byte0=0x00 → enforcing=0.** This chain is statically watertight.
- The v4 handoff's "which slot gets written depends on luck" is ruled out: as long as `[target+8] != node` (always true for W1), it is target+0.

### 1.2 SKB data is at block+0 (v5's ±64 "correction" is wrong)

- The diff of run 32/33/36 shows 8 bytes only at recv `0x0E08` (`60 bc .. c0 ff` = **the forged waiter's
  kernel stack address**), all 0 in the pristine run. This can only be "the empty-tree enqueue's `root->rb_node=waiter`" written at
  **slab+0x0E08** (fake_lock+8) — that is, recv[j] = slab[j], **the data starts at block+0, with no NET_SKB_PAD**.
- After v5 shifted payload and forensic by −64 in sync, the payload landed at `slab+OFF−64`, whereas the walk's address is
  `page_base+OFF = slab+OFF`: the fake lock the walk reads is all 0 (empty lock, owner=0) → this is exactly the complete
  explanation of run 32/33's "usage=0x100, leftmost at its original value, only rb_node differs".
- `GL_SKB_PAD=0` aligns the payload (the walk traverses the full owner chain); **but the aligned geometry dies**: punch 1's
  walk pollutes the forged waiter (stack slot) with RB_CLEAR_NODE/rb_link_node, and punch 2 (if nice changes again) reinserts
  it into the tree producing a **self-looping rb_node**, so punch 3's rb_erase case-C descent loop
  (0x774b50-0x774b60) has `x11=[x11+0x10]=S` always true → **infinite loop → watchdog reset**.
  → **v9.21 changed punch to a fixed nice (no more alternation)**: punches 2-5 exit at rt_mutex_adjust_pi's
  `waiter->prio == task->prio` precondition check and do not walk a second time.
- Note: in `__sched_setscheduler` the prio store (0x199b68) happens **before** the rt_mutex_adjust_pi call
  (0x199d80, gated on pi=true, with sched_setattr always true) → during the walk A->prio=139 (nice 19).

### 1.3 The full walk path (rt_mutex_adjust_prio_chain @ 0x1ceaf4) has been checked line by line

- This device uses the 5.9+ new signature: `(task, chwalk=0, orig_lock=NULL, next_lock, orig_waiter=NULL, top_task)`;
  rt_mutex_adjust_pi (0x1ce9fc) calls it with x4=NULL → all top_waiter/pi_waiters precondition checks are skipped.
- **pad 64 (current geometry)**: leftmost=0 → x27=0 → dequeue's RB_EMPTY_NODE check
  `[S]==S?` false → rb_erase (**write to target+0**) → enqueue (empty tree: root=leftmost=S) →
  owner=0 → the wake path at `1cf2a0` short-circuits because x27=0 → clean return. **No fault anywhere**.
- **pad 0 (aligned geometry)**: erase the same → enqueue inserts {w0} (prio 139>130 → **right child**,
  rb_insert black-parent path `0x774a18 tbnz → 0x774ad4 str node,[root]` is safe but changes root to S) →
  owner=ftask|1 → usage round-trips 0x100↔0x101, wait_lock round-trips 0↔1 → forged≠leftmost → pi-block
  and 0x1985f0 (rt_mutex_setprio style, touches the runqueue) are **all skipped** → clean. Statically pad 0 is safe too;
  the punch-1 death in run 39 is attributed to a side effect of the gate flood (see §3), **not a fault in the walk itself**.
- After fixing nice, each attempt produces exactly one walk, at punch 1.

### 1.4 enforcing @ selinux_state+0 is verified on device

`/proc/config.gz`: `CONFIG_SECURITY_SELINUX_DISABLE` **not set**, `CONFIG_SECURITY_SELINUX_DEVELOP=y`
→ `struct selinux_state { bool enforcing; ... }`, with enforcing right at +0; the disassembly of sel_read_enforce,
`ldarb [state+0]&1`, matches. **The target's address arithmetic is correct.**

---

## 2. This round's changes (v9.20 → v9.26)

| Version | Content |
|---|---|
| v9.20 | GL_SKB_PAD defaults to 0; strict forensic (from the walk-address viewpoint read the child_pc/scratch/w0/pi fields at rb+OFF, diff cap 64, pass only if child_pc==(target-8)\|1); GL_TARGET_DELTA env; enforce status printing |
| v9.21 | **punch fixed nice** (fixes the punch-3 rb_erase self-loop infinite loop); versioned banner |
| v9.22 | V9_PURGE_CNT 700→1800 (**548 clone failures**: snitch 4096 threads + child processes ≈ 6608 RLIMIT_NPROC limit); settle cap 60 |
| v9.23 | purge back to 700; dummy 450→300; settle soak 28 + quiet 10 (**waves need ~12-24s**) |
| v9.24 | Removed the pre-send /proc/pagetypeinfo read (it reopens a ~1-5ms steal window between dip detection and the first send); dip poll tries sysfs (/sys/kernel/slab/* gives shell **EACCES**, automatically falls back to slabinfo) |
| v9.25 | purge 700→1000; settle soak 40 + quiet 30 (**quiet must exceed the ~10-15s lazy drain wave period**, otherwise false quiet between waves) + cap 120 |
| v9.26 | purge 1000→1100 (phase-1 stacks ≈30.3MB, just past the measured ~29-31MB vmap purge threshold); dummy 300→200 (GL_DUMMY_KILLS=180 must be < the 200-224 flush threshold to avoid early firing) |

Also produced `scripts/ghostlock_reboot_run.ps1` (**reboot-per-attempt batch run**: each cycle reboot → wait for
uptime≥150s → run once with `GL_SETUP_ATTEMPTS=1` → host tee saves the log; the success string has been changed to
`carried the STRICT walk signature|SELinux DISABLED`).

## 3. This round's 9 on-device trial matrix (all pad 64 unless noted)

| run | Configuration | Result |
|---|---|---|
| 39 | pad **0** | punch 1 dies (gate flood +620 side effect) |
| 40 | dummy 0 | 8 attempts: attempt 1 dip=1, the rest dip=0; **all zero diff** |
| 41 | dummy 100 | punch 1 dies (gate flood +1041) |
| 42 | dummy 250, purge 1800 (only 1251 succeeded) | attempt 1 dip=1; zero diff |
| 43 | dummy 250, purge 700 | attempt 1 dip=1; zero diff |
| 44 | dummy 180 | attempt 1 dip=1 (flood +960); zero diff |
| 45 | dummy 180, purge 1000, **mode 1 (W1)** | cycle1 punch 1 dies (flood +1410); cycle2 five attempts all zero diff, **enforce stays '1'** |
| 46 | dummy 180, purge 1100 | 4 cycles (reboot-per-attempt): **every attempt 1 dip=1**; zero diff |

**Patterns**: (a) only attempt 1 on a fresh boot dips (for attempt 2+ the leftover makes the flush fire early, and pre+leak stay
at the tail of the queue and are never drained); (b) after the dip the gate always has a +800~+1400 o2 flood (the vmap purge crosses
the threshold only at the gate), the leak slab gets buried and the first send cannot obtain it → zero diff; (c) punch-1 death = after the
flood the leak slab is reallocated (mostly becoming a kernel stack), and the walk's four writes land in a live stack → the victim crashes → reset.
**The zero diffs from run 40 onward are a payload that did not land, not a walk that did not run** (v4's GL_POISON trylock panic and the
deaths in run 39/41/45 all prove the walk executes).

## 4. Left for the next holder (in priority order)

1. **First: prove/disprove that punch 1's erase really executes**. Do `GL_POISON2=1` (route.c is already prepared:
   tree_pc=0xdead000000000001 → erase's `[parent+0x10]` reads 0xdead…010 → fault):
   punch 1 panics ⟺ erase executed; survives ⟺ the walk did not reach erase (re-examine punch's preconditions).
   This is the decisive binary split for the mystery of the W1 that never flipped.
2. **The gate flood's residue**: settle's quiet o2+objs is blind to the silent accumulation on the vmap list — in run 46, after
   settle quiet=35 (stable for 17.5s), phase-3's ~3MB still pushed the near-threshold list over (+825). Directions:
   (a) append a "forced threshold crossing squad" at the end of settle (kill ~300 more so it must cross the threshold → purge lands inside settle → second quiet);
   (b) or raise purge to 1300 (stacks 33.5MB, note the 6608 budget: snitch 4096 + child processes;
   run 42's 1800→548 failure was RLIMIT_NPROC).
3. **After the payload lands (diff appears)**: selftest strict signature (child_pc@recv[SAFE_OFF]==(target-8)|1,
   mode3 additionally verifies scratch==page_base+SAFE_OFF) → immediately switch to W1 (`--write1`, same set of env vars,
   `W1_ATTEMPTS=1` + reboot-per-attempt; criterion `SELinux DISABLED`). If strict passes but enforce is still 1,
   sweep {−8,0,+8,−64,+64} with `GL_TARGET_DELTA` (§1.1 says 0 is right, but this is a cheap final check).
4. Once W1 is stable → W2 (mode 2 is ready, write_value=data_addr(INIT_CRED)).
5. **Do not go back to pad 0** (the aligned geometry has punch-3 infinite-loop risk unless punch is also changed to single-shot);
   and do not restore nice alternation (same reason).

## 5. Artifacts / files

| Location | Content |
|---|---|
| `source_code/ghostlock_rv101/` | v9.26 source (md5 `B6DCFBFC3F9F5B415F79621D4019CEC6`, synced to the device) |
| `scripts/rv101_disasm.py` | reusable kernel disassembly tool (capstone) |
| `scripts/ghostlock_reboot_run.ps1` | reboot-per-attempt batch run (the $Args automatic-variable pitfall already fixed) |
| `reports/ghostlock_runlogs/exploit_run{39..46}.log` | this round's 9 device logs (run 45 includes the mode-1 W1 sample) |

Build (as before): `$env:ANDROID_NDK_HOME='C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\android-ndk-r27d';
cd source_code\ghostlock_rv101; mingw32-make -B`; after pushing, always compare md5.

How to run (current):
```
adb -s <DEVICE_SERIAL> shell "GL_SKB_PAD=64 GL_DEBUG_DRAIN=1 GL_DUMMY_KILLS=180 /data/local/tmp/ghostlock_rv101 --selftest"
```
reboot-per-attempt: `& scripts/ghostlock_reboot_run.ps1 -Cycles 4 -Env "GL_SKB_PAD=64 GL_DEBUG_DRAIN=1 GL_DUMMY_KILLS=180" -ExploitArgs "--selftest" -Log runNN`

Operating discipline as before: a panic reboots automatically in 2-5 minutes, KASLR re-leaks automatically, dmesg/pstore are useless,
**each exploit process runs only attempt 1 (attempt 2+ in the same boot will not dip)**, the device has shown a
spontaneous ~5-6 minute reboot cycle (suspected to be accumulated pinned mm from previous runs + low memory); check uptime before running.
