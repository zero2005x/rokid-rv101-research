/*
 * cve_5_10_x_scanner.c
 * Linux 5.10.x 漏洞快速掃描工具
 * 檢測 io_uring、perf_event、BPF 的可用性
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/mman.h>

/*
 * io_uring 系統調用號 (ARM64)
 */
#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter 426
#endif
#ifndef __NR_io_uring_register
#define __NR_io_uring_register 427
#endif

/*
 * perf_event_open 系統調用號 (ARM64)
 */
#ifndef __NR_perf_event_open
#define __NR_perf_event_open 241
#endif

/*
 * io_uring 結構體定義
 */
struct io_uring_params
{
    __u32 sq_entries;
    __u32 cq_entries;
    __u32 flags;
    __u32 sq_thread_cpu;
    __u32 sq_thread_idle;
    __u32 features;
    __u32 wq_fd;
    __u32 resv[3];
    struct
    {
        __u32 head;
        __u32 tail;
        __u32 ring_mask;
        __u32 ring_entries;
        __u32 flags;
        __u32 dropped;
        __u32 array;
        __u32 resv1;
        __u64 resv2;
    } sq_off;
    struct
    {
        __u32 head;
        __u32 tail;
        __u32 ring_mask;
        __u32 ring_entries;
        __u32 overflow;
        __u32 cqes;
        __u32 flags;
        __u32 resv1;
        __u64 resv2;
    } cq_off;
};

/*
 * perf_event_attr 結構體
 */
struct perf_event_attr
{
    __u32 type;
    __u32 size;
    __u64 config;
    union
    {
        __u64 sample_period;
        __u64 sample_freq;
    };
    __u64 sample_type;
    __u64 read_format;
    __u64 flags_bits;
    __u32 wakeup_events;
    __u32 bp_type;
    union
    {
        __u64 bp_addr;
        __u64 config1;
    };
    union
    {
        __u64 bp_len;
        __u64 config2;
    };
    __u64 branch_sample_type;
    __u64 sample_regs_user;
    __u32 sample_stack_user;
    __s32 clockid;
    __u64 sample_regs_intr;
    __u32 aux_watermark;
    __u16 sample_max_stack;
    __u16 reserved_2;
};

#define PERF_TYPE_SOFTWARE 1
#define PERF_COUNT_SW_CPU_CLOCK 0

/*
 * 檢測 io_uring 可用性
 */
int check_io_uring(void)
{
    struct io_uring_params params;
    memset(&params, 0, sizeof(params));

    printf("[io_uring] 嘗試 io_uring_setup(8, &params)...\n");

    long fd = syscall(__NR_io_uring_setup, 8, &params);

    if (fd >= 0)
    {
        printf("[io_uring] ✅ 成功! fd=%ld\n", fd);
        printf("[io_uring] sq_entries=%u, cq_entries=%u\n",
               params.sq_entries, params.cq_entries);
        printf("[io_uring] features=0x%x\n", params.features);

        // 嘗試 mmap SQ ring
        size_t sq_size = params.sq_off.array + params.sq_entries * sizeof(__u32);
        void *sq_ptr = mmap(NULL, sq_size, PROT_READ | PROT_WRITE,
                            MAP_SHARED | MAP_POPULATE, fd, 0);

        if (sq_ptr != MAP_FAILED)
        {
            printf("[io_uring] ✅ SQ ring mmap 成功! addr=%p, size=%zu\n",
                   sq_ptr, sq_size);
            munmap(sq_ptr, sq_size);
        }
        else
        {
            printf("[io_uring] ❌ SQ ring mmap 失敗: %s\n", strerror(errno));
        }

        close(fd);
        return 1;
    }
    else
    {
        printf("[io_uring] ❌ 失敗: %s (errno=%d)\n", strerror(errno), errno);
        return 0;
    }
}

/*
 * 檢測 perf_event 可用性
 */
int check_perf_event(void)
{
    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_SOFTWARE;
    attr.size = sizeof(attr);
    attr.config = PERF_COUNT_SW_CPU_CLOCK;

    printf("[perf_event] 嘗試 perf_event_open()...\n");

    long fd = syscall(__NR_perf_event_open, &attr, 0, -1, -1, 0);

    if (fd >= 0)
    {
        printf("[perf_event] ✅ 成功! fd=%ld\n", fd);

        // 嘗試讀取計數器
        long long count;
        if (read(fd, &count, sizeof(count)) == sizeof(count))
        {
            printf("[perf_event] ✅ 計數器值: %lld\n", count);
        }

        close(fd);
        return 1;
    }
    else
    {
        printf("[perf_event] ❌ 失敗: %s (errno=%d)\n", strerror(errno), errno);
        return 0;
    }
}

