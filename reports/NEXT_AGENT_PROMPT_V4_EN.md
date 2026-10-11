# Handoff Prompt — GhostLock RV101 (CVE-2026-43499), Fifth Agent

> 中文版：[NEXT_AGENT_PROMPT_V4.md](NEXT_AGENT_PROMPT_V4.md)

> ⚠ **Historical document (not the current state)**: this text records the situation at the time; the project has since changed significantly (root acquired, engine switched to runtime probing, phone UI redone, i18n 193 keys).
> For the current state see `README.md`, `MANIFEST.md`, `docs/deploy.md`.


The full background has been written into `reports/GHOSTLOCK_HANDOFF_V4.md` (required reading) and
`reports/GHOSTLOCK_V3_PANIC_ROOTCAUSE_ANALYSIS.md`. Below is a directly executable handoff prompt:

---

You are taking over the CVE-2026-43499 (GhostLock) root exploit project for the Rokid RV101 (working directory
`C:\Users\<USER>\Documents\codebase\rokid_root`). The fourth agent's conclusions and progress:

1. **v3's "fake page panic" has been falsified as a "garbage-page walk from failed reclaim"** — the payload was never
   forensically confirmed to be in place; the o2 evidence in v9 was only inference. A genuine payload-walk (5 write locations,
   forensically visible) has not been observed even once so far.
2. Ruled out: capturing the PC via dmesg (this device's panic is a hard reset, with no oops output at all), the spinlock debug
   hypothesis (the lock helpers are vanilla qspinlock, disassembly in rv101_disasm/spinlock_helpers.txt),
   punch timing (already changed to multi-punch, GL_POISON measured reaching trylock), and clone/memfd failure
   (fail=0, the slab arithmetic is intact).
3. The reclaim mechanism has been fixed to the point of "the gate triggers intermittently": run 10's tight probe `o2 2470→2471` proves the leak slab
   really is discarded back to the buddy allocator by the slab_empty gate; but payload placement is still not achieved (run 12 still shows zero writes /
   panic, and the whole sequence of attempt 1 showing o2 +63 indicates an **RCU noise flood**).

**Your one and only task: make the vehicle forensics show a diff (= payload in place + walk writes), then
stabilize selftest → W1 → W2.**

Execute in order:

1. **Adaptive settle (the number-one suspect)**: `prepare_good_kernel_page()` in `src/util.c` currently uses a fixed
   `usleep(150000)` after the kill. Change it to: poll `debug_pagetype_o2()` (every 100ms) and only enter the close sequence
   once two consecutive reads are identical (RCU quiet) (or simply fix it at 1-2s). The +63 in run 12 proves 150ms
   cannot flush the kernel-stack return flow of 765 children, and that flow can at any moment land inside the
   µs window between leak-close and the first send and steal the leak slab.
2. Build (`$env:ANDROID_NDK_HOME='...\android-ndk-r27d'; mingw32-make -B`) → push →
   **compare Get-FileHash against the device md5sum** (the PowerShell pipeline swallows make's failure code) → wait for the boot
   to stabilize (uptime ≥ 5 minutes) before running `--selftest`.
3. Selftest criteria (already printed by cleanup): a vehicle diff line is present, `child_pc=(target-8)|1`,
   `scratch=0x…500`, `fake_task_usage=0x101`. After each panic the device restarts in 2-5 minutes and
   KASLR is re-leaked automatically.
4. If placement is achieved but the walk still panics: use the "safe triage" variant to bisect without a PC (V4 handoff §3.3):
   leftmost=0 → NOOWNER → w0->lock=0xdead… narrowing the fault point section by section.
5. Once selftest is stable: W1 (`--write1` → getenforce=Permissive) → W2 (fake fops →
   configfs arbitrary read/write → cred=init_cred; for kallsyms offsets see v3 handoff §5).

Operating discipline (as before): every adb command carries `-s <DEVICE_SERIAL>`; do not touch the Redmi `<PHONE_SERIAL>` if it is online;
dmesg/pstore are useless, no need to try them again; the only progress probes are forensics and controlled-fault.
