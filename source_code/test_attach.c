#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/bpf.h>

int main(void) {
    static struct bpf_insn insns[] = {
        { .code = 0xb7, .dst_reg = 0, .off = 0, .imm = 0 }, /* r0 = 0 */
        { .code = 0x95, .off = 0, .imm = 0 }, /* exit */
    };
    char log_buf[8192];
    union bpf_attr attr;
    memset(&attr, 0, sizeof(attr));
    memset(log_buf, 0, sizeof(log_buf));
    attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
    attr.insn_cnt = 2;
    attr.insns = (uint64_t)(uintptr_t)insns;
    attr.license = (uint64_t)(uintptr_t)"GPL";
    attr.log_level = 2;
    attr.log_size = sizeof(log_buf);
    attr.log_buf = (uint64_t)(uintptr_t)log_buf;
    errno = 0;
    long pfd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));
    printf("PROG_LOAD simple -> fd=%ld errno=%d(%s)\n", pfd, errno, strerror(errno));
    if (pfd < 0) return 1;

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) != 0) { perror("socketpair"); return 1; }
    int bpf_fd = (int)pfd;
    errno = 0;
    int r = setsockopt(sv[0], SOL_SOCKET, SO_ATTACH_BPF, &bpf_fd, sizeof(bpf_fd));
    printf("SO_ATTACH_BPF(simple prog, unix dgram) -> %d errno=%d(%s)\n", r, errno, strerror(errno));

    /* try UDP inet too */
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s >= 0) {
        errno = 0;
        r = setsockopt(s, SOL_SOCKET, SO_ATTACH_BPF, &bpf_fd, sizeof(bpf_fd));
        printf("SO_ATTACH_BPF(simple prog, inet udp) -> %d errno=%d(%s)\n", r, errno, strerror(errno));
    }
    return 0;
}