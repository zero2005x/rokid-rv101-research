/*
 * KGSL/DRM Vulnerability Scanner
 * Target: Kernel 5.10.209 on Qualcomm SoC
 *
 * /dev/kgsl-3d0, /dev/dri/* are world-writable
 * Scan for known KGSL CVEs and interesting ioctl behavior
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>

/* KGSL ioctl definitions */
#define KGSL_IOC_TYPE 0x09

#define IOCTL_KGSL_DEVICE_GETPROPERTY \
    _IOWR(KGSL_IOC_TYPE, 0x02, struct kgsl_device_getproperty)
#define IOCTL_KGSL_DEVICE_WAITTIMESTAMP \
    _IOW(KGSL_IOC_TYPE, 0x06, struct kgsl_device_waittimestamp)
#define IOCTL_KGSL_RINGBUFFER_ISSUEIBCMDS \
    _IOWR(KGSL_IOC_TYPE, 0x10, struct kgsl_ringbuffer_issueibcmds)
#define IOCTL_KGSL_CMDSTREAM_READTIMESTAMP \
    _IOWR(KGSL_IOC_TYPE, 0x11, struct kgsl_cmdstream_readtimestamp)
#define IOCTL_KGSL_CMDSTREAM_FREEMEMONTIMESTAMP \
    _IOWR(KGSL_IOC_TYPE, 0x12, struct kgsl_cmdstream_freememontimestamp)
#define IOCTL_KGSL_DRAWCTXT_CREATE \
    _IOWR(KGSL_IOC_TYPE, 0x13, struct kgsl_drawctxt_create)
#define IOCTL_KGSL_DRAWCTXT_DESTROY \
    _IOWR(KGSL_IOC_TYPE, 0x14, struct kgsl_drawctxt_destroy)
#define IOCTL_KGSL_MAP_USER_MEM \
    _IOWR(KGSL_IOC_TYPE, 0x15, struct kgsl_map_user_mem)
#define IOCTL_KGSL_SHAREDMEM_FROM_PMEM \
    _IOWR(KGSL_IOC_TYPE, 0x20, struct kgsl_pmem)
#define IOCTL_KGSL_SHAREDMEM_FREE \
    _IOW(KGSL_IOC_TYPE, 0x21, struct kgsl_sharedmem_free)
#define IOCTL_KGSL_SHAREDMEM_FLUSH_CACHE \
    _IOW(KGSL_IOC_TYPE, 0x24, struct kgsl_sharedmem_free)
#define IOCTL_KGSL_GPUMEM_ALLOC \
    _IOWR(KGSL_IOC_TYPE, 0x2F, struct kgsl_gpumem_alloc)
#define IOCTL_KGSL_GPUMEM_FREE_ID \
    _IOWR(KGSL_IOC_TYPE, 0x34, struct kgsl_gpumem_free_id)
#define IOCTL_KGSL_GPUMEM_ALLOC_ID \
    _IOWR(KGSL_IOC_TYPE, 0x35, struct kgsl_gpumem_alloc_id)
#define IOCTL_KGSL_GPUMEM_GET_INFO \
    _IOWR(KGSL_IOC_TYPE, 0x36, struct kgsl_gpumem_get_info)
#define IOCTL_KGSL_GPUOBJ_ALLOC \
    _IOWR(KGSL_IOC_TYPE, 0x45, struct kgsl_gpuobj_alloc)
#define IOCTL_KGSL_GPUOBJ_FREE \
    _IOWR(KGSL_IOC_TYPE, 0x46, struct kgsl_gpuobj_free)
#define IOCTL_KGSL_GPU_COMMAND \
    _IOWR(KGSL_IOC_TYPE, 0x48, struct kgsl_gpu_command)

/* Property types */
#define KGSL_PROP_DEVICE_INFO 0x1
#define KGSL_PROP_DEVICE_SHADOW 0x2
#define KGSL_PROP_DEVICE_POWER 0x3
#define KGSL_PROP_SHMEM 0x4
#define KGSL_PROP_VERSION 0x7
#define KGSL_PROP_GPU_RESET_STAT 0x8
#define KGSL_PROP_PWRCTRL 0x9
#define KGSL_PROP_PWR_CONSTRAINT 0xB
#define KGSL_PROP_UCHE_GMEM_VADDR 0xC
#define KGSL_PROP_SP_GENERIC_MEM 0xD
#define KGSL_PROP_UCODE_VERSION 0xE
#define KGSL_PROP_GPMU_VERSION 0xF
#define KGSL_PROP_DEVICE_BITNESS 0x18

struct kgsl_device_getproperty
{
    unsigned int type;
    void *value;
    unsigned int sizebytes;
};

struct kgsl_version
{
    unsigned int drv_major;
    unsigned int drv_minor;
    unsigned int dev_major;
    unsigned int dev_minor;
};

