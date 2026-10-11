/*
 * adsprpc_ioctl_v2.c - 正確的 FASTRPC ioctl 測試
 *
 * 使用 Qualcomm adsprpc driver 的實際 ioctl 定義
 * 參考: drivers/char/adsprpc.c
 *
 * 編譯:
 * $CC -static -O2 -o adsprpc_ioctl_v2 adsprpc_ioctl_v2.c
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
#include <stdint.h>

/*
 * FASTRPC IOCTL 定義 - 來自 Qualcomm kernel source
 * Magic number 可能是 'R' (0x52) 或使用其他值
 */

#define FASTRPC_IOCTL_ALLOC_DMA_BUFF 0xc0105201
#define FASTRPC_IOCTL_FREE_DMA_BUFF 0x80045202
#define FASTRPC_IOCTL_INVOKE_ATTRS 0xc0485207
#define FASTRPC_IOCTL_INVOKE 0xc0485201
#define FASTRPC_IOCTL_INIT_ATTACH 0x80085203
#define FASTRPC_IOCTL_INIT_CREATE 0x80085204
#define FASTRPC_IOCTL_MMAP 0xc0285206
#define FASTRPC_IOCTL_MUNMAP 0xc0185207
#define FASTRPC_IOCTL_INIT_ATTACH_SNS 0x80085208
#define FASTRPC_IOCTL_MEM_MAP 0xc028520a
#define FASTRPC_IOCTL_MEM_UNMAP 0xc018520b
#define FASTRPC_IOCTL_GET_DSP_INFO 0xc00c520d
#define FASTRPC_IOCTL_CONTROL 0x80045209

/* Alternative magic - 可能是 0x52 = 'R' */
#define FASTRPC_MAGIC_R 'R'

/* IOCTL 命令構造宏 */
#define FASTRPC_IOCTL_INVOKE2_R _IOWR('R', 1, void *)
#define FASTRPC_IOCTL_MMAP_R _IOWR('R', 6, void *)
#define FASTRPC_IOCTL_MUNMAP_R _IOWR('R', 7, void *)
#define FASTRPC_IOCTL_GETINFO_R _IOWR('R', 8, uint32_t)
#define FASTRPC_IOCTL_INIT_R _IOWR('R', 3, void *)
#define FASTRPC_IOCTL_SETMODE_R _IOWR('R', 4, uint32_t)
#define FASTRPC_IOCTL_CONTROL_R _IOWR('R', 9, void *)
#define FASTRPC_IOCTL_GETPERF_R _IOWR('R', 10, void *)
#define FASTRPC_IOCTL_MEM_MAP_R _IOWR('R', 11, void *)
#define FASTRPC_IOCTL_MEM_UNMAP_R _IOWR('R', 12, void *)
#define FASTRPC_IOCTL_MUNMAP_FD_R _IOWR('R', 15, void *)
#define FASTRPC_IOCTL_GET_DSP_INFO_R _IOWR('R', 17, void *)

/* fastrpc_ctrl 結構 */
struct fastrpc_ctrl
{
    uint32_t req;
    uint32_t lp;
};

/* GETINFO 結構 */
struct smq_phy_page
{
    uint64_t addr; /* physical address */
    uint64_t size; /* size of contiguous region */
};

/* fastrpc_ioctl_init 結構 */
struct fastrpc_ioctl_init
{
    uint32_t flags;  /* one of FASTRPC_INIT_* macros */
    uint32_t file;   /* in: user address of init process ELF file */
    int32_t filelen; /* length of file */
    int32_t filefd;  /* fd of memory where init process file is stored */
    uint64_t mem;    /* in: physical address of the process region */
};

/* DSP 資訊結構 */
struct fastrpc_ioctl_dsp_info
{
    uint32_t domain;
    uint32_t attribute_ID;
    uint32_t capability;
};

void test_ioctl_numeric(int fd)
{
    int ret;
    int32_t mode = 0;
    uint32_t info = 0;
    struct fastrpc_ctrl ctrl = {0};
    struct fastrpc_ioctl_dsp_info dsp_info = {0};

    printf("\n[*] Testing IOCTL with numeric commands on fd=%d...\n", fd);

    // 測試一系列可能的 ioctl 命令
    unsigned int ioctls_to_test[] = {
        FASTRPC_IOCTL_GET_DSP_INFO, // 0xc00c520d
        FASTRPC_IOCTL_CONTROL,      // 0x80045209
        FASTRPC_IOCTL_INVOKE,       // 0xc0485201
        FASTRPC_IOCTL_INIT_ATTACH,  // 0x80085203
        FASTRPC_IOCTL_MMAP,         // 0xc0285206
        0};

    const char *names[] = {
        "GET_DSP_INFO",
        "CONTROL",
        "INVOKE",
        "INIT_ATTACH",
        "MMAP"};

    for (int i = 0; ioctls_to_test[i] != 0; i++)
    {
        char buf[128] = {0};
        errno = 0;
        ret = ioctl(fd, ioctls_to_test[i], buf);
        printf("  ioctl(0x%08x, %s): ret=%d, errno=%d (%s)\n",
               ioctls_to_test[i], names[i],
               ret, errno, ret < 0 ? strerror(errno) : "OK");
    }
}

