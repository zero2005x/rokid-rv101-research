#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/syscall.h>

/* minimal BPF insns: return 0 */
static uint64_t insns[] = {
    0x0000000000000000ULL, /* r0 = 0 */
    0x9500000000000000ULL, /* exit */
};

int main(void) {
    char log_buf[4096];
    struct {
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
    } attr;
    memset(&attr, 0, sizeof(attr));
    attr.prog_type = 1; /* BPF_PROG_TYPE_SOCKET_FILTER */
    attr.insn_cnt = 2;
    attr.insns = (uint64_t)(uintptr_t)insns;
    attr.license = (uint64_t)(uintptr_t)"GPL";
    attr.log_level = 1;
    attr.log_size = sizeof(log_buf);
    attr.log_buf = (uint64_t)(uintptr_t)log_buf;
    memset(log_buf, 0, sizeof(log_buf));
    errno = 0;
    long fd = syscall(__NR_bpf, 5 /* BPF_PROG_LOAD */, &attr, sizeof(attr));
    printf("BPF_PROG_LOAD(SOCKET_FILTER) -> fd=%ld errno=%d(%s)\n", fd, errno, strerror(errno));
    if (fd > 0) close(fd);
    if (log_buf[0]) printf("LOG: %s\n", log_buf);
    return 0;
}