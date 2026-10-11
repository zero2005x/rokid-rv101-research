/*
 * io_uring_capability_probe.c
 * CVE-2024-0582 ?�利?�性探測工??
 *
 * 測試?��?:
 * 1. IORING_REGISTER_BUFFERS - CVE-2024-0582 ?��??��?
 * 2. IORING_REGISTER_FILES - ?�件註�??�能
 * 3. IORING_OP_PROVIDE_BUFFERS - ?��?緩�??�?��?
 * 4. Ring buffer mmap ?�能
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
#include <stdint.h>

/*
 * io_uring 系統調用??(ARM64)
 */
#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter 426
#endif
#ifndef __NR_io_uring_register
#define __NR_io_uring_register 427
#endif

/*
 * io_uring 結�?體�?�?
 */
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

/*
 * io_uring_sqe - Submission Queue Entry
 */
struct io_uring_sqe
{
    uint8_t opcode;
    uint8_t flags;
    uint16_t ioprio;
    int32_t fd;
    union
    {
        uint64_t off;
        uint64_t addr2;
    };
    union
    {
        uint64_t addr;
        uint64_t splice_off_in;
    };
    uint32_t len;
    union
    {
        uint32_t rw_flags;
        uint32_t fsync_flags;
        uint32_t poll_events;
        uint32_t poll32_events;
        uint32_t sync_range_flags;
        uint32_t msg_flags;
        uint32_t timeout_flags;
        uint32_t accept_flags;
        uint32_t cancel_flags;
        uint32_t open_flags;
        uint32_t statx_flags;
        uint32_t fadvise_advice;
        uint32_t splice_flags;
        uint32_t rename_flags;
        uint32_t unlink_flags;
        uint32_t hardlink_flags;
    };
    uint64_t user_data;
    union
    {
        uint16_t buf_index;
        uint16_t buf_group;
    };
    uint16_t personality;
    union
    {
        int32_t splice_fd_in;
        uint32_t file_index;
    };
    uint64_t __pad2[2];
};

/*
 * io_uring_cqe - Completion Queue Entry
 */
struct io_uring_cqe
{
    uint64_t user_data;
    int32_t res;
    uint32_t flags;
};

/*
 * iovec 結�?�?
 */
struct iovec
{
    void *iov_base;
    size_t iov_len;
};

/*
 * io_uring register ?��?�?
 */
#define IORING_REGISTER_BUFFERS 0
#define IORING_UNREGISTER_BUFFERS 1
#define IORING_REGISTER_FILES 2
#define IORING_UNREGISTER_FILES 3
#define IORING_REGISTER_EVENTFD 4
#define IORING_UNREGISTER_EVENTFD 5
#define IORING_REGISTER_FILES_UPDATE 6
#define IORING_REGISTER_EVENTFD_ASYNC 7
#define IORING_REGISTER_PROBE 8
#define IORING_REGISTER_PERSONALITY 9
#define IORING_UNREGISTER_PERSONALITY 10
#define IORING_REGISTER_RESTRICTIONS 11
#define IORING_REGISTER_ENABLE_RINGS 12
#define IORING_REGISTER_PBUF_RING 22
#define IORING_UNREGISTER_PBUF_RING 23

/*
 * io_uring ?��?�?
 */
#define IORING_OP_NOP 0
#define IORING_OP_READV 1
#define IORING_OP_WRITEV 2
#define IORING_OP_FSYNC 3
#define IORING_OP_READ_FIXED 4
#define IORING_OP_WRITE_FIXED 5
#define IORING_OP_POLL_ADD 6
#define IORING_OP_POLL_REMOVE 7
#define IORING_OP_SYNC_FILE_RANGE 8
#define IORING_OP_SENDMSG 9
#define IORING_OP_RECVMSG 10
#define IORING_OP_TIMEOUT 11
#define IORING_OP_TIMEOUT_REMOVE 12
#define IORING_OP_ACCEPT 13
#define IORING_OP_ASYNC_CANCEL 14
#define IORING_OP_LINK_TIMEOUT 15
#define IORING_OP_CONNECT 16
#define IORING_OP_FALLOCATE 17
#define IORING_OP_OPENAT 18
#define IORING_OP_CLOSE 19
#define IORING_OP_FILES_UPDATE 20
#define IORING_OP_STATX 21
#define IORING_OP_READ 22
#define IORING_OP_WRITE 23
#define IORING_OP_FADVISE 24
#define IORING_OP_MADVISE 25
#define IORING_OP_SEND 26
#define IORING_OP_RECV 27
#define IORING_OP_OPENAT2 28
#define IORING_OP_EPOLL_CTL 29
#define IORING_OP_SPLICE 30
#define IORING_OP_PROVIDE_BUFFERS 31
#define IORING_OP_REMOVE_BUFFERS 32

