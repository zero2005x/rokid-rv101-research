/*
 * attack_surface_probe.c — 驗證 §5.3 攻擊向量可達性
 *
 * 對每個裝置節點做 O_RDONLY / O_RDWR open 測試，並對 I2C / Binder 做
 * ioctl 權限測試，最後驗證 BPF socket filter 的 load + SO_ATTACH_BPF。
 *
 * 編譯 (動態連結):
 *   aarch64-linux-android24-clang -O2 -o attack_surface_probe attack_surface_probe.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <linux/bpf.h>

#ifndef I2C_SLAVE
#define I2C_SLAVE 0x0703
#endif

#define BINDER_VERSION _IOWR('b', 9, struct binder_version)
struct binder_version
{
    int32_t protocol_version;
};

#ifndef __NR_bpf
#define __NR_bpf 280
#endif

static void try_open(const char *path)
{
    errno = 0;
    int ro = open(path, O_RDONLY);
    int ro_err = errno;
    if (ro >= 0)
        close(ro);

    errno = 0;
    int rw = open(path, O_RDWR);
    int rw_err = errno;
    if (rw >= 0)
        close(rw);

    printf("  %-30s O_RDONLY=%s  O_RDWR=%s\n", path,
           ro >= 0 ? "OK " : "DENY",
           rw >= 0 ? "OK " : "DENY");
    if (ro < 0)
        printf("      O_RDONLY errno=%d (%s)\n", ro_err, strerror(ro_err));
    if (rw < 0)
        printf("      O_RDWR   errno=%d (%s)\n", rw_err, strerror(rw_err));
}

static void test_i2c(void)
{
    printf("\n[I2C /dev/i2c-1] ioctl 權限測試\n");
    int fd = open("/dev/i2c-1", O_RDWR);
    if (fd < 0)
    {
        printf("  open O_RDWR failed: %s\n", strerror(errno));
        return;
    }
    printf("  open O_RDWR OK (fd=%d)\n", fd);

    /* 0x3f = mp2724 電池充電 IC；只設 slave 位址，不做寫入 */
    errno = 0;
    int ret = ioctl(fd, I2C_SLAVE, 0x3f);
    printf("  ioctl(I2C_SLAVE, 0x3f mp2724): ret=%d errno=%d (%s)\n",
           ret, errno, strerror(errno));

    /* 0x64 = cw221x 電量計；只設 slave 位址 */
    errno = 0;
    ret = ioctl(fd, I2C_SLAVE, 0x64);
    printf("  ioctl(I2C_SLAVE, 0x64 cw221x): ret=%d errno=%d (%s)\n",
           ret, errno, strerror(errno));

    close(fd);
}

static void test_binder(void)
{
    printf("\n[Binder] open + BINDER_VERSION ioctl\n");
    int fd = open("/dev/binder", O_RDWR);
    if (fd < 0)
    {
        printf("  open O_RDWR failed: %s\n", strerror(errno));
        return;
    }
    struct binder_version bv = {0};
    errno = 0;
    int ret = ioctl(fd, BINDER_VERSION, &bv);
    printf("  open O_RDWR OK (fd=%d); BINDER_VERSION ret=%d proto=%d errno=%d (%s)\n",
           fd, ret, bv.protocol_version, errno, strerror(errno));
    close(fd);
}

static void test_bpf_attach(void)
{
    printf("\n[BPF] load trivial SOCKET_FILTER + SO_ATTACH_BPF\n");

    /* r0 = 0 ; exit */
    struct bpf_insn prog[] = {
        {.code = BPF_ALU64 | BPF_MOV | BPF_K, .dst_reg = BPF_REG_0, .src_reg = 0, .off = 0, .imm = 0},
        {.code = BPF_JMP | BPF_EXIT, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0},
    };

    char log_buf[8192] = {0};
    union bpf_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
    attr.insns = (uint64_t)prog;
    attr.insn_cnt = sizeof(prog) / sizeof(prog[0]);
    attr.license = (uint64_t)"GPL";
    attr.log_buf = (uint64_t)log_buf;
    attr.log_size = sizeof(log_buf);
    attr.log_level = 1;

    int prog_fd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));
    if (prog_fd < 0)
    {
        printf("  BPF_PROG_LOAD failed: errno=%d (%s)\n%s\n", errno, strerror(errno), log_buf);
        return;
    }
    printf("  BPF_PROG_LOAD OK (prog_fd=%d)\n", prog_fd);

    int socks[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, socks) != 0)
    {
        printf("  socketpair failed: %s\n", strerror(errno));
        close(prog_fd);
        return;
    }

    int ret = setsockopt(socks[0], SOL_SOCKET, SO_ATTACH_BPF, &prog_fd, sizeof(prog_fd));
    printf("  SO_ATTACH_BPF: ret=%d errno=%d (%s)\n", ret, errno, strerror(errno));

    close(socks[0]);
    close(socks[1]);
    close(prog_fd);
}

int main(void)
{
    printf("=== Attack Surface Probe ===\n");
    printf("uid=%d euid=%d\n", getuid(), geteuid());

    printf("\n[Device nodes] open 測試\n");
    try_open("/dev/kgsl-3d0");
    try_open("/dev/i2c-0");
    try_open("/dev/i2c-1");
    try_open("/dev/i2c-2");
    try_open("/dev/binder");
    try_open("/dev/hwbinder");
    try_open("/dev/vndbinder");
    try_open("/dev/adsprpc-smd");
    try_open("/dev/adsprpc-smd-secure");
    try_open("/dev/ttyS0");
    try_open("/dev/ttyHS0");
    try_open("/dev/spidev0.0");
    try_open("/dev/diag");

    test_i2c();
    test_binder();
    test_bpf_attach();

    printf("\n=== Done ===\n");
    return 0;
}
