#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/io_uring.h>

#define IORING_OP_PROVIDE_BUFFERS 31
#define IORING_OP_REMOVE_BUFFERS 32

// Structs are defined in linux/io_uring.h

// Simplified io_uring setup
struct io_uring
{
    int ring_fd;
    struct io_uring_sqe *sq_sqes;
    unsigned *sq_khead;
    unsigned *sq_ktail;
    unsigned *sq_kring_mask;
    unsigned *sq_kentries;
    struct io_uring_cqe *cq_cqes;
    unsigned *cq_khead;
    unsigned *cq_ktail;
    unsigned *cq_kring_mask;
};

int io_uring_setup(unsigned entries, struct io_uring_params *p)
{
    return syscall(__NR_io_uring_setup, entries, p);
}

int io_uring_enter(int fd, unsigned to_submit, unsigned min_complete, unsigned flags, sigset_t *sig)
{
    return syscall(__NR_io_uring_enter, fd, to_submit, min_complete, flags, sig, _NSIG / 8);
}

int setup_ring(struct io_uring *ring)
{
    struct io_uring_params p = {0};
    int fd = io_uring_setup(8, &p);
    if (fd < 0)
        return fd;
    ring->ring_fd = fd;

    int sring_sz = p.sq_off.array + p.sq_entries * sizeof(unsigned);
    int cring_sz = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);
    if (p.features & IORING_FEAT_SINGLE_MMAP)
    {
        if (cring_sz > sring_sz)
            sring_sz = cring_sz;
        cring_sz = sring_sz;
    }

    void *sq_ptr = mmap(0, sring_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING);
    if (sq_ptr == MAP_FAILED)
        return -1;

    void *cq_ptr = mmap(0, cring_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_CQ_RING);
    if (cq_ptr == MAP_FAILED)
        return -1;

    ring->sq_khead = sq_ptr + p.sq_off.head;
    ring->sq_ktail = sq_ptr + p.sq_off.tail;
    ring->sq_kring_mask = sq_ptr + p.sq_off.ring_mask;
    ring->sq_kentries = sq_ptr + p.sq_off.ring_entries;
    ring->sq_sqes = mmap(0, p.sq_entries * sizeof(struct io_uring_sqe), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES);
    if (ring->sq_sqes == MAP_FAILED)
        return -1;

    ring->cq_khead = cq_ptr + p.cq_off.head;
    ring->cq_ktail = cq_ptr + p.cq_off.tail;
    ring->cq_kring_mask = cq_ptr + p.cq_off.ring_mask;
    ring->cq_cqes = cq_ptr + p.cq_off.cqes;

    return 0;
}

void submit_sqe(struct io_uring *ring)
{
    unsigned tail = *ring->sq_ktail;
    unsigned mask = *ring->sq_kring_mask;
    unsigned index = tail & mask;
    // Assume SQE is already populated at index

    // In simpler usage, we just increment tail
    // But we need to update the array index too if not using IORING_SETUP_SQPOLL which we are not
    // Wait, with standard setup we need to put the index into the array
    // The SQ ring has an array of indices at IORING_OFF_SQ_RING + p.sq_off.array
    // My setup code above didn't map the array pointer.
    // Let's rely on standard liburing behavior or fix this.
    // For simplicity, let's fix the pointer:
    // Actually, sq_ptr + p.sq_off.array is the array.
    // I need to store that offset.
}

// Let's restart with a simpler approach - Just use the variables we have.
// I will blindly assume I am putting SQE at index 0 and submitting 1.
// And I need to write '0' into the sq_array[tail & mask].