/*
 * io_uring features
 */
#define IORING_FEAT_SINGLE_MMAP (1U << 0)
#define IORING_FEAT_NODROP (1U << 1)
#define IORING_FEAT_SUBMIT_STABLE (1U << 2)
#define IORING_FEAT_RW_CUR_POS (1U << 3)
#define IORING_FEAT_CUR_PERSONALITY (1U << 4)
#define IORING_FEAT_FAST_POLL (1U << 5)
#define IORING_FEAT_POLL_32BITS (1U << 6)
#define IORING_FEAT_SQPOLL_NONFIXED (1U << 7)
#define IORING_FEAT_EXT_ARG (1U << 8)
#define IORING_FEAT_NATIVE_WORKERS (1U << 9)
#define IORING_FEAT_RSRC_TAGS (1U << 10)

/*
 * mmap offsets
 */
#define IORING_OFF_SQ_RING 0ULL
#define IORING_OFF_CQ_RING 0x8000000ULL
#define IORING_OFF_SQES 0x10000000ULL

/*
 * io_uring_probe 結�?
 */
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

#define IO_URING_OP_SUPPORTED (1U << 0)

/*
 * 系統調用封�?
 */
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

/*
 * �?? features 標�?
 */
void print_features(uint32_t features)
{
    printf("  Features (0x%x):\n", features);
    if (features & IORING_FEAT_SINGLE_MMAP)
        printf("    - SINGLE_MMAP\n");
    if (features & IORING_FEAT_NODROP)
        printf("    - NODROP\n");
    if (features & IORING_FEAT_SUBMIT_STABLE)
        printf("    - SUBMIT_STABLE\n");
    if (features & IORING_FEAT_RW_CUR_POS)
        printf("    - RW_CUR_POS\n");
    if (features & IORING_FEAT_CUR_PERSONALITY)
        printf("    - CUR_PERSONALITY\n");
    if (features & IORING_FEAT_FAST_POLL)
        printf("    - FAST_POLL\n");
    if (features & IORING_FEAT_POLL_32BITS)
        printf("    - POLL_32BITS\n");
    if (features & IORING_FEAT_SQPOLL_NONFIXED)
        printf("    - SQPOLL_NONFIXED\n");
    if (features & IORING_FEAT_EXT_ARG)
        printf("    - EXT_ARG\n");
    if (features & IORING_FEAT_NATIVE_WORKERS)
        printf("    - NATIVE_WORKERS\n");
    if (features & IORING_FEAT_RSRC_TAGS)
        printf("    - RSRC_TAGS\n");
}

/*
 * 主�?�?
 */
