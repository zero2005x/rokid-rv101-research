/*
 * ashmem_kgsl_bridge.c - Ashmem to KGSL Bridge Research
 *
 * Attempts to use ashmem as a memory source for GPU operations
 *
 * Build: aarch64-linux-android31-clang -static -O2 -o ashmem_kgsl_bridge ashmem_kgsl_bridge.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdint.h>

/* Ashmem IOCTL definitions */
#define ASHMEM_NAME_LEN 256
#define __ASHMEMIOC 0x77

#define ASHMEM_SET_NAME _IOW(__ASHMEMIOC, 1, char[ASHMEM_NAME_LEN])
#define ASHMEM_GET_NAME _IOR(__ASHMEMIOC, 2, char[ASHMEM_NAME_LEN])
#define ASHMEM_SET_SIZE _IOW(__ASHMEMIOC, 3, size_t)
#define ASHMEM_GET_SIZE _IO(__ASHMEMIOC, 4)
#define ASHMEM_SET_PROT_MASK _IOW(__ASHMEMIOC, 5, unsigned long)
#define ASHMEM_GET_PROT_MASK _IO(__ASHMEMIOC, 6)
#define ASHMEM_PIN _IOW(__ASHMEMIOC, 7, struct ashmem_pin)
#define ASHMEM_UNPIN _IOW(__ASHMEMIOC, 8, struct ashmem_pin)
#define ASHMEM_GET_PIN_STATUS _IO(__ASHMEMIOC, 9)
#define ASHMEM_PURGE_ALL_CACHES _IO(__ASHMEMIOC, 10)

struct ashmem_pin
{
    uint32_t offset;
    uint32_t len;
};

/* KGSL IOCTLs */
#define KGSL_IOC_TYPE 0x09

struct kgsl_gpumem_alloc_id
{
    unsigned int id;
    unsigned int flags;
    unsigned long size;
    unsigned long mmapsize;
    unsigned long gpuaddr;
};

struct kgsl_map_user_mem
{
    int fd;
    unsigned long gpuaddr;
    unsigned long len;
    unsigned long offset;
    unsigned long hostptr;
    unsigned int memtype;
    unsigned int flags;
};

struct kgsl_sharedmem_from_pmem
{
    int pmem_fd;
    unsigned int gpuaddr;
    unsigned int len;
    unsigned int offset;
};

#define IOCTL_KGSL_GPUMEM_ALLOC_ID _IOWR(KGSL_IOC_TYPE, 0x35, struct kgsl_gpumem_alloc_id)
#define IOCTL_KGSL_MAP_USER_MEM _IOWR(KGSL_IOC_TYPE, 0x16, struct kgsl_map_user_mem)
#define IOCTL_KGSL_SHAREDMEM_FROM_PMEM _IOWR(KGSL_IOC_TYPE, 0x20, struct kgsl_sharedmem_from_pmem)

/* Memory types for MAP_USER_MEM */
#define KGSL_USER_MEM_TYPE_PMEM 0x00000000
#define KGSL_USER_MEM_TYPE_ASHMEM 0x00000001
#define KGSL_USER_MEM_TYPE_ADDR 0x00000002
#define KGSL_USER_MEM_TYPE_ION 0x00000003
#define KGSL_USER_MEM_TYPE_DMABUF 0x00000004

/*========== ASHMEM TESTS ==========*/

