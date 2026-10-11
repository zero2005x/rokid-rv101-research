/*
 * Deep BPF Verifier Probe for 5.10.209
 * 探測更細微的 verifier bugs
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <linux/bpf.h>
#include <stdint.h>

#ifndef __NR_bpf
#define __NR_bpf 280
#endif

static inline int bpf(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

// BPF instructions
#define BPF_ALU64_IMM(OP, DST, IMM) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_OP(OP) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})
#define BPF_ALU64_REG(OP, DST, SRC) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_OP(OP) | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = 0, .imm = 0})
#define BPF_ALU32_IMM(OP, DST, IMM) \
    ((struct bpf_insn){.code = BPF_ALU | BPF_OP(OP) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})
#define BPF_ALU32_REG(OP, DST, SRC) \
    ((struct bpf_insn){.code = BPF_ALU | BPF_OP(OP) | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = 0, .imm = 0})
#define BPF_MOV64_IMM(DST, IMM) BPF_ALU64_IMM(BPF_MOV, DST, IMM)
#define BPF_MOV64_REG(DST, SRC) BPF_ALU64_REG(BPF_MOV, DST, SRC)
#define BPF_MOV32_IMM(DST, IMM) BPF_ALU32_IMM(BPF_MOV, DST, IMM)
#define BPF_MOV32_REG(DST, SRC) BPF_ALU32_REG(BPF_MOV, DST, SRC)
#define BPF_LD_MAP_FD(DST, FD)                                                                                                   \
    ((struct bpf_insn){.code = BPF_LD | BPF_DW | BPF_IMM, .dst_reg = DST, .src_reg = BPF_PSEUDO_MAP_FD, .off = 0, .imm = (FD)}), \
        ((struct bpf_insn){.code = 0, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = ((FD) >> 32)})
#define BPF_LDX_MEM(SIZE, DST, SRC, OFF) \
    ((struct bpf_insn){.code = BPF_LDX | BPF_SIZE(SIZE) | BPF_MEM, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = 0})
#define BPF_STX_MEM(SIZE, DST, SRC, OFF) \
    ((struct bpf_insn){.code = BPF_STX | BPF_SIZE(SIZE) | BPF_MEM, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = 0})
#define BPF_ST_MEM(SIZE, DST, OFF, IMM) \
    ((struct bpf_insn){.code = BPF_ST | BPF_SIZE(SIZE) | BPF_MEM, .dst_reg = DST, .src_reg = 0, .off = OFF, .imm = IMM})
#define BPF_JMP_IMM(OP, DST, IMM, OFF) \
    ((struct bpf_insn){.code = BPF_JMP | BPF_OP(OP) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = OFF, .imm = IMM})
#define BPF_JMP_REG(OP, DST, SRC, OFF) \
    ((struct bpf_insn){.code = BPF_JMP | BPF_OP(OP) | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = 0})
#define BPF_JMP32_IMM(OP, DST, IMM, OFF) \
    ((struct bpf_insn){.code = BPF_JMP32 | BPF_OP(OP) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = OFF, .imm = IMM})
#define BPF_EXIT_INSN() \
    ((struct bpf_insn){.code = BPF_JMP | BPF_EXIT, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0})
#define BPF_CALL_INSN(FUNC) \
    ((struct bpf_insn){.code = BPF_JMP | BPF_CALL, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = FUNC})
#define BPF_EMIT_CALL(FUNC) BPF_CALL_INSN(FUNC)

#define BPF_FUNC_map_lookup_elem 1
#define BPF_FUNC_map_update_elem 2
#define BPF_FUNC_ringbuf_reserve 131
#define BPF_FUNC_ringbuf_submit 132
#define BPF_FUNC_ringbuf_discard 133
#define BPF_FUNC_ringbuf_output 130
#define BPF_FUNC_get_current_pid_tgid 14
#define BPF_FUNC_ktime_get_ns 5
#define BPF_PSEUDO_MAP_FD 1

char verifier_log[65536];

int create_array_map(int key_size, int value_size, int max_entries)
{
    union bpf_attr attr = {
        .map_type = BPF_MAP_TYPE_ARRAY,
        .key_size = key_size,
        .value_size = value_size,
        .max_entries = max_entries,
    };
    return bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
}

int create_ringbuf(int size)
{
    union bpf_attr attr = {
        .map_type = BPF_MAP_TYPE_RINGBUF,
        .max_entries = size,
    };
    return bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
}

int load_prog(struct bpf_insn *insns, int insn_cnt, char *log_buf, int log_size)
{
    union bpf_attr attr = {
        .prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
        .insns = (unsigned long)insns,
        .insn_cnt = insn_cnt,
        .license = (unsigned long)"GPL",
        .log_level = 2,
        .log_buf = (unsigned long)log_buf,
        .log_size = log_size,
    };
    return bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
}

/*
 * Test 1: Pointer arithmetic on stack with tricky bounds
 */
