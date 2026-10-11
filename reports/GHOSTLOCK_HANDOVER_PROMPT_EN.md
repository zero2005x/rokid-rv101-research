# GhostLock (CVE-2026-43499) RV101 Root Exploit — Handover Prompt (for the next agent)

> 中文版：[GHOSTLOCK_HANDOVER_PROMPT.md](GHOSTLOCK_HANDOVER_PROMPT.md)

> ⚠ **2026-09-13 update**: the working directory has changed to `/home/kali/rokid_root` (WSL/Kali; the old
> `C:\Users\<USER>\Documents\codebase\rokid_root` is no longer used). **Runtime escalation is complete and stable**
> (two paths, bridge/Shizuku, armed once per boot via adb); the current maintenance focus is the **operations layer**
> (survival watchdog, automatic engine probing, escalation metrics), see `docs/deploy.md` §escalation operations P0–P3.


> This document is the complete "takeover instruction". Please continue the work starting directly from this document, and do not redo analysis that is already complete.
> Working directory: `C:\Users\<USER>\Documents\codebase\rokid_root`

---

## 0. Mission overview

Write a **CVE-2026-43499 (GhostLock) local privilege-escalation exploit** for the
**Rokid RV101 (RG-glasses, codename neo, Android 12, Qualcomm QCM4290-class SoC, 4 cores)** to obtain root.
kernel: `5.10.209-perf-gd908d71a0404-ab1.22.009-20260710-150201` (boot.img matches the device).
The device is connected over adb (serial `<DEVICE_SERIAL>`), shell uid=2000, SELinux Enforcing,
kptr_restrict=2, dmesg_restrict=0, seccomp=0, **perf_event_paranoid=3 (perf is blocked, measured EACCES)**.

**Status: the exploit chain is about 90% complete; the last obstacle is "slab reclaim of the fake kernel page".
Once reclaim is fixed, W1 (SELinux permissive) can be verified → then W2 (cred = init_cred).**

---

## 1. Completed research results (do not redo)

### 1.1 The essence of the GhostLock vulnerability (confirmed)
- CVE-2026-43499: rt_mutex futex PI **stack-UAF**. `remove_waiter()` (kernel/locking/rtmutex.c)
  incorrectly clears `current->pi_blocked_on` instead of `waiter->task->pi_blocked_on`.
  Trigger: a 3-futex PI ring + `FUTEX_CMP_REQUEUE_PI` causes the -EDEADLK rollback to execute in the context of
  **the requeue side (not the waiter side)** → the real waiter's (A's) `pi_blocked_on` is left dangling, pointing at a dead `rt_mutex_waiter` on A's kernel stack.
- Fix commit: mainline `3bfdc63936dd` (fixed in 7.1); affected range 2.6.39~7.1; Android GKI 6.12.x was still unfixed at the time.
- The full fix diff is at `research/ghostlock_refs/extracted/openvz_patch/.../CVE-2026-43499-vzkernel3.10.patch`.
- **The RV101 kernel is confirmed unpatched by disassembly**: `remove_waiter` @ `0xffffffc0081cffb0` uses
  `mrs x20, sp_el0` (current) + `str xzr, [x20, #0x888]` (current->pi_blocked_on = NULL),
  with no dereference of `waiter->task` ([waiter+0x30]) at all. Evidence: `research/ghostlock_refs/remove_waiter.disasm.txt`.

### 1.2 Successful cases (web research; reference code is already local)
Already cloned (unzipped) under `research/ghostlock_refs/extracted/`:
- **OnePlus** `oneplus/GhostLock-for-OnePlus-main/` (6.12 SM8845, escalation with locked BL, two-stage write, verified)
- **Honor** `honor/cve-2026-43499-honor-main/` (6.1.128 SM8650, includes docs/report.md and exploit source)
- **POCO** `aresin/ghostlock-aresin-main/` (4.14 MT6893, trigger details and KernelSnitch)
- Others: `emerald/`, `m3q/`, `selinuxdis/` (SELinux disabler ideas)
- Original research: NebuSec CyberMeowfia's "IonStack" series.

