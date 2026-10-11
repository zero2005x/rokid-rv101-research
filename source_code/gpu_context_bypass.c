/*
 * GPU Context Creation Bypass Research
 * =====================================
 *
 * Problem: KGSL returns "legacy context switch not supported"
 * Goal: Find alternative ways to create GPU context or bypass the check
 *
 * Analysis:
 * - A6XX GPU uses GMU (GPU Management Unit)
 * - Context creation fails with all standard flags
 * - Need to find what flags/conditions the driver expects
 *
 * Potential bypasses:
 * 1. Use correct context flags for A6XX
 * 2. Manipulate GPU power state before context creation
 * 3. Use internal IOCTL commands that bypass validation
 * 4. Exploit GPUMEM operations that don't need context
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
// KGSL IOCTL Definitions
// ═══════════════════════════════════════════════════════════════════════════

#define KGSL_IOC_TYPE 0x09

/* Context flags - from kgsl.h */
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
#define KGSL_CONTEXT_PRIORITY_UNDEF 0
#define KGSL_CONTEXT_IFH_NOP 0x00010000
#define KGSL_CONTEXT_SECURE 0x00020000
#define KGSL_CONTEXT_PREEMPT_STYLE_MASK 0x0E000000
#define KGSL_CONTEXT_PREEMPT_STYLE_SHIFT 25
#define KGSL_CONTEXT_PREEMPT_STYLE_DEFAULT 0x0
#define KGSL_CONTEXT_PREEMPT_STYLE_RINGBUFFER 0x1
#define KGSL_CONTEXT_PREEMPT_STYLE_FINEGRAIN 0x2
#define KGSL_CONTEXT_TYPE_MASK 0x01F00000
#define KGSL_CONTEXT_TYPE_SHIFT 20
#define KGSL_CONTEXT_TYPE_ANY 0
#define KGSL_CONTEXT_TYPE_GL 1
#define KGSL_CONTEXT_TYPE_CL 2
#define KGSL_CONTEXT_TYPE_C2D 3
#define KGSL_CONTEXT_TYPE_RS 4
#define KGSL_CONTEXT_TYPE_VK 5
#define KGSL_CONTEXT_INVALIDATE_ON_FAULT 0x10000000

/* Calculated type values */
#define KGSL_CONTEXT_TYPE_GL_VALUE (KGSL_CONTEXT_TYPE_GL << KGSL_CONTEXT_TYPE_SHIFT)
#define KGSL_CONTEXT_TYPE_CL_VALUE (KGSL_CONTEXT_TYPE_CL << KGSL_CONTEXT_TYPE_SHIFT)
#define KGSL_CONTEXT_TYPE_C2D_VALUE (KGSL_CONTEXT_TYPE_C2D << KGSL_CONTEXT_TYPE_SHIFT)
#define KGSL_CONTEXT_TYPE_RS_VALUE (KGSL_CONTEXT_TYPE_RS << KGSL_CONTEXT_TYPE_SHIFT)
#define KGSL_CONTEXT_TYPE_VK_VALUE (KGSL_CONTEXT_TYPE_VK << KGSL_CONTEXT_TYPE_SHIFT)

/* Preempt style values */
#define KGSL_PREEMPT_DEFAULT (KGSL_CONTEXT_PREEMPT_STYLE_DEFAULT << KGSL_CONTEXT_PREEMPT_STYLE_SHIFT)
#define KGSL_PREEMPT_RINGBUFFER (KGSL_CONTEXT_PREEMPT_STYLE_RINGBUFFER << KGSL_CONTEXT_PREEMPT_STYLE_SHIFT)
#define KGSL_PREEMPT_FINEGRAIN (KGSL_CONTEXT_PREEMPT_STYLE_FINEGRAIN << KGSL_CONTEXT_PREEMPT_STYLE_SHIFT)

/* Priority values */
#define KGSL_CONTEXT_PRIORITY(x) (((x) & 0xF) << KGSL_CONTEXT_PRIORITY_SHIFT)

struct kgsl_drawctxt_create
{
    unsigned int flags;
    unsigned int drawctxt_id;
};