/*
 * 檢測 BPF 可用性
 */
int check_bpf(void)
{
    int fd = open("/proc/sys/kernel/unprivileged_bpf_disabled", O_RDONLY);
    if (fd < 0)
    {
        printf("[BPF] ⚠️ 無法讀取 unprivileged_bpf_disabled\n");
        return -1;
    }

    char buf[16];
    memset(buf, 0, sizeof(buf));
    read(fd, buf, sizeof(buf) - 1);
    close(fd);

    int val = atoi(buf);
    printf("[BPF] unprivileged_bpf_disabled = %d\n", val);

    if (val == 0)
    {
        printf("[BPF] ✅ 非特權 BPF 啟用!\n");
        return 1;
    }
    else if (val == 1)
    {
        printf("[BPF] ❌ 非特權 BPF 禁用\n");
        return 0;
    }
    else if (val == 2)
    {
        printf("[BPF] ❌ BPF 完全禁用\n");
        return 0;
    }

    return -1;
}

/*
 * 讀取 sysctl 值
 */
int read_sysctl_int(const char *path, int *value)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    char buf[32];
    memset(buf, 0, sizeof(buf));
    read(fd, buf, sizeof(buf) - 1);
    close(fd);

    *value = atoi(buf);
    return 0;
}

/*
 * 顯示系統安全配置
 */
void show_security_config(void)
{
    int value;

    printf("\n=== 系統安全配置 ===\n");

    if (read_sysctl_int("/proc/sys/kernel/perf_event_paranoid", &value) == 0)
    {
        printf("perf_event_paranoid = %d", value);
        switch (value)
        {
        case -1:
            printf(" (無限制)\n");
            break;
        case 0:
            printf(" (允許原始追蹤)\n");
            break;
        case 1:
            printf(" (限制 CPU 事件)\n");
            break;
        case 2:
            printf(" (不允許內核追蹤)\n");
            break;
        case 3:
            printf(" (不允許任何測量) ❌\n");
            break;
        default:
            printf("\n");
        }
    }
    else
    {
        printf("perf_event_paranoid = N/A\n");
    }

    if (read_sysctl_int("/proc/sys/kernel/kptr_restrict", &value) == 0)
    {
        printf("kptr_restrict = %d", value);
        switch (value)
        {
        case 0:
            printf(" (無限制) ⚠️\n");
            break;
        case 1:
            printf(" (需要 CAP_SYSLOG)\n");
            break;
        case 2:
            printf(" (完全隱藏) ❌\n");
            break;
        default:
            printf("\n");
        }
    }
    else
    {
        printf("kptr_restrict = N/A\n");
    }

    if (read_sysctl_int("/proc/sys/kernel/randomize_va_space", &value) == 0)
    {
        printf("randomize_va_space = %d", value);
        switch (value)
        {
        case 0:
            printf(" (ASLR 禁用) ⚠️\n");
            break;
        case 1:
            printf(" (部分 ASLR)\n");
            break;
        case 2:
            printf(" (完整 ASLR)\n");
            break;
        default:
            printf("\n");
        }
    }
    else
    {
        printf("randomize_va_space = N/A\n");
    }

    // SELinux 狀態
    int selinux_fd = open("/sys/fs/selinux/enforce", O_RDONLY);
    if (selinux_fd >= 0)
    {
        char buf[8];
        read(selinux_fd, buf, sizeof(buf));
        close(selinux_fd);
        printf("SELinux = %s\n", buf[0] == '1' ? "Enforcing" : "Permissive ⚠️");
    }
    else
    {
        printf("SELinux = N/A (可能禁用)\n");
    }
}

/*
 * 嘗試 CVE-2024-0582 (io_uring mmap UAF) 的初步探測
 */
