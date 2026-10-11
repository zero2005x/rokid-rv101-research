/*
 * KGSL & Binder Safe Probe Tool
 * Target: Qualcomm GPU driver + Binder subsystem
 *
 * Safe probing without direct ioctl - uses sysfs and procfs
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <dirent.h>

void read_file_content(const char *path, const char *desc)
{
    int fd = open(path, O_RDONLY);
    if (fd >= 0)
    {
        char buf[4096] = {0};
        int n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0)
        {
            /* Remove trailing newline */
            while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
                buf[--n] = 0;
            printf("[+] %s: %s\n", desc, buf);
        }
        close(fd);
    }
    else
    {
        printf("[-] %s: FAILED (%s)\n", desc, strerror(errno));
    }
}

void probe_kgsl_sysfs(void)
{
    printf("\n[TEST 1] KGSL Sysfs Information\n");
    printf("================================\n");

    char *entries[] = {
        "/sys/class/kgsl/kgsl-3d0/gpu_model",
        "/sys/class/kgsl/kgsl-3d0/gpuclk",
        "/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage",
        "/sys/class/kgsl/kgsl-3d0/max_gpuclk",
        "/sys/class/kgsl/kgsl-3d0/ft_policy",
        "/sys/class/kgsl/kgsl-3d0/ft_pagefault_policy",
        "/sys/class/kgsl/kgsl-3d0/power_level",
        "/sys/class/kgsl/kgsl-3d0/throttling",
        "/sys/class/kgsl/kgsl-3d0/thermal_pwrlevel",
        "/sys/class/kgsl/kgsl-3d0/force_bus_on",
        "/sys/class/kgsl/kgsl-3d0/force_rail_on",
        "/sys/class/kgsl/kgsl-3d0/force_clk_on",
        "/sys/class/kgsl/kgsl-3d0/wake_timeout",
        "/sys/class/kgsl/kgsl-3d0/idle_timer",
        "/sys/class/kgsl/kgsl-3d0/popp",
        "/sys/class/kgsl/kgsl-3d0/gpu_clock_stats",
        NULL};

    for (int i = 0; entries[i]; i++)
    {
        int fd = open(entries[i], O_RDONLY);
        if (fd >= 0)
        {
            char buf[1024] = {0};
            int n = read(fd, buf, sizeof(buf) - 1);
            if (n > 0)
            {
                buf[n] = 0;
                while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
                    buf[--n] = 0;
                printf("[+] %s = %s\n", entries[i] + 24, buf); // Skip /sys/class/kgsl/
            }
            close(fd);
        }
    }

    /* Check for writable entries */
    printf("\n[*] Checking for writable entries...\n");
    char *writable_candidates[] = {
        "/sys/class/kgsl/kgsl-3d0/ft_policy",
        "/sys/class/kgsl/kgsl-3d0/ft_pagefault_policy",
        "/sys/class/kgsl/kgsl-3d0/force_bus_on",
        "/sys/class/kgsl/kgsl-3d0/force_rail_on",
        "/sys/class/kgsl/kgsl-3d0/force_clk_on",
        "/sys/class/kgsl/kgsl-3d0/wake_timeout",
        "/sys/class/kgsl/kgsl-3d0/idle_timer",
        "/sys/class/kgsl/kgsl-3d0/throttling",
        "/sys/class/kgsl/kgsl-3d0/popp",
        NULL};

    for (int i = 0; writable_candidates[i]; i++)
    {
        if (access(writable_candidates[i], W_OK) == 0)
        {
            printf("[!] WRITABLE: %s\n", writable_candidates[i]);
        }
    }
}

void probe_device_nodes(void)
{
    printf("\n[TEST 2] Device Node Accessibility\n");
    printf("===================================\n");

    struct
    {
        const char *path;
        const char *desc;
    } devices[] = {
        {"/dev/kgsl-3d0", "KGSL GPU"},
        {"/dev/binder", "Binder"},
        {"/dev/hwbinder", "HW Binder"},
        {"/dev/vndbinder", "Vendor Binder"},
        {"/dev/ashmem", "Ashmem"},
        {"/dev/ion", "ION"},
        {"/dev/dma_heap/system", "DMA Heap System"},
        {"/dev/dma_heap/qcom,system", "DMA Heap Qcom System"},
        {"/dev/adsprpc-smd", "ADSP RPC"},
        {"/dev/msm_audio_ion", "MSM Audio ION"},
        {"/dev/qseecom", "QSEE COM"},
        {"/dev/fastrpc-adsp", "FastRPC ADSP"},
        {"/dev/fastrpc-cdsp", "FastRPC CDSP"},
        {"/dev/ipa", "IPA"},
        {"/dev/graphics/fb0", "Framebuffer"},
        {NULL, NULL}};

    for (int i = 0; devices[i].path; i++)
    {
        int fd = open(devices[i].path, O_RDWR);
        if (fd >= 0)
        {
            printf("[+] %s: OPEN SUCCESS (fd=%d)\n", devices[i].desc, fd);
            close(fd);
        }
        else if (errno == EACCES)
        {
            printf("[-] %s: Permission denied\n", devices[i].desc);
        }
        else if (errno == ENOENT)
        {
            printf("[ ] %s: Not present\n", devices[i].desc);
        }
        else
        {
            printf("[-] %s: %s\n", devices[i].desc, strerror(errno));
        }
    }
}

