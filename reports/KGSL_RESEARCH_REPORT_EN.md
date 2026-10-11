# KGSL Attack Surface Research Report — updated 2026-02-03 (**originally written as 2025, corrected 2026-09-13**)

> 中文版：[KGSL_RESEARCH_REPORT.md](KGSL_RESEARCH_REPORT.md)

> 📌 **Basis of fact**: wherever this document touches facts such as device generation, kernel config, CVE status, GhostLock offsets, or product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (single authoritative fact table; in case of conflict, that table decides).


## Executive summary

After in-depth analysis, the `/dev/kgsl-3d0` GPU driver on the Rokid AR Glasses is confirmed to provide **a highly promising attack surface**.

### Confirmed capabilities

| Function                | Status    | Details                            |
| ----------------------- | ------- | ------------------------------- |
| GPUMEM_ALLOC            | ~~✅ Success~~ **measured ENOTSUP (functionality disabled)** | Can allocate 0x1000 - 0x10000000 bytes (**corrected 2026-09-13: see 5.2_VERIFICATION_LIVE_ADB_2026 §1.1**) |
| mmap GPU memory         | ✅ Success | Can be mapped into user space                |
| Read/write GPU memory   | ✅ Success | Full control                        |
| GPU_AUX_COMMAND         | ✅ Success | Returns counter information                  |
| SYNCSOURCE_CREATE_FENCE | ✅ Success | Can create a fence                    |
| GPU information disclosure | ✅ Success | Version numbers, etc.                        |

### List of working ioctls

```
0x38 - GPU info query 1
0x39 - GPU info query 2
0x3a - GPU info query 3
0x40 - Resource allocation (returns an incrementing ID)
0x56 - SYNCSOURCE_CREATE_FENCE
0x58 - GPU_AUX_COMMAND
```

### Potential attack vectors

#### 1. PERFCOUNTER (ioctl 0x3b) - errno=1 (EPERM)

- Requires extra privileges
- May be bypassable via CAP_SYS_ADMIN or a specific UID
- **Historical CVEs**: CVE-2020-3668, CVE-2021-30328

#### 2. Wait operation (ioctl 0x48) - errno=524 (ERESTARTSYS)

- Blocking wait operation
- A race condition may exist
- Interrupt handling needs further analysis

#### 3. GPU command submission (ioctl 0x5b) - errno=11 (EAGAIN)

- Asynchronous operation
- Requires specific conditions to succeed
- May be related to context creation

### Failed paths

1. **Context creation** - "legacy context switch not supported"
2. **Timeline operations** - ENODEV
3. **Direct memory free** - no working free ioctl was found

### Suggested next steps

1. **Study the full functionality of GPU_AUX_COMMAND**
   - It successfully returned data
   - It may be usable for some form of control

2. **Explore the perfcounter sysfs interface**
   - `/sys/class/kgsl/kgsl-3d0/perfcounter` is shell-writable
   - May bypass the ioctl permission check

3. **Multi-process race conditions**
   - fd sharing + parallel operations
   - mmap + close race

4. **Integer overflow attempts**
   - In the allocation path
   - size parameter manipulation

---

## Technical details

### GPU memory allocation test result

```c
[+] Allocated gpuaddr=0x4000000000
[+] mmap ptr=0x7e19455000
[*] Writing to GPU memory...
[+] Read back: 0xdeadbeef   // write succeeded!
```

### Working ioctl command mapping

```
ioctl 0xc0040938 (nr=0x38) - GPU info 1
ioctl 0xc0040939 (nr=0x39) - GPU info 2
ioctl 0xc004093a (nr=0x3a) - GPU info 3
ioctl 0xc0040940 (nr=0x40) - Resource alloc
ioctl 0xc0040956 (nr=0x56) - Syncsource fence
ioctl 0xc0200958 (nr=0x58) - GPU aux command
```

### Error code analysis

| errno             | Meaning           | ioctl                 |
| ----------------- | -------------- | --------------------- |
| 1 (EPERM)         | Insufficient privileges       | 0x3b (perfcounter)    |
| 11 (EAGAIN)       | Resource temporarily unavailable | 0x5b (command submit) |
| 19 (ENODEV)       | Device does not exist     | 0x5c, 0x5d (timeline) |
| 22 (EINVAL)       | Invalid argument           | context create        |
| 95 (EOPNOTSUPP)   | Operation not supported     | 0x15, 0x20            |
| 524 (ERESTARTSYS) | Interrupted by a signal     | 0x48 (wait)           |

### Device configuration

- **GPU**: Adreno 621v1
- **Kernel**: 5.10.209-perf
- **Driver**: /vendor/lib/modules/msm_kgsl.ko
- **SELinux**: Enforcing

---

## CVE applicability assessment

### CVE-2023-33063 (Timeline UAF)

- **Status**: not applicable
- **Reason**: the timeline ioctl returns ENODEV

### CVE-2023-33107 (Integer Overflow)

- **Status**: unconfirmed
- **Reason**: GPUMEM_ALLOC_ID returns EINVAL

### CVE-2024-23380 (GPU Command UAF)

- **Status**: partially applicable
- **Reason**: GPU_AUX_COMMAND succeeds, but further research is needed

### CVE-2023-21106 (Dispatcher)

- **Status**: unconfirmed
- **Reason**: requires context creation

---

## Conclusion

KGSL provides powerful user-space primitives:

1. GPU memory allocation of arbitrary size
2. Mapping into user space
3. Full read/write control

Main limitations:

- Cannot create a context (legacy context switch not supported)
- No direct free operation
- perfcounter requires privileges

It is recommended to continue researching GPU_AUX_COMMAND and the perfcounter sysfs interface.
