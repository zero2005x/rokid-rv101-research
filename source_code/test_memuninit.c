#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/bpf.h>

#ifndef BPF_F_RDONLY_PROG
#define BPF_F_RDONLY_PROG (1U << 7)
#endif

static int create_map(uint64_t flags, uint32_t vs) {
    union bpf_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.map_type = BPF_MAP_TYPE_ARRAY;
    attr.key_size = 4;
    attr.value_size = vs;
    attr.max_entries = 1;
    attr.map_flags = flags;
    errno = 0;
    int fd = syscall(__NR_bpf, BPF_MAP_CREATE, &attr, sizeof(attr));
    printf("MAP_CREATE flags=0x%llx vs=%u -> fd=%d errno=%d(%s)\n",
           (unsigned long long)flags, vs, fd, errno, strerror(errno));
    return fd;
}

static long map_lookup(int mfd, uint64_t *out) {
    uint32_t k = 0;
    union bpf_attr attr; memset(&attr, 0, sizeof(attr));
    attr.map_fd = mfd; attr.key = (uint64_t)(uintptr_t)&k;
    attr.value = (uint64_t)(uintptr_t)out;
    errno = 0;
    long r = syscall(__NR_bpf, BPF_MAP_LOOKUP_ELEM, &attr, sizeof(attr));
    printf("LOOKUP -> %ld errno=%d(%s) value=0x%016llx\n", r, errno, strerror(errno), (unsigned long long)*out);
    return r;
}

