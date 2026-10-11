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

static int create_map(uint32_t type, uint32_t vs, uint32_t max_entries) {
    union bpf_attr attr; memset(&attr, 0, sizeof(attr));
    attr.map_type = type; attr.key_size = 4; attr.value_size = vs;
    attr.max_entries = max_entries;
    errno = 0;
    int fd = syscall(__NR_bpf, BPF_MAP_CREATE, &attr, sizeof(attr));
    printf("MAP_CREATE type=%u vs=%u max=%u -> fd=%d errno=%d(%s)\n",
           type, vs, max_entries, fd, errno, strerror(errno));
    return fd;
}

static long map_lookup(int mfd, uint64_t *out) {
    uint32_t k = 0;
    union bpf_attr attr; memset(&attr, 0, sizeof(attr));
    attr.map_fd = mfd; attr.key = (uint64_t)(uintptr_t)&k;
    attr.value = (uint64_t)(uintptr_t)out;
    errno = 0;
    long r = syscall(__NR_bpf, BPF_MAP_LOOKUP_ELEM, &attr, sizeof(attr));
    printf("LOOKUP(m%d) -> %ld errno=%d(%s) value=0x%016llx\n",
           mfd, r, errno, strerror(errno), (unsigned long long)*out);
    return r;
}

static long load_prog(struct bpf_insn *insns, int cnt, const char *name) {
    char log_buf[65536];
    union bpf_attr attr; memset(&attr, 0, sizeof(attr));
    memset(log_buf, 0, sizeof(log_buf));
    attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
    attr.insn_cnt = cnt;
    attr.insns = (uint64_t)(uintptr_t)insns;
    attr.license = (uint64_t)(uintptr_t)"GPL";
    attr.log_level = 1; attr.log_size = sizeof(log_buf);
    attr.log_buf = (uint64_t)(uintptr_t)log_buf;
    errno = 0;
    long fd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));
    printf("PROG_LOAD %s -> fd=%ld errno=%d(%s)\n", name, fd, errno, strerror(errno));
    if (fd < 0 && log_buf[0]) printf("LOG: %.4000s\n", log_buf);
    return fd;
}

static long prog_array_update(int pa_fd, int prog_fd, uint32_t index) {
    uint32_t k = index;
    union bpf_attr attr; memset(&attr, 0, sizeof(attr));
    attr.map_fd = pa_fd; attr.key = (uint64_t)(uintptr_t)&k;
    attr.value = (uint64_t)(uintptr_t)&prog_fd; attr.flags = BPF_ANY;
    errno = 0;
    long r = syscall(__NR_bpf, BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
    printf("PROG_ARRAY_UPDATE [%u]=fd%d -> %ld errno=%d(%s)\n",
           index, prog_fd, r, errno, strerror(errno));
    return r;
}

static void trigger(int snd, struct sockaddr_in *sa, int recv_sock) {
    uint64_t junk = 0x00;
    sendto(snd, &junk, sizeof(junk), 0, (struct sockaddr*)sa, sizeof(*sa));
    usleep(200000);
    struct timeval tv = {0, 200000};
    setsockopt(recv_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char buf[64];
    while (recv(recv_sock, buf, sizeof(buf), 0) > 0) {}
}

int main(void) {
    int pa = create_map(BPF_MAP_TYPE_PROG_ARRAY, 4, 8);
    int m3 = create_map(BPF_MAP_TYPE_ARRAY, 8, 1);
    if (pa < 0 || m3 < 0) return 1;

    /* ---- P2 (tail-called): writes m3[0] = 0x2222... ---- */
    struct bpf_insn p2[14];
    memset(p2, 0, sizeof(p2));
    p2[0].code = 0xbf; p2[0].dst_reg = 6; p2[0].src_reg = 1;       /* r6 = ctx */
    p2[1].code = 0x62; p2[1].dst_reg = 10; p2[1].off = -4;          /* *(u32*)(fp-4)=0 */
    p2[2].code = 0x18; p2[2].dst_reg = 1; p2[2].src_reg = 1; p2[2].imm = m3; /* r1=m3 */
    p2[3].code = 0x00;
    p2[4].code = 0xbf; p2[4].dst_reg = 2; p2[4].src_reg = 10;       /* r2=fp */
    p2[5].code = 0x07; p2[5].dst_reg = 2; p2[5].imm = -4;           /* r2+=-4 */
    p2[6].code = 0x85; p2[6].imm = 1;                               /* call lookup */
    p2[7].code = 0x15; p2[7].dst_reg = 0; p2[7].off = 5;            /* if r0==0 goto exit */
    p2[8].code = 0xbf; p2[8].dst_reg = 7; p2[8].src_reg = 0;        /* r7=r0 */
    p2[9].code = 0x18; p2[9].dst_reg = 2; p2[9].imm = 0x22222222;    /* r2 = 0x2222222222222222 (lo) */
    p2[10].code = 0x00; p2[10].imm = 0x22222222;                     /* hi */
    p2[11].code = 0x7b; p2[11].dst_reg = 7; p2[11].src_reg = 2;     /* *(u64*)(r7+0)=r2 */
    p2[12].code = 0xb7; p2[12].dst_reg = 0;                         /* r0=0 */
    p2[13].code = 0x95;                                             /* exit */
    long p2fd = load_prog(p2, 14, "P2(callee)");
    if (p2fd < 0) return 1;

    /* ---- P1 (caller): tail_call(ctx, pa, 0); exit 0 ---- */
    struct bpf_insn p1[8];
    memset(p1, 0, sizeof(p1));
    p1[0].code = 0xbf; p1[0].dst_reg = 6; p1[0].src_reg = 1;        /* r6=ctx */
    p1[1].code = 0xbf; p1[1].dst_reg = 1; p1[1].src_reg = 6;        /* r1=ctx */
    p1[2].code = 0x18; p1[2].dst_reg = 2; p1[2].src_reg = 1; p1[2].imm = pa; /* r2=pa */
    p1[3].code = 0x00;
    p1[4].code = 0xb7; p1[4].dst_reg = 3; p1[4].imm = 0;            /* r3=0 (index) */
    p1[5].code = 0x85; p1[5].imm = 12;                              /* call tail_call */
    p1[6].code = 0xb7; p1[6].dst_reg = 0;                           /* r0=0 */
    p1[7].code = 0x95;                                              /* exit */
    long p1fd = load_prog(p1, 8, "P1(caller)");
    if (p1fd < 0) return 1;

    if (prog_array_update(pa, (int)p2fd, 0) != 0) return 1;

    /* attach P1 and trigger */
    int recv_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (recv_sock < 0) { perror("socket"); return 1; }
    struct sockaddr_in sa; memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET; sa.sin_addr.s_addr = htonl(0x7f000001); sa.sin_port = 0;
    bind(recv_sock, (struct sockaddr*)&sa, sizeof(sa));
    socklen_t slen = sizeof(sa);
    getsockname(recv_sock, (struct sockaddr*)&sa, &slen);
    int bpf_fd = (int)p1fd;
    if (setsockopt(recv_sock, SOL_SOCKET, SO_ATTACH_BPF, &bpf_fd, sizeof(bpf_fd)) != 0) {
        perror("SO_ATTACH_BPF"); return 1;
    }
    printf("attached P1\n");
    int snd = socket(AF_INET, SOCK_DGRAM, 0);

    uint64_t out;
    map_lookup(m3, &out);
    trigger(snd, &sa, recv_sock);
    map_lookup(m3, &out);
    printf("==> m3 = 0x%016llx (0x2222... => P2 ran via tail call)\n", (unsigned long long)out);

    return 0;
}