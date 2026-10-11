# KGSL GPU Exploitation Research Report - Session 11

> 中文版：[KGSL_RESEARCH_SESSION_11.md](KGSL_RESEARCH_SESSION_11.md)

> 📌 **Basis of fact**: wherever this document touches facts such as device generation, kernel config, CVE status, GhostLock offsets, or product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (single authoritative fact table; in case of conflict, that table decides).


**Date**: 2026-02-02  
**Target**: Rokid AR Glasses (Android 12, Kernel 5.10.209, Adreno 621v1)  
**Research Focus**: GPU Memory, Perfcounter, Information Leak

---

## 📊 Executive Summary

Deep analysis of the KGSL GPU driver interface revealed multiple potential information-leak vectors and exploitation primitives. Although a GPU Context cannot be created directly (blocked by hwsched mode), there is still a usable attack surface.

### Key Findings

| Finding                                     | Severity | Exploitation potential       |
| ---------------------------------------- | ------ | -------------- |
| Perfcounter offset_hi leaks GPU MMIO offsets | Medium     | KASLR bypass aid |
| Property 0x02 leaks the GPU virtual address base      | Medium     | Address prediction       |
| IOCTL 0x58 global counter                    | Low     | Timing side channel     |
| GPU Memory ID is predictable                     | Medium     | UAF exploitation aid   |
| mmapsize fixed at 768KB                      | High     | Heap spray optimization     |

---

## 🔍 Detailed Findings

### 1. Perfcounter Register Offset Leak

**Finding**: all perfcounter groups return valid `offset` and `offset_hi` values

```
Base offset_hi: 0x041b (GPU MMIO offset base)
Max offset_hi:  0x3cd1 (Group 14 RB_RAW)
Range:          0x38b6 bytes (~14.5 KB)
```

**Unique offset values (20 of them)**:

```
0x041b → Group 0  (CP - Command Processor)
0x0423 → Group 1  (RBBM - Ring Buffer Block Manager)
0x0433 → Group 2  (PC - Primitive Cull)
0x0443 → Group 3  (VFD - Vertex Fetch Decode)
0x044f → Group 4  (HLSQ - High Level Shader Queue)
0x045b → Group 5  (VPC - Varying/Parameter Cache)
0x0465 → Group 24 (Unknown - possibly Debug)
0x046d → Group 6  (TSE - Triangle Setup Engine)
0x0475 → Group 7  (RAS - Rasterizer)
0x048d → Group 8  (RB - Render Buffer)
0x04a5 → Group 9  (VSC - Visibility Stream Compress)
0x04d5 → Group 10 (CCU - Color Cache Unit)
0x04e5 → Group 11 (UCHE - Unified Cache)
0x04e9 → Group 23 (Unknown)
0x04f1 → Group 25 (GMU_GENERAL)
0x04f9 → Group 26 (GMU_ALWAYS)
0x0981 → Group 27 (GMU_POWER)
0x3ccf → Group 14 (RB_RAW counter 0)
0x3cd0 → Group 14 (RB_RAW counter 1)
0x3cd1 → Group 14 (RB_RAW counter 2)
```

**Analysis**:

- These offset values correspond to MMIO registers of the Adreno 621 GPU
- The offset spacing is not fixed (8, 12, 16 bytes), indicating different functional blocks
- The RB_RAW group's offsets (0x3ccc-0x3cd1) are noticeably higher than the others, and may be a GPU shared memory region

---

### 2. Device Property Information Leak

#### Property 0x02 (DEVICE_SHADOW)

```
Value: 0xefff_f000
```

**Interpretation**:

- This is the shadow-mapping base address of the GPU virtual address space
- It sits near `0xefff_f000` (near the top of the ~4GB address space)
- This address can be used to:
  - Predict GPU command buffer locations
  - Aid in constructing an arbitrary write primitive

#### Property 0x15

```
Value: 0x016dd112 (23,974,162)
```

Possibly:

- A GPU clock counter
- A random seed value
- A runtime timestamp

#### Property 0x29

```
Value: 0x65726441 = "Adre" (ASCII)
```

Part of the GPU model string ("Adreno")

#### Property 0x2a

```
Value: 0x06020100
```

GPU Chip ID: `06 02 01 00` = Adreno 621 revision 1.0

---

### 3. GPU Memory Allocation Patterns

#### ID allocation pattern

