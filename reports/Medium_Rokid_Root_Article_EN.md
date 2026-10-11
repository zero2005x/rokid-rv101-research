# Rooting the Rokid AR Glasses: A 22-Session Deep Dive into Android Security Research
> ⚠ **歷史文件（非現況）**：本文為當時的紀錄；專案其後已有重大變化（root 已取得、引擎改執行期探測、手機 UI 重做、i18n 193 key）。
> 現行狀態請看 `README.md`、`MANIFEST.md`、`docs/deploy.md`。


_From 200,000+ kernel exploit attempts to cracking OTA authentication, reverse-engineering a TrustZone proxy daemon, and navigating Qualcomm EDL — lessons in modern Android defense-in-depth_

---

## Disclaimer

This article documents **authorized security research** conducted on a personally owned device for academic and defensive purposes. All findings have been reported through responsible disclosure channels. No third-party systems, users, or data were affected. Vulnerability details are presented at a conceptual level to aid defenders and security practitioners — **no working exploit code or credentials are included**. Reproduction of these techniques on devices you do not own may violate applicable laws.

---

## Introduction

I recently acquired a pair of **Rokid AR Glasses** (model RV101), a wearable computing device running Android 12 on a Qualcomm SoC. As a security researcher focused on embedded Android systems, my goal was to evaluate the device's security posture through a full-scope assessment — from firmware extraction to kernel exploitation to application-layer analysis.

What followed was a **22-session research project** spanning kernel vulnerability research, OTA API reverse-engineering, vendor daemon reverse engineering, Qualcomm EDL exploration, and USB driver debugging. The project revealed both the remarkable effectiveness of modern Android's defense-in-depth architecture and several significant vulnerabilities that warrant vendor attention.

This article documents the methodology, findings, and lessons learned — emphasizing what worked, what didn't, and why.

---

## Device Specifications

| Specification        | Value                           |
| -------------------- | ------------------------------- |
| **Model**            | Rokid RG-glasses (RV101)        |
| **Android Version**  | 12 (API 32)                     |
| **Linux Kernel**     | 5.10.209-perf                   |
| **SoC**              | Qualcomm (SM6375/khaje series)  |
| **Architecture**     | AArch64 (ARM64 Cortex-A55 x4)   |
| **Security Patch**   | 2024-07-05                      |
| **Partition Scheme** | A/B Dynamic Partitions (eMMC)   |
| **SELinux**          | Enforcing                       |
| **Bootloader**       | Unlocked (orange verified boot) |

### The Paradox: Unlocked But Locked Down

The device reports as fully unlocked:

```
unlocked:yes
secure:no
verifiedbootstate:orange
```

On a typical Android device, this would mean straightforward rooting via Magisk. But Rokid had other plans — they stripped the bootloader's write capabilities while leaving the "unlocked" flag intact. A locked door with the key removed from the lock.

---

## Phase 1: Conventional Boot Image Extraction (Sessions 1–5)

### Every Standard Method Failed

I systematically attempted every documented method for extracting the boot partition:

| Method                | Result         | Reason                                   |
| --------------------- | -------------- | ---------------------------------------- |
| `adb shell dd`        | ❌ Denied      | Shell UID 2000 lacks block device access |
| `fastboot fetch`      | ❌ Unsupported | "Device does not support fetch command"  |
| `fastboot oem dump`   | ❌ Unknown cmd | 8+ OEM variants tested, all unknown      |
| Fastbootd (userspace) | ❌ Unavailable | Cannot enter fastbootd mode              |
| Recovery sideload     | ⚠️ USB issue   | Windows PID D001 driver incompatible     |
| Magisk Direct Install | ❌ Chicken-egg | Requires existing root access            |

The key discovery: **Rokid's bootloader accepts `download` commands** (data transfer at 111 MB/s) **but rejects all `write`/`flash`/`boot`/`erase` commands** — returning "unknown command." This is a deliberate OEM firmware lockdown that persists even with the bootloader reporting "unlocked."

