# Parasitic GPU command injection — attack status report

> 中文版：[PARASITIC_INJECT_STATUS.md](PARASITIC_INJECT_STATUS.md)

> 📌 **Basis of fact**: wherever this document touches facts such as device generation, kernel config, CVE status, GhostLock offsets, or product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (single authoritative fact table; in case of conflict, that table decides).


## 📋 Goal

Combine **CVE-2025-21479** (Cross-FD GPU Memory Access) with **CVE-2025-21480** (Micronode Command Injection) to achieve parasitic GPU command injection, attempting to:

- Write `RBBM_SECVID_TRUST_CNTL (0x0F400)` to downgrade the GPU to a non-secure state
- Write `CP_PROTECT_CNTL (0x0084C)` to disable command processor protection

---

## 🔍 CVE-2025-21479 analysis result

### Confirmed items

| Item             | Status | Notes                                                       |
| ---------------- | ---- | ---------------------------------------------------------- |
| IOCTL identification       | ✅   | `IOCTL_GPUMEM_ALLOC` = 0xc018092f                          |
| Offset computation      | ✅   | `mmap_offset = (page_index + 1) * 0x1000`                  |
| In-process cross-FD access | ✅   | A second FD in the same process can read/write the first FD's GPU buffer          |
| Cross-process access       | ❌   | The GPU address space is **isolated per process**; SurfaceFlinger's buffer cannot be accessed |

### Test results

```
=== Testing Offset Mechanism ===
[*] Allocating 10 GPU buffers...
  [0] gpuaddr=0x4000001000
  [9] gpuaddr=0x400000a000

[*] Testing offset access (using second fd):
  offset=0x2000 -> val=0xaa00000000000000 [MATCH]
  offset=0xb000 -> val=0xaa00000000000009 [MATCH]
  ✅ All 10 matched

[*] Large-scale scan (100,000 pages):
  mapped=1, readable=1, PM4=0
  ⚠️ Only found our own allocation - cross-process NOT possible
```

### Key finding

**CVE-2025-21479 only applies to in-process attacks**:

- If code can be injected into the SurfaceFlinger process, it can be used
- A shell process cannot directly access another process's GPU buffer
- The GPU address 0x4000000000 is an independent starting position per process

---

## 🔍 CVE-2025-21480 (GPU Command Injection) analysis result

### Confirmed items

| Item              | Status | Notes                              |
| ----------------- | ---- | --------------------------------- |
| GPU_COMMAND IOCTL | ✅   | 0xc0580940 - returns success             |
| Context Creation  | ❌   | All 32 flag combinations return EINVAL |
| Submission without a Context   | ⚠️   | IOCTL returns 0 but the command does not execute         |
| PM4 Packet construction   | ✅   | Type3/Type4/Type7 packets are ready      |

### GPU Context creation failure

```
[-] FAILED: A6XX Std: GL + Submit + PreCtxTS (errno=22: Invalid argument)
[-] FAILED: Minimal: Zero flags (errno=22: Invalid argument)
[-] FAILED: Type VK + Submit (errno=22: Invalid argument)
[*] Total: 0/31 successful
```

**Cause analysis**:

1. The KGSL driver may restrict the shell user from creating a GPU context
2. SELinux policy may block it (but no AVC log was seen)
3. A specific system UID or the graphics group may be required

---

## 🔓 Major finding: the Bootloader is unlocked!

```properties
ro.boot.flash.locked=0                    # ✅ UNLOCKED
ro.boot.vbmeta.device_state=unlocked      # ✅ UNLOCKED
ro.boot.verifiedbootstate=orange          # 🟠 custom images can be flashed
ro.oem_unlock_supported=1                 # ✅ OEM unlock supported
```

### Impact

- A custom boot.img can be flashed directly through Fastboot
- Magisk can be used to patch boot.img and obtain root
- This is the **most direct root path**

---

## 📊 Other findings

### Writable Sysfs nodes

```
/sys/class/kgsl/kgsl-3d0/perfcounter  (-rw-rw----, shell can write)
```

### DRM devices

```
/dev/dri/renderD128  (crw-rw-rw-, world-accessible)
- GEM allocation ✅
- PRIME export ✅
- SurfaceFlinger fds ❌ (Permission denied)
```

### Service interfaces

```
android.graphicsenv.IGpuService          # GPU management service
android.hardware.graphics.allocator@4.0  # Gralloc HAL
vendor.qti.hardware.display.composer@3.1 # display composer
```

---

## 🎯 Recommended attack paths

### 1️⃣ Fastboot Root (highest priority)

```bash
# Steps
1. adb reboot bootloader  (or a key combination)
2. fastboot getvar all
3. fastboot boot twrp.img  (or a custom recovery)
4. Extract boot.img → Magisk patch → fastboot flash boot boot_patched.img
```

### 2️⃣ System application privilege escalation

```bash
# Find debuggable system applications
adb shell pm list packages -f | grep -E 'rokid|system'
# Check sharedUserId
adb shell dumpsys package <package> | grep sharedUserId
```

### 3️⃣ Binder FD hijack

If SurfaceFlinger's buffer FD can be obtained, CVE-2025-21479 can be combined with it:

```c
// requires root or system privileges
int fd = get_sf_buffer_fd_via_binder();
// then use cross-FD access
```

---

## 📁 Tools developed

| Tool                       | Function                | Status            |
| -------------------------- | ------------------- | --------------- |
| `kgsl_parasitic_inject`    | PM4 scan and injection      | ✅ Compilation complete     |
| `surfaceflinger_ib_hijack` | SF IB hijack          | ✅ Compilation complete     |
| `gpu_cmdbuf_spy`           | Real-time GPU buffer monitoring | ✅ Compilation complete     |
| `parasitic_inject_v3`      | Corrected injection tool      | ✅ **Verification succeeded** |
| `global_gpu_scan`          | Large-scale offset scan      | ✅ Compilation complete     |
| `sf_ib_inject_v2`          | Corrected SF injection      | ✅ Compilation complete     |

---

## ⏭️ Next steps

1. **Wait for the Rokid device to reconnect**
2. **Try to enter Fastboot mode**
   - `adb reboot bootloader`
   - or use a key combination (volume-/+ + power)
3. **Extract and patch boot.img**
4. **Flash the Magisk-patched boot.img to obtain root**
5. **With root, the PM4 injection of CVE-2025-21480 can be fully tested**

---

_Last updated: 2026-02-03_
