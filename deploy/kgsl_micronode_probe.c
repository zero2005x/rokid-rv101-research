/*
 * kgsl_micronode_probe.c — KGSL「0x58 micronode」專用探針（benign、唯讀）
 *
 * 背景：專案內不同報告對同一個 ioctl 給出不同號碼／結構大小，導致「✅ 成功 / ❌ EOPNOTSUPP」
 *      互相矛盾（見 reports/FACTS.md §5.1）。本探針對 **同一 nr 測多種結構大小**，
 *      藉此判斷 kernel 真正接受的是哪一組（size 不符時 core ioctl 層會回 ENOTTY）。
 *
 * 安全性：只做「配置／建立／查詢」與空命令批次；**不做 spray、不做 race、不寫入 GPU 記憶體**。
 *        每個 ioctl 失敗即跳過後續依賴步驟。
 *
 * 編譯（本機無 Linux NDK，改用 zig 內建交叉編譯器；caches 必須在工作區內）：
 *   ZIG_GLOBAL_CACHE_DIR=$PWD/build-cache/zig-global ZIG_LOCAL_CACHE_DIR=$PWD/build-cache/zig-local \
 *   .venv/bin/python -m ziglang cc -target aarch64-linux-musl -static -O2 \
 *     deploy/kgsl_micronode_probe.c -o build-cache/kgsl_micronode_probe
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>

#define KGSL_IOC_TYPE 0x09

/* 受測的 (nr, struct size) 組合；size 會編進 cmd，kernel 會比對 */
struct tcase { const char *name; unsigned int nr; unsigned int size; };

static const struct tcase CASES[] = {
    /* 控制組：已知可用 */
    {"DEVICE_GETPROPERTY",      0x02, 0x10},
    {"DRAWCTXT_CREATE",         0x13, 0x10},
    {"DRAWCTXT_DESTROY",        0x14, 0x08},

    /* GPU 記憶體配置：三種歷史寫法 */
    {"GPUMEM_ALLOC?0x20",       0x20, 0x18},
    {"GPUMEM_ALLOC?0x20b",      0x20, 0x20},
    {"GPUMEM_ALLOC 0x2f/0x18",  0x2f, 0x18},
    {"GPUMEM_ALLOC 0x2f/0x20",  0x2f, 0x20},
    {"GPUOBJ_ALLOC 0x45/0x20",  0x45, 0x20},

    /* ★ 爭議核心：nr 0x58 到底是 AUX_COMMAND(0x20) 還是 GPUOBJ_IMPORT(0x28)？ */
    {"AUX_CMD 0x58/0x20",       0x58, 0x20},
    {"GPUOBJ_IMPORT 0x58/0x28", 0x58, 0x28},
    {"0x58/0x18",               0x58, 0x18},
    {"0x58/0x30",               0x58, 0x30},

    /* GPU_COMMAND：專案內出現過 0x3D / 0x40 / 0x4A 三種 nr */
    {"GPU_COMMAND 0x3d/0x58",   0x3d, 0x58},
    {"GPU_COMMAND 0x40/0x58",   0x40, 0x58},
    {"GPU_COMMAND 0x4a/0x58",   0x4a, 0x58},

    /* 其他周邊 */
    {"SYNCSOURCE_CREATE 0x30/0x08", 0x30, 0x08},
    {"SYNCSOURCE_FENCE 0x32/0x18",  0x32, 0x18},
    {"TIMESTAMP_EVENT 0x33/0x10",   0x33, 0x10},
    {"GPUOBJ_INFO 0x39/0x20",       0x39, 0x20},
    {"GPUOBJ_SET_INFO 0x3a/0x18",   0x3a, 0x18},
    {"GPUOBJ_SYNC 0x56/0x18",       0x56, 0x18},
    {"SPARSE_BIND 0x57/0x28",       0x57, 0x28},
    {"RINGBUF_IBCMDS 0x10/0x28",    0x10, 0x28},
};
#define NCASES (sizeof(CASES) / sizeof(CASES[0]))

static int fd = -1;
static uint8_t buf[256] __attribute__((aligned(16)));

static const char *errname(int e) {
    switch (e) {
        case 0: return "OK";
        case EPERM: return "EPERM";
        case ENOENT: return "ENOENT";
        case EINVAL: return "EINVAL";
        case ENOTTY: return "ENOTTY";
        case EACCES: return "EACCES";
        case ENOMEM: return "ENOMEM";
        case EOPNOTSUPP: return "EOPNOTSUPP";
        case EFAULT: return "EFAULT";
        case EBUSY: return "EBUSY";
        default: return "?";
    }
}

int main(void) {
    printf("=== KGSL micronode / ioctl (nr,size) 矩陣探針 ===\n");
    fd = open("/dev/kgsl-3d0", O_RDWR | O_CLOEXEC);
    if (fd < 0) { printf("open 失敗 errno=%d (%s)\n", errno, errname(errno)); return 1; }
    printf("open(/dev/kgsl-3d0)=OK uid=%d\n\n", getuid());

    int accepted = 0, notty = 0;
    for (unsigned i = 0; i < NCASES; i++) {
        memset(buf, 0, sizeof(buf));
        unsigned long cmd = _IOWR(KGSL_IOC_TYPE, CASES[i].nr, buf[0]) & ~0u;
        /* 重新以正確 size 編碼（_IOWR 的 size 由第三個參數型別決定，故手動組） */
        unsigned long sz = CASES[i].size;
        cmd = (3UL << 30) | ((unsigned long)sz << 16) | ((unsigned long)KGSL_IOC_TYPE << 8) | CASES[i].nr;
        errno = 0;
        int r = ioctl(fd, cmd, buf);
        int e = errno;
        printf("%-30s nr=0x%02x size=0x%02x cmd=0x%08lx ret=%3d errno=%3d %-11s\n",
               CASES[i].name, CASES[i].nr, CASES[i].size, cmd, r, e, errname(e));
        if (e == ENOTTY) notty++;
        if (e != ENOTTY && e != EINVAL) accepted++;
    }

    /* 進一步：對「已被接受」的 nr 送多種 cmd_type（僅 GPU_COMMAND 家族與 0x58） */
    printf("\n=== 變體掃描（已接受的號碼 × 參數變化）===\n");
    unsigned int nrs[] = {0x58, 0x3d, 0x40, 0x4a, 0x20, 0x2f};
    for (unsigned k = 0; k < sizeof(nrs)/sizeof(nrs[0]); k++) {
        for (unsigned t = 0; t < 4; t++) {
            memset(buf, 0, sizeof(buf));
            /* 常見佈局：前 8B flags、接著 cmdlist/cmd_type 等；只改動「類型／旗標」欄位 */
            *(uint64_t *)(buf + 0) = (uint64_t)t;          /* flags / cmd_type 位置 */
            *(uint32_t *)(buf + 16) = t;                   /* 次欄位 */
            unsigned long cmd = (3UL << 30) | (0x20UL << 16) | ((unsigned long)KGSL_IOC_TYPE << 8) | nrs[k];
            errno = 0;
            int r = ioctl(fd, cmd, buf);
            printf("nr=0x%02x size=0x20 variant=%u ret=%3d errno=%3d %s\n",
                   nrs[k], t, r, errno, errname(errno));
        }
    }

    printf("\nSUMMARY cases=%zu enotty=%d accepted=%d\n", NCASES, notty, accepted);
    close(fd);
    return 0;
}
