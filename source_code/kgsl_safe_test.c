/*
 * kgsl_safe_test.c - 安全版本的 KGSL 測試
 *
 * 分步測試，避免 crash
 *
 * 編譯:
 * aarch64-linux-android31-clang -static -O2 -o kgsl_safe_test kgsl_safe_test.c
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
    else
    {
        return -1;
    }
}

struct kgsl_gpumem_alloc
{
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
    uint32_t padding;
};

int main(int argc, char **argv)
{
    printf("=== KGSL Safe Test ===\n");

    if (argc < 2)
    {
        printf("Usage: %s <test_num>\n", argv[0]);
        printf("  1: Basic alloc/mmap\n");
        printf("  2: Multiple allocs\n");
        printf("  3: fd close test (DANGEROUS)\n");
        printf("  4: Large alloc test\n");
        printf("  5: Free ioctl scan\n");
        return 1;
    }

    int test = atoi(argv[1]);

    // 設置信號處理
    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);

    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
    {
        printf("[-] Cannot open: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Opened fd=%d\n", fd);

    if (test == 1)
    {
        printf("\n=== Test 1: Basic Alloc/Mmap ===\n");

        struct kgsl_gpumem_alloc req = {0};
        req.size = 0x1000;

        if (ioctl(fd, IOCTL_GPUMEM_ALLOC, &req) < 0)
        {
            printf("[-] Alloc failed\n");
            goto out;
        }
        printf("[+] Allocated gpuaddr=0x%lx\n", req.gpuaddr);

        void *ptr = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, req.gpuaddr);
        if (ptr == MAP_FAILED)
        {
            printf("[-] mmap failed\n");
            goto out;
        }
        printf("[+] mmap ptr=%p\n", ptr);

        // 安全讀寫
        *(volatile uint32_t *)ptr = 0xDEADBEEF;
        uint32_t val = *(volatile uint32_t *)ptr;
        printf("[+] Write/Read: 0x%08x\n", val);

        munmap(ptr, 0x1000);
        printf("[+] Test 1 passed!\n");
    }
    else if (test == 2)
    {
        printf("\n=== Test 2: Multiple Allocs ===\n");

        for (int i = 0; i < 10; i++)
        {
            struct kgsl_gpumem_alloc req = {0};
            req.size = 0x1000;

            if (ioctl(fd, IOCTL_GPUMEM_ALLOC, &req) < 0)
            {
                printf("[-] Alloc %d failed\n", i);
                break;
            }
            printf("[+] Alloc %d: gpuaddr=0x%lx\n", i, req.gpuaddr);
        }
        printf("[+] Test 2 complete\n");
    }
    else if (test == 3)
    {
        printf("\n=== Test 3: FD Close Test ===\n");
        printf("[!] This may crash the system!\n");

        // 新的 fd
        int fd2 = open("/dev/kgsl-3d0", O_RDWR);
        if (fd2 < 0)
        {
            printf("[-] Cannot open fd2\n");
            goto out;
        }

        struct kgsl_gpumem_alloc req = {0};
        req.size = 0x1000;

        if (ioctl(fd2, IOCTL_GPUMEM_ALLOC, &req) < 0)
        {
            printf("[-] Alloc on fd2 failed\n");
            close(fd2);
            goto out;
        }
        printf("[+] fd2 alloc: gpuaddr=0x%lx\n", req.gpuaddr);

        void *ptr = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd2, req.gpuaddr);
        if (ptr == MAP_FAILED)
        {
            printf("[-] mmap failed\n");
            close(fd2);
            goto out;
        }
        printf("[+] mmap ptr=%p\n", ptr);

        // 寫入標記
        *(volatile uint64_t *)ptr = 0xCAFEBABE;
        printf("[*] Wrote marker\n");

        // 關閉 fd2
        printf("[*] Closing fd2...\n");
        close(fd2);
        printf("[*] fd2 closed\n");

        // 嘗試讀取
        printf("[*] Attempting read...\n");
        uint64_t val;
        if (safe_read(ptr, &val) == 0)
        {
            printf("[+] Can still read: 0x%lx\n", val);
            if (val == 0xCAFEBABE)
            {
                printf("[*] Data intact\n");
            }
            else
            {
                printf("[!] Data changed to 0x%lx\n", val);
            }
        }
        else
        {
            printf("[!] Read crashed! Signal %d\n", sig_received);
        }

        munmap(ptr, 0x1000);
        printf("[+] Test 3 complete\n");
    }
    else if (test == 4)
    {
        printf("\n=== Test 4: Large Alloc ===\n");

        uint64_t sizes[] = {0x100000, 0x1000000, 0x10000000};

        for (int i = 0; i < 3; i++)
        {
            struct kgsl_gpumem_alloc req = {0};
            req.size = sizes[i];

            errno = 0;
            int ret = ioctl(fd, IOCTL_GPUMEM_ALLOC, &req);
            printf("[*] Alloc 0x%lx: ret=%d, errno=%d, addr=0x%lx\n",
                   sizes[i], ret, errno, req.gpuaddr);
        }
        printf("[+] Test 4 complete\n");
    }
    else if (test == 5)
    {
        printf("\n=== Test 5: Free ioctl Scan ===\n");

        // 先分配一些
        struct kgsl_gpumem_alloc req = {0};
        req.size = 0x1000;
        ioctl(fd, IOCTL_GPUMEM_ALLOC, &req);
        printf("[+] Allocated: 0x%lx\n", req.gpuaddr);

        // 掃描可能的 free ioctl (只測試安全的)
        int found_free = 0;

        // 常見的 free ioctl 編號: 0x30, 0x31, 0x32, 0x34
        for (int nr = 0x30; nr <= 0x40; nr++)
        {
            uint64_t data[4] = {req.gpuaddr, 0x1000, 0, 0};

            // 只讀 ioctl
            int cmd_r = (0x80 << 24) | (8 << 16) | (0x09 << 8) | nr;
            errno = 0;
            int ret = ioctl(fd, cmd_r, data);
            if (ret == 0 || (ret < 0 && errno != 25))
            {
                printf("[+] READ nr=0x%02x: ret=%d, errno=%d\n", nr, ret, errno);
            }

            // 只寫 ioctl
            int cmd_w = (0x40 << 24) | (8 << 16) | (0x09 << 8) | nr;
            errno = 0;
            ret = ioctl(fd, cmd_w, data);
            if (ret == 0)
            {
                printf("[!!!] WRITE nr=0x%02x: WORKED! (potential free)\n", nr);
                found_free = 1;
            }
        }

        if (!found_free)
        {
            printf("[*] No direct free ioctl found\n");
        }
        printf("[+] Test 5 complete\n");
    }

out:
    close(fd);
    return 0;
}
