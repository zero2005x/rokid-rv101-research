#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <stdint.h>

#define IORING_OP_PROVIDE_BUFFERS 31
#define IORING_OP_REMOVE_BUFFERS 32
#define IORING_ENTER_GETEVENTS (1U << 0)

struct io_uring_sqe {
    uint8_t opcode; uint8_t flags; uint16_t ioprio; int32_t fd;
    union { uint64_t off; uint64_t addr2; };
    union { uint64_t addr; uint64_t splice_off_in; };
    uint32_t len;
    union { uint32_t rw_flags; uint32_t fsync_flags; uint32_t poll_events; uint32_t buf_index; uint32_t buf_group; uint32_t splice_flags; };
    uint64_t user_data;
    union { uint16_t buf_index_16; uint16_t buf_group_16; struct { uint16_t buf_index_inner; uint16_t personality; }; };
    union { int32_t splice_fd_in; uint32_t file_index; };
    uint64_t __pad2[2];
};

struct io_uring_cqe { uint64_t user_data; int32_t res; uint32_t flags; };
struct io_uring_params {
    uint32_t sq_entries, cq_entries, flags, sq_thread_cpu, sq_thread_idle, features, wq_fd, resv[3];
    struct { uint32_t head, tail, ring_mask, ring_entries, flags, dropped, array, resv1; uint64_t resv2; } sq_off;
    struct { uint32_t head, tail, ring_mask, ring_entries, overflow, cqes, flags, resv1; uint64_t resv2; } cq_off;
};

int main() {
    int fd = syscall(425, 32, &(struct io_uring_params){0}); // io_uring_setup
    if (fd < 0) { perror("setup"); return 1; }
    
    // Test REMOVE_BUFFERS ABI
    struct io_uring_sqe sqe = {0};
    sqe.opcode = IORING_OP_REMOVE_BUFFERS;
    sqe.fd = 1;
    sqe.buf_group = 1;
    
    struct io_uring_sqe *sqes = mmap(0, 32*64, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE, fd, 0x10000000ULL);
    memcpy(&sqes[0], &sqe, sizeof(sqe));
    
    uint32_t *sq_tail = mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE, fd, 0);
    sq_tail[1] = 1; // sq_tail ptr
    
    syscall(426, fd, 1, 1, IORING_ENTER_GETEVENTS, 0); // io_uring_enter
    
    struct io_uring_cqe *cqes = mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE, fd, 0x8000000ULL);
    printf("REMOVE_BUFFERS res=%d\n", cqes[0].res);
    return 0;
}
