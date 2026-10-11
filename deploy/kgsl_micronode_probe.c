/*
 * kgsl_micronode_probe.c — dedicated probe for the KGSL "0x58 micronode" (benign, read-only)
 *
 * Background: different reports in this project give different numbers / struct sizes for the
 *       same ioctl, so "✅ success / ❌ EOPNOTSUPP" contradict each other (see
 *       reports/FACTS.md §5.1). This probe tests **several struct sizes for the same nr** to
 *       determine which set the kernel really accepts (a size mismatch makes the core ioctl
 *       layer return ENOTTY).
 *
 * Safety: only "allocate / create / query" and empty command batches; **no spray, no race, no
 *        writes to GPU memory**. Every failed ioctl skips the steps that depend on it.
 *
 * Build (no Linux NDK locally, so use zig's bundled cross-compiler; caches must stay in the workspace):
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

/* (nr, struct size) combinations under test; the size is encoded into cmd and the kernel compares it */
struct tcase { const char *name; unsigned int nr; unsigned int size; };

static const struct tcase CASES[] = {
    /* control group: known to work */
    {"DEVICE_GETPROPERTY",      0x02, 0x10},
    {"DRAWCTXT_CREATE",         0x13, 0x10},
    {"DRAWCTXT_DESTROY",        0x14, 0x08},

    /* GPU memory allocation: three historical spellings */
    {"GPUMEM_ALLOC?0x20",       0x20, 0x18},
    {"GPUMEM_ALLOC?0x20b",      0x20, 0x20},
    {"GPUMEM_ALLOC 0x2f/0x18",  0x2f, 0x18},
    {"GPUMEM_ALLOC 0x2f/0x20",  0x2f, 0x20},
    {"GPUOBJ_ALLOC 0x45/0x20",  0x45, 0x20},

    /* ★ core of the dispute: is nr 0x58 AUX_COMMAND (0x20) or GPUOBJ_IMPORT (0x28)? */
    {"AUX_CMD 0x58/0x20",       0x58, 0x20},
    {"GPUOBJ_IMPORT 0x58/0x28", 0x58, 0x28},
    {"0x58/0x18",               0x58, 0x18},
    {"0x58/0x30",               0x58, 0x30},

    /* GPU_COMMAND: three different nrs (0x3D / 0x40 / 0x4A) appear in this project */
    {"GPU_COMMAND 0x3d/0x58",   0x3d, 0x58},
    {"GPU_COMMAND 0x40/0x58",   0x40, 0x58},
    {"GPU_COMMAND 0x4a/0x58",   0x4a, 0x58},

    /* other peripherals */
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
        /* re-encode with the correct size (_IOWR takes the size from the third argument's type,
         * so build it by hand) */
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

    /* Further: send several cmd_types for the "accepted" nrs (only the GPU_COMMAND family and 0x58) */
    printf("\n=== 變體掃描（已接受的號碼 × 參數變化）===\n");
    unsigned int nrs[] = {0x58, 0x3d, 0x40, 0x4a, 0x20, 0x2f};
    for (unsigned k = 0; k < sizeof(nrs)/sizeof(nrs[0]); k++) {
        for (unsigned t = 0; t < 4; t++) {
            memset(buf, 0, sizeof(buf));
            /* Common layout: 8 bytes of flags first, then cmdlist/cmd_type etc.; only the
             * type/flag field changes */
            *(uint64_t *)(buf + 0) = (uint64_t)t;          /* flags / cmd_type slot */
            *(uint32_t *)(buf + 16) = t;                   /* secondary field */
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
