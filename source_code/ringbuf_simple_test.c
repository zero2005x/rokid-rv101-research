/*
 * Simplified BPF Ringbuf Test
 * Focuses on validating the reserve/submit flow
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <linux/bpf.h>

/* BPF instruction macros */
#define BPF_OP(op) ((op) & 0xff)
#define BPF_ALU64_IMM(op, dst, imm) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_OP(op) | BPF_K, .dst_reg = dst, .src_reg = 0, .off = 0, .imm = imm})
#define BPF_MOV64_REG(dst, src) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_MOV | BPF_X, .dst_reg = dst, .src_reg = src, .off = 0, .imm = 0})
#define BPF_MOV64_IMM(dst, imm) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_MOV | BPF_K, .dst_reg = dst, .src_reg = 0, .off = 0, .imm = imm})
#define BPF_LD_MAP_FD(dst, fd)                                                                                   \
    ((struct bpf_insn){.code = BPF_LD | BPF_DW | BPF_IMM, .dst_reg = dst, .src_reg = 1, .off = 0, .imm = (fd)}), \
        ((struct bpf_insn){.code = 0, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0})
#define BPF_JMP_IMM(op, dst, imm, off) \
    ((struct bpf_insn){.code = BPF_JMP | BPF_OP(op) | BPF_K, .dst_reg = dst, .src_reg = 0, .off = off, .imm = imm})
#define BPF_STX_MEM(size, dst, src, off) \
    ((struct bpf_insn){.code = BPF_STX | BPF_OP(size) | BPF_MEM, .dst_reg = dst, .src_reg = src, .off = off, .imm = 0})
#define BPF_LDX_MEM(size, dst, src, off) \
    ((struct bpf_insn){.code = BPF_LDX | BPF_OP(size) | BPF_MEM, .dst_reg = dst, .src_reg = src, .off = off, .imm = 0})
#define BPF_RAW_INSN(code, dst, src, off, imm) \
    ((struct bpf_insn){.code = code, .dst_reg = dst, .src_reg = src, .off = off, .imm = imm})
#define BPF_EXIT_INSN() \
    ((struct bpf_insn){.code = BPF_JMP | BPF_EXIT, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0})

/* Helper IDs */
#define BPF_FUNC_ringbuf_reserve 131
#define BPF_FUNC_ringbuf_submit 132
#define BPF_FUNC_ringbuf_discard 133

static char log_buf[65536];

int bpf(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

int create_ringbuf(int size)
{
    union bpf_attr attr = {
        .map_type = BPF_MAP_TYPE_RINGBUF,
        .max_entries = size,
    };
    return bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
}

int load_prog(struct bpf_insn *insns, int insn_cnt, const char *name)
{
    union bpf_attr attr = {
        .prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
        .insns = (unsigned long)insns,
        .insn_cnt = insn_cnt,
        .license = (unsigned long)"GPL",
        .log_buf = (unsigned long)log_buf,
        .log_size = sizeof(log_buf),
        .log_level = 1,
    };
    int fd = bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
    if (fd < 0)
    {
        printf("[%s] fd=%d err=%d\n", name, fd, errno);
        if (strlen(log_buf) > 0)
        {
            printf("Log: %s\n", log_buf);
        }
    }
    return fd;
}

void trigger_prog(int prog_fd)
{
    int socks[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, socks) < 0)
    {
        perror("socketpair");
        return;
    }

    if (setsockopt(socks[0], SOL_SOCKET, SO_ATTACH_BPF, &prog_fd, sizeof(prog_fd)) < 0)
    {
        perror("setsockopt");
        close(socks[0]);
        close(socks[1]);
        return;
    }

    printf("  BPF attached to socket\n");

    char buf[64] = "TRIGGER";
    ssize_t n = write(socks[1], buf, sizeof(buf));
    printf("  Sent %zd bytes\n", n);

    n = read(socks[0], buf, sizeof(buf));
    printf("  Received %zd bytes\n", n);

    close(socks[0]);
    close(socks[1]);
}

void test_minimal_ringbuf(void)
{
    printf("=== Minimal Ringbuf Test ===\n");

    int rb_fd = create_ringbuf(4096);
    if (rb_fd < 0)
    {
        printf("Failed to create ringbuf: %d\n", errno);
        return;
    }
    printf("Ringbuf map fd=%d\n", rb_fd);

    /* Minimal program: just exit 0 */
    struct bpf_insn prog1[] = {
        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
    };

    int fd1 = load_prog(prog1, 2, "MinExit");
    if (fd1 >= 0)
    {
        printf("Minimal exit program loaded (fd=%d)\n", fd1);
        close(fd1);
    }

    /* Program that uses ringbuf_reserve without using the result */
    /*
     * insn 0-1: r1 = map_fd (LD_MAP_FD is 2 insns)
     * insn 2:   r2 = 8 (size)
     * insn 3:   r3 = 0 (flags)
     * insn 4:   call ringbuf_reserve
     * insn 5:   if r0 == 0, jump +4 -> insn 10
     * insn 6:   r1 = r0 (ptr)
     * insn 7:   r2 = 1 (discard flag)
     * insn 8:   call ringbuf_discard
     * insn 9:   exit
     * insn 10:  exit (null path)
     */
    struct bpf_insn prog2[] = {
        BPF_LD_MAP_FD(BPF_REG_1, rb_fd),                                     /* 0-1 */
        BPF_MOV64_IMM(BPF_REG_2, 8),                                         /* 2 */
        BPF_MOV64_IMM(BPF_REG_3, 0),                                         /* 3 */
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_reserve), /* 4 */

        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 4), /* 5: jump to 10 if null */

        BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),                                 /* 6 */
        BPF_MOV64_IMM(BPF_REG_2, 1),                                         /* 7 */
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_discard), /* 8 */

        BPF_MOV64_IMM(BPF_REG_0, 0), /* 9 */
        BPF_EXIT_INSN(),             /* 10 (null lands at 10, jumps +4 from 5) */
    };

    int fd2 = load_prog(prog2, sizeof(prog2) / sizeof(prog2[0]), "RingbufDiscard");
    if (fd2 >= 0)
    {
        printf("Ringbuf discard program loaded (fd=%d)\n", fd2);
        trigger_prog(fd2);
        close(fd2);
    }

    /* Program that reserves, writes, and submits */
    /*
     * insn 0-1: r1 = map_fd
     * insn 2:   r2 = 8
     * insn 3:   r3 = 0
     * insn 4:   call reserve
     * insn 5:   if r0 == 0, jump +6 -> 12
     * insn 6:   r7 = r0 (save ptr)
     * insn 7:   r8 = 0xDEADBEEF
     * insn 8:   *(u64*)(r7+0) = r8
     * insn 9:   r1 = r7
     * insn 10:  r2 = 0 (submit)
     * insn 11:  call submit
     * insn 12:  r0 = 0
     * insn 13:  exit
     */
    struct bpf_insn prog3[] = {
        BPF_LD_MAP_FD(BPF_REG_1, rb_fd),                                     /* 0-1 */
        BPF_MOV64_IMM(BPF_REG_2, 8),                                         /* 2 */
        BPF_MOV64_IMM(BPF_REG_3, 0),                                         /* 3 */
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_reserve), /* 4 */

        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 7), /* 5: jump to 13 if null */

        BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),          /* 6 */
        BPF_MOV64_IMM(BPF_REG_8, 0x41424344),         /* 7: ABCD */
        BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_8, 0), /* 8 */

        BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),                                /* 9 */
        BPF_MOV64_IMM(BPF_REG_2, 0),                                        /* 10 */
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_submit), /* 11 */

        BPF_MOV64_IMM(BPF_REG_0, 0), /* 12 */
        BPF_EXIT_INSN(),             /* 13 */
    };

    int fd3 = load_prog(prog3, sizeof(prog3) / sizeof(prog3[0]), "RingbufSubmit");
    if (fd3 >= 0)
    {
        printf("Ringbuf submit program loaded (fd=%d)\n", fd3);

        /* mmap consumer page before triggering */
        void *consumer = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, rb_fd, 0);
        if (consumer != MAP_FAILED)
        {
            printf("Consumer page at %p\n", consumer);

            unsigned long *pos = (unsigned long *)consumer;
            printf("  Before trigger: consumer_pos=0x%lx\n", *pos);

            trigger_prog(fd3);

            printf("  After trigger: consumer_pos=0x%lx\n", *pos);

            /* Also map data page to see submitted data */
            void *data = mmap(NULL, 4096, PROT_READ, MAP_SHARED, rb_fd, 4096);
            if (data != MAP_FAILED)
            {
                printf("Data page at %p\n", data);
                unsigned long *d = (unsigned long *)data;
                printf("  Data[0]: 0x%016lx\n", d[0]);
                printf("  Data[1]: 0x%016lx\n", d[1]);
                printf("  Data[2]: 0x%016lx\n", d[2]);
                munmap(data, 4096);
            }

            munmap(consumer, 4096);
        }
        else
        {
            printf("mmap consumer failed: %d\n", errno);
        }

        close(fd3);
    }

    close(rb_fd);
}

int main(void)
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("===========================================\n");
    printf("Simplified Ringbuf Test\n");
    printf("===========================================\n\n");

    test_minimal_ringbuf();

    printf("\n=== Done ===\n");
    return 0;
}
