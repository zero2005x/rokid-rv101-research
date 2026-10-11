# Rokid AR Glasses - Privilege Escalation Vulnerability Research Report

> 中文版：[PRIVESC_REPORT.md](PRIVESC_REPORT.md)

> ⚠ **Historical document (not current state)**: this write-up records the situation at the time; the project has changed substantially since (root acquired, engine switched to runtime probing, phone UI reworked, i18n 193 keys).
> For the current state see `README.md`, `MANIFEST.md`, `docs/deploy.md`.


## Device Information

| Item             | Value                             |
| ---------------- | --------------------------------- |
| **Device**       | Rokid AR Glasses (RV101)          |
| **Kernel**       | 5.10.209-perf                     |
| **Android version** | 12                             |
| **GPU**          | Adreno 621v1 (Chip ID: 0x6020100) |
| **SELinux**      | Enforcing (`u:r:shell:s0`)        |
| **Current user** | UID=2000 (shell)                  |

---

## 🔴 Confirmed Vulnerabilities

### 1. KGSL Use-After-Free (UAF)

**Severity**: High

**Description**:
After the KGSL file descriptor is closed, the already-mapped GPU memory can still be read and written.

**Reproduction steps**:

```c
// 1. Open KGSL and allocate GPU memory
int fd = open("/dev/kgsl-3d0", O_RDWR);
struct kgsl_gpumem_alloc alloc = {.size = 0x1000};
ioctl(fd, IOCTL_KGSL_GPUMEM_ALLOC, &alloc);

// 2. Map the GPU memory
void *map = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, offset);

// 3. Close the fd (triggers the UAF)
close(fd);

// 4. The freed memory can still be read and written!
*(uint64_t*)map = 0xAAAAAAAAAAAAAAAA;  // write succeeds
printf("Read: 0x%lx\n", *(uint64_t*)map); // read succeeds
```

**Impact**:

- Potential memory corruption
- If GPU address reuse can be triggered, arbitrary read/write becomes possible

---

### 2. KGSL OOB Read via mmap Offset

**Severity**: Medium

**Description**:
Mapping with a negative offset allows reading the contents of other GPU buffers.

**Test result**:

```
offset 0x59000 (diff -65536): val=0x4a4a4a4a4a4a4a4a  ← data from another buffer
offset 0x68000 (diff -4096): val=0x0
```

**Impact**:

- GPU data disclosure within the same process
- Cannot read across processes (GPU MMU isolation)

---

## 🟡 Potential Attack Surface

### 3. World-Writable Device Nodes

| Device                | Permissions | Status            |
| --------------------- | --------- | --------------- |
| `/dev/kgsl-3d0`       | rw-rw-rw- | ✅ Exploitable   |
| `/dev/binder`         | rw-rw-rw- | ⚠️ Needs more research |
| `/dev/ashmem`         | rw-rw-rw- | ⚠️ Needs more research |
| `/dev/hwbinder`       | rw-rw-rw- | ⚠️ Needs more research |
| `/dev/vndbinder`      | rw-rw-rw- | ⚠️ Needs more research |
| `/dev/i2c-1`          | rw-rw-rw- | ⚠️ Memory leak?  |
| `/dev/dri/card0`      | rw-rw-rw- | ❌ Blocked by SELinux |
| `/dev/dri/renderD128` | rw-rw-rw- | ❌ Blocked by SELinux |

### 4. Binder BC_ENTER_LOOPER Issue

**Description**: sending a `BC_ENTER_LOOPER` command makes the process hang or crash.

**Status**: this is expected behavior, not a vulnerability.

---

## ❌ Ruled-Out Attack Vectors

| Vector                     | Status | Reason                        |
| -------------------------- | ---- | ----------------------------- |
| Dirty Pipe (CVE-2022-0847) | ❌   | Patched (5.10.209 >= 5.10.102) |
| io_uring vulnerabilities   | ❌   | io_uring is not enabled on the device |
| SUID binaries              | ❌   | No SUID files found           |
| Kernel symbol leak         | ❌   | `/proc/kallsyms` addresses are zero |
| su binary                  | ❌   | Not found                     |
| debuggable mode            | ❌   | `ro.debuggable=0`             |

---

## 🔬 Technical Details

### KGSL Memory Mapping Formula

```
GPU address range: 0x4000000000 - 0x4001f32000
mmap offset = (page_index + 1) * 0x1000
where: page_index = (gpuaddr - 0x4000000000) / 0x1000
```

### Working KGSL IOCTLs

| IOCTL         | Code       | Function          |
| ------------- | ---------- | ----------------- |
| GPUMEM_ALLOC  | 0xc018092f | GPU memory allocation |
| GPUOBJ_INFO   | 0xc0200939 | Get GPU object information |
| GPU_COMMAND   | 0xc0580940 | GPU command submission |
| GPUOBJ_IMPORT | 0xc0280958 | Import external memory |
| GPUOBJ_SYNC   | 0xc0180956 | Synchronization operation |

### I2C Bus Findings

- `/dev/i2c-1`: accessible
- I2C functionality: 0xefe0009 (I2C, Block R/W)
- Read results show a possible **memory-content leak** (pointers and ASCII strings were visible)

---

## 📋 Follow-Up Research Directions

1. **KGSL GPU address reuse**
   - Try a larger-scale spray
   - Study the GPU address allocation algorithm

2. **Binder transaction operations**
   - Study `BC_TRANSACTION` attacks
   - The Binder death-notification mechanism

3. **I2C write testing**
   - Determine whether I2C devices can be written to
   - May affect hardware configuration

4. **Vendor-specific drivers**
   - The `rt600_spiboot` module
   - The `adsprpc-smd` device

---

## 📁 List of Test Programs

| File                   | Purpose                 |
| ---------------------- | ----------------------- |
| `privesc_scanner.c`    | Comprehensive privilege-escalation scanner |
| `kgsl_deep_research.c` | KGSL deep vulnerability research |
| `kgsl_uaf_v2.c`        | KGSL UAF exploitation attempt |
| `drm_attack.c`         | DRM/Graphics attack-surface probing |
| `i2c_attack.c`         | I2C bus probing         |
| `binder_cmd_test.c`    | Binder command test     |
| `binder_simple_test.c` | Simplified Binder test  |

---

## ⚠️ Disclaimer

This research is intended solely for authorized security testing and educational purposes.
Unauthorized use of these techniques may violate the law.
