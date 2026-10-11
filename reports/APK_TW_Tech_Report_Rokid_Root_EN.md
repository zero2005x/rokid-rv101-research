# [Tech Sharing] Rokid AR Glasses Root Guide - A Deep Dive into the Obstacles Even with an Unlocked Bootloader

> 中文版：[APK_TW_Tech_Report_Rokid_Root.md](APK_TW_Tech_Report_Rokid_Root.md)

> ⚠ **2026-09-13 status correction (especially important for external documents)**: at the time this document was written **root had not yet been obtained**; **since 2026-08-22 runtime root has been obtained via the GhostLock LPE (CVE-2026-43499)** (`uid=0(root)`, `context=u:r:kernel:s0`, **SELinux Permissive**). The "SELinux Enforcing / Root Access: None" in the text represents only the state at that time; for the current flow see `docs/deploy.md` (armed once per boot via adb).


> **Author**: <USER>  
> **Publish date**: 2026-01-31  
> **Applicable device**: Rokid AR Glasses (RG-glasses / RV101)  
> **Difficulty**: ★★★★☆ (advanced)  
> **Research status**: ~~in progress - seeking community help~~ → **resolved: runtime root obtained on 2026-08-22 via the GhostLock LPE (CVE-2026-43499)** (this article is retained as a 2026-01/02 record)

---

## 📱 Introduction

Hello everyone!

I recently got a pair of **Rokid AR glasses** and wanted to root them so I could install custom apps and adjust system settings. After several days of deep research, I found that although this device's **bootloader is already unlocked**, obtaining root privileges is not as simple as it seemed.

This article shares my research process, the difficulties I ran into, and the possible solutions found so far. I hope it helps friends who are also interested in this device, and I welcome suggestions from those with experience!

---

## 🔧 Device Specifications

| Item             | Specification                                                       |
| ---------------- | ---------------------------------------------------------- |
| **Model**         | Rokid RG-glasses (RV101)                                   |
| **Android version** | 12 (API 32)                                                |
| **Linux kernel**   | 5.10.209-perf-gb80a8319d11b                                |
| **Processor**       | Qualcomm neo (SM6375 series)                                 |
| **Architecture**       | AArch64 (ARM64 Cortex-A55 x4)                              |
| **Security patch**     | 2024-07-05                                                 |
| **Build number**     | Rokid/glasses/glasses:12/SKQ1.240613.001/1.12.009-20260109 |
| **Partition scheme**     | A/B dynamic partitions (boot_a / boot_b)                             |

### 🎉 Good news: the bootloader is unlocked!

```
ro.boot.flash.locked = 0           # unlocked
ro.boot.verifiedbootstate = orange # orange state = custom system can be flashed
ro.boot.vbmeta.device_state = unlocked
```

This means we can flash a patched boot.img directly with `fastboot flash boot`!

---

## 🛠️ My Root Strategy

Since the bootloader is unlocked, the most direct approach is:

```
1. Extract the stock boot.img
2. Patch boot.img with Magisk
3. Flash the patched boot.img with Fastboot
4. Reboot = root achieved!
```

Sounds simple, right? But the problem is in **step one**... 😢

---

## 🚧 Difficulties Encountered

### The core problem: boot.img cannot be extracted

I tried all of the following methods, and all failed:

#### ❌ Method 1: ADB shell dd extraction

```bash
adb shell "dd if=/dev/block/by-name/boot_a of=/sdcard/boot.img"
```

**Result**: `Permission denied` - the shell user has no permission to read the block device

#### ❌ Method 2: Fastboot fetch command

```bash
fastboot fetch boot_a boot.img
```

**Result**: `Device does not support fetch command`

This device's bootloader is too basic and does not support the fetch command.

#### ❌ Method 3: Fastboot OEM commands

Tried various OEM commands:

| Command                                 | Result               |
| ------------------------------------ | ------------------ |
| `fastboot oem dump boot_a`           | ❌ unknown command |
| `fastboot oem read-partition boot_a` | ❌ unknown command |
| `fastboot oem readback boot_a`       | ❌ unknown command |
| `fastboot oem sha1sum boot_a`        | ❌ unknown command |
| `fastboot oem reboot-edl`            | ❌ unknown command |

The Qualcomm basic bootloader has no extended commands at all.

#### ❌ Method 4: Recovery Sideload

Created a sideload zip to run dd commands in Recovery:

```bash
adb reboot recovery
# select "Apply update from ADB"
adb sideload extract_boot.zip
```

**Result**: the device got stuck in sideload mode!

