/*
 * Simple Binder/Ashmem Test
 * Test each operation step by step to find the crash point
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/ashmem.h>

// Binder IOCTL
#define BINDER_VERSION _IOWR('b', 9, uint32_t)
#define BINDER_SET_MAX_THREADS _IOW('b', 5, uint32_t)

typedef uint64_t binder_size_t;
typedef uint64_t binder_uintptr_t;

struct binder_write_read
{
    binder_size_t write_size;
    binder_size_t write_consumed;
    binder_uintptr_t write_buffer;
    binder_size_t read_size;
    binder_size_t read_consumed;
    binder_uintptr_t read_buffer;
};

#define BINDER_WRITE_READ _IOWR('b', 1, struct binder_write_read)
#define BC_ENTER_LOOPER _IO('c', 12)

int main(int argc, char **argv)
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("=== Simple Binder/Ashmem Test ===\n\n");

    // Test 1: Open devices
    printf("[1] Opening devices...\n");

    int kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    printf("    kgsl_fd = %d (errno=%d)\n", kgsl_fd, kgsl_fd < 0 ? errno : 0);

    int binder_fd = open("/dev/binder", O_RDWR);
    printf("    binder_fd = %d (errno=%d)\n", binder_fd, binder_fd < 0 ? errno : 0);

    int ashmem_fd = open("/dev/ashmem", O_RDWR);
    printf("    ashmem_fd = %d (errno=%d)\n", ashmem_fd, ashmem_fd < 0 ? errno : 0);

    // Test 2: Binder version
    printf("\n[2] Binder version...\n");
    if (binder_fd >= 0)
    {
        int version = 0;
        int ret = ioctl(binder_fd, BINDER_VERSION, &version);
        printf("    version=%d ret=%d\n", version, ret);
    }

    // Test 3: Binder mmap (this may be the problem point)
    printf("\n[3] Binder mmap...\n");
    void *binder_map = NULL;
    if (binder_fd >= 0)
    {
        // Try a smaller mapping size
        size_t map_size = 0x1000; // 4KB first
        printf("    Trying mmap size=0x%zx...\n", map_size);

        binder_map = mmap(NULL, map_size, PROT_READ, MAP_PRIVATE, binder_fd, 0);
        if (binder_map == MAP_FAILED)
        {
            printf("    mmap FAILED: errno=%d (%s)\n", errno, strerror(errno));
            binder_map = NULL;
        }
        else
        {
            printf("    mmap SUCCESS: addr=%p\n", binder_map);

            // Do not read the contents, only map
            printf("    (not reading from map yet)\n");
        }
    }

    // Test 4: Binder write-read (simplified)
    printf("\n[4] Binder write-read (empty)...\n");
    if (binder_fd >= 0 && binder_map)
    {
        struct binder_write_read bwr = {0};
        // Write nothing, only test the IOCTL
        int ret = ioctl(binder_fd, BINDER_WRITE_READ, &bwr);
        printf("    ret=%d errno=%d\n", ret, errno);
    }

    // Test 5: Ashmem operations
    printf("\n[5] Ashmem operations...\n");
    void *ashmem_map = NULL;
    if (ashmem_fd >= 0)
    {
        // Set size
        int ret = ioctl(ashmem_fd, ASHMEM_SET_SIZE, 0x1000);
        printf("    SET_SIZE ret=%d\n", ret);

        // Map
        ashmem_map = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                          MAP_SHARED, ashmem_fd, 0);
        if (ashmem_map == MAP_FAILED)
        {
            printf("    mmap FAILED: errno=%d\n", errno);
            ashmem_map = NULL;
        }
        else
        {
            printf("    mmap SUCCESS: addr=%p\n", ashmem_map);

            // Write test
            *(uint64_t *)ashmem_map = 0x4141414141414141ULL;
            uint64_t val = *(uint64_t *)ashmem_map;
            printf("    Read back: 0x%lx\n", val);
        }
    }

    // Test 6: KGSL allocation
    printf("\n[6] KGSL allocation...\n");
    if (kgsl_fd >= 0)
    {
        struct
        {
            uint64_t gpuaddr;
            uint64_t size;
            uint32_t flags;
            uint32_t padding;
        } alloc = {0};
        alloc.size = 0x1000;

        int ret = ioctl(kgsl_fd, 0xc018092f, &alloc);
        printf("    ret=%d gpuaddr=0x%lx\n", ret, alloc.gpuaddr);
    }

    // Cleanup
    printf("\n[7] Cleanup...\n");
    if (ashmem_map)
    {
        munmap(ashmem_map, 0x1000);
        printf("    ashmem unmapped\n");
    }
    if (binder_map)
    {
        munmap(binder_map, 0x1000);
        printf("    binder unmapped\n");
    }
    if (ashmem_fd >= 0)
        close(ashmem_fd);
    if (binder_fd >= 0)
        close(binder_fd);
    if (kgsl_fd >= 0)
        close(kgsl_fd);

    printf("\n=== All tests passed ===\n");
    return 0;
}
