/*
 * io_uring Vulnerability Scanner for Kernel 5.10.209
 * 測試 io_uring 相關 CVE
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <linux/io_uring.h>

#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter 426
#endif
#ifndef __NR_io_uring_register
#define __NR_io_uring_register 427
#endif

static inline int io_uring_setup(unsigned entries, struct io_uring_params *p)
{
    return syscall(__NR_io_uring_setup, entries, p);
}

static inline int io_uring_enter(int fd, unsigned to_submit, unsigned min_complete,
                                 unsigned flags, void *sig)
{
    return syscall(__NR_io_uring_enter, fd, to_submit, min_complete, flags, sig, 0);
}

static inline int io_uring_register(int fd, unsigned opcode, void *arg,
                                    unsigned nr_args)
{
    return syscall(__NR_io_uring_register, fd, opcode, arg, nr_args);
}

void test_basic_setup(void)
{
    printf("\n=== Basic io_uring Setup ===\n");

    struct io_uring_params params = {0};
    int ring_fd = io_uring_setup(8, &params);

    if (ring_fd >= 0)
    {
        printf("[OK] io_uring available! fd=%d\n", ring_fd);
        printf("  SQ entries: %u\n", params.sq_entries);
        printf("  CQ entries: %u\n", params.cq_entries);
        printf("  Features: 0x%x\n", params.features);
        printf("  SQ off: %u\n", params.sq_off.array);
        printf("  CQ off: %u\n", params.cq_off.cqes);
        close(ring_fd);
    }
    else
    {
        printf("[X] io_uring setup failed: errno=%d (%s)\n", errno, strerror(errno));
    }
}

void test_features(void)
{
    printf("\n=== io_uring Features ===\n");

    struct
    {
        unsigned flag;
        const char *name;
    } features[] = {
        {IORING_SETUP_IOPOLL, "IOPOLL"},
        {IORING_SETUP_SQPOLL, "SQPOLL"},
        {IORING_SETUP_SQ_AFF, "SQ_AFF"},
        {IORING_SETUP_CQSIZE, "CQSIZE"},
        {IORING_SETUP_CLAMP, "CLAMP"},
        {IORING_SETUP_ATTACH_WQ, "ATTACH_WQ"},
    };

    for (int i = 0; i < sizeof(features) / sizeof(features[0]); i++)
    {
        struct io_uring_params params = {0};
        params.flags = features[i].flag;

        // SQPOLL needs root
        if (features[i].flag == IORING_SETUP_SQPOLL)
        {
            params.sq_thread_idle = 1000;
        }

        int fd = io_uring_setup(8, &params);
        printf("  %-15s: ", features[i].name);
        if (fd >= 0)
        {
            printf("[OK] Supported\n");
            close(fd);
        }
        else if (errno == EPERM)
        {
            printf("[X] Permission denied\n");
        }
        else
        {
            printf("[X] errno=%d\n", errno);
        }
    }
}

void test_register_ops(void)
{
    printf("\n=== io_uring Register Operations ===\n");

    struct io_uring_params params = {0};
    int ring_fd = io_uring_setup(8, &params);
    if (ring_fd < 0)
    {
        printf("[X] Cannot create ring\n");
        return;
    }

    // Map the rings
    size_t sq_size = params.sq_off.array + params.sq_entries * sizeof(unsigned);
    size_t cq_size = params.cq_off.cqes + params.cq_entries * sizeof(struct io_uring_cqe);

    void *sq_ptr = mmap(NULL, sq_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_SQ_RING);
    void *cq_ptr = mmap(NULL, cq_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_CQ_RING);
    void *sqe_ptr = mmap(NULL, params.sq_entries * sizeof(struct io_uring_sqe),
                         PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                         ring_fd, IORING_OFF_SQES);

    if (sq_ptr == MAP_FAILED || cq_ptr == MAP_FAILED || sqe_ptr == MAP_FAILED)
    {
        printf("[X] mmap failed\n");
        close(ring_fd);
        return;
    }

    printf("  Ring mapped successfully\n");
    printf("  SQ: %p, CQ: %p, SQEs: %p\n", sq_ptr, cq_ptr, sqe_ptr);

    // Test register operations
    struct
    {
        unsigned op;
        const char *name;
    } reg_ops[] = {
        {IORING_REGISTER_BUFFERS, "REGISTER_BUFFERS"},
        {IORING_REGISTER_FILES, "REGISTER_FILES"},
        {IORING_REGISTER_EVENTFD, "REGISTER_EVENTFD"},
        {IORING_REGISTER_PROBE, "REGISTER_PROBE"},
        {IORING_REGISTER_PERSONALITY, "REGISTER_PERSONALITY"},
    };

    for (int i = 0; i < sizeof(reg_ops) / sizeof(reg_ops[0]); i++)
    {
        int ret;

        if (reg_ops[i].op == IORING_REGISTER_PROBE)
        {
            // Probe is special
            struct io_uring_probe probe = {0};
            ret = io_uring_register(ring_fd, IORING_REGISTER_PROBE, &probe, 0);
            printf("  %-25s: ", reg_ops[i].name);
            if (ret >= 0)
            {
                printf("[OK] last_op=%u, ops_len=%u\n", probe.last_op, probe.ops_len);
            }
            else
            {
                printf("[X] errno=%d\n", errno);
            }
        }
        else if (reg_ops[i].op == IORING_REGISTER_PERSONALITY)
        {
            ret = io_uring_register(ring_fd, IORING_REGISTER_PERSONALITY, NULL, 0);
            printf("  %-25s: ", reg_ops[i].name);
            if (ret >= 0)
            {
                printf("[OK] personality_id=%d\n", ret);
                // Unregister
                io_uring_register(ring_fd, IORING_UNREGISTER_PERSONALITY, NULL, ret);
            }
            else
            {
                printf("[X] errno=%d\n", errno);
            }
        }
        else
        {
            printf("  %-25s: (skipped, needs setup)\n", reg_ops[i].name);
        }
    }

    munmap(sq_ptr, sq_size);
    munmap(cq_ptr, cq_size);
    munmap(sqe_ptr, params.sq_entries * sizeof(struct io_uring_sqe));
    close(ring_fd);
}

void test_ops_available(void)
{
    printf("\n=== io_uring Operations Probe ===\n");

    struct io_uring_params params = {0};
    int ring_fd = io_uring_setup(8, &params);
    if (ring_fd < 0)
    {
        printf("[X] Cannot create ring\n");
        return;
    }

    // Allocate probe buffer
    size_t probe_size = sizeof(struct io_uring_probe) + 256 * sizeof(struct io_uring_probe_op);
    struct io_uring_probe *probe = malloc(probe_size);
    memset(probe, 0, probe_size);

    int ret = io_uring_register(ring_fd, IORING_REGISTER_PROBE, probe, 256);
    if (ret < 0)
    {
        printf("[X] Probe failed: errno=%d\n", errno);
        free(probe);
        close(ring_fd);
        return;
    }

    printf("  Last op: %u\n", probe->last_op);
    printf("  Supported operations:\n");

    const char *op_names[] = {
        "NOP",
        "READV",
        "WRITEV",
        "FSYNC",
        "READ_FIXED",
        "WRITE_FIXED",
        "POLL_ADD",
        "POLL_REMOVE",
        "SYNC_FILE_RANGE",
        "SENDMSG",
        "RECVMSG",
        "TIMEOUT",
        "TIMEOUT_REMOVE",
        "ACCEPT",
        "ASYNC_CANCEL",
        "LINK_TIMEOUT",
        "CONNECT",
        "FALLOCATE",
        "OPENAT",
        "CLOSE",
        "FILES_UPDATE",
        "STATX",
        "READ",
        "WRITE",
        "FADVISE",
        "MADVISE",
        "SEND",
        "RECV",
        "OPENAT2",
        "EPOLL_CTL",
        "SPLICE",
        "PROVIDE_BUFFERS",
        "REMOVE_BUFFERS",
        "TEE",
        "SHUTDOWN",
        "RENAMEAT",
        "UNLINKAT",
        "MKDIRAT",
        "SYMLINKAT",
        "LINKAT",
    };

    int count = 0;
    for (int i = 0; i <= probe->last_op && i < 40; i++)
    {
        if (probe->ops[i].flags & IO_URING_OP_SUPPORTED)
        {
            if (i < sizeof(op_names) / sizeof(op_names[0]))
            {
                printf("    [%2d] %s\n", i, op_names[i]);
            }
            else
            {
                printf("    [%2d] OP_%d\n", i, i);
            }
            count++;
        }
    }
    printf("  Total: %d operations supported\n", count);

    free(probe);
    close(ring_fd);
}

void test_cve_2021_41073(void)
{
    printf("\n=== CVE-2021-41073 (io_uring PROVIDE_BUFFERS) ===\n");
    printf("  Affected: 5.7 to 5.14.6\n");
    printf("  Target: 5.10.209 - likely patched\n");

    struct io_uring_params params = {0};
    int ring_fd = io_uring_setup(8, &params);
    if (ring_fd < 0)
    {
        printf("[X] Cannot create ring\n");
        return;
    }

    // Map rings
    size_t sq_size = params.sq_off.array + params.sq_entries * sizeof(unsigned);
    void *sq_ptr = mmap(NULL, sq_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_SQ_RING);
    void *sqe_ptr = mmap(NULL, params.sq_entries * sizeof(struct io_uring_sqe),
                         PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                         ring_fd, IORING_OFF_SQES);

    if (sq_ptr == MAP_FAILED || sqe_ptr == MAP_FAILED)
    {
        printf("[X] mmap failed\n");
        close(ring_fd);
        return;
    }

    // Get ring pointers
    unsigned *sq_array = (unsigned *)((char *)sq_ptr + params.sq_off.array);
    unsigned *sq_head = (unsigned *)((char *)sq_ptr + params.sq_off.head);
    unsigned *sq_tail = (unsigned *)((char *)sq_ptr + params.sq_off.tail);
    struct io_uring_sqe *sqes = sqe_ptr;

    // Try PROVIDE_BUFFERS with edge case values
    char *buf = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    struct io_uring_sqe *sqe = &sqes[0];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_PROVIDE_BUFFERS;
    sqe->fd = 1; // nbufs
    sqe->addr = (unsigned long)buf;
    sqe->len = 4096;
    sqe->off = 0;       // bgid
    sqe->buf_index = 0; // bid

    sq_array[0] = 0;
    *sq_tail = 1;
    __sync_synchronize();

    int ret = io_uring_enter(ring_fd, 1, 1, IORING_ENTER_GETEVENTS, NULL);
    printf("  PROVIDE_BUFFERS: enter ret=%d\n", ret);

    munmap(buf, 4096);
    munmap(sq_ptr, sq_size);
    munmap(sqe_ptr, params.sq_entries * sizeof(struct io_uring_sqe));
    close(ring_fd);
}

void test_double_free_pattern(void)
{
    printf("\n=== Double Free Pattern Test ===\n");

    struct io_uring_params params = {0};
    int ring_fd = io_uring_setup(64, &params);
    if (ring_fd < 0)
    {
        printf("[X] Cannot create ring\n");
        return;
    }

    // Quick setup
    size_t sq_size = params.sq_off.array + params.sq_entries * sizeof(unsigned);
    void *sq_ptr = mmap(NULL, sq_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_SQ_RING);
    void *sqe_ptr = mmap(NULL, params.sq_entries * sizeof(struct io_uring_sqe),
                         PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                         ring_fd, IORING_OFF_SQES);

    if (sq_ptr == MAP_FAILED || sqe_ptr == MAP_FAILED)
    {
        printf("[X] mmap failed\n");
        close(ring_fd);
        return;
    }

    // Try to trigger race conditions with rapid operations
    unsigned *sq_tail = (unsigned *)((char *)sq_ptr + params.sq_off.tail);
    unsigned *sq_array = (unsigned *)((char *)sq_ptr + params.sq_off.array);
    struct io_uring_sqe *sqes = sqe_ptr;

    // Submit many NOPs rapidly
    for (int i = 0; i < 32; i++)
    {
        sqes[i].opcode = IORING_OP_NOP;
        sqes[i].user_data = i;
        sq_array[i] = i;
    }
    *sq_tail = 32;
    __sync_synchronize();

    int ret = io_uring_enter(ring_fd, 32, 32, IORING_ENTER_GETEVENTS, NULL);
    printf("  Rapid NOP submit: ret=%d\n", ret);

    munmap(sq_ptr, sq_size);
    munmap(sqe_ptr, params.sq_entries * sizeof(struct io_uring_sqe));
    close(ring_fd);
    printf("  [OK] No crash detected\n");
}

int main(void)
{
    printf("==========================================\n");
    printf("io_uring Vulnerability Scanner\n");
    printf("Target: Kernel 5.10.209 Rokid AR\n");
    printf("==========================================\n");

    test_basic_setup();
    test_features();
    test_register_ops();
    test_ops_available();
    test_cve_2021_41073();
    test_double_free_pattern();

    printf("\n==========================================\n");
    printf("Scan Complete\n");
    printf("==========================================\n");

    return 0;
}
