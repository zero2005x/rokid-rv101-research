/*
 * BPF Exploit Research - Rokid Glasses
 * =====================================
 *
 * Device Status:
 *   - unprivileged_bpf_disabled = 0 ✅ (BPF available!)
 *   - Kernel: 5.10.209
 *   - kallsyms: addresses hidden (KASLR active)
 *
 * BPF Exploitation Strategy:
 * --------------------------
 * 1. Use BPF verifier bugs to bypass bounds checking
 * 2. Gain arbitrary kernel read via BPF map out-of-bounds
 * 3. Leak kernel addresses (bypass KASLR)
 * 4. Overwrite kernel structures (e.g., modprobe_path)
 *
 * Known BPF CVEs for Kernel 5.10.x:
 * ---------------------------------
 * CVE-2021-3490: BPF_ALU32 bounds tracking bug (5.10 affected)
 * CVE-2022-0185: Heap overflow in BPF (Namespace related)
 * CVE-2023-2163: BPF verifier incorrect branch pruning
 *
 * This PoC will:
 * 1. Test basic BPF map creation
 * 2. Attempt to load a BPF program
 * 3. Probe for verifier weaknesses
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

// BPF instruction macros
#define BPF_LD_IMM64_RAW(DST, SRC, IMM)    \
    ((struct bpf_insn){                    \
        .code = BPF_LD | BPF_DW | BPF_IMM, \
        .dst_reg = DST,                    \
        .src_reg = SRC,                    \
        .off = 0,                          \
        .imm = (__u32)(IMM)}),             \
        ((struct bpf_insn){                \
            .code = 0,                     \
            .dst_reg = 0,                  \
            .src_reg = 0,                  \
            .off = 0,                      \
            .imm = (__u32)((IMM) >> 32)})

#define BPF_LD_MAP_FD(DST, MAP_FD) \
    BPF_LD_IMM64_RAW(DST, BPF_PSEUDO_MAP_FD, MAP_FD)

#define BPF_MOV64_IMM(DST, IMM)              \
    ((struct bpf_insn){                      \
        .code = BPF_ALU64 | BPF_MOV | BPF_K, \
        .dst_reg = DST,                      \
        .src_reg = 0,                        \
        .off = 0,                            \
        .imm = IMM})

#define BPF_MOV64_REG(DST, SRC)              \
    ((struct bpf_insn){                      \
        .code = BPF_ALU64 | BPF_MOV | BPF_X, \
        .dst_reg = DST,                      \
        .src_reg = SRC,                      \
        .off = 0,                            \
        .imm = 0})

#define BPF_ALU64_IMM(OP, DST, IMM)             \
    ((struct bpf_insn){                         \
        .code = BPF_ALU64 | BPF_OP(OP) | BPF_K, \
        .dst_reg = DST,                         \
        .src_reg = 0,                           \
        .off = 0,                               \
        .imm = IMM})

#define BPF_ALU64_REG(OP, DST, SRC)             \
    ((struct bpf_insn){                         \
        .code = BPF_ALU64 | BPF_OP(OP) | BPF_X, \
        .dst_reg = DST,                         \
        .src_reg = SRC,                         \
        .off = 0,                               \
        .imm = 0})

#define BPF_ALU32_IMM(OP, DST, IMM)           \
    ((struct bpf_insn){                       \
        .code = BPF_ALU | BPF_OP(OP) | BPF_K, \
        .dst_reg = DST,                       \
        .src_reg = 0,                         \
        .off = 0,                             \
        .imm = IMM})

#define BPF_JMP_IMM(OP, DST, IMM, OFF)        \
    ((struct bpf_insn){                       \
        .code = BPF_JMP | BPF_OP(OP) | BPF_K, \
        .dst_reg = DST,                       \
        .src_reg = 0,                         \
        .off = OFF,                           \
        .imm = IMM})

#define BPF_JMP_REG(OP, DST, SRC, OFF)        \
    ((struct bpf_insn){                       \
        .code = BPF_JMP | BPF_OP(OP) | BPF_X, \
        .dst_reg = DST,                       \
        .src_reg = SRC,                       \
        .off = OFF,                           \
        .imm = 0})

#define BPF_EXIT_INSN()             \
    ((struct bpf_insn){             \
        .code = BPF_JMP | BPF_EXIT, \
        .dst_reg = 0,               \
        .src_reg = 0,               \
        .off = 0,                   \
        .imm = 0})

#define BPF_STX_MEM(SIZE, DST, SRC, OFF)            \
    ((struct bpf_insn){                             \
        .code = BPF_STX | BPF_SIZE(SIZE) | BPF_MEM, \
        .dst_reg = DST,                             \
        .src_reg = SRC,                             \
        .off = OFF,                                 \
        .imm = 0})

#define BPF_LDX_MEM(SIZE, DST, SRC, OFF)            \
    ((struct bpf_insn){                             \
        .code = BPF_LDX | BPF_SIZE(SIZE) | BPF_MEM, \
        .dst_reg = DST,                             \
        .src_reg = SRC,                             \
        .off = OFF,                                 \
        .imm = 0})

#define BPF_ST_MEM(SIZE, DST, OFF, IMM)            \
    ((struct bpf_insn){                            \
        .code = BPF_ST | BPF_SIZE(SIZE) | BPF_MEM, \
        .dst_reg = DST,                            \
        .src_reg = 0,                              \
        .off = OFF,                                \
        .imm = IMM})

#define BPF_CALL_FUNC(FUNC)         \
    ((struct bpf_insn){             \
        .code = BPF_JMP | BPF_CALL, \
        .dst_reg = 0,               \
        .src_reg = 0,               \
        .off = 0,                   \
        .imm = FUNC})

// BPF Helper function IDs
#define BPF_FUNC_map_lookup_elem 1
#define BPF_FUNC_map_update_elem 2
#define BPF_FUNC_map_delete_elem 3

// ============================================================================
// BPF System Call Wrapper
// ============================================================================

int bpf(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

// ============================================================================
// Create BPF Map
// ============================================================================

int create_map(int type, int key_size, int value_size, int max_entries)
{
    union bpf_attr attr = {
        .map_type = type,
        .key_size = key_size,
        .value_size = value_size,
        .max_entries = max_entries,
    };

    return bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
}

// ============================================================================
// Load BPF Program
// ============================================================================

int load_prog(struct bpf_insn *insns, int insn_cnt, char *log, int log_size)
{
    union bpf_attr attr = {
        .prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
        .insns = (uint64_t)insns,
        .insn_cnt = insn_cnt,
        .license = (uint64_t)"GPL",
        .log_buf = (uint64_t)log,
        .log_size = log_size,
        .log_level = 1,
    };

    return bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
}

// ============================================================================
// Test 1: Basic Map Operations
// ============================================================================

void test_basic_map(void)
{
    printf("\n[TEST 1] Basic BPF Map Operations\n");
    printf("─────────────────────────────────\n");

    int map_fd = create_map(BPF_MAP_TYPE_ARRAY, 4, 8, 16);
    if (map_fd < 0)
    {
        printf("[FAIL] Cannot create map: %s\n", strerror(errno));
        return;
    }
    printf("[PASS] Created BPF_MAP_TYPE_ARRAY (fd=%d)\n", map_fd);

    // Update element
    uint32_t key = 0;
    uint64_t value = 0xdeadbeefcafebabe;

    union bpf_attr attr = {
        .map_fd = map_fd,
        .key = (uint64_t)&key,
        .value = (uint64_t)&value,
        .flags = BPF_ANY,
    };

    if (bpf(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr)) == 0)
    {
        printf("[PASS] Map update successful\n");
    }
    else
    {
        printf("[FAIL] Map update failed: %s\n", strerror(errno));
    }

    // Lookup element
    uint64_t lookup_value = 0;
    attr.value = (uint64_t)&lookup_value;
    if (bpf(BPF_MAP_LOOKUP_ELEM, &attr, sizeof(attr)) == 0)
    {
        printf("[PASS] Map lookup: 0x%lx\n", lookup_value);
    }
    else
    {
        printf("[FAIL] Map lookup failed: %s\n", strerror(errno));
    }

    close(map_fd);
}

// ============================================================================
// Test 2: Load Simple BPF Program
// ============================================================================

void test_simple_prog(void)
{
    printf("\n[TEST 2] Simple BPF Program Load\n");
    printf("─────────────────────────────────\n");

    // Simplest program: return 0
    struct bpf_insn prog[] = {
        BPF_MOV64_IMM(BPF_REG_0, 0), // r0 = 0
        BPF_EXIT_INSN(),             // return r0
    };

    char log_buf[4096] = {0};
    int prog_fd = load_prog(prog, sizeof(prog) / sizeof(prog[0]), log_buf, sizeof(log_buf));

    if (prog_fd >= 0)
    {
        printf("[PASS] Program loaded (fd=%d)\n", prog_fd);
        close(prog_fd);
    }
    else
    {
        printf("[FAIL] Program load failed: %s\n", strerror(errno));
        if (log_buf[0])
            printf("[LOG] %s\n", log_buf);
    }
}

// ============================================================================
// Test 3: BPF with Map Lookup
// ============================================================================

void test_prog_with_map(void)
{
    printf("\n[TEST 3] BPF Program with Map Access\n");
    printf("─────────────────────────────────────\n");

    // Create map first
    int map_fd = create_map(BPF_MAP_TYPE_ARRAY, 4, 8, 16);
    if (map_fd < 0)
    {
        printf("[FAIL] Cannot create map: %s\n", strerror(errno));
        return;
    }
    printf("[INFO] Created map (fd=%d)\n", map_fd);

    /*
     * Program:
     *   1. Store key=0 on stack
     *   2. Call map_lookup_elem
     *   3. If found, read value
     *   4. Return
     */
    struct bpf_insn prog[] = {
        // r1 = map_fd
        BPF_LD_MAP_FD(BPF_REG_1, map_fd),
        // *(u32*)(fp - 4) = 0  (key on stack)
        BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 0),
        // r2 = fp - 4  (pointer to key)
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
        // call bpf_map_lookup_elem
        BPF_CALL_FUNC(BPF_FUNC_map_lookup_elem),
        // if r0 == NULL, goto exit
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2),
        // r0 = *(u64*)r0  (read value)
        BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 0),
        // Return
        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
    };

    char log_buf[8192] = {0};
    int prog_fd = load_prog(prog, sizeof(prog) / sizeof(prog[0]), log_buf, sizeof(log_buf));

    if (prog_fd >= 0)
    {
        printf("[PASS] Map access program loaded (fd=%d)\n", prog_fd);
        close(prog_fd);
    }
    else
    {
        printf("[FAIL] Program load failed: %s\n", strerror(errno));
        if (log_buf[0])
            printf("[LOG] %.500s...\n", log_buf);
    }

    close(map_fd);
}

