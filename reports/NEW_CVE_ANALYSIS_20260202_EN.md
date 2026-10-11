# 🆕 Newly Discovered CVE Feasibility Analysis Report

> 中文版：[NEW_CVE_ANALYSIS_20260202.md](NEW_CVE_ANALYSIS_20260202.md)

> ⚠ **2026-09-13 correction**: the **Fastboot / Magisk / flashing boot.img route assumed in this document is infeasible** (boot.img cannot be obtained, no Magisk, no persistent root); **root has already been obtained at runtime via the GhostLock LPE (CVE-2026-43499)**.


**Date**: 2026-02-02  
**Device**: Rokid AR Glasses (Kernel 5.10.209, Android 12, Qualcomm neo)  
**Security patch**: 2024-07-05

---

## 📊 Executive Summary

Based on the latest system configuration checks, the following is the feasibility assessment for each CVE:

| CVE                | Vulnerability type        | Feasibility | Reason                            |
| ------------------ | ------------------------- | ----------- | --------------------------------- |
| **CVE-2024-49848** | ADSPRPC UAF               | ⭐⭐⭐ Medium | device affected but privileges insufficient |
| **CVE-2024-56720** | BPF sockmap UAF           | ❌ Infeasible | `CONFIG_BPF_STREAM_PARSER` not enabled |
| **CVE-2025-21655** | io_uring eventfd UAF      | ❌ Infeasible | only affects Linux 6.1+           |
| **CVE-2024-1086**  | nf_tables UAF             | ❌ Infeasible | `CONFIG_NF_TABLES` not enabled    |
| **CVE-2024-26581** | nft_set_rbtree OOB        | ❌ Infeasible | `CONFIG_NF_TABLES` not enabled    |
| **CVE-2024-53197** | USB Audio driver          | ⭐⭐ Low     | requires special USB hardware     |
| **CVE-2023-20938** | Binder UAF                | ⭐⭐ Low     | requires further research         |
| **CVE-2024-43047** | ADSPRPC Map collision UAF | ⭐⭐⭐⭐ High | **ITW exploitation confirmed**    |

---

## 🔴 Key Configuration Check Results

### 1. nf_tables (CVE-2024-1086, CVE-2024-26581)

```
# CONFIG_NF_TABLES is not set   ❌ unavailable
CONFIG_NETFILTER=y              ✅ Netfilter available
CONFIG_NETFILTER_XTABLES=y      ✅ iptables available
```

**Conclusion**: nf_tables is not compiled into the kernel at all; CVE-2024-1086 and CVE-2024-26581 are **not exploitable**.

### 2. BPF sockmap (CVE-2024-56720)

```
CONFIG_BPF_SYSCALL=y            ✅ BPF syscall available
CONFIG_CGROUP_BPF=y             ✅ cgroup BPF available
CONFIG_BPF_JIT=y                ✅ BPF JIT enabled
# CONFIG_BPF_STREAM_PARSER is not set  ❌ Stream parser not enabled
```

**Conclusion**: `BPF_STREAM_PARSER` is a prerequisite for the sockmap feature; not being enabled means CVE-2024-56720 is **not exploitable**.

### 3. io_uring eventfd (CVE-2025-21655)

```
CONFIG_IO_URING=y               ✅ io_uring available
CONFIG_EVENTFD=y                ✅ eventfd available
```

**However**: CVE-2025-21655 only affects **Linux 6.1+**; our device is **5.10.209**, so the version is not affected.

**Conclusion**: CVE-2025-21655 is **not exploitable** (the version is outside the affected range).

### 4. ADSPRPC (CVE-2024-49848, CVE-2024-43047)

```
/dev/adsprpc-smd:
  permissions: crw-rw-r-- (664)
  Owner: system:system
  Major: 489

Shell user groups:
  shell input log adb sdcard_rw sdcard_r ext_data_rw ext_obb_rw
  net_bt_admin net_bt inet net_bw_stats readproc uhid readtracefs
```

**Analysis**:

- The device node exists with permissions 664 (`rw-rw-r--`)
- Both Owner and Group are `system`
- The shell user (uid=2000) is **not in the system group**
- A direct `open()` will fail

**However**: according to Google Project Zero's report, the following methods exist for obtaining access:

> "untrusted_app does not have the ability to directly open the driver device file, but at least on some devices it can obtain limited access by **receiving a file descriptor** to the device file from the **dspservice** process through the **IDspService HAL interface**, which is reachable through **hwbinder**."

**Conclusion**: the HWBinder access path requires further research. This is a **high-priority** research target.

### 5. USB Audio (CVE-2024-53197)

```
CONFIG_SND_USB=y                 ✅ USB Audio available
CONFIG_SND_USB_AUDIO=y           ✅ USB Audio driver enabled
```

**Exploitation conditions**:

- A specially crafted USB Audio device must be connected
- Or a USB device must be emulated (requires USB OTG support)

**Conclusion**: theoretically feasible, but practically complex to carry out.

---

## 🎯 Highest Priority: CVE-2024-43047 / CVE-2024-49848 (ADSPRPC)

### Vulnerability Background

This is an **ITW (In-The-Wild) vulnerability confirmed by Google Project Zero**, used by the NoviSpy spyware.

### Vulnerability Details

#### CVE-2024-43047: Map Collision UAF

- **Type**: Use-After-Free
- **Trigger condition**: create overlapping fastrpc_mmap structures
- **Exploitation difficulty**: medium
- **Affected versions**: Linux 4.x kernels and some 5.x configurations

#### CVE-2024-49848: FASTRPC_ATTR_KEEP_MAP UAF

- **Type**: Use-After-Free race condition
- **Trigger condition**: a special flags/attrs combination + a race
- **Exploitation difficulty**: medium-high
- **In-the-wild status**: exploited ITW

### Exploitation Strategy (from the Project Zero analysis)

1. **Heap Spray**: use `inotify_event_info` objects for heap spraying
2. **UAF trigger**: trigger through map collision or a KEEP_MAP logic error
3. **Object control**: use pipe buffers to reclaim the freed memory
4. **Arbitrary read/write**: obtain arbitrary kernel memory read/write capability

### Research Directions

1. **Confirm HWBinder access**:

```bash
# check dspservice
adb shell "service list | grep -i dsp"
adb shell "dumpsys -l | grep -i dsp"

# check HWBinder services
adb shell "lshal | grep -i adsp"
```

2. **Check the SELinux policy**:

```bash
adb shell "sesearch --allow -s shell -t adsprpc_device_device"
```

3. **Test HAL access**:
   - C++ code must be developed to obtain the fd through HWBinder

---

## 📋 Next Action Plan

### Priority 1: ADSPRPC access research

1. Check the `dspservice` and `IDspService` HALs
2. Research the HWBinder access path
3. Develop a CVE-2024-43047 PoC

### Priority 2: USB Audio attack (if OTG is present)

1. Confirm whether the device supports USB OTG
2. Research the exploitation details of CVE-2024-53197

### Priority 3: Continue looking for boot.img

1. TWRP is still the most reliable path
2. Official firmware download

---

## ⚠️ Important Findings

### Why CVE-2024-49848 is the most promising path (**Note: the table in this same document rates CVE-2024-43047 ⭐⭐⭐⭐ higher; the heading and the table are inconsistent — note added 2026-09-13**)

1. **ITW exploitation confirmed**: Google Project Zero confirmed use by real attackers
2. **Device node present**: `/dev/adsprpc-smd` exists on the system
3. **Qualcomm device**: our device is a Qualcomm SoC
4. **Kernel version matches**: 5.10.209 is within the affected range
5. **Detailed technical analysis available**: Project Zero provided a complete vulnerability analysis

### Main Challenges

1. **Permission problem**: the device cannot be opened directly; HWBinder is required
2. **Race condition**: the vulnerability requires precise timing control
3. **Heap Spray**: a suitable spray object must be found
4. **KASLR**: an information leak is needed to bypass KASLR

---

## 📚 Reference Resources

1. [Project Zero: The Qualcomm DSP Driver](https://projectzero.google/2024/12/qualcomm-dsp-driver-unexpectedly-excavating-exploit.html)
2. [Amnesty: NoviSpy Analysis](https://securitylab.amnesty.org/latest/2024/12/a-digital-prison-surveillance-and-the-suppression-of-civil-society-in-serbia/)
3. [Qualcomm Security Bulletin](https://docs.qualcomm.com/product/publicresources/securitybulletin/october-2024-bulletin.html)

---

_End of report - 2026-02-02_
