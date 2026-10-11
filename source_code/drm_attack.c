/*
 * DRM/Graphics Attack Surface Exploration
 * /dev/dri/card0 and /dev/dri/renderD128 are world-writable
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/types.h>

// DRM IOCTL definitions
#define DRM_IOCTL_BASE 'd'
#define DRM_IOCTL_VERSION _IOWR(DRM_IOCTL_BASE, 0x00, struct drm_version)
#define DRM_IOCTL_GET_UNIQUE _IOWR(DRM_IOCTL_BASE, 0x01, struct drm_unique)
#define DRM_IOCTL_GET_MAGIC _IOR(DRM_IOCTL_BASE, 0x02, struct drm_auth)
#define DRM_IOCTL_GET_CAP _IOWR(DRM_IOCTL_BASE, 0x0c, struct drm_get_cap)
#define DRM_IOCTL_SET_VERSION _IOWR(DRM_IOCTL_BASE, 0x07, struct drm_set_version)
#define DRM_IOCTL_GET_CLIENT _IOWR(DRM_IOCTL_BASE, 0x05, struct drm_client)

// MSM specific IOCTLs (Qualcomm)
#define DRM_MSM_GET_PARAM 0x00
#define DRM_MSM_GEM_NEW 0x02
#define DRM_MSM_GEM_INFO 0x03
#define DRM_MSM_GEM_CPU_PREP 0x04
#define DRM_MSM_GEM_CPU_FINI 0x05
#define DRM_MSM_GEM_SUBMIT 0x06
#define DRM_MSM_WAIT_FENCE 0x07
#define DRM_MSM_GEM_MADVISE 0x08
#define DRM_MSM_SUBMITQUEUE_NEW 0x0a
#define DRM_MSM_SUBMITQUEUE_CLOSE 0x0b
#define DRM_MSM_SUBMITQUEUE_QUERY 0x0c

#define DRM_COMMAND_BASE 0x40

struct drm_version
{
    int version_major;
    int version_minor;
    int version_patchlevel;
    size_t name_len;
    char *name;
    size_t date_len;
    char *date;
    size_t desc_len;
    char *desc;
};

struct drm_unique
{
    size_t unique_len;
    char *unique;
};

struct drm_auth
{
    uint32_t magic;
};

struct drm_get_cap
{
    uint64_t capability;
    uint64_t value;
};

struct drm_set_version
{
    int drm_di_major;
    int drm_di_minor;
    int drm_dd_major;
    int drm_dd_minor;
};

struct drm_client
{
    int idx;
    int auth;
    unsigned long pid;
    unsigned long uid;
    unsigned long magic;
    unsigned long iocs;
};

// MSM structures
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

struct drm_msm_submitqueue
{
    uint32_t flags;
    uint32_t prio;
    uint32_t id;
};

static sigjmp_buf jmp_env;
static volatile sig_atomic_t sig_received = 0;

void sig_handler(int sig)
{
    sig_received = sig;
    siglongjmp(jmp_env, 1);
}

void test_drm_device(const char *path)
{
    printf("\n=== Testing %s ===\n", path);

    int fd = open(path, O_RDWR);
    if (fd < 0)
    {
        printf("[-] Cannot open: %s\n", strerror(errno));
        return;
    }
    printf("[+] Opened fd=%d\n", fd);

    // Get version
    char name[64] = {0};
    char date[64] = {0};
    char desc[128] = {0};

    struct drm_version ver = {0};
    ver.name = name;
    ver.name_len = sizeof(name);
    ver.date = date;
    ver.date_len = sizeof(date);
    ver.desc = desc;
    ver.desc_len = sizeof(desc);

    if (ioctl(fd, DRM_IOCTL_VERSION, &ver) == 0)
    {
        printf("[+] DRM Version: %d.%d.%d\n",
               ver.version_major, ver.version_minor, ver.version_patchlevel);
        printf("    Name: %s\n", name);
        printf("    Date: %s\n", date);
        printf("    Desc: %s\n", desc);
    }
    else
    {
        printf("[-] DRM_IOCTL_VERSION failed: %s\n", strerror(errno));
    }

    // Get magic (auth token)
    struct drm_auth auth = {0};
    if (ioctl(fd, DRM_IOCTL_GET_MAGIC, &auth) == 0)
    {
        printf("[+] Magic token: 0x%x\n", auth.magic);
    }

    // Try MSM specific IOCTLs
    printf("\n[*] Testing MSM (Qualcomm) IOCTLs...\n");

    // GET_PARAM
    struct drm_msm_param param = {0};
    param.pipe = 0;
    param.param = 0; // MSM_PARAM_GPU_ID

    int msm_ioctl = _IOWR(DRM_IOCTL_BASE, DRM_COMMAND_BASE + DRM_MSM_GET_PARAM,
                          struct drm_msm_param);
    if (ioctl(fd, msm_ioctl, &param) == 0)
    {
        printf("[+] MSM GPU_ID: 0x%lx\n", param.value);
    }
    else
    {
        printf("[-] MSM_GET_PARAM failed: %s\n", strerror(errno));
    }

    // Try to create GEM buffer
    struct drm_msm_gem_new gem = {0};
    gem.size = 0x1000;
    gem.flags = 0;

    int gem_ioctl = _IOWR(DRM_IOCTL_BASE, DRM_COMMAND_BASE + DRM_MSM_GEM_NEW,
                          struct drm_msm_gem_new);
    if (ioctl(fd, gem_ioctl, &gem) == 0)
    {
        printf("[+] GEM created: handle=%u size=0x%lx\n", gem.handle, gem.size);

        // Try to get GEM info
        struct drm_msm_gem_info info = {0};
        info.handle = gem.handle;
        info.info = 0; // MSM_INFO_GET_OFFSET

        int info_ioctl = _IOWR(DRM_IOCTL_BASE, DRM_COMMAND_BASE + DRM_MSM_GEM_INFO,
                               struct drm_msm_gem_info);
        if (ioctl(fd, info_ioctl, &info) == 0)
        {
            printf("[+] GEM offset: 0x%lx\n", info.value);

            // Try to mmap the GEM buffer
            void *map = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                             MAP_SHARED, fd, info.value);
            if (map != MAP_FAILED)
            {
                printf("[+] GEM mapped at %p\n", map);

                // Test write
                sig_received = 0;
                if (sigsetjmp(jmp_env, 1) == 0)
                {
                    *(uint64_t *)map = 0x4343434343434343ULL;
                    uint64_t val = *(uint64_t *)map;
                    printf("[+] GEM write test: 0x%lx\n", val);
                }
                else
                {
                    printf("[!] Signal %d during GEM access\n", sig_received);
                }

                munmap(map, 0x1000);
            }
            else
            {
                printf("[-] GEM mmap failed: %s\n", strerror(errno));
            }
        }
    }
    else
    {
        printf("[-] GEM_NEW failed: %s\n", strerror(errno));
    }

    // Create submit queue
    struct drm_msm_submitqueue sq = {0};
    sq.flags = 0;
    sq.prio = 0;

    int sq_ioctl = _IOWR(DRM_IOCTL_BASE, DRM_COMMAND_BASE + DRM_MSM_SUBMITQUEUE_NEW,
                         struct drm_msm_submitqueue);
    if (ioctl(fd, sq_ioctl, &sq) == 0)
    {
        printf("[+] Submit queue created: id=%u\n", sq.id);
    }
    else
    {
        printf("[-] SUBMITQUEUE_NEW failed: %s\n", strerror(errno));
    }

    // Enumerate IOCTLs
    printf("\n[*] Scanning DRM IOCTLs...\n");
    for (int i = 0; i < 0x20; i++)
    {
        uint8_t buf[256] = {0};
        int cmd = _IOWR(DRM_IOCTL_BASE, i, buf);
        errno = 0;
        int ret = ioctl(fd, cmd, buf);
        if (ret == 0 || (ret < 0 && errno != ENOTTY && errno != EINVAL))
        {
            printf("    [+] 0x%02x: ret=%d errno=%d\n", i, ret, errno);
        }
    }

    printf("\n[*] Scanning MSM IOCTLs (0x40+)...\n");
    for (int i = 0; i < 0x20; i++)
    {
        uint8_t buf[256] = {0};
        int cmd = _IOWR(DRM_IOCTL_BASE, DRM_COMMAND_BASE + i, buf);
        errno = 0;
        int ret = ioctl(fd, cmd, buf);
        if (ret == 0 || (ret < 0 && errno != ENOTTY && errno != EINVAL))
        {
            printf("    [+] 0x%02x: ret=%d errno=%d\n", DRM_COMMAND_BASE + i, ret, errno);
        }
    }

    close(fd);
}

// Test hwbinder and vndbinder
void test_binder_variant(const char *path)
{
    printf("\n=== Testing %s ===\n", path);

    int fd = open(path, O_RDWR);
    if (fd < 0)
    {
        printf("[-] Cannot open: %s\n", strerror(errno));
        return;
    }
    printf("[+] Opened fd=%d\n", fd);

// Get version
#define BINDER_VERSION _IOWR('b', 9, uint32_t)
    int version = 0;
    if (ioctl(fd, BINDER_VERSION, &version) == 0)
    {
        printf("[+] Binder version: %d\n", version);
    }

    // Try mmap
    void *map = mmap(NULL, 0x1000, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map != MAP_FAILED)
    {
        printf("[+] Mapped at %p\n", map);
        munmap(map, 0x1000);
    }
    else
    {
        printf("[-] mmap failed: %s\n", strerror(errno));
    }

    close(fd);
}

int main(int argc, char **argv)
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);

    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  DRM/Graphics Attack Surface Explorer                          ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");

    test_drm_device("/dev/dri/card0");
    test_drm_device("/dev/dri/renderD128");

    test_binder_variant("/dev/hwbinder");
    test_binder_variant("/dev/vndbinder");

    printf("\n=== Complete ===\n");
    return 0;
}
