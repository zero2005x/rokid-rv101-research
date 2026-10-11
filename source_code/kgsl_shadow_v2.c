/*
 * KGSL Shadow Buffer Analysis v2
 * Read-only analysis of GPU shadow memory
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#define KGSL_IOC_TYPE 0x09
#define IOCTL_KGSL_DEVICE_GETPROPERTY _IOWR(KGSL_IOC_TYPE, 0x02, struct kgsl_device_getproperty)

#define KGSL_PROP_DEVICE_SHADOW 0x02

struct kgsl_device_getproperty
{
    unsigned int type;
    void *value;
    unsigned long sizebytes;
};

struct kgsl_shadowprop
{
    unsigned long gpuaddr;
    unsigned long size;
    unsigned int flags;
};

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

int main()
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  KGSL Shadow Buffer Analysis v2                                ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");

    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
    {
        perror("[-] Failed to open /dev/kgsl-3d0");
        return 1;
    }

    // Get shadow info
    struct kgsl_shadowprop shadow = {0};
    struct kgsl_device_getproperty prop = {0};
    prop.type = KGSL_PROP_DEVICE_SHADOW;
    prop.value = &shadow;
    prop.sizebytes = sizeof(shadow);

    if (ioctl(fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop) != 0)
    {
        perror("[-] Failed to get shadow property");
        close(fd);
        return 1;
    }

    printf("[+] Shadow GPU addr: 0x%lx\n", shadow.gpuaddr);
    printf("[+] Shadow size: 0x%lx (%lu KB)\n", shadow.size, shadow.size / 1024);
    printf("[+] Shadow flags: 0x%x\n", shadow.flags);

    // Map the shadow buffer (read-only)
    void *shadow_map = mmap(NULL, shadow.size, PROT_READ,
                            MAP_SHARED, fd, shadow.gpuaddr);
    if (shadow_map == MAP_FAILED)
    {
        perror("[-] Failed to mmap shadow");
        close(fd);
        return 1;
    }
    printf("[+] Shadow mapped at: %p\n", shadow_map);

    // ============ Full analysis of non-zero regions ============
    printf("\n=== All Non-Zero Values in Shadow ===\n");
    for (unsigned long off = 0; off < shadow.size; off += 8)
    {
        unsigned long val = *(unsigned long *)((char *)shadow_map + off);
        if (val != 0)
        {
            printf("[+] 0x%04lx: 0x%016lx", off, val);

            // Analyze the value
            if ((val & 0xFFFF000000000000ULL) == 0xFFFF000000000000ULL)
            {
                printf(" [POTENTIAL KERNEL PTR]");
            }
            else if (val < 0x100000)
            {
                printf(" [small value, likely counter/timestamp]");
            }
            else if ((val & 0xFFFFF00000000000ULL) == 0)
            {
                printf(" [low 64-bit value]");
            }
            printf("\n");
        }
    }

    // ============ Dump key regions ============
    printf("\n=== Shadow Hex Dump: Offset 0x000-0x100 ===\n");
    hexdump(shadow_map, 256, 0);

    printf("\n=== Shadow Hex Dump: Offset 0x100-0x200 ===\n");
    hexdump((char *)shadow_map + 0x100, 256, 0x100);

    // ============ Look for interesting patterns ============
    printf("\n=== Pattern Analysis ===\n");

    // Check for repeated values (might indicate structure)
    unsigned long prev = 0;
    int repeat_count = 0;
    for (unsigned long off = 0; off < shadow.size; off += 8)
    {
        unsigned long val = *(unsigned long *)((char *)shadow_map + off);
        if (val == prev && val != 0)
        {
            repeat_count++;
            if (repeat_count == 2)
            {
                printf("[*] Repeated value 0x%lx starting at 0x%lx\n", val, off - 16);
            }
        }
        else
        {
            repeat_count = 0;
        }
        prev = val;
    }

    // ============ Monitor for changes over time ============
    printf("\n=== Monitoring Shadow for 5 seconds ===\n");

    unsigned char initial[0x200];
    memcpy(initial, shadow_map, 0x200);

    for (int iter = 0; iter < 5; iter++)
    {
        sleep(1);

        int changes = 0;
        for (unsigned long off = 0; off < 0x200; off += 8)
        {
            unsigned long v1 = *(unsigned long *)(initial + off);
            unsigned long v2 = *(unsigned long *)((char *)shadow_map + off);
            if (v1 != v2)
            {
                printf("  [%d sec] 0x%03lx: 0x%lx -> 0x%lx\n", iter + 1, off, v1, v2);
                changes++;
                // Update baseline
                *(unsigned long *)(initial + off) = v2;
            }
        }

        if (changes == 0)
        {
            printf("  [%d sec] No changes\n", iter + 1);
        }
    }

    // ============ Try other mmap offsets ============
    printf("\n=== Probing Other GPU Memory Regions ===\n");

    // Try offsets around the shadow region
    unsigned long offsets[] = {
        0x00000000,
        0x10000000,
        0x20000000,
        0x40000000,
        0x80000000,
        0xC0000000,
        0xE0000000,
        0xF0000000,
        shadow.gpuaddr - 0x1000,
        shadow.gpuaddr + shadow.size,
    };

    for (int i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++)
    {
        void *map = mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, fd, offsets[i]);
        if (map != MAP_FAILED)
        {
            printf("\n[+] Offset 0x%lx MAPPED:\n", offsets[i]);

            // Check for non-zero content
            int non_zero = 0;
            for (int j = 0; j < 0x1000 / 8; j++)
            {
                unsigned long val = ((unsigned long *)map)[j];
                if (val != 0)
                {
                    if (non_zero < 8)
                    {
                        printf("    +0x%03x: 0x%016lx\n", j * 8, val);
                    }
                    non_zero++;

                    // Check for kernel pointers
                    if ((val & 0xFFFF000000000000ULL) == 0xFFFF000000000000ULL)
                    {
                        printf("    [!] POTENTIAL KERNEL PTR: 0x%016lx\n", val);
                    }
                }
            }
            printf("    Total non-zero values: %d\n", non_zero);

            munmap(map, 0x1000);
        }
    }

    munmap(shadow_map, shadow.size);
    close(fd);

    printf("\n=== Analysis Complete ===\n");
    return 0;
}