int main(void)
{
    int ret;
    int success_count = 0;
    int total_tests = 0;

    printf("============================================================\n");
    printf("  CVE-2024-0582 io_uring ?��??�測工具\n");
    printf("  Target: Rokid AR Glasses (Linux 5.10.209)\n");
    printf("============================================================\n\n");

    /*
     * 測試 1: io_uring_setup
     */
    printf("[TEST 1] io_uring_setup()\n");
    total_tests++;

    struct io_uring_params params;
    memset(&params, 0, sizeof(params));

    int ring_fd = io_uring_setup(64, &params);
    if (ring_fd < 0)
    {
        printf("  ??FAILED: %s (errno=%d)\n", strerror(errno), errno);
        printf("\n??io_uring 不可?��??��?繼�?測試\n");
        return 1;
    }

    printf("  ??SUCCESS! ring_fd=%d\n", ring_fd);
    printf("  SQ entries: %u, CQ entries: %u\n", params.sq_entries, params.cq_entries);
    print_features(params.features);
    success_count++;

    /*
     * 測試 2: mmap SQ/CQ rings
     */
    printf("\n[TEST 2] mmap SQ/CQ rings\n");
    total_tests++;

    size_t sq_ring_size = params.sq_off.array + params.sq_entries * sizeof(uint32_t);
    size_t cq_ring_size = params.cq_off.cqes + params.cq_entries * sizeof(struct io_uring_cqe);
    size_t sqes_size = params.sq_entries * sizeof(struct io_uring_sqe);

    void *sq_ring = mmap(NULL, sq_ring_size, PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_SQ_RING);
    void *sqes = mmap(NULL, sqes_size, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_SQES);

    if (sq_ring == MAP_FAILED || sqes == MAP_FAILED)
    {
        printf("  ??FAILED: %s\n", strerror(errno));
    }
    else
    {
        printf("  ??SUCCESS!\n");
        printf("  SQ ring: %p (size: %zu)\n", sq_ring, sq_ring_size);
        printf("  SQEs:    %p (size: %zu)\n", sqes, sqes_size);
        success_count++;
    }

    /*
     * 測試 3: IORING_REGISTER_BUFFERS (CVE-2024-0582 ?��??��?)
     */
    printf("\n[TEST 3] IORING_REGISTER_BUFFERS (?��? CVE-2024-0582 ?�鍵?�能)\n");
    total_tests++;

    void *user_buf = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (user_buf == MAP_FAILED)
    {
        printf("  ??FAILED: ?��??��??�戶緩�??�\n");
    }
    else
    {
        struct iovec iov = {
            .iov_base = user_buf,
            .iov_len = 4096};

        ret = io_uring_register(ring_fd, IORING_REGISTER_BUFFERS, &iov, 1);
        if (ret < 0)
        {
            printf("  ??FAILED: %s (errno=%d)\n", strerror(errno), errno);
            printf("     ?�是 CVE-2024-0582 ?�核心�??��?不可?��??��??�用\n");
        }
        else
        {
            printf("  ??SUCCESS! 緩�??�已註?�\n");
            printf("  ?�� CVE-2024-0582 漏�??�已?�露!\n");
            success_count++;

            // ?�試註銷
            ret = io_uring_register(ring_fd, IORING_UNREGISTER_BUFFERS, NULL, 0);
            printf("  UNREGISTER_BUFFERS: %s\n", ret < 0 ? "failed" : "success");
        }
        munmap(user_buf, 4096);
    }

    /*
     * 測試 4: IORING_REGISTER_FILES
     */
    printf("\n[TEST 4] IORING_REGISTER_FILES\n");
    total_tests++;

    int file_fd = open("/dev/null", O_RDWR);
    if (file_fd < 0)
    {
        printf("  ??FAILED: ?��??��? /dev/null\n");
    }
    else
    {
        ret = io_uring_register(ring_fd, IORING_REGISTER_FILES, &file_fd, 1);
        if (ret < 0)
        {
            printf("  ??FAILED: %s (errno=%d)\n", strerror(errno), errno);
        }
        else
        {
            printf("  ??SUCCESS! ?�件已註?�\n");
            success_count++;

            // 註銷
            io_uring_register(ring_fd, IORING_UNREGISTER_FILES, NULL, 0);
        }
        close(file_fd);
    }

    /*
     * 測試 5: IORING_REGISTER_PROBE (?�詢?�援?��?�?
     */
    printf("\n[TEST 5] IORING_REGISTER_PROBE (?�詢?�援?��?�?\n");
    total_tests++;

    size_t probe_size = sizeof(struct io_uring_probe) +
                        IORING_OP_REMOVE_BUFFERS * sizeof(struct io_uring_probe_op);
    struct io_uring_probe *probe = calloc(1, probe_size);
    if (!probe)
    {
        printf("  ??FAILED: 記憶體�??�失?�\n");
    }
    else
    {
        ret = io_uring_register(ring_fd, IORING_REGISTER_PROBE, probe, IORING_OP_REMOVE_BUFFERS);
        if (ret < 0)
        {
            printf("  ??FAILED: %s (errno=%d)\n", strerror(errno), errno);
        }
        else
        {
            printf("  ??SUCCESS! last_op=%d, ops_len=%d\n", probe->last_op, probe->ops_len);
            success_count++;

            // 檢查?�鍵?��?
            printf("  ?�鍵?��??�援?�??\n");

            const char *critical_ops[] = {
                "NOP", "READV", "WRITEV", "FSYNC",
                "READ_FIXED", "WRITE_FIXED", "POLL_ADD"};
            int critical_op_ids[] = {
                IORING_OP_NOP, IORING_OP_READV, IORING_OP_WRITEV, IORING_OP_FSYNC,
                IORING_OP_READ_FIXED, IORING_OP_WRITE_FIXED, IORING_OP_POLL_ADD};

            for (int i = 0; i < 7; i++)
            {
                int op = critical_op_ids[i];
                if (op < probe->ops_len)
                {
                    int supported = probe->ops[op].flags & IO_URING_OP_SUPPORTED;
                    printf("    - %s: %s\n", critical_ops[i],
                           supported ? "???�援" : "??不支??);
                }
            }

            // ?�別檢查 PROVIDE_BUFFERS ??REMOVE_BUFFERS
            if (IORING_OP_PROVIDE_BUFFERS < probe->ops_len)
            {
                int supported = probe->ops[IORING_OP_PROVIDE_BUFFERS].flags & IO_URING_OP_SUPPORTED;
                printf("    - PROVIDE_BUFFERS: %s (CVE-2024-0582 ?��?)\n",
                       supported ? "???�援" : "??不支??);
            }
            if (IORING_OP_REMOVE_BUFFERS < probe->ops_len)
            {
                int supported = probe->ops[IORING_OP_REMOVE_BUFFERS].flags & IO_URING_OP_SUPPORTED;
                printf("    - REMOVE_BUFFERS: %s (CVE-2024-0582 ?��?)\n",
                       supported ? "???�援" : "??不支??);
            }
        }
        free(probe);
    }

    /*
     * 測試 6: 多緩衝�?註�? (壓�?測試)
     */
    printf("\n[TEST 6] 多緩衝�?註�? (壓�?測試)\n");
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
        printf("  ??FAILED: 緩�??�?��?失�?\n");
    }
    else
    {
        ret = io_uring_register(ring_fd, IORING_REGISTER_BUFFERS, iovs, NUM_BUFS);
        if (ret < 0)
        {
            printf("  ??FAILED: %s (errno=%d)\n", strerror(errno), errno);
        }
        else
        {
            printf("  ??SUCCESS! 已註??%d ?�緩衝�?\n", NUM_BUFS);
            success_count++;

            // 註銷
            io_uring_register(ring_fd, IORING_UNREGISTER_BUFFERS, NULL, 0);
        }
    }

    // 清�?
    for (int i = 0; i < NUM_BUFS && bufs[i] != MAP_FAILED; i++)
    {
        munmap(bufs[i], 4096);
    }

    /*
     * 測試 7: ?�交一??NOP ?��?
     */
    printf("\n[TEST 7] ?�交 NOP ?��? (驗�?完整 I/O 路�?)\n");
    total_tests++;

    if (sq_ring != MAP_FAILED && sqes != MAP_FAILED)
    {
        // ?��? ring ?��?
        uint32_t *sq_head = (uint32_t *)((char *)sq_ring + params.sq_off.head);
        uint32_t *sq_tail = (uint32_t *)((char *)sq_ring + params.sq_off.tail);
        uint32_t *sq_array = (uint32_t *)((char *)sq_ring + params.sq_off.array);
        uint32_t sq_mask = params.sq_entries - 1;

        // 填�? SQE
        struct io_uring_sqe *sqe = &((struct io_uring_sqe *)sqes)[*sq_tail & sq_mask];
        memset(sqe, 0, sizeof(*sqe));
        sqe->opcode = IORING_OP_NOP;
        sqe->user_data = 0x1234;

        // ?�新 sq_array ??tail
        sq_array[*sq_tail & sq_mask] = *sq_tail & sq_mask;
        __sync_synchronize(); // memory barrier
        (*sq_tail)++;

        // ?�交
        ret = io_uring_enter(ring_fd, 1, 1, 0, NULL);
        if (ret < 0)
        {
            printf("  ??FAILED: io_uring_enter: %s (errno=%d)\n", strerror(errno), errno);
        }
        else
        {
            printf("  ??SUCCESS! io_uring_enter 返�? %d\n", ret);
            success_count++;
        }
    }
    else
    {
        printf("  ?��? SKIPPED: ring ?�正確�?射\n");
    }

    /*
     * 清�?
     */
    if (sq_ring != MAP_FAILED)
        munmap(sq_ring, sq_ring_size);
    if (sqes != MAP_FAILED)
        munmap(sqes, sqes_size);
    close(ring_fd);

    /*
     * 總�?
     */
    printf("\n============================================================\n");
    printf("  測試結�?總�?\n");
    printf("============================================================\n");
    printf("  ?��?測試: %d / %d\n\n", success_count, total_tests);

    if (success_count >= 5)
    {
        printf("  ?�� CVE-2024-0582 ?�用條件滿足!\n");
        printf("  \n");
        printf("  ??io_uring_setup: ?��?\n");
        printf("  ??REGISTER_BUFFERS: ?��? (?��??�能)\n");
        printf("  ??Ring buffer mmap: ?��?\n");
        printf("  \n");
        printf("  ?? 下�?�?\n");
        printf("     1. 構�?CVE-2024-0582 PoC\n");
        printf("     2. ?�用 mmap UAF 實現任�?記憶體�?寫\n");
        printf("     3. 覆寫 cred 結�??��? root\n");
    }
    else if (success_count >= 3)
    {
        printf("  ?��? ?��??�能?�用，�?要進�?步�?證\n");
    }
    else
    {
        printf("  ??io_uring ?�能?��?，CVE-2024-0582 ?�能?��??�用\n");
    }

    printf("============================================================\n");

    return 0;
}