struct kgsl_devinfo
{
    unsigned int device_id;
    unsigned int chip_id;
    unsigned int mmu_enabled;
    unsigned long gmem_gpubaseaddr;
    unsigned int gpu_id;
    size_t gmem_sizebytes;
};

struct kgsl_device_getproperty
{
    unsigned int type;
    void *value;
    size_t sizebytes;
};

struct kgsl_gpumem_alloc_id
{
    unsigned int id;
    unsigned int flags;
    size_t size;
    size_t mmapsize;
    uint64_t gpuaddr;
};

struct kgsl_gpumem_free_id
{
    unsigned int id;
    unsigned int pad;
};

struct kgsl_gpumem_get_info
{
    uint64_t gpuaddr;
    unsigned int id;
    unsigned int flags;
    size_t size;
    size_t mmapsize;
    uint64_t useraddr;
};

struct kgsl_gpu_command
{
    uint64_t flags;
    unsigned int numcmds;
    unsigned int numobjs;
    unsigned int numsyncs;
    unsigned int context_id;
    uint64_t cmdlist;
    uint64_t objlist;
    uint64_t synclist;
    unsigned int cmdsize;
    unsigned int objsize;
    unsigned int syncsize;
    unsigned int timestamp;
};

struct kgsl_cmdbatch_profiling_buffer
{
    uint64_t wall_clock_s;
    uint64_t wall_clock_ns;
    uint64_t gpu_ticks_queued;
    uint64_t gpu_ticks_submitted;
    uint64_t gpu_ticks_retired;
};

/* Property types */
#define KGSL_PROP_DEVICE_INFO 0x1
#define KGSL_PROP_DEVICE_SHADOW 0x2
#define KGSL_PROP_DEVICE_POWER 0x3
#define KGSL_PROP_SHMEM 0x4
#define KGSL_PROP_SHMEM_APERTURES 0x5
#define KGSL_PROP_MMU_ENABLE 0x6
#define KGSL_PROP_INTERRUPT_WAITS 0x7
#define KGSL_PROP_VERSION 0x8
#define KGSL_PROP_GPU_RESET_STAT 0x9
#define KGSL_PROP_PWRCTRL 0xE
#define KGSL_PROP_PWR_CONSTRAINT 0x12
#define KGSL_PROP_UCHE_GMEM_VADDR 0x13
#define KGSL_PROP_SP_GENERIC_MEM 0x14
#define KGSL_PROP_UCODE_VERSION 0x15
#define KGSL_PROP_GPMU_VERSION 0x16
#define KGSL_PROP_HIGHEST_BANK_BIT 0x17
#define KGSL_PROP_DEVICE_BITNESS 0x18
#define KGSL_PROP_DEVICE_QDSS_STM 0x19
#define KGSL_PROP_MIN_ACCESS_LENGTH 0x1A
#define KGSL_PROP_UBWC_MODE 0x1B
#define KGSL_PROP_DEVICE_QTIMER 0x20
#define KGSL_PROP_L3_PWR_CONSTRAINT 0x22
#define KGSL_PROP_SECURE_BUFFER_ALIGNMENT 0x23
#define KGSL_PROP_SECURE_CTXT_SUPPORT 0x24
#define KGSL_PROP_SPEED_BIN 0x25
#define KGSL_PROP_GAMING_BIN 0x26
#define KGSL_PROP_QUERY_CAPABILITIES 0x27
#define KGSL_PROP_CONTEXT_PROPERTY 0x28
#define KGSL_PROP_GPU_MODEL 0x29