void test_stack_pointer_arith(void)
{
    printf("\n=== Stack Pointer Arithmetic ===\n");

    struct bpf_insn prog[] = {
        // r6 = r10 (fp)
        BPF_MOV64_REG(6, 10),
        // r6 -= 512 (valid stack area)
        BPF_ALU64_IMM(BPF_ADD, 6, -512),
        // r1 = 0
        BPF_MOV64_IMM(1, 0),
        // *(u64*)(r6) = r1
        BPF_STX_MEM(BPF_DW, 6, 1, 0),
        // r0 = 0
        BPF_MOV64_IMM(0, 0),
        BPF_EXIT_INSN(),
    };

    memset(verifier_log, 0, sizeof(verifier_log));
    int fd = load_prog(prog, sizeof(prog) / sizeof(prog[0]), verifier_log, sizeof(verifier_log));

    if (fd >= 0)
    {
        printf("  [!] Loaded successfully (fd=%d)\n", fd);
        close(fd);
    }
    else
    {
        printf("  Rejected: %s\n", verifier_log[0] ? verifier_log : strerror(errno));
    }
}

/*
 * Test 2: Attempt to confuse verifier with overlapping register ranges
 */
void test_reg_range_confusion(void)
{
    printf("\n=== Register Range Confusion ===\n");

    struct bpf_insn prog[] = {
        // r1 = some unknown value (pid)
        BPF_EMIT_CALL(BPF_FUNC_get_current_pid_tgid),
        // r1 = r0 & 0xff
        BPF_MOV64_REG(1, 0),
        BPF_ALU64_IMM(BPF_AND, 1, 0xff),
        // r2 = r1 + 1
        BPF_MOV64_REG(2, 1),
        BPF_ALU64_IMM(BPF_ADD, 2, 1),
        // Now r1 is [0, 255], r2 is [1, 256]
        // Check if they overlap incorrectly
        // if r1 >= r2 goto fail (should never happen)
        BPF_JMP_REG(BPF_JGE, 1, 2, 2),
        BPF_MOV64_IMM(0, 0),
        BPF_EXIT_INSN(),
        // fail path
        BPF_MOV64_IMM(0, 1),
        BPF_EXIT_INSN(),
    };

    memset(verifier_log, 0, sizeof(verifier_log));
    int fd = load_prog(prog, sizeof(prog) / sizeof(prog[0]), verifier_log, sizeof(verifier_log));

    if (fd >= 0)
    {
        printf("  [OK] Basic range tracking works (fd=%d)\n", fd);
        close(fd);
    }
    else
    {
        printf("  Rejected: errno=%d\n", errno);
    }
}

/*
 * Test 3: Check for map_lookup_elem return value confusion
 */
void test_map_lookup_confusion(int map_fd)
{
    printf("\n=== Map Lookup Return Confusion ===\n");

    struct bpf_insn prog[] = {
        // Store key on stack
        BPF_ST_MEM(BPF_W, 10, -4, 0),
        // r2 = fp - 4 (key pointer)
        BPF_MOV64_REG(2, 10),
        BPF_ALU64_IMM(BPF_ADD, 2, -4),
        // r1 = map_fd
        BPF_LD_MAP_FD(1, map_fd),
        // r0 = map_lookup_elem(map, &key)
        BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
        // if r0 == NULL, exit
        BPF_JMP_IMM(BPF_JNE, 0, 0, 2),
        BPF_MOV64_IMM(0, 0),
        BPF_EXIT_INSN(),
        // r0 is value pointer, try to access OOB
        // First do valid access
        BPF_LDX_MEM(BPF_DW, 1, 0, 0),
        // Now try at offset 7 (should be OK for 8 byte value)
        BPF_LDX_MEM(BPF_B, 1, 0, 7),
        BPF_MOV64_IMM(0, 0),
        BPF_EXIT_INSN(),
    };

    memset(verifier_log, 0, sizeof(verifier_log));
    int fd = load_prog(prog, sizeof(prog) / sizeof(prog[0]), verifier_log, sizeof(verifier_log));

    if (fd >= 0)
    {
        printf("  [OK] Valid accesses work (fd=%d)\n", fd);
        close(fd);
    }
    else
    {
        printf("  Rejected: %s\n", verifier_log[0] ? verifier_log : strerror(errno));
    }
}

/*
 * Test 4: Negative offset with 32-bit operations
 */
