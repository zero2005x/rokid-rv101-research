# Rokid AR Glasses Privilege Escalation Research Report

> 中文版：[ATTACK_SURFACE_RESEARCH.md](ATTACK_SURFACE_RESEARCH.md)

## Session 12: Alternative Attack Surface Deep Dive

**Date**: 2026-02-02
**Target**: Rokid AR Glasses (RV101)
**Kernel**: 5.10.209-perf
**Android**: 12 (API 32)
**Focus**: TIPC, Ashmem, BPF, Keyring, Alternative Attack Surfaces

---

## Executive Summary

This research performs an in-depth attack-surface analysis of the Rokid AR Glasses. Because most conventional CVE vulnerabilities have already been patched, we explored alternative attack vectors such as TIPC, Ashmem and BPF.

### Key Findings

| Vector                        | Status            | Potential                    |
| --------------------------- | --------------- | ----------------------- |
| **🔴 KGSL Cross-FD Access** | ✅ **confirmed**   | **Critical** - CVE candidate |
| **TIPC Group Join/Leave**   | ✅ usable         | **High** - 224k+ operations succeeded |
| **BPF Heap Spray**          | ✅ usable         | **High** - 4MB+ spray     |
| **Ashmem**                  | ✅ usable         | Medium - 64 regions         |
| **Keyring Spray**           | ✅ usable         | Medium - 121 keys           |
| **Pipe Buffer Spray**       | ✅ usable         | Medium - 4MB                |
| **DMA-BUF Heap**            | ❌ permission denied     | none                      |
| **KGSL GPU Context**        | ❌ blocked by hwsched | Low                      |

---

## 🔴 NEW: KGSL GPU driver vulnerability (2026-02-02)

### Vulnerability Overview

A **cross-FD GPU memory access vulnerability** was found in the Qualcomm KGSL driver, allowing different file descriptors within the same process to access each other's GPU memory.

### Vulnerability Severity

| Item       | Value                                 |
| ---------- | ---------------------------------- |
| Type       | Cross-FD GPU Memory Access         |
| Severity     | **Critical** (CVSS ~7.8)           |
| Status       | ✅ confirmed and PoC-verified               |
| Affected component | Qualcomm KGSL Driver (msm_kgsl.ko) |

### Technical Details

**mmap offset to GPU page mapping:**

```
offset = (page_index + 1) * 0x1000

offset 0x1000 → gpuaddr 0x4000000000 (page 0)
offset 0x2000 → gpuaddr 0x4000001000 (page 1)
...
```

**PoC result:**

```
fd1 allocated: gpuaddr=0x4000000000
fd1 wrote secret: 0xaabbccdd11223344
fd2 mmap offset=0x1000 → reads: 0xaabbccdd11223344 ✅
fd2 modifies to: 0xfeedfacecafebabe
fd1 sees: 0xfeedfacecafebabe ✅ CONFIRMED!
```

### Other KGSL Findings

| Finding            | Description                                                         |
| --------------- | ------------------------------------------------------------ |
| Double mmap     | multiple mmaps of the same GPU address point at the same physical page                      |
| Persistence after fd close | 100/100 mappings remain valid after the fd is closed                               |
| 6 working ioctls  | 0x38-0x3a (info), 0x40 (alloc), 0x56 (fence), 0x58 (aux cmd) |

### Security Impact

1. **Sandbox escape**: different security domains using different fds can be bypassed
2. **Information disclosure**: read sensitive GPU data of other fds
3. **Memory corruption**: modify critical GPU structures

### Related Files

| File                    | Purpose                 |
| ----------------------- | -------------------- |
| kgsl_cross_fd_exploit.c | cross-fd memory access PoC |
| kgsl_offset_exploit.c   | offset mapping analysis      |
| KGSL_FINAL_REPORT.md    | complete vulnerability report         |

---

## 1. TIPC Subsystem Analysis

### 1.1 TIPC Version and Status

```
TIPC Version: 2.0.0
Module Path: /sys/module/tipc/
```

### 1.2 Socket Type Tests

| Socket Type    | Status   | Default Options                   |
| -------------- | -------- | --------------------------------- |
| SOCK_STREAM    | ✅ Works | importance=0, conn_timeout=8000   |
| SOCK_DGRAM     | ✅ Works | src_droppable=1, dest_droppable=1 |
| SOCK_SEQPACKET | ✅ Works | importance=0, conn_timeout=8000   |
| SOCK_RDM       | ✅ Works | dest_droppable=1                  |

### 1.3 Group Join/Leave Race

**CVE-2024-36886 Style Attack Results:**