/* IOCTL numbers */
#define IOCTL_KGSL_DEVICE_GETPROPERTY _IOWR(KGSL_IOC_TYPE, 0x02, struct kgsl_device_getproperty)
#define IOCTL_KGSL_DEVICE_WAITTIMESTAMP _IOW(KGSL_IOC_TYPE, 0x06, struct { unsigned int ts; unsigned int timeout; })
#define IOCTL_KGSL_DRAWCTXT_CREATE _IOWR(KGSL_IOC_TYPE, 0x13, struct kgsl_drawctxt_create)
#define IOCTL_KGSL_DRAWCTXT_DESTROY _IOW(KGSL_IOC_TYPE, 0x14, struct { unsigned int drawctxt_id; })
#define IOCTL_KGSL_GPUMEM_ALLOC _IOWR(KGSL_IOC_TYPE, 0x2F, struct kgsl_gpumem_alloc_id)
#define IOCTL_KGSL_GPUMEM_FREE _IOW(KGSL_IOC_TYPE, 0x30, struct kgsl_gpumem_free_id)
#define IOCTL_KGSL_GPUMEM_GET_INFO _IOWR(KGSL_IOC_TYPE, 0x36, struct kgsl_gpumem_get_info)
#define IOCTL_KGSL_GPU_COMMAND _IOWR(KGSL_IOC_TYPE, 0x39, struct kgsl_gpu_command)
#define IOCTL_KGSL_GPUOBJ_ALLOC _IOWR(KGSL_IOC_TYPE, 0x45, struct kgsl_gpumem_alloc_id)
#define IOCTL_KGSL_GPUOBJ_FREE _IOW(KGSL_IOC_TYPE, 0x46, struct kgsl_gpumem_free_id)
#define IOCTL_KGSL_GPUOBJ_INFO _IOWR(KGSL_IOC_TYPE, 0x47, struct kgsl_gpumem_get_info)
#define IOCTL_KGSL_GPUOBJ_SYNC _IOW(KGSL_IOC_TYPE, 0x48, struct { uint64_t gpuaddr; unsigned int op; })

/* GPU memory flags */
#define KGSL_MEMALIGN_SHIFT 16
#define KGSL_MEMALIGN_MASK 0x00FF0000
#define KGSL_MEMFLAGS_GPUREADONLY 0x01000000
#define KGSL_MEMFLAGS_GPUWRITEONLY 0x02000000
#define KGSL_MEMFLAGS_IOCOHERENT 0x80000000
#define KGSL_CACHEMODE_SHIFT 26
#define KGSL_CACHEMODE_MASK 0x0C000000
#define KGSL_CACHEMODE_WRITECOMBINE 0
#define KGSL_CACHEMODE_UNCACHED 1
#define KGSL_CACHEMODE_WRITETHROUGH 2
#define KGSL_CACHEMODE_WRITEBACK 3
#define KGSL_MEMTYPE_SHIFT 8
#define KGSL_MEMTYPE_MASK 0x0000FF00
#define KGSL_MEMTYPE_OBJECTANY 0
#define KGSL_MEMTYPE_FRAMEBUFFER 1
#define KGSL_MEMTYPE_RENDERBUFFER 2
#define KGSL_MEMTYPE_ARRAYBUFFER 3
#define KGSL_MEMTYPE_ELEMENTARRAYBUFFER 4
#define KGSL_MEMTYPE_VERTEXARRAYBUFFER 5
#define KGSL_MEMTYPE_TEXTURE 6
#define KGSL_MEMTYPE_SURFACE 7
#define KGSL_MEMTYPE_EGL_SURFACE 8
#define KGSL_MEMTYPE_GL 9
#define KGSL_MEMTYPE_CL 10
#define KGSL_MEMTYPE_CL_BUFFER_MAP 11
#define KGSL_MEMTYPE_CL_BUFFER_NOMAP 12
#define KGSL_MEMTYPE_CL_IMAGE_MAP 13
#define KGSL_MEMTYPE_CL_IMAGE_NOMAP 14
#define KGSL_MEMTYPE_CL_KERNEL_STACK 15
#define KGSL_MEMTYPE_COMMAND 16
#define KGSL_MEMTYPE_2D 17
#define KGSL_MEMTYPE_EGL_IMAGE 18
#define KGSL_MEMTYPE_EGL_SHADOW 19
#define KGSL_MEMTYPE_MULTISAMPLE 20
#define KGSL_MEMTYPE_KERNEL 255

// ═══════════════════════════════════════════════════════════════════════════
// Globals
// ═══════════════════════════════════════════════════════════════════════════

static int kgsl_fd = -1;

// ═══════════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════════