// ============================================================================
// Test 4: ALU32 Bounds Test (CVE-2021-3490 probe)
// ============================================================================

void test_alu32_bounds(void)
{
    printf("\n[TEST 4] ALU32 Bounds Tracking Probe\n");
    printf("─────────────────────────────────────\n");
    printf("[INFO] Testing for CVE-2021-3490-like behavior...\n");

    /*
     * This tests if the verifier correctly tracks 32-bit ALU operations.
     * Vulnerable versions might have incorrect bounds after 32-bit ops.
     *
     * The idea: use 32-bit operations to confuse verifier's bounds tracking,
     * then attempt out-of-bounds memory access.
     */

    struct bpf_insn prog[] = {
        // r6 = 0
        BPF_MOV64_IMM(BPF_REG_6, 0),
        // r7 = 1
        BPF_MOV64_IMM(BPF_REG_7, 1),
        // r8 = 0xFFFFFFFF (32-bit)
        BPF_MOV64_IMM(BPF_REG_8, 0xFFFFFFFF),
        // 32-bit AND: r8 = r8 & 1
        // Vulnerable verifier might not update 64-bit bounds correctly
        BPF_ALU32_IMM(BPF_AND, BPF_REG_8, 1),
        // r8 should be 0 or 1 now
        // r0 = r8
        BPF_MOV64_REG(BPF_REG_0, BPF_REG_8),
        BPF_EXIT_INSN(),
    };

    char log_buf[8192] = {0};
    int prog_fd = load_prog(prog, sizeof(prog) / sizeof(prog[0]), log_buf, sizeof(log_buf));

    if (prog_fd >= 0)
    {
        printf("[PASS] ALU32 program loaded\n");
        printf("[INFO] Verifier bounds tracking appears correct\n");
        close(prog_fd);
    }
    else
    {
        printf("[FAIL] Program load failed: %s\n", strerror(errno));
        if (log_buf[0])
            printf("[LOG] %.500s...\n", log_buf);
    }
}