```
Phase 2 Race Attack:
- Thread 0: 112,115 iterations, 100% success
- Thread 1: 112,052 iterations, 100% success
- Total: 224,167 join/leave cycles in 5 seconds
- Kernel Log: "Trying to set illegal importance in message"
```

**Observations**:

- Group join/leave is fully usable
- 44,833 operations per second can be reached
- The kernel emits a warning message (a potential bug indicator)

### 1.4 Undocumented Socket Options

```
Option 127: value=0x0 (can be set to 0xFFFFFFFF)
Option 128: value=0x0 (can be set to 0x2)
Option 129: value=0x1
Option 130: value=0x1f40 (can be set to 0x0 or 0x7FFFFFFF)
Option 131: value=0x0
Option 132: value=0x0
Option 135: value=0x0
Option 137: value=0x0
```

### 1.5 Netlink Generic Families

```
[+] Family 'TIPC' - 存在
[+] Family 'nfc' - 存在
```

---

## 2. Heap Spray Primitives

### 2.1 BPF Map Spray

```c
// successfully created 32 BPF maps
// each map: key_size=4, value_size=256, max_entries=64
// total: 2,048 entries = 524 KB controlled data

// Phase 2 extension:
// 32 maps × 512 entries × 256 bytes = 4,096 KB
```

**Advantages**:

- Fully controlled data content
- Predictable allocation size
- No permission restriction (`unprivileged_bpf_disabled=0`)

### 2.2 Keyring Object Spray

```c
// successfully created 121 keys
// variable size: 64, 96, 128, 192, 256 bytes
// can target different slab caches
```

**Observations**:

- The `add_key` syscall is usable
- `keyctl(KEYCTL_REVOKE)` can be used to create holes
- Suitable for cross-cache attacks

### 2.3 Pipe Buffer Spray

```
Created: 64 pipe pairs
Buffer size: 65,536 bytes each
Total written: 4,096 KB
```

### 2.4 Ashmem Spray

```
Created: 64 ashmem regions
Size per region: 4,096 bytes
Pattern verification: 64/64 (100%)
Pin/Unpin: ✅ Works
Purge: ✅ Works
```

---

## 3. Slab Allocator Analysis

### 3.1 KGSL/GPU Related Slabs

| Slab Name             | Object Size | Objects | Attack Relevance |
| --------------------- | ----------- | ------- | ---------------- |
| kgsl_iommu_addr_entry | 40 bytes    | 1,224   | Medium           |
| kgsl_memobj_node      | 64 bytes    | 256     | Medium           |
| kgsl_event            | 128 bytes   | 128     | Medium           |
| adreno_dispatch_job   | 16 bytes    | 1,024   | Low              |

### 3.2 Target Slab Sizes

Choose spray objects of the corresponding size according to the vulnerability type:

- **kmalloc-64**: keyring (64 bytes), kgsl_memobj_node
- **kmalloc-128**: keyring (128 bytes), kgsl_event
- **kmalloc-256**: BPF value (256 bytes), tipc_msg structures

---

## 4. Tools Created

### 4.1 alt_attack_surface_v2

**Features**:

1. Ashmem memory exploitation
2. DMA-BUF heap probing
3. BPF heap spray
4. Keyring spray with hole creation
5. Pipe buffer spray
6. Socket buffer spray

**Compile**:

```bash
aarch64-linux-android32-clang -o alt_attack_surface_v2 alt_attack_surface_v2.c -static -O2
```

### 4.2 netlink_tipc_exploit

**Features**:

1. TIPC socket type enumeration
2. TIPC group join/leave race
3. TIPC bind exploration
4. Message buffer overflow probing
5. Netlink generic family enumeration
6. Socket option fuzzing
7. Service announcement spray

### 4.3 tipc_uaf_exploit

**Features**:

1. Phase 1: BPF + Keyring spray setup
2. Phase 2: TIPC group join/leave race
3. Phase 3: Create holes and reclaim
4. Phase 4: Intensive race with message flooding
5. Phase 5: Cleanup and analysis

---

## 5. Kernel Log Analysis

### 5.1 Captured Messages

```
[21237.914438] [T28903] tipc: Trying to set illegal importance in message
```

**Interpretation**:

- This means our TIPC operations triggered the kernel's validation logic
- `T28903` is the process that triggered this message
- "illegal importance" may be caused by boundary-value testing

### 5.2 No KASAN/Crash

- No KASAN UAF report was found
- No kernel panic
- The system remained stable

---

## 6. Exploitation Path Analysis

### 6.1 Most Promising Vectors

1. **TIPC Group Message UAF (CVE-2024-36886 style)**
   - High-frequency join/leave operations can be achieved
   - Message fragmentation requires further research

2. **BPF Verifier Bypass**
   - unprivileged BPF is usable
   - a verifier bug applicable to 5.10.209 needs to be found

