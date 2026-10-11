/*
 * kgsl_context_probe.c - 探測正確的 KGSL context 創建方式
 *
 * 發現: "legacy context switch not supported"
 * 需要使用新的 context 創建 API
 *
 * 編譯:
 * aarch64-linux-android31-clang -static -O2 -o kgsl_context_probe kgsl_context_probe.c
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <stdint.h>

/* KGSL Magic */
#define KGSL_IOC_TYPE 0x09

/* Context 創建 flags */
#define KGSL_CONTEXT_NO_GMEM_ALLOC 0x00000001
#define KGSL_CONTEXT_SUBMIT_IB_LIST 0x00000002
#define KGSL_CONTEXT_CTX_SWITCH 0x00000008
#define KGSL_CONTEXT_PREAMBLE 0x00000010
#define KGSL_CONTEXT_TRASH_STATE 0x00000020
#define KGSL_CONTEXT_PER_CONTEXT_TS 0x00000040
#define KGSL_CONTEXT_USER_GENERATED_TS 0x00000080
#define KGSL_CONTEXT_END_OF_FRAME 0x00000100
#define KGSL_CONTEXT_NO_FAULT_TOLERANCE 0x00000200
#define KGSL_CONTEXT_SYNC 0x00000400
#define KGSL_CONTEXT_PWR_CONSTRAINT 0x00000800
#define KGSL_CONTEXT_PRIORITY_MASK 0x0000F000
#define KGSL_CONTEXT_PRIORITY_SHIFT 12
#define KGSL_CONTEXT_PRIORITY_HIGH 8
#define KGSL_CONTEXT_PRIORITY_MEDIUM 6
#define KGSL_CONTEXT_PRIORITY_LOW 4
#define KGSL_CONTEXT_IFH_NOP 0x00010000
#define KGSL_CONTEXT_SECURE 0x00020000
#define KGSL_CONTEXT_NO_SNAPSHOT 0x00040000
#define KGSL_CONTEXT_SPARSE 0x00080000

/* Context type */
#define KGSL_CONTEXT_TYPE_ANY 0
#define KGSL_CONTEXT_TYPE_GL 1
#define KGSL_CONTEXT_TYPE_CL 2
#define KGSL_CONTEXT_TYPE_RS 3
#define KGSL_CONTEXT_TYPE_UNKNOWN 0x1E
#define KGSL_CONTEXT_TYPE_SHIFT 16
#define KGSL_CONTEXT_TYPE_MASK 0x01F0000

/* 標準 ioctl (從 kgsl.h) */
#define IOCTL_KGSL_DRAWCTXT_CREATE 0xc0080913

/* 可能的新 context 創建 ioctl */
#define IOCTL_KGSL_DRAWCTXT_CREATE_V2 0xc0080946 // 可能的新版本
#define IOCTL_KGSL_DRAWCTXT_CREATE_V3 0xc0100946 // 16 bytes

/* 不同的結構嘗試 */
struct kgsl_drawctxt_create_v1
{
    unsigned int flags;
    unsigned int drawctxt_id;
};

struct kgsl_drawctxt_create_v2
{
    unsigned int flags;
    unsigned int drawctxt_id;
    unsigned int type; // Context type
    unsigned int padding;
};

struct kgsl_drawctxt_create_v3
{
    unsigned int flags;
    unsigned int drawctxt_id;
    unsigned int bin_base_offset;
    unsigned int cond_flags;
};

void try_context_with_flags(int fd, unsigned int flags, const char *desc)
{
    struct kgsl_drawctxt_create_v1 ctx = {0};
    ctx.flags = flags;

    errno = 0;
    int ret = ioctl(fd, IOCTL_KGSL_DRAWCTXT_CREATE, &ctx);
    printf("  flags=0x%08x (%s): ret=%d, errno=%d, id=%u\n",
           flags, desc, ret, errno, ctx.drawctxt_id);
}

void test_context_creation(int fd)
{
    printf("\n=== Testing Context Creation with Various Flags ===\n");

    // 嘗試各種 flags 組合
    try_context_with_flags(fd, 0, "default");
    try_context_with_flags(fd, KGSL_CONTEXT_NO_GMEM_ALLOC, "NO_GMEM_ALLOC");
    try_context_with_flags(fd, KGSL_CONTEXT_SUBMIT_IB_LIST, "SUBMIT_IB_LIST");
    try_context_with_flags(fd, KGSL_CONTEXT_PER_CONTEXT_TS, "PER_CONTEXT_TS");
    try_context_with_flags(fd, KGSL_CONTEXT_USER_GENERATED_TS, "USER_GENERATED_TS");
    try_context_with_flags(fd, KGSL_CONTEXT_SYNC, "SYNC");
    try_context_with_flags(fd, KGSL_CONTEXT_NO_FAULT_TOLERANCE, "NO_FAULT_TOLERANCE");
    try_context_with_flags(fd, KGSL_CONTEXT_IFH_NOP, "IFH_NOP");
    try_context_with_flags(fd, KGSL_CONTEXT_SPARSE, "SPARSE");

    // 組合 flags
    try_context_with_flags(fd,
                           KGSL_CONTEXT_NO_GMEM_ALLOC | KGSL_CONTEXT_PER_CONTEXT_TS,
                           "NO_GMEM|PER_CONTEXT_TS");

    try_context_with_flags(fd,
                           KGSL_CONTEXT_SUBMIT_IB_LIST | KGSL_CONTEXT_PER_CONTEXT_TS,
                           "SUBMIT_IB|PER_CONTEXT_TS");

    // 帶優先級
    unsigned int priority_flags = KGSL_CONTEXT_PRIORITY_MEDIUM << KGSL_CONTEXT_PRIORITY_SHIFT;
    try_context_with_flags(fd, priority_flags, "PRIORITY_MEDIUM");

    // 帶 type
    unsigned int type_gl = KGSL_CONTEXT_TYPE_GL << KGSL_CONTEXT_TYPE_SHIFT;
    try_context_with_flags(fd, type_gl, "TYPE_GL");

    unsigned int type_cl = KGSL_CONTEXT_TYPE_CL << KGSL_CONTEXT_TYPE_SHIFT;
    try_context_with_flags(fd, type_cl, "TYPE_CL");

    // 組合所有
    try_context_with_flags(fd,
                           KGSL_CONTEXT_SUBMIT_IB_LIST | KGSL_CONTEXT_PER_CONTEXT_TS | type_gl | priority_flags,
                           "SUBMIT_IB|PER_TS|GL|MED_PRI");
}

