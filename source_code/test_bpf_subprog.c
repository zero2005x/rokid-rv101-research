#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <linux/bpf.h>

/* Test: can unprivileged SOCKET_FILTER load a prog with subprog (BPF_PSEUDO_CALL)?
 * subprog uses ld_abs (legacy packet load) - checks ld_abs reachability too.
 *
 * main:
 *   r1 = ctx (skb)
 *   r0 = 0
 *   call subprog     (BPF_PSEUDO_CALL to insn index 4)
 *   exit
 * subprog @4:
 *   r0 = 1
 *   ld_abs r0, 0     (load packet byte at offset 0 -> r0)
 *   exit
 */
static struct bpf_insn insns[] = {
    { .code = 0xb7, .dst_reg = 0, .off = 0, .imm = 0 },          /* 0: r0 = 0 */
    { .code = 0x85, .dst_reg = 0, .src_reg = 1, .off = 0, .imm = 1 }, /* 1: BPF_PSEUDO_CALL subprog (target 3, imm=3-1-1=1) */
    { .code = 0x95, .off = 0, .imm = 0 },                        /* 2: exit */
    { .code = 0xb7, .dst_reg = 0, .off = 0, .imm = 1 },          /* 3: r0 = 1 */
    { .code = 0x47, .dst_reg = 0, .off = 0, .imm = 0 },          /* 4: ld_abs r0, 0 (BPF_LD|BPF_ABS|BPF_W) */
    { .code = 0x95, .off = 0, .imm = 0 },                        /* 5: exit */
};

int main(void) {
    char log_buf[16384];
    union bpf_attr attr;
    memset(&attr, 0, sizeof(attr));
    memset(log_buf, 0, sizeof(log_buf));
    attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
    attr.insn_cnt = sizeof(insns)/sizeof(insns[0]);
    attr.insns = (uint64_t)(uintptr_t)insns;
    attr.license = (uint64_t)(uintptr_t)"GPL";
    attr.log_level = 2;
    attr.log_size = sizeof(log_buf);
    attr.log_buf = (uint64_t)(uintptr_t)log_buf;
    errno = 0;
    long fd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));
    printf("BPF_PROG_LOAD(SOCKET_FILTER subprog+ld_abs) -> fd=%ld errno=%d(%s)\n", fd, errno, strerror(errno));
    if (fd > 0) close(fd);
    if (log_buf[0]) printf("LOG:\n%s\n", log_buf);
    return 0;
}
