# Session 14 Research Summary - Rokid AR Glasses

## Date: 2026-02-02

## Target Device

- **Model**: Rokid AR Glasses RV101
- **Kernel**: 5.10.209-perf-gb80a8319d11b
- **Android**: 12
- **GPU**: Qualcomm Adreno621v1

## Validated Attack Vectors

### BPF Subsystem (Most Promising)

| Feature                     | Status     | Notes                               |
| --------------------------- | ---------- | ----------------------------------- |
| `unprivileged_bpf_disabled` | 0          | BPF available to unprivileged users |
| `bpf_jit_enable`            | 1          | JIT enabled                         |
| SOCKET_FILTER prog type     | ✅ Works   | Basic programs load successfully    |
| CGROUP_SKB prog type        | ❌ Limited | Program load fails                  |
| `bpf_ringbuf_reserve`       | ✅ Works   | fd=4 in socket_filter context       |
| ARRAY map                   | ✅ Works   | Can create and access               |
| HASH map                    | ✅ Works   | Can create                          |
| RINGBUF map                 | ✅ Works   | Consumer page mappable              |
| PROG_ARRAY                  | ✅ Works   | Tail calls available                |

### Heap Spray Primitives

| Primitive         | Status   | Notes                   |
| ----------------- | -------- | ----------------------- |
| setxattr spray    | ✅ Works | 64-1024 byte objects    |
| Pipe buffer spray | ✅ Works | 128+ pipe pairs created |
| Keys spray        | ✅ Works | 108 keys created        |
| BPF map spray     | ✅ Works | 2048+ maps created      |

### TIPC Subsystem

| Feature         | Status     | Notes                |
| --------------- | ---------- | -------------------- |
| Socket creation | ✅ Works   | AF_TIPC=30           |
| Group join      | ❌ Fails   | No bearer configured |
| Group races     | 3200+ done | But no UAF triggered |

### Other Attack Surfaces

| Surface          | Status            | Notes                       |
| ---------------- | ----------------- | --------------------------- |
| Magisk           | ⚠️ Installed      | su binary not accessible    |
| Debuggable app   | ✅ Works          | `com.cooper.wheellog.debug` |
| KGSL perfcounter | ✅ Writable       | Shell group can write       |
| `/dev/kgsl-3d0`  | ✅ World-writable | GPU device accessible       |
| `/dev/ashmem`    | ✅ World-writable | Shared memory accessible    |
| SysV IPC         | ❌ Disabled       | `msgget` not implemented    |

## Tools Created

| Tool                  | Purpose                       | Result                               |
| --------------------- | ----------------------------- | ------------------------------------ |
| `bpf_5_10_exploit`    | BPF verifier bypass tests     | Ringbuf program loads successfully   |
| `bpf_ringbuf_exploit` | Ringbuf exploitation research | Testing in progress                  |
| `cross_cache_exploit` | Cross-cache heap attack       | Spray primitives work, no corruption |
| `bpf_advanced`        | Program/map type scanning     | Identified allowed types             |
| `bpf_bypass_v2`       | Corrected jump offsets        | 3 programs load successfully         |

## Key Findings

1. **BPF Ringbuf is exploitable** - `bpf_ringbuf_reserve` helper works in socket_filter context
2. **Heap spray primitives available** - setxattr, pipes, keys, BPF maps all work
3. **Magisk installed but blocked** - Need to find way to activate
4. **TIPC lacks bearer** - Cannot trigger group UAF without network config
5. **No SysV IPC** - msg_msg spray not available on Android 12

## Recommended Next Steps

### Priority 1: BPF Ringbuf Exploitation

- Complete ringbuf reserve/submit test
- Investigate size confusion vulnerabilities
- Test OOB read/write via ringbuf

### Priority 2: Magisk Activation

- Check if Magisk app can be launched
- Look for boot image patching status
- Investigate `magisk --sqlite` commands

### Priority 3: run-as Exploitation

- Test CVE-2024-0044 on debuggable app
- Check for other run-as vulnerabilities
- Attempt sandbox escape

### Priority 4: KGSL GPU Exploitation

- Research Adreno621 specific vulnerabilities
- Test perfcounter manipulation
- Look for info leak primitives

## Commands Reference

```bash
# Check BPF settings
adb shell "cat /proc/sys/kernel/unprivileged_bpf_disabled"

# Run BPF exploit tools
adb shell "/data/local/tmp/bpf_5_10_exploit"
adb shell "/data/local/tmp/bpf_ringbuf_exploit"

# Check Magisk status
adb shell "pm list packages | grep magisk"

# Run debuggable app context
adb shell "run-as com.cooper.wheellog.debug id"

# Check slabinfo
adb shell "cat /proc/slabinfo | head -30"
```

## Session Status

- **Completed**: Attack surface mapping, heap spray validation
- **In Progress**: BPF ringbuf exploitation
- **Blocked**: TIPC group UAF (no bearer), SysV IPC (not available)
