/*
 * ADSPRPC Read-Only Exploration + DMA-BUF Attack
 *
 * Since adsprpc is readable, let's see what we can get from it.
 * Also explore DMA-BUF (replacement for ION)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <dirent.h>
#include <sys/stat.h>

// DMA-BUF Heap definitions (from dma-heap.h)
#define DMA_HEAP_IOCTL_ALLOC _IOWR('H', 0, struct dma_heap_allocation_data)

struct dma_heap_allocation_data
{
    unsigned long len;
    unsigned int fd;
    unsigned int fd_flags;
    unsigned long heap_flags;
};

// ADSPRPC - try various ioctl commands
#define FASTRPC_IOCTL_MAGIC 'R'

void hexdump(const void *data, size_t size, unsigned long base)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < size; i += 16)
    {
        printf("%08lx: ", base + i);
        for (size_t j = 0; j < 16; j++)
        {
            if (i + j < size)
                printf("%02x ", p[i + j]);
            else
                printf("   ");
            if (j == 7)
                printf(" ");
        }
        printf(" |");
        for (size_t j = 0; j < 16 && i + j < size; j++)
        {
            printf("%c", (p[i + j] >= 32 && p[i + j] < 127) ? p[i + j] : '.');
        }
        printf("|\n");
    }
}

void test_dma_heaps()
{
    printf("\n=== DMA-BUF Heap Test ===\n");

    // Check for DMA-BUF heaps
    DIR *d = opendir("/dev/dma_heap");
    if (!d)
    {
        printf("[-] /dev/dma_heap not found\n");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;

        char path[256];
        snprintf(path, sizeof(path), "/dev/dma_heap/%s", entry->d_name);

        int fd = open(path, O_RDWR);
        if (fd >= 0)
        {
            printf("[+] %s: WRITABLE\n", path);

            // Try to allocate
            struct dma_heap_allocation_data alloc = {0};
            alloc.len = 4096;
            alloc.fd_flags = O_RDWR | O_CLOEXEC;

            if (ioctl(fd, DMA_HEAP_IOCTL_ALLOC, &alloc) == 0)
            {
                printf("    [+] Allocation succeeded, fd=%u\n", alloc.fd);

                // Try to mmap
                void *map = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, alloc.fd, 0);
                if (map != MAP_FAILED)
                {
                    printf("    [+] mmap succeeded at %p\n", map);

                    // Write and read
                    *(unsigned long *)map = 0xDEADBEEF12345678ULL;
                    unsigned long rb = *(unsigned long *)map;
                    printf("    [+] R/W test: %s\n",
                           rb == 0xDEADBEEF12345678ULL ? "PASS" : "FAIL");

                    munmap(map, 4096);
                }
                close(alloc.fd);
            }
            else
            {
                printf("    [-] Allocation failed: %s\n", strerror(errno));
            }

            close(fd);
        }
        else if (errno == EACCES)
        {
            printf("[!] %s: PERMISSION DENIED\n", path);
        }
    }
    closedir(d);
}

void test_adsprpc_read()
{
    printf("\n=== ADSPRPC Read-Only Exploration ===\n");

    int fd = open("/dev/adsprpc-smd", O_RDONLY);
    if (fd < 0)
    {
        printf("[-] /dev/adsprpc-smd: %s\n", strerror(errno));
        return;
    }
    printf("[+] /dev/adsprpc-smd opened (read-only)\n");

    // Try to read anything
    unsigned char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n > 0)
    {
        printf("[+] Read %zd bytes:\n", n);
        hexdump(buf, n > 256 ? 256 : n, 0);
    }
    else if (n < 0)
    {
        printf("[-] Read failed: %s\n", strerror(errno));
    }
    else
    {
        printf("[-] Read returned 0 bytes\n");
    }

    // Try mmap (might work read-only)
    void *map = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);
    if (map != MAP_FAILED)
    {
        printf("[+] mmap succeeded at %p\n", map);

        // Check for non-zero data
        int non_zero = 0;
        for (int i = 0; i < 4096; i++)
        {
            if (((unsigned char *)map)[i] != 0)
                non_zero++;
        }
        printf("    Non-zero bytes: %d\n", non_zero);

        if (non_zero > 0)
        {
            hexdump(map, 64, 0);
        }

        munmap(map, 4096);
    }
    else
    {
        printf("[-] mmap failed: %s\n", strerror(errno));
    }

    // Try various ioctl commands (even read-only might work for some)
    printf("\n[*] Testing ioctl commands:\n");

    // GETINFO = 8
    unsigned int info_buf[32] = {0};
    int ret = ioctl(fd, _IOWR(FASTRPC_IOCTL_MAGIC, 8, info_buf), info_buf);
    printf("    GETINFO (8): ret=%d, errno=%d\n", ret, errno);

    close(fd);
}

void test_diag_ffs()
{
    printf("\n=== FFS-DIAG Exploration ===\n");

    const char *diag_devs[] = {
        "/dev/ffs-diag",
        "/dev/ffs-diag-1",
        "/dev/ffs-diag-2",
        NULL};

    for (int i = 0; diag_devs[i]; i++)
    {
        int fd = open(diag_devs[i], O_RDONLY);
        if (fd < 0)
            continue;

        printf("[+] %s opened\n", diag_devs[i]);

        // Try to read
        unsigned char buf[256];
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0)
        {
            printf("    Read %zd bytes:\n", n);
            hexdump(buf, n > 64 ? 64 : n, 0);
        }

        close(fd);
    }
}

void scan_all_world_writable()
{
    printf("\n=== All World-Writable Devices in /dev ===\n");

    DIR *d = opendir("/dev");
    if (!d)
        return;

    struct dirent *entry;
    struct stat st;
    char path[512];

    while ((entry = readdir(d)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;

        snprintf(path, sizeof(path), "/dev/%s", entry->d_name);

        if (stat(path, &st) == 0)
        {
            // Check if world-writable
            if (st.st_mode & S_IWOTH)
            {
                int fd = open(path, O_RDWR);
                if (fd >= 0)
                {
                    printf("[+] %s (mode: %04o)\n", path, st.st_mode & 0777);
                    close(fd);
                }
            }
        }
    }

    closedir(d);
}

void scan_proc_for_info()
{
    printf("\n=== Proc/Sys Information Gathering ===\n");

    // Check for KASLR info
    printf("\n[*] Checking memory layout:\n");

    FILE *f = fopen("/proc/self/maps", "r");
    if (f)
    {
        char line[512];
        int count = 0;
        while (fgets(line, sizeof(line), f) && count < 10)
        {
            // Look for interesting mappings
            if (strstr(line, "[vdso]") ||
                strstr(line, "[vvar]") ||
                strstr(line, "linker") ||
                strstr(line, "libc.so"))
            {
                printf("    %s", line);
                count++;
            }
        }
        fclose(f);
    }

    // Check boot parameters
    printf("\n[*] Boot parameters:\n");
    f = fopen("/proc/cmdline", "r");
    if (f)
    {
        char buf[1024];
        if (fgets(buf, sizeof(buf), f))
        {
            // Print in chunks for readability
            char *p = buf;
            while (*p)
            {
                int len = 0;
                while (p[len] && p[len] != ' ' && len < 80)
                    len++;
                if (len > 0)
                {
                    printf("    %.*s\n", len, p);
                    p += len;
                    if (*p == ' ')
                        p++;
                }
            }
        }
        fclose(f);
    }

    // Check SELinux mode
    printf("\n[*] SELinux info:\n");
    f = fopen("/sys/fs/selinux/enforce", "r");
    if (f)
    {
        char buf[16];
        if (fgets(buf, sizeof(buf), f))
        {
            printf("    Enforce: %s", buf);
        }
        fclose(f);
    }

    f = fopen("/proc/self/attr/current", "r");
    if (f)
    {
        char buf[256];
        if (fgets(buf, sizeof(buf), f))
        {
            printf("    Context: %s", buf);
        }
        fclose(f);
    }

    // Check kernel version details
    printf("\n[*] Kernel info:\n");
    f = fopen("/proc/version", "r");
    if (f)
    {
        char buf[512];
        if (fgets(buf, sizeof(buf), f))
        {
            printf("    %s", buf);
        }
        fclose(f);
    }
}

void test_misc_devices()
{
    printf("\n=== Misc Device Exploration ===\n");

    const char *misc_devs[] = {
        "/dev/mhi0_ADSP",
        "/dev/mhi0_DIAG",
        "/dev/spidev0.0",
        "/dev/spidev1.0",
        "/dev/rt600_spiboot",
        "/dev/ttyMSM0",
        "/dev/ttyHS0",
        "/dev/rfkill",
        "/dev/rtc0",
        "/dev/watchdog",
        "/dev/dma_heap/system",
        "/dev/dma_heap/system-uncached",
        "/dev/dma_heap/qcom,system",
        NULL};

    for (int i = 0; misc_devs[i]; i++)
    {
        int fd = open(misc_devs[i], O_RDWR);
        if (fd >= 0)
        {
            printf("[+] %s: WRITABLE\n", misc_devs[i]);
            close(fd);
        }
        else if (errno == EACCES)
        {
            printf("[!] %s: PERMISSION DENIED\n", misc_devs[i]);
        }
    }
}

int main()
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  ADSPRPC/DMA-BUF/Misc Attack Surface                           ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");

    test_dma_heaps();
    test_adsprpc_read();
    test_diag_ffs();
    scan_all_world_writable();
    test_misc_devices();
    scan_proc_for_info();

    printf("\n=== Exploration Complete ===\n");
    return 0;
}