### 1.3 RV101 kernel static analysis (all complete; artifacts in `research/ghostlock_refs/`)
- Full kallsyms extraction: `rv101_kallsyms.txt` (176,575 symbols; tool vmlinux-to-elf, installable via pip).
- Disassembly script: `scripts/ghostlock_analysis/disasm_func.py` (capstone 5.0.7); disassembly of key functions is in
  `rv101_disasm/*.txt` (rt_mutex_adjust_prio_chain, task_blocks_on_rt_mutex, futex_*, core_sys_select,
  hash_futex, get_futex_key, __sched_setscheduler, commit_creds, sel_write_enforce, etc.).
- Original kernel: `binaries/extracted_1.22/kernel` (uncompressed ARM64 Image, `ARMd` magic @0x38,
  link base 0xffffffc008000000).
- All derived offsets/constants are centralized in `source_code/ghostlock_rv101/target_rv101.h` (all values below are disasm-verified).

### 1.4 Key address-space facts (measured and verified)
- **VA_BITS = 39** (the leaked mm is at `0xffffff80..`, the kernel image runtime is at the `0xffffffde..` level).
- `PAGE_OFFSET = 0xffffff8000000000`, `PHYS_OFFSET = 0x80000000` (/proc/zoneinfo start_pfn=524288),
  `KIMAGE_TEXT_BASE = 0xffffffc008000000`, `P0_KERNEL_PHYS_LOAD = 0x80800000`
  (TEXT_OFFSET=0x800000; **0x80080000 once caused W1 to write into random RAM and panic, while 0x80800000 does not panic —
  but it has not yet succeeded in flipping enforcing; the cause may be a reclaim failure rather than a wrong alias, see §3**).
- `futex_hash` (private) = `jhash2({mm_lo,mm_hi,addr_lo,addr_hi}, 4, addr & 0xfff)`,
  bucket = hash & (hashsize-1), hashsize=1024 (4 cores). **Note the word order puts mm first and there is no |2 flag**
  (|2 is specific to the shared-futex anonymous-mapping path).
- `mm_struct` slab: **order-2 (16KB)**, objsize **0x3C0**, 17 obj/slab (measured from /proc/slabinfo).

### 1.5 Status of each link in the exploit chain
| Link | Status | Evidence/location |
|---|---|---|
| KASLR leak (printk_formats) | ✅ done | `/sys/kernel/tracing/printk_formats` is readable, anchors "Rescheduling interrupts"@0x1c2ac0c, "Function call interrupts"@0x1cca100; the slide changes on every reboot, just re-leak |
| KernelSnitch mm leak | ✅ done | futex hash collision timing (3/3 collisions stable) + linear-map brute force hit (single_matches≈12k, mm=FOUND) |
| GhostLock trigger | ✅ done | requeue returns -EDEADLK (errno 35) reproducibly |
| pselect forgery | ✅ done | `rt_waiter = stack_fds + 0x30` (frame arithmetic in the target_rv101.h comments); PWS_* word mapping is correct |
| punch (sched_setattr→rt_mutex_adjust_pi) | ✅ done | disassembly confirms the pi=true path; the punch_done sync flag avoids unlock clobbering the forged bytes |
| walk reaches the forged waiter / rb_erase executes | ✅ proven | **poison test**: both `GL_POISON=1` (lock=0xdead→trylock crash) and `GL_POISON2=1` (tree_pc=0xdead→erase parent deref crash) cause a kernel panic reboot → the walk really arrives |
| **fake page reclaim** | ❌ **the only obstacle** | the freed mm slab is not obtained by the sub-spray carrier (keyctl_read reads back zero write traces) |
| W1 (selinux_state.enforcing=0) | ⏸ to be verified after reclaim is fixed | target = data_addr(selinux_state)=`0xffffff802b4a0140` (enforcing byte @+0, confirmed by disassembling sel_write_enforce) |
| W2 (cred=init_cred) | ⏸ to do | perf is blocked → needs the FOPS route (see §5) |

---

## 2. Current state of the exploit code

Location: `source_code/ghostlock_rv101/`
- `target_rv101.h` — all offsets/constants (including the PWS_* mapping, KEY_PAYLOAD_DELTA=0x18, etc.)
- `src/main.c` — flow: `--selftest` (write into its own sprayed page) / `--write1` / full flow (W1→W2 skeleton)
- `src/route.c` — trigger thread + pselect forgery + consumer punch + punch_done synchronization
- `src/util.c` — KASLR leak, KernelSnitch, mm spray, **key-payload reclaim**, perf (blocked), forensic
- `src/perf_probe.c`, `Makefile`

