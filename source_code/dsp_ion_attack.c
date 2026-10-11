/*
 * DSP RPC / ION / QSEE Attack Surface Exploration
 *
 * Targets:
 * - /dev/adsprpc-smd (Hexagon DSP RPC)
 * - /dev/ion (ION memory allocator)
 * - Various Qualcomm-specific devices
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <dirent.h>

// ION definitions (from ion.h)
#define ION_IOC_MAGIC 'I'
#define ION_IOC_ALLOC _IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data)
#define ION_IOC_FREE _IOWR(ION_IOC_MAGIC, 1, struct ion_handle_data)
#define ION_IOC_MAP _IOWR(ION_IOC_MAGIC, 2, struct ion_fd_data)
#define ION_IOC_SHARE _IOWR(ION_IOC_MAGIC, 4, struct ion_fd_data)
#define ION_IOC_IMPORT _IOWR(ION_IOC_MAGIC, 5, struct ion_fd_data)
#define ION_IOC_HEAP_QUERY _IOWR(ION_IOC_MAGIC, 8, struct ion_heap_query)

struct ion_allocation_data
{
    unsigned long len;
    unsigned int heap_id_mask;
    unsigned int flags;
    unsigned int fd;
    unsigned int unused;
};

struct ion_handle_data
{
    int handle;
};

struct ion_fd_data
{
    int handle;
    int fd;
};

struct ion_heap_data
{
    char name[32];
    unsigned int type;
    unsigned int heap_id;
    unsigned int reserved0;
    unsigned int reserved1;
    unsigned int reserved2;
};

struct ion_heap_query
{
    unsigned int cnt;
    unsigned int reserved0;
    unsigned long heaps;
    unsigned int reserved1;
    unsigned int reserved2;
};

// ADSPRPC definitions
#define FASTRPC_IOCTL_INVOKE _IOWR('R', 1, struct fastrpc_ioctl_invoke)
#define FASTRPC_IOCTL_MMAP _IOWR('R', 2, struct fastrpc_ioctl_mmap)
#define FASTRPC_IOCTL_MUNMAP _IOWR('R', 3, struct fastrpc_ioctl_munmap)
#define FASTRPC_IOCTL_INIT _IOWR('R', 6, struct fastrpc_ioctl_init)
#define FASTRPC_IOCTL_GETINFO _IOWR('R', 8, struct fastrpc_ioctl_getinfo)

struct fastrpc_ioctl_invoke
{
    unsigned int handle;
    unsigned int sc;
    unsigned long pra;
};

struct fastrpc_ioctl_mmap
{
    int fd;
    unsigned int flags;
    unsigned long vaddrin;
    unsigned long size;
    unsigned long vaddrout;
};

struct fastrpc_ioctl_munmap
{
    unsigned long vaddrout;
    unsigned long size;
};

struct fastrpc_ioctl_init
{
    unsigned int flags;
    unsigned long file;
    unsigned int filelen;
    int filefd;
    unsigned long mem;
    unsigned int memlen;
    int memfd;
};

struct fastrpc_ioctl_getinfo
{
    unsigned int domain;
    unsigned int info;
};

void test_device(const char *path)
{
    int fd = open(path, O_RDWR);
    if (fd >= 0)
    {
        printf("[+] %s: OPEN SUCCESS\n", path);
        close(fd);
    }
    else if (errno == EACCES)
    {
        printf("[!] %s: PERMISSION DENIED\n", path);
    }
    else
    {
        printf("[-] %s: %s\n", path, strerror(errno));
    }
}

void scan_dev_directory(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d)
        return;

    struct dirent *entry;
    char path[512];

    while ((entry = readdir(d)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;

        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);

        // Check if it's interesting
        if (strstr(entry->d_name, "ion") ||
            strstr(entry->d_name, "dsp") ||
            strstr(entry->d_name, "adsp") ||
            strstr(entry->d_name, "qsee") ||
            strstr(entry->d_name, "sec") ||
            strstr(entry->d_name, "smem") ||
            strstr(entry->d_name, "msm") ||
            strstr(entry->d_name, "qce") ||
            strstr(entry->d_name, "ipa") ||
            strstr(entry->d_name, "rmnet") ||
            strstr(entry->d_name, "diag") ||
            strstr(entry->d_name, "smd"))
        {

            int fd = open(path, O_RDWR);
            if (fd >= 0)
            {
                printf("[+] %s: WRITABLE\n", path);
                close(fd);
            }
            else
            {
                fd = open(path, O_RDONLY);
                if (fd >= 0)
                {
                    printf("[~] %s: READABLE\n", path);
                    close(fd);
                }
            }
        }
    }

    closedir(d);
}

void test_ion()
{
    printf("\n=== ION Memory Allocator Test ===\n");

    int fd = open("/dev/ion", O_RDWR);
    if (fd < 0)
    {
        printf("[-] /dev/ion: %s\n", strerror(errno));
        return;
    }
    printf("[+] /dev/ion opened\n");

    // Query heaps
    struct ion_heap_data heaps[16];
    memset(heaps, 0, sizeof(heaps));

    struct ion_heap_query query = {0};
    query.cnt = 16;
    query.heaps = (unsigned long)heaps;

    if (ioctl(fd, ION_IOC_HEAP_QUERY, &query) == 0)
    {
        printf("[+] ION heaps found: %u\n", query.cnt);
        for (unsigned int i = 0; i < query.cnt && i < 16; i++)
        {
            printf("    [%u] Name: %s, Type: %u, ID: %u\n",
                   i, heaps[i].name, heaps[i].type, heaps[i].heap_id);
        }
    }
    else
    {
        printf("[-] ION_IOC_HEAP_QUERY failed: %s\n", strerror(errno));
    }

    // Try to allocate from each heap
    printf("\n[*] Testing allocation from each heap:\n");
    for (int heap_id = 0; heap_id < 32; heap_id++)
    {
        struct ion_allocation_data alloc = {0};
        alloc.len = 4096;
        alloc.heap_id_mask = 1 << heap_id;
        alloc.flags = 0;

        if (ioctl(fd, ION_IOC_ALLOC, &alloc) == 0)
        {
            printf("[+] Heap %d: allocation SUCCESS, fd=%d\n", heap_id, alloc.fd);

            // Try to mmap
            void *map = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, alloc.fd, 0);
            if (map != MAP_FAILED)
            {
                printf("    [+] mmap SUCCESS at %p\n", map);

                // Test read/write
                *(unsigned long *)map = 0xDEADBEEFCAFEBABEULL;
                unsigned long rb = *(unsigned long *)map;
                if (rb == 0xDEADBEEFCAFEBABEULL)
                {
                    printf("    [+] Read/write SUCCESS\n");
                }

                munmap(map, 4096);
            }

            close(alloc.fd);
        }
    }

    close(fd);
}

void test_adsprpc()
{
    printf("\n=== ADSPRPC (DSP RPC) Test ===\n");

    const char *adsp_devs[] = {
        "/dev/adsprpc-smd",
        "/dev/adsprpc-smd-secure",
        "/dev/cdsp-smd",
        "/dev/sdsp-smd",
        "/dev/mdsp-smd",
        NULL};

    for (int i = 0; adsp_devs[i]; i++)
    {
        int fd = open(adsp_devs[i], O_RDWR);
        if (fd < 0)
        {
            printf("[-] %s: %s\n", adsp_devs[i], strerror(errno));
            continue;
        }
        printf("[+] %s opened\n", adsp_devs[i]);

        // Try GETINFO
        struct fastrpc_ioctl_getinfo info = {0};
        info.domain = 0;

        if (ioctl(fd, FASTRPC_IOCTL_GETINFO, &info) == 0)
        {
            printf("    [+] GETINFO: domain=%u, info=0x%x\n", info.domain, info.info);
        }
        else
        {
            printf("    [-] GETINFO failed: %s\n", strerror(errno));
        }

        // Try INIT with null params (see what happens)
        struct fastrpc_ioctl_init init = {0};
        if (ioctl(fd, FASTRPC_IOCTL_INIT, &init) == 0)
        {
            printf("    [+] INIT succeeded (no params)\n");
        }
        else
        {
            printf("    [-] INIT failed: %s (errno=%d)\n", strerror(errno), errno);
        }

        // Try MMAP
        struct fastrpc_ioctl_mmap mmap_req = {0};
        mmap_req.fd = -1;
        mmap_req.size = 4096;
        mmap_req.flags = 0;

        if (ioctl(fd, FASTRPC_IOCTL_MMAP, &mmap_req) == 0)
        {
            printf("    [+] MMAP succeeded: vaddrout=0x%lx\n", mmap_req.vaddrout);

            // Try to unmap
            struct fastrpc_ioctl_munmap munmap_req = {0};
            munmap_req.vaddrout = mmap_req.vaddrout;
            munmap_req.size = 4096;
            ioctl(fd, FASTRPC_IOCTL_MUNMAP, &munmap_req);
        }
        else
        {
            printf("    [-] MMAP failed: %s\n", strerror(errno));
        }

        close(fd);
    }
}

void test_qualcomm_devices()
{
    printf("\n=== Qualcomm Device Scan ===\n");

    const char *qc_devs[] = {
        "/dev/qseecom",
        "/dev/qce",
        "/dev/sec-nfc",
        "/dev/msm_camera",
        "/dev/msm_vidc_enc",
        "/dev/msm_vidc_dec",
        "/dev/msm_jpeg0",
        "/dev/msm_rotator",
        "/dev/smd0",
        "/dev/smd_cntl0",
        "/dev/smem_log",
        "/dev/ipa",
        "/dev/rmnet_ctrl",
        "/dev/rmnet_data",
        "/dev/diag",
        "/dev/dpl_ctrl",
        "/dev/subsys_cdsp",
        "/dev/subsys_adsp",
        "/dev/wlan0",
        "/dev/wcnss_ctrl",
        "/dev/btpower",
        NULL};

    for (int i = 0; qc_devs[i]; i++)
    {
        int fd = open(qc_devs[i], O_RDWR);
        if (fd >= 0)
        {
            printf("[+] %s: WRITABLE\n", qc_devs[i]);
            close(fd);
        }
        else if (errno == EACCES || errno == EPERM)
        {
            printf("[!] %s: PERMISSION DENIED\n", qc_devs[i]);
        }
        // Skip others silently
    }
}

void test_sysfs_bypass()
{
    printf("\n=== Sysfs/Procfs Bypass Attempts ===\n");

    // Try to read interesting kernel info
    const char *interesting_files[] = {
        "/proc/kallsyms",
        "/sys/kernel/debug/kgsl/proc/%d/mem",
        "/sys/devices/virtual/graphics/fb0/mdp/caps",
        "/sys/class/kgsl/kgsl-3d0/gpu_model",
        "/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage",
        "/sys/class/kgsl/kgsl-3d0/max_gpuclk",
        "/sys/class/kgsl/kgsl-3d0/devfreq/cur_freq",
        "/proc/device-tree/model",
        "/proc/device-tree/compatible",
        "/sys/class/power_supply/battery/capacity",
        NULL};

    char buf[256];
    for (int i = 0; interesting_files[i]; i++)
    {
        char path[256];
        snprintf(path, sizeof(path), interesting_files[i], getpid());

        int fd = open(path, O_RDONLY);
        if (fd >= 0)
        {
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            if (n > 0)
            {
                buf[n] = '\0';
                // Truncate to first line
                char *nl = strchr(buf, '\n');
                if (nl)
                    *nl = '\0';
                printf("[+] %s: %s\n", path, buf);
            }
            close(fd);
        }
    }

    // Check KGSL debugfs
    printf("\n[*] Checking KGSL debugfs:\n");
    scan_dev_directory("/sys/kernel/debug/kgsl");
}

int main()
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  DSP/ION/QSEE Attack Surface Exploration                       ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");

    // Scan interesting devices
    printf("\n=== Scanning /dev for interesting devices ===\n");
    scan_dev_directory("/dev");

    // Test ION
    test_ion();

    // Test ADSPRPC
    test_adsprpc();

    // Test other Qualcomm devices
    test_qualcomm_devices();

    // Sysfs/procfs checks
    test_sysfs_bypass();

    printf("\n=== Exploration Complete ===\n");
    return 0;
}
