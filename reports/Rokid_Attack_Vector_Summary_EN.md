# Rokid AR Glasses Attack Vector Summary Report

> 中文版：[Rokid_Attack_Vector_Summary.md](Rokid_Attack_Vector_Summary.md)

> 📌 **Basis of fact**: wherever this document touches facts such as device generation, kernel config, CVE status, GhostLock offsets or product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (the single authoritative fact table; it decides any conflict).

> ⚠ **2026-09-13 correction**: the **Fastboot/Magisk/boot.img flashing route mentioned in this document is confirmed infeasible** (boot.img cannot be obtained: `fastboot fetch` is unsupported, `dd` requires root, and there is no compatible recovery/OTA source), and **root was acquired on 2026-08-22 via the GhostLock LPE** (at runtime, armed once per boot over adb). Treat the following sections as a historical assessment and do not follow the flashing steps.


## 🎯 Executive Summary

**Device**: Rokid AR Glasses (RG-glasses / RV101)  
**Kernel**: Linux 5.10.209-perf-gb80a8319d11b  
**Android**: 12 (API 32)  
**Security patch**: 2024-07-05

---

## 🔴 Key Finding: Bootloader Is Unlocked!

### Confirmed Unlock State

| Property                      | Value        | Meaning             |
| ----------------------------- | ------------ | ------------------- |
| `ro.boot.vbmeta.device_state` | **unlocked** | Device state is unlocked     |
| `ro.boot.verifiedbootstate`   | **orange**   | Orange = custom system can be flashed |
| `ro.boot.flash.locked`        | **0**        | Flash is not locked        |
| `ro.oem_unlock_supported`     | **1**        | OEM unlock is supported      |
| `ro.boot.slot_suffix`         | **\_a**      | Currently using slot A    |

### Partition Layout (A/B dynamic partitions)

```
boot_a     → /dev/block/mmcblk0p17  ← currently in use
boot_b     → /dev/block/mmcblk0p18
vendor_boot_a → /dev/block/mmcblk0p24
vendor_boot_b → /dev/block/mmcblk0p25
vbmeta_a   → /dev/block/mmcblk0p78
vbmeta_b   → /dev/block/mmcblk0p79
dtbo_a     → /dev/block/mmcblk0p45
dtbo_b     → /dev/block/mmcblk0p46
```

### � Recommended Attack Path: Fastboot + Magisk

> **⚠️ Blocker**: the boot.img file is required and currently cannot be obtained!

**This is theoretically the most direct root method, but it is currently blocked:**

```bash
# Step summary (boot.img must be obtained first)
1. Obtain the stock boot.img (❌ currently impossible)
2. Patch boot.img with the Magisk app
3. adb reboot recovery → choose Enter Fastboot
4. fastboot flash boot_a magisk_patched_boot.img
5. fastboot reboot
```

**Why boot.img cannot be obtained:**

- `dd /dev/block/mmcblk0p17` → Permission denied
- `fastboot fetch` → not supported by the device
- No official OTA firmware or compatible TWRP can be found

For detailed steps see: [Rokid_Root_Guide_boot_img.md](./Rokid_Root_Guide_boot_img.md)

---

## 📊 Other Attack Vector Analysis (all failed)

### 1. CVE-2023-2163 BPF Verifier Vulnerability

| Item     | Status                    |
| -------- | ------------------------- |
| Vulnerability type | BPF Verifier branch-pruning bug |
| Target kernels | 4.15 - 6.4                |
| Current kernel | 5.10.209                  |
| **Status** | ❌ **Patched**             |

**Test result**: all 8 probes show the vulnerability has been patched. 5.10.209 includes the backported security fix.

### 2. CVE-2024-36971 + BPF Heap Spray

| Item         | Status                               |
| ------------ | ---------------------------------- |
| Vulnerability type     | Routing table UAF (Use-After-Free)        |
| Target slab    | ip_dst_cache (192 bytes)           |
| BPF availability   | ✅ unprivileged_bpf_disabled=0     |
| Heap Spray   | ✅ successfully sprayed 16384 192-byte objects |
| **UAF trigger** | ⚠️ not successfully triggered                      |

**Test result**:

- The BPF heap spray technique is viable
- Successfully created 64 BPF maps with 256 entries each
- About 3 MB of 192-byte structures sprayed into the kernel in total
- But triggering the UAF requires a real ICMP error or routing operation
- msgget() is unavailable in this Android kernel

### 3. KASLR Bypass Attempts

