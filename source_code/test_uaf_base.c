#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <stdint.h>

#define IORING_OP_PROVIDE_BUFFERS 31
#define IORING_OP_REMOVE_BUFFERS 32
#define IORING_OP_READ 22

struct io_uring_sqe {
    uint8_t opcode; uint8_t flags; uint16_t ioprio; int32_t fd;
    union { uint64_t off; uint64_t addr2; };
    union { uint64_t addr; uint64_t splice_off_in; };
    uint32_t len;
    union { uint32_t rw_flags; uint32_t fsync_flags; uint32_t poll_events; uint32_t splice_flags; };
    uint64_t user_data;
    union { uint16_t buf_index; uint16_t buf_group; } __attribute__((packed));
    uint16_t personality;
    union { int32_t splice_fd_in; uint32_t file_index; };
    uint64_t __pad2[2];
};

struct io_uring_cqe { uint64_t user_data; int32_t res; uint32_t flags; };
struct io_uring_params {
    uint32_t sq_entries, cq_entries, flags, sq_thread_cpu, sq_thread_idle, features, wq_fd, resv[3];
    struct { uint32_t head, tail, ring_mask, ring_entries, flags, dropped, array, resv1; uint64_t resv2; } sq_off;
    struct { uint32_t head, tail, ring_mask, ring_entries, overflow, cqes, flags, resv1; uint64_t resv2; } cq_off;
};

int io_uring_setup(unsigned entries, struct io_uring_params *p) { return syscall(425, entries, p); }
int io_uring_enter(int fd, unsigned to_submit, unsigned min_complete, unsigned flags, void *sig) { return syscall(426, fd, to_submit, min_complete, flags, sig); }

int main() {
    struct io_uring_params p = {0};
    int fd = io_uring_setup(32, &p);
    if (fd < 0) { perror("setup"); return 1; }
    
    void *sq_ptr = mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE, fd, 0);
    struct io_uring_sqe *sqes = mmap(0, 32*64, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE, fd, 0x10000000ULL);
    struct io_uring_cqe *cqes = mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE, fd, 0x8000000ULL);
    uint32_t *sq_tail = sq_ptr + p.sq_off.tail;
    uint32_t *cq_head = cqes + (p.cq_off.head / sizeof(struct io_uring_cqe)); // wait, cq_head is usually via cq_ring_ptr + p.cq_off.head
    
    // Test PROVIDE
    sqes[0].opcode = IORING_OP_PROVIDE_BUFFERS;
    sqes[0].fd = 1; // 1 buffer
    sqes[0].addr = 0x100000;
    sqes[0].len = 1024;
    sqes[0].rw_flags = 0;
    sqes[0].buf_group = 1;
    sqes[0].off = 0; // bid
    
    // We need to set sq_array
    uint32_t *sq_array = sq_ptr + p.sq_off.array;
    sq_array[0] = 0;
    *sq_tail = 1;
    
    io_uring_enter(fd, 1, 1, 1, NULL);
    
    // Read CQE
    uint32_t *cq_head_ptr = (void *)cqes + p.cq_off.head;
    uint32_t *cq_tail_ptr = (void *)cqes + p.cq_off.tail;
    struct io_uring_cqe *cqe = (void *)cqes + p.cq_off.cqes;
    printf("PROVIDE res=%d\n", cqe[0].res);
    
    // Test REMOVE
    memset(&sqes[1], 0, sizeof(struct io_uring_sqe));
    sqes[1].opcode = IORING_OP_REMOVE_BUFFERS;
    sqes[1].fd = 1; // nr buffers
    sqes[1].buf_group = 1;
    sqes[1].off = 0;
    
    sq_array[1] = 1;
    *sq_tail = 2;
    io_uring_enter(fd, 1, 1, 1, NULL);
    
    printf("REMOVE res=%d\n", cqe[1].res);
    return 0;
}