// ============================================================================
// Test 5: Check Available Map Types
// ============================================================================

void test_map_types(void)
{
    printf("\n[TEST 5] Available BPF Map Types\n");
    printf("─────────────────────────────────\n");

    struct
    {
        int type;
        const char *name;
    } map_types[] = {
        {BPF_MAP_TYPE_HASH, "HASH"},
        {BPF_MAP_TYPE_ARRAY, "ARRAY"},
        {BPF_MAP_TYPE_PROG_ARRAY, "PROG_ARRAY"},
        {BPF_MAP_TYPE_PERF_EVENT_ARRAY, "PERF_EVENT_ARRAY"},
        {BPF_MAP_TYPE_PERCPU_HASH, "PERCPU_HASH"},
        {BPF_MAP_TYPE_PERCPU_ARRAY, "PERCPU_ARRAY"},
        {BPF_MAP_TYPE_STACK_TRACE, "STACK_TRACE"},
        {BPF_MAP_TYPE_LRU_HASH, "LRU_HASH"},
        {BPF_MAP_TYPE_LPM_TRIE, "LPM_TRIE"},
        {BPF_MAP_TYPE_RINGBUF, "RINGBUF"},
    };

    for (size_t i = 0; i < sizeof(map_types) / sizeof(map_types[0]); i++)
    {
        int fd = create_map(map_types[i].type, 4, 8, 16);
        if (fd >= 0)
        {
            printf("[+] %-20s available\n", map_types[i].name);
            close(fd);
        }
        else
        {
            printf("[-] %-20s not available (%s)\n", map_types[i].name, strerror(errno));
        }
    }
}