int create_ashmem_region(const char *name, size_t size)
{
    int fd = open("/dev/ashmem", O_RDWR);
    if (fd < 0)
    {
        printf("[-] Cannot open /dev/ashmem: %s\n", strerror(errno));
        return -1;
    }

    if (ioctl(fd, ASHMEM_SET_NAME, name) < 0)
    {
        printf("[-] ASHMEM_SET_NAME failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    if (ioctl(fd, ASHMEM_SET_SIZE, size) < 0)
    {
        printf("[-] ASHMEM_SET_SIZE failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

void test_ashmem_basic()
{
    printf("\n=== ASHMEM BASIC TEST ===\n");

    int fd = create_ashmem_region("test_buffer", 4096);
    if (fd < 0)
        return;

    printf("[+] Ashmem created: fd=%d\n", fd);

    /* mmap */
    void *ptr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED)
    {
        printf("[-] mmap failed: %s\n", strerror(errno));
        close(fd);
        return;
    }

    printf("[+] mmap success: %p\n", ptr);

    /* Write pattern */
    memset(ptr, 0x42, 64);
    printf("[+] Written pattern 0x42\n");

    /* Read back */
    unsigned char *bytes = (unsigned char *)ptr;
    printf("[+] Read back: %02x %02x %02x %02x\n",
           bytes[0], bytes[1], bytes[2], bytes[3]);

    munmap(ptr, 4096);
    close(fd);
}

/*========== KGSL + ASHMEM BRIDGE ==========*/

void test_kgsl_map_ashmem()
{
    printf("\n=== KGSL MAP ASHMEM TEST ===\n");

    /* Create ashmem region */
    int ashmem_fd = create_ashmem_region("gpu_buffer", 0x10000);
    if (ashmem_fd < 0)
        return;

    printf("[+] Ashmem fd: %d\n", ashmem_fd);

    /* mmap ashmem first */
    void *ashmem_ptr = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE,
                            MAP_SHARED, ashmem_fd, 0);
    if (ashmem_ptr == MAP_FAILED)
    {
        printf("[-] Ashmem mmap failed\n");
        close(ashmem_fd);
        return;
    }
    printf("[+] Ashmem mmap: %p\n", ashmem_ptr);

    /* Open KGSL */
    int kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        printf("[-] Cannot open KGSL: %s\n", strerror(errno));
        munmap(ashmem_ptr, 0x10000);
        close(ashmem_fd);
        return;
    }
    printf("[+] KGSL fd: %d\n", kgsl_fd);

    /* Try KGSL_MAP_USER_MEM with ashmem */
    struct kgsl_map_user_mem map_req = {0};
    map_req.fd = ashmem_fd;
    map_req.len = 0x10000;
    map_req.offset = 0;
    map_req.hostptr = (unsigned long)ashmem_ptr;
    map_req.memtype = KGSL_USER_MEM_TYPE_ASHMEM;
    map_req.flags = 0;

    int ret = ioctl(kgsl_fd, IOCTL_KGSL_MAP_USER_MEM, &map_req);
    if (ret == 0)
    {
        printf("[+] KGSL_MAP_USER_MEM (ASHMEM) SUCCESS!\n");
        printf("    gpuaddr: 0x%lx\n", map_req.gpuaddr);
    }
    else
    {
        printf("[-] KGSL_MAP_USER_MEM (ASHMEM) failed: %s (errno=%d)\n",
               strerror(errno), errno);
    }

    /* Try with DMABUF type */
    map_req.memtype = KGSL_USER_MEM_TYPE_DMABUF;
    ret = ioctl(kgsl_fd, IOCTL_KGSL_MAP_USER_MEM, &map_req);
    if (ret == 0)
    {
        printf("[+] KGSL_MAP_USER_MEM (DMABUF) SUCCESS!\n");
        printf("    gpuaddr: 0x%lx\n", map_req.gpuaddr);
    }
    else
    {
        printf("[-] KGSL_MAP_USER_MEM (DMABUF) failed: %s (errno=%d)\n",
               strerror(errno), errno);
    }

    /* Try with ION type */
    map_req.memtype = KGSL_USER_MEM_TYPE_ION;
    ret = ioctl(kgsl_fd, IOCTL_KGSL_MAP_USER_MEM, &map_req);
    if (ret == 0)
    {
        printf("[+] KGSL_MAP_USER_MEM (ION) SUCCESS!\n");
        printf("    gpuaddr: 0x%lx\n", map_req.gpuaddr);
    }
    else
    {
        printf("[-] KGSL_MAP_USER_MEM (ION) failed: %s (errno=%d)\n",
               strerror(errno), errno);
    }

    /* Try SHAREDMEM_FROM_PMEM (legacy) */
    struct kgsl_sharedmem_from_pmem pmem_req = {0};
    pmem_req.pmem_fd = ashmem_fd;
    pmem_req.len = 0x10000;
    pmem_req.offset = 0;

    ret = ioctl(kgsl_fd, IOCTL_KGSL_SHAREDMEM_FROM_PMEM, &pmem_req);
    if (ret == 0)
    {
        printf("[+] KGSL_SHAREDMEM_FROM_PMEM SUCCESS!\n");
        printf("    gpuaddr: 0x%x\n", pmem_req.gpuaddr);
    }
    else
    {
        printf("[-] KGSL_SHAREDMEM_FROM_PMEM failed: %s (errno=%d)\n",
               strerror(errno), errno);
    }

    close(kgsl_fd);
    munmap(ashmem_ptr, 0x10000);
    close(ashmem_fd);
}

/*========== KGSL ALLOC WITH CORRECT IOCTL ==========*/

void test_kgsl_gpumem_alloc()
{
    printf("\n=== KGSL GPUMEM_ALLOC_ID TEST ===\n");

    int kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        printf("[-] Cannot open KGSL: %s\n", strerror(errno));
        return;
    }

    struct kgsl_gpumem_alloc_id alloc = {0};
    alloc.size = 0x10000;
    alloc.flags = 0; /* Try default flags */

    int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc);
    if (ret == 0)
    {
        printf("[+] GPUMEM_ALLOC_ID SUCCESS!\n");
        printf("    id: %u\n", alloc.id);
        printf("    gpuaddr: 0x%lx\n", alloc.gpuaddr);
        printf("    mmapsize: 0x%lx\n", alloc.mmapsize);

        /* Try mmap */
        void *ptr = mmap(NULL, alloc.mmapsize, PROT_READ | PROT_WRITE,
                         MAP_SHARED, kgsl_fd, alloc.id * 0x1000);
        if (ptr != MAP_FAILED)
        {
            printf("[+] mmap: %p\n", ptr);

            /* Write test pattern */
            memset(ptr, 0x55, 64);
            printf("[+] Written 0x55 pattern\n");

            munmap(ptr, alloc.mmapsize);
        }
        else
        {
            printf("[-] mmap failed: %s\n", strerror(errno));
        }
    }
    else
    {
        printf("[-] GPUMEM_ALLOC_ID failed: %s (errno=%d)\n",
               strerror(errno), errno);
    }

    /* Try different flag combinations */
    printf("\n[*] Testing flag combinations:\n");
    unsigned int flags[] = {
        0x00000000, /* Default */
        0x00100000, /* GPUREADONLY */
        0x00000200, /* SECURE */
        0x00001000, /* UNCACHED */
        0x00002000, /* WRITECOMBINE */
        0x00000008, /* NO_MEMDESC */
        0x00000010, /* FORCE_APERTURE */
    };

    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); i++)
    {
        alloc.size = 0x1000;
        alloc.flags = flags[i];
        alloc.id = 0;

        ret = ioctl(kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc);
        if (ret == 0)
        {
            printf("    flags=0x%08x: SUCCESS (id=%u, gpuaddr=0x%lx)\n",
                   flags[i], alloc.id, alloc.gpuaddr);
        }
        else
        {
            printf("    flags=0x%08x: FAILED (%s)\n", flags[i], strerror(errno));
        }
    }

    close(kgsl_fd);
}