### Fastboot Deep Probe

Through systematic command enumeration, I established what works and what doesn't:

**Working:** `getvar all`, `download` (data transfer), `reboot`, `reboot-recovery`
**Blocked:** `flash`, `boot`, `erase`, `oem unlock`, `flashing unlock`, `fetch`, `oem dump`, `oem readback`

This confirmed that the bootloader is functionally read-only despite its "unlocked" status.

---

## Phase 2: Kernel Vulnerability Research (Sessions 3–10)

### Attack Surface Analysis

The kernel configuration presented a mixed picture:

```
kernel.unprivileged_bpf_disabled = 0    # BPF available (unusual for Android 12)
kernel.kptr_restrict = 2                # Kernel pointers fully hidden
kernel.perf_event_paranoid = 3          # Perf events restricted
```

Unprivileged BPF access is noteworthy — most Android 12 devices disable this. It enables heap spray primitives but the device's compile-time hardening proved overwhelming.

### CVE Investigation Summary

| CVE            | Attack Vector               | Status      | Root Cause of Failure                  |
| -------------- | --------------------------- | ----------- | -------------------------------------- |
| CVE-2024-36971 | Route cache UAF             | ❌ Blocked  | Loopback DST never becomes obsolete    |
| CVE-2024-0582  | io_uring mmap UAF           | ❌ N/A      | PBUF_RING requires kernel 5.19+        |
| CVE-2023-2163  | BPF verifier range tracking | ❌ Patched  | Backported fix present in 5.10.209     |
| CVE-2024-0044  | run-as package installer    | ❌ Patched  | Security patch 2024-07-05 includes fix |
| Binder UAF     | Thread exit race condition  | ❌ Hardened | SLAB_FREELIST_HARDENED + CFI           |
| CVE-2024-38401 | KGSL SyncSource UAF         | ❌ Patched  | Fixed 18+ months prior                 |
| CVE-2024-56720 | SOCKMAP                     | ❌ N/A      | SOCKMAP not compiled into kernel       |

### Binder UAF Deep Dive

