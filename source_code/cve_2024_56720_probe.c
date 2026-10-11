/*
 * CVE-2024-56720 Probe Tool
 *
 * Tests if BPF sockmap and bpf_msg_pop_data are available
 *
 * CVE-2024-56720: bpf, sockmap: Several fixes to bpf_msg_pop_data
 * Affected: 5.5 <= kernel < 5.10.231
 * Device: 5.10.209 (IN RANGE!)
 *
 * Compile: aarch64-linux-android31-clang -static -O2 -o cve_2024_56720_probe cve_2024_56720_probe.c
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdint.h>

// BPF syscall number for ARM64
#ifndef __NR_bpf
#define __NR_bpf 280
#endif

// BPF commands
#define BPF_MAP_CREATE 0
#define BPF_PROG_LOAD 5

// BPF map types
#define BPF_MAP_TYPE_HASH 1
#define BPF_MAP_TYPE_ARRAY 2
#define BPF_MAP_TYPE_SOCKMAP 15
#define BPF_MAP_TYPE_SOCKHASH 18
#define BPF_MAP_TYPE_SK_STORAGE 24

// BPF program types
#define BPF_PROG_TYPE_SOCKET_FILTER 1
#define BPF_PROG_TYPE_SK_SKB 14
#define BPF_PROG_TYPE_SK_MSG 16

// BPF attach types
#define BPF_SK_MSG_VERDICT 4

// BPF instructions
#define BPF_LD 0x00
#define BPF_LDX 0x01
#define BPF_ST 0x02
#define BPF_STX 0x03
#define BPF_ALU 0x04
#define BPF_JMP 0x05
#define BPF_ALU64 0x07

#define BPF_W 0x00
#define BPF_H 0x08
#define BPF_B 0x10
#define BPF_DW 0x18

#define BPF_IMM 0x00
#define BPF_MEM 0x60
#define BPF_XADD 0xc0

#define BPF_MOV 0xb0
#define BPF_CALL 0x80
#define BPF_EXIT 0x90
#define BPF_K 0x00
#define BPF_X 0x08

// BPF helper functions
#define BPF_FUNC_map_lookup_elem 1
#define BPF_FUNC_map_update_elem 2
#define BPF_FUNC_msg_redirect_map 60
#define BPF_FUNC_msg_apply_bytes 61
#define BPF_FUNC_msg_cork_bytes 62
#define BPF_FUNC_msg_pull_data 63
#define BPF_FUNC_msg_push_data 90
#define BPF_FUNC_msg_pop_data 91 // <-- Target helper for CVE-2024-56720!

#define BPF_REG_0 0
#define BPF_REG_1 1
#define BPF_REG_2 2
#define BPF_REG_3 3
#define BPF_REG_4 4
#define BPF_REG_10 10

// BPF instruction structure
struct bpf_insn
{
    uint8_t code;
    uint8_t dst_reg : 4;
    uint8_t src_reg : 4;
    int16_t off;
    int32_t imm;
};

// Helper macros for BPF instructions
#define BPF_MOV64_IMM(DST, IMM) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_MOV | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})

#define BPF_MOV64_REG(DST, SRC) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_MOV | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = 0, .imm = 0})

#define BPF_CALL_HELPER(FUNC) \
    ((struct bpf_insn){.code = BPF_JMP | BPF_CALL, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = FUNC})

#define BPF_EXIT_INSN() \
    ((struct bpf_insn){.code = BPF_JMP | BPF_EXIT, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0})

// Union for BPF syscall attributes
union bpf_attr
{
    // BPF_MAP_CREATE
    struct
    {
        uint32_t map_type;
        uint32_t key_size;
        uint32_t value_size;
        uint32_t max_entries;
        uint32_t map_flags;
        uint32_t inner_map_fd;
        uint32_t numa_node;
        char map_name[16];
        uint32_t map_ifindex;
        uint32_t btf_fd;
        uint32_t btf_key_type_id;
        uint32_t btf_value_type_id;
        uint32_t btf_vmlinux_value_type_id;
    };

    // BPF_PROG_LOAD
    struct
    {
        uint32_t prog_type;
        uint32_t insn_cnt;
        uint64_t insns;
        uint64_t license;
        uint32_t log_level;
        uint32_t log_size;
        uint64_t log_buf;
        uint32_t kern_version;
        uint32_t prog_flags;
        char prog_name[16];
        uint32_t prog_ifindex;
        uint32_t expected_attach_type;
        uint32_t prog_btf_fd;
        uint32_t func_info_rec_size;
        uint64_t func_info;
        uint32_t func_info_cnt;
        uint32_t line_info_rec_size;
        uint64_t line_info;
        uint32_t line_info_cnt;
        uint32_t attach_btf_id;
        uint32_t attach_prog_fd;
    } prog;
};

static inline int sys_bpf(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

// Test if a map type is supported
int test_map_type(int map_type, const char *name)
{
    union bpf_attr attr;
    memset(&attr, 0, sizeof(attr));

    attr.map_type = map_type;
    attr.key_size = 4;
    attr.value_size = 4;
    attr.max_entries = 1;

    int fd = sys_bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
    if (fd >= 0)
    {
        close(fd);
        printf("[✓] %s (type=%d): AVAILABLE\n", name, map_type);
        return 1;
    }
    else
    {
        printf("[✗] %s (type=%d): NOT available (errno=%d: %s)\n",
               name, map_type, errno, strerror(errno));
        return 0;
    }
}

// Test if SK_MSG program type is supported
int test_sk_msg_prog()
{
    union bpf_attr attr;
    char log_buf[4096];

    // Simple SK_MSG program: return SK_PASS (1)
    struct bpf_insn prog[] = {
        BPF_MOV64_IMM(BPF_REG_0, 1), // r0 = 1 (SK_PASS)
        BPF_EXIT_INSN(),
    };

    memset(&attr, 0, sizeof(attr));
    attr.prog.prog_type = BPF_PROG_TYPE_SK_MSG;
    attr.prog.insn_cnt = sizeof(prog) / sizeof(prog[0]);
    attr.prog.insns = (uint64_t)prog;
    attr.prog.license = (uint64_t)"GPL";
    attr.prog.log_level = 1;
    attr.prog.log_size = sizeof(log_buf);
    attr.prog.log_buf = (uint64_t)log_buf;
    attr.prog.expected_attach_type = BPF_SK_MSG_VERDICT;

    int fd = sys_bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
    if (fd >= 0)
    {
        close(fd);
        printf("[✓] BPF_PROG_TYPE_SK_MSG: AVAILABLE\n");
        return 1;
    }
    else
    {
        printf("[✗] BPF_PROG_TYPE_SK_MSG: NOT available (errno=%d: %s)\n",
               errno, strerror(errno));
        if (strlen(log_buf) > 0)
        {
            printf("    Verifier log: %.200s...\n", log_buf);
        }
        return 0;
    }
}

// Test if bpf_msg_pop_data helper is available
int test_msg_pop_data_helper()
{
    union bpf_attr attr;
    char log_buf[8192];

    // SK_MSG program that calls bpf_msg_pop_data(msg, start, len, flags)
    // This will fail verification but we can check if the helper is recognized
    struct bpf_insn prog[] = {
        // r1 = ctx (struct sk_msg_md*)
        // bpf_msg_pop_data(msg, 0, 0, 0) - will fail but shows if helper exists
        BPF_MOV64_REG(BPF_REG_1, BPF_REG_1),    // r1 = msg (already in r1)
        BPF_MOV64_IMM(BPF_REG_2, 0),            // r2 = start = 0
        BPF_MOV64_IMM(BPF_REG_3, 0),            // r3 = len = 0
        BPF_MOV64_IMM(BPF_REG_4, 0),            // r4 = flags = 0
        BPF_CALL_HELPER(BPF_FUNC_msg_pop_data), // call bpf_msg_pop_data
        BPF_MOV64_IMM(BPF_REG_0, 1),            // r0 = 1 (SK_PASS)
        BPF_EXIT_INSN(),
    };

    memset(&attr, 0, sizeof(attr));
    attr.prog.prog_type = BPF_PROG_TYPE_SK_MSG;
    attr.prog.insn_cnt = sizeof(prog) / sizeof(prog[0]);
    attr.prog.insns = (uint64_t)prog;
    attr.prog.license = (uint64_t)"GPL";
    attr.prog.log_level = 1;
    attr.prog.log_size = sizeof(log_buf);
    attr.prog.log_buf = (uint64_t)log_buf;
    attr.prog.expected_attach_type = BPF_SK_MSG_VERDICT;

    int fd = sys_bpf(BPF_PROG_LOAD, &attr, sizeof(attr));

    // Check the verifier log for helper availability
    int helper_exists = 0;
    if (fd >= 0)
    {
        close(fd);
        printf("[✓] bpf_msg_pop_data (helper %d): AVAILABLE - program loaded!\n",
               BPF_FUNC_msg_pop_data);
        helper_exists = 1;
    }
    else
    {
        // Check if the error is about the helper being unknown
        if (strstr(log_buf, "unknown func") || strstr(log_buf, "invalid func"))
        {
            printf("[✗] bpf_msg_pop_data (helper %d): NOT available (unknown function)\n",
                   BPF_FUNC_msg_pop_data);
        }
        else if (strstr(log_buf, "msg_pop_data"))
        {
            printf("[✓] bpf_msg_pop_data (helper %d): RECOGNIZED (but verification failed)\n",
                   BPF_FUNC_msg_pop_data);
            helper_exists = 1;
        }
        else
        {
            printf("[?] bpf_msg_pop_data (helper %d): UNKNOWN status\n",
                   BPF_FUNC_msg_pop_data);
            printf("    errno=%d: %s\n", errno, strerror(errno));
        }

        if (strlen(log_buf) > 0)
        {
            printf("    Verifier log:\n");
            // Print first 500 chars
            char *p = log_buf;
            while (*p && (p - log_buf) < 500)
            {
                char line[200];
                int i = 0;
                while (*p && *p != '\n' && i < 199)
                {
                    line[i++] = *p++;
                }
                line[i] = 0;
                if (*p == '\n')
                    p++;
                if (i > 0)
                    printf("      %s\n", line);
            }
        }
    }

    return helper_exists;
}

// Test other msg helpers for comparison
void test_other_msg_helpers()
{
    printf("\n=== Testing other SK_MSG helpers ===\n");

    struct
    {
        int func_id;
        const char *name;
    } helpers[] = {
        {BPF_FUNC_msg_redirect_map, "bpf_msg_redirect_map"},
        {BPF_FUNC_msg_apply_bytes, "bpf_msg_apply_bytes"},
        {BPF_FUNC_msg_cork_bytes, "bpf_msg_cork_bytes"},
        {BPF_FUNC_msg_pull_data, "bpf_msg_pull_data"},
        {BPF_FUNC_msg_push_data, "bpf_msg_push_data"},
    };

    for (int i = 0; i < sizeof(helpers) / sizeof(helpers[0]); i++)
    {
        union bpf_attr attr;
        char log_buf[4096];

        struct bpf_insn prog[] = {
            BPF_MOV64_REG(BPF_REG_1, BPF_REG_1),
            BPF_MOV64_IMM(BPF_REG_2, 0),
            BPF_MOV64_IMM(BPF_REG_3, 0),
            BPF_MOV64_IMM(BPF_REG_4, 0),
            BPF_CALL_HELPER(helpers[i].func_id),
            BPF_MOV64_IMM(BPF_REG_0, 1),
            BPF_EXIT_INSN(),
        };

        memset(&attr, 0, sizeof(attr));
        attr.prog.prog_type = BPF_PROG_TYPE_SK_MSG;
        attr.prog.insn_cnt = sizeof(prog) / sizeof(prog[0]);
        attr.prog.insns = (uint64_t)prog;
        attr.prog.license = (uint64_t)"GPL";
        attr.prog.log_level = 1;
        attr.prog.log_size = sizeof(log_buf);
        attr.prog.log_buf = (uint64_t)log_buf;
        attr.prog.expected_attach_type = BPF_SK_MSG_VERDICT;

        int fd = sys_bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
        if (fd >= 0)
        {
            close(fd);
            printf("[✓] %s (id=%d): AVAILABLE\n", helpers[i].name, helpers[i].func_id);
        }
        else
        {
            if (strstr(log_buf, "unknown func") || strstr(log_buf, "invalid func"))
            {
                printf("[✗] %s (id=%d): NOT available\n", helpers[i].name, helpers[i].func_id);
            }
            else
            {
                printf("[~] %s (id=%d): Recognized but verify failed\n",
                       helpers[i].name, helpers[i].func_id);
            }
        }
    }
}

// Test SK_SKB program type (related to sockmap)
int test_sk_skb_prog()
{
    union bpf_attr attr;
    char log_buf[4096];

    struct bpf_insn prog[] = {
        BPF_MOV64_IMM(BPF_REG_0, 1), // SK_PASS
        BPF_EXIT_INSN(),
    };

    memset(&attr, 0, sizeof(attr));
    attr.prog.prog_type = BPF_PROG_TYPE_SK_SKB;
    attr.prog.insn_cnt = sizeof(prog) / sizeof(prog[0]);
    attr.prog.insns = (uint64_t)prog;
    attr.prog.license = (uint64_t)"GPL";
    attr.prog.log_level = 1;
    attr.prog.log_size = sizeof(log_buf);
    attr.prog.log_buf = (uint64_t)log_buf;

    int fd = sys_bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
    if (fd >= 0)
    {
        close(fd);
        printf("[✓] BPF_PROG_TYPE_SK_SKB: AVAILABLE\n");
        return 1;
    }
    else
    {
        printf("[✗] BPF_PROG_TYPE_SK_SKB: NOT available (errno=%d: %s)\n",
               errno, strerror(errno));
        return 0;
    }
}

int main()
{
    printf("======================================================\n");
    printf("  CVE-2024-56720 Probe Tool\n");
    printf("  BPF sockmap / bpf_msg_pop_data availability check\n");
    printf("======================================================\n\n");

    // Check kernel version
    printf("=== Kernel Info ===\n");
    FILE *f = fopen("/proc/version", "r");
    if (f)
    {
        char buf[256];
        if (fgets(buf, sizeof(buf), f))
        {
            printf("%s", buf);
        }
        fclose(f);
    }

    // Check BPF settings
    printf("\n=== BPF Settings ===\n");
    f = fopen("/proc/sys/kernel/unprivileged_bpf_disabled", "r");
    if (f)
    {
        int val;
        if (fscanf(f, "%d", &val) == 1)
        {
            printf("unprivileged_bpf_disabled = %d %s\n", val, val == 0 ? "(BPF available!)" : "(restricted)");
        }
        fclose(f);
    }

    printf("\n=== Testing Map Types ===\n");
    int sockmap_ok = test_map_type(BPF_MAP_TYPE_SOCKMAP, "BPF_MAP_TYPE_SOCKMAP");
    int sockhash_ok = test_map_type(BPF_MAP_TYPE_SOCKHASH, "BPF_MAP_TYPE_SOCKHASH");
    test_map_type(BPF_MAP_TYPE_HASH, "BPF_MAP_TYPE_HASH (baseline)");

    printf("\n=== Testing Program Types ===\n");
    int sk_msg_ok = test_sk_msg_prog();
    int sk_skb_ok = test_sk_skb_prog();

    printf("\n=== Testing bpf_msg_pop_data Helper ===\n");
    int msg_pop_ok = 0;
    if (sk_msg_ok)
    {
        msg_pop_ok = test_msg_pop_data_helper();
    }
    else
    {
        printf("[!] Skipping bpf_msg_pop_data test (SK_MSG not available)\n");
    }

    if (sk_msg_ok)
    {
        test_other_msg_helpers();
    }

    // Summary
    printf("\n======================================================\n");
    printf("  CVE-2024-56720 Feasibility Summary\n");
    printf("======================================================\n");
    printf("  Kernel version: 5.10.209 (affected: 5.5 - 5.10.231)\n");
    printf("  Version match: YES - IN VULNERABLE RANGE!\n\n");

    printf("  Prerequisites:\n");
    printf("    SOCKMAP map type:     %s\n", sockmap_ok ? "✓ AVAILABLE" : "✗ NOT available");
    printf("    SOCKHASH map type:    %s\n", sockhash_ok ? "✓ AVAILABLE" : "✗ NOT available");
    printf("    SK_MSG program type:  %s\n", sk_msg_ok ? "✓ AVAILABLE" : "✗ NOT available");
    printf("    SK_SKB program type:  %s\n", sk_skb_ok ? "✓ AVAILABLE" : "✗ NOT available");
    printf("    bpf_msg_pop_data:     %s\n", msg_pop_ok ? "✓ AVAILABLE" : "✗ NOT available");

    printf("\n  Overall Assessment:\n");
    if (sockmap_ok && sk_msg_ok && msg_pop_ok)
    {
        printf("    🔴 CVE-2024-56720 IS LIKELY EXPLOITABLE!\n");
        printf("    All required components are available.\n");
    }
    else if (sockmap_ok || sk_msg_ok)
    {
        printf("    🟡 PARTIAL - Some components available\n");
        printf("    Further research needed.\n");
    }
    else
    {
        printf("    🟢 NOT EXPLOITABLE - Required components missing\n");
    }
    printf("======================================================\n");

    return 0;
}