/*========== MEMFD CREATE TEST ==========*/

void test_memfd()
{
    printf("\n=== MEMFD_CREATE TEST ===\n");

    /* Try memfd_create syscall */
    int fd = syscall(319, "gpu_test", 0); /* __NR_memfd_create = 319 on aarch64 */
    if (fd >= 0)
    {
        printf("[+] memfd_create SUCCESS: fd=%d\n", fd);

        /* Set size */
        if (ftruncate(fd, 0x10000) == 0)
        {
            printf("[+] ftruncate to 64KB\n");

            /* mmap */
            void *ptr = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE,
                             MAP_SHARED, fd, 0);
            if (ptr != MAP_FAILED)
            {
                printf("[+] mmap: %p\n", ptr);
                munmap(ptr, 0x10000);
            }
        }
        close(fd);
    }
    else
    {
        printf("[-] memfd_create failed: %s\n", strerror(errno));
    }
}

/*========== MAIN ==========*/

int main(int argc, char *argv[])
{
    printf("=== Ashmem to KGSL Bridge Research ===\n");
    printf("Alternative GPU memory allocation paths\n");

    test_ashmem_basic();
    test_kgsl_gpumem_alloc();
    test_kgsl_map_ashmem();
    test_memfd();

    printf("\n=== SUMMARY ===\n");
    printf("[*] Ashmem: Full access (mode 666)\n");
    printf("[*] KGSL: May support mapping ashmem regions\n");
    printf("[*] memfd_create: Modern replacement for ashmem\n");

    return 0;
}
