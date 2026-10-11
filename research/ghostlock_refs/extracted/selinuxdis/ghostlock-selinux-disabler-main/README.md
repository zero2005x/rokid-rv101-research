# GhostLock SELinux Disabler

Write-1-only extraction of
[JoinChang/ghostlock-oneplus](https://github.com/JoinChang/ghostlock-oneplus)
for [CVE-2026-43499](https://nvd.nist.gov/vuln/detail/CVE-2026-43499). It
disables SELinux `enforcing` on supported, vulnerable OnePlus kernels with a
locked bootloader, and does nothing else: no cred overwrite, no root shell, no
KernelSU, no bootstrap, and no KASLR leak.

## Vulnerability

CVE-2026-43499 is a Linux kernel rtmutex use-after-free in the futex PI requeue
path. During proxy-lock rollback, `remove_waiter()` operates on `current`
instead of the waiter's task, leaving the waiter's `pi_blocked_on` pointer
dangling. It is fixed upstream in Linux 6.12.86 and newer 6.12 releases.

## What it does

A single child-node PI write:

```
fake_right = base + 0x100   ->  byte0 = 0 (enforcing), byte1 = 1
```

It reuses the full exploit's heap spray + `pselect` PI route, but bypasses KASLR
(`kaslr_slide = 0`). It retries up to 20 times (with slab draining between
attempts) and exits once `/sys/fs/selinux/enforce` reads `0`.

## What was dropped from the full exploit

| Removed | Reason |
|--------|--------|
| `slide.c` | KASLR SLIDE leak — not needed (slide = 0) |
| `pipe.c` | pipe-based physical R/W — only used by the CFI/root stage |
| `root.c` | cred patching / `install_android_root` — Write 2 only |
| `miniadb.c` | bootstrap mini ADB client — not used |
| `try_cfi_stage`, `install_child_root`, `repair/refresh/leak/restore` helpers (fops.c) | CFI / KASLR-leak / pipe-physrw stage — never reached on the custom-write path |
| `perf_find_task`, `spawn_child`, `write_root_script`, `run_exploit`, `run_bootstrap` (main.c) | Write 2 (cred → init_cred), root shell, KSU, bootstrap |

The shared primitive (`util.c` heap spray, `fops.c` pselect route,
`kernelsnitch`, and offset tables) is retained.

## Tested kernels

See `src/devices/offsets.h`, keyed by `uname -r`:

- OnePlus Ace 6T — `6.12.38-android16-5-...-ab14275539-4k`
- OnePlus Ace 6T — `6.12.38-android16-5-...-ab14552068-4k`
- OnePlus 15 — `6.12.23-android16-5-...-ab14541642-4k` (offsets only, unverified)

## Build

```bash
make            # uses /opt/android-ndk (or $ANDROID_NDK_HOME)
```

Produces `selinux_disabler`, an aarch64 PIE for Android API 35.

## Usage

```bash
adb push selinux_disabler /data/local/tmp/a/e && chmod 755 /data/local/tmp/a/e
adb shell /data/local/tmp/a/e
```

Verify:

```bash
adb shell cat /sys/fs/selinux/enforce   # -> 0
```

## Adding devices

Only `boot.img` is needed. Use the extraction utilities in the upstream
project's [tools directory](https://github.com/JoinChang/ghostlock-oneplus/tree/main/tools),
then add an entry under `src/devices/<device>/offsets.h` and `#include` it from
`src/devices/offsets.h`.

## License

For authorized security research and educational purposes only.
