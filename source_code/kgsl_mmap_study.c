/*
 * KGSL mmap Deep Study
 * =====================
 *
 * Problem: mmap fails with EINVAL because no GPU context exists
 * The hwsched mode blocks context creation: "legacy context switch not supported"
 *
 * Alternative strategies:
 * 1. Try different mmap offsets (not based on alloc ID)
 * 2. Look for other mappable resources (properties, device info)
 * 3. Analyze what offsets the driver accepts
 * 4. Try CPU-only allocation flags
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
#include <signal.h>
#include <setjmp.h>

#define KGSL_IOC_TYPE 0x09

// Flag definitions
#define KGSL_MEMFLAGS_GPUREADONLY 0x01000000
#define KGSL_MEMALIGN_SHIFT 16
#define KGSL_MEMTYPE_OBJECTANY 0

// Cachemode flags
#define KGSL_CACHEMODE_WRITECOMBINE 0x40000000
#define KGSL_CACHEMODE_UNCACHED 0x80000000
#define KGSL_CACHEMODE_WRITETHROUGH 0xC0000000

// Memory type flags
#define KGSL_MEMTYPE_KERNEL 0x100000
#define KGSL_MEMTYPE_COMMAND 0x200000

// CPU allocation flags
#define KGSL_MEMFLAGS_SECURE 0x00000008
#define KGSL_MEMFLAGS_USE_CPU_MAP 0x10000000

struct kgsl_gpumem_alloc_id
{
    unsigned int id;
    unsigned int flags;
    size_t size;
    size_t mmapsize;
    uint64_t gpuaddr;
};

struct kgsl_gpumem_free_id
{
    unsigned int id;
    unsigned int pad;
};

struct kgsl_gpumem_get_info
{
    uint64_t gpuaddr;
    unsigned int id;
    unsigned int flags;
    size_t size;
    size_t mmapsize;
    uint64_t useraddr;
};

#define IOCTL_KGSL_GPUMEM_ALLOC_ID _IOWR(KGSL_IOC_TYPE, 0x2F, struct kgsl_gpumem_alloc_id)
#define IOCTL_KGSL_GPUMEM_FREE_ID _IOW(KGSL_IOC_TYPE, 0x30, struct kgsl_gpumem_free_id)
#define IOCTL_KGSL_GPUMEM_GET_INFO _IOWR(KGSL_IOC_TYPE, 0x36, struct kgsl_gpumem_get_info)

static int g_fd = -1;

// ═══════════════════════════════════════════════════════════════════════════
// Test 1: Scan mmap offset space
// ═══════════════════════════════════════════════════════════════════════════

void test_mmap_offset_scan(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  TEST 1: Scan mmap Offset Space                                   ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    printf("[*] Trying various mmap offsets to find valid mappings...\n\n");

    // Various offsets to try
    off_t offsets[] = {
        0x0,                     // Zero
        0x1000,                  // 4KB
        0x2000,                  // 8KB
        0x10000,                 // 64KB
        0x100000,                // 1MB
        0x1000000,               // 16MB
        0x10000000,              // 256MB
        (off_t)0x100000000LL,    // 4GB
        (off_t)0xC0000000LL,     // Special region?
        (off_t)0xFC000000LL,     // MMIO region?
        (off_t)0x800000000000LL, // Large offset
    };

    for (int i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++)
    {
        void *ptr = mmap(NULL, 4096, PROT_READ, MAP_SHARED, g_fd, offsets[i]);

        if (ptr != MAP_FAILED)
        {
            printf("[+] Offset 0x%llx: SUCCESS at %p\n",
                   (unsigned long long)offsets[i], ptr);

            // Try to read first uint64_t
            uint64_t val = *(volatile uint64_t *)ptr;
            printf("    First 8 bytes: 0x%016llx\n", (unsigned long long)val);

            munmap(ptr, 4096);
        }
        else
        {
            printf("[-] Offset 0x%llx: %s\n",
                   (unsigned long long)offsets[i], strerror(errno));
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 2: Try different allocation flags
// ═══════════════════════════════════════════════════════════════════════════

void test_alloc_flags(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  TEST 2: Try Different Allocation Flags                           ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    struct
    {
        const char *name;
        unsigned int flags;
    } flag_tests[] = {
        {"Normal", (12 << KGSL_MEMALIGN_SHIFT)},
        {"WriteCombine", (12 << KGSL_MEMALIGN_SHIFT) | KGSL_CACHEMODE_WRITECOMBINE},
        {"Uncached", (12 << KGSL_MEMALIGN_SHIFT) | KGSL_CACHEMODE_UNCACHED},
        {"WriteThrough", (12 << KGSL_MEMALIGN_SHIFT) | KGSL_CACHEMODE_WRITETHROUGH},
        {"USE_CPU_MAP", (12 << KGSL_MEMALIGN_SHIFT) | KGSL_MEMFLAGS_USE_CPU_MAP},
        {"GPU_READONLY", (12 << KGSL_MEMALIGN_SHIFT) | KGSL_MEMFLAGS_GPUREADONLY},
        {"CPU_MAP+UNCACHED", (12 << KGSL_MEMALIGN_SHIFT) | KGSL_MEMFLAGS_USE_CPU_MAP | KGSL_CACHEMODE_UNCACHED},
        {"Large Align (20)", (20 << KGSL_MEMALIGN_SHIFT)}, // 1MB alignment
        {"Small Align (4)", (4 << KGSL_MEMALIGN_SHIFT)},   // 16B alignment
        {"No flags", 0},
    };

    for (int i = 0; i < sizeof(flag_tests) / sizeof(flag_tests[0]); i++)
    {
        struct kgsl_gpumem_alloc_id alloc = {
            .flags = flag_tests[i].flags,
            .size = 4096,
        };

        int ret = ioctl(g_fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc);

        if (ret != 0)
        {
            printf("[-] %-20s: Alloc failed (%s)\n",
                   flag_tests[i].name, strerror(errno));
            continue;
        }

        printf("[+] %-20s: id=%u, mmapsize=%zu\n",
               flag_tests[i].name, alloc.id, alloc.mmapsize);

        // Try mmap with the allocated ID
        off_t offset = (off_t)alloc.id << 12;
        void *ptr = mmap(NULL, alloc.mmapsize, PROT_READ | PROT_WRITE,
                         MAP_SHARED, g_fd, offset);

        if (ptr == MAP_FAILED)
        {
            printf("    mmap(offset=0x%llx): FAILED (%s)\n",
                   (unsigned long long)offset, strerror(errno));
        }
        else
        {
            printf("    mmap(offset=0x%llx): SUCCESS at %p\n",
                   (unsigned long long)offset, ptr);
            munmap(ptr, alloc.mmapsize);
        }

        struct kgsl_gpumem_free_id free_req = {.id = alloc.id};
        ioctl(g_fd, IOCTL_KGSL_GPUMEM_FREE_ID, &free_req);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 3: Device memory regions
// ═══════════════════════════════════════════════════════════════════════════

void test_device_regions(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  TEST 3: Try Known Device Memory Regions                          ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    // From earlier property leak, we know GPU shadow addr is 0xefff_f000
    // Try to mmap around that area

    printf("[*] Known: GPU shadow address from property = 0xeffffffff000\n");
    printf("[*] Trying to map device-related offsets...\n\n");

    // Adreno GPU MMIO regions typically around these addresses
    off_t regions[] = {
        0x0,
        0x1000,
        0x10000,
        0x5C00000, // Common Adreno register base
        0x5C10000,
        0x5C20000,
        (off_t)0xefff0000LL,
        (off_t)0xefffff000LL,
        (off_t)0xeffff000LL,
    };

    for (int i = 0; i < sizeof(regions) / sizeof(regions[0]); i++)
    {
        void *ptr = mmap(NULL, 4096, PROT_READ, MAP_SHARED, g_fd, regions[i]);

        if (ptr != MAP_FAILED)
        {
            printf("[+] Region 0x%llx: Mapped at %p\n",
                   (unsigned long long)regions[i], ptr);
            munmap(ptr, 4096);
        }
        else if (errno != EINVAL && errno != ENOMEM)
        {
            printf("[?] Region 0x%llx: %s (interesting error)\n",
                   (unsigned long long)regions[i], strerror(errno));
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 4: Check /dev/kgsl-3d0 with read()
// ═══════════════════════════════════════════════════════════════════════════

void test_direct_read(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  TEST 4: Direct read() on Device                                  ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    char buf[256];

    ssize_t n = read(g_fd, buf, sizeof(buf));

    if (n > 0)
    {
        printf("[+] read() returned %zd bytes!\n", n);
        printf("    Data: ");
        for (int i = 0; i < n && i < 64; i++)
        {
            printf("%02x ", (unsigned char)buf[i]);
        }
        printf("\n");
    }
    else if (n == 0)
    {
        printf("[-] read() returned 0 (EOF)\n");
    }
    else
    {
        printf("[-] read() failed: %s\n", strerror(errno));
    }

    // Try pread at different offsets
    printf("\n[*] Trying pread at various offsets...\n");

    off_t offsets[] = {0, 0x1000, 0x10000, 0x100000};
    for (int i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++)
    {
        n = pread(g_fd, buf, sizeof(buf), offsets[i]);
        if (n > 0)
        {
            printf("[+] pread(offset=0x%llx): %zd bytes\n",
                   (unsigned long long)offsets[i], n);
        }
        else if (errno != ESPIPE && errno != EINVAL)
        {
            printf("[?] pread(offset=0x%llx): %s\n",
                   (unsigned long long)offsets[i], strerror(errno));
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 5: Analyze what the mmap implementation checks
// ═══════════════════════════════════════════════════════════════════════════

void test_mmap_requirements(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  TEST 5: Understand mmap Requirements                             ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    printf("[*] KGSL mmap offset format: (entry_id << PAGE_SHIFT)\n");
    printf("[*] The driver checks if the entry exists and belongs to the process\n");
    printf("[*] Without a GPU context, entries cannot be properly bound\n\n");

    // Allocate and get info
    struct kgsl_gpumem_alloc_id alloc = {
        .flags = (12 << KGSL_MEMALIGN_SHIFT),
        .size = 4096,
    };

    if (ioctl(g_fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc) != 0)
    {
        printf("[-] Allocation failed\n");
        return;
    }

    printf("[+] Allocated: id=%u, mmapsize=%zu, gpuaddr=0x%llx\n",
           alloc.id, alloc.mmapsize, (unsigned long long)alloc.gpuaddr);

    // Get info to check useraddr
    struct kgsl_gpumem_get_info info = {.id = alloc.id};

    if (ioctl(g_fd, IOCTL_KGSL_GPUMEM_GET_INFO, &info) == 0)
    {
        printf("[*] Entry info:\n");
        printf("    id       = %u\n", info.id);
        printf("    flags    = 0x%08x\n", info.flags);
        printf("    size     = %zu\n", info.size);
        printf("    mmapsize = %zu\n", info.mmapsize);
        printf("    gpuaddr  = 0x%016llx\n", (unsigned long long)info.gpuaddr);
        printf("    useraddr = 0x%016llx\n", (unsigned long long)info.useraddr);

        if (info.useraddr == 0)
        {
            printf("\n[!] useraddr is 0 - memory not mapped to userspace yet\n");
            printf("    mmap must succeed first to populate useraddr\n");
        }
    }

    // Try mmap with exact mmapsize
    printf("\n[*] Attempting mmap with exact mmapsize...\n");

    off_t offset = (off_t)alloc.id << 12;

    // Different protection flags
    int prots[] = {PROT_READ, PROT_READ | PROT_WRITE, PROT_NONE};
    const char *prot_names[] = {"READ", "READ|WRITE", "NONE"};

    for (int i = 0; i < 3; i++)
    {
        void *ptr = mmap(NULL, alloc.mmapsize, prots[i], MAP_SHARED, g_fd, offset);
        if (ptr != MAP_FAILED)
        {
            printf("[+] PROT_%s: SUCCESS at %p\n", prot_names[i], ptr);
            munmap(ptr, alloc.mmapsize);
        }
        else
        {
            printf("[-] PROT_%s: %s\n", prot_names[i], strerror(errno));
        }
    }

    // Try MAP_PRIVATE
    printf("\n[*] Trying MAP_PRIVATE...\n");
    void *ptr = mmap(NULL, alloc.mmapsize, PROT_READ, MAP_PRIVATE, g_fd, offset);
    if (ptr != MAP_FAILED)
    {
        printf("[+] MAP_PRIVATE: SUCCESS at %p\n", ptr);
        munmap(ptr, alloc.mmapsize);
    }
    else
    {
        printf("[-] MAP_PRIVATE: %s\n", strerror(errno));
    }

    struct kgsl_gpumem_free_id free_req = {.id = alloc.id};
    ioctl(g_fd, IOCTL_KGSL_GPUMEM_FREE_ID, &free_req);
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 6: Check other KGSL devices
// ═══════════════════════════════════════════════════════════════════════════

void test_other_devices(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  TEST 6: Check Other KGSL/Graphics Devices                        ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    const char *devices[] = {
        "/dev/kgsl-3d0",
        "/dev/kgsl",
        "/dev/mali0",
        "/dev/dri/card0",
        "/dev/dri/renderD128",
        "/dev/graphics/fb0",
        "/dev/ion",
        "/dev/dma_heap/system",
        "/dev/dma_heap/reserved",
        "/dev/dma_heap/carveout_heap",
        "/dev/ashmen",
        "/dev/ashmem",
    };

    for (int i = 0; i < sizeof(devices) / sizeof(devices[0]); i++)
    {
        int fd = open(devices[i], O_RDWR);
        if (fd >= 0)
        {
            printf("[+] %s: Opened (fd=%d)\n", devices[i], fd);

            // Try mmap at offset 0
            void *ptr = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);
            if (ptr != MAP_FAILED)
            {
                printf("    mmap(0): SUCCESS at %p\n", ptr);
                munmap(ptr, 4096);
            }
            else
            {
                printf("    mmap(0): %s\n", strerror(errno));
            }

            close(fd);
        }
        else if (errno == ENOENT)
        {
            // Device doesn't exist, skip
        }
        else
        {
            printf("[-] %s: %s\n", devices[i], strerror(errno));
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

int main(void)
{
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║  KGSL mmap Deep Study                                              ║\n");
    printf("║  Understanding the mmap requirements                               ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n\n");

    printf("[*] UID=%d, EUID=%d\n", getuid(), geteuid());

    g_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (g_fd < 0)
    {
        printf("[-] Failed to open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Opened /dev/kgsl-3d0 (fd=%d)\n", g_fd);

    test_mmap_offset_scan();
    test_alloc_flags();
    test_device_regions();
    test_direct_read();
    test_mmap_requirements();
    test_other_devices();

    close(g_fd);

    printf("\n[*] mmap study complete\n\n");

    return 0;
}