void scan_context_ioctls(int fd)
{
    printf("\n=== Scanning for Context-related IOCTLs ===\n");

    struct kgsl_drawctxt_create_v2 ctx = {0};

    // 掃描 0x09XX 範圍，尋找 context 相關的 ioctl
    for (int nr = 0x10; nr <= 0x20; nr++)
    {
        unsigned int cmd = 0xc0080000 | (0x09 << 8) | nr;

        ctx.flags = 0;
        ctx.drawctxt_id = 0;

        errno = 0;
        int ret = ioctl(fd, cmd, &ctx);

        if (errno != 25 && errno != 22)
        {
            printf("  ioctl(0x%08x, nr=0x%02x): ret=%d, errno=%d (%s)",
                   cmd, nr, ret, errno, strerror(errno));
            if (ret == 0)
            {
                printf(", id=%u", ctx.drawctxt_id);
            }
            printf("\n");
        }
    }

    // 也檢查 0x40-0x50 範圍
    printf("\n[*] Scanning 0x40-0x50 range:\n");
    for (int nr = 0x40; nr <= 0x50; nr++)
    {
        for (int size = 8; size <= 32; size += 8)
        {
            unsigned int cmd = 0xc0000000 | (size << 16) | (0x09 << 8) | nr;

            char buf[64] = {0};

            errno = 0;
            int ret = ioctl(fd, cmd, buf);

            if (errno != 25 && errno != 22 && ret >= 0)
            {
                printf("  ioctl(0x%08x, nr=0x%02x, size=%d): ret=%d\n",
                       cmd, nr, size, ret);
            }
        }
    }
}

void test_gpu_operations_without_context(int fd)
{
    printf("\n=== Testing GPU Operations Without Context ===\n");

    /* 嘗試直接分配記憶體，不需要 context */

    // GPUMEM_ALLOC (0x22)
    struct
    {
        unsigned long gpuaddr;
        size_t size;
        unsigned int flags;
    } alloc = {0};
    alloc.size = 4096;

    unsigned int sizes[] = {16, 20, 24, 32, 0};

    for (int s = 0; sizes[s] != 0; s++)
    {
        unsigned int cmd = 0xc0000000 | (sizes[s] << 16) | 0x0922;
        memset(&alloc, 0, sizeof(alloc));
        alloc.size = 4096;

        errno = 0;
        int ret = ioctl(fd, cmd, &alloc);
        if (errno != 25 && errno != 22)
        {
            printf("  GPUMEM_ALLOC (size=%d): ret=%d, errno=%d, gpuaddr=0x%lx\n",
                   sizes[s], ret, errno, alloc.gpuaddr);
        }
    }
}

void test_secure_context(int fd)
{
    printf("\n=== Testing Secure Context ===\n");

    // 嘗試創建 secure context
    struct kgsl_drawctxt_create_v1 ctx = {0};
    ctx.flags = KGSL_CONTEXT_SECURE;

    errno = 0;
    int ret = ioctl(fd, IOCTL_KGSL_DRAWCTXT_CREATE, &ctx);
    printf("  SECURE context: ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));
}

void test_mmap_gpu_memory(int fd)
{
    printf("\n=== Testing GPU Memory mmap ===\n");

    // 嘗試各種 offset 進行 mmap
    for (unsigned long offset = 0; offset < 0x100000; offset += 0x10000)
    {
        void *ptr = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, offset);
        if (ptr != MAP_FAILED)
        {
            printf("[+] mmap(offset=0x%lx) succeeded at %p\n", offset, ptr);
            munmap(ptr, 4096);
        }
    }
}

int main(int argc, char **argv)
{
    printf("=== KGSL Context Probe ===\n");
    printf("Finding correct context creation method\n");
    printf("========================================\n");

    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
    {
        printf("[-] Cannot open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Opened /dev/kgsl-3d0: fd=%d\n", fd);

    // 測試各種 context 創建方式
    test_context_creation(fd);

    // 掃描 context 相關 ioctl
    scan_context_ioctls(fd);

    // 測試不需要 context 的操作
    test_gpu_operations_without_context(fd);

    // 測試 secure context
    test_secure_context(fd);

    // 測試 mmap
    test_mmap_gpu_memory(fd);

    close(fd);
    return 0;
}