Build (Windows, NDK r27d):
```powershell
cd source_code\ghostlock_rv101
$env:ANDROID_NDK_HOME='C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\android-ndk-r27d'
mingw32-make -B
adb push build\ghostlock_rv101 /data/local/tmp/ghostlock_rv101
adb shell chmod 755 /data/local/tmp/ghostlock_rv101
```
Run: `adb shell /data/local/tmp/ghostlock_rv101 --selftest` (safe verification) or `--write1`.
Environment variables: `GL_POISON`, `GL_POISON2` (poison tests), `W1_ATTEMPTS`, `SELFTEST_RUNS`.

**Important notes**:
- After every kernel panic the device reboots automatically (about 2-5 minutes); use `adb wait-for-device`; the slide changes and KASLR re-leaks automatically.
- pstore is empty (a panic leaves no trace), so debugging from a crash log is impossible — only the poison bisection works.
- After modifying `target_rv101.h`, always run `mingw32-make -B` (the header has dependency rules, but -B is safest).

---

## 3. Current blocker: reclaim failure (the key task)

**Symptom**: after selftest mode (write target = the page it sprayed itself) finishes, the payload read back by
`keyctl_read` is **byte-for-byte identical** to the pristine copy (no trace of any kernel write: child_pc=0, wait_lock=0, usage=0x100 unchanged).
Yet the poison tests prove the walk reaches the forged waiter and executes rb_erase. → Conclusion: **the key payload did not get the
freed mm slab** (the writes landed on the contents of the old slab still held by mm_cachep and were silently absorbed).

**Sequences already tried**:
1. SKB sendmsg reclaim (~16KB payload → kmalloc-16k) + various free orders → failed.
2. key payload reclaim (`add_key("user",..., 16360B, KEY_SPEC_PROCESS_KEYRING)`,
   data is at kmalloc object+0x18, `KEY_PAYLOAD_DELTA=0x18`) + flooding many mm slabs → still failed.
3. Free order: leak slab freed first vs **freed last** (the current version is leak-last: first close the pre/post/spray/prepare
   memfds to fill node partial, and finally close(memfd_leak) to trigger discard) → the last test still showed no write.

**Suggested next steps (in priority order)**:
1. **Run selftest in a large loop** (`SELFTEST_RUNS=20`) to see whether a lucky hit occurs (the appearance of a diff proves reclaim succeeded;
   switch to W1 immediately). Note that each round re-sprays; statistically, as long as the free path occasionally returns the leak slab to buddy, a hit is possible.
2. **Observe buddy changes**: whether the order-2 free count in /proc/pagetypeinfo rises before and after the flood;
   if it does not rise at all → SLUB is keeping the slab in partial (increase the flood: `MM_PARTIALS`, multiply the prepare count 3-5×).
3. **Switch reclaim carrier** (kmalloc-16k family, landing precisely in order-2 according to kmalloc size):
   - `setxattr` with a large value (Android has a limit; try it first),
   - `sendmsg` with the payload adjusted so the skb head is exactly ≤16KB (skb_shared_info overhead is about +0x200),
   - BPF ringbuf/array map (`unprivileged_bpf_disabled=0`, but the verifier must allow it),
   - the `keyctl` big_key type (note the quota: `/proc/sys/kernel/keys/`).
4. **Locate the physical address of the reclaim carrier in reverse**: if the carrier itself can be read back (key can, socket can recv), first confirm
   whether the carrier's contents were written by the walk; if not → the carrier did not get the slab; if yes but the offset is odd → calibrate SKB_DATA_DELTA/KEY_PAYLOAD_DELTA.
5. If reclaim is completely unsolvable: evaluate a "no-spray" alternative — putting the fake lock/fake task into **real objects at a known linear address**
   (e.g. near the init_task.pi_waiters region) is not feasible (it would corrupt init_task); or switch to the boot_id leak style of the OnePlus SLIDE route
   (KASLR only, which does not help this problem).

---

## 4. W1 verification (to be run immediately after reclaim is fixed)