void test_ioctl_magic_r(int fd)
{
    int ret;

    printf("\n[*] Testing IOCTL with magic 'R' on fd=%d...\n", fd);

    // 測試使用 'R' magic 的命令
    uint32_t info = 0;

    ret = ioctl(fd, FASTRPC_IOCTL_GETINFO_R, &info);
    printf("  GETINFO_R (0x%lx): ret=%d, errno=%d (%s)\n",
           (unsigned long)FASTRPC_IOCTL_GETINFO_R, ret, errno,
           ret < 0 ? strerror(errno) : "OK");

    // GET_DSP_INFO
    struct fastrpc_ioctl_dsp_info dsp = {0};
    dsp.domain = 0;
    ret = ioctl(fd, FASTRPC_IOCTL_GET_DSP_INFO_R, &dsp);
    printf("  GET_DSP_INFO_R (0x%lx): ret=%d, errno=%d (%s)\n",
           (unsigned long)FASTRPC_IOCTL_GET_DSP_INFO_R, ret, errno,
           ret < 0 ? strerror(errno) : "OK");

    // SETMODE
    uint32_t mode = 0;
    ret = ioctl(fd, FASTRPC_IOCTL_SETMODE_R, &mode);
    printf("  SETMODE_R (0x%lx): ret=%d, errno=%d (%s)\n",
           (unsigned long)FASTRPC_IOCTL_SETMODE_R, ret, errno,
           ret < 0 ? strerror(errno) : "OK");

    // INIT
    struct fastrpc_ioctl_init init = {0};
    ret = ioctl(fd, FASTRPC_IOCTL_INIT_R, &init);
    printf("  INIT_R (0x%lx): ret=%d, errno=%d (%s)\n",
           (unsigned long)FASTRPC_IOCTL_INIT_R, ret, errno,
           ret < 0 ? strerror(errno) : "OK");

    // CONTROL
    struct fastrpc_ctrl ctrl = {0};
    ret = ioctl(fd, FASTRPC_IOCTL_CONTROL_R, &ctrl);
    printf("  CONTROL_R (0x%lx): ret=%d, errno=%d (%s)\n",
           (unsigned long)FASTRPC_IOCTL_CONTROL_R, ret, errno,
           ret < 0 ? strerror(errno) : "OK");
}

void test_raw_ioctls(int fd)
{
    int ret;
    char buf[256] = {0};

    printf("\n[*] Testing raw IOCTL numbers on fd=%d...\n", fd);

    /*
     * adsprpc 驅動可能使用的 ioctl 範圍:
     * - 基於 0x52 ('R') 的命令
     * - 或者直接的數字命令
     */

    // 嘗試找到有效的 ioctl
    printf("  Scanning for valid ioctls (this may take a moment)...\n");

    int found = 0;

    // 測試 0xc00c52XX 範圍 (可能的 GET_DSP_INFO)
    for (unsigned int cmd = 0xc00c5200; cmd <= 0xc00c5220 && !found; cmd++)
    {
        errno = 0;
        ret = ioctl(fd, cmd, buf);
        if (errno != ENOTTY)
        {
            printf("    [!] ioctl(0x%08x) returned: ret=%d, errno=%d (%s)\n",
                   cmd, ret, errno, strerror(errno));
            if (errno != ENOTTY && errno != EINVAL)
                found++;
        }
    }

    // 測試 0x5200-0x5220 範圍
    for (unsigned int cmd = 0xc0005200; cmd <= 0xc0005220 && !found; cmd++)
    {
        errno = 0;
        ret = ioctl(fd, cmd, buf);
        if (errno != ENOTTY)
        {
            printf("    [!] ioctl(0x%08x) returned: ret=%d, errno=%d (%s)\n",
                   cmd, ret, errno, strerror(errno));
            if (errno != ENOTTY)
                found++;
        }
    }

    // 測試 _IOWR('R', n, ...) 格式 (0xc0xx52nn)
    for (int n = 0; n <= 20 && !found; n++)
    {
        unsigned int cmd = _IOWR('R', n, uint32_t);
        errno = 0;
        ret = ioctl(fd, cmd, buf);
        if (errno != ENOTTY)
        {
            printf("    [!] _IOWR('R', %d, uint32_t)=0x%08x returned: ret=%d, errno=%d (%s)\n",
                   n, cmd, ret, errno, strerror(errno));
            found++;
        }
    }

    if (!found)
    {
        printf("  No valid ioctls found in scanned range\n");
    }
}

int main(int argc, char **argv)
{
    int fd_ro;

    printf("=== ADSPRPC IOCTL Test v2 ===\n");
    printf("Testing with correct FASTRPC ioctl definitions\n");
    printf("==============================\n");

    // 打開設備
    printf("\n[*] Opening /dev/adsprpc-smd O_RDONLY...\n");
    fd_ro = open("/dev/adsprpc-smd", O_RDONLY);
    if (fd_ro >= 0)
    {
        printf("[+] Success: fd=%d\n", fd_ro);
    }
    else
    {
        printf("[-] Failed: %s\n", strerror(errno));
        return 1;
    }

    // 運行測試
    test_ioctl_numeric(fd_ro);
    test_ioctl_magic_r(fd_ro);
    test_raw_ioctls(fd_ro);

    // 總結
    printf("\n=== Summary ===\n");
    printf("If any ioctl returns non-ENOTTY error, we have a path forward.\n");
    printf("EBADF/EPERM = Write required, EINVAL = Wrong params\n");

    close(fd_ro);
    return 0;
}