void probe_debugfs(void)
{
    printf("\n[TEST 3] Debug Filesystem Access\n");
    printf("=================================\n");

    char *debug_paths[] = {
        "/sys/kernel/debug/",
        "/d/",
        "/sys/kernel/debug/kgsl/",
        "/sys/kernel/debug/binder/",
        "/sys/kernel/debug/ion/",
        "/sys/kernel/debug/dma_buf/",
        "/sys/kernel/debug/msm-bus-dbg/",
        "/sys/kernel/debug/clk/",
        "/sys/kernel/debug/regulator/",
        "/sys/kernel/debug/iommu/",
        NULL};

    for (int i = 0; debug_paths[i]; i++)
    {
        DIR *dir = opendir(debug_paths[i]);
        if (dir)
        {
            printf("[+] ACCESSIBLE: %s\n", debug_paths[i]);
            closedir(dir);
        }
        else
        {
            printf("[-] %s: %s\n", debug_paths[i], strerror(errno));
        }
    }
}

void probe_proc_interfaces(void)
{
    printf("\n[TEST 4] Proc Interfaces\n");
    printf("========================\n");

    char *proc_paths[] = {
        "/proc/gpu",
        "/proc/kgsl",
        "/proc/msm-bus",
        "/proc/iomem",
        "/proc/binder",
        NULL};

    for (int i = 0; proc_paths[i]; i++)
    {
        int fd = open(proc_paths[i], O_RDONLY);
        if (fd >= 0)
        {
            printf("[+] %s: exists\n", proc_paths[i]);
            close(fd);
        }
    }

    /* Read relevant /proc entries */
    printf("\n[*] /proc/iomem (GPU regions):\n");
    int fd = open("/proc/iomem", O_RDONLY);
    if (fd >= 0)
    {
        char buf[8192] = {0};
        int n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0)
        {
            /* Find kgsl/gpu entries */
            char *line = strtok(buf, "\n");
            while (line)
            {
                if (strstr(line, "kgsl") || strstr(line, "gpu") ||
                    strstr(line, "adreno") || strstr(line, "Graphics"))
                {
                    printf("  %s\n", line);
                }
                line = strtok(NULL, "\n");
            }
        }
        close(fd);
    }
}

void probe_binder_simple(void)
{
    printf("\n[TEST 5] Binder Simple Probe\n");
    printf("============================\n");

    int fd = open("/dev/binder", O_RDWR);
    if (fd < 0)
    {
        printf("[-] Cannot open binder: %s\n", strerror(errno));
        return;
    }
    printf("[+] Binder opened: fd=%d\n", fd);

    /* Try mmap - this should work */
    size_t map_size = 1024 * 1024;
    void *map = mmap(NULL, map_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map != MAP_FAILED)
    {
        printf("[+] Binder mmap SUCCESS: %p (size=%zu)\n", map, map_size);

        /* Scan first page for interesting values */
        unsigned long *ptr = (unsigned long *)map;
        printf("[*] First 16 qwords:\n");
        for (int i = 0; i < 16; i++)
        {
            if (ptr[i] != 0)
            {
                printf("    [%d] 0x%016lx", i, ptr[i]);
                if (ptr[i] > 0xffffff0000000000UL)
                {
                    printf(" <- KERNEL PTR?");
                }
                printf("\n");
            }
        }

        munmap(map, map_size);
    }
    else
    {
        printf("[-] Binder mmap failed: %s\n", strerror(errno));
    }

    close(fd);
}