The reason is that sideload mode uses a different USB PID (D001), and Windows misidentifies it as a COM port, causing driver problems. Long-pressing the power button for 30 seconds could not force a reboot either.

> ⚠️ **Warning**: do not casually try sideload; it is very easy to get stuck!

**Solution**: uninstall the wrong driver in Device Manager (remember to check "delete the driver software"), then unplug and replug the USB.

#### ❌ Method 5: Entering fastbootd (Userspace Fastboot)

```bash
fastboot reboot fastboot
```

**Result**: `Failed to boot into userspace fastboot`

The device cannot enter fastbootd mode, so GKI's modern features are unavailable.

#### ❌ Method 6: Magisk Direct Install

Installed Magisk v30.6 and used scrcpy to operate the glasses UI:

```bash
adb install Magisk-v30.6.apk
scrcpy --window-title "Rokid Glasses"
```

**Result**: Magisk only shows the "Select and Patch a File" option; "Direct Install" requires root privileges already.

This is a chicken-and-egg problem... you need root to root.

---

## ✅ What Worked

### Entering Fastboot mode

After many attempts, I found the correct way to enter it:

```bash
# Step 1: enter Recovery
adb reboot recovery

# Step 2: operate on the glasses screen
# - Any button cycles highlight
# - select "Enter fastboot"
# - Long-press activates

# Step 3: verify the connection
fastboot devices
# output: 33115a5c         fastboot
```

### Fastboot device information

```
(bootloader) max-download-size: 805306368
(bootloader) current-slot:a
(bootloader) slot-count:2
(bootloader) unlocked:yes            # ← unlocked!
(bootloader) secure:no               # ← secure boot off!
(bootloader) hw-revision:10000
(bootloader) variant:SSG EMMC
(bootloader) off-mode-charge:0
(bootloader) battery-soc-ok:yes
```

### boot_a partition information

```
partition-size:boot_a: 0x6000000   # 96MB
partition-type:boot_a: raw
slot-retry-count:a: 6
slot-unbootable:a: no
slot-successful:a: yes
```

---

## 🔬 Extra Research: Kernel Vulnerability Exploration

Since the proper methods did not work, I also tried kernel vulnerabilities:

### CVE-2024-36971 (Route Cache UAF)

This UAF vulnerability theoretically affects 5.10.x kernels, but actual testing found:

- ✅ BPF Heap Spray succeeded (unprivileged_bpf_disabled=0)
- ❌ the UAF could not be triggered - it needs a real network interface and ICMP errors
- ❌ the device has no WiFi connection, only loopback

### CVE-2024-0582 (io_uring mmap UAF)

```
Result: feature does not exist
Reason: PBUF_RING is a feature only available in Linux 5.19+
```

### CVE-2023-2163 (BPF Verifier)

```
Result: patched
Reason: 5.10.209 includes the backported security fix
```

### KASLR bypass

```
kptr_restrict = 2 (strictest)
/proc/kallsyms all addresses are 0
/proc/modules all addresses are 0
~~Conclusion: cannot be bypassed~~ → **2026-09-13: KASLR can now be leaked (kernelsnitch mm leak), leak→root takes about 86–155 seconds**
```

---

## 📋 USB ID Reference Table

This is important! Different modes use different USB PIDs:

| Mode       | VID  | PID  | Note                   |
| ---------- | ---- | ---- | ---------------------- |
| ADB (normal) | 18D1 | 4EE7 | normal Android system      |
| Fastboot   | 18D1 | D00D | bootloader mode        |
| Sideload   | 18D1 | D001 | Recovery sideload mode |

If you hit driver problems, you can use **Zadig** to force-install the WinUSB driver.

---

## 🎯 Currently Feasible Options

### Option A: fastboot boot a temporary TWRP ⭐recommended

```bash
fastboot boot twrp_sm6375.img
# run inside TWRP
dd if=/dev/block/by-name/boot_a of=/sdcard/boot.img
adb pull /sdcard/boot.img
```

**Problem**: a TWRP compatible with Qualcomm SM6375/khaje needs to be found

### Option B: Obtain the official Rokid firmware

If the official firmware package can be obtained (usually `.zip` or `.tar`), `payload-dumper` can extract boot.img from it

**Channels**:

- Rokid customer service
- XDA/4PDA/Coolapk
- Telegram groups
- Intercepting OTA updates

### Option C: Hardware EDL mode

Qualcomm Emergency Download Mode:

1. Disassemble the device to find the test points
2. Short the EDL test points
3. Use the QFIL tool to read the partitions