| Item           | Status                   |
| -------------- | ---------------------- |
| /proc/kallsyms | ❌ all addresses show as 0     |
| /proc/modules  | ❌ all show 0x00000000 |
| kptr_restrict  | 2 (strictest)             |
| BPF pointer leak   | ❌ blocked by the Verifier       |
| **Status**       | ❌ **cannot be bypassed**        |

### 4. Other Potential Vulnerabilities (5.10.x)

| CVE           | Type                  | Patch status           |
| ------------- | --------------------- | ------------------ |
| CVE-2022-2585 | POSIX CPU timer UAF   | Possibly patched         |
| CVE-2022-2586 | nft_object UAF        | Requires CAP_NET_ADMIN |
| CVE-2022-2588 | route4_filter UAF     | Requires CAP_NET_ADMIN |
| CVE-2023-0179 | nftables stack buffer overflow | Possibly patched         |
| CVE-2023-3390 | nftables UAF          | Requires CAP_NET_ADMIN |

**Note**: most network-subsystem vulnerabilities require CAP_NET_ADMIN, which the current shell user does not have.

### 5. CVE-2024-0582 (io_uring mmap UAF) - 🔴 New finding!

| Item            | Status                          |
| --------------- | ----------------------------- |
| Vulnerability type        | io_uring ring buffer mmap UAF |
| io_uring availability | ✅ **fully available!**              |
| mmap operations       | ✅ succeeded (SQ ring, CQ ring)    |
| features        | 0x7ff (full functionality)              |
| **Status**        | ⚠️ **possibly exploitable**             |

**Test result**:

- `io_uring_setup(64, &params)` succeeded
- SQ ring mmap succeeded, address 0x7b49244000
- The mmap region is still accessible after the fd is closed
- A complete exploit program must be built to verify the UAF

---

## 🔒 Security Configuration Summary

### Favorable Factors (exploitable)

| Configuration                      | Value         | Impact                 |
| ------------------------- | ---------- | -------------------- |
| Bootloader                | **Unlocked** | custom boot.img can be flashed    |
| unprivileged_bpf_disabled | **0**      | BPF maps can be created      |
| bpf_jit_enable            | **1**      | JIT compilation enabled         |
| sched_debug               | Readable       | task information can be leaked       |
| **io_uring**              | **Available**   | CVE-2024-0582 can be tested |

### Unfavorable Factors (hinder exploitation)

| Configuration                | Value            | Impact            |
| ------------------- | ------------- | --------------- |
| kptr_restrict       | **2**         | prevents address leaks    |
| SELinux             | **Enforcing** | restricts shell operations |
| seccomp             | Enabled          | filters system calls    |
| Current UID            | 2000 (shell)  | unprivileged user      |
| perf_event_paranoid | **3**         | perf fully disabled   |

---

## 📋 Conclusions and Recommendations

### Recommended Paths

```
🥇 First choice: Fastboot + Magisk
   - Bootloader is unlocked
   - Most reliable and most direct
   - No kernel exploit required
   - Fully recoverable

🥈 Second choice: keep testing CVE-2024-36971
   - The BPF heap spray has been proven viable
   - A UAF trigger method still has to be found
   - Further testing may require root

🥉 Third choice: scan for other 5.10.x vulnerabilities
   - Most require extra privileges
   - Lower success rate
```

### Immediate Actions

1. **Prepare the Fastboot environment**
   - Confirm the Qualcomm USB driver is installed on Windows
   - Download the latest Magisk APK

2. **Extract boot.img**
   - Enter Fastboot mode
   - Confirm the partitions with `fastboot getvar all`
   - Try `fastboot fetch boot_a boot_a.img` (if supported)

3. **Magisk patching**
   - Patch on a computer or another Android device
   - Produce magisk_patched_boot.img

4. **Flash and test**
   - First disable vbmeta verification
   - Flash the patched boot.img
   - Reboot and verify root

---

## 📁 Related Documents

- [Rokid_Root_Guide_boot_img.md](./Rokid_Root_Guide_boot_img.md) - detailed flashing guide
- [CVE-2024-36971_deep_analysis.md](./CVE-2024-36971_deep_analysis.md) - UAF vulnerability analysis
- [cve_2024_36971_bpf_spray.c](./cve_2024_36971_bpf_spray.c) - BPF Heap Spray tool
- [cve_2023_2163_probe.c](./cve_2023_2163_probe.c) - BPF Verifier probe tool

---

_Report generated: **2026-01** (originally written 2025-01; the text references a 20260109 build; corrected 2026-09-13)_