void probe_kgsl_simple(void)
{
    printf("\n[TEST 6] KGSL Simple Probe\n");
    printf("==========================\n");

    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
    {
        printf("[-] Cannot open KGSL: %s\n", strerror(errno));
        return;
    }
    printf("[+] KGSL opened: fd=%d\n", fd);

    /* Try various mmap offsets */
    printf("[*] Attempting mmap with various offsets...\n");

    size_t sizes[] = {4096, 8192, 16384, 65536, 1048576};
    for (int s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
    {
        void *map = mmap(NULL, sizes[s], PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, 0);
        if (map != MAP_FAILED)
        {
            printf("[+] mmap(size=%zu, offset=0): SUCCESS at %p\n", sizes[s], map);
            munmap(map, sizes[s]);
        }
    }

    close(fd);
}

void probe_ashmem(void)
{
    printf("\n[TEST 7] Ashmem Probe\n");
    printf("=====================\n");

    int fd = open("/dev/ashmem", O_RDWR);
    if (fd < 0)
    {
        printf("[-] Cannot open ashmem: %s\n", strerror(errno));
        return;
    }
    printf("[+] Ashmem opened: fd=%d\n", fd);

/* Set name and size */
#define ASHMEM_SET_NAME 0x41007701
#define ASHMEM_SET_SIZE 0x40087703
#define ASHMEM_GET_SIZE 0x7704
#define ASHMEM_PIN 0x40087705
#define ASHMEM_UNPIN 0x40087706
#define ASHMEM_GET_PIN_STATUS 0x7709

    char name[] = "test_region";
    if (ioctl(fd, ASHMEM_SET_NAME, name) == 0)
    {
        printf("[+] Set name: %s\n", name);
    }

    size_t size = 0x10000;
    if (ioctl(fd, ASHMEM_SET_SIZE, size) == 0)
    {
        printf("[+] Set size: 0x%zx\n", size);
    }

    /* mmap the region */
    void *map = mmap(NULL, size, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, 0);
    if (map != MAP_FAILED)
    {
        printf("[+] mmap SUCCESS: %p\n", map);

        /* Write pattern */
        memset(map, 0x41, size);
        printf("[+] Written 0x41 pattern\n");

        /* Try pin/unpin */
        struct
        {
            unsigned int offset;
            unsigned int len;
        } pin = {0, (unsigned int)size};

        if (ioctl(fd, ASHMEM_PIN, &pin) == 0)
        {
            printf("[+] PIN succeeded\n");
        }

        if (ioctl(fd, ASHMEM_UNPIN, &pin) == 0)
        {
            printf("[+] UNPIN succeeded\n");
        }

        int status = ioctl(fd, ASHMEM_GET_PIN_STATUS);
        printf("[*] Pin status: %d\n", status);

        munmap(map, size);
    }

    close(fd);
}

void check_selinux(void)
{
    printf("\n[TEST 8] SELinux Status\n");
    printf("=======================\n");

    read_file_content("/sys/fs/selinux/enforce", "SELinux enforce");
    read_file_content("/proc/self/attr/current", "Current context");

    /* Check what we can do */
    int fd = open("/sys/fs/selinux/policy", O_RDONLY);
    if (fd >= 0)
    {
        printf("[+] Can read SELinux policy\n");
        close(fd);
    }
    else
    {
        printf("[-] Cannot read SELinux policy: %s\n", strerror(errno));
    }
}

void scan_vendor_services(void)
{
    printf("\n[TEST 9] Vendor HAL Services\n");
    printf("============================\n");

    /* Check vendor partitions */
    char *vendor_paths[] = {
        "/vendor/lib64/hw/",
        "/vendor/lib64/",
        "/vendor/bin/hw/",
        "/odm/lib64/",
        NULL};

    for (int i = 0; vendor_paths[i]; i++)
    {
        DIR *dir = opendir(vendor_paths[i]);
        if (dir)
        {
            printf("[+] Can list: %s\n", vendor_paths[i]);
            int count = 0;
            struct dirent *ent;
            while ((ent = readdir(dir)) && count < 5)
            {
                if (ent->d_name[0] != '.')
                {
                    printf("    %s\n", ent->d_name);
                    count++;
                }
            }
            if (count >= 5)
                printf("    ... (more files)\n");
            closedir(dir);
        }
    }
}

void check_kernel_config(void)
{
    printf("\n[TEST 10] Kernel Config & Build Info\n");
    printf("=====================================\n");

    read_file_content("/proc/version", "Kernel version");
    read_file_content("/proc/cmdline", "Cmdline");

    /* Check for config.gz */
    int fd = open("/proc/config.gz", O_RDONLY);
    if (fd >= 0)
    {
        printf("[+] /proc/config.gz: EXISTS (can extract kernel config!)\n");
        close(fd);
    }
    else
    {
        printf("[-] /proc/config.gz: %s\n", strerror(errno));
    }

    /* Check kallsyms */
    fd = open("/proc/kallsyms", O_RDONLY);
    if (fd >= 0)
    {
        char buf[1024] = {0};
        read(fd, buf, sizeof(buf) - 1);
        printf("[*] /proc/kallsyms sample (first entry):\n");
        char *nl = strchr(buf, '\n');
        if (nl)
            *nl = 0;
        printf("    %s\n", buf);

        if (strstr(buf, "0000000000000000"))
        {
            printf("    [!] Addresses are zeroed (kptr_restrict)\n");
        }
        else
        {
            printf("    [+] ADDRESSES VISIBLE!\n");
        }
        close(fd);
    }
}

int main(void)
{
    printf("=================================================\n");
    printf("KGSL & Binder Safe Probe Tool\n");
    printf("=================================================\n");

    probe_kgsl_sysfs();
    probe_device_nodes();
    probe_debugfs();
    probe_proc_interfaces();
    probe_binder_simple();
    probe_kgsl_simple();
    probe_ashmem();
    check_selinux();
    scan_vendor_services();
    check_kernel_config();

    printf("\n=================================================\n");
    printf("Probe Complete\n");
    printf("=================================================\n");

    return 0;
}
