/*
 * adsprpc_ioctl_test.c - 測試 ADSPRPC ioctl 操作
 *
 * 發現: /dev/adsprpc-smd 可以 O_RDONLY 打開！
 * 測試: ioctl 是否需要 write 權限
 *
 * 編譯:
 * $CC -static -O2 -o adsprpc_ioctl adsprpc_ioctl_test.c
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

/* FASTRPC IOCTL 定義 (來自 kernel headers) */
#define FASTRPC_IOCTL_MAGIC 'R'

#define FASTRPC_IOCTL_INIT _IOWR(FASTRPC_IOCTL_MAGIC, 0, unsigned int)
#define FASTRPC_IOCTL_INVOKE _IOWR(FASTRPC_IOCTL_MAGIC, 1, void *)
#define FASTRPC_IOCTL_MMAP _IOWR(FASTRPC_IOCTL_MAGIC, 2, void *)
#define FASTRPC_IOCTL_MUNMAP _IOWR(FASTRPC_IOCTL_MAGIC, 3, void *)
#define FASTRPC_IOCTL_GETINFO _IOWR(FASTRPC_IOCTL_MAGIC, 7, void *)
#define FASTRPC_IOCTL_INIT_ATTRS _IOWR(FASTRPC_IOCTL_MAGIC, 8, void *)
#define FASTRPC_IOCTL_INVOKE2 _IOWR(FASTRPC_IOCTL_MAGIC, 9, void *)
#define FASTRPC_IOCTL_MEM_MAP _IOWR(FASTRPC_IOCTL_MAGIC, 11, void *)
#define FASTRPC_IOCTL_MEM_UNMAP _IOWR(FASTRPC_IOCTL_MAGIC, 12, void *)
#define FASTRPC_IOCTL_MUNMAP_FD _IOWR(FASTRPC_IOCTL_MAGIC, 15, void *)
#define FASTRPC_IOCTL_GET_DSP_INFO _IOWR(FASTRPC_IOCTL_MAGIC, 17, void *)

/* fastrpc_ioctl_init 結構 */
struct fastrpc_ioctl_init
{
    unsigned int flags;     /* one of FASTRPC_INIT_* macros */
    unsigned int file_size; /* size of the init process file */
    int file_fd;            /* fd of init process file */
    int unused;
    unsigned long long mem;
};

/* fastrpc_ioctl_init_attrs 結構 */
struct fastrpc_ioctl_init_attrs
{
    struct fastrpc_ioctl_init init;
    int attrs;
    int siglen;
};

/* DSP 資訊結構 */
struct fastrpc_ioctl_dsp_capabilities
{
    unsigned int domain;
    unsigned int attribute_ID;
    unsigned int capability;
};

/* GETINFO 結構 */
struct fastrpc_ioctl_getinfo
{
    unsigned int domain;
    unsigned int attribute_ID;
    unsigned int capability;
};

