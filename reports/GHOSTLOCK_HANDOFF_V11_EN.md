# GhostLock RV101 — Work Status Handoff v11 (Eleventh Custodian: mm-leak flake root cause + blind-write assault)

> 中文版：[GHOSTLOCK_HANDOFF_V11.md](GHOSTLOCK_HANDOFF_V11.md)

> 📌 **Fact basis**: Whenever this document touches on facts such as device generation, kernel config, CVE status, GhostLock offsets, or product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (the single authoritative fact table; in case of conflict, that table decides).


> Updated 2026-08-22. This round (the eleventh custodian) completed/found on the real device (<DEVICE_SERIAL>):
> 1. **Settled init_cred->uid = 0** (dumped directly from the kernel Image, not inferred).
> 2. **Found that the blind cred write (v9.50 main path) fails: child uid = -128 = 0xFFFFFF80**,
>    consistent across all 10 rounds of run_r21.
> 3. **Located the root cause of the mm-leak (kernelsnitch) flake = EAGAIN (errno=11)**: fork/pthread_create
>    runs out of resources at ~2000 tasks, far below RLIMIT_NPROC=6608, and **it is unrelated to reclaim**.
> 4. Added two diagnostic code paths (init_cred.usage before/after + clone/pthread errno capture).

---

## 0. Most Critical Conclusions (Read First)