Inspired by CVE-2019-2215 (exploited by NSO Group's Pegasus), I developed 4 progressive exploit versions targeting the Binder driver's `binder_thread` lifecycle:

| Version | Strategy                         | Race Attempts | BPF Sprays | Result    |
| ------- | -------------------------------- | ------------- | ---------- | --------- |
| v2      | epoll + close() race             | 40,000        | 6.7M       | Stable ❌ |
| v3      | pthread_barrier + looper stress  | 50,000        | 46.6M      | Stable ❌ |
| v4      | CVE-2019-2215 iovec primitive    | 50,000        | 134M       | Stable ❌ |
| v5      | 5 combined modes (poll/dup/exit) | 60,000        | 15M        | Stable ❌ |

Kernel logs confirmed race conditions were being triggered:

```
binder: ERROR: BC_REGISTER_LOOPER called without request
binder: undelivered transaction 12345
```

But the driver's hardened state machine catches invalid states before memory corruption reaches exploitable conditions.

### Why Kernel Exploitation Failed: Compile-Time Hardening

| Protection                 | Effect                                         |
| -------------------------- | ---------------------------------------------- |
| **SLAB_FREELIST_HARDENED** | Randomized freelists + pointer mangling        |
| **CFI_CLANG**              | Control-Flow Integrity blocks vtable hijacking |
| **SHADOW_CALL_STACK**      | Return addresses protected in shadow stack     |
| **CONFIG_DEBUG_LIST**      | List corruption detected pre-exploitation      |
| **kptr_restrict=2**        | All kernel addresses zeroed in /proc/kallsyms  |

**Conclusion:** After 200,000+ race attempts and 200M+ BPF spray operations, kernel exploitation was abandoned. The combination of backported security patches and compile-time hardening makes memory corruption attacks impractical without a 0-day.

---

## Phase 3: Qualcomm GPU Driver Research (Sessions 8–11)

### CVE-2025-21479: Cross-FD GPU Memory Access

The Qualcomm KGSL GPU driver (`/dev/kgsl-3d0`, mode 0666 — world-readable/writable) contained a confirmed vulnerability:

**Finding:** Different file descriptors within the same process can access each other's GPU memory allocations through predictable mmap offset calculations. This violates the expected file descriptor isolation model.

**Impact:** Within a single process, an attacker can read/write GPU memory belonging to a different security context (different fd). This has implications for:

- GPU sandbox escape within a process
- Information disclosure (ML model weights, cryptographic buffers)
- GPU memory corruption

**Limitation:** GPU address spaces are **per-process isolated** on this device. Cross-process attacks (e.g., reading SurfaceFlinger frame buffers) are not possible, limiting the practical impact to in-process scenarios.

### CVE-2025-21480: GPU Micronode Authorization Bypass

All 32 GPU context flag combinations returned EINVAL. GPU commands with `context_id=0` were accepted but never executed. This CVE could not be triggered on this device.

---

## Phase 4: The OTA Breakthrough (Sessions 14–15)

After 14 sessions of dead ends, I pivoted to reverse-engineering the device's firmware update mechanism.

### Reverse-Engineering the OTA Authentication

By decompiling the `RokidOtaUpgrade.apk` system application, I identified the authentication flow in `CheckService.java`. The OTA API uses an MD5-based signature scheme where the signing parameters are derived from device properties accessible via `getprop`.

The authentication scheme concatenates several device-specific identifiers and a timestamp into a signing string, computes its MD5 hash, and sends it as part of the OTA check request. The API endpoint responds with firmware metadata and a download URL.

**Key Insight:** The credentials required for OTA authentication are stored as **system properties on the device itself**, readable by any ADB shell. This means any user with USB debugging access can download firmware updates — a design choice that trades security for update reliability.

### Firmware Acquisition

With the authentication mechanism understood:

1. **Firmware downloaded** — 893.6 MB OTA package, integrity verified
2. **Boot image extracted** — 96 MB `boot.img` from OTA's `payload.bin` using standard tools
3. **Magisk patched** — Magisk v30.6 successfully patched the boot image

The critical blocker — obtaining `boot.img` — was finally resolved through the OTA path rather than direct device extraction.

---

## Phase 5: The Flash Barrier (Sessions 15–18)

### Every Write Path Blocked

With the Magisk-patched boot image ready, I systematically attempted every flash method:

| Method                      | Result         | Details                                            |
| --------------------------- | -------------- | -------------------------------------------------- |
| `fastboot flash boot_a`     | ❌ Unknown cmd | Bootloader stripped flash command                  |
| `fastboot boot <image>`     | ❌ Unknown cmd | Boot-from-RAM also stripped                        |
| `fastboot erase boot_a`     | ❌ Unknown cmd | Erase command stripped                             |
| `fastboot oem unlock`       | ❌ Unknown cmd | OEM unlock stripped                                |
| `fastboot flashing unlock`  | ❌ Unknown cmd | Flashing unlock stripped                           |
| `adb shell dd` (normal)     | ❌ Permission  | Shell UID 2000 cannot write block devices          |
| Recovery sideload (Windows) | ❌ USB Error   | PID D001 → WinUSB Error 10 (6 driver combos tried) |

### The Windows USB Driver War

Recovery/Sideload mode on the Rokid glasses presents itself as USB VID:18D1 PID:D001. On Windows, this device consistently fails with **CM_PROB_FAILED_START (Error Code 10)** regardless of driver:

| Driver Approach      | Result              |
| -------------------- | ------------------- |
| Google USB Driver    | ❌ Error 10         |
| Zadig → WinUSB       | ❌ Error 10         |
| Zadig → libusbK      | ❌ No device        |
| Zadig → libusb-win32 | ❌ Error 10         |
| usbipd-win → WSL2    | ❌ vhci_hcd missing |

**Root Cause:** The issue isn't the driver — it's Windows' WinUSB service (`wudfrd`) failing during initialization with the device's non-standard Recovery USB descriptor. Linux handles this natively without issue.

**Solution Identified:** Use a Linux machine (Raspberry Pi 4) as the ADB host for Recovery sideload operations.

---

## Phase 6: GateServiced — Vendor Daemon Research (Sessions 17–22)

### Discovery: A Root-Privileged Network Daemon

During attack surface enumeration, I discovered `GateServiced` — a vendor daemon with extraordinary privileges:

| Property         | Value                       |
| ---------------- | --------------------------- |
| **UID**          | 0 (root)                    |
| **Capabilities** | ALL (000001ffffffffff)      |
| **SELinux**      | u:r:tee:s0                  |
| **Seccomp**      | Disabled                    |
| **Network**      | TCP 0.0.0.0:8341 (no auth)  |
| **Binary Size**  | 20,304 bytes (13 functions) |

A 20KB daemon running as root with ALL capabilities, listening on all interfaces with no authentication or encryption. From a security perspective, this is a high-value target.

### Reverse Engineering

Using custom Python disassembly tools (the binary was too small for Ghidra to analyze effectively), I mapped all 13 functions:

**Purpose:** GateServiced is a QSEE (Qualcomm Secure Execution Environment) TrustZone proxy. It accepts TCP connections, receives commands, and forwards them to Trusted Applications running in the secure world via the TZCom driver.

**Wire Protocol:** Simple 4-byte command ID followed by variable-length payload, max 20,480 bytes per frame. Three command types: LOAD (load a TA), UNLOAD (unload a TA), and EXEC (execute a TA command).

### Vulnerability Findings

#### 1. DMA Heap Buffer Overflow (Confirmed)

Sending a payload exceeding the DMA buffer allocation size (20,468+ bytes) reliably causes a 1–9 byte overflow, resulting in SIGSEGV. This is 100% reproducible and indicates a bounds-checking error in the EXEC command handler.

#### 2. NULL Pointer Dereference via LOAD/UNLOAD Race (Session 22 Correction)

**Initial Assessment (Session 21):** Classified as a heap Use-After-Free based on crash patterns.

**Corrected Assessment (Session 22):** Through detailed disassembly of the crash site and the UNLOAD cleanup path, I determined this is actually a **NULL pointer dereference on global variables** (`.data` segment), not a heap UAF.

The crash mechanism:

- LOAD stores a function pointer and handle in global variables
- UNLOAD zeroes these globals using `STP XZR, XZR` (store zero pair)
- EXEC reads the function pointer from the same globals
- If UNLOAD runs between LOAD and EXEC, the function pointer is NULL
- The indirect branch instruction dereferences this NULL pointer

**Evidence:** All 6 crash dumps show the target register as 0x0 — consistent with NULL dereference, not a freed heap pointer (which would show a non-zero dangling address).

**Exploitation Assessment:** Not exploitable for code execution. Android 12's `mmap_min_addr` prevents mapping page 0, so the NULL dereference cannot be redirected to attacker-controlled memory.

#### 3. ASLR Information Leakage via Crash Dumps

Each GateServiced crash produces a tombstone containing full register dumps, stack traces, and memory maps. From 6 crashes, complete ASLR layouts were recovered including the binary base, libc base, stack range, and DMA heap range.

#### 4. No Authentication / No Encryption

GateServiced accepts connections from any local process (or any device on the same network) without authentication. All TrustZone commands and responses travel in plaintext.

### CVSS Assessment

| Finding                       | CVSS 4.0 | Impact                                     |
| ----------------------------- | -------- | ------------------------------------------ |
| Unauthenticated QSEE access   | 9.8      | Any local process can invoke TrustZone TAs |
| DMA heap overflow             | 8.6      | Reliable crash, potential code execution   |
| No TLS on TA commands         | 7.5      | MitM can intercept/modify TA traffic       |
| ASLR leak via crash dumps     | 6.8      | Full address space layout disclosure       |
| NULL deref (LOAD/UNLOAD race) | 5.3      | DoS only, not exploitable for RCE          |
| Hardcoded TA path             | 4.3      | Predictable filesystem access pattern      |

---

## Phase 7: Qualcomm EDL and Final Reconnaissance (Session 22)

### EDL 9008 Mode

The device can enter Qualcomm Emergency Download mode, presenting as a QDLoader 9008 device. However, without the device-specific firehose programmer (`.mbn` file — typically OEM-restricted), EDL cannot be used for firmware operations.

I developed a custom Sahara protocol tool to recover the device from EDL mode after it became stuck — a useful recovery technique when devices enter unresponsive EDL states.

### Debuggable Applications

Five system applications were found with the `android:debuggable=true` flag, allowing `run-as` access with the `runas_app` SELinux context. While these provide app-level sandbox access, they do not directly enable privilege escalation.

### Additional Attack Surface

| Component                  | Access     | Finding                                       |
| -------------------------- | ---------- | --------------------------------------------- |
| I2C devices (`/dev/i2c-1`) | Read/Write | PMIC, display controller, RTC, battery IC     |
| SPI (`/dev/rt600_spidev`)  | Read/Write | External flash interface (responses zeroed)   |
| Serial (`/dev/ttyHS0`)     | Write      | High-speed serial, possibly co-processor link |
| Qualcomm Diag              | Available  | Diagnostic interface enabled alongside ADB    |
| ADSPRPC                    | Read-only  | DSP RPC interface, ioctl denied               |
| QSEECOM                    | Denied     | Direct TrustZone driver access blocked        |

---

## Application Security Findings

### TrustZoneAccessService: Exported Without Permission Checks

The `TrustZoneAccessService` app exposes AIDL interfaces to all applications with no permission verification. Any installed app can invoke TrustZone Trusted Applications (device attestation, license management, token operations) via Mink Socket IPC. This represents a significant privilege boundary violation.

### OTA Credentials Accessible

The OTA authentication credentials are stored as readable system properties, meaning any process with ADB shell access can authenticate to the OTA API and download firmware. While this simplifies updates, it expands the attack surface.

---

## Current Status

| Milestone                   | Status                                     |
| --------------------------- | ------------------------------------------ |
| Boot image extraction       | ✅ Via OTA firmware download               |
| Magisk patching             | ✅ v30.6 patched boot image ready          |
| Fastboot flash              | ❌ All write commands stripped             |
| Recovery sideload (Windows) | ❌ USB PID D001 Error 10                   |
| Recovery sideload (Linux)   | 🔄 Pending — requires Linux host           |
| Kernel exploitation         | ❌ Abandoned — hardening too comprehensive |
| GateServiced RCE            | ❌ NULL deref, not exploitable             |
| EDL flash                   | ❌ No firehose programmer available        |
| Root achieved               | ❌ Not yet                                 |

### Remaining Path

The most promising remaining approach is using a **Linux machine** as the ADB host for Recovery-mode sideloading. Linux's USB stack handles the non-standard Recovery USB interface natively, bypassing the Windows driver incompatibility entirely.

---

## Key Takeaways

### 1. Unlocked Bootloader ≠ Full Access

Rokid demonstrates an unusual security model: the bootloader reports "unlocked" but has write commands surgically removed. This is more restrictive than a locked bootloader on many other devices, where `fastboot flashing unlock` at least provides a path.

### 2. Defense in Depth Actually Works

The device implements 7 distinct security layers, each blocking a different attack category:

| Layer | Control                      | Blocks                             |
| ----- | ---------------------------- | ---------------------------------- |
| 1     | Block device permissions     | Boot.img extraction via dd         |
| 2     | CVE backports in 5.10.209    | Known kernel vulnerabilities       |
| 3     | SLAB_FREELIST_HARDENED       | Heap-based memory corruption       |
| 4     | CFI + Shadow Call Stack      | Code execution after corruption    |
| 5     | kptr_restrict=2              | Kernel address information leakage |
| 6     | Bootloader command stripping | Firmware modification via fastboot |
| 7     | USB descriptor compatibility | Recovery sideload from Windows     |

Bypassing the device required going **around** all layers (OTA API path) rather than through any single one.

### 3. OTA APIs as an Overlooked Attack Surface

When firmware is delivered over-the-air, the authentication mechanism becomes a potential bypass for all on-device security controls. If an attacker can download firmware, they can extract and modify boot images offline.

### 4. Vendor Daemons Deserve Scrutiny

GateServiced demonstrates how a small vendor daemon (20KB, 13 functions) can introduce significant risk when running with excessive privileges. A root-privileged, unauthenticated network service that proxies TrustZone commands is a high-value target.

### 5. Correct Vulnerability Classification Matters

The Session 21→22 reclassification from "heap UAF" to "NULL pointer dereference" completely changed the exploitation assessment — from critical RCE primitive to DoS-only. This underscores the importance of disassembly-level verification over behavioral inference.

### 6. Have a Linux Machine Ready

I spent more time fighting Windows USB drivers than writing kernel exploits. For Android security research, a dedicated Linux environment eliminates an entire category of tooling problems.

---

## The Numbers

| Metric                            | Value              |
| --------------------------------- | ------------------ |
| Total research sessions           | 22                 |
| Calendar days                     | 12 (Jan 31–Feb 11) |
| Boot.img extraction methods tried | 6+                 |
| Fastboot OEM commands tested      | 15+                |
| Kernel CVEs investigated          | 8+                 |
| Binder UAF exploit versions       | 4                  |
| KGSL GPU exploit versions         | 9                  |
| Total race condition attempts     | 200,000+           |
| BPF heap spray operations         | 200,000,000+       |
| Windows USB driver approaches     | 6                  |
| GateServiced functions mapped     | 13                 |
| GateServiced crash dumps analyzed | 6                  |
| Custom tools developed            | 15+                |
| Firmware downloaded               | 893.6 MB ✅        |
| Boot.img extracted                | 96 MB ✅           |
| Magisk patched                    | ✅                 |
| Root achieved                     | 🔄 Pending         |

---

## Responsible Disclosure

The following findings have been documented for responsible disclosure to the respective vendors:

1. GateServiced unauthenticated QSEE access (CVSS 9.8)
2. GateServiced DMA heap overflow (CVSS 8.6)
3. CVE-2025-21479: KGSL cross-FD GPU memory access
4. TrustZoneAccessService exported without permissions
5. OTA credentials stored in readable system properties

---

## Conclusion

The Rokid AR Glasses project demonstrates that modern Android's security architecture is genuinely effective against software-only attacks, even on a device with an unlocked bootloader and BPF available to unprivileged users. The kernel hardening stack (CFI, Shadow Call Stack, SLAB hardening, kptr_restrict) successfully blocked every exploitation attempt.

However, the research also revealed that security is only as strong as its weakest layer. The OTA authentication mechanism — designed for reliability rather than security — provided the bypass that 200,000+ kernel exploit attempts could not.

The most important takeaway: **security research is 90% dead ends and 10% breakthroughs.** Each failed attempt revealed something about the device's security architecture, and the cumulative knowledge now points to a clear resolution path.

**Update coming with the final result.**

---

_This research was conducted on a personally owned device for educational and defensive security purposes. The author advocates for responsible disclosure and does not endorse unauthorized access to systems or devices._

---

**Tags:** #Android #Security #ReverseEngineering #Magisk #Qualcomm #ARGlasses #IoT #KernelSecurity #TrustZone #Bootloader #DefenseInDepth #ResponsibleDisclosure

---

_Last Updated: February 11, 2026_
