#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <stdint.h>
#include <linux/keyctl.h>

#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter 426
#endif

#define IORING_OP_READ 22
#define IORING_OP_PROVIDE_BUFFERS 31
#define IORING_OP_REMOVE_BUFFERS 32
#define IOSQE_BUFFER_SELECT (1U << 1)

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
        uint32_t buf_index;
        uint32_t buf_group;
    };
    uint64_t user_data;
    union
    {
        uint16_t buf_index_16;
        uint16_t buf_group_16;
    };
    int32_t personality;
    int32_t splice_fd_in;
    uint64_t __pad2[2];
};

struct io_uring_cqe
{
    uint64_t user_data;
    int32_t res;
    uint32_t flags;
};

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

struct io_uring_ring
{
    int ring_fd;
    struct io_uring_params params;
    void *sq_ptr;
    void *cq_ptr;
    struct io_uring_sqe *sqes;
    struct io_uring_cqe *cqes;
    unsigned *sq_head, *sq_tail, *sq_mask, *sq_array;
    unsigned *cq_head, *cq_tail, *cq_mask;
};

static int io_uring_setup(unsigned entries, struct io_uring_params *p)
{
    return syscall(__NR_io_uring_setup, entries, p);
}

static int io_uring_enter(int fd, unsigned to_submit, unsigned min_complete,
                          unsigned flags, void *sig)
{
    return syscall(__NR_io_uring_enter, fd, to_submit, min_complete, flags, sig);
}

static int setup_uring(struct io_uring_ring *ring)
{
    memset(&ring->params, 0, sizeof(ring->params));
    ring->ring_fd = io_uring_setup(32, &ring->params);
    if (ring->ring_fd < 0)
        return -1;

    size_t sq_ring_sz = ring->params.sq_off.array + ring->params.sq_entries * sizeof(unsigned);
    size_t cq_ring_sz = ring->params.cq_off.cqes + ring->params.cq_entries * sizeof(struct io_uring_cqe);
    size_t sqes_sz = ring->params.sq_entries * sizeof(struct io_uring_sqe);

    ring->sq_ptr = mmap(0, sq_ring_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring->ring_fd, 0);
    ring->cq_ptr = mmap(0, cq_ring_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring->ring_fd, 0x8000000ULL);
    ring->sqes = mmap(0, sqes_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring->ring_fd, 0x10000000ULL);

    if (ring->sq_ptr == MAP_FAILED)
        return -1;

    ring->sq_head = ring->sq_ptr + ring->params.sq_off.head;
    ring->sq_tail = ring->sq_ptr + ring->params.sq_off.tail;
    ring->sq_mask = ring->sq_ptr + ring->params.sq_off.ring_mask;
    ring->sq_array = ring->sq_ptr + ring->params.sq_off.array;
    ring->cq_head = ring->cq_ptr + ring->params.cq_off.head;
    ring->cq_tail = ring->cq_ptr + ring->params.cq_off.tail;
    ring->cq_mask = ring->cq_ptr + ring->params.cq_off.ring_mask;
    ring->cqes = ring->cq_ptr + ring->params.cq_off.cqes;

    return 0;
}

static struct io_uring_sqe *get_sqe(struct io_uring_ring *ring)
{
    unsigned head = *ring->sq_head;
    unsigned tail = *ring->sq_tail;
    if (tail - head > *ring->sq_mask)
        return NULL;
    struct io_uring_sqe *sqe = &ring->sqes[tail & *ring->sq_mask];
    memset(sqe, 0, sizeof(*sqe));
    sqe->fd = -1;
    return sqe;
}

static void submit_sqe(struct io_uring_ring *ring)
{
    unsigned tail = *ring->sq_tail;
    ring->sq_array[tail & *ring->sq_mask] = tail & *ring->sq_mask;
    __sync_synchronize();
    *ring->sq_tail = tail + 1;
    __sync_synchronize();
    io_uring_enter(ring->ring_fd, 1, 1, 0, NULL);
}

int main()
{
    struct io_uring_ring ring;
    setup_uring(&ring);

    void *buf = mmap(NULL, 0x1000 * 10, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    // 1. Provide 10 Buffers
    printf("[*] Providing 10 buffers...\n");
    struct io_uring_sqe *sqe = get_sqe(&ring);
    sqe->opcode = IORING_OP_PROVIDE_BUFFERS;
    sqe->fd = 10;
    sqe->addr = (uint64_t)buf;
    sqe->len = 0x100;
    sqe->off = 1; // BGID 1
    sqe->buf_index_16 = 0;
    submit_sqe(&ring);

    unsigned head = *ring.cq_head;
    while (head == *ring.cq_tail)
        usleep(10);
    struct io_uring_cqe *cqe = &ring.cqes[head & *ring.cq_mask];
    printf("[*] Provide Res: %d\n", cqe->res);
    *ring.cq_head = head + 1;

    // 2. Read 1 (Should succeed)
    printf("[*] Reading 1 buffer...\n");
    int pipefd[2];
    pipe(pipefd);
    write(pipefd[1], "TEST1111", 8);

    sqe = get_sqe(&ring);
    sqe->opcode = IORING_OP_READ;
    sqe->fd = pipefd[0];
    sqe->len = 8;
    sqe->flags = IOSQE_BUFFER_SELECT;
    sqe->buf_group = 1;
    submit_sqe(&ring);

    head = *ring.cq_head;
    while (head == *ring.cq_tail)
        usleep(10);
    cqe = &ring.cqes[head & *ring.cq_mask];
    printf("[*] Read Res: %d (Flags: %x, BID: %d)\n", cqe->res, cqe->flags, cqe->flags >> 16);
    *ring.cq_head = head + 1;

    // 3. Remove 5 Buffers
    printf("[*] Removing 5 buffers...\n");
    sqe = get_sqe(&ring);
    sqe->opcode = IORING_OP_REMOVE_BUFFERS;
    sqe->fd = 5;
    sqe->off = 1;
    submit_sqe(&ring);

    head = *ring.cq_head;
    while (head == *ring.cq_tail)
        usleep(10);
    cqe = &ring.cqes[head & *ring.cq_mask];
    printf("[*] Remove Res: %d (Should be 5)\n", cqe->res);
    *ring.cq_head = head + 1;

    // End
    return 0;
}
