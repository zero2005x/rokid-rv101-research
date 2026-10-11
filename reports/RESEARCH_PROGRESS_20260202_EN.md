# Rokid AR Glasses Root Research Progress Report

> 中文版：[RESEARCH_PROGRESS_20260202.md](RESEARCH_PROGRESS_20260202.md)

> ⚠ **Historical document (not current state)**: this write-up records the situation at the time; the project has changed substantially since (root acquired, engine switched to runtime probing, phone UI reworked, i18n 193 keys).
> For the current state see `README.md`, `MANIFEST.md`, `docs/deploy.md`.


**Date**: 2026-02-02
**Target device**: Rokid AR Glasses (Kernel 5.10.209-perf, Android 12)

---

## Current Status

### ✅ Achieved

| Item            | Status      | Notes                                       |
| --------------- | --------- | ------------------------------------------ |
| Bootloader unlocked | ✅ Done   | `unlocked: yes`, `critical unlocked: true` |
| Magisk installed     | ✅ Installed | v30.6, but not activated                            |
| BPF validation        | ✅ Done   | Data flow succeeded, but pointers cannot be leaked                 |
| KGSL probing       | ✅ Done   | GPUOBJ can be allocated, but there is no info leak                |

### ❌ Attack Paths Confirmed Failed

| CVE/Technique                   | Reason                                         |
| -------------------------- | -------------------------------------------- |
| BPF Ringbuf OOB            | The verifier tracks bounds strictly; pointer arithmetic is forbidden for non-root  |
| BPF pointer leak               | `ringbuf_reserve` returns NULL in socket_filter |
| KGSL uninitialized memory          | GPU memory is correctly zeroed                           |
| Reading the boot partition directly         | Requires root privileges (`brw-------`)                |
| CVE-2024-36971             | Slab isolation                               |
| CVE-2024-0582              | io_uring PBUF_RING feature does not exist                |
| CVE-2022-0847 (Dirty Pipe) | Patched                                       |
| CVE-2019-2215 (Binder UAF) | Patched                                       |

---

## Detailed Device Information

```
Model: Rokid/glasses/glasses
Android: 12 (SKQ1.240613.001)
Kernel: 5.10.209-perf
SoC: Qualcomm (platform=neo, soc_id=579)
GPU: Adreno 6xx (Chip ID: 0x6020100)
Slot: a (boot_a = /dev/block/mmcblk0p17)
Boot partition size: 96MB (0x6000000)
```

---

## Next Action Plan

### Option A: Fastboot boot a custom recovery (recommended)

1. Find/compile a TWRP recovery for the Qualcomm neo platform
2. Boot it temporarily with `fastboot boot twrp.img`
3. From the TWRP shell run `dd if=/dev/block/mmcblk0p17 of=/sdcard/boot_a.img`
4. Pull boot.img and patch it with Magisk
5. Flash the patched boot: `fastboot flash boot_a patched_boot.img`

### Option B: Find an official OTA package

1. Search for official Rokid firmware updates
2. Extract boot.img with payload-dumper-go
3. Patch it with Magisk and flash it

### Option C: Use Magisk Direct Install (requires ADB root)

1. If the device supports `adb root` (usually the case on userdebug builds)
2. Run Magisk Direct Install

### Option D: Look for other kernel vulnerabilities

1. Study Qualcomm-specific CVEs (e.g. CVE-2023-33063 KGSL UAF)
2. Explore other world-writable device nodes
3. Study vendor-specific vulnerabilities

---

## Tools Created

| File                      | Purpose                 |
| ------------------------- | -------------------- |
| `bpf_ringbuf_oob_v1-v6.c` | BPF ringbuf OOB attempts |
| `kgsl_vuln_scan.c`        | KGSL device scan        |
| `kgsl_exploit_v2-v3.c`    | KGSL memory-leak attempts    |
| `extract_boot.sh`         | Boot partition extraction script    |

---

## Key Findings

1. **BPF restriction**: Kernel 5.10.209 forbids pointer arithmetic for non-root users
   - Error message: `pointer arithmetic with it prohibited for !root`

2. **KGSL is accessible but safe**:
   - `/dev/kgsl-3d0` and `/dev/dri/*` are world-writable
   - but GPU memory allocations are correctly initialized (no info leak)
   - Context creation requires specific flags

3. **Magisk needs boot.img**:
   - Magisk v30.6 is installed but su is unavailable
   - a patched boot.img is required to activate it

---

## Recommendations

Priority order:

1. **Option A** — if a compatible recovery image can be found
2. **Option B** — if an OTA update package can be found
3. **Option D** — if neither of the first two is feasible, continue studying kernel vulnerabilities
