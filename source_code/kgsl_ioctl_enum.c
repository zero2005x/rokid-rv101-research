/*
 * KGSL IOCTL Complete Enumeration
 * =================================
 *
 * GPU_COMMAND returns success but doesn't execute.
 * Let's find all working IOCTLs and their behaviors.
 *
 * Compile:
 * aarch64-linux-android31-clang -static -O2 -o kgsl_ioctl_enum kgsl_ioctl_enum.c
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
// All Known KGSL IOCTLs
// ============================================================================

#define KGSL_IOC_TYPE 0x09

// Define all KGSL ioctls manually
typedef struct
{
    uint32_t cmd;
    const char *name;
    int size; // Expected structure size
} ioctl_info_t;

ioctl_info_t kgsl_ioctls[] = {
    // Device operations
    {0xc0100901, "DEVICE_WAITTIMESTAMP", 16},
    {0xc0100902, "DEVICE_GETPROPERTY", 16},
    {0x40100903, "DEVICE_SETPROPERTY", 16},
    {0x40040904, "READTIMESTAMP", 4},
    {0xc0040905, "ISSUEIBCMDS", 4},
    {0xc0100906, "CMDSTREAM_READTIMESTAMP", 16},
    {0xc0040907, "CMDWINDOW_WRITE", 4},
    {0x40080908, "CL_USER_PROFILING", 8},

    // Memory operations
    {0xc0180909, "SHAREDMEM_FROM_VMALLOC", 24},
    {0xc018090a, "SHAREDMEM_FREE", 24},
    {0xc010090b, "MAP_USER_MEM", 16},
    {0xc018090c, "SHAREDMEM_FLUSH_CACHE", 24},
    {0xc010090d, "DRAWCTXT_SET_BIN_BASE_OFFSET", 16},
    {0xc010090e, "TIMESTAMP_EVENT", 16},
    {0xc010090f, "FREEMEMONTIMESTAMP", 16},
    {0x40080910, "GPUMEM_FREE", 8},
    {0xc0180911, "GPUMEM_GET_INFO", 24},

    // Context operations
    {0xc0100913, "DRAWCTXT_CREATE", 16},
    {0x40080914, "DRAWCTXT_DESTROY", 8},
    {0x40040915, "PERFCOUNTER_GET", 4},
    {0x40040916, "PERFCOUNTER_PUT", 4},
    {0xc0100917, "PERFCOUNTER_QUERY", 16},
    {0xc0200918, "PERFCOUNTER_READ", 32},

    // Timestamp operations
    {0xc0100919, "TIMESTAMP_EVENT_OLD", 16},
    {0xc010091a, "SETPROPERTY", 16},
    {0xc010091b, "WAIT_TIMESTAMP", 16},
    {0xc010091c, "CMDWINDOW_WRITE2", 16},
    {0xc018091d, "DRAWCTXT_QUEUE_WAIT", 24},
    {0xc018091e, "GENLOCK_WAIT", 24},

    // Extended operations
    {0xc018092f, "GPUMEM_ALLOC", 24},
    {0xc0080930, "SYNCSOURCE_CREATE", 8},
    {0x40040931, "SYNCSOURCE_DESTROY", 4},
    {0xc0180932, "SYNCSOURCE_CREATE_FENCE", 24},
    {0x40040933, "SYNCSOURCE_SIGNAL_FENCE", 4},

    // GPU object operations
    {0xc0180938, "GPUOBJ_ALLOC", 24},
    {0xc0200939, "GPUOBJ_INFO", 32},
    {0xc018093a, "GPUOBJ_SET_INFO", 24},
    {0xc018093b, "GPUMEM_SYNC_CACHE", 24},
    {0xc020093c, "GPUMEM_SYNC_CACHE_BULK", 32},
    {0xc020093d, "GPUOBJ_GETINFO_V2", 32},
    {0xc028093e, "PERFCOUNTER_QUERY_V2", 40},
    {0xc028093f, "GPU_AUX_COMMAND", 40},

    // GPU command
    {0xc0580940, "GPU_COMMAND", 88},

    // Additional operations
    {0xc0100941, "GPUOBJ_BIND", 16},
    {0xc0100942, "GPUOBJ_UNBIND", 16},
    {0xc0100943, "GPU_STAT", 16},
    {0xc0180944, "GPU_POWER", 24},
    {0xc0100945, "GPU_TIMER", 16},
    {0xc0100946, "GPU_STAT_V2", 16},

    // Sparse memory
    {0xc0280957, "SPARSE_BIND", 40},

    // GPU object import/sync
    {0xc0280958, "GPUOBJ_IMPORT", 40},
    {0xc0180956, "GPUOBJ_SYNC", 24},

    // Additional (guessed from adjacent codes)
    {0xc0180950, "UNK_50", 24},
    {0xc0180951, "UNK_51", 24},
    {0xc0180952, "UNK_52", 24},
    {0xc0180953, "UNK_53", 24},
    {0xc0180954, "UNK_54", 24},
    {0xc0180955, "UNK_55", 24},

    {0, NULL, 0}};

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

struct kgsl_device_getproperty
{
    uint32_t type;
    void *value;
    uint32_t sizebytes;
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

void hexdump(const char *desc, const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    printf("%s:\n", desc);
    for (size_t i = 0; i < size && i < 64; i++)
    {
        printf("%02x", p[i]);
        if ((i + 1) % 16 == 0)
            printf("\n");
        else if ((i + 1) % 8 == 0)
            printf("  ");
        else
            printf(" ");
    }
    if (size % 16 != 0)
        printf("\n");
}

// ============================================================================
// Test each IOCTL
// ============================================================================

void test_ioctl(uint32_t cmd, const char *name, int size)
{
    uint8_t buf_in[256] = {0};
    uint8_t buf_out[256] = {0};

    memcpy(buf_out, buf_in, sizeof(buf_in));

    errno = 0;
    int ret = ioctl(kgsl_fd, cmd, buf_out);

    // Check if output changed
    int changed = memcmp(buf_in, buf_out, sizeof(buf_in)) != 0;

    if (ret == 0 || changed || errno == 0)
    {
        printf("[+] 0x%08x %-30s ret=%2d changed=%d", cmd, name, ret, changed);
        if (changed)
        {
            // Show first 32 bytes of output
            printf(" data=");
            for (int i = 0; i < 32; i++)
            {
                printf("%02x", buf_out[i]);
            }
        }
        printf("\n");
    }
    else if (errno != ENOTTY && errno != EINVAL)
    {
        printf("[?] 0x%08x %-30s errno=%d (%s)\n", cmd, name, errno, strerror(errno));
    }
}

// ============================================================================
// Scan all KGSL IOCTLs
// ============================================================================

void scan_all_ioctls(void)
{
    printf("\n=== Scanning Known KGSL IOCTLs ===\n");

    for (int i = 0; kgsl_ioctls[i].name != NULL; i++)
    {
        test_ioctl(kgsl_ioctls[i].cmd, kgsl_ioctls[i].name, kgsl_ioctls[i].size);
    }
}

// ============================================================================
// Brute force IOCTL numbers
// ============================================================================

void bruteforce_ioctls(void)
{
    printf("\n=== Brute Force IOCTL Scan (0x09 type) ===\n");

    // KGSL uses type 0x09
    // Try all command numbers from 0x00 to 0xFF

    int found = 0;

    for (int nr = 0; nr <= 0xFF; nr++)
    {
        // Try different sizes
        for (int size = 0; size <= 128; size += 8)
        {
            // Build IOCTL command:
            // _IOC(dir, type, nr, size)
            // dir: 0=none, 1=write, 2=read, 3=read/write

            for (int dir = 0; dir <= 3; dir++)
            {
                uint32_t cmd = (dir << 30) | (size << 16) | (0x09 << 8) | nr;

                uint8_t buf[256] = {0};
                errno = 0;
                int ret = ioctl(kgsl_fd, cmd, buf);

                if (ret == 0)
                {
                    printf("[+] 0x%08x: SUCCESS (dir=%d nr=0x%02x size=%d)\n",
                           cmd, dir, nr, size);
                    found++;
                    break; // Found a working size for this nr
                }
                else if (errno == EFAULT)
                {
                    // EFAULT means it tried to access our pointer = valid ioctl
                    printf("[?] 0x%08x: EFAULT (dir=%d nr=0x%02x size=%d)\n",
                           cmd, dir, nr, size);
                }
            }
        }
    }

    printf("[*] Found %d potentially working IOCTLs\n", found);
}

// ============================================================================
// Deep test of successful IOCTLs
// ============================================================================

void deep_test_getproperty(void)
{
    printf("\n=== Deep Test: DEVICE_GETPROPERTY ===\n");

    uint8_t buf[4096];

    for (uint32_t prop = 0; prop <= 0x50; prop++)
    {
        memset(buf, 0, sizeof(buf));

        struct kgsl_device_getproperty get = {0};
        get.type = prop;
        get.value = buf;
        get.sizebytes = sizeof(buf);

        errno = 0;
        int ret = ioctl(kgsl_fd, 0xc0100902, &get);

        if (ret == 0)
        {
            // Count non-zero bytes
            int nonzero = 0;
            for (int i = 0; i < 256; i++)
            {
                if (buf[i] != 0)
                    nonzero++;
            }

            if (nonzero > 0)
            {
                printf("[+] Property 0x%02x: %d non-zero bytes\n", prop, nonzero);

                // Check for interesting patterns
                uint64_t *vals = (uint64_t *)buf;
                for (int i = 0; i < 32; i++)
                {
                    // Kernel pointer?
                    if ((vals[i] & 0xFFFF000000000000ULL) == 0xFFFF000000000000ULL)
                    {
                        printf("    [!!!] Kernel ptr at offset %d: 0x%lx\n", i * 8, vals[i]);
                    }
                    // User pointer?
                    if ((vals[i] & 0xFFFF000000000000ULL) == 0x0000700000000000ULL)
                    {
                        printf("    [*] User ptr at offset %d: 0x%lx\n", i * 8, vals[i]);
                    }
                }

                // Check for ASCII
                for (int i = 0; i < 256; i++)
                {
                    if (buf[i] >= 0x20 && buf[i] < 0x7f)
                    {
                        int len = 0;
                        while (i + len < 256 && buf[i + len] >= 0x20 && buf[i + len] < 0x7f)
                        {
                            len++;
                        }
                        if (len >= 4)
                        {
                            char str[65];
                            memcpy(str, &buf[i], len < 64 ? len : 64);
                            str[len < 64 ? len : 64] = 0;
                            printf("    String at %d: \"%s\"\n", i, str);
                            i += len;
                        }
                    }
                }
            }
        }
    }
}

// ============================================================================
// Test GPU_COMMAND variations
// ============================================================================

void deep_test_gpu_command(void)
{
    printf("\n=== Deep Test: GPU_COMMAND ===\n");

    // Allocate command buffer
    struct gpumem_alloc_req alloc = {0};
    alloc.size = 0x10000;
    if (ioctl(kgsl_fd, 0xc018092f, &alloc) < 0)
    {
        printf("[-] Alloc failed\n");
        return;
    }
    printf("[+] Command buffer at 0x%lx\n", alloc.gpuaddr);

    // Map it
    uint64_t page_idx = (alloc.gpuaddr - 0x4000000000ULL) / 0x1000;
    uint64_t offset = (page_idx + 1) * 0x1000;
    void *cmd_map = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE,
                         MAP_SHARED, kgsl_fd, offset);
    if (cmd_map == MAP_FAILED)
    {
        printf("[-] mmap failed\n");
        return;
    }

    // Write NOP commands
    uint32_t *cmds = (uint32_t *)cmd_map;
    cmds[0] = 0x80000007; // Type 2 NOP with 8 dwords
    for (int i = 1; i < 8; i++)
        cmds[i] = 0;

    // Command object
    struct
    {
        uint64_t gpuaddr;
        uint64_t size;
        uint32_t flags;
        uint32_t id;
    } cmd_obj = {0};
    cmd_obj.gpuaddr = alloc.gpuaddr;
    cmd_obj.size = 32;

    // GPU command structure (88 bytes)
    struct
    {
        uint64_t flags;
        uint64_t cmdlist;
        uint64_t cmdsize;
        uint64_t objlist;
        uint64_t objsize;
        uint64_t synclist;
        uint64_t syncsize;
        uint32_t context_id;
        uint32_t numcmds;
        uint32_t numobjs;
        uint32_t numsyncs;
        uint32_t timestamp;
        uint32_t result;
        uint64_t result_ts;
    } gpu_cmd = {0};

    gpu_cmd.cmdlist = (uint64_t)&cmd_obj;
    gpu_cmd.cmdsize = sizeof(cmd_obj);
    gpu_cmd.numcmds = 1;

    // Test with different flags
    uint64_t test_flags[] = {0, 1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x100};

    printf("[*] Testing different flags:\n");
    for (size_t i = 0; i < sizeof(test_flags) / sizeof(test_flags[0]); i++)
    {
        gpu_cmd.flags = test_flags[i];
        gpu_cmd.timestamp = 0;
        gpu_cmd.result = 0;

        errno = 0;
        int ret = ioctl(kgsl_fd, 0xc0580940, &gpu_cmd);
        printf("    flags=0x%lx: ret=%d errno=%d ts=%u result=%u\n",
               test_flags[i], ret, errno, gpu_cmd.timestamp, gpu_cmd.result);
    }

    munmap(cmd_map, 0x10000);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  KGSL IOCTL Complete Enumeration                              ║\n");
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

    // Initial allocation
    struct gpumem_alloc_req alloc = {0};
    alloc.size = 0x1000;
    if (ioctl(kgsl_fd, 0xc018092f, &alloc) < 0)
    {
        printf("[-] Initial alloc failed\n");
        return 1;
    }
    gpu_base = alloc.gpuaddr;
    printf("[+] Base allocation at 0x%lx\n", gpu_base);

    // Run tests
    scan_all_ioctls();
    deep_test_getproperty();
    deep_test_gpu_command();

    // Optional: brute force (uncomment if needed)
    // bruteforce_ioctls();

    close(kgsl_fd);

    printf("\n=== Enumeration Complete ===\n");
    return 0;
}
