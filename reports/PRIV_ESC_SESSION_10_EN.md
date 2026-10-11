# Rokid AR Glasses Privilege Escalation Vulnerability Research Report - Session 10

> 中文版：[PRIV_ESC_SESSION_10.md](PRIV_ESC_SESSION_10.md)

> 📌 **Basis of fact**: wherever this document touches facts such as device generation, kernel config, CVE status, GhostLock offsets or product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (the single authoritative fact table; it decides any conflict).

> ⚠ **2026-09-13 correction**: the Fastboot/Magisk/flash-boot.img route assumed in this document is **not feasible** (boot.img cannot be obtained and `fastboot flash/boot/erase` has been removed); root has been acquired at runtime by the GhostLock LPE.


**Date**: 2026-01-31  
**Target**: Rokid AR Glasses (Android 12, Kernel 5.10.209)  
**Researcher**: AI Assistant

---

## 📊 Environment Security Summary

### Favorable Factors ✅

| Item            | Status      | Notes                           |
| --------------- | --------- | ------------------------------ |
| **Bootloader**  | ✅ Unlocked | `unlocked:yes`, `secure:no`    |
| **BPF**         | ✅ Available   | `unprivileged_bpf_disabled=0`  |
| **TIPC Socket** | ✅ Available   | All socket types can be created and bound   |
| **Seccomp**     | ✅ Unrestricted | `Seccomp: 0`                   |
| **dmesg**       | ✅ Readable   | `dmesg_restrict=0`             |
| **KGSL GPU**    | ✅ Accessible | `/dev/kgsl-3d0` world-writable |
| **DRM/DRI**     | ✅ Openable | `/dev/dri/card0` is accessible        |
| **Keyring**     | ✅ Available   | `add_key` syscall is available         |
| **Ashmem**      | ✅ Accessible | `/dev/ashmem` world-writable   |

### Unfavorable Factors ❌

| Item               | Status         | Notes                          |
| ------------------ | ------------ | ----------------------------- |
| **Userfaultfd**    | ❌ Restricted      | Requires CAP_SYS_PTRACE           |
| **SLAB hardening** | ❌ Enabled      | `SLAB_FREELIST_HARDENED=y`    |
| **SLAB randomization** | ❌ Enabled      | `SLAB_FREELIST_RANDOM=y`      |
| **System V IPC**   | ❌ Disabled      | `CONFIG_SYSVIPC is not set`   |
| **User Namespace** | ❌ Disabled      | `CONFIG_USER_NS is not set`   |
| **nf_tables**      | ❌ Disabled      | `CONFIG_NF_TABLES is not set` |
| **kptr_restrict**  | ❌ Enabled      | value = 2, kernel pointers are hidden          |
| **SELinux**        | ❌ Enforcing | Must be bypassed                      |

---

## 🔍 Vulnerabilities Tested

### 1. CVE-2024-38401 (KGSL SyncSource UAF)

**Status**: ⚠️ Theoretically feasible, but not triggered in practice

**Versions tested**: v6, v7, v8 (Pipe spray, Keyring spray, Cross-cache)

**Findings**:

- `/dev/kgsl-3d0` is fully accessible
- The SYNCSOURCE IOCTLs work correctly
- The BPF heap spray successfully created 150,000+ objects
- Keyring spray is available
- No kernel crash could be triggered

**Root-cause analysis**:

1. `SLAB_FREELIST_HARDENED` prevents straightforward UAF exploitation
2. The KGSL driver may already have patched the reference counting in 5.10.209
3. The race window is extremely small and requires more precise timing control

### 2. CVE-2024-36971 (Route Cache UAF)

**Status**: ❌ Not exploitable

**Reason**:

- The device has no external network connection
- The loopback interface's DST never becomes obsolete
- Requires a real ICMP error to trigger

### 3. CVE-2024-0582 (io_uring PBUF_RING UAF)

**Status**: ❌ Feature does not exist

**Reason**:

