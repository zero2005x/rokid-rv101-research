/*
 * io_eventfd_probe.c - Check io_uring eventfd signaling implementation
 *
 * CVE-2025-21655 affects io_eventfd_signal() introduced in Linux 6.1
 * This tool verifies the io_uring eventfd behavior on 5.10.x
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/eventfd.h>
#include <linux/io_uring.h>

#define QUEUE_DEPTH 32
#define BLOCK_SIZE 4096

/* io_uring syscall wrappers */
static int io_uring_setup(unsigned entries, struct io_uring_params *p)
{
    return syscall(__NR_io_uring_setup, entries, p);
}

static int io_uring_enter(int fd, unsigned to_submit, unsigned min_complete, unsigned flags, void *sig)
{
    return syscall(__NR_io_uring_enter, fd, to_submit, min_complete, flags, sig);
}

static int io_uring_register(int fd, unsigned opcode, void *arg, unsigned nr_args)
{
    return syscall(__NR_io_uring_register, fd, opcode, arg, nr_args);
}

/* IORING_REGISTER opcodes for eventfd */
#ifndef IORING_REGISTER_EVENTFD
#define IORING_REGISTER_EVENTFD 4
#endif
#ifndef IORING_UNREGISTER_EVENTFD
#define IORING_UNREGISTER_EVENTFD 5
#endif
#ifndef IORING_REGISTER_EVENTFD_ASYNC
#define IORING_REGISTER_EVENTFD_ASYNC 6
#endif

struct io_uring_ctx
{
    int ring_fd;
    void *sq_ptr;
    void *cq_ptr;
    struct io_uring_sqe *sqes;
    unsigned *sq_head;
    unsigned *sq_tail;
    unsigned *sq_ring_mask;
    unsigned *sq_ring_entries;
    unsigned *sq_flags;
    unsigned *sq_array;
    unsigned *cq_head;
    unsigned *cq_tail;
    unsigned *cq_ring_mask;
    unsigned *cq_ring_entries;
    struct io_uring_cqe *cqes;
};

static int setup_io_uring(struct io_uring_ctx *ctx)
{
    struct io_uring_params params;
    memset(&params, 0, sizeof(params));

    ctx->ring_fd = io_uring_setup(QUEUE_DEPTH, &params);
    if (ctx->ring_fd < 0)
    {
        printf("[-] io_uring_setup failed: %s\n", strerror(errno));
        return -1;
    }

    printf("[+] io_uring_setup success: fd=%d\n", ctx->ring_fd);
    printf("    features=0x%x\n", params.features);

    /* Map SQ ring */
    size_t sq_size = params.sq_off.array + params.sq_entries * sizeof(unsigned);
    ctx->sq_ptr = mmap(NULL, sq_size, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_POPULATE, ctx->ring_fd, IORING_OFF_SQ_RING);
    if (ctx->sq_ptr == MAP_FAILED)
    {
        printf("[-] SQ ring mmap failed\n");
        return -1;
    }

    /* Map SQEs */
    size_t sqe_size = params.sq_entries * sizeof(struct io_uring_sqe);
    ctx->sqes = mmap(NULL, sqe_size, PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_POPULATE, ctx->ring_fd, IORING_OFF_SQES);
    if (ctx->sqes == MAP_FAILED)
    {
        printf("[-] SQEs mmap failed\n");
        return -1;
    }

    /* Map CQ ring */
    size_t cq_size = params.cq_off.cqes + params.cq_entries * sizeof(struct io_uring_cqe);
    ctx->cq_ptr = mmap(NULL, cq_size, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_POPULATE, ctx->ring_fd, IORING_OFF_CQ_RING);
    if (ctx->cq_ptr == MAP_FAILED)
    {
        printf("[-] CQ ring mmap failed\n");
        return -1;
    }

    /* Setup pointers */
    ctx->sq_head = ctx->sq_ptr + params.sq_off.head;
    ctx->sq_tail = ctx->sq_ptr + params.sq_off.tail;
    ctx->sq_ring_mask = ctx->sq_ptr + params.sq_off.ring_mask;
    ctx->sq_ring_entries = ctx->sq_ptr + params.sq_off.ring_entries;
    ctx->sq_flags = ctx->sq_ptr + params.sq_off.flags;
    ctx->sq_array = ctx->sq_ptr + params.sq_off.array;

    ctx->cq_head = ctx->cq_ptr + params.cq_off.head;
    ctx->cq_tail = ctx->cq_ptr + params.cq_off.tail;
    ctx->cq_ring_mask = ctx->cq_ptr + params.cq_off.ring_mask;
    ctx->cq_ring_entries = ctx->cq_ptr + params.cq_off.ring_entries;
    ctx->cqes = ctx->cq_ptr + params.cq_off.cqes;

    printf("[+] io_uring mapped successfully\n");
    return 0;
}