int main(void) {
    int m1 = create_map(BPF_F_RDONLY_PROG, 8);
    int m2 = create_map(0, 8);
    if (m1 < 0 || m2 < 0) return 1;

    uint32_t k = 0; uint64_t v1 = 0x1111111111111111ULL; uint32_t kv = 8;
    union bpf_attr upd; memset(&upd, 0, sizeof(upd));
    upd.map_fd = m1; upd.key = (uint64_t)(uintptr_t)&k; upd.value = (uint64_t)(uintptr_t)&v1;
    long ur = syscall(__NR_bpf, BPF_MAP_UPDATE_ELEM, &upd, sizeof(upd));
    printf("UPDATE_ELEM(m1 init=0x1111...) -> %ld errno=%d(%s)\n", ur, errno, strerror(errno));
    upd.map_fd = m2; upd.value = (uint64_t)(uintptr_t)&kv;
    ur = syscall(__NR_bpf, BPF_MAP_UPDATE_ELEM, &upd, sizeof(upd));
    printf("UPDATE_ELEM(m2 len=8) -> %ld errno=%d(%s)\n", ur, errno, strerror(errno));

    static struct bpf_insn insns[] = {
        { .code = 0xbf, .dst_reg = 7, .src_reg = 1, .off = 0, .imm = 0 }, /* 0: r7=r1 (ctx) */
        { .code = 0xb7, .dst_reg = 0, .off = 0, .imm = 0 },            /* 1: r0=0 */
        { .code = 0x63, .dst_reg = 10, .src_reg = 0, .off = -4, .imm = 0 }, /* 2: *(u32*)(fp-4)=0 */
        { .code = 0x18, .dst_reg = 1, .src_reg = 1, .off = 0, .imm = 0 }, /* 3: r1=m1 */
        { .code = 0x00, .dst_reg = 0, .off = 0, .imm = 0 },            /* 4: aux */
        { .code = 0xbf, .dst_reg = 2, .src_reg = 10, .off = 0, .imm = 0 }, /* 5: r2=fp */
        { .code = 0x07, .dst_reg = 2, .off = 0, .imm = -4 },           /* 6: r2+=-4 */
        { .code = 0x85, .dst_reg = 0, .off = 0, .imm = 1 },            /* 7: call map_lookup */
        { .code = 0x15, .dst_reg = 0, .src_reg = 0, .off = 18, .imm = 0 }, /* 8: if r0==0 goto +18 (to 27) */
        { .code = 0xbf, .dst_reg = 6, .src_reg = 0, .off = 0, .imm = 0 }, /* 9: r6=r0 (m1 value) */
        { .code = 0xb7, .dst_reg = 0, .off = 0, .imm = 0 },            /* 10: r0=0 */
        { .code = 0x63, .dst_reg = 10, .src_reg = 0, .off = -4, .imm = 0 }, /* 11: *(u32*)(fp-4)=0 */
        { .code = 0x18, .dst_reg = 1, .src_reg = 1, .off = 0, .imm = 0 }, /* 12: r1=m2 */
        { .code = 0x00, .dst_reg = 0, .off = 0, .imm = 0 },            /* 13: aux */
        { .code = 0xbf, .dst_reg = 2, .src_reg = 10, .off = 0, .imm = 0 }, /* 14: r2=fp */
        { .code = 0x07, .dst_reg = 2, .off = 0, .imm = -4 },           /* 15: r2+=-4 */
        { .code = 0x85, .dst_reg = 0, .off = 0, .imm = 1 },            /* 16: call map_lookup */
        { .code = 0x15, .dst_reg = 0, .src_reg = 0, .off = 9, .imm = 0 }, /* 17: if r0==0 goto +9 (to 27) */
        { .code = 0xbf, .dst_reg = 5, .src_reg = 0, .off = 0, .imm = 0 }, /* 18: r5=r0 */
        { .code = 0x61, .dst_reg = 5, .src_reg = 5, .off = 0, .imm = 0 }, /* 19: r5=*(u32*)(r5+0) (len) */
        { .code = 0xa6, .dst_reg = 5, .src_reg = 0, .off = 6, .imm = 1 }, /* 20: if w5<1 goto +6 (to 27) */
        { .code = 0x26, .dst_reg = 5, .src_reg = 0, .off = 5, .imm = 8 }, /* 21: if w5>8 goto +5 (to 27) */
        { .code = 0xb7, .dst_reg = 2, .off = 0, .imm = 0 },            /* 22: r2=0 (offset into skb) */
        { .code = 0xbf, .dst_reg = 3, .src_reg = 6, .off = 0, .imm = 0 }, /* 23: r3=r6 (dest m1) */
        { .code = 0xbf, .dst_reg = 4, .src_reg = 5, .off = 0, .imm = 0 }, /* 24: r4=r5 (len [1,8]) */
        { .code = 0xbf, .dst_reg = 1, .src_reg = 7, .off = 0, .imm = 0 }, /* 25: r1=r7 */
        { .code = 0x85, .dst_reg = 0, .off = 0, .imm = 26 },           /* 26: call load_bytes (write m1) */
        { .code = 0xb7, .dst_reg = 0, .off = 0, .imm = 0 },            /* 27: r0=0 */
        { .code = 0x95, .off = 0, .imm = 0 },                          /* 28: exit */
    };
    insns[3].imm = m1;
    insns[12].imm = m2;

    char log_buf[65536];
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
    long pfd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));
    printf("PROG_LOAD -> fd=%ld errno=%d(%s)\n", pfd, errno, strerror(errno));
    if (pfd < 0) { if (log_buf[0]) printf("LOG: %.4000s\n", log_buf); return 1; }

    int bpf_fd = (int)pfd;
    if (fcntl(pfd, F_GETFD) < 0) { printf("pfd %ld invalid: %s\n", pfd, strerror(errno)); return 1; }

    int recv_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (recv_sock < 0) { perror("socket(AF_INET)"); return 1; }
    struct sockaddr_in sa; memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET; sa.sin_addr.s_addr = htonl(0x7f000001); sa.sin_port = 0;
    if (bind(recv_sock, (struct sockaddr*)&sa, sizeof(sa)) != 0) { perror("bind"); return 1; }
    socklen_t slen = sizeof(sa);
    getsockname(recv_sock, (struct sockaddr*)&sa, &slen);
    printf("udp bound to 127.0.0.1:%u\n", ntohs(sa.sin_port));
    if (setsockopt(recv_sock, SOL_SOCKET, SO_ATTACH_BPF, &bpf_fd, sizeof(bpf_fd)) != 0) {
        perror("SO_ATTACH_BPF"); return 1;
    }
    printf("attached filter to udp socket\n");

    uint64_t out;
    map_lookup(m1, &out); /* before */

    /* send UDP payload whose first 8 bytes we control */
    uint64_t payload = 0x2222222222222222ULL;
    int snd_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (snd_sock < 0) { perror("socket(snd)"); return 1; }
    ssize_t n = sendto(snd_sock, &payload, sizeof(payload), 0, (struct sockaddr*)&sa, sizeof(sa));
    printf("sendto -> %zd errno=%d(%s)\n", n, errno, strerror(errno));

    /* drain; filter returns 0 -> dropped, that's fine, it still executed */
    usleep(200000);
    struct timeval tv = {0, 200000};
    setsockopt(recv_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char buf[64];
    while (recv(recv_sock, buf, sizeof(buf), 0) > 0) {}

    map_lookup(m1, &out); /* after: should be 0x2222222222222222 if write hit */

    close(pfd);
    return 0;
}