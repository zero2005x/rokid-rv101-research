/*
 * kgsl_deep_probe.c - in-depth analysis of the KGSL ioctl commands
 *
 * ioctls found to succeed:
 * 0xc1000938, 0xc1000939, 0xc100093a, 0xc1000940, 0xc1000956, 0xc1000958
 *
 * Analyzing the ioctl numbers:
 * 0xc1000938 = _IOWR(0x09, 0x38, 256_bytes) - this may be some kind of query
 *
 * Build:
 * aarch64-linux-android31-clang -static -O2 -o kgsl_deep_probe kgsl_deep_probe.c
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

/*
 * ioctl command decoding:
 * bits 0-7: command number
 * bits 8-15: magic number (0x09 for KGSL)
 * bits 16-29: size
 * bits 30-31: direction (00=none, 01=write, 10=read, 11=read+write)
 *
 * 0xc1000938:
 *   Direction: 11 (R/W)
 *   Size: 0x100 = 256 bytes
 *   Magic: 0x09
 *   Number: 0x38 = 56
 */

/* commands known to succeed */
#define IOCTL_0x38 0xc1000938 // 256 bytes, nr=56
#define IOCTL_0x39 0xc1000939 // 256 bytes, nr=57
#define IOCTL_0x3a 0xc100093a // 256 bytes, nr=58
#define IOCTL_0x40 0xc1000940 // 256 bytes, nr=64
#define IOCTL_0x56 0xc1000956 // 256 bytes, nr=86
#define IOCTL_0x58 0xc1000958 // 256 bytes, nr=88

/* structures used for testing */
struct generic_ioctl_data
{
    uint64_t data[32]; // 256 bytes
};

void hexdump(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < size; i++)
    {
        if (i % 16 == 0)
            printf("    %04zx: ", i);
        printf("%02x ", bytes[i]);
        if (i % 16 == 15)
            printf("\n");
    }
    if (size % 16 != 0)
        printf("\n");
}

void analyze_successful_ioctl(int fd, unsigned int cmd, const char *name)
{
    struct generic_ioctl_data data = {0};

    printf("\n[*] Analyzing %s (0x%08x)...\n", name, cmd);

    errno = 0;
    int ret = ioctl(fd, cmd, &data);
    printf("  Result: ret=%d, errno=%d\n", ret, errno);

    if (ret >= 0)
    {
        // Check whether there is any non-zero data
        int has_data = 0;
        for (int i = 0; i < 32; i++)
        {
            if (data.data[i] != 0)
            {
                has_data = 1;
                break;
            }
        }

        if (has_data)
        {
            printf("  Output data:\n");
            hexdump(&data, 64); // only show the first 64 bytes

            // Try to interpret it as meaningful values
            printf("  As uint64_t[8]: ");
            for (int i = 0; i < 8; i++)
            {
                printf("0x%llx ", (unsigned long long)data.data[i]);
            }
            printf("\n");
        }
        else
        {
            printf("  No output data (all zeros)\n");
        }
    }
}

void test_gpumem_operations(int fd)
{
    printf("\n=== Testing GPU memory operations ===\n");

    /* KGSL_IOCTL_GPUMEM_ALLOC_ID = 0x3d */
    struct
    {
        unsigned int id;
        unsigned int flags;
        uint64_t size;
        uint64_t mmapsize;
        uint64_t gpuaddr;
    } alloc = {0};

    alloc.size = 4096;
    alloc.flags = 0;

    unsigned int cmd = 0xc024093d; // the original GPUMEM_ALLOC_ID command
    errno = 0;
    int ret = ioctl(fd, cmd, &alloc);
    printf("[*] GPUMEM_ALLOC_ID (0xc024093d): ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));

    if (ret >= 0)
    {
        printf("  [+] Allocated! id=%u, gpuaddr=0x%llx, mmapsize=0x%llx\n",
               alloc.id, (unsigned long long)alloc.gpuaddr,
               (unsigned long long)alloc.mmapsize);
    }

    /* Try GPUOBJ_ALLOC */
    struct
    {
        uint64_t size;
        uint64_t flags;
        uint64_t va_base;
        uint64_t va_len;
        unsigned int id;
        unsigned int metadata_len;
        uint64_t metadata;
        uint64_t padding;
    } obj_alloc = {0};

    obj_alloc.size = 4096;

    cmd = 0xc0500951; // GPUOBJ_ALLOC
    errno = 0;
    ret = ioctl(fd, cmd, &obj_alloc);
    printf("[*] GPUOBJ_ALLOC (0xc0500951): ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));
}

void try_kgsl_mmap(int fd)
{
    printf("\n=== Testing KGSL mmap ===\n");

    /* Try to mmap the GPU driver directly */
    void *map = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map != MAP_FAILED)
    {
        printf("[+] mmap(offset=0) succeeded at %p\n", map);

        // Try to read
        printf("  First 16 bytes: ");
        for (int i = 0; i < 16; i++)
        {
            printf("%02x ", ((uint8_t *)map)[i]);
        }
        printf("\n");

        munmap(map, 4096);
    }
    else
    {
        printf("[-] mmap(offset=0) failed: %s\n", strerror(errno));
    }

    // Try different offsets
    for (int i = 1; i <= 16; i++)
    {
        map = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, i * 4096);
        if (map != MAP_FAILED)
        {
            printf("[+] mmap(offset=%d pages) succeeded at %p\n", i, map);
            munmap(map, 4096);
        }
    }
}

void scan_deep_ioctls(int fd)
{
    printf("\n=== Deep ioctl scan (smaller structures) ===\n");

    char buf[512] = {0};

    /* Scan commands that use an 8-64 byte structure */
    unsigned int sizes[] = {4, 8, 12, 16, 20, 24, 32, 40, 48, 64, 0};

    for (int s = 0; sizes[s] != 0; s++)
    {
        unsigned int size = sizes[s];
        printf("\n[*] Size=%d bytes:\n", size);

        int found = 0;
        for (int nr = 0x00; nr <= 0x80 && found < 10; nr++)
        {
            unsigned int cmd = 0xc0000000 | (size << 16) | (0x09 << 8) | nr;

            memset(buf, 0, sizeof(buf));
            errno = 0;
            int ret = ioctl(fd, cmd, buf);

            if (errno != 25 && errno != 22)
            { // exclude ENOTTY and EINVAL
                printf("  nr=0x%02x: ret=%d, errno=%d (%s)\n",
                       nr, ret, errno, strerror(errno));
                found++;
            }
        }
    }
}

int main(int argc, char **argv)
{
    printf("=== KGSL Deep Probe ===\n");
    printf("Analyzing successful ioctl commands\n");
    printf("====================================\n");

    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
    {
        printf("[-] Cannot open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Opened /dev/kgsl-3d0: fd=%d\n", fd);

    // Analyze the successful ioctls
    analyze_successful_ioctl(fd, IOCTL_0x38, "IOCTL_0x38");
    analyze_successful_ioctl(fd, IOCTL_0x39, "IOCTL_0x39");
    analyze_successful_ioctl(fd, IOCTL_0x3a, "IOCTL_0x3a");
    analyze_successful_ioctl(fd, IOCTL_0x40, "IOCTL_0x40");
    analyze_successful_ioctl(fd, IOCTL_0x56, "IOCTL_0x56");
    analyze_successful_ioctl(fd, IOCTL_0x58, "IOCTL_0x58");

    // Test GPU memory operations
    test_gpumem_operations(fd);

    // Test mmap
    try_kgsl_mmap(fd);

    // Deep scan
    scan_deep_ioctls(fd);

    close(fd);
    return 0;
}