1. The selftest forensic should show: `child_pc=(target-8)|1`, `w0_right=<stack waiter addr>`,
   `fake_task_usage=0x101`, `wait_lock=1`, `scratch=<written value>` → proving the whole primitive is correct.
2. `adb shell /data/local/tmp/ghostlock_rv101 --write1` → expect `getenforce` to become **Permissive**
   (/sys/fs/selinux/enforce reads back 0).
3. If enforcing did not flip and there is no panic: check the target alias (`0xffffff802b4a0140`) and
   `P0_KERNEL_PHYS_LOAD` (candidates: 0x80800000, 0x80080000, 0x80000000; a panic means the alias is wrong).
   Criteria: panic = wrote into non-image RAM; no effect and no panic = the walk did not write or there is a reclaim problem.

---

## 5. W2 plan (the FOPS route after perf was blocked)

- `perf_event_open` measured **EACCES** (paranoid=3), so W2 cannot use OnePlus's perf_find_task.
- Adopt the reference projects' **FOPS route** (OnePlus fops.c / Honor root.c):
  1. Use the same write primitive as W1 to write the **fake file_operations table address** into `ashmem_miscs[0].fops`
     (data_addr(ASHMEM_MISC_FOPS), finding RV101's symbol from kallsyms).
  2. Put the fake fops table in the sprayed page: `llseek=noop_llseek`, `read_iter=configfs_read_iter`,
     `write_iter=configfs_bin_write_iter`, and the rest the original ashmem functions (find the symbols in rv101_kallsyms.txt).
  3. Control the configfs fields through the ashmem `SET_NAME` blob → arbitrary kernel read/write
     (configfs_write_once/configfs_read_once, see OnePlus util.c).
  4. Walk the task list from `init_task.tasks` (requires `TASK_TASKS_OFF/TASK_PID_OFF/TASK_TGID_OFF`,
     derived by disassembling `__arm64_sys_getpid`/`sys_kill` etc., or verified directly by writing and reading through configfs) to find the child task_struct.
  5. `task->cred` (0x780) ← data_addr(init_cred) (or patch the original cred: uid/caps/sid),
     and clear seccomp (shell has no seccomp, can be skipped).
  6. The child writes `/sys/fs/selinux/enforce 0` and execs `/data/local/tmp/.ghostlock_root.sh` (already built in).
- The required symbol addresses can be grepped directly from `research/ghostlock_refs/rv101_kallsyms.txt`.
- One may also first explore: once W1 succeeds (permissive), whether there is a simpler escalation shortcut.

---

## 6. Key file index

| File | Content |
|---|---|
| `binaries/extracted_1.22/kernel` | uncompressed ARM64 Image (analysis source) |
| `research/ghostlock_refs/rv101_kallsyms.txt` | full symbol table |
| `research/ghostlock_refs/rv101_disasm/*.txt` | disassembly of key functions |
| `research/ghostlock_refs/extracted/{honor,oneplus,aresin,...}/` | reference exploit source and reports |
| `research/ghostlock_refs/remove_waiter.disasm.txt` | unpatched evidence |
| `scripts/ghostlock_analysis/disasm_func.py` | capstone disassembly tool |
| `source_code/ghostlock_rv101/` | this exploit (see §2) |
| `reports/REVERSE_ANALYSIS_1.22_009.md` etc. | existing device reverse-engineering background (fastboot crippling, EDL, etc.) |

---

## 7. Notes for the agent taking over

1. **Do not redo** the analysis in §1; first run `--selftest` once to become familiar with the output and flow.
2. The device reboots on every panic (normal, do not panic); the slide changing after a reboot is normal.
3. The poison tests are the only reliable probe of walk progress (pstore is unusable).
4. After changing constants always run `mingw32-make -B`; `target_rv101.h` is the single source of all offsets.
5. Acceptance criteria: the selftest forensic shows the write traces of §4 → reclaim is fixed; `getenforce` returns 0 → W1 succeeded;
   `id` returns uid=0 → W2 succeeded.
6. Final deliverable: a single ELF (`ghostlock_rv101`) that runs under adb shell (uid 2000) and reliably obtains a root shell,
   together with usage instructions and a research summary (for the summary of successful cases see the README of each repo under `research/ghostlock_refs/` and §1.2 of this document).
