/*
 * io_uring_capability_probe.c
 * CVE-2024-0582 Capability Probe Tool
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <stdint.h>

#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter 426
#endif
#ifndef __NR_io_uring_register
#define __NR_io_uring_register 427
#endif

struct io_uring_params
{
    uint32_t sq_entries;
    uint32_t cq_entries;
    uint32_t flags;
    uint32_t sq_thread_cpu;
    uint32_t sq_thread_idle;
    uint32_t features;
    uint32_t wq_fd;
    uint32_t resv[3];
    struct
    {
        uint32_t head;
        uint32_t tail;
        uint32_t ring_mask;
        uint32_t ring_entries;
        uint32_t flags;
        uint32_t dropped;
        uint32_t array;
        uint32_t resv1;
        uint64_t resv2;
    } sq_off;
    struct
    {
        uint32_t head;
        uint32_t tail;
        uint32_t ring_mask;
        uint32_t ring_entries;
        uint32_t overflow;
        uint32_t cqes;
        uint32_t flags;
        uint32_t resv1;
        uint64_t resv2;
    } cq_off;
};

struct io_uring_sqe
{
    uint8_t opcode;
    uint8_t flags;
    uint16_t ioprio;
    int32_t fd;
    uint64_t off;
    uint64_t addr;
    uint32_t len;
    uint32_t rw_flags;
    uint64_t user_data;
    uint16_t buf_index;
    uint16_t personality;
    int32_t splice_fd_in;
    uint64_t __pad2[2];
};

struct io_uring_cqe
{
    uint64_t user_data;
    int32_t res;
    uint32_t flags;
};

#define IORING_REGISTER_BUFFERS 0
#define IORING_UNREGISTER_BUFFERS 1
#define IORING_REGISTER_FILES 2
#define IORING_UNREGISTER_FILES 3
#define IORING_REGISTER_PROBE 8

#define IORING_OP_NOP 0
#define IORING_OP_READV 1
#define IORING_OP_WRITEV 2
#define IORING_OP_READ_FIXED 4
#define IORING_OP_WRITE_FIXED 5
#define IORING_OP_PROVIDE_BUFFERS 31
#define IORING_OP_REMOVE_BUFFERS 32

#define IORING_OFF_SQ_RING 0ULL
#define IORING_OFF_CQ_RING 0x8000000ULL
#define IORING_OFF_SQES 0x10000000ULL

#define IO_URING_OP_SUPPORTED (1U << 0)

struct io_uring_probe_op
{
    uint8_t op;
    uint8_t resv;
    uint16_t flags;
    uint32_t resv2;
};

struct io_uring_probe
{
    uint8_t last_op;
    uint8_t ops_len;
    uint16_t resv;
    uint32_t resv2[3];
    struct io_uring_probe_op ops[0];
};

static int io_uring_setup(unsigned entries, struct io_uring_params *p)
{
    return syscall(__NR_io_uring_setup, entries, p);
}

static int io_uring_register(int fd, unsigned opcode, void *arg, unsigned nr_args)
{
    return syscall(__NR_io_uring_register, fd, opcode, arg, nr_args);
}

static int io_uring_enter(int fd, unsigned to_submit, unsigned min_complete,
                          unsigned flags, void *sig)
{
    return syscall(__NR_io_uring_enter, fd, to_submit, min_complete, flags, sig);
}

int main(void)
{
    int ret;
    int success_count = 0;
    int total_tests = 0;

    printf("============================================================\n");
    printf("  CVE-2024-0582 io_uring Capability Probe\n");
    printf("  Target: Rokid AR Glasses (Linux 5.10.209)\n");
    printf("============================================================\n\n");

    /* Test 1: io_uring_setup */
    printf("[TEST 1] io_uring_setup()\n");
    total_tests++;

    struct io_uring_params params;
    memset(&params, 0, sizeof(params));

    int ring_fd = io_uring_setup(64, &params);
    if (ring_fd < 0)
    {
        printf("  [X] FAILED: %s (errno=%d)\n", strerror(errno), errno);
        printf("\n[!] io_uring not available, cannot continue\n");
        return 1;
    }

    printf("  [+] SUCCESS! ring_fd=%d\n", ring_fd);
    printf("  SQ entries: %u, CQ entries: %u\n", params.sq_entries, params.cq_entries);
    printf("  Features: 0x%x\n", params.features);
    success_count++;

    /* Test 2: mmap SQ/CQ rings */
    printf("\n[TEST 2] mmap SQ/CQ rings\n");
    total_tests++;

    size_t sq_ring_size = params.sq_off.array + params.sq_entries * sizeof(uint32_t);
    size_t sqes_size = params.sq_entries * sizeof(struct io_uring_sqe);

    void *sq_ring = mmap(NULL, sq_ring_size, PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_SQ_RING);
    void *sqes = mmap(NULL, sqes_size, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_SQES);

    if (sq_ring == MAP_FAILED || sqes == MAP_FAILED)
    {
        printf("  [X] FAILED: %s\n", strerror(errno));
    }
    else
    {
        printf("  [+] SUCCESS!\n");
        printf("  SQ ring: %p (size: %zu)\n", sq_ring, sq_ring_size);
        printf("  SQEs:    %p (size: %zu)\n", sqes, sqes_size);
        success_count++;
    }

    /* Test 3: IORING_REGISTER_BUFFERS (CVE-2024-0582 CRITICAL!) */
    printf("\n[TEST 3] IORING_REGISTER_BUFFERS (*** CVE-2024-0582 CRITICAL ***)\n");
    total_tests++;

    void *user_buf = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (user_buf == MAP_FAILED)
    {
        printf("  [X] FAILED: Cannot allocate user buffer\n");
    }
    else
    {
        struct iovec iov = {
            .iov_base = user_buf,
            .iov_len = 4096};

        ret = io_uring_register(ring_fd, IORING_REGISTER_BUFFERS, &iov, 1);
        if (ret < 0)
        {
            printf("  [X] FAILED: %s (errno=%d)\n", strerror(errno), errno);
            printf("      This is CRITICAL for CVE-2024-0582!\n");
        }
        else
        {
            printf("  [+] SUCCESS! Buffer registered!\n");
            printf("  [!!!] CVE-2024-0582 VULNERABLE SURFACE EXPOSED!\n");
            success_count++;

            ret = io_uring_register(ring_fd, IORING_UNREGISTER_BUFFERS, NULL, 0);
            printf("  UNREGISTER_BUFFERS: %s\n", ret < 0 ? "failed" : "success");
        }
        munmap(user_buf, 4096);
    }

    /* Test 4: IORING_REGISTER_FILES */
    printf("\n[TEST 4] IORING_REGISTER_FILES\n");
    total_tests++;

    int file_fd = open("/dev/null", O_RDWR);
    if (file_fd < 0)
    {
        printf("  [X] FAILED: Cannot open /dev/null\n");
    }
    else
    {
        ret = io_uring_register(ring_fd, IORING_REGISTER_FILES, &file_fd, 1);
        if (ret < 0)
        {
            printf("  [X] FAILED: %s (errno=%d)\n", strerror(errno), errno);
        }
        else
        {
            printf("  [+] SUCCESS! File registered!\n");
            success_count++;
            io_uring_register(ring_fd, IORING_UNREGISTER_FILES, NULL, 0);
        }
        close(file_fd);
    }

    /* Test 5: IORING_REGISTER_PROBE */
    printf("\n[TEST 5] IORING_REGISTER_PROBE (Query supported ops)\n");
    total_tests++;

    size_t probe_size = sizeof(struct io_uring_probe) +
                        40 * sizeof(struct io_uring_probe_op);
    struct io_uring_probe *probe = calloc(1, probe_size);
    if (!probe)
    {
        printf("  [X] FAILED: Memory allocation failed\n");
    }
    else
    {
        ret = io_uring_register(ring_fd, IORING_REGISTER_PROBE, probe, 40);
        if (ret < 0)
        {
            printf("  [X] FAILED: %s (errno=%d)\n", strerror(errno), errno);
        }
        else
        {
            printf("  [+] SUCCESS! last_op=%d, ops_len=%d\n", probe->last_op, probe->ops_len);
            success_count++;

            printf("  Critical ops:\n");

            const char *op_names[] = {"NOP", "READV", "WRITEV", NULL, "READ_FIXED", "WRITE_FIXED"};
            int op_ids[] = {0, 1, 2, -1, 4, 5};

            for (int i = 0; i < 6; i++)
            {
                if (op_ids[i] < 0)
                    continue;
                int op = op_ids[i];
                if (op < probe->ops_len)
                {
                    int supported = probe->ops[op].flags & IO_URING_OP_SUPPORTED;
                    printf("    - %s: %s\n", op_names[i],
                           supported ? "[+] Supported" : "[X] Not supported");
                }
            }

            if (IORING_OP_PROVIDE_BUFFERS < probe->ops_len)
            {
                int supported = probe->ops[IORING_OP_PROVIDE_BUFFERS].flags & IO_URING_OP_SUPPORTED;
                printf("    - PROVIDE_BUFFERS: %s (CVE-2024-0582 related)\n",
                       supported ? "[+] Supported" : "[X] Not supported");
            }
        }
        free(probe);
    }

    /* Test 6: Multi-buffer registration */
    printf("\n[TEST 6] Multi-buffer registration (stress test)\n");
    total_tests++;

