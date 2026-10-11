#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <stdint.h>
#include <sys/socket.h>

#define 1 27
#define IORING_OP_PROVIDE_BUFFERS 31
#define IOSQE_BUFFER_SELECT (1U << 4)
#define IORING_CQE_F_BUFFER (1U << 0)
#define IORING_CQE_BUFFER_SHIFT 16
#define IORING_ENTER_GETEVENTS (1U << 0)

struct io_uring_sqe {
    uint8_t opcode; uint8_t flags; uint16_t ioprio; int32_t fd;
    union { uint64_t off; uint64_t addr2; };
    union { uint64_t addr; uint64_t splice_off_in; };
    uint32_t len;
    union { uint32_t rw_flags; uint32_t fsync_flags; uint32_t rand_flags; };
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
    int ring_fd = io_uring_setup(32, &p);
    void *sq_ptr = mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE, ring_fd, 0);
    struct io_uring_sqe *sqes = mmap(0, 32*64, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE, ring_fd, 0x10000000ULL);
    void *cq_ptr = mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE, ring_fd, 0x8000000ULL);
    void *ubuf = mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    uint32_t *sq_array = sq_ptr + p.sq_off.array;
    uint32_t *sq_tail = sq_ptr + p.sq_off.tail;
    struct io_uring_cqe *cqes = cq_ptr + p.cq_off.cqes;
    
    // 1. PROVIDE
    sqes[0].opcode = IORING_OP_PROVIDE_BUFFERS;
    sqes[0].fd = 1; sqes[0].addr = (uint64_t)ubuf; sqes[0].len = 1024;
    sqes[0].buf_group = 1337; sqes[0].off = 0; sqes[0].user_data = 1;
    sq_array[0] = 0; *sq_tail = 1;
    io_uring_enter(ring_fd, 1, 1, IORING_ENTER_GETEVENTS, NULL);
    
    // Consume CQE
    *(uint32_t*)(cq_ptr + p.cq_off.head) = *(uint32_t*)(cq_ptr + p.cq_off.tail);

    int socks[2]; socketpair(AF_UNIX, SOCK_STREAM, 0, socks);
    
    // 2. STALL RECV
    sqes[1].opcode = 1;
    sqes[1].fd = socks[0]; sqes[1].addr = 0; sqes[1].len = 1024;
    sqes[1].off = 0;
    sqes[1].flags = IOSQE_BUFFER_SELECT;
    sqes[1].buf_group = 1337; sqes[1].user_data = 2;
    sq_array[1] = 1; *sq_tail = 2;
    io_uring_enter(ring_fd, 1, 0, 0, NULL);
    
    usleep(10000);
    write(socks[1], "HELLO", 5);
    io_uring_enter(ring_fd, 0, 1, IORING_ENTER_GETEVENTS, NULL);
    
    uint32_t head = *(uint32_t*)(cq_ptr + p.cq_off.head);
    uint32_t tail = *(uint32_t*)(cq_ptr + p.cq_off.tail);
    printf("CQE head=%u tail=%u\n", head, tail);
    for(uint32_t j=head; j<tail; j++) {
        int idx = j & *(uint32_t*)(cq_ptr + p.cq_off.ring_mask);
        printf("ID: %d RES: %d FLAGS: %08x\n", (int)cqes[idx].user_data, cqes[idx].res, cqes[idx].flags);
    }
    return 0;
}


