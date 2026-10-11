/*
 * kgsl_ioctl_probe.c — KGSL ioctl 世代回測探針（唯讀、benign）
 *
 * 目的：裁決「同一組 ioctl 在不同韌體世代回傳值不同」的歷史爭議
 *      （1.14.006 時代報告寫 ✅ 成功；1.22.009 實測 ENOTSUP／ENOTTY）。
 *
 * 設計原則（安全）：
 *   - **不做任何 exploit**：沒有 heap spray、沒有 fake fops、沒有 race。
 *   - 參數一律 zero-init；只呼叫「查詢／配置／建立」類 ioctl。
 *   - GPU_COMMAND 類以**空命令批次**送（會在最前段被拒），並由外層 `timeout` 保護。
 *
 * 輸出：每個 ioctl 一行 `name cmd=0x… ret=… errno=… (…)`，最後一行 SUMMARY。
 *
 * 編譯：deploy/build_kgsl_probe.sh（NDK clang，與 root_daemon 相同工具鏈）
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>

/* ---- ioctl 編碼（取自本專案既有探針 source_code/kgsl_gpu_cmd.c，對應 msm_kgsl.h）---- */
#define IOCTL_KGSL_DEVICE_GETPROPERTY 0xc0100902
#define IOCTL_KGSL_GPUMEM_ALLOC       0xc018092f
#define IOCTL_KGSL_GPUMEM_FREE        0x40080910
#define IOCTL_KGSL_DRAWCTXT_CREATE    0xc0100913
#define IOCTL_KGSL_DRAWCTXT_DESTROY   0x40080914
#define IOCTL_KGSL_GPUOBJ_INFO        0xc0200939
#define IOCTL_KGSL_GPUOBJ_SET_INFO    0xc018093a
#define IOCTL_KGSL_GPUOBJ_IMPORT      0xc0280958
#define IOCTL_KGSL_GPUOBJ_SYNC        0xc0180956
#define IOCTL_KGSL_SPARSE_BIND        0xc0280957
#define IOCTL_KGSL_TIMESTAMP_EVENT    0xc010090e
#define IOCTL_KGSL_GPU_COMMAND        0xc0580940

/* 歷史爭議中出現過的號碼（用來判斷「當年到底打的是哪一個」） */
#define CMD_0xc0180920 0xc0180920u
#define CMD_0xc0180932 0xc0180932u
#define CMD_0xc0040956 0xc0040956u
#define CMD_0xc0040958 0xc0040958u

/* KGSL property ids（msm_kgsl.h）*/
#define KGSL_PROP_DEVICE_INFO       0x1
#define KGSL_PROP_VERSION           0x2
#define KGSL_PROP_GPU_RESET_STAT    0x7

struct gpumem_alloc_req { uint64_t size; uint64_t flags; uint64_t gpuaddr; uint32_t id; uint32_t padding; };
struct gpumem_free_req   { uint32_t id; uint32_t padding; };
struct dev_getproperty   { uint32_t type; void *value; uint32_t sizebytes; uint32_t padding; };
struct drawctxt_create   { uint32_t flags; uint32_t priority; uint32_t id; uint32_t padding; };
struct drawctxt_destroy  { uint32_t id; uint32_t padding; };
struct cmd_obj           { uint32_t type; uint32_t padding; uint64_t gpuaddr; uint32_t size; uint32_t padding2; };
struct gpu_command       { uint32_t flags; uint32_t cmdlist_len; struct cmd_obj *cmdlist; uint64_t timestamp; uint32_t context_id; uint32_t padding; };
struct gpuobj_info       { uint64_t gpuaddr; uint32_t id; uint32_t type; uint64_t size; uint32_t flags; uint32_t padding; };
struct gpuobj_import     { uint64_t gpuaddr; uint32_t id; uint32_t padding; };
/* 泛用大緩衝：避免個別結構大小差異造成越界讀 */
static uint8_t big[512] __attribute__((aligned(16)));

static int fd = -1;
static int pass = 0, fail = 0;

static const char *errname(int e) {
    switch (e) {
        case 0: return "OK";
        case EPERM: return "EPERM(1)";
        case ENOENT: return "ENOENT(2)";
        case EINVAL: return "EINVAL(22)";
        case ENOTTY: return "ENOTTY(25)";
        case EACCES: return "EACCES(13)";
        case ENOMEM: return "ENOMEM(12)";
        case EOPNOTSUPP: return "EOPNOTSUPP/ENOTSUP(95)";
        case EFAULT: return "EFAULT(14)";
        default: return "?";
    }
}

static void probe(const char *name, unsigned long cmd, void *arg, size_t len) {
    memset(big, 0, sizeof(big));
    void *p = arg ? arg : big;
    (void)len;
    errno = 0;
    int r = ioctl(fd, cmd, p);
    int e = errno;
    printf("%-30s cmd=0x%08lx ret=%3d errno=%3d %s\n", name, cmd, r, e, errname(e));
    if (r >= 0) pass++; else fail++;
}