**Risk**: requires disassembly and may void the warranty

---

## 🤝 Seeking Help

If you have any of the following resources, feel free to message me:

1. **Official Rokid AR Glasses firmware** (any version)
2. **A TWRP recovery compatible with SM6375/khaje**
3. **An EDL firehose programmer for the Rokid device**
4. **Other root experience to share**

---

## 📦 Tool Preparation Checklist

For friends who want to try:

1. **ADB & Fastboot** (version >= 34.0.0)
   - [Android SDK Platform-Tools](https://developer.android.com/studio/releases/platform-tools)

2. **Magisk** (v26.x+ recommended)
   - [Magisk Releases](https://github.com/topjohnwu/Magisk/releases)

3. **Zadig** (USB driver installation)
   - [Zadig](https://zadig.akeo.ie/)

4. **scrcpy** (mirror the glasses screen)
   - [scrcpy Releases](https://github.com/Genymobile/scrcpy/releases)

---

## 📊 Research Summary

| Method                  | Status | Note              |
| --------------------- | ---- | ----------------- |
| ADB shell dd          | ❌   | Permission denied |
| fastboot fetch        | ❌   | command not supported      |
| fastboot oem dump     | ❌   | unknown command   |
| Recovery sideload     | ⚠️   | serious driver problems      |
| fastbootd             | ❌   | cannot enter          |
| Magisk Direct Install | ❌   | requires existing root       |
| CVE-2024-36971        | ❌   | requires a real network        |
| CVE-2024-0582         | ❌   | feature does not exist        |
| fastboot boot TWRP    | 🔄   | needs a compatible image    |
| Official firmware              | 🔄   | needs to be obtained          |

---

## 🔐 Device Security Configuration Reference

For friends interested in research, this is the device's security configuration:

```bash
# security-related
ro.secure = 1
ro.debuggable = 0
ro.adb.secure = 1
kernel.kptr_restrict = 2
kernel.perf_event_paranoid = 3
kernel.unprivileged_bpf_disabled = 0  # BPF available!

# SELinux
getenforce = Enforcing

# partitions
boot_a → /dev/block/mmcblk0p17 (currently in use)
boot_b → /dev/block/mmcblk0p18
vendor_boot_a → /dev/block/mmcblk0p24
vbmeta_a → /dev/block/mmcblk0p78
```

---

## 💬 Closing Remarks

The root path for this device is much harder than expected. Although the unlocked bootloader is a huge advantage, without a way to extract boot.img, Magisk cannot be used.

The most promising options at the moment are finding a compatible TWRP or the official firmware.

I will update this if there is any progress, and everyone is welcome to share their experience!

---

## 📝 Changelog

- **2026-02-02**: added the GateServiced protocol reverse-engineering analysis
- **2026-01-31**: first version published

---

## 🔓 GateServiced Protocol Reverse-Engineering Analysis (added 2026-02-02)

### Key Findings

After in-depth analysis, the GateServiced service was found to have the following characteristics:

| Item                | Value                          |
| ------------------- | --------------------------- |
| **Process PID**        | 1787                        |
| **UID/GID**         | 0 (root) / 0 (root)         |
| **Listening port**          | TCP 8341 (0.0.0.0)          |
| **SELinux Context** | u:r:tee:s0                  |
| **Capabilities**    | 000001ffffffffff (almost all) |

### Protocol Format

According to logcat analysis, GateServiced uses the following protocol format:

```
+--------+--------+------------+
| cmd_id | rsv_len| payload    |
| 4 bytes| 4 bytes| variable   |
+--------+--------+------------+
```

- **cmd_id**: the first 4 bytes are used directly as the command ID (little-endian ASCII)
- **rsv_len**: the complete data length
- **input_len**: rsv_len - 4

### Log Analysis Example

```
GateServiced: [GATE CA][INFO][handle_client][53]:"Received, cmd_id: 6c6c6568, rsv_len:5"
GateServiced: [GATE CA][INFO][run_exec_cmd_ta][231]:"cmd_id = 6c6c6568, input_len = 1, output_len = 20480"
GateServiced: [GATE CA][ERRO][run_exec_cmd_ta][234]:"TA is not already loaded."
GateServiced: [GATE CA][ERRO][handle_client][59]:"cmd_dispatch failed, ret:300c"
```

Parsing: `cmd_id: 6c6c6568` = "lleh" (little-endian "hell" from "hello")

### Problem

**"TA is not already loaded"** - GateServiced expects a TrustZone Trusted Application to already be loaded, but currently:

- the `ssgqmigd` service does not exist
- the `qseeproxydaemon` service does not exist

This indicates that the TrustZone-related services may not be fully configured.

### Test Script

The shell test script run on the device:

```bash
# location: /data/local/tmp/gate_exploit.sh
# usage:
/data/local/tmp/gate_exploit.sh -admin    # test admin commands
/data/local/tmp/gate_exploit.sh -binary   # test binary command IDs
/data/local/tmp/gate_exploit.sh -all      # run all tests
```

### Attack Vector Assessment

| Vector        | Status | Note                    |
| ----------- | ---- | ----------------------- |
| Command injection    | ❌   | not found                  |
| Path traversal    | ❌   | not found                  |
| Format string  | ❌   | not found                  |
| Buffer overflow  | ⚠️   | further testing needed            |
| TA load bypass | 🔄   | the QSEE HIDL interface needs to be studied |

### Next Steps

1. **Study the QSEE HIDL service**: `vendor.qti.hardware.qseecom@1.0::IQSEECom/default`
2. **TZAccessService exploitation**: exported=true, no permission restriction
3. **Frida dynamic Hook**: requires root or SELinux permissive

---

## 🔬 Research Log Update (2026-02-02)

### 1. In-depth GateServiced protocol analysis

After detailed testing, the protocol format was confirmed:

```
[cmd_id:4B little-endian ASCII][rsv_len:4B][payload:variable]
```

Command identifiers tested:
| ASCII String | Hex cmd_id | Result |
|-------------|------------|------|
| ping | 676e6970 | ret:300c |
| stat | 74617473 | ret:300c |
| info | 6f666e69 | ret:300c |
| vers | 73726576 | ret:300c |
| GATE | 45544147 | ret:300c |
| help | 706c6568 | ret:300c |

**Core problem**: all commands returned error code `300c` = "TA is not already loaded"

### 2. QSEE subsystem access analysis

```bash
# QSEE kernel module
qseecom 1003520 - Live 0x0000000000000000 (O)

# QSEECOM version
cat /sys/bus/platform/drivers/qseecom/version
# Output: 0x1402000

# qseecomd process
# PID 553-554, u:r:tee:s0 context

# HIDL service
vendor.qti.hardware.qseecom@1.0::IQSEECom/default
```

**Socket permission restriction**:

```
/dev/socket/notify-topology: srw-rw---- system drmrpc
```

- the `drmrpc` group permission is required (GID 1019)
- the shell user only has: shell, input, log, adb, sdcard_rw, sdcard_r, inet

### 3. TrustZoneAccessService research

APK location: `/vendor/app/TrustZoneAccessService/`
Package: `com.qualcomm.qti.qms.service.trustzoneaccess`

**AndroidManifest analysis**:

- `exported="true"` - can be invoked from any app
- no permission requirement - a potential attack surface

**Test result**:

```bash
am start-foreground-service -n com.qualcomm.qti.qms.service.trustzoneaccess/...
# result: ForegroundServiceDidNotStartInTimeException
```

The service requires a foreground notification but none was provided; a self-built APK is needed to bind it correctly.

### 4. Magisk discovery

```bash
pm list packages | grep magisk
# package:com.topjohnwu.magisk

dumpsys package com.topjohnwu.magisk | grep version
# versionCode=30600 versionName=30.6
```

**Status**: the App is installed but not activated (no root binary; possibly a leftover installation)

### 5. Attack vector priority update

| Priority | Vector                 | Feasibility | Obstacle                       |
| ------ | -------------------- | ------ | -------------------------- |
| 🔴 1   | GateServiced TA loading | high potential | a load trigger needs to be found           |
| 🟡 2   | TZAccessService binding | medium   | requires APK development              |
| 🟡 3   | qmcs partition injection        | unknown   | graphics_composer needs to be analyzed |
| 🟢 4   | DRM service exploitation         | low     | more research needed               |

### 6. Deployed test scripts

```
/data/local/tmp/
├── gate_exploit.sh    # basic protocol test
├── gate_adv.sh        # advanced TA load test
├── gate_rce.sh        # RCE vector test
├── tz_call.sh         # TZ service invocation
├── priv_enum.sh       # permission enumeration
└── ta_monitor.sh      # TA load monitoring
```

---

**#Rokid #ARGlasses #Root #Magisk #Fastboot #Android12 #TechSharing**

---

> 💡 **Disclaimer**: this article is for technical research and learning purposes only. Rooting a device may void the warranty; please assess the risk yourself.
