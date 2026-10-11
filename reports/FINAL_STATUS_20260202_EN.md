# 🔴 Rokid AR Glasses Root Research - Final Status Report

> 中文版：[FINAL_STATUS_20260202.md](FINAL_STATUS_20260202.md)

> ⚠ **Historical document (not the current state)**: this text records the situation at the time; the project has since changed significantly (root acquired, engine switched to runtime probing, phone UI redone, i18n 193 keys).
> For the current state see `README.md`, `MANIFEST.md`, `docs/deploy.md`.


**Date**: 2026-02-02  
**Conclusion**: ❌ **All software attack paths have failed; privilege escalation has not yet succeeded**

> **This is the project's core status document; please read it first to understand the current progress.**

---

## 📊 Executive Summary

| Item             | Status                     |
| ---------------- | -------------------------- |
| **Root access**  | ❌ **Not acquired**        |
| **Bootloader**   | ✅ Unlocked (orange state) |
| **Magisk**       | ✅ v30.6 installed, ❌ not activated |
| **boot.img**     | ❌ Cannot be extracted     |
| **Kernel exploit** | ❌ All failed            |

---

## 🎯 Current Blocker

**The only thing missing is the boot.img file!**

- Bootloader is fully unlocked
- Magisk App is installed
- All that is needed is a usable boot.img for Magisk to patch

### Why boot.img Cannot Be Obtained

| Attempted method                     | Result | Reason                        |
| ------------------------------------ | ---- | ----------------------------- |
| `adb shell dd /dev/block/mmcblk0p17` | ❌   | Permission denied (requires root) |
| `fastboot fetch boot_a`              | ❌   | Device does not support this command |
| `fastboot oem dump-partitions`       | ❌   | unknown command               |
| Recovery Sideload                    | ❌   | Gets stuck due to driver problems |
| Custom Recovery (TWRP)               | ❌   | No compatible version found   |
| Official OTA firmware                | ❌   | No download source found      |

---

## ❌ Attack Paths Tested and Confirmed Failed

### Kernel Exploits

| CVE/Technique                | Versions | Reason for failure            |
| ---------------------------- | ------ | ---------------------------- |
| CVE-2024-36971 (Route UAF)   | v1-v13 | Loopback DST never becomes obsolete |
| CVE-2024-0582 (io_uring UAF) | -      | PBUF_RING feature does not exist (5.19+) |
| CVE-2022-0847 (Dirty Pipe)   | -      | Patched                       |
| CVE-2019-2215 (Binder UAF)   | -      | Patched                       |
| CVE-2023-2163 (BPF Verifier) | -      | Patched                       |
| CVE-2024-38401 (KGSL UAF)    | v1-v6  | Patched                       |
| CVE-2025-21479 (KGSL)        | v1-v4  | Context creation is blocked   |

### BPF Information Leaks (KASLR Bypass)

| Technique        | Versions | Reason for failure                  |
| ---------------- | ------ | ----------------------------------- |
| BPF Ringbuf OOB  | v1-v6  | Blocked by the verifier; pointer arithmetic forbidden for non-root |
| BPF pointer leak | -      | ringbuf_reserve returns NULL        |
| Perfcounter leak | -      | Returns a GPU MMIO offset, not a kernel pointer |

### GPU/Driver Attacks

| Technique                 | Versions | Reason for failure          |
| ------------------------- | ------ | ------------------ |
| KGSL uninitialized memory | v1-v3  | GPU memory is fully zeroed  |
| DRM msm exploitation      | v1-v2  | No exploitable vulnerability |

### Other

| Technique       | Reason for failure            |
| --------------- | ------------------------ |
| TIPC bug        | AF_TIPC available but nothing exploitable |
| Binder bug      | v2-v5 all failed, patched |
| Ashmem overflow | No exploitable vulnerability |
| nf_tables       | CONFIG_NF_TABLES not enabled |

---

## ✅ Techniques Verified as Working

> These techniques work but **cannot achieve privilege escalation on their own**

| Technique         | Status | Notes                               |
| ----------------- | ---- | ----------------------------------- |
| BPF Heap Spray    | ✅   | Can spray 192-byte objects (ip_dst_cache) |
| KGSL GPUOBJ_ALLOC | ✅   | Can allocate GPU memory             |
| Basic io_uring operations | ✅ | 37 operations available         |
| TIPC socket creation | ✅ | Can create and bind              |
| Fastboot connection | ✅ | Can enter Fastboot mode           |

---

## 📱 Complete Device Information

```
Model: Rokid RG-glasses (RV101)
Android: 12 (SKQ1.240613.001)
Kernel: 5.10.209-perf-gb80a8319d11b
SoC: Qualcomm neo (SoC ID: 579)
GPU: Adreno 621v1 (Chip ID: 0x6020100)
Security patch: 2024-07-05
Boot partition: boot_a = /dev/block/mmcblk0p17 (96MB)
Bootloader: unlocked (unlocked:yes, secure:no)
```

---

## 🔧 Remaining Viable Paths

### Priority 1: Obtain boot.img

1. **Find official Rokid firmware** (best)
   - Version: 1.12.009 or any version
   - Unpack with `payload-dumper-go`

2. **Find a compatible TWRP**
   - Platform: Qualcomm SM6375/khaje
   - Boot it temporarily with `fastboot boot twrp.img`

3. **EDL mode** (requires opening the device)
   - Short the test points to enter Qualcomm download mode
   - Read partitions with the QFIL tool

### Priority 2: Qualcomm-specific vulnerabilities

- Requires deeper research into the KGSL driver
- Undisclosed vulnerabilities may exist

---

## 📁 Important File Index

### Primary files (recommended to keep)

| File                           | Purpose              |
| ------------------------------ | ----------------- |
| `HANDOVER_SESSION_20260131.md` | Complete session handoff |
| `FINAL_STATUS_20260202.md`     | This file - final status |
| `Rokid_Root_Guide_boot_img.md` | Root procedure guide |
| `CVE_5_10_x_Scan_Report.md`    | Vulnerability scan report |

### Final-version PoCs (other intermediate versions can be deleted)

| File                    | Purpose               |
| ----------------------- | ------------------ |
| `cve_2024_36971_v13.c`  | Final Route UAF version |
| `bpf_ringbuf_oob_v6.c`  | Final Ringbuf leak version |
| `kgsl_exploit_v9.c`     | Final KGSL exploit version |
| `io_uring_uaf_verify.c` | io_uring verification |

---

## ⚠️ Caveats

1. **Every item marked "success" only means the technique is feasible**; it does not mean privilege escalation succeeded
2. **The unlocked bootloader is the key advantage**; root only needs boot.img

---

_End of report - 2026-02-02_
