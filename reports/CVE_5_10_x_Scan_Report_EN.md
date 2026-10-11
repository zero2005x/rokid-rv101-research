# Linux 5.10.x Kernel Vulnerability Scan Report

> 中文版：[CVE_5_10_x_Scan_Report.md](CVE_5_10_x_Scan_Report.md)

> 📌 **Basis in fact**: wherever this document touches on facts such as device generation, kernel config, CVE status, the GhostLock offset, and product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (the single authoritative fact table; in case of conflict that table decides).

> ⚠ **2026-09-13 correction**: the Fastboot / Magisk / flashing boot.img route assumed in this document is **infeasible** (boot.img cannot be obtained, and `fastboot flash/boot/erase` have been removed); root has already been obtained at runtime via the GhostLock LPE.


## Target Kernel Version

**Linux 5.10.209-perf-gb80a8319d11b**

---

## Vulnerability List (sorted by exploitability)

### 🔴 High risk - potentially exploitable

| CVE            | Vulnerability type | Affected versions | Privileges required | Fixed version | Assessment   |
| -------------- | ----------------- | -------- | ------------- | ----------- | ----------- |
| CVE-2024-36971 | route table UAF   | 5.10+    | low privilege | 5.10.220+   | ⚠️ needs verification |
| CVE-2024-0582  | io_uring mmap UAF | 5.10-6.4 | low privilege | included in 5.10.209 | 🔴 possibly unpatched |
| CVE-2023-6931  | perf_event heap overflow | 4.4-6.7 | low privilege | 5.10.205+   | ⚠️ needs verification |
| CVE-2023-4623  | hfsc qdisc UAF    | 2.6.25+  | CAP_NET_ADMIN | 5.10.192+   | ❌ privileges required |

### 🟡 Medium risk - conditional exploitation

| CVE           | Vulnerability type | Affected versions | Privileges required | Fixed version | Assessment |
| ------------- | -------------- | --------- | ------------- | --------- | --------- |
| CVE-2023-2163 | BPF Verifier   | 4.15-6.4  | low privilege | 5.10.179+ | ✅ patched |
| CVE-2023-3390 | nftables UAF   | 4.1-6.4   | CAP_NET_ADMIN | 5.10.186+ | ❌ privileges required |
| CVE-2023-1829 | tcindex UAF    | 4.14-6.3  | CAP_NET_ADMIN | 5.10.176+ | ❌ privileges required |
| CVE-2022-2586 | nft_object UAF | 3.16-5.19 | CAP_NET_ADMIN | 5.10.136+ | ❌ privileges required |

### 🟢 Low risk - difficult to exploit

| CVE            | Vulnerability type | Reason             |
| -------------- | ----------------- | ------------------ |
| CVE-2023-0179  | nftables stack overflow | requires CAP_NET_ADMIN |
| CVE-2022-34918 | nftables type confusion | requires CAP_NET_ADMIN |
| CVE-2022-2588  | route4 UAF        | requires CAP_NET_ADMIN |
| CVE-2022-2585  | POSIX timer UAF   | possibly already patched |

---

## Key Analysis

### CVE-2024-0582 (io_uring mmap UAF)

**Severity**: high  
**Exploitation difficulty**: medium

```
Vulnerability description:
The ring buffer mmap handling in io_uring contains a UAF vulnerability.
After the ring buffer is unmapped, the memory may be freed incorrectly,
while user space may still hold the mmap mapping.

Affected versions: 5.10 - 6.4
Fixed version: whether 5.10.209 includes the fix needs to be confirmed

Detection method:
1. Check whether io_uring is available
2. Try to create a ring buffer
3. Test the mmap/munmap order
```

**Quick detection**:

```bash
# check whether io_uring is available
ls -la /dev/io_uring
# or
cat /proc/sys/kernel/io_uring_disabled
```

### CVE-2023-6931 (perf_event heap overflow)

**Severity**: high  
**Exploitation difficulty**: medium

```
Vulnerability description:
The perf_read_group() function in the perf_event subsystem contains
a heap buffer overflow. When reading a perf event group,
if the group structure is specially crafted, an out-of-bounds write may result.

Affected versions: 4.4 - 6.7
Fixed version: 5.10.205+

Detection method:
1. Check whether perf_event_open is available
2. Set the perf_event_paranoid level
3. Try to create perf event groups
```

**Quick detection**:

```bash
# check perf permissions
cat /proc/sys/kernel/perf_event_paranoid
# larger values mean more restrictions: -1 = unrestricted, 0-3 = increasing restrictions

# try perf_event_open
strace -e perf_event_open perf stat ls 2>&1
```