void test_ioctl_readonly(int fd)
{
    int ret;

    printf("\n[*] Testing IOCTL on read-only fd=%d...\n", fd);

    // 測試 GETINFO - 這通常是只讀操作
    struct fastrpc_ioctl_getinfo info = {0};
    info.domain = 0; // ADSP domain
    info.attribute_ID = 1;

    ret = ioctl(fd, FASTRPC_IOCTL_GETINFO, &info);
    printf("  FASTRPC_IOCTL_GETINFO: ret=%d, errno=%d (%s)\n",
           ret, errno, ret < 0 ? strerror(errno) : "OK");
    if (ret >= 0)
    {
        printf("    -> capability = 0x%x\n", info.capability);
    }

    // 測試 GET_DSP_INFO
    struct fastrpc_ioctl_dsp_capabilities dsp_cap = {0};
    dsp_cap.domain = 0;
    dsp_cap.attribute_ID = 0;

    ret = ioctl(fd, FASTRPC_IOCTL_GET_DSP_INFO, &dsp_cap);
    printf("  FASTRPC_IOCTL_GET_DSP_INFO: ret=%d, errno=%d (%s)\n",
           ret, errno, ret < 0 ? strerror(errno) : "OK");
    if (ret >= 0)
    {
        printf("    -> domain=%u, attr=%u, cap=0x%x\n",
               dsp_cap.domain, dsp_cap.attribute_ID, dsp_cap.capability);
    }

    // 測試 INIT (這需要 write 權限)
    struct fastrpc_ioctl_init_attrs init_attrs = {0};
    init_attrs.init.flags = 0;
    init_attrs.init.file_size = 0;
    init_attrs.init.file_fd = -1;
    init_attrs.attrs = 0;

    ret = ioctl(fd, FASTRPC_IOCTL_INIT_ATTRS, &init_attrs);
    printf("  FASTRPC_IOCTL_INIT_ATTRS: ret=%d, errno=%d (%s)\n",
           ret, errno, ret < 0 ? strerror(errno) : "OK");

    // 測試基本 INIT
    struct fastrpc_ioctl_init init = {0};
    init.flags = 0;

    ret = ioctl(fd, FASTRPC_IOCTL_INIT, &init);
    printf("  FASTRPC_IOCTL_INIT: ret=%d, errno=%d (%s)\n",
           ret, errno, ret < 0 ? strerror(errno) : "OK");
}

void test_ioctl_rdwr(int fd)
{
    int ret;

    printf("\n[*] Testing IOCTL on read-write fd=%d...\n", fd);

    // 測試 GETINFO
    struct fastrpc_ioctl_getinfo info = {0};
    info.domain = 0;

    ret = ioctl(fd, FASTRPC_IOCTL_GETINFO, &info);
    printf("  FASTRPC_IOCTL_GETINFO: ret=%d, errno=%d (%s)\n",
           ret, errno, ret < 0 ? strerror(errno) : "OK");

    // 測試 INIT
    struct fastrpc_ioctl_init init = {0};
    init.flags = 0;

    ret = ioctl(fd, FASTRPC_IOCTL_INIT, &init);
    printf("  FASTRPC_IOCTL_INIT: ret=%d, errno=%d (%s)\n",
           ret, errno, ret < 0 ? strerror(errno) : "OK");
}

int main(int argc, char **argv)
{
    int fd_ro, fd_rw;

    printf("=== ADSPRPC IOCTL Test ===\n");
    printf("Testing if read-only fd can perform ioctl\n");
    printf("==========================\n");

    // 嘗試 read-only 打開
    printf("\n[*] Opening /dev/adsprpc-smd O_RDONLY...\n");
    fd_ro = open("/dev/adsprpc-smd", O_RDONLY);
    if (fd_ro >= 0)
    {
        printf("[+] O_RDONLY succeeded: fd=%d\n", fd_ro);
        test_ioctl_readonly(fd_ro);
    }
    else
    {
        printf("[-] O_RDONLY failed: %s\n", strerror(errno));
    }

    // 嘗試 read-write 打開
    printf("\n[*] Opening /dev/adsprpc-smd O_RDWR...\n");
    fd_rw = open("/dev/adsprpc-smd", O_RDWR);
    if (fd_rw >= 0)
    {
        printf("[+] O_RDWR succeeded: fd=%d\n", fd_rw);
        test_ioctl_rdwr(fd_rw);
    }
    else
    {
        printf("[-] O_RDWR failed: %s (this is expected)\n", strerror(errno));
    }

    // 總結
    printf("\n=== Results ===\n");
    if (fd_ro >= 0)
    {
        printf("Read-only access: AVAILABLE\n");
        printf("If any IOCTL works, we may have partial exploitation path!\n");
    }

    if (fd_ro >= 0)
        close(fd_ro);
    if (fd_rw >= 0)
        close(fd_rw);

    return 0;
}