int main()
{
    struct io_uring ring;
    struct io_uring_params p = {0};
    int fd = io_uring_setup(8, &p);
    if (fd < 0)
    {
        perror("[-] io_uring_setup");
        return 1;
    }
    ring.ring_fd = fd;

    // Map SQ Ring
    void *sq_ptr = mmap(0, p.sq_off.array + p.sq_entries * sizeof(unsigned), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING);
    unsigned *sq_array = sq_ptr + p.sq_off.array;
    ring.sq_khead = sq_ptr + p.sq_off.head;
    ring.sq_ktail = sq_ptr + p.sq_off.tail;
    ring.sq_kring_mask = sq_ptr + p.sq_off.ring_mask;
    ring.sq_sqes = mmap(0, p.sq_entries * sizeof(struct io_uring_sqe), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES);

    // Map CQ Ring
    void *cq_ptr = mmap(0, p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_CQ_RING);
    ring.cq_khead = cq_ptr + p.cq_off.head;
    ring.cq_ktail = cq_ptr + p.cq_off.tail;
    ring.cq_kring_mask = cq_ptr + p.cq_off.ring_mask;
    ring.cq_cqes = cq_ptr + p.cq_off.cqes;

    printf("[*] io_uring setup complete. Version check...\n");
    // Just blindly try operations.

    // 1. PROVIDE_BUFFERS
    char buffer[1024];
    struct io_uring_sqe *sqe = &ring.sq_sqes[0];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_PROVIDE_BUFFERS;
    sqe->addr = (unsigned long)buffer;
    sqe->len = 1024;
    sqe->off = 1;       // BGID = 1
    sqe->buf_group = 1; // Set both? Usually distinct fields.
                        // For PROVIDE_BUFFERS:
                        // addr = start address
                        // len = length of EACH buffer
                        // off = number of buffers
                        // buf_group = group ID
                        // buf_index = starting index (bid)

    // Correction:
    // addr = address
    // len = length
    // off = ID of the buffer group (Wait, usually buf_group is in union)
    // man io_uring_enter says for IORING_OP_PROVIDE_BUFFERS:
    // addr = address
    // len = length
    // off = starting buffer ID (bid)
    // buf_group = group ID
    // fd = number of buffers (nbufs)

    sqe->addr = (uint64_t)buffer;
    sqe->len = 1024;
    sqe->off = 0;       // Starting BID = 0
    sqe->buf_group = 1; // Group ID = 1
    sqe->fd = 1;        // nbufs = 1
    sqe->user_data = 100;

    // Submit
    unsigned tail = *ring.sq_ktail;
    unsigned mask = *ring.sq_kring_mask;
    sq_array[tail & mask] = 0; // Index 0
    *ring.sq_ktail = tail + 1;

    int ret = io_uring_enter(ring.ring_fd, 1, 1, IORING_ENTER_GETEVENTS, NULL);
    if (ret < 0)
    {
        perror("[-] Enter failed");
        return 1;
    }

    unsigned head = *ring.cq_khead;
    struct io_uring_cqe *cqe = &ring.cq_cqes[head & *ring.cq_kring_mask];
    printf("[*] PROVIDE_BUFFERS Result: %d (UserData: %llu)\n", cqe->res, cqe->user_data);
    *ring.cq_khead = head + 1; // Advance CQ

    if (cqe->res < 0)
    {
        printf("[-] Failed to provide buffers: %d\n", cqe->res);
        return 1;
    }

    // 2. REMOVE_BUFFERS
    sqe = &ring.sq_sqes[0]; // Reuse index 0 (safe as we consumed it)
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_REMOVE_BUFFERS;
    sqe->fd = 1;        // nbufs = 1
    sqe->buf_group = 1; // Group ID = 1
    sqe->user_data = 200;

    // Note: Documentation says:
    // buf_group = group ID
    // fd = number of buffers to remove

    tail = *ring.sq_ktail;
    sq_array[tail & mask] = 0;
    *ring.sq_ktail = tail + 1;

    ret = io_uring_enter(ring.ring_fd, 1, 1, IORING_ENTER_GETEVENTS, NULL);

    head = *ring.cq_khead;
    cqe = &ring.cq_cqes[head & *ring.cq_kring_mask];
    printf("[*] REMOVE_BUFFERS Result: %d (UserData: %llu)\n", cqe->res, cqe->user_data);
    *ring.cq_khead = head + 1;

    if (cqe->res == 0)
    {
        printf("[-] Warning: REMOVE_BUFFERS returned 0! (Means 0 buffers removed)\n");
    }
    else if (cqe->res == 1)
    {
        printf("[+] Success: Removed 1 buffer.\n");
    }

    // 3. UAF Check: Try to Read using the removed buffer
    int pipefds[2];
    if (pipe(pipefds) < 0)
    {
        perror("pipe");
        return 1;
    }
    if (write(pipefds[1], "AAAA", 4) != 4)
    {
        perror("write pipe");
        return 1;
    }

    sqe = &ring.sq_sqes[0];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_READ;
    sqe->fd = pipefds[0];
    sqe->len = 4;
    sqe->flags = IOSQE_BUFFER_SELECT; // Select buffer from group
    sqe->buf_group = 1;               // Group ID = 1
    sqe->user_data = 300;

    tail = *ring.sq_ktail;
    sq_array[tail & mask] = 0;
    *ring.sq_ktail = tail + 1;

    ret = io_uring_enter(ring.ring_fd, 1, 1, IORING_ENTER_GETEVENTS, NULL);
    if (ret < 0)
    {
        perror("[-] Enter failed logic");
        return 1;
    }

    head = *ring.cq_khead;
    cqe = &ring.cq_cqes[head & *ring.cq_kring_mask];
    printf("[*] READ (After Remove) Result: %d (UserData: %llu)\n", cqe->res, cqe->user_data);

    // Advance only if we consumed a CQE
    *ring.cq_khead = head + 1;

    if (cqe->res == -ENOBUFS)
    {
        printf("[+] Correct Behavior: Read failed with ENOBUFS (Buffer was truly removed)\n");
    }
    else if (cqe->res > 0)
    {
        printf("[!] VULNERABILITY SUSPECTED: Read succeeded using removed buffer!\n");
    }
    else
    {
        printf("[-] Read failed with unexpected error: %d\n", cqe->res);
    }

    return 0;
}
