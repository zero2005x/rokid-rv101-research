# DeepSeek Harness Prompt for GhostLock RV101 (v9.30)

> 中文版：[NEXT_AGENT_PROMPT_V7.md](NEXT_AGENT_PROMPT_V7.md)

> ⚠ **Historical document (not the current state)**: this text records the situation at the time; the project has since changed significantly (root acquired, engine switched to runtime probing, phone UI redone, i18n 193 keys).
> For the current state see `README.md`, `MANIFEST.md`, `docs/deploy.md`.


You are the eighth agent to take over the GhostLock project. The goal is to carry the RV101 device's root privilege escalation chain through to its final stage.

## Current Project Status
The project code is at `source_code\ghostlock_rv101\` and is currently built to `v9.30` (already synced to the device).
The first seven agents completed kernel vulnerability triggering, the KASLR leak, the kernel mm leak, heap feng shui, and verification of the write primitive.
The seventh agent successfully located and fixed the two major address-computation bugs that caused "the W1 write not landing" (v9.29 fixed the symbol offset, v9.30 fixed the phys load base).
However, in the first on-device test after the address fix (run51), the device suffered a kernel panic during the punch 1 stage (3/3 all died).

## Your Primary Task: Diagnose the Root Cause of the run51 Panic
Because the `rb_erase` write primitive is an 8-byte pointer write while `selinux_state.enforcing` is only 1 byte, the strong suspicion is: the write smeared a garbage pointer value over the bool fields following `selinux_state` (such as `disabled`), causing a crash when the kernel accessed the SELinux state.

Please proceed as follows:

1. **Harmless-target test**:
   Run W1 mode, but use the environment variable `GL_TARGET_DELTA` to shift the write target to a harmless RAM location (for example `+0x800000`, back to the previously wrong address).
   Example command:
   `adb -s <DEVICE_SERIAL> shell "GL_SKB_PAD=64 GL_DEBUG_DRAIN=1 GL_DUMMY_KILLS=180 GL_TARGET_DELTA=8388608 /data/local/tmp/ghostlock_rv101 --write1"`
   - If 0/3 die and enforce is still 1: confirm it is the "8-byte smear" problem.
   - If it still dies 3/3: confirm it is the old "vmap purge flood" race condition.

2. **If confirmed to be the 8-byte smear problem**:
   Since the write width of `rb_erase` cannot be changed, evaluate skipping W1 (SELinux) and advancing directly to W2.
   W2's goal is to overwrite the task's `cred` pointer with `init_cred`; the pointer length is exactly 8 bytes, a perfect fit for the write primitive, with no smearing problem.

3. **Fix the W2 path**:
   Inspect the W2 flow in `main.c` and `util.c`. It currently uses `perf_find_task()`, but the device's `perf_event_paranoid=3` blocks perf.
   You need to implement an alternative (such as the configfs/FOPS route) to locate the address of the child task_struct.

## Environment Discipline
- Device serial: `<DEVICE_SERIAL>`
- Before each run, make sure the device uptime > 150s.
- After each panic the device restarts automatically in 2-5 minutes, and the KASLR slide changes.
- Use `scripts/ghostlock_reboot_run.ps1` for reboot-per-attempt automated testing.
- Build command: `$env:ANDROID_NDK_HOME='C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\android-ndk-r27d'; cd source_code\ghostlock_rv101; mingw32-make -B`

Please first read `reports/GHOSTLOCK_HANDOFF_V7.md` and `source_code/ghostlock_rv101/src/main.c`, then start with the harmless-target test.