struct kgsl_devinfo
{
    unsigned int device_id;
    unsigned int chip_id;
    unsigned int mmu_enabled;
    unsigned long gmem_gpubaseaddr;
    unsigned int gpu_id;
    unsigned int gmem_sizebytes;
};

struct kgsl_drawctxt_create
{
    unsigned int flags;
    unsigned int drawctxt_id;
};

struct kgsl_drawctxt_destroy
{
    unsigned int drawctxt_id;
};

struct kgsl_gpuobj_alloc
{
    uint64_t size;
    uint64_t flags;
    uint64_t va_len;
    uint64_t mmapsize;
    unsigned int id;
    uint64_t metadata;
    uint64_t va_alignment;
};

struct kgsl_gpuobj_free
{
    uint64_t flags;
    unsigned int id;
    unsigned int type;
    unsigned int len;
};

struct kgsl_gpumem_alloc_id
{
    unsigned int id;
    unsigned int flags;
    uint64_t size;
    uint64_t mmapsize;
    uint64_t gpuaddr;
};

struct kgsl_gpumem_free_id
{
    unsigned int id;
    unsigned int __pad;
};

struct kgsl_gpumem_get_info
{
    uint64_t gpuaddr;
    unsigned int id;
    unsigned int flags;
    uint64_t size;
    uint64_t mmapsize;
    uint64_t useraddr;
};

struct kgsl_gpumem_alloc
{
    uint64_t gpuaddr;
    uint64_t size;
    unsigned int flags;
};

struct kgsl_map_user_mem
{
    int fd;
    uint64_t gpuaddr;
    uint64_t len;
    uint64_t offset;
    uint64_t hostptr;
    unsigned int memtype;
    unsigned int flags;
};

struct kgsl_pmem
{
    int fd;
    uint64_t gpuaddr;
    uint64_t len;
    uint64_t offset;
};

struct kgsl_sharedmem_free
{
    uint64_t gpuaddr;
};

struct kgsl_device_waittimestamp
{
    unsigned int timestamp;
    unsigned int timeout;
};

struct kgsl_cmdstream_readtimestamp
{
    unsigned int type;
    unsigned int timestamp;
};

struct kgsl_cmdstream_freememontimestamp
{
    uint64_t gpuaddr;
    unsigned int type;
    unsigned int timestamp;
};

struct kgsl_ringbuffer_issueibcmds
{
    uint64_t gpuaddr;
    int numibs;
    unsigned int timestamp;
    unsigned int flags;
    unsigned int drawctxt_id;
    unsigned int cmdsize;
    unsigned int __pad;
};

struct kgsl_gpu_command
{
    uint64_t flags;
    uint64_t cmdlist;
    unsigned int numcmds;
    uint64_t objlist;
    unsigned int numobjs;
    uint64_t synclist;
    unsigned int numsyncs;
    unsigned int context_id;
    unsigned int timestamp;
};

void hexdump(const char *title, void *buf, int len)
{
    printf("%s:\n", title);
    unsigned char *p = buf;
    for (int i = 0; i < len; i += 16)
    {
        printf("  ");
        for (int j = 0; j < 16 && i + j < len; j++)
        {
            printf("%02x ", p[i + j]);
        }
        printf("\n");
    }
}