> **[Major breakthrough in the second half of the eleventh round — the -128 root cause is settled, and the blind write actually did land!]**
> The parallel agent's run_r23_v952 (11:23, successfully LANDED + obtained the complete DIAG) proves:
> - `init_cred.usage before=0x4 → after_cred=0xffffff8039c262b9 after_real=0xffffff8039c262b1`
>   (= `(task+0x778)|1` / `(task+0x770)|1`) → **both mode-2 write#1 and write#2 definitely landed**,
>   and `task->cred = init_cred` really was written. `prev==prev2 stable=1` (no tail race).
> - **The real root cause of -128**: `struct cred.usage` in this kernel is a **4-byte `atomic_t`** (not
>   mainline's 8-byte `atomic_long_t`), so **uid is at offset +0x04** (not +0x08).
>   mode-2's write#2 = `str x9,[x10]` stores the 8-byte `tree_pc` at `init_cred+0`, and therefore
>   **clobbers uid**: `init_cred+4..7 = tree_pc>>32 = 0xffffff80 = -128`.
>   In other words, **child uid=-128 is not "the write did not land" nor "the wrong task"; it is write#2 blowing up
>   init_cred->uid into -128**.
>
> **[Eleventh round v9.56 — the correct repair: write_value=0 skips write#2 entirely]**
> Disassembly settles it: rb_erase@0x774bb0 is `cbnz x8, 0x774d84` (the gate for write#2). **As long as
> write_value (=x8) = 0, write#2 is skipped**, while write#1 still does `str x8,[x11]` to write the 8-byte 0
> into target. So the repair should use `set_pselect_write_mode(init_cred+0x04, 0, 4)`:
> - write#1 = `init_cred+4 = 0` → **uid(+4)=0 and gid(+8)=0**.
> - write#2 **is skipped** (x8=0, cbnz does not jump) → **completely avoids the phys 0x80000000 fault**.
>
> This replaces the two dead ends of the parallel agent's v9.52 ("write_value=0xffffff8000000000 (writes phys 0x80000000, which faults)")
> and v952b ("0xffffff8010000000 (uid becomes 0x10000000, not 0)"). **v9.56 has not yet been tested on device**
> (run_r29 is stuck at mm-leak EAGAIN; see below).

---

## 0. Most Critical Conclusions (Read First)

### 0.1 init_cred->uid Settled = 0 (Rules Out the "init_cred Is Not Root" Hypothesis)

Dumping init_cred directly from `binaries/extracted_1.22/kernel` (link=0xffffffc00a202058,
file offset=0x02202058):

```
init_cred bytes @ 0x2202058:
  +0x00: 0x0000000000000004   (usage=4)
  +0x08: 0x0000000000000000   (uid=0, gid=0)
  +0x10: 0x0000000000000000   (suid=0, sgid=0)
  +0x18: 0x0000000000000000   (euid=0, egid=0)
```

So **if mode-2 write#1 really lands (task->cred = init_cred), the child's getuid() must return 0**.
child uid = -128 means **write#1 did not land (or the task pointer is wrong)**, not a problem with init_cred.

### 0.2 blind cred write (v9.50) Fails Consistently: child uid = -128 = 0xFFFFFF80

run_r21 (v9.50, 10 rounds) had `child uid = 4294967168` = 0xFFFFFF80 = -128 (signed) in every round.
This is a **systematic** failure, not a race (a race should be random). init_cred->uid=0 already rules out "init_cred is wrong".
Two possibilities remain:
1. **reclaim failed → the fake page contents are not in the leak slab → walk reads garbage → rb_erase is never reached**
   → write#1 does not land. (Most mode-2 writes in run_r21 were `dip=0 calm=364` = the leak slab was not discarded;
   even when round1 had `dip=1`, `cleanup: 0 diffs` means the SKB did not take the leak slab.)
2. the task pointer is wrong (`prev - TASK_TASKS_OFF` points to a non-child task).

**There is not yet DIAG data to distinguish these** (because the mm-leak flake kept me from reaching DIAG). The DIAG is already written (see §2);
a single boot where mm-leak succeeds is enough to produce it.

### 0.3 mm-leak (kernelsnitch) flake root cause = EAGAIN (errno=11), Not reclaim

After errno capture was added, the evidence from a failing boot:

```
v9.13 diag: dummy clones ok=125 fail=325 (first errno=11 Try again)
kernelsnitch: ks_increase 4096 threads, 3040 failed (first err=11)
Threads: 1057   ← only 1056 threads (not 4097)
```

`errno=11` = EAGAIN. For clone/pthread_create that means RLIMIT_NPROC or a thread/pid resource limit.
Measured: the total task count at the failure point ≈ 765(flood)+16+1+17+125(dummy)+1056(threads) ≈ 1980,
**far below RLIMIT_NPROC=6608, threads-max=13217, and pid_max=32768**, and there is no cgroup pids controller
(`/sys/fs/cgroup/cgroup.controllers` is empty, no `pids.max` anywhere). So which limit is being hit still needs
kernel-side localization (`Committed_AS` 28.5GB >> `CommitLimit` 2.48GB over-commit is another suspect, but
that usually returns ENOMEM=12, not EAGAIN=11).

**Practical observation (5 boots)**: mm-leak succeeded 3 times (run_r21@396s, run_r22@397s, selftest2@454s,
`Threads:4097`) and failed 2 times (selftest1@522s, run_r24@405s/run_r25@548s, `Threads:1043~1057`).
Successful boots were almost all at **~396-397s uptime**; from 405s on it usually fails. **It is recommended to run at ~380-400s uptime every time,
and to reboot immediately and start over as soon as mm-leak fails, rather than retrying 6 times in the same boot.**

**v9.55 fork-limit diagnostics (settling the flake variable)**: at the first clone failure it dumps
`RLIMIT_NPROC=6608/6608`, `loadavg`, `threads-max=13217`, `pid_max=32768`. Key evidence
(run_r28 @770s, run_r29 @407s):
```
loadavg=... 39/7839 ...   ← nr_threads = 7839 (total system thread count)
```
In other words, **the real variable behind mm-leak EAGAIN is the total system thread count nr_threads**, not uptime:
- Good boots (run_r21/r22/r23_v952): nr_threads is low (~1241), so mm-leak's ~5300 tasks fit.
- Bad boots (run_r28/r29): nr_threads is high (**~5850 baseline, and 7841 already at 407s**),
  and fork gets EAGAIN at ~1950 tasks. Bad boots also have **24~91 runnable** in loadavg (the camera
  service is stuck in a CAM-ISP error loop); this is a device-state problem, not an exploit bug.
- Practical countermeasure: **run `cat /proc/loadavg` first; only nr_threads < ~3000 is a good boot**; if >5000,
  reboot and start over. Alternatively consider lowering APPENDED_FUTEXES from 4096 → 1024 so mm-leak fits even on a bad boot (untried;
  it would weaken the collision signal).

---

## 1. Code Changes This Round (`source_code/ghostlock_rv101/`)

- `src/main.c`:
  - **v9.51 DIAG (round loop)**: reads `init_task.tasks.prev` twice (to detect the tail race),
    and reads `init_cred.usage` before and after every mode-2 write (`kernel_read64(fd, data_addr(INIT_CRED))`):
    - `ic0` (before the write), `ic1` (after writing cred), `ic2` (after writing real_cred).
    - write#2 stores `tree_pc=(target-8)|1` into `init_cred.usage`, so `ic1 != ic0`
      **proves the rb_erase walk really landed** (write#1 and write#2 are in the same rb_erase call, so they live and die together).
    - child uid is now printed as `%u (0x%08x)` so signed/unsigned can be seen.
  - (added by the previous custodian) `v9.50 DIAG`: after FOPS installed, read init_cred@0x00/@0x08/@0x1c.
- `src/util.c`:
  - the `clone_child` caller (the dummy block) captures the errno of the first clone failure.
  - `ks_increase` captures the pthread_create failure count and the first errno.
  - **v9.54**: reclaim gate_poll changed from `usleep(3000)` to `usleep(300)` (iteration 60→600,
    300→3000, keeping the ~180ms/~900ms coverage), shrinking the SKB head-grab window after the leak slab is discarded from ~3ms
    to ~300us. **Not yet verified** (run_r27 hit EAGAIN already at mm-leak and never entered reclaim).

### write#2 Risk in repair (the panic point of the parallel agent's run_r23_v952)

The write_value of repair (mode-4 writing `init_cred+0x04`) must have "low 32 bits = 0" in order to write uid as 0.
The **only** address in the kernel whose low 32 bits are 0 and which lies within RAM is the direct-map base `0xffffff8000000000`
(= phys 0x80000000, zoneinfo start_pfn=524288). So repair's write#2 writes into **phys
0x80000000 (the first page of RAM)**, and run_r23_v952 panicked right at the repair punch (most likely a reserved-page
fault, though a resource spike / concurrent fake_fops has not been ruled out by bisection). The parallel agent's v952b swapped the value for
`0xffffff8010000000` (phys 0x90000000) to try to avoid the first page, but that would write uid as 0x10000000 (not 0).

**Rebuild/push** (using WSL's Windows NDK under Linux; bit-identical already verified):
```bash
cd /mnt/c/Users/<USER>/Documents/codebase/rokid_root/source_code/ghostlock_rv101
NDKBIN="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/ndk/android-ndk-r27d/toolchains/llvm/prebuilt/windows-x86_64/bin"
"$NDKBIN/clang.exe" --target=aarch64-linux-android31 -O2 -Wall -Wextra \
  -Wno-unused-parameter -Wno-sign-compare -fno-stack-protector -fPIE -Isrc \
  -include target_rv101.h -o build/ghostlock_rv101 src/main.c src/util.c src/route.c \
  -pie -static-libgcc -pthread
ADB="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe"
"$ADB" -s <DEVICE_SERIAL> push build/ghostlock_rv101 /data/local/tmp/ghostlock_rv101
"$ADB" -s <DEVICE_SERIAL> shell "chmod 755 /data/local/tmp/ghostlock_rv101"
```

---

## 2. Next Round's To-Do List (in Priority Order)

1. **Obtain the DIAG data (the only blocker for the blind-write root cause)**: on a boot where mm-leak succeeds (~396s uptime) run
   ```
   GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 W2_ATTEMPTS=1 \
     /data/local/tmp/ghostlock_rv101
   ```
   and look at the two key output lines:
   - `DIAG init_cred=... usage=... @0x08=...` (should be @0x08=0, confirming init_cred->uid=0).
   - `init_cred.usage before=... after_cred=... after_real=...`:
     - **if `after_cred != before` (becoming the `(task+0x778)|1` form) → both write#1/write#2 landed**,
       so child uid != 0 means **the task pointer is wrong** (race/TASK_TASKS_OFF wrong); then attack task localization.
     - **if `after_cred == before` (both small refcounts) → the walk did not reach rb_erase** (reclaim failed
       or the mode-2 walk systematically does not reach rb_erase); then attack reclaim/mode-2.
2. **Reduce mm-leak's resource requirements / find an alternative** (see the three directions in §3).
3. **reclaim stability** (originally Direction 1) is still needed: `GL_PURGE_CNT=0 GL_TAIL_KILLS=16` is right,
   but the mm-leak hurdle must be passed first.

---

## 3. Three Viable Directions for mm-leak EAGAIN

1. **Hit the uptime window precisely**: measured mm-leak only succeeds at ~396-397s, and from 405s on it gets EAGAIN. It may be that some
   background service (camera/gfx) starts at ~400s after boot and eats the thread/memory budget. Approach: poll uptime
   immediately after reboot and start at ~380s (run_r21 succeeded at 396s and all 10 rounds were OK).
2. **Reduce mm-leak's thread count**: `APPENDED_FUTEXES=4096` (=1024 buckets×4). Lowering it to 2048
   or 1024 would weaken the collision timing signal and reduce mm-leak reliability, but it can be tried. Or lower flood(765)/dummy(450)
   (they are for reclaim; mm-leak itself does not need them), giving the 4096 threads more resources — but that touches the
   reclaim parameters, at your own risk.
3. **Localize which limit it actually is**: at the failure point dump `getrlimit(RLIMIT_NPROC)`, `/proc/loadavg`
   (nr_threads), `/proc/sys/kernel/threads-max`, and the pid-namespace situation from
   `cat /proc/self/status | grep Threads`. Static analysis has so far ruled everything out (6608/13217/32768 are all far above 1980, and there is no pids cgroup);
   the only remaining suspect is **VM overcommit (Committed_AS 28.5GB >> CommitLimit 2.48GB)**, but that usually
   returns ENOMEM rather than EAGAIN — settling it requires the kernel side or a logcat/dmesg capture of the WARN from the failed clone.

---

### A.3 Kernel-Side Clues for Reboots/panics (kernel cmdline captured from dmesg)

The boot cmdline captured from dmesg has several key entries (earlier handoffs said "no serial port, pstore empty"; in reality there **is
ramoops but the pstore directory is empty**, and there **is a hypervisor watchdog**):

```
console=ttyMSM0,115200n8        ← there actually is a serial console (ttyMSM0), it is just not wired up
kernel.panic_on_rcu_stall=1     ← an RCU stall will panic!
pstore.compress=none            ← ramoops is configured
ramoops: using 0x200000@0xf8e00000, ecc: 0   ← ramoops 2MB @ 0xf8e00000
gh-watchdog hypervisor:qcom,gh-watchdog: QCOM Apps Watchdog Initialized   ← hypervisor watchdog
kiwi_v2: Loading driver ... +PANIC_ON_BUG   ← the kiwi driver panics on BUG()
```

**Implications**: the exploit's reclaim mass-kill (the RCU lazy-free wave from 765 flood mm_structs) very likely triggers an
**RCU stall → panic_on_rcu_stall=1 → panic**; or triggers a **hypervisor watchdog hard reset** (that kind of reset does not go through the kernel panic
path → ramoops is not written → pstore is empty, which explains why no oops could be captured). Neither is controllable through
`panic_on_warn`. To capture the scene, **first wire up the ttyMSM0 serial port** (the cmdline has console=ttyMSM0),
or find a way to read /sys/fs/pstore (currently shell is in the log group but the directory is empty, possibly because watchdog resets do not write ramoops).

---

## 4. States Confirmed Unchanged

| Item | State |
|---|---|
| W1 (--write1-text → getenforce=Permissive) | ✅ verified (V10) |
| init_cred->uid = 0 | ✅ settled by this round's dump |
| init_cred / init_task / selinux_state / ashmem_misc offsets | ✅ kallsyms all correct |
| TASK_CRED_OFF=0x780 / TASK_REAL_CRED_OFF=0x778 | ✅ matches commit_creds disassembly |
| rb_erase write#1=target=write_value, write#2=*(write_value)=tree_pc | ✅ settled by disassembly |
| mm-leak (kernelsnitch) | ❌ ~60% flaky (EAGAIN, exhausted at ~2000 tasks) |
| W2 blind write | ❌ child uid=-128 (DIAG still to be run) |
| FOPS install (mode-5) | ❌ concurrent panic (global fops overwrite, known since V10) |

---

## Appendix A: Key Log Excerpts from This Round

### A.1 The panic point drifts (new observation this round)

The eleventh custodian had 6 boots in total, and the device panicked and rebooted at **several different stages** (no serial port, pstore empty, no oops obtainable):

| boot | uptime | Result |
|---|---|---|
| run_r21 (previous custodian) | 396s | mm-leak 11× OK, FOPS LANDED, 10 rounds of blind write uid=-128, round9/10 panic |
| selftest1 | 522s | mm-leak EAGAIN (Threads:1037), no panic |
| selftest2 | 454s | mm-leak in progress (Threads:4097), panic in the collision stage |
| run_r22 | 397s | mm-leak OK, panic after the FOPS punch |
| run_r24/25 | 405/548s | mm-leak EAGAIN (Threads:1043~1057), no panic |
| **run_r26** | **396s** | **mm-leak OK, panic in the reclaim settle stage (before the punch)** |

> **Conclusion**: mm-leak succeeding (Threads:4097) does not mean the run can complete — reclaim's phase-1 mass-kill (765 flood
> SIGKILL + RCU callback wave + vmap stack free) also panics. This is a **resource-stability problem** of a 765+450+4096-thread fork
> storm on a 1.8GB low-memory device, not a single bug. run_r21 completing 10 rounds
> was purely that boot being lucky. **To obtain the DIAG reliably, the fork storm must be shrunk or the reclaim vehicle changed** (see §3).

### A.0 The SKB head-grab flake is settled = the "direct" blocker of FOPS install (parallel agent run_r22_v950diag_110131)

The parallel agent at 11:01:31 (uptime 395s, mm-leak 6/6 all succeeding, `Threads:4097`) produced the complete evidence:

```
FOPS install attempt 1: dip=1 → write#1 MISSED (vehicle -1, llseek=0x0 exp=0xffffffe2733bcbc0)
FOPS install attempt 2: dip=1 → write#1 MISSED
FOPS install attempt 3: dip=0 → MISSED
FOPS install attempt 4: dip=0 → MISSED
FOPS install attempt 5: dip=0 → MISSED
FOPS install attempt 6: dip=1 → MISSED
```

Every round had `pselect returned ret=1 calls=5 success=5` (all 5 sched_setattr calls succeeded), but the DIAG's
`llseek` slot was still the pristine `CFI_NOOP_LLSEEK` (0x...1ab88), **not fops_field**. This means:

- **sched_setattr succeeding ≠ the walk reaching rb_erase's write#1** (when reclaim fails the fake page is garbage,
  the walk does not reach rb_erase or writes into a freed slab that is not in the SKB).
- mode-5 write#1 writes **fake_fops->llseek (inside the SKB page)**, so DIAG MISSED directly = **the SKB did not take the
  leak slab (reclaim failed)**. `dip=1` (the leak slab was discarded) but the SKB cannot grab the head is exactly the
  "SKB head-grab" flake from handoff App.H/J, and **GL_PURGE_CNT=0 + GL_TAIL_KILLS=16 is not enough to solve it**.

**What this means for W2**: mode-2 (blind write)'s write#1 target is `task->cred` (heap, not an SKB page),
but **the walk still needs a valid fake page to reach rb_erase**. So reclaim failing → the walk does not reach rb_erase →
write#1 does not land → the child cred is not changed. This is consistent with child uid=-128 in run_r21 (and `cleanup: 0 diffs` every round)
— **the -128 of the blind write is most likely reclaim failing (the walk not landing), not a wrong task pointer**.
Settling it 100% still needs the init_cred.usage DIAG data (see §2), but "solve the SKB head-grab first" is already established to be the top priority.

### A.2 Failing mm-leak boot (after errno capture)


Failing mm-leak boot (after errno capture):
```
v9.13 diag: dummy clones ok=125 fail=325 (first errno=11 Try again)
kernelsnitch: ks_increase 4096 threads, 3040 failed (first err=11)
Threads:	1057
kernelsnitch: hashsize=1024 sz=0x3c0 single_matches=0 mm=-
kernelsnitch mm leak failed (mm=0000000000000000 r=8)
```

Successful boot (run_r21 / run_r22):
```
v9.13 diag: dummy clones ok=450 fail=0
Threads:	4097
kernelsnitch: hashsize=1024 sz=0x3c0 single_matches=9571 mm=FOUND
leaked mm=ffffff8061a8da40 slab base=ffffff8061a8c000
```