```
Requested size   ID        Increment
4KB        0         -
8KB        4096      +4096
16KB       12288     +8192
64KB       28672     +16384
256KB      94208     +65536
1MB        356352    +262144
4MB        1404928   +1048576
```

**Finding**: the ID increment = the requested size

This means:

1. The ID is perfectly predictable
2. If the size of an allocation is known, its ID can be computed
3. This is very useful for target identification in UAF attacks

#### mmapsize is fixed

```
mmapsize of every allocation = 786,432 bytes (768 KB)
```

**Important finding**:

- No matter how much memory is requested, the kernel always allocates a fixed 768KB user-space mapping
- This means we can:
  1. Request a small allocation (4KB)
  2. Get a large mapping (768KB)
  3. Potentially out-of-bounds read/write

---

### 4. IOCTL 0x56/0x58 Timeline Interface

#### IOCTL 0x56 (Timeline Create?)

```
Structure:
  offset 0:  input value (echo back)
  offset 12: fixed value 0x20 (32)
```

This may be a timeline creation interface; the returned 32 may be:

- Timeline capacity
- The maximum number of fences supported

#### IOCTL 0x58 (Timeline Query?)

```
Structure:
  offset 0:  input value (echo back)
  offset 8:  global monotonically increasing counter
```

**Exploitation value**:

- This counter can be used for a timing side channel
- It can probe other processes' GPU activity
- It increases by 1 on every call

---

## 🎯 Exploitation Strategies

### Strategy A: GPU Memory OOB Read/Write

**Principle**: exploit the fixed 768KB mmapsize

```c
// 1. Allocate several small buffers in a row
gpumem_alloc(4096);  // ID=0, mapped 0-768KB
gpumem_alloc(4096);  // ID=4096, mapped 0-768KB

// 2. If these allocations are physically adjacent
// we may be able to reach the second allocation's data through the first mapping

// 3. Combine with syncsource creation to allocate in a specific slab
```

### Strategy B: Direct Perfcounter Register Access

**Hypothesis**: if the offset value can be controlled

```c
// 1. Analyze whether PERFCOUNTER_GET really writes to the register
// 2. If offset is controllable, out-of-bounds reads of GPU MMIO may be possible
// 3. This may leak more address information
```

### Strategy C: Timeline Counter Side-Channel

**Use**: probe system state

```c
// 1. Monitor changes in the IOCTL 0x58 counter
// 2. When other processes use the GPU, the counter changes
// 3. Can be used for timing attacks or state inference
```

### Strategy D: Combined Attack

1. Use Property 0x02 to get the GPU VA base address
2. Use Perfcounter offsets to infer the MMIO layout
3. Use GPU Memory's fixed mmapsize for heap spraying
4. Combine with a Syncsource race or another kernel vulnerability

---

## 🛠️ Tools Created

| File                      | Purpose                 |
| ------------------------- | -------------------- |
| `kgsl_advanced_exploit.c` | Comprehensive exploitation research tool |
| `kgsl_leak_analysis.c`    | Focused information-leak analysis     |

---

## 📋 Next Steps

### Priority 1: mmap out-of-bounds analysis

1. Confirm whether the 768KB mmapsize really allows out-of-bounds access
2. Try reading data beyond the allocation size after consecutive allocations
3. If feasible, this is a powerful information-leak primitive

### Priority 2: Perfcounter write analysis

1. Confirm whether PERFCOUNTER_GET really sets hardware registers
2. Try using abnormal countable values
3. Check whether an out-of-bounds write is possible

### Priority 3: In-depth study of IOCTL 0x5b

1. This IOCTL keeps returning EAGAIN
2. Reverse engineering msm_kgsl.ko is needed to understand the required state
3. It may be an alternative path for GPU command submission

### Priority 4: Continue the boot.img plan

Since the Bootloader is unlocked, obtaining the official firmware or OTA package remains the most reliable root path.

---

## 🔐 Security Assessment

### Attack difficulty assessment

| Vector              | Complexity | Feasibility     |
| ----------------- | ------ | ---------- |
| mmap OOB          | Low     | To be verified     |
| Perfcounter abuse | Medium     | To be researched     |
| Side-channel      | Low     | Limited value   |
| Context bypass    | High     | Requires a 0-day |

### Defensive recommendations

1. Fix the mmapsize computation logic; it should equal the actual allocation size
2. Restrict perfcounter access for unprivileged users
3. Lock the Bootloader and verify the boot chain
4. Update security patches to the latest

---

**Report Generated**: 2026-02-02  
**Status**: Research In Progress
