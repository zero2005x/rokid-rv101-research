/*
 * CGROUP_SKB BPF Exploit Probe
 * Probe the full capabilities of CGROUP_SKB
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
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

#define BPF_MOV64_IMM(DST, IMM) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_OP(BPF_MOV) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})
#define BPF_ALU64_IMM(OP, DST, IMM) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_OP(OP) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})
#define BPF_MOV64_REG(DST, SRC) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_OP(BPF_MOV) | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = 0, .imm = 0})
#define BPF_LDX_MEM(SIZE, DST, SRC, OFF) \
    ((struct bpf_insn){.code = BPF_LDX | BPF_SIZE(SIZE) | BPF_MEM, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = 0})
#define BPF_STX_MEM(SIZE, DST, SRC, OFF) \
    ((struct bpf_insn){.code = BPF_STX | BPF_SIZE(SIZE) | BPF_MEM, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = 0})
#define BPF_ST_MEM(SIZE, DST, OFF, IMM) \
    ((struct bpf_insn){.code = BPF_ST | BPF_SIZE(SIZE) | BPF_MEM, .dst_reg = DST, .src_reg = 0, .off = OFF, .imm = IMM})
#define BPF_JMP_IMM(OP, DST, IMM, OFF) \
    ((struct bpf_insn){.code = BPF_JMP | BPF_OP(OP) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = OFF, .imm = IMM})
#define BPF_EXIT_INSN() \
    ((struct bpf_insn){.code = BPF_JMP | BPF_EXIT, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0})
#define BPF_CALL_INSN(FUNC) \
    ((struct bpf_insn){.code = BPF_JMP | BPF_CALL, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = FUNC})

// Helper IDs
#define BPF_FUNC_get_current_pid_tgid 14
#define BPF_FUNC_get_current_uid_gid 15
#define BPF_FUNC_get_current_cgroup_id 80
#define BPF_FUNC_ktime_get_ns 5
#define BPF_FUNC_get_socket_cookie 46
#define BPF_FUNC_skb_load_bytes 26
#define BPF_FUNC_get_local_storage 51
#define BPF_FUNC_get_cgroup_classid 17
#define BPF_FUNC_skb_under_cgroup 33

char verifier_log[32768];

int load_cgroup_skb(struct bpf_insn *insns, int insn_cnt, int attach_type)
{
    memset(verifier_log, 0, sizeof(verifier_log));
    union bpf_attr attr = {
        .prog_type = BPF_PROG_TYPE_CGROUP_SKB,
        .insns = (unsigned long)insns,
        .insn_cnt = insn_cnt,
        .license = (unsigned long)"GPL",
        .log_level = 1,
        .log_buf = (unsigned long)verifier_log,
        .log_size = sizeof(verifier_log),
        .expected_attach_type = attach_type,
    };
    return bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
}

void test_cgroup_skb_helpers(void)
{
    printf("\n=== CGROUP_SKB Helper Functions ===\n");

    struct
    {
        int id;
        const char *name;
    } helpers[] = {
        {BPF_FUNC_get_current_pid_tgid, "get_current_pid_tgid"},
        {BPF_FUNC_get_current_uid_gid, "get_current_uid_gid"},
        {BPF_FUNC_get_current_cgroup_id, "get_current_cgroup_id"},
        {BPF_FUNC_ktime_get_ns, "ktime_get_ns"},
        {BPF_FUNC_get_socket_cookie, "get_socket_cookie"},
        {BPF_FUNC_get_cgroup_classid, "get_cgroup_classid"},
        {7, "get_prandom_u32"},
        {8, "get_smp_processor_id"},
    };

    for (int i = 0; i < sizeof(helpers) / sizeof(helpers[0]); i++)
    {
        struct bpf_insn prog[] = {
            BPF_CALL_INSN(helpers[i].id),
            BPF_MOV64_IMM(0, 1), // Allow packet
            BPF_EXIT_INSN(),
        };

        int fd = load_cgroup_skb(prog, 3, 0); // INGRESS
        printf("  %-25s: ", helpers[i].name);
        if (fd >= 0)
        {
            printf("[OK] Available\n");
            close(fd);
        }
        else
        {
            if (strstr(verifier_log, "unknown func"))
            {
                printf("[X] Unknown\n");
            }
            else
            {
                printf("[X] errno=%d\n", errno);
            }
        }
    }
}

void test_cgroup_skb_context(void)
{
    printf("\n=== CGROUP_SKB Context Access ===\n");

    // __sk_buff fields accessible from CGROUP_SKB
    struct
    {
        int offset;
        const char *name;
    } fields[] = {
        {0, "len"},
        {4, "pkt_type"},
        {8, "mark"},
        {12, "queue_mapping"},
        {16, "protocol"},
        {20, "vlan_present"},
        {24, "vlan_tci"},
        {28, "vlan_proto"},
        {32, "priority"},
        {36, "ingress_ifindex"},
        {40, "ifindex"},
        {44, "tc_index"},
        {48, "cb[0]"},
        {68, "hash"},
        {72, "tc_classid"},
        {76, "data"},
        {80, "data_end"},
        {84, "napi_id"},
        {88, "family"},
        {92, "remote_ip4"},
        {96, "local_ip4"},
        {100, "remote_ip6[0]"},
        {116, "local_ip6[0]"},
        {132, "remote_port"},
        {136, "local_port"},
    };

    for (int i = 0; i < sizeof(fields) / sizeof(fields[0]); i++)
    {
        struct bpf_insn prog[] = {
            // r1 = ctx (__sk_buff)
            BPF_LDX_MEM(BPF_W, 0, 1, fields[i].offset),
            BPF_MOV64_IMM(0, 1),
            BPF_EXIT_INSN(),
        };

        int fd = load_cgroup_skb(prog, 3, 0);
        printf("  offset %3d %-18s: ", fields[i].offset, fields[i].name);
        if (fd >= 0)
        {
            printf("[OK] Readable\n");
            close(fd);
        }
        else
        {
            printf("[X] Blocked\n");
        }
    }
}

void test_cgroup_attach(void)
{
    printf("\n=== CGROUP_SKB Attach Test ===\n");

    struct bpf_insn prog[] = {
        BPF_MOV64_IMM(0, 1), // Allow all packets
        BPF_EXIT_INSN(),
    };

    int prog_fd = load_cgroup_skb(prog, 2, 0); // INGRESS
    if (prog_fd < 0)
    {
        printf("  Failed to load prog: %s\n", strerror(errno));
        return;
    }
    printf("  Prog loaded: fd=%d\n", prog_fd);

    // Try to open cgroup fd
    // On Android, cgroups are usually at /sys/fs/cgroup or /dev/cg2_bpf
    const char *cgroup_paths[] = {
        "/dev/cg2_bpf",
        "/sys/fs/cgroup",
        "/sys/fs/cgroup/unified",
        "/acct",
        "/dev/cpuctl",
        "/dev/memcg",
    };

    for (int i = 0; i < sizeof(cgroup_paths) / sizeof(cgroup_paths[0]); i++)
    {
        int cgroup_fd = open(cgroup_paths[i], O_RDONLY | O_DIRECTORY);
        printf("  open(%s): fd=%d, errno=%d\n", cgroup_paths[i], cgroup_fd, cgroup_fd < 0 ? errno : 0);

        if (cgroup_fd >= 0)
        {
            // Try to attach
            union bpf_attr attr = {
                .attach_type = 0, // BPF_CGROUP_INET_INGRESS
                .target_fd = cgroup_fd,
                .attach_bpf_fd = prog_fd,
            };

            int ret = bpf(BPF_PROG_ATTACH, &attr, sizeof(attr));
            printf("    BPF_PROG_ATTACH: ret=%d, errno=%d\n", ret, ret < 0 ? errno : 0);

            if (ret == 0)
            {
                printf("    [!!] ATTACHED! Can filter cgroup traffic!\n");

                // Detach
                bpf(BPF_PROG_DETACH, &attr, sizeof(attr));
            }

            close(cgroup_fd);
        }
    }

    close(prog_fd);
}

void test_socket_cookie_leak(void)
{
    printf("\n=== Socket Cookie Info Leak Test ===\n");

    // CGROUP_SKB can get socket cookie - might leak kernel addresses
    struct bpf_insn prog[] = {
        // r1 = ctx (skb)
        BPF_CALL_INSN(BPF_FUNC_get_socket_cookie),
        // r0 now has cookie - 64-bit value
        // Return low bits to indicate we ran
        BPF_ALU64_IMM(BPF_AND, 0, 0xFF),
        BPF_ALU64_IMM(BPF_ADD, 0, 1),
        BPF_EXIT_INSN(),
    };

    int fd = load_cgroup_skb(prog, 4, 0);
    if (fd >= 0)
    {
        printf("  [OK] get_socket_cookie available in CGROUP_SKB\n");
        printf("  Socket cookies can be used for tracking connections\n");
        close(fd);
    }
    else
    {
        printf("  [X] get_socket_cookie not available: %.100s\n", verifier_log);
    }
}

void test_skb_load_bytes(void)
{
    printf("\n=== SKB Load Bytes Test ===\n");

    // Test if we can read packet data
    struct bpf_insn prog[] = {
        // skb_load_bytes(skb, offset, to, len)
        // r1 = skb (ctx)
        BPF_MOV64_IMM(2, 0),  // offset = 0
        BPF_MOV64_REG(3, 10), // to = stack
        BPF_ALU64_IMM(BPF_ADD, 3, -8),
        BPF_MOV64_IMM(4, 4), // len = 4
        BPF_CALL_INSN(BPF_FUNC_skb_load_bytes),
        // r0 = 0 on success, negative on error
        BPF_JMP_IMM(BPF_JEQ, 0, 0, 2),
        BPF_MOV64_IMM(0, 0), // Drop if error
        BPF_EXIT_INSN(),
        // Read what we got from stack
        BPF_LDX_MEM(BPF_W, 0, 10, -8),
        BPF_MOV64_IMM(0, 1), // Allow packet
        BPF_EXIT_INSN(),
    };

    int fd = load_cgroup_skb(prog, 11, 0);
    if (fd >= 0)
    {
        printf("  [OK] skb_load_bytes available - can read packet data!\n");
        close(fd);
    }
    else
    {
        printf("  [X] skb_load_bytes error: %.100s\n", verifier_log);
    }
}

int main(void)
{
    printf("==========================================\n");
    printf("CGROUP_SKB BPF Exploit Probe\n");
    printf("==========================================\n");
    printf("UID: %d\n", getuid());

    test_cgroup_skb_helpers();
    test_cgroup_skb_context();
    test_socket_cookie_leak();
    test_skb_load_bytes();
    test_cgroup_attach();

    printf("\n==========================================\n");
    printf("Probe Complete\n");
    printf("==========================================\n");

    return 0;
}
