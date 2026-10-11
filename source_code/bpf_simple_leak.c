/*
 * Simple BPF Info Leak Test
 * Simplified version - tests helpers and return values
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
#define BPF_MOV64_IMM(DST, IMM) BPF_ALU64_IMM(BPF_MOV, DST, IMM)
#define BPF_MOV64_REG(DST, SRC) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_OP(BPF_MOV) | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = 0, .imm = 0})
#define BPF_LDX_MEM(SIZE, DST, SRC, OFF) \
    ((struct bpf_insn){.code = BPF_LDX | BPF_SIZE(SIZE) | BPF_MEM, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = 0})
#define BPF_STX_MEM(SIZE, DST, SRC, OFF) \
    ((struct bpf_insn){.code = BPF_STX | BPF_SIZE(SIZE) | BPF_MEM, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = 0})
#define BPF_ST_MEM(SIZE, DST, OFF, IMM) \
    ((struct bpf_insn){.code = BPF_ST | BPF_SIZE(SIZE) | BPF_MEM, .dst_reg = DST, .src_reg = 0, .off = OFF, .imm = IMM})
#define BPF_EXIT_INSN() \
    ((struct bpf_insn){.code = BPF_JMP | BPF_EXIT, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0})
#define BPF_CALL_INSN(FUNC) \
    ((struct bpf_insn){.code = BPF_JMP | BPF_CALL, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = FUNC})

#define BPF_FUNC_get_current_pid_tgid 14
#define BPF_FUNC_get_current_uid_gid 15
#define BPF_FUNC_get_prandom_u32 7
#define BPF_FUNC_get_smp_processor_id 8

char verifier_log[32768];

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

int main(void)
{
    printf("==========================================\n");
    printf("Simple BPF Info Leak Test\n");
    printf("==========================================\n\n");

    printf("Current PID: %d\n", getpid());
    printf("Current UID: %d\n", getuid());

    // Test 1: Simple return
    printf("\n=== Test 1: Simple Return ===\n");
    {
        struct bpf_insn prog[] = {
            BPF_MOV64_IMM(0, 42),
            BPF_EXIT_INSN(),
        };
        int fd = load_prog(prog, 2);
        printf("Simple prog: fd=%d, errno=%d\n", fd, errno);
        if (fd >= 0)
            close(fd);
    }

    // Test 2: get_current_pid_tgid
    printf("\n=== Test 2: get_current_pid_tgid ===\n");
    {
        struct bpf_insn prog[] = {
            BPF_CALL_INSN(BPF_FUNC_get_current_pid_tgid),
            BPF_EXIT_INSN(),
        };
        int fd = load_prog(prog, 2);
        printf("pid_tgid prog: fd=%d, errno=%d\n", fd, errno);
        if (fd < 0)
        {
            printf("Error log: %.200s\n", verifier_log);
        }
        if (fd >= 0)
            close(fd);
    }

    // Test 3: get_current_uid_gid
    printf("\n=== Test 3: get_current_uid_gid ===\n");
    {
        struct bpf_insn prog[] = {
            BPF_CALL_INSN(BPF_FUNC_get_current_uid_gid),
            BPF_EXIT_INSN(),
        };
        int fd = load_prog(prog, 2);
        printf("uid_gid prog: fd=%d, errno=%d\n", fd, errno);
        if (fd < 0)
        {
            printf("Error log: %.200s\n", verifier_log);
        }
        if (fd >= 0)
            close(fd);
    }

    // Test 4: get_prandom_u32
    printf("\n=== Test 4: get_prandom_u32 ===\n");
    {
        struct bpf_insn prog[] = {
            BPF_CALL_INSN(BPF_FUNC_get_prandom_u32),
            BPF_EXIT_INSN(),
        };
        int fd = load_prog(prog, 2);
        printf("prandom prog: fd=%d, errno=%d\n", fd, errno);
        if (fd >= 0)
            close(fd);
    }

    // Test 5: get_smp_processor_id
    printf("\n=== Test 5: get_smp_processor_id ===\n");
    {
        struct bpf_insn prog[] = {
            BPF_CALL_INSN(BPF_FUNC_get_smp_processor_id),
            BPF_EXIT_INSN(),
        };
        int fd = load_prog(prog, 2);
        printf("cpu_id prog: fd=%d, errno=%d\n", fd, errno);
        if (fd >= 0)
            close(fd);
    }

    // Test 6: SKB context read
    printf("\n=== Test 6: SKB Context Read ===\n");
    {
        // For SOCKET_FILTER, ctx is __sk_buff
        // __sk_buff->len is at offset 0
        struct bpf_insn prog[] = {
            BPF_LDX_MEM(BPF_W, 0, 1, 0), // r0 = ctx->len
            BPF_EXIT_INSN(),
        };
        int fd = load_prog(prog, 2);
        printf("skb len prog: fd=%d, errno=%d\n", fd, errno);
        if (fd >= 0)
            close(fd);
    }

    // Test 7: Try to attach and run
    printf("\n=== Test 7: Attach and Execute ===\n");
    {
        struct bpf_insn prog[] = {
            BPF_CALL_INSN(BPF_FUNC_get_prandom_u32),
            BPF_ALU64_IMM(BPF_AND, 0, 0xFFFF),
            BPF_EXIT_INSN(),
        };

        int prog_fd = load_prog(prog, 3);
        if (prog_fd < 0)
        {
            printf("Failed to load: %s\n", strerror(errno));
        }
        else
        {
            printf("Prog loaded fd=%d\n", prog_fd);

            int sv[2];
            int ret = socketpair(AF_UNIX, SOCK_DGRAM, 0, sv);
            printf("socketpair: ret=%d, errno=%d\n", ret, errno);

            if (ret == 0)
            {
                ret = setsockopt(sv[1], SOL_SOCKET, SO_ATTACH_BPF, &prog_fd, sizeof(prog_fd));
                printf("setsockopt SO_ATTACH_BPF: ret=%d, errno=%d\n", ret, errno);

                if (ret == 0)
                {
                    char buf[64] = "TEST";
                    ssize_t w = write(sv[0], buf, 64);
                    printf("write: %zd\n", w);

                    ssize_t r = recv(sv[1], buf, 64, 0);
                    printf("recv: %zd\n", r);
                    printf("BPF return affected packet length\n");
                }
                close(sv[0]);
                close(sv[1]);
            }
            close(prog_fd);
        }
    }

    printf("\n==========================================\n");
    printf("Done\n");
    printf("==========================================\n");

    return 0;
}
