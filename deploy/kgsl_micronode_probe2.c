/*
 * kgsl_micronode_probe2.c — 「0x58 micronode」語意驗證（v2，benign）
 *
 * v1（deploy/kgsl_micronode_probe.c）發現：nr=0x58 對 **任何 struct size（0x18/0x20/0x28/0x30）
 * 與任何參數內容都回 0（OK）** → 疑似「不驗證即成功」的空路徑，這正是歷史報告「0x58 ✅ 成功」
 * 互相矛盾的根源。v2 用**故意非法的參數**去逼出驗證行為：
 *
 *   T1  arg=NULL            → 有空路徑會回 0；真正 kernel 路徑應 EFAULT
 *   T2  gpuaddr=0xdead0000  → 真正 import/command 路徑應 EINVAL/ENODEV
 *   T3  gpuaddr=0x4000000000（合法外觀）
 *   T4  size/欄位極大值
 *   T5  先做合法 GPUMEM_ALLOC（0x20 與 0x2f 各試）→ 再用該 gpuaddr 測 0x58
 *
 * 安全性：僅配置與查詢；不做 spray／race。alloc 後嘗試 free。
 *
 * 編譯：ZIG_GLOBAL_CACHE_DIR=$PWD/build-cache/zig-global ZIG_LOCAL_CACHE_DIR=$PWD/build-cache/zig-local \
 *       .venv/bin/python -m ziglang cc -target aarch64-linux-musl -static -O2 \
 *       deploy/kgsl_micronode_probe2.c -o build-cache/kgsl_micronode_probe2
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>

#define KGSL_IOC_TYPE 0x09
static int fd = -1;
static uint8_t buf[256] __attribute__((aligned(16)));

static unsigned long cmd(unsigned nr, unsigned size) {
    return (3UL << 30) | ((unsigned long)size << 16) | ((unsigned long)KGSL_IOC_TYPE << 8) | nr;
}
static const char *errname(int e) {
    switch (e) {
        case 0: return "OK";
        case 1: return "EPERM";
        case 2: return "ENOENT";
        case 12: return "ENOMEM";
        case 13: return "EACCES";
        case 14: return "EFAULT";
        case 16: return "EBUSY";
        case 19: return "ENODEV";
        case 22: return "EINVAL";
        case 25: return "ENOTTY";
        case 75: return "EOVERFLOW";
        case 95: return "EOPNOTSUPP";
        default: return "?";
    }
}
static void run(const char *tag, unsigned nr, unsigned size, void *arg, int arglen) {
    memset(buf, 0, sizeof(buf));
    if (arg && arglen > 0 && arglen <= (int)sizeof(buf)) memcpy(buf, arg, arglen);
    errno = 0;
    int r = ioctl(fd, cmd(nr, size), arg ? buf : NULL);
    printf("%-42s nr=0x%02x sz=0x%02x ret=%3d errno=%3d %s\n",
           tag, nr, size, r, errno, errname(errno));
    fflush(stdout);
}

int main(void) {
    printf("=== KGSL 0x58 語意驗證 v2 ===\n");
    fd = open("/dev/kgsl-3d0", O_RDWR | O_CLOEXEC);
    if (fd < 0) { printf("open 失敗 errno=%d\n", errno); return 1; }
    printf("open=OK uid=%d\n\n", getuid());

    printf("--- T1: arg=NULL（真正 kernel 路徑應 EFAULT）---\n");
    run("0x58/0x20 NULL", 0x58, 0x20, NULL, 0);
    run("0x40/0x58 NULL (GPU_COMMAND)", 0x40, 0x58, NULL, 0);
    run("0x45/0x20 NULL (GPUOBJ_ALLOC)", 0x45, 0x20, NULL, 0);

    printf("\n--- T2/T3: 非 gpuaddr（0xdead0000 / 0x4000000000）---\n");
    struct { uint64_t a; uint64_t b; uint32_t c; uint32_t d; uint32_t e; uint32_t f; } s;
    memset(&s, 0, sizeof(s)); s.a = 0xdead0000ULL;
    run("0x58/0x20 gpuaddr=0xdead0000", 0x58, 0x20, &s, sizeof(s));
    memset(&s, 0, sizeof(s)); s.a = 0x4000000000ULL;
    run("0x58/0x20 gpuaddr=0x4000000000", 0x58, 0x20, &s, sizeof(s));
    memset(&s, 0, sizeof(s)); s.a = 0x4000000000ULL;
    run("0x45/0x20 size=0x1000", 0x45, 0x20, &s, sizeof(s));

    printf("\n--- T4: 極大值 ---\n");
    memset(&s, 0, sizeof(s)); s.a = ~0ULL; s.b = ~0ULL; s.c = ~0u; s.d = ~0u;
    run("0x58/0x20 全 0xffff", 0x58, 0x20, &s, sizeof(s));

    printf("\n--- T5: 合法 GPUMEM_ALLOC 嘗試（size=0x1000）---\n");
    struct { uint64_t size; uint64_t flags; uint64_t gpuaddr; uint32_t id; uint32_t pad; } al;
    for (unsigned i = 0; i < 2; i++) {
        unsigned nr = i == 0 ? 0x2f : 0x20;
        memset(&al, 0, sizeof(al)); al.size = 0x1000; al.flags = 0;
        errno = 0;
        int r = ioctl(fd, cmd(nr, 0x18), &al);
        printf("GPUMEM_ALLOC nr=0x%02x size=0x18 ret=%3d errno=%3d %-11s id=%u gpuaddr=0x%llx\n",
               nr, r, errno, errname(errno), al.id, (unsigned long long)al.gpuaddr);
        if (r == 0 && al.id) {
            struct { uint32_t id; uint32_t pad; } fr = { al.id, 0 };
            errno = 0;
            int r2 = ioctl(fd, cmd(0x10, 0x08), &fr);
            printf("   └ GPUMEM_FREE(0x10/0x08) ret=%d errno=%d %s\n", r2, errno, errname(errno));
        }
    }

    printf("\n--- T6: GPU_COMMAND(0x40) 空批次 vs 假 cmdlist ---\n");
    struct { uint64_t flags; uint64_t cmdlist; uint32_t cmdlist_len; uint32_t objlist_len; uint32_t synclist_len; uint32_t ctx; } gc;
    memset(&gc, 0, sizeof(gc)); gc.cmdlist_len = 0;
    run("0x40/0x58 空批次", 0x40, 0x58, &gc, sizeof(gc));
    memset(&gc, 0, sizeof(gc)); gc.cmdlist = 0xdead0000ULL; gc.cmdlist_len = 1;
    run("0x40/0x58 假 cmdlist", 0x40, 0x58, &gc, sizeof(gc));

    printf("\nSUMMARY done\n");
    close(fd);
    return 0;
}