void test_negative_offset_32bit(int map_fd)
{
    printf("\n=== Negative Offset with 32-bit Ops ===\n");

    struct bpf_insn prog[] = {
        // Store key
        BPF_ST_MEM(BPF_W, 10, -4, 0),
        BPF_MOV64_REG(2, 10),
        BPF_ALU64_IMM(BPF_ADD, 2, -4),
        BPF_LD_MAP_FD(1, map_fd),
        BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JNE, 0, 0, 2),
        BPF_MOV64_IMM(0, 0),
        BPF_EXIT_INSN(),
        // r6 = value ptr
        BPF_MOV64_REG(6, 0),
        // r1 = -1 (32-bit)
        BPF_MOV32_IMM(1, -1),
        // r1 &= 0x7 (should be 7)
        BPF_ALU32_IMM(BPF_AND, 1, 0x7),
        // r6 += r1 (ptr + 7)
        // This is where pointer arith is blocked for non-root
        BPF_ALU64_REG(BPF_ADD, 6, 1),
        BPF_LDX_MEM(BPF_B, 0, 6, 0),
        BPF_EXIT_INSN(),
    };

    memset(verifier_log, 0, sizeof(verifier_log));
    int fd = load_prog(prog, sizeof(prog) / sizeof(prog[0]), verifier_log, sizeof(verifier_log));

    if (fd >= 0)
    {
        printf("  [!!] Pointer arith allowed?! (fd=%d)\n", fd);
        close(fd);
    }
    else
    {
        if (strstr(verifier_log, "pointer arithmetic"))
        {
            printf("  [X] Blocked: pointer arithmetic prohibited\n");
        }
        else
        {
            printf("  Rejected: %s\n", verifier_log[0] ? verifier_log : strerror(errno));
        }
    }
}

/*
 * Test 5: Ringbuf reserve size bounds
 */
void test_ringbuf_size_bounds(int rb_fd)
{
    printf("\n=== Ringbuf Size Bounds ===\n");

    // Test various sizes
    unsigned int sizes[] = {0, 1, 8, 4096, 4097, 0x7fffffff, 0x80000000, 0xffffffff};

    for (int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
        struct bpf_insn prog[] = {
            BPF_LD_MAP_FD(1, rb_fd),
            BPF_MOV64_IMM(2, sizes[i]),
            BPF_MOV64_IMM(3, 0),
            BPF_EMIT_CALL(BPF_FUNC_ringbuf_reserve),
            BPF_JMP_IMM(BPF_JNE, 0, 0, 2),
            BPF_MOV64_IMM(0, 0),
            BPF_EXIT_INSN(),
            // If not NULL, discard and exit
            BPF_MOV64_REG(1, 0),
            BPF_MOV64_IMM(2, 1),
            BPF_EMIT_CALL(BPF_FUNC_ringbuf_discard),
            BPF_MOV64_IMM(0, 1),
            BPF_EXIT_INSN(),
        };

        memset(verifier_log, 0, sizeof(verifier_log));
        int fd = load_prog(prog, sizeof(prog) / sizeof(prog[0]), verifier_log, sizeof(verifier_log));

        printf("  Size 0x%08x: ", sizes[i]);
        if (fd >= 0)
        {
            printf("Loads (fd=%d)\n", fd);
            close(fd);
        }
        else
        {
            printf("Rejected\n");
        }
    }
}

/*
 * Test 6: Check BTF-less verifier behavior
 */
void test_btf_less_behavior(int map_fd)
{
    printf("\n=== BTF-less Verifier Behavior ===\n");

    // Try to create a program that uses register spill/fill
    struct bpf_insn prog[] = {
        // Spill r6 to stack
        BPF_STX_MEM(BPF_DW, 10, 6, -8),
        // Load map lookup
        BPF_ST_MEM(BPF_W, 10, -12, 0),
        BPF_MOV64_REG(2, 10),
        BPF_ALU64_IMM(BPF_ADD, 2, -12),
        BPF_LD_MAP_FD(1, map_fd),
        BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
        // Save result
        BPF_MOV64_REG(6, 0),
        // Fill from stack (should invalidate?)
        BPF_LDX_MEM(BPF_DW, 7, 10, -8),
        // Check r6
        BPF_JMP_IMM(BPF_JNE, 6, 0, 2),
        BPF_MOV64_IMM(0, 0),
        BPF_EXIT_INSN(),
        // r6 is value ptr
        BPF_LDX_MEM(BPF_DW, 0, 6, 0),
        BPF_EXIT_INSN(),
    };

    memset(verifier_log, 0, sizeof(verifier_log));
    int fd = load_prog(prog, sizeof(prog) / sizeof(prog[0]), verifier_log, sizeof(verifier_log));

    if (fd >= 0)
    {
        printf("  [OK] Spill/Fill works (fd=%d)\n", fd);
        close(fd);
    }
    else
    {
        printf("  Rejected: %s\n", verifier_log[0] ? verifier_log : strerror(errno));
    }
}

