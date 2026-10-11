#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <linux/bpf.h>

/* exit: code = 0x95 (BPF_JMP|BPF_EXIT); mov64 r0,0: code=0xb7 */
static struct bpf_insn insns[] = {
    { .code = 0xb7, .dst_reg = 0, .off = 0, .imm = 0 }, /* r0 = 0 */
    { .code = 0x95, .off = 0, .imm = 0 }, /* exit */
};

int main(void) {
    char log_buf[8192];
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
    printf("BPF_PROG_LOAD(SOCKET_FILTER valid) -> fd=%ld errno=%d(%s)\n", fd, errno, strerror(errno));
    if (fd > 0) close(fd);
    if (log_buf[0]) printf("LOG: %.4000s\n", log_buf);
    return 0;
}