int main(void) {
    printf("=== KGSL ioctl 回測（benign）===\n");
    fd = open("/dev/kgsl-3d0", O_RDWR | O_CLOEXEC);
    if (fd < 0) { printf("open(/dev/kgsl-3d0) 失敗: %s\n", strerror(errno)); return 1; }
    printf("open(/dev/kgsl-3d0) = OK\n");
    printf("uid=%d gid=%d\n", getuid(), getgid());

    /* 1) DEVICE_GETPROPERTY：世代資訊（最安全的查詢） */
    struct dev_getproperty p1; memset(&p1, 0, sizeof(p1));
    /* 實務上 value 需為 kernel 可寫緩衝；用 big 當目標 */
    p1.type = KGSL_PROP_VERSION; p1.value = big; p1.sizebytes = 64;
    probe("DEVICE_GETPROPERTY(VERSION)", IOCTL_KGSL_DEVICE_GETPROPERTY, &p1, sizeof(p1));
    printf("    version 原始位元組: %02x %02x %02x %02x\n", big[0], big[1], big[2], big[3]);

    memset(&p1, 0, sizeof(p1));
    p1.type = KGSL_PROP_GPU_RESET_STAT; p1.value = big; p1.sizebytes = 8;
    probe("DEVICE_GETPROPERTY(RESET)", IOCTL_KGSL_DEVICE_GETPROPERTY, &p1, sizeof(p1));

    /* 2) GPUMEM_ALLOC（歷史爭議核心） */
    struct gpumem_alloc_req a; memset(&a, 0, sizeof(a));
    a.size = 0x1000; a.flags = 0;
    probe("GPUMEM_ALLOC(0x1000)", IOCTL_KGSL_GPUMEM_ALLOC, &a, sizeof(a));
    printf("    id=%u gpuaddr=0x%llx\n", a.id, (unsigned long long)a.gpuaddr);
    uint32_t alloc_id = a.id;

    /* 3) 爭議中的其他號碼（判斷當年打的是哪個 ioctl） */
    probe("?0xc0180920(未知/舊編碼)", CMD_0xc0180920, NULL, 0);
    probe("?0xc0180932(未知)", CMD_0xc0180932, NULL, 0);
    probe("?0xc0040956(舊 SYNCSOURCE?)", CMD_0xc0040956, NULL, 0);
    probe("?0xc0040958(舊 GPU_AUX?)", CMD_0xc0040958, NULL, 0);

    /* 4) drawctxt create/destroy */
    struct drawctxt_create c; memset(&c, 0, sizeof(c));
    c.flags = 0x1; /* KGSL_CONTEXT_SAVE_GMEM 類的常見旗標 */
    probe("DRAWCTXT_CREATE", IOCTL_KGSL_DRAWCTXT_CREATE, &c, sizeof(c));
    printf("    ctx id=%u\n", c.id);
    uint32_t ctx = c.id;
    struct drawctxt_destroy d; memset(&d, 0, sizeof(d)); d.id = ctx;
    probe("DRAWCTXT_DESTROY", IOCTL_KGSL_DRAWCTXT_DESTROY, &d, sizeof(d));

    /* 5) GPUOBJ 系列 */
    struct gpuobj_info gi; memset(&gi, 0, sizeof(gi)); gi.id = alloc_id; gi.type = 1;
    probe("GPUOBJ_INFO(alloc_id)", IOCTL_KGSL_GPUOBJ_INFO, &gi, sizeof(gi));
    struct gpuobj_import im; memset(&im, 0, sizeof(im)); im.gpuaddr = a.gpuaddr;
    probe("GPUOBJ_IMPORT(self)", IOCTL_KGSL_GPUOBJ_IMPORT, &im, sizeof(im));
    probe("GPUOBJ_SYNC(0xc0180956)", IOCTL_KGSL_GPUOBJ_SYNC, NULL, 0);
    probe("SPARSE_BIND(0xc0280957)", IOCTL_KGSL_SPARSE_BIND, NULL, 0);
    probe("TIMESTAMP_EVENT", IOCTL_KGSL_TIMESTAMP_EVENT, NULL, 0);

    /* 6) GPU_COMMAND（空批次；最前段就會被拒） */
    struct gpu_command gc; memset(&gc, 0, sizeof(gc));
    gc.flags = 0; gc.cmdlist_len = 0; gc.cmdlist = NULL; gc.context_id = ctx;
    probe("GPU_COMMAND(空批次)", IOCTL_KGSL_GPU_COMMAND, &gc, sizeof(gc));

    /* 7) 釋放 */
    struct gpumem_free_req f; memset(&f, 0, sizeof(f)); f.id = alloc_id;
    probe("GPUMEM_FREE", IOCTL_KGSL_GPUMEM_FREE, &f, sizeof(f));

    printf("SUMMARY pass=%d fail=%d\n", pass, fail);
    close(fd);
    return 0;
}
