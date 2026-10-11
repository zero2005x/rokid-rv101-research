/*
 * Binder Write-Read Command Test
 * Tests various Binder commands
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

typedef uint64_t binder_size_t;
typedef uint64_t binder_uintptr_t;

struct binder_write_read
{
    binder_size_t write_size;
    binder_size_t write_consumed;
    binder_uintptr_t write_buffer;
    binder_size_t read_size;
    binder_size_t read_consumed;
    binder_uintptr_t read_buffer;
};

#define BINDER_VERSION _IOWR('b', 9, uint32_t)
#define BINDER_WRITE_READ _IOWR('b', 1, struct binder_write_read)
#define BINDER_SET_MAX_THREADS _IOW('b', 5, uint32_t)
#define BINDER_SET_CONTEXT_MGR _IOW('b', 7, int32_t)

// Binder commands (BC_*)
#define BC_TRANSACTION _IOW('c', 0, struct binder_transaction_data)
#define BC_REPLY _IOW('c', 1, struct binder_transaction_data)
#define BC_ACQUIRE_RESULT _IOW('c', 2, int32_t)
#define BC_FREE_BUFFER _IOW('c', 3, binder_uintptr_t)
#define BC_INCREFS _IOW('c', 4, uint32_t)
#define BC_ACQUIRE _IOW('c', 5, uint32_t)
#define BC_RELEASE _IOW('c', 6, uint32_t)
#define BC_DECREFS _IOW('c', 7, uint32_t)
#define BC_INCREFS_DONE _IOW('c', 8, struct binder_ptr_cookie)
#define BC_ACQUIRE_DONE _IOW('c', 9, struct binder_ptr_cookie)
#define BC_ATTEMPT_ACQUIRE _IOW('c', 10, struct binder_pri_desc)
#define BC_REGISTER_LOOPER _IO('c', 11)
#define BC_ENTER_LOOPER _IO('c', 12)
#define BC_EXIT_LOOPER _IO('c', 13)
#define BC_REQUEST_DEATH_NOTIFICATION _IOW('c', 14, struct binder_handle_cookie)
#define BC_CLEAR_DEATH_NOTIFICATION _IOW('c', 15, struct binder_handle_cookie)
#define BC_DEAD_BINDER_DONE _IOW('c', 16, binder_uintptr_t)

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

struct binder_ptr_cookie
{
    binder_uintptr_t ptr;
    binder_uintptr_t cookie;
};

struct binder_pri_desc
{
    int32_t priority;
    uint32_t desc;
};

struct binder_handle_cookie
{
    uint32_t handle;
    binder_uintptr_t cookie;
};

static sigjmp_buf jmp_env;
static volatile sig_atomic_t sig_received = 0;

void sig_handler(int sig)
{
    sig_received = sig;
    siglongjmp(jmp_env, 1);
}

void test_binder_cmd(int fd, uint32_t cmd, const char *name, size_t extra_size)
{
    uint8_t write_buf[512] = {0};
    uint8_t read_buf[256] = {0};

    // Build the command
    *(uint32_t *)write_buf = cmd;
    size_t write_size = 4 + extra_size;

    struct binder_write_read bwr = {0};
    bwr.write_size = write_size;
    bwr.write_buffer = (binder_uintptr_t)write_buf;
    bwr.read_size = sizeof(read_buf);
    bwr.read_buffer = (binder_uintptr_t)read_buf;

    printf("  Testing %-25s (0x%08x)... ", name, cmd);
    fflush(stdout);

    sig_received = 0;
    if (sigsetjmp(jmp_env, 1) == 0)
    {
        int ret = ioctl(fd, BINDER_WRITE_READ, &bwr);
        printf("ret=%d errno=%d consumed=%lu/%lu\n",
               ret, errno, bwr.write_consumed, bwr.read_consumed);
    }
    else
    {
        printf("SIGNAL %d!\n", sig_received);
    }
    fflush(stdout);
}

int main(int argc, char **argv)
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);

    printf("=== Binder Command Test ===\n\n");

    int fd = open("/dev/binder", O_RDWR);
    if (fd < 0)
    {
        printf("[-] Cannot open binder\n");
        return 1;
    }
    printf("[+] binder_fd = %d\n", fd);

    // Mmap binder
    void *map = mmap(NULL, 0x100000, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED)
    {
        printf("[-] mmap failed\n");
        close(fd);
        return 1;
    }
    printf("[+] Mapped at %p\n\n", map);

    // Test commands
    printf("[*] Testing BC commands:\n");

    // Commands without arguments
    test_binder_cmd(fd, BC_REGISTER_LOOPER, "BC_REGISTER_LOOPER", 0);
    test_binder_cmd(fd, BC_ENTER_LOOPER, "BC_ENTER_LOOPER", 0);
    test_binder_cmd(fd, BC_EXIT_LOOPER, "BC_EXIT_LOOPER", 0);

    // Commands with arguments
    test_binder_cmd(fd, BC_INCREFS, "BC_INCREFS", 4);
    test_binder_cmd(fd, BC_ACQUIRE, "BC_ACQUIRE", 4);
    test_binder_cmd(fd, BC_RELEASE, "BC_RELEASE", 4);
    test_binder_cmd(fd, BC_DECREFS, "BC_DECREFS", 4);
    test_binder_cmd(fd, BC_ACQUIRE_RESULT, "BC_ACQUIRE_RESULT", 4);
    test_binder_cmd(fd, BC_FREE_BUFFER, "BC_FREE_BUFFER", 8);
    test_binder_cmd(fd, BC_DEAD_BINDER_DONE, "BC_DEAD_BINDER_DONE", 8);

    // More complex commands
    test_binder_cmd(fd, BC_TRANSACTION, "BC_TRANSACTION", sizeof(struct binder_transaction_data));
    test_binder_cmd(fd, BC_REPLY, "BC_REPLY", sizeof(struct binder_transaction_data));

    // Others
    test_binder_cmd(fd, BC_REQUEST_DEATH_NOTIFICATION, "BC_REQUEST_DEATH_NOTIF", sizeof(struct binder_handle_cookie));
    test_binder_cmd(fd, BC_CLEAR_DEATH_NOTIFICATION, "BC_CLEAR_DEATH_NOTIF", sizeof(struct binder_handle_cookie));

    // Try reading from mapped memory
    printf("\n[*] Reading from mapped binder memory...\n");
    sig_received = 0;
    if (sigsetjmp(jmp_env, 1) == 0)
    {
        uint64_t val = *(volatile uint64_t *)map;
        printf("  Value at map base: 0x%lx\n", val);
    }
    else
    {
        printf("  SIGNAL %d reading map!\n", sig_received);
    }

    // Cleanup
    printf("\n[*] Cleanup...\n");
    munmap(map, 0x100000);
    close(fd);

    printf("\n=== Test Complete ===\n");
    return 0;
}