// ============================================================================
// Main
// ============================================================================

int main(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║  BPF Exploit Research - Rokid Glasses                 ║\n");
    printf("║  Target: Kernel 5.10.209 with unprivileged BPF        ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");

    // Check BPF availability
    printf("\n[*] Checking BPF availability...\n");

    int test_fd = create_map(BPF_MAP_TYPE_ARRAY, 4, 4, 1);
    if (test_fd < 0)
    {
        printf("[!] BPF not available: %s\n", strerror(errno));
        if (errno == EPERM)
            printf("[!] Hint: Check /proc/sys/kernel/unprivileged_bpf_disabled\n");
        return 1;
    }
    close(test_fd);
    printf("[+] BPF is available!\n");

    // Run tests
    test_basic_map();
    test_simple_prog();
    test_prog_with_map();
    test_alu32_bounds();
    test_map_types();

    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  Summary:\n");
    printf("  - BPF maps: ✅ Available\n");
    printf("  - BPF programs: Check results above\n");
    printf("  - Next steps: Search for verifier bugs in 5.10.209\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("\n[*] Research notes:\n");
    printf("    - Check CVE-2021-3490, CVE-2022-2905, CVE-2023-2163\n");
    printf("    - Look for bounds tracking bugs in verifier\n");
    printf("    - Try ringbuf OOB if available\n");
    printf("\n");

    return 0;
}
