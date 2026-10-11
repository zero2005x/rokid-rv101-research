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

static int create_map(uint64_t flags) {
    union bpf_attr attr; memset(&attr, 0, sizeof(attr));
    attr.map_type = BPF_MAP_TYPE_ARRAY;
    attr.key_size = 4; attr.value_size = 8; attr.max_entries = 1;
    attr.map_flags = flags;
    errno = 0;
    int fd = syscall(__NR_bpf, BPF_MAP_CREATE, &attr, sizeof(attr));
    printf("MAP_CREATE flags=0x%llx -> fd=%d errno=%d(%s)\n",
           (unsigned long long)flags, fd, errno, strerror(errno));
    return fd;
}

static long map_update(int mfd, uint64_t v) {
    uint32_t k = 0;
    union bpf_attr attr; memset(&attr, 0, sizeof(attr));
    attr.map_fd = mfd; attr.key = (uint64_t)(uintptr_t)&k;
    attr.value = (uint64_t)(uintptr_t)&v; attr.flags = BPF_ANY;
    errno = 0;
    long r = syscall(__NR_bpf, BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
    printf("UPDATE(m%d, 0x%016llx) -> %ld errno=%d(%s)\n",
           mfd, (unsigned long long)v, r, errno, strerror(errno));
    return r;
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

int main(void) {
    int m1 = create_map(BPF_F_RDONLY_PROG);   /* readonly map we later modify via bug */
    int m3 = create_map(0);                    /* normal map to exfil the observed value */
    map_update(m1, 0x1111111111111111ULL);

    /* r6=lookup(m1); r7=*(u64*)(r6+0); r8=lookup(m3); *(u64*)(r8+0)=r7; r0=0; exit */
    struct bpf_insn insns[22];
    memset(insns, 0, sizeof(insns));
    insns[0].code = 0x18; insns[0].dst_reg = 6; insns[0].src_reg = 1; insns[0].imm = m1;   /* r6 = map1 ptr */
    insns[1].code = 0x00;
    insns[2].code = 0x62; insns[2].dst_reg = 10; insns[2].off = -4;                       /* *(u32*)(fp-4)=0 */
    insns[3].code = 0xbf; insns[3].dst_reg = 2; insns[3].src_reg = 10;                     /* r2 = fp */
    insns[4].code = 0x07; insns[4].dst_reg = 2; insns[4].imm = -4;                         /* r2 += -4 (key) */
    insns[5].code = 0xbf; insns[5].dst_reg = 1; insns[5].src_reg = 6;                      /* r1 = r6 */
    insns[6].code = 0x85; insns[6].imm = 1;                                                /* call lookup */
    insns[7].code = 0x15; insns[7].dst_reg = 0; insns[7].off = 13;                         /* if r0==0 goto exit */
    insns[8].code = 0xbf; insns[8].dst_reg = 6; insns[8].src_reg = 0;                      /* r6 = r0 */
    insns[9].code = 0x79; insns[9].dst_reg = 7; insns[9].src_reg = 6;                      /* r7 = *(u64*)(r6+0) */
    insns[10].code = 0x18; insns[10].dst_reg = 8; insns[10].src_reg = 1; insns[10].imm = m3; /* r8 = map3 ptr */
    insns[11].code = 0x00;
    insns[12].code = 0x62; insns[12].dst_reg = 10; insns[12].off = -4;                     /* *(u32*)(fp-4)=0 */
    insns[13].code = 0xbf; insns[13].dst_reg = 2; insns[13].src_reg = 10;                  /* r2 = fp */
    insns[14].code = 0x07; insns[14].dst_reg = 2; insns[14].imm = -4;                      /* r2 += -4 (key) */
    insns[15].code = 0xbf; insns[15].dst_reg = 1; insns[15].src_reg = 8;                   /* r1 = r8 */
    insns[16].code = 0x85; insns[16].imm = 1;                                              /* call lookup */
    insns[17].code = 0x15; insns[17].dst_reg = 0; insns[17].off = 3;                       /* if r0==0 goto exit */
    insns[18].code = 0xbf; insns[18].dst_reg = 8; insns[18].src_reg = 0;                   /* r8 = r0 */
    insns[19].code = 0x7b; insns[19].dst_reg = 8; insns[19].src_reg = 7;                   /* *(u64*)(r8+0) = r7 */
    insns[20].code = 0xb7; insns[20].dst_reg = 0;                                         /* r0 = 0 */
    insns[21].code = 0x95;                                                                /* exit */
    char log_buf[8192];
    union bpf_attr attr; memset(&attr, 0, sizeof(attr));
    memset(log_buf, 0, sizeof(log_buf));
    attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
    attr.insn_cnt = 22;
    attr.insns = (uint64_t)(uintptr_t)insns;
    attr.license = (uint64_t)(uintptr_t)"GPL";
    attr.log_level = 2; attr.log_size = sizeof(log_buf);
    attr.log_buf = (uint64_t)(uintptr_t)log_buf;
    errno = 0;
    long pfd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));
    printf("PROG_LOAD -> fd=%ld errno=%d(%s)\n", pfd, errno, strerror(errno));
    if (pfd < 0) { if (log_buf[0]) printf("LOG: %.4000s\n", log_buf); return 1; }
    if (log_buf[0]) printf("LOG: %s\n", log_buf);

    int recv_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (recv_sock < 0) { perror("socket"); return 1; }
    struct sockaddr_in sa; memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET; sa.sin_addr.s_addr = htonl(0x7f000001); sa.sin_port = 0;
    bind(recv_sock, (struct sockaddr*)&sa, sizeof(sa));
    socklen_t slen = sizeof(sa);
    getsockname(recv_sock, (struct sockaddr*)&sa, &slen);
    int bpf_fd = (int)pfd;
    if (setsockopt(recv_sock, SOL_SOCKET, SO_ATTACH_BPF, &bpf_fd, sizeof(bpf_fd)) != 0) {
        perror("SO_ATTACH_BPF"); return 1;
    }
    printf("attached\n");

    int snd = socket(AF_INET, SOCK_DGRAM, 0);
    uint64_t junk = 0x00;
    sendto(snd, &junk, sizeof(junk), 0, (struct sockaddr*)&sa, sizeof(sa));
    usleep(200000);
    uint64_t out;
    map_lookup(m3, &out);
    printf("==> observed value from readonly map: 0x%016llx (expected 0x1111111111111111)\n",
           (unsigned long long)out);

    /* now change m1 value */
    map_update(m1, 0xABCDABCDABCDABCDULL);

    sendto(snd, &junk, sizeof(junk), 0, (struct sockaddr*)&sa, sizeof(sa));
    usleep(200000);
    map_lookup(m3, &out);
    printf("==> observed after m1 changed: 0x%016llx (0x1111... -> JIT folded immediate; 0xABCD... -> real memory read)\n",
           (unsigned long long)out);

    return 0;
}