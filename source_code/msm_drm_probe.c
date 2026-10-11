/*
 * MSM DRM Driver Vulnerability Probe
 * ====================================
 *
 * The MSM DRM driver (msm_drm) is world-accessible via /dev/dri/card0.
 * Multiple vulnerabilities exist in Qualcomm DRM drivers:
 * - CVE-2023-21663: SDE DRM connector buffer overflow
 * - CVE-2023-28575: Kernel buffer copy without length check
 * - Various IOCTL handling bugs
 *
 * Target: Rokid AR Glasses (Android 12, Kernel 5.10.209)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/types.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

// MSM-specific IOCTLs
#define DRM_MSM_GET_PARAM 0x00
#define DRM_MSM_GEM_NEW 0x02
#define DRM_MSM_GEM_INFO 0x03
#define DRM_MSM_GEM_CPU_PREP 0x04
#define DRM_MSM_GEM_CPU_FINI 0x05
#define DRM_MSM_GEM_SUBMIT 0x06
#define DRM_MSM_WAIT_FENCE 0x07
#define DRM_MSM_GEM_MADVISE 0x08
#define DRM_MSM_SUBMITQUEUE_NEW 0x0A
#define DRM_MSM_SUBMITQUEUE_CLOSE 0x0B
#define DRM_MSM_SUBMITQUEUE_QUERY 0x0C

#define DRM_IOCTL_MSM_GET_PARAM DRM_IOWR(DRM_COMMAND_BASE + DRM_MSM_GET_PARAM, struct drm_msm_param)
#define DRM_IOCTL_MSM_GEM_NEW DRM_IOWR(DRM_COMMAND_BASE + DRM_MSM_GEM_NEW, struct drm_msm_gem_new)
#define DRM_IOCTL_MSM_GEM_INFO DRM_IOWR(DRM_COMMAND_BASE + DRM_MSM_GEM_INFO, struct drm_msm_gem_info)

// MSM param IDs
#define MSM_PARAM_GPU_ID 0x01
#define MSM_PARAM_GMEM_SIZE 0x02
#define MSM_PARAM_CHIP_ID 0x03
#define MSM_PARAM_MAX_FREQ 0x04
#define MSM_PARAM_TIMESTAMP 0x05
#define MSM_PARAM_GMEM_BASE 0x06
#define MSM_PARAM_PP_PGTABLE 0x08
#define MSM_PARAM_FAULTS 0x09
#define MSM_PARAM_SUSPENDS 0x0a

struct drm_msm_param
{
    uint32_t pipe;
    uint32_t param;
    uint64_t value;
};

struct drm_msm_gem_new
{
    uint64_t size;
    uint32_t flags;
    uint32_t handle;
};

struct drm_msm_gem_info
{
    uint32_t handle;
    uint32_t info;
    uint64_t value;
};

#define MSM_INFO_GET_OFFSET 0x00
#define MSM_INFO_GET_IOVA 0x01
#define MSM_INFO_SET_NAME 0x02
#define MSM_INFO_GET_NAME 0x03

int drm_fd = -1;

void test_basic_ioctls(void)
{
    printf("\n[*] Testing basic DRM IOCTLs...\n");

    // Get DRM version
    struct drm_version ver = {0};
    char name[32] = {0}, date[32] = {0}, desc[128] = {0};
    ver.name = name;
    ver.name_len = sizeof(name);
    ver.date = date;
    ver.date_len = sizeof(date);
    ver.desc = desc;
    ver.desc_len = sizeof(desc);

    if (ioctl(drm_fd, DRM_IOCTL_VERSION, &ver) == 0)
    {
        printf("[+] DRM Version: %d.%d.%d\n", ver.version_major,
               ver.version_minor, ver.version_patchlevel);
        printf("    Name: %s\n", name);
        printf("    Date: %s\n", date);
        printf("    Desc: %s\n", desc);
    }
    else
    {
        printf("[-] DRM_IOCTL_VERSION failed: %s\n", strerror(errno));
    }

    // Get capabilities
    struct drm_get_cap cap = {0};
    uint64_t caps[] = {
        DRM_CAP_DUMB_BUFFER,
        DRM_CAP_PRIME,
        DRM_CAP_TIMESTAMP_MONOTONIC,
        DRM_CAP_ASYNC_PAGE_FLIP,
    };
    const char *cap_names[] = {
        "DUMB_BUFFER", "PRIME", "TIMESTAMP_MONOTONIC", "ASYNC_PAGE_FLIP"};

    printf("\n[*] DRM Capabilities:\n");
    for (int i = 0; i < 4; i++)
    {
        cap.capability = caps[i];
        cap.value = 0;
        if (ioctl(drm_fd, DRM_IOCTL_GET_CAP, &cap) == 0)
        {
            printf("    %s: %llu\n", cap_names[i], (unsigned long long)cap.value);
        }
    }
}

void test_msm_params(void)
{
    printf("\n[*] Testing MSM-specific parameters...\n");

    struct drm_msm_param param = {0};

    uint32_t params[] = {
        MSM_PARAM_GPU_ID,
        MSM_PARAM_GMEM_SIZE,
        MSM_PARAM_CHIP_ID,
        MSM_PARAM_MAX_FREQ,
        MSM_PARAM_TIMESTAMP,
        MSM_PARAM_FAULTS,
        MSM_PARAM_SUSPENDS,
    };
    const char *param_names[] = {
        "GPU_ID", "GMEM_SIZE", "CHIP_ID", "MAX_FREQ",
        "TIMESTAMP", "FAULTS", "SUSPENDS"};

    for (int i = 0; i < 7; i++)
    {
        param.pipe = 0;
        param.param = params[i];
        param.value = 0;

        if (ioctl(drm_fd, DRM_IOCTL_MSM_GET_PARAM, &param) == 0)
        {
            printf("[+] %s: 0x%llx (%llu)\n", param_names[i],
                   (unsigned long long)param.value,
                   (unsigned long long)param.value);
        }
        else
        {
            printf("[-] %s: %s\n", param_names[i], strerror(errno));
        }
    }
}

void test_gem_operations(void)
{
    printf("\n[*] Testing GEM buffer operations...\n");

    // Create a GEM buffer
    struct drm_msm_gem_new gem = {0};
    gem.size = 4096; // 1 page
    gem.flags = 0;

    if (ioctl(drm_fd, DRM_IOCTL_MSM_GEM_NEW, &gem) == 0)
    {
        printf("[+] Created GEM buffer: handle=%u, size=%llu\n",
               gem.handle, (unsigned long long)gem.size);

        // Get info
        struct drm_msm_gem_info info = {0};
        info.handle = gem.handle;
        info.info = MSM_INFO_GET_OFFSET;

        if (ioctl(drm_fd, DRM_IOCTL_MSM_GEM_INFO, &info) == 0)
        {
            printf("    Offset: 0x%llx\n", (unsigned long long)info.value);

            // Try to mmap
            void *ptr = mmap(NULL, gem.size, PROT_READ | PROT_WRITE,
                             MAP_SHARED, drm_fd, info.value);
            if (ptr != MAP_FAILED)
            {
                printf("[+] mmap succeeded at %p\n", ptr);

                // Try to read/write
                memset(ptr, 0x41, 256);
                printf("    Write test passed\n");

                munmap(ptr, gem.size);
            }
            else
            {
                printf("[-] mmap failed: %s\n", strerror(errno));
            }
        }

        // Close handle
        struct drm_gem_close close_arg = {.handle = gem.handle};
        ioctl(drm_fd, DRM_IOCTL_GEM_CLOSE, &close_arg);
    }
    else
    {
        printf("[-] GEM_NEW failed: %s\n", strerror(errno));
    }
}

void test_mode_resources(void)
{
    printf("\n[*] Testing mode resources...\n");

    struct drm_mode_card_res res = {0};

    if (ioctl(drm_fd, DRM_IOCTL_MODE_GETRESOURCES, &res) == 0)
    {
        printf("[+] Mode resources:\n");
        printf("    FBs: %u, CRTCs: %u, Connectors: %u, Encoders: %u\n",
               res.count_fbs, res.count_crtcs,
               res.count_connectors, res.count_encoders);
        printf("    Min size: %ux%u, Max size: %ux%u\n",
               res.min_width, res.min_height,
               res.max_width, res.max_height);
    }
    else
    {
        printf("[-] MODE_GETRESOURCES failed: %s\n", strerror(errno));
    }
}

void probe_ioctl_range(void)
{
    printf("\n[*] Probing MSM IOCTL range for interesting responses...\n");

    for (int cmd = 0; cmd < 0x20; cmd++)
    {
        unsigned long ioctl_nr = DRM_IOWR(DRM_COMMAND_BASE + cmd, uint64_t);
        uint64_t arg = 0;

        int ret = ioctl(drm_fd, ioctl_nr, &arg);
        if (ret == 0 || (ret < 0 && errno != ENOTTY && errno != EINVAL))
        {
            printf("    CMD 0x%02x: ret=%d errno=%d (%s)\n",
                   cmd, ret, errno, strerror(errno));
        }
    }
}

int main(void)
{
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  MSM DRM Driver Vulnerability Probe                           \n");
    printf("  Target: Rokid AR Glasses (Kernel 5.10.209)                   \n");
    printf("═══════════════════════════════════════════════════════════════\n");

    // Try both card0 and renderD128
    const char *devices[] = {"/dev/dri/card0", "/dev/dri/renderD128"};

    for (int i = 0; i < 2; i++)
    {
        printf("\n[*] Opening %s...\n", devices[i]);
        drm_fd = open(devices[i], O_RDWR);
        if (drm_fd < 0)
        {
            printf("[-] Failed: %s\n", strerror(errno));
            continue;
        }
        printf("[+] Opened successfully (fd=%d)\n", drm_fd);

        test_basic_ioctls();
        test_msm_params();
        test_gem_operations();
        test_mode_resources();
        probe_ioctl_range();

        close(drm_fd);
    }

    printf("\n[*] MSM DRM probe complete.\n");
    printf("[*] Check: dmesg | grep -iE 'drm|msm|sde'\n");

    return 0;
}
