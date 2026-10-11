# Rokid AR Glasses - CVE Probe Results Report (2026-02-03)

> 中文版：[CVE_PROBE_REPORT_20260203.md](CVE_PROBE_REPORT_20260203.md)

> ⚠ **2026-09-13 correction**: the **Fastboot / Magisk / boot.img flashing route mentioned in this document has been confirmed infeasible** (boot.img cannot be obtained: `fastboot fetch` is unsupported, `dd` requires root, and there is no compatible recovery / OTA source), and **root was obtained on 2026-08-22 via the GhostLock LPE** (runtime, armed once per boot over adb). Treat the following sections as a historical assessment; do not carry out the flashing steps.


## Device Information

- **Kernel**: 5.10.209
- **Android**: 12
- **SoC**: Qualcomm neo (SoC ID: 579)
- **GPU**: Adreno 621v1
- **Security patch level**: 2024-07-05

---

## 1. Attack Surface Discoveries

### 1. `/dev/kgsl-3d0` (GPU driver) ⭐⭐⭐⭐⭐ Highest priority

**Access permission**: **O_RDWR fully available!**

**Successful ioctl commands**:
| IOCTL | Return | Data leaked |
|-------|------|----------|
| 0xc1000938 | ret=0 | GPU version: 0x41a, 0x41b |
| 0xc1000939 | ret=0 | (empty) |
| 0xc100093a | ret=0 | counter: 0xe |
| 0xc1000940 | ret=0 | status: 0x1 |
| 0xc1000956 | ret=0 | size: 0x20 |
| 0xc1000958 | ret=0 | counter: 0x2 |

**Commands requiring privileges**:

- `0xXXXX093b`: returns **errno=1 (EPERM)** - sensitive operation

**Potential CVEs**:

- CVE-2023-33063: KGSL Use-After-Free
- CVE-2023-33107: KGSL Integer Overflow
- CVE-2024-23380: KGSL OOB Write
- CVE-2024-43098: kgsl_ioctl_timeline_wait UAF

**Writable sysfs file**:

```
-rw-rw---- 1 root shell /sys/class/kgsl/kgsl-3d0/perfcounter
```

⚠️ **The shell user can write to it directly!** Writing has been verified.

---

### 2. `/dev/adsprpc-smd` & `/dev/adsprpc-smd-secure` ⭐⭐⭐⭐

**Access permission**: O_RDONLY (O_RDWR requires privileges)

**Successful ioctls**:
| IOCTL | Device | Result |
|-------|------|------|
| 0xc00c5211 (GET_DSP_INFO) | smd | ret=0, leaks DSP information |
| 0xc0045208 (GETINFO) | smd-secure | ret=0, info=0x1 |
| 0xc0285206 (MMAP) | smd | errno=44 (requires a channel) |
| 0xc0405213 | smd | ret=53 |

**DSP domain information leak**:

- Domain 0 (ADSP): attr=6 → cap=0xca73
- Domain 2 (SDSP): attr=6 → cap=0xca73
- Domain 3 (CDSP): attr=1 → cap=0x1 (enabled)
- Domain 3 (CDSP): attr=4 → cap=0x200000 (2MB)
- Domain 3 (CDSP): attr=6 → cap=0x8b73

**Potential CVEs**:

- **CVE-2024-43047**: ADSPRPC fastrpc_mmap UAF (ITW exploitation confirmed)
- **CVE-2024-49848**: ADSPRPC UAF in refcount

**Next step**: a connection to `vendor.qti.hardware.dsp@1.0::IDspService` via HWBinder is required to obtain an fd with write permission.

---

### 3. `/dev/ashmem` ⭐⭐⭐

**Access permission**: O_RDWR fully available

This is the Android shared-memory driver and can be used for:

- Heap spray
- Memory layout control
- Use in combination with other vulnerabilities

---

## 2. Excluded CVEs

| CVE            | Reason                                |
| -------------- | ------------------------------------- |
| CVE-2024-1086  | `CONFIG_NF_TABLES is not set`         |
| CVE-2024-56720 | `CONFIG_BPF_STREAM_PARSER is not set` |
| CVE-2025-21655 | requires kernel 6.1+, currently 5.10  |
| CVE-2024-0582  | `CONFIG_IO_URING is not set`          |

---

## 3. Feasible Attack Paths

### Path A: KGSL GPU driver (most recommended)

1. **Goal**: exploit a known KGSL vulnerability to obtain arbitrary kernel read/write
2. **Advantages**:
   - O_RDWR fully available
   - Multiple ioctls successfully return data
   - A writable sysfs file exists
   - The Adreno GPU driver has historically had many vulnerabilities
3. **Steps**:
   - Identify the correct ioctl structures
   - Trigger a UAF or OOB vulnerability
   - Use heap spray to control the memory layout
   - Overwrite kernel structures to obtain privilege escalation

### Path B: ADSPRPC (alternative)

1. **Goal**: obtain a kernel UAF through CVE-2024-43047/49848
2. **Challenge**: requires O_RDWR or obtaining an fd through HWBinder
3. **Steps**:
   - Connect to the IDspService HAL service
   - Request an ADSPRPC fd
   - Trigger the map collision UAF
   - Use inotify_event_info heap spray

### Path C: Combined attack

1. Use the KGSL information leak to bypass KASLR
2. Use ashmem for heap spray
3. Trigger the ADSPRPC or KGSL UAF
4. Overwrite the cred structure for privilege escalation

---

## 4. Next Actions

### Priority 1: Deep KGSL probing

- [ ] Identify the functionality of ioctl 0x3b (EPERM)
- [ ] Attempt to create a GPU context with the correct parameters
- [ ] Explore the exploitation potential of the perfcounter sysfs
- [ ] Research Adreno 621v1-specific vulnerabilities

### Priority 2: ADSPRPC HWBinder

- [ ] Develop an HWBinder client to connect to IDspService
- [ ] Obtain an ADSPRPC fd with write permission
- [ ] Test the CVE-2024-43047 exploitation path

### Priority 3: Boot.img modification (fallback)

- [ ] If the kernel exploit fails, attempt to modify boot.img

---

## 5. Technical Details

### KGSL ioctl command format

```
bits 0-7:   command number
bits 8-15:  magic number (0x09 for KGSL)
bits 16-29: size
bits 30-31: direction (00=none, 01=write, 10=read, 11=read+write)

Example: 0xc1000938
  Direction: 11 (R/W)
  Size: 0x100 = 256 bytes
  Magic: 0x09
  Number: 0x38 = 56
```

### ADSPRPC device list

```
/dev/adsprpc-smd         mode=664 (O_RDONLY OK)
/dev/adsprpc-smd-secure  mode=664 (O_RDONLY OK, GETINFO works)
```

### HAL service

```
vendor.qti.hardware.dsp@1.0::IDspService/dspservice (PID 804)
```