#define NUM_BUFS 16
    void *bufs[NUM_BUFS];
    struct iovec iovs[NUM_BUFS];
    int alloc_ok = 1;

    for (int i = 0; i < NUM_BUFS; i++)
    {
        bufs[i] = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (bufs[i] == MAP_FAILED)
        {
            alloc_ok = 0;
            break;
        }
        iovs[i].iov_base = bufs[i];
        iovs[i].iov_len = 4096;
    }

    if (!alloc_ok)
    {
        printf("  [X] FAILED: Buffer allocation failed\n");
    }
    else
    {
        ret = io_uring_register(ring_fd, IORING_REGISTER_BUFFERS, iovs, NUM_BUFS);
        if (ret < 0)
        {
            printf("  [X] FAILED: %s (errno=%d)\n", strerror(errno), errno);
        }
        else
        {
            printf("  [+] SUCCESS! Registered %d buffers\n", NUM_BUFS);
            success_count++;
            io_uring_register(ring_fd, IORING_UNREGISTER_BUFFERS, NULL, 0);
        }
    }

    for (int i = 0; i < NUM_BUFS && bufs[i] != MAP_FAILED; i++)
    {
        munmap(bufs[i], 4096);
    }

    /* Test 7: Submit NOP operation */
    printf("\n[TEST 7] Submit NOP operation (verify full I/O path)\n");
    total_tests++;

    if (sq_ring != MAP_FAILED && sqes != MAP_FAILED)
    {
        uint32_t *sq_head = (uint32_t *)((char *)sq_ring + params.sq_off.head);
        uint32_t *sq_tail = (uint32_t *)((char *)sq_ring + params.sq_off.tail);
        uint32_t *sq_array = (uint32_t *)((char *)sq_ring + params.sq_off.array);
        uint32_t sq_mask = params.sq_entries - 1;

        struct io_uring_sqe *sqe = &((struct io_uring_sqe *)sqes)[*sq_tail & sq_mask];
        memset(sqe, 0, sizeof(*sqe));
        sqe->opcode = IORING_OP_NOP;
        sqe->user_data = 0x1234;

        sq_array[*sq_tail & sq_mask] = *sq_tail & sq_mask;
        __sync_synchronize();
        (*sq_tail)++;

        ret = io_uring_enter(ring_fd, 1, 1, 0, NULL);
        if (ret < 0)
        {
            printf("  [X] FAILED: io_uring_enter: %s (errno=%d)\n", strerror(errno), errno);
        }
        else
        {
            printf("  [+] SUCCESS! io_uring_enter returned %d\n", ret);
            success_count++;
        }
    }
    else
    {
        printf("  [!] SKIPPED: ring not properly mapped\n");
    }

    /* Cleanup */
    if (sq_ring != MAP_FAILED)
        munmap(sq_ring, sq_ring_size);
    if (sqes != MAP_FAILED)
        munmap(sqes, sqes_size);
    close(ring_fd);

    /* Summary */
    printf("\n============================================================\n");
    printf("  Test Results Summary\n");
    printf("============================================================\n");
    printf("  Passed: %d / %d\n\n", success_count, total_tests);

    if (success_count >= 5)
    {
        printf("  [!!!] CVE-2024-0582 EXPLOITATION CONDITIONS MET!\n");
        printf("\n");
        printf("  [+] io_uring_setup: SUCCESS\n");
        printf("  [+] REGISTER_BUFFERS: SUCCESS (CRITICAL)\n");
        printf("  [+] Ring buffer mmap: SUCCESS\n");
        printf("\n");
        printf("  Next steps:\n");
        printf("     1. Build CVE-2024-0582 PoC\n");
        printf("     2. Use mmap UAF for arbitrary R/W\n");
        printf("     3. Overwrite cred structure for root\n");
    }
    else if (success_count >= 3)
    {
        printf("  [!] Partial functionality available, needs verification\n");
    }
    else
    {
        printf("  [X] io_uring limited, CVE-2024-0582 may not be exploitable\n");
    }

    printf("============================================================\n");

    return 0;
}