int probe_cve_2024_0582(void)
{
    printf("\n=== CVE-2024-0582 探測 (io_uring mmap UAF) ===\n");

    struct io_uring_params params;
    memset(&params, 0, sizeof(params));

    long fd = syscall(__NR_io_uring_setup, 64, &params);
    if (fd < 0)
    {
        printf("[CVE-2024-0582] ❌ io_uring 不可用\n");
        return 0;
    }

    printf("[CVE-2024-0582] io_uring fd=%ld\n", fd);
    printf("[CVE-2024-0582] sq_entries=%u, cq_entries=%u\n",
           params.sq_entries, params.cq_entries);

    // 計算 ring buffer 大小
    size_t sq_ring_size = params.sq_off.array + params.sq_entries * sizeof(__u32);
    size_t cq_ring_size = params.cq_off.cqes + params.cq_entries * 16;

    printf("[CVE-2024-0582] 預估 SQ ring size: %zu\n", sq_ring_size);
    printf("[CVE-2024-0582] 預估 CQ ring size: %zu\n", cq_ring_size);

    // 嘗試 mmap
    void *sq_ring = mmap(NULL, sq_ring_size, PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_POPULATE, fd, 0);

    if (sq_ring == MAP_FAILED)
    {
        printf("[CVE-2024-0582] ❌ SQ mmap 失敗: %s\n", strerror(errno));
        close(fd);
        return 0;
    }

    printf("[CVE-2024-0582] ✅ SQ ring mmap: %p\n", sq_ring);

    // 關閉 fd，但保留 mmap
    close(fd);
    printf("[CVE-2024-0582] 已關閉 fd，但保留 mmap...\n");

    // 嘗試訪問 mmap 區域 (如果存在 UAF，這可能觸發問題)
    // 警告：這只是探測，不是實際利用
    volatile unsigned int *ptr = (volatile unsigned int *)sq_ring;
    unsigned int val = *ptr;
    printf("[CVE-2024-0582] 讀取 mmap 區域: 0x%08x\n", val);

    // 清理
    munmap(sq_ring, sq_ring_size);
    printf("[CVE-2024-0582] 已 munmap\n");

    printf("[CVE-2024-0582] ⚠️ io_uring 可用，CVE-2024-0582 可能可利用\n");
    printf("[CVE-2024-0582] 需要進一步構造利用程式驗證\n");

    return 1;
}

int main(void)
{
    printf("============================================\n");
    printf("   Linux 5.10.x 漏洞掃描工具\n");
    printf("   Target: Rokid AR Glasses\n");
    printf("============================================\n\n");

    // 基本系統資訊
    printf("=== 系統資訊 ===\n");
    system("uname -a");
    printf("UID: %d, EUID: %d\n", getuid(), geteuid());
    printf("\n");

    // 安全配置
    show_security_config();
    printf("\n");

    // 功能檢測
    printf("=== 功能檢測 ===\n");

    printf("\n[1] io_uring 檢測:\n");
    int io_uring_ok = check_io_uring();

    printf("\n[2] perf_event 檢測:\n");
    int perf_ok = check_perf_event();

    printf("\n[3] BPF 檢測:\n");
    int bpf_ok = check_bpf();

    // CVE 探測
    if (io_uring_ok)
    {
        probe_cve_2024_0582();
    }

    // 總結
    printf("\n============================================\n");
    printf("   掃描結果總結\n");
    printf("============================================\n");
    printf("io_uring:     %s\n", io_uring_ok ? "✅ 可用" : "❌ 不可用");
    printf("perf_event:   %s\n", perf_ok ? "✅ 可用" : "❌ 不可用");
    printf("BPF:          %s\n", bpf_ok > 0 ? "✅ 可用" : "❌ 不可用");

    printf("\n可能可利用的漏洞:\n");
    if (io_uring_ok)
    {
        printf("  - CVE-2024-0582 (io_uring mmap UAF) ⚠️\n");
    }
    if (bpf_ok > 0)
    {
        printf("  - CVE-2024-36971 (路由表 UAF + BPF spray) ⚠️\n");
    }

    printf("\n不可利用 (需要權限或已禁用):\n");
    if (!perf_ok)
    {
        printf("  - CVE-2023-6931 (perf_event 堆溢出) - perf_event_paranoid=3\n");
    }

    printf("\n最佳建議: 使用 Fastboot + Magisk (Bootloader 已解鎖)\n");
    printf("============================================\n");

    return 0;
}