### CVE-2024-36971 (route table UAF)

**Severity**: high  
**Exploitation difficulty**: high

```
Vulnerability description:
The dst_entry structure in the network route table contains a UAF.
When a route entry is marked as expired and GC is triggered,
a use-after-free may result if a user-space reference still exists.

Affected versions: 5.10+
Fixed version: needs to be confirmed

Current test results:
- BPF heap spray: ✅ success (16384 192-byte objects)
- UAF trigger: ❌ not successful
- A real ICMP error or routing operation is required
```

---

## Quick Detection Script

```c
// Linux 5.10.x vulnerability quick detection tool
// cve_5_10_x_scanner.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/perf_event.h>
#include <linux/io_uring.h>

// io_uring syscall numbers (ARM64)
#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter 426
#endif

int check_io_uring() {
    struct io_uring_params params;
    memset(&params, 0, sizeof(params));

    int fd = syscall(__NR_io_uring_setup, 8, &params);
    if (fd >= 0) {
        close(fd);
        return 1; // io_uring available
    }
    return 0;
}

int check_perf_event() {
    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_SOFTWARE;
    attr.config = PERF_COUNT_SW_CPU_CLOCK;
    attr.size = sizeof(attr);

    int fd = syscall(__NR_perf_event_open, &attr, 0, -1, -1, 0);
    if (fd >= 0) {
        close(fd);
        return 1; // perf_event available
    }
    return 0;
}

int check_bpf_available() {
    int fd = open("/proc/sys/kernel/unprivileged_bpf_disabled", O_RDONLY);
    if (fd < 0) return -1;

    char buf[8];
    read(fd, buf, sizeof(buf));
    close(fd);

    return (buf[0] == '0') ? 1 : 0;
}

int main() {
    printf("=== Linux 5.10.x 漏洞掃描器 ===\n\n");

    // io_uring detection
    printf("[1] io_uring 可用性: ");
    if (check_io_uring()) {
        printf("✅ 可用 - CVE-2024-0582 可能可利用\n");
    } else {
        printf("❌ 不可用或需要權限\n");
    }

    // perf_event detection
    printf("[2] perf_event 可用性: ");
    if (check_perf_event()) {
        printf("✅ 可用 - CVE-2023-6931 可能可利用\n");
    } else {
        printf("❌ 不可用或需要權限\n");
    }

    // BPF detection
    printf("[3] 非特權 BPF: ");
    int bpf = check_bpf_available();
    if (bpf == 1) {
        printf("✅ 啟用 - BPF 相關漏洞可測試\n");
    } else if (bpf == 0) {
        printf("❌ 禁用\n");
    } else {
        printf("⚠️ 無法檢測\n");
    }

    // kernel version
    printf("\n[4] 內核版本檢測:\n");
    system("uname -r");

    // perf paranoid
    printf("\n[5] Perf 權限級別:\n");
    system("cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo 'N/A'");

    // kptr_restrict
    printf("\n[6] 指標限制:\n");
    system("cat /proc/sys/kernel/kptr_restrict 2>/dev/null || echo 'N/A'");

    printf("\n=== 掃描完成 ===\n");
    return 0;
}
```

---

## Recommended Next Steps

### High priority

1. **CVE-2024-0582 (io_uring)**
   - Check whether io_uring is available
   - If available, attempt mmap UAF exploitation

2. **CVE-2023-6931 (perf_event)**
   - Check the perf_event_paranoid setting
   - If <= 2, attempt heap overflow exploitation

### Low priority

3. **Continue with CVE-2024-36971**
   - A UAF trigger method needs to be found
   - Further testing may require root

4. **CAP_NET_ADMIN vulnerabilities**
   - Root must be obtained first
   - Can be used for persistence or to bypass SELinux

---

## Conclusion

**~~The best path is still Fastboot + Magisk~~ (**2026-09-13: neither is feasible; root has been obtained via the GhostLock LPE**)**

The unlocked bootloader is the biggest advantage. Rather than spending time attempting complex kernel exploits,
flashing a patched boot.img directly through Fastboot is the most reliable option.

Kernel exploitation can serve as:

1. A research and learning exercise
2. A fallback option (if the Fastboot method fails)
3. Further privilege escalation after root (such as bypassing SELinux)

---

_Scan report generation time: **2026-01** (**originally written 2025-01, inconsistent with the project timeline; corrected 2026-09-13**)_