/*
 * Test 7: Check for state pruning issues
 */
void test_state_pruning(int map_fd)
{
    printf("\n=== State Pruning Behavior ===\n");

    struct bpf_insn prog[] = {
        // r6 = unknown value
        BPF_EMIT_CALL(BPF_FUNC_get_current_pid_tgid),
        BPF_MOV64_REG(6, 0),
        BPF_ALU64_IMM(BPF_AND, 6, 1),
        // if r6 == 0 goto path_a
        BPF_JMP_IMM(BPF_JEQ, 6, 0, 6),
        // path_b: r6 == 1
        BPF_ST_MEM(BPF_W, 10, -4, 0),
        BPF_MOV64_REG(2, 10),
        BPF_ALU64_IMM(BPF_ADD, 2, -4),
        BPF_LD_MAP_FD(1, map_fd),
        BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JA, 0, 0, 6),
        // path_a: r6 == 0
        BPF_ST_MEM(BPF_W, 10, -4, 1),
        BPF_MOV64_REG(2, 10),
        BPF_ALU64_IMM(BPF_ADD, 2, -4),
        BPF_LD_MAP_FD(1, map_fd),
        BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
        // merge point - r0 could be NULL or ptr
        BPF_JMP_IMM(BPF_JNE, 0, 0, 2),
        BPF_MOV64_IMM(0, 0),
        BPF_EXIT_INSN(),
        // access value
        BPF_LDX_MEM(BPF_DW, 0, 0, 0),
        BPF_EXIT_INSN(),
    };

    memset(verifier_log, 0, sizeof(verifier_log));
    int fd = load_prog(prog, sizeof(prog) / sizeof(prog[0]), verifier_log, sizeof(verifier_log));

    if (fd >= 0)
    {
        printf("  [OK] State pruning works correctly (fd=%d)\n", fd);
        close(fd);
    }
    else
    {
        printf("  Rejected: %s\n", verifier_log[0] ? verifier_log : strerror(errno));
    }
}

/*
 * Test 8: Try to leak kernel pointer via return value
 */
void test_ptr_leak_via_return(int map_fd)
{
    printf("\n=== Pointer Leak via Return ===\n");

    struct bpf_insn prog[] = {
        BPF_ST_MEM(BPF_W, 10, -4, 0),
        BPF_MOV64_REG(2, 10),
        BPF_ALU64_IMM(BPF_ADD, 2, -4),
        BPF_LD_MAP_FD(1, map_fd),
        BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
        // Try to return the pointer directly (should be blocked)
        BPF_EXIT_INSN(),
    };

    memset(verifier_log, 0, sizeof(verifier_log));
    int fd = load_prog(prog, sizeof(prog) / sizeof(prog[0]), verifier_log, sizeof(verifier_log));

    if (fd >= 0)
    {
        printf("  [!!] Can return pointer?! (fd=%d)\n", fd);
        close(fd);
    }
    else
    {
        if (strstr(verifier_log, "cannot return stack") ||
            strstr(verifier_log, "leaks") ||
            strstr(verifier_log, "invalid return"))
        {
            printf("  [OK] Pointer leak blocked\n");
        }
        else
        {
            printf("  Rejected: %s\n", verifier_log[0] ? verifier_log : strerror(errno));
        }
    }
}

int main(void)
{
    printf("==========================================\n");
    printf("Deep BPF Verifier Probe - Kernel 5.10.209\n");
    printf("==========================================\n");

    // Create test map
    int map_fd = create_array_map(4, 8, 1);
    if (map_fd < 0)
    {
        printf("[-] Failed to create array map: %s\n", strerror(errno));
        return 1;
    }
    printf("Array map fd=%d\n", map_fd);

    // Create ringbuf
    int rb_fd = create_ringbuf(4096);
    if (rb_fd < 0)
    {
        printf("[-] Failed to create ringbuf: %s\n", strerror(errno));
    }
    else
    {
        printf("Ringbuf fd=%d\n", rb_fd);
    }

    test_stack_pointer_arith();
    test_reg_range_confusion();
    test_map_lookup_confusion(map_fd);
    test_negative_offset_32bit(map_fd);

    if (rb_fd >= 0)
    {
        test_ringbuf_size_bounds(rb_fd);
    }

    test_btf_less_behavior(map_fd);
    test_state_pruning(map_fd);
    test_ptr_leak_via_return(map_fd);

    printf("\n==========================================\n");
    printf("Probe Complete\n");
    printf("==========================================\n");

    if (rb_fd >= 0)
        close(rb_fd);
    close(map_fd);

    return 0;
}