- PBUF_RING was only introduced in Linux 5.19+
- 5.10.209 does not have this feature

### 4. Dirty Pipe (CVE-2022-0847)

**Status**: ❌ Patched

**Reason**:

- 5.10.209 > 5.10.102 (the patched version)

### 5. TIPC Protocol

**Status**: ⭐ Potentially exploitable

**Findings**:

```
TIPC activated (version 2.0.0)
AF_TIPC/STREAM socket: ✅ can be created
AF_TIPC/DGRAM socket: ✅ can be created
AF_TIPC/SEQPACKET socket: ✅ can be created
AF_TIPC/RDM socket: ✅ can be created
TIPC bind: ✅ succeeded
```

**Potential vulnerabilities**:

- CVE-2022-0435: TIPC stack overflow (requires networking)
- CVE-2021-43267: tipc_crypto_key_rcv heap overflow (requires encryption to be enabled)

---

## 🛠️ Tools Created

| File                   | Purpose                              |
| ---------------------- | --------------------------------- |
| `kgsl_uffd_exploit.c`  | KGSL UAF + Userfaultfd (requires CAP) |
| `kgsl_pipe_spray.c`    | KGSL UAF + Pipe buffer spray      |
| `kgsl_keyring_spray.c` | KGSL UAF + Keyring cross-cache    |
| `tipc_probe.c`         | TIPC feature probing                     |
| `msm_drm_probe.c`      | MSM DRM driver probing                  |

---

## 🎯 Recommended Next Steps

### ~~Priority path 1: boot.img patching (most feasible)~~ (**2026-09-13: not feasible; root has been acquired via the GhostLock LPE**)

Because the **bootloader is unlocked**, this is still the most direct path:

1. **Obtain the official firmware**: contact Rokid or search the developer community
2. **Extract boot.img**: use payload-dumper
3. **Patch**: Magisk v30.6 is already installed
4. **Flash**: `fastboot flash boot magisk_patched.img`

### Priority path 2: TIPC local vulnerabilities

The TIPC module is loaded and sockets are usable. Research directions:

- Vulnerabilities related to tipc_group_create
- tipc_link state-machine vulnerabilities
- Locally triggerable memory corruption

### Priority path 3: Other drivers

1. **synx_driver**: loaded; its interface needs study
2. **msm_drm**: requires graphics group permissions
3. **Binder**: complex, but may contain local vulnerabilities

### Priority path 4: Application-layer attacks

1. **Debuggable App**: `com.cooper.wheellog.debug` can be debugged
2. **Rokid AssistServer**: a system app with extensive permissions
3. **Intent hijacking**: study the communication of Rokid-specific apps

---

## 📝 Key Configuration Summary

```
CONFIG_BPF=y                    # BPF available ⭐
CONFIG_BPF_SYSCALL=y
CONFIG_BPF_JIT_ALWAYS_ON=y
CONFIG_USERFAULTFD=y            # UFFD exists but requires privileges
CONFIG_TIPC=y                   # TIPC available ⭐
CONFIG_IO_URING=y
CONFIG_KEYS=y
CONFIG_FUSE_FS=y
CONFIG_SLUB_DEBUG=y
CONFIG_SLAB_FREELIST_RANDOM=y
CONFIG_SLAB_FREELIST_HARDENED=y # heap exploitation is difficult
# CONFIG_KASAN is not set       # no memory checking
# CONFIG_SYSVIPC is not set     # msg_msg unavailable
# CONFIG_USER_NS is not set     # no user namespaces
# CONFIG_NF_TABLES is not set   # no nftables
```

---

## 🔐 Device Information

```
Device: Rokid AR Glasses (RG-glasses / RV101)
Android: 12 (API 32)
Kernel: 5.10.209-perf-gb80a8319d11b
Patch Level: 2024-07-05 (about 19 months behind)
SoC: Qualcomm SM6375 (neo)
GPU: Adreno 621
Bootloader: UNLOCKED
SELinux: Enforcing
```

---

**Report generated**: 2026-01-31 Session 10