int main(void)
{
    setbuf(stdout, NULL);

    printf("=====================================================\n");
    printf("KGSL/DRM Vulnerability Scanner\n");
    printf("Target: Kernel 5.10.209 (Qualcomm)\n");
    printf("=====================================================\n\n");

    /* Try KGSL device */
    int kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        printf("[-] Failed to open /dev/kgsl-3d0: %s\n", strerror(errno));
    }
    else
    {
        printf("[+] /dev/kgsl-3d0 opened (fd=%d)\n", kgsl_fd);

        /* Get version */
        struct kgsl_version ver = {0};
        struct kgsl_device_getproperty prop = {
            .type = KGSL_PROP_VERSION,
            .value = &ver,
            .sizebytes = sizeof(ver)};

        if (ioctl(kgsl_fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop) == 0)
        {
            printf("[+] KGSL Version: drv=%u.%u dev=%u.%u\n",
                   ver.drv_major, ver.drv_minor, ver.dev_major, ver.dev_minor);
        }

        /* Get device info */
        struct kgsl_devinfo devinfo = {0};
        prop.type = KGSL_PROP_DEVICE_INFO;
        prop.value = &devinfo;
        prop.sizebytes = sizeof(devinfo);

        if (ioctl(kgsl_fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop) == 0)
        {
            printf("[+] Device ID: 0x%x, Chip ID: 0x%x\n", devinfo.device_id, devinfo.chip_id);
            printf("[+] GPU ID: 0x%x, GMEM: 0x%lx (%u bytes)\n",
                   devinfo.gpu_id, devinfo.gmem_gpubaseaddr, devinfo.gmem_sizebytes);
            printf("[+] MMU enabled: %d\n", devinfo.mmu_enabled);
        }

        /* Try to create a draw context */
        struct kgsl_drawctxt_create ctx_create = {
            .flags = 0,
            .drawctxt_id = 0};

        if (ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_CREATE, &ctx_create) == 0)
        {
            printf("[+] Created draw context: id=%u\n", ctx_create.drawctxt_id);

            /* Try to allocate GPU memory */
            struct kgsl_gpumem_alloc_id alloc = {
                .id = 0,
                .flags = 0x100000, /* KGSL_MEMALIGN_4K */
                .size = 4096,
            };

            if (ioctl(kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc) == 0)
            {
                printf("[+] Allocated GPU memory: id=%u gpuaddr=0x%lx mmapsize=%lu\n",
                       alloc.id, alloc.gpuaddr, alloc.mmapsize);

                /* Try to mmap the GPU memory */
                void *gpu_mem = mmap(NULL, alloc.mmapsize, PROT_READ | PROT_WRITE,
                                     MAP_SHARED, kgsl_fd, alloc.id * 4096);

                if (gpu_mem != MAP_FAILED)
                {
                    printf("[+] Mapped GPU memory at %p\n", gpu_mem);

                    /* Check if there's any kernel data leaked */
                    uint64_t *p = (uint64_t *)gpu_mem;
                    int kptrs = 0;
                    for (int i = 0; i < 512; i++)
                    {
                        if (p[i] != 0)
                        {
                            uint64_t val = p[i];
                            if ((val >> 48) == 0xffff || (val >> 40) == 0xffffff)
                            {
                                printf("    [%d] 0x%016lx <-- KERNEL?\n", i, val);
                                kptrs++;
                            }
                        }
                    }

                    if (kptrs > 0)
                    {
                        printf("[!] Found %d potential kernel pointers!\n", kptrs);
                    }
                    else
                    {
                        printf("[*] No kernel pointers in mapped memory (clean alloc)\n");
                    }

                    munmap(gpu_mem, alloc.mmapsize);
                }
                else
                {
                    printf("[-] mmap failed: %s\n", strerror(errno));
                }

                /* Free the GPU memory */
                struct kgsl_gpumem_free_id free_id = {.id = alloc.id};
                ioctl(kgsl_fd, IOCTL_KGSL_GPUMEM_FREE_ID, &free_id);
            }
            else
            {
                printf("[-] GPU memory alloc failed: %s\n", strerror(errno));
            }

            /* Destroy context */
            struct kgsl_drawctxt_destroy ctx_destroy = {.drawctxt_id = ctx_create.drawctxt_id};
            ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_DESTROY, &ctx_destroy);
        }
        else
        {
            printf("[-] Draw context creation failed: %s\n", strerror(errno));
        }

        /* Test timestamp reading (CVE-2016-2067 related) */
        struct kgsl_cmdstream_readtimestamp ts = {.type = 0};
        if (ioctl(kgsl_fd, IOCTL_KGSL_CMDSTREAM_READTIMESTAMP, &ts) == 0)
        {
            printf("[*] Current timestamp: %u\n", ts.timestamp);
        }

        close(kgsl_fd);
    }

    printf("\n");

    /* Try DRI device */
    int dri_fd = open("/dev/dri/renderD128", O_RDWR);
    if (dri_fd < 0)
    {
        printf("[-] Failed to open /dev/dri/renderD128: %s\n", strerror(errno));
    }
    else
    {
        printf("[+] /dev/dri/renderD128 opened (fd=%d)\n", dri_fd);

/* DRM_IOCTL_VERSION */
#define DRM_IOCTL_BASE 'd'
#define DRM_IOCTL_VERSION _IOWR(DRM_IOCTL_BASE, 0x00, struct drm_version)

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

        char name[64] = {0}, date[64] = {0}, desc[256] = {0};
        struct drm_version ver = {
            .name_len = sizeof(name) - 1,
            .name = name,
            .date_len = sizeof(date) - 1,
            .date = date,
            .desc_len = sizeof(desc) - 1,
            .desc = desc};

        if (ioctl(dri_fd, DRM_IOCTL_VERSION, &ver) == 0)
        {
            printf("[+] DRM Version: %d.%d.%d\n",
                   ver.version_major, ver.version_minor, ver.version_patchlevel);
            printf("[+] DRM Name: %s\n", name);
            printf("[+] DRM Date: %s\n", date);
        }

        close(dri_fd);
    }

    printf("\n=====================================================\n");
    printf("Scan complete\n");
    printf("=====================================================\n");

    return 0;
}