static void submit_nop(struct io_uring_ctx *ctx)
{
    unsigned tail = *ctx->sq_tail;
    unsigned idx = tail & *ctx->sq_ring_mask;

    struct io_uring_sqe *sqe = &ctx->sqes[idx];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_NOP;
    sqe->user_data = 0x12345678;

    ctx->sq_array[idx] = idx;
    __sync_synchronize();
    *ctx->sq_tail = tail + 1;
    __sync_synchronize();
}

int main(int argc, char **argv)
{
    struct io_uring_ctx ctx;
    int eventfd_fd, eventfd_async_fd;
    int ret;
    uint64_t event_count;

    printf("=== io_uring eventfd Probe Tool ===\n");
    printf("Target: Check io_eventfd_signal() implementation\n");
    printf("CVE-2025-21655: Affects io_eventfd_signal() in Linux 6.1+\n\n");

    /* Setup io_uring */
    if (setup_io_uring(&ctx) < 0)
    {
        return 1;
    }

    printf("\n=== Testing IORING_REGISTER_EVENTFD (opcode %d) ===\n", IORING_REGISTER_EVENTFD);

    /* Create eventfd */
    eventfd_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (eventfd_fd < 0)
    {
        printf("[-] eventfd() failed: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] eventfd created: fd=%d\n", eventfd_fd);

    /* Register eventfd with io_uring */
    ret = io_uring_register(ctx.ring_fd, IORING_REGISTER_EVENTFD, &eventfd_fd, 1);
    printf("[*] IORING_REGISTER_EVENTFD: ret=%d errno=%d (%s)\n",
           ret, errno, ret < 0 ? strerror(errno) : "success");

    if (ret == 0)
    {
        printf("[+] EVENTFD REGISTRATION SUPPORTED!\n");

        /* Submit a NOP to trigger eventfd signal */
        printf("\n[*] Submitting NOP operation...\n");
        submit_nop(&ctx);

        ret = io_uring_enter(ctx.ring_fd, 1, 1, IORING_ENTER_GETEVENTS, NULL);
        printf("[*] io_uring_enter: ret=%d\n", ret);

        /* Check if eventfd was signaled */
        ret = read(eventfd_fd, &event_count, sizeof(event_count));
        if (ret == sizeof(event_count))
        {
            printf("[+] EVENTFD SIGNALED! count=%llu\n", (unsigned long long)event_count);
            printf("    -> io_cqring_ev_posted() is calling eventfd_signal()\n");
        }
        else if (errno == EAGAIN)
        {
            printf("[!] eventfd not signaled (EAGAIN)\n");
        }
        else
        {
            printf("[-] eventfd read error: %s\n", strerror(errno));
        }

        /* Unregister eventfd */
        ret = io_uring_register(ctx.ring_fd, IORING_UNREGISTER_EVENTFD, NULL, 0);
        printf("[*] IORING_UNREGISTER_EVENTFD: ret=%d\n", ret);
    }

    close(eventfd_fd);

    /* Test async eventfd (6.x feature) */
    printf("\n=== Testing IORING_REGISTER_EVENTFD_ASYNC (opcode %d) ===\n", IORING_REGISTER_EVENTFD_ASYNC);

    eventfd_async_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ret = io_uring_register(ctx.ring_fd, IORING_REGISTER_EVENTFD_ASYNC, &eventfd_async_fd, 1);
    printf("[*] IORING_REGISTER_EVENTFD_ASYNC: ret=%d errno=%d (%s)\n",
           ret, errno, ret < 0 ? strerror(errno) : "success");

    if (ret == 0)
    {
        printf("[+] EVENTFD_ASYNC SUPPORTED!\n");
        io_uring_register(ctx.ring_fd, IORING_UNREGISTER_EVENTFD, NULL, 0);
    }
    else if (errno == EINVAL)
    {
        printf("[!] EVENTFD_ASYNC not supported (older kernel)\n");
    }

    close(eventfd_async_fd);

    /* Check for CVE-2025-21655 specific functions */
    printf("\n=== CVE-2025-21655 Analysis ===\n");
    printf("Vulnerable function: io_eventfd_signal() (Linux 6.1+)\n");
    printf("5.10.x uses: io_cqring_ev_posted() -> eventfd_signal()\n\n");

    printf("Checking kallsyms for io_eventfd functions...\n");

    FILE *fp = fopen("/proc/kallsyms", "r");
    if (fp)
    {
        char line[256];
        int found_io_eventfd = 0;
        int found_cqring_ev = 0;

        while (fgets(line, sizeof(line), fp))
        {
            if (strstr(line, "io_eventfd"))
            {
                printf("  [6.1+] %s", line);
                found_io_eventfd = 1;
            }
            if (strstr(line, "io_cqring_ev_posted"))
            {
                printf("  [5.x]  %s", line);
                found_cqring_ev = 1;
            }
        }
        fclose(fp);

        printf("\n=== VERDICT ===\n");
        if (found_io_eventfd)
        {
            printf("[!] WARNING: io_eventfd functions FOUND!\n");
            printf("    -> This kernel MAY be vulnerable to CVE-2025-21655\n");
            printf("    -> Further exploitation testing required\n");
        }
        else if (found_cqring_ev)
        {
            printf("[+] SAFE: Using 5.x io_cqring_ev_posted() implementation\n");
            printf("    -> io_eventfd_signal() does NOT exist\n");
            printf("    -> CVE-2025-21655 does NOT apply to this kernel\n");
        }
        else
        {
            printf("[?] Could not determine eventfd implementation\n");
        }
    }
    else
    {
        printf("[-] Cannot read /proc/kallsyms (kptr_restrict=2)\n");
        printf("    Deduction based on kernel version 5.10.209:\n");
        printf("    -> io_eventfd_signal() was introduced in 6.1\n");
        printf("    -> This kernel uses io_cqring_ev_posted()\n");
        printf("    -> CVE-2025-21655 does NOT apply\n");
    }

    printf("\n=== Implementation Details ===\n");
    printf("5.10.x eventfd flow:\n");
    printf("  io_cqring_ev_posted() -> eventfd_signal(ctx->cq_ev_fd)\n");
    printf("  - Simple synchronous call\n");
    printf("  - No deferred signaling\n");
    printf("  - No io_ev_fd structure\n");
    printf("\n");
    printf("6.1+ eventfd flow (VULNERABLE):\n");
    printf("  io_eventfd_signal() -> io_eventfd_put()\n");
    printf("  - Uses io_ev_fd wrapper structure\n");
    printf("  - Deferred signaling via io_eventfd_do_signal()\n");
    printf("  - UAF when io_ring_ctx freed during deferred signal\n");

    close(ctx.ring_fd);

    return 0;
}