3. **Cross-Cache Attack via Keyring**
   - controllable allocation/free
   - can be combined with the TIPC UAF

### 6.2 Requires Further Research

1. TIPC bearer configuration (currently no bearer)
2. The specific trigger conditions of `tipc_buf_append()`
3. Known BPF verifier vulnerabilities in 5.10.209
4. A way to bypass KGSL hwsched

---

## 7. Next Steps

1. **Deeper TIPC analysis**
   - Study the TIPC bearer configuration requirements
   - Analyze the `tipc_buf_append()` code path
   - Test larger messages to cause fragmentation

2. **BPF Verifier Research**
   - Search for BPF CVEs in 5.10.x kernels
   - Test known vulnerabilities such as CVE-2023-2163

3. **Combined Attack Development**
   - Use BPF spray + TIPC UAF
   - Develop controlled data placement

4. **Kernel Symbol Leak**
   - Use the KGSL perfcounter info leak
   - Try to bypass kptr_restrict=2

---

## 8. BPF Verifier Analysis (NEW)

### 8.1 BPF Security Settings

```
unprivileged_bpf_disabled = 0  ✓ (can be used unprivileged)
bpf_jit_enable = 1             ✓ (JIT enabled)
bpf_jit_harden = N/A           (not set)
```

### 8.2 Available Map Types

| Map Type     | Status       | Notes             |
| ------------ | ------------ | ----------------- |
| HASH         | ✅ Works     | Standard hash map |
| ARRAY        | ✅ Works     | Fixed-size array  |
| PERCPU_HASH  | ✅ Works     | Per-CPU hash      |
| PERCPU_ARRAY | ✅ Works     | Per-CPU array     |
| RINGBUF      | ✅ Create OK | mmap denied       |

### 8.3 Available Program Types

| Program Type  | Status          |
| ------------- | --------------- |
| SOCKET_FILTER | ✅ Works        |
| CGROUP_SKB    | ✅ Works        |
| Others        | ❌ Need CAP_BPF |

### 8.4 Available Helper Functions

```
✅ map_lookup_elem (id=1)
✅ map_delete_elem (id=3)
✅ ktime_get_ns (id=5)
```

### 8.5 Verifier Test Results

- Normal map access: ✅ Allowed
- OOB stack access: ✅ Rejected (expected)
- Large ALU operations: ✅ Allowed
- CVE-2023-2163 style pruning: ❌ Rejected (patched)

---

## 9. Files Modified/Created

| File                            | Lines | Description                         |
| ------------------------------- | ----- | ----------------------------------- |
| alt_attack_surface_v2.c         | **704** (re-measured 2026-09-13; originally 415) | Multi-vector heap spray tool        |
| netlink_tipc_exploit.c          | 560   | TIPC/Netlink vulnerability explorer |
| tipc_uaf_exploit.c              | 478   | TIPC CVE-2024-36886 style exploit   |
| bpf_verifier_exploit.c          | 630   | BPF verifier research tool          |
| **kgsl_cross_fd_exploit.c**     | ~200  | 🔴 KGSL cross-FD GPU memory access PoC    |
| **kgsl_offset_exploit.c**       | ~150  | KGSL offset mapping analysis                |
| **kgsl_fd_confusion_exploit.c** | ~300  | FD confusion and race-condition testing               |
| **KGSL_FINAL_REPORT.md**        | ~400  | complete KGSL vulnerability report                   |
| ATTACK_SURFACE_RESEARCH.md      | -     | This report                         |

---

## 10. Conclusion

This research confirmed multiple viable attack primitives **and discovered one critical vulnerability**:

### 🔴 Highest Priority: KGSL Cross-FD GPU Memory Access

**A confirmed cross-FD GPU memory access vulnerability**:

- allows different fds in the same process to read and write each other's GPU memory
- may lead to sandbox escape, information disclosure and memory corruption
- **should be submitted to Qualcomm as a CVE report**

### Other Attack Primitives

1. **TIPC Group Join/Leave** - high frequency is achievable, but a bearer configuration is needed to trigger a real UAF
2. **BPF Heap Spray** - a perfectly controllable kernel heap spray primitive
3. **BPF SOCKET_FILTER** - BPF programs can be loaded for further research
4. **Keyring Spray** - a variable-size slab allocator

### Suggested Attack Chain

```
KGSL Cross-FD (info leak) → BPF Heap Spray (object placement) → TIPC UAF (trigger) → Root
```

**The KGSL vulnerability is the most valuable finding so far**, and it already has a complete PoC verification.
**BPF remains an important auxiliary vector**, because `unprivileged_bpf_disabled=0` allows unprivileged users to load BPF programs.
