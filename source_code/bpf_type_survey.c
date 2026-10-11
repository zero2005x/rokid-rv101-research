/*
 * BPF Program Type Survey
 * Test the availability of different program types and helper functions
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

#define BPF_MOV64_IMM(DST, IMM) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_OP(BPF_MOV) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})
#define BPF_EXIT_INSN() \
    ((struct bpf_insn){.code = BPF_JMP | BPF_EXIT, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0})
#define BPF_CALL_INSN(FUNC) \
    ((struct bpf_insn){.code = BPF_JMP | BPF_CALL, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = FUNC})

char verifier_log[16384];

int load_prog_type(int prog_type, struct bpf_insn *insns, int insn_cnt)
{
    memset(verifier_log, 0, sizeof(verifier_log));
    union bpf_attr attr = {
        .prog_type = prog_type,
        .insns = (unsigned long)insns,
        .insn_cnt = insn_cnt,
        .license = (unsigned long)"GPL",
        .log_level = 1,
        .log_buf = (unsigned long)verifier_log,
        .log_size = sizeof(verifier_log),
        .expected_attach_type = 0,
    };
    return bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
}

struct prog_type_info
{
    int type;
    const char *name;
};

struct prog_type_info prog_types[] = {
    {1, "SOCKET_FILTER"},
    {2, "KPROBE"},
    {3, "SCHED_CLS"},
    {4, "SCHED_ACT"},
    {5, "TRACEPOINT"},
    {6, "XDP"},
    {7, "PERF_EVENT"},
    {8, "CGROUP_SKB"},
    {9, "CGROUP_SOCK"},
    {10, "LWT_IN"},
    {11, "LWT_OUT"},
    {12, "LWT_XMIT"},
    {13, "SOCK_OPS"},
    {14, "SK_SKB"},
    {15, "CGROUP_DEVICE"},
    {17, "RAW_TRACEPOINT"},
    {20, "CGROUP_SOCK_ADDR"},
    {22, "TRACING"},
    {26, "SK_LOOKUP"},
    {27, "SYSCALL"},
};

void test_prog_types(void)
{
    printf("\n=== Testing Program Types ===\n");

    struct bpf_insn simple_prog[] = {
        BPF_MOV64_IMM(0, 0),
        BPF_EXIT_INSN(),
    };

    for (int i = 0; i < sizeof(prog_types) / sizeof(prog_types[0]); i++)
    {
        int fd = load_prog_type(prog_types[i].type, simple_prog, 2);
        printf("  Type %2d %-20s: ", prog_types[i].type, prog_types[i].name);
        if (fd >= 0)
        {
            printf("[OK] Loadable\n");
            close(fd);
        }
        else if (errno == EPERM)
        {
            printf("[X] Permission denied\n");
        }
        else if (errno == EINVAL)
        {
            if (strstr(verifier_log, "CAP_"))
            {
                printf("[X] Needs capability\n");
            }
            else
            {
                printf("[X] Invalid/Unsupported\n");
            }
        }
        else
        {
            printf("[X] errno=%d\n", errno);
        }
    }
}

#define BPF_FUNC_get_current_pid_tgid 14
#define BPF_FUNC_get_current_uid_gid 15
#define BPF_FUNC_probe_read 4
#define BPF_FUNC_probe_read_kernel 113
#define BPF_FUNC_get_current_task 35
#define BPF_FUNC_get_current_cgroup_id 80
#define BPF_FUNC_get_ns_current_pid_tgid 120
#define BPF_FUNC_get_current_comm 16
#define BPF_FUNC_ktime_get_ns 5
#define BPF_FUNC_ktime_get_boot_ns 125

struct helper_info
{
    int id;
    const char *name;
};

struct helper_info helpers[] = {
    {BPF_FUNC_get_current_pid_tgid, "get_current_pid_tgid"},
    {BPF_FUNC_get_current_uid_gid, "get_current_uid_gid"},
    {BPF_FUNC_probe_read, "probe_read"},
    {BPF_FUNC_probe_read_kernel, "probe_read_kernel"},
    {BPF_FUNC_get_current_task, "get_current_task"},
    {BPF_FUNC_get_current_cgroup_id, "get_current_cgroup_id"},
    {BPF_FUNC_ktime_get_ns, "ktime_get_ns"},
    {BPF_FUNC_ktime_get_boot_ns, "ktime_get_boot_ns"},
};

void test_helpers_for_type(int prog_type, const char *name)
{
    printf("\n=== Helpers for %s (type %d) ===\n", name, prog_type);

    for (int i = 0; i < sizeof(helpers) / sizeof(helpers[0]); i++)
    {
        struct bpf_insn prog[] = {
            BPF_CALL_INSN(helpers[i].id),
            BPF_MOV64_IMM(0, 0),
            BPF_EXIT_INSN(),
        };

        int fd = load_prog_type(prog_type, prog, 3);
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
            else if (errno == EPERM)
            {
                printf("[X] Permission\n");
            }
            else
            {
                printf("[X] errno=%d\n", errno);
            }
        }
    }
}

void test_cgroup_skb(void)
{
    printf("\n=== CGROUP_SKB Detailed Test ===\n");

    // CGROUP_SKB needs attach type
    struct bpf_insn prog[] = {
        BPF_MOV64_IMM(0, 1), // Return 1 (allow)
        BPF_EXIT_INSN(),
    };

    memset(verifier_log, 0, sizeof(verifier_log));
    union bpf_attr attr = {
        .prog_type = BPF_PROG_TYPE_CGROUP_SKB,
        .insns = (unsigned long)prog,
        .insn_cnt = 2,
        .license = (unsigned long)"GPL",
        .log_level = 1,
        .log_buf = (unsigned long)verifier_log,
        .log_size = sizeof(verifier_log),
        .expected_attach_type = 0, // BPF_CGROUP_INET_INGRESS
    };

    int fd = bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
    printf("  CGROUP_SKB ingress: fd=%d, errno=%d\n", fd, errno);
    if (fd < 0)
    {
        printf("  Log: %.200s\n", verifier_log);
    }
    if (fd >= 0)
        close(fd);

    // Try with EGRESS
    attr.expected_attach_type = 1; // BPF_CGROUP_INET_EGRESS
    fd = bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
    printf("  CGROUP_SKB egress: fd=%d, errno=%d\n", fd, errno);
    if (fd >= 0)
        close(fd);
}

void test_tracepoint(void)
{
    printf("\n=== TRACEPOINT Test ===\n");

    struct bpf_insn prog[] = {
        BPF_MOV64_IMM(0, 0),
        BPF_EXIT_INSN(),
    };

    memset(verifier_log, 0, sizeof(verifier_log));
    union bpf_attr attr = {
        .prog_type = BPF_PROG_TYPE_TRACEPOINT,
        .insns = (unsigned long)prog,
        .insn_cnt = 2,
        .license = (unsigned long)"GPL",
        .log_level = 1,
        .log_buf = (unsigned long)verifier_log,
        .log_size = sizeof(verifier_log),
    };

    int fd = bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
    printf("  TRACEPOINT: fd=%d, errno=%d\n", fd, errno);
    if (fd < 0)
    {
        printf("  Log: %.200s\n", verifier_log);
    }
    else
    {
        printf("  [!] TRACEPOINT available - can attach to kernel tracepoints!\n");

        // Test with pid_tgid helper
        struct bpf_insn prog2[] = {
            BPF_CALL_INSN(BPF_FUNC_get_current_pid_tgid),
            BPF_MOV64_IMM(0, 0),
            BPF_EXIT_INSN(),
        };

        attr.insns = (unsigned long)prog2;
        attr.insn_cnt = 3;

        int fd2 = bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
        printf("  TRACEPOINT + pid_tgid: fd=%d\n", fd2);
        if (fd2 >= 0)
        {
            printf("  [!] Can get PID/TGID from tracepoint context!\n");
            close(fd2);
        }
        close(fd);
    }
}

void test_raw_tracepoint(void)
{
    printf("\n=== RAW_TRACEPOINT Test ===\n");

    struct bpf_insn prog[] = {
        BPF_MOV64_IMM(0, 0),
        BPF_EXIT_INSN(),
    };

    memset(verifier_log, 0, sizeof(verifier_log));
    union bpf_attr attr = {
        .prog_type = BPF_PROG_TYPE_RAW_TRACEPOINT,
        .insns = (unsigned long)prog,
        .insn_cnt = 2,
        .license = (unsigned long)"GPL",
        .log_level = 1,
        .log_buf = (unsigned long)verifier_log,
        .log_size = sizeof(verifier_log),
    };

    int fd = bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
    printf("  RAW_TRACEPOINT: fd=%d, errno=%d\n", fd, errno);
    if (fd >= 0)
    {
        printf("  [!] RAW_TRACEPOINT available!\n");
        close(fd);
    }
}

int main(void)
{
    printf("==========================================\n");
    printf("BPF Program Type Survey\n");
    printf("==========================================\n");
    printf("UID: %d, EUID: %d\n", getuid(), geteuid());

    test_prog_types();
    test_cgroup_skb();
    test_tracepoint();
    test_raw_tracepoint();

    // Test helpers for SOCKET_FILTER
    test_helpers_for_type(1, "SOCKET_FILTER");

    // Test helpers for TRACEPOINT if it loads
    struct bpf_insn simple[] = {BPF_MOV64_IMM(0, 0), BPF_EXIT_INSN()};
    if (load_prog_type(5, simple, 2) >= 0)
    {
        test_helpers_for_type(5, "TRACEPOINT");
    }

    printf("\n==========================================\n");
    printf("Survey Complete\n");
    printf("==========================================\n");

    return 0;
}