void print_banner(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  GPU Context Creation Bypass Research                         ║\n");
    printf("║  ───────────────────────────────────────────────────────────  ║\n");
    printf("║  Target: Rokid AR Glasses (Adreno 621, A6XX GMU)              ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
}

const char *get_context_type_name(unsigned int flags)
{
    unsigned int type = (flags & KGSL_CONTEXT_TYPE_MASK) >> KGSL_CONTEXT_TYPE_SHIFT;
    switch (type)
    {
    case KGSL_CONTEXT_TYPE_ANY:
        return "ANY";
    case KGSL_CONTEXT_TYPE_GL:
        return "GL";
    case KGSL_CONTEXT_TYPE_CL:
        return "CL";
    case KGSL_CONTEXT_TYPE_C2D:
        return "C2D";
    case KGSL_CONTEXT_TYPE_RS:
        return "RS";
    case KGSL_CONTEXT_TYPE_VK:
        return "VK";
    default:
        return "UNKNOWN";
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Get Device Properties (for context creation hints)
// ═══════════════════════════════════════════════════════════════════════════

void get_device_properties(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" Device Properties (Context Creation Hints)\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    /* Query capabilities */
    uint64_t caps = 0;
    struct kgsl_device_getproperty prop = {
        .type = KGSL_PROP_QUERY_CAPABILITIES,
        .value = &caps,
        .sizebytes = sizeof(caps),
    };

    if (ioctl(kgsl_fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop) == 0)
    {
        printf("[+] GPU Capabilities: 0x%llx\n", (unsigned long long)caps);
        printf("    Bit 0 (Preemption): %s\n", (caps & 0x1) ? "YES" : "NO");
        printf("    Bit 1 (APRIV): %s\n", (caps & 0x2) ? "YES" : "NO");
        printf("    Bit 2 (Sync Points): %s\n", (caps & 0x4) ? "YES" : "NO");
    }

    /* Query secure context support */
    int secure = 0;
    prop.type = KGSL_PROP_SECURE_CTXT_SUPPORT;
    prop.value = &secure;
    prop.sizebytes = sizeof(secure);

    if (ioctl(kgsl_fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop) == 0)
    {
        printf("[+] Secure Context Support: %s\n", secure ? "YES" : "NO");
    }

    /* Query context property */
    uint32_t ctx_prop = 0;
    prop.type = KGSL_PROP_CONTEXT_PROPERTY;
    prop.value = &ctx_prop;
    prop.sizebytes = sizeof(ctx_prop);

    if (ioctl(kgsl_fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop) == 0)
    {
        printf("[+] Context Property: 0x%x\n", ctx_prop);
    }
    else
    {
        printf("[-] Context Property: failed (%s)\n", strerror(errno));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Comprehensive Context Flag Testing
// ═══════════════════════════════════════════════════════════════════════════

void test_context_flags_comprehensive(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" Comprehensive Context Flag Testing (A6XX GMU)\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    struct
    {
        const char *name;
        unsigned int flags;
    } tests[] = {
        /* Standard A6XX flags */
        {"A6XX Std: GL + Submit + PreCtxTS",
         KGSL_CONTEXT_TYPE_GL_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST | KGSL_CONTEXT_PER_CONTEXT_TS},
        {"A6XX Std: GL + Submit + UserTS",
         KGSL_CONTEXT_TYPE_GL_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST | KGSL_CONTEXT_USER_GENERATED_TS},
        {"A6XX Std: VK + Submit + PreCtxTS",
         KGSL_CONTEXT_TYPE_VK_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST | KGSL_CONTEXT_PER_CONTEXT_TS},

        /* With preemption styles */
        {"Preempt Default: GL + Submit",
         KGSL_CONTEXT_TYPE_GL_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST | KGSL_PREEMPT_DEFAULT},
        {"Preempt Ringbuffer: GL + Submit",
         KGSL_CONTEXT_TYPE_GL_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST | KGSL_PREEMPT_RINGBUFFER},
        {"Preempt Finegrain: GL + Submit",
         KGSL_CONTEXT_TYPE_GL_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST | KGSL_PREEMPT_FINEGRAIN},

        /* With priorities */
        {"Priority 0: GL + Submit",
         KGSL_CONTEXT_TYPE_GL_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST | KGSL_CONTEXT_PRIORITY(0)},
        {"Priority 1: GL + Submit",
         KGSL_CONTEXT_TYPE_GL_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST | KGSL_CONTEXT_PRIORITY(1)},
        {"Priority 8: GL + Submit",
         KGSL_CONTEXT_TYPE_GL_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST | KGSL_CONTEXT_PRIORITY(8)},
        {"Priority 15: GL + Submit",
         KGSL_CONTEXT_TYPE_GL_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST | KGSL_CONTEXT_PRIORITY(15)},

        /* Special flags */
        {"NoFaultTolerance: GL + Submit",
         KGSL_CONTEXT_TYPE_GL_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST | KGSL_CONTEXT_NO_FAULT_TOLERANCE},
        {"Sync: GL + Submit",
         KGSL_CONTEXT_TYPE_GL_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST | KGSL_CONTEXT_SYNC},
        {"IFH NOP: GL + Submit",
         KGSL_CONTEXT_TYPE_GL_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST | KGSL_CONTEXT_IFH_NOP},
        {"InvalidateOnFault: GL + Submit",
         KGSL_CONTEXT_TYPE_GL_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST | KGSL_CONTEXT_INVALIDATE_ON_FAULT},

        /* Context switch flags */
        {"CTX_SWITCH: GL + Submit",
         KGSL_CONTEXT_TYPE_GL_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST | KGSL_CONTEXT_CTX_SWITCH},
        {"No CTX_SWITCH: GL + Submit",
         KGSL_CONTEXT_TYPE_GL_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST},

        /* Preamble flags */
        {"Preamble: GL + Submit",
         KGSL_CONTEXT_TYPE_GL_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST | KGSL_CONTEXT_PREAMBLE},
        {"TrashState: GL + Submit",
         KGSL_CONTEXT_TYPE_GL_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST | KGSL_CONTEXT_TRASH_STATE},

        /* GMEM flags */
        {"SaveGMEM: GL + Submit",
         KGSL_CONTEXT_TYPE_GL_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST | KGSL_CONTEXT_SAVE_GMEM},
        {"NoGMEM: GL + Submit",
         KGSL_CONTEXT_TYPE_GL_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST | KGSL_CONTEXT_NO_GMEM_ALLOC},

        /* Combined A6XX typical */
        {"A6XX Typical: GL + All",
         KGSL_CONTEXT_TYPE_GL_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST |
             KGSL_CONTEXT_PER_CONTEXT_TS | KGSL_CONTEXT_NO_FAULT_TOLERANCE | KGSL_CONTEXT_PRIORITY(8)},

        /* Minimal flags */
        {"Minimal: Just GL", KGSL_CONTEXT_TYPE_GL_VALUE},
        {"Minimal: Just VK", KGSL_CONTEXT_TYPE_VK_VALUE},
        {"Minimal: Just Submit", KGSL_CONTEXT_SUBMIT_IB_LIST},
        {"Minimal: Zero flags", 0},

        /* ALL context types */
        {"Type ANY + Submit", KGSL_CONTEXT_SUBMIT_IB_LIST},
        {"Type GL + Submit", KGSL_CONTEXT_TYPE_GL_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST},
        {"Type CL + Submit", KGSL_CONTEXT_TYPE_CL_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST},
        {"Type C2D + Submit", KGSL_CONTEXT_TYPE_C2D_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST},
        {"Type RS + Submit", KGSL_CONTEXT_TYPE_RS_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST},
        {"Type VK + Submit", KGSL_CONTEXT_TYPE_VK_VALUE | KGSL_CONTEXT_SUBMIT_IB_LIST},
    };

    int success_count = 0;

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
        struct kgsl_drawctxt_create ctx = {
            .flags = tests[i].flags,
            .drawctxt_id = 0,
        };

        int ret = ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_CREATE, &ctx);

        if (ret == 0)
        {
            success_count++;
            printf("[+] SUCCESS: %s\n", tests[i].name);
            printf("    Flags: 0x%08x, ID: %u\n", tests[i].flags, ctx.drawctxt_id);

            /* Destroy the context */
            unsigned int destroy_id = ctx.drawctxt_id;
            ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_DESTROY, &destroy_id);
        }
        else
        {
            printf("[-] FAILED: %s (errno=%d: %s)\n",
                   tests[i].name, errno, strerror(errno));
        }
    }

    printf("\n[*] Total: %d/%zu successful\n", success_count, sizeof(tests) / sizeof(tests[0]));
}

// ═══════════════════════════════════════════════════════════════════════════
// Alternative: Direct GPU Command (without context)
// ═══════════════════════════════════════════════════════════════════════════

void test_direct_gpu_command(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" Alternative: Direct GPU Command (No Context)\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    /* Try GPU_COMMAND with context_id = 0 */
    struct kgsl_gpu_command cmd = {0};
    cmd.context_id = 0; /* No context */
    cmd.flags = 0;

    int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPU_COMMAND, &cmd);
    printf("[*] GPU_COMMAND (ctx=0): ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));

    /* Try with context_id = -1 */
    cmd.context_id = 0xFFFFFFFF;
    ret = ioctl(kgsl_fd, IOCTL_KGSL_GPU_COMMAND, &cmd);
    printf("[*] GPU_COMMAND (ctx=-1): ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));
}

// ═══════════════════════════════════════════════════════════════════════════
// Alternative: GPUOBJ Operations (memory corruption potential)
// ═══════════════════════════════════════════════════════════════════════════

void test_gpuobj_operations(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" Alternative: GPUOBJ Operations\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    /* Allocate GPUOBJ */
    struct kgsl_gpumem_alloc_id alloc = {
        .id = 0,
        .flags = (12 << KGSL_MEMALIGN_SHIFT) |
                 (KGSL_CACHEMODE_WRITECOMBINE << KGSL_CACHEMODE_SHIFT) |
                 (KGSL_MEMTYPE_COMMAND << KGSL_MEMTYPE_SHIFT),
        .size = 4096,
    };

    int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPUOBJ_ALLOC, &alloc);

    if (ret == 0)
    {
        printf("[+] GPUOBJ Alloc: id=%u, gpuaddr=0x%llx, mmapsize=%zu\n",
               alloc.id, (unsigned long long)alloc.gpuaddr, alloc.mmapsize);

        /* Get info */
        struct kgsl_gpumem_get_info info = {
            .id = alloc.id,
        };
        ret = ioctl(kgsl_fd, IOCTL_KGSL_GPUOBJ_INFO, &info);
        if (ret == 0)
        {
            printf("[+] GPUOBJ Info: gpuaddr=0x%llx, size=%zu, flags=0x%x\n",
                   (unsigned long long)info.gpuaddr, info.size, info.flags);
        }

        /* Free */
        struct kgsl_gpumem_free_id free_req = {
            .id = alloc.id,
        };
        ioctl(kgsl_fd, IOCTL_KGSL_GPUOBJ_FREE, &free_req);
        printf("[*] GPUOBJ freed\n");
    }
    else
    {
        printf("[-] GPUOBJ Alloc failed: %s\n", strerror(errno));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Alternative: Brute-force hidden IOCTL numbers
// ═══════════════════════════════════════════════════════════════════════════

void brute_force_ioctls(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" Brute-Force Hidden IOCTLs (0x50-0x70)\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    for (int cmd = 0x50; cmd <= 0x70; cmd++)
    {
        unsigned long ioctl_num = _IOWR(KGSL_IOC_TYPE, cmd, char[256]);
        char buf[256] = {0};

        int ret = ioctl(kgsl_fd, ioctl_num, buf);
        int err = errno;

        /* Only report interesting results */
        if (err != ENOTTY && err != EINVAL && err != 515)
        { /* 515 = ENOIOCTLCMD */
            printf("[!] KGSL 0x%02x: ret=%d, errno=%d (%s)\n",
                   cmd, ret, err, strerror(err));

            /* Hex dump first 32 bytes if successful */
            if (ret == 0)
            {
                printf("    Data: ");
                for (int i = 0; i < 32; i++)
                {
                    printf("%02x ", (unsigned char)buf[i]);
                }
                printf("\n");
            }
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

    kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        printf("[-] Failed to open KGSL: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Opened /dev/kgsl-3d0 (fd=%d)\n", kgsl_fd);

    get_device_properties();
    test_context_flags_comprehensive();
    test_direct_gpu_command();
    test_gpuobj_operations();
    brute_force_ioctls();

    close(kgsl_fd);

    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" Analysis Complete\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    return 0;
}
