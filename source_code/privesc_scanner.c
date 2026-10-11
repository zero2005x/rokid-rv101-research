/*
 * Android Privilege Escalation Scanner
 * =====================================
 * 系統化檢查各種提權向量
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <linux/ashmem.h>

static sigjmp_buf jmp_env;
static volatile sig_atomic_t sig_received = 0;

void sig_handler(int sig)
{
    sig_received = sig;
    siglongjmp(jmp_env, 1);
}

void print_section(const char *title)
{
    printf("\n╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  %-61s ║\n", title);
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
}

// ============================================================================
// 1. Kernel Information
// ============================================================================
void check_kernel_info(void)
{
    print_section("Kernel Information");

    struct utsname u;
    if (uname(&u) == 0)
    {
        printf("[*] System:  %s\n", u.sysname);
        printf("[*] Node:    %s\n", u.nodename);
        printf("[*] Release: %s\n", u.release);
        printf("[*] Version: %s\n", u.version);
        printf("[*] Machine: %s\n", u.machine);
    }

    // Check for known vulnerable kernels
    printf("\n[*] Vulnerability check:\n");

    // Dirty Pipe: 5.8 <= version < 5.16.11, 5.15.25, 5.10.102
    // This device is 5.10.209 which is patched
    printf("    - Dirty Pipe (CVE-2022-0847): ");
    if (strstr(u.release, "5.10"))
    {
        int minor = 0;
        sscanf(u.release, "5.10.%d", &minor);
        if (minor < 102)
        {
            printf("VULNERABLE (5.10.%d < 5.10.102)\n", minor);
        }
        else
        {
            printf("Patched (5.10.%d >= 5.10.102)\n", minor);
        }
    }
    else
    {
        printf("N/A\n");
    }

    // io_uring vulnerabilities
    int io_uring = open("/dev/io_uring", O_RDONLY);
    printf("    - io_uring: %s\n", io_uring >= 0 ? "Available" : "Not available");
    if (io_uring >= 0)
        close(io_uring);
}

// ============================================================================
// 2. SUID Binary Check
// ============================================================================
void check_suid_binaries(void)
{
    print_section("SUID/SGID Binary Check");

    const char *paths[] = {
        "/system/bin",
        "/system/xbin",
        "/vendor/bin",
        "/data/local/tmp",
        "/sbin",
        NULL};

    int found = 0;
    for (int i = 0; paths[i]; i++)
    {
        DIR *d = opendir(paths[i]);
        if (!d)
            continue;

        struct dirent *ent;
        while ((ent = readdir(d)))
        {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", paths[i], ent->d_name);

            struct stat st;
            if (stat(path, &st) == 0)
            {
                if (st.st_mode & S_ISUID)
                {
                    printf("[+] SUID: %s (owner=%d)\n", path, st.st_uid);
                    found++;
                }
                if (st.st_mode & S_ISGID)
                {
                    printf("[+] SGID: %s (group=%d)\n", path, st.st_gid);
                    found++;
                }
            }
        }
        closedir(d);
    }

    if (found == 0)
    {
        printf("[-] No SUID/SGID binaries found in common paths\n");
    }
}

// ============================================================================
// 3. World-Writable Device Nodes
// ============================================================================
void check_device_permissions(void)
{
    print_section("World-Writable Device Nodes");

    const char *devices[] = {
        "/dev/kgsl-3d0",
        "/dev/binder",
        "/dev/ashmem",
        "/dev/hwbinder",
        "/dev/vndbinder",
        "/dev/ion",
        "/dev/dma_heap",
        "/dev/dri/card0",
        "/dev/dri/renderD128",
        "/dev/mali0",
        "/dev/graphics/fb0",
        "/dev/video0",
        "/dev/mem",
        "/dev/kmem",
        "/dev/ptmx",
        "/dev/tty",
        NULL};

    for (int i = 0; devices[i]; i++)
    {
        struct stat st;
        if (stat(devices[i], &st) == 0)
        {
            char perms[16];
            snprintf(perms, sizeof(perms), "%c%c%c%c%c%c%c%c%c",
                     (st.st_mode & S_IRUSR) ? 'r' : '-',
                     (st.st_mode & S_IWUSR) ? 'w' : '-',
                     (st.st_mode & S_IXUSR) ? 'x' : '-',
                     (st.st_mode & S_IRGRP) ? 'r' : '-',
                     (st.st_mode & S_IWGRP) ? 'w' : '-',
                     (st.st_mode & S_IXGRP) ? 'x' : '-',
                     (st.st_mode & S_IROTH) ? 'r' : '-',
                     (st.st_mode & S_IWOTH) ? 'w' : '-',
                     (st.st_mode & S_IXOTH) ? 'x' : '-');

            int world_write = (st.st_mode & S_IWOTH);
            printf("%s %-25s %s (uid=%d gid=%d)\n",
                   world_write ? "[+]" : "[-]",
                   devices[i], perms, st.st_uid, st.st_gid);
        }
    }
}

// ============================================================================
// 4. Proc Filesystem Checks
// ============================================================================
void check_proc_access(void)
{
    print_section("Proc Filesystem Access");

    // Check kallsyms
    printf("[*] /proc/kallsyms: ");
    int fd = open("/proc/kallsyms", O_RDONLY);
    if (fd >= 0)
    {
        char buf[256];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0)
        {
            buf[n] = 0;
            // Check if addresses are visible
            if (buf[0] == '0' && buf[1] == '0' && buf[2] == '0' && buf[3] == '0')
            {
                printf("HIDDEN (addresses zeroed)\n");
            }
            else
            {
                printf("READABLE!\n");
                printf("    First symbol: %.60s...\n", buf);
            }
        }
        close(fd);
    }
    else
    {
        printf("Cannot open (errno=%d)\n", errno);
    }

    // Check other sensitive proc files
    const char *proc_files[] = {
        "/proc/config.gz",
        "/proc/modules",
        "/proc/iomem",
        "/proc/ioports",
        "/proc/slab",
        "/proc/slabinfo",
        "/proc/pagetypeinfo",
        "/proc/buddyinfo",
        NULL};

    for (int i = 0; proc_files[i]; i++)
    {
        fd = open(proc_files[i], O_RDONLY);
        printf("[%c] %s\n", fd >= 0 ? '+' : '-', proc_files[i]);
        if (fd >= 0)
            close(fd);
    }
}

// ============================================================================
// 5. Capabilities Check (Linux Capabilities)
// ============================================================================
void check_capabilities(void)
{
    print_section("Process Capabilities");

    // Read capabilities from /proc/self/status
    FILE *f = fopen("/proc/self/status", "r");
    if (!f)
    {
        printf("[-] Cannot read /proc/self/status\n");
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        if (strncmp(line, "Cap", 3) == 0)
        {
            printf("    %s", line);
        }
        if (strncmp(line, "Uid:", 4) == 0 || strncmp(line, "Gid:", 4) == 0)
        {
            printf("    %s", line);
        }
    }
    fclose(f);
}

// ============================================================================
// 6. SELinux/Seccomp Status
// ============================================================================
void check_security_status(void)
{
    print_section("Security Framework Status");

    // SELinux
    int fd = open("/sys/fs/selinux/enforce", O_RDONLY);
    if (fd >= 0)
    {
        char buf[8] = {0};
        read(fd, buf, sizeof(buf) - 1);
        printf("[*] SELinux: %s\n", buf[0] == '1' ? "Enforcing" : "Permissive/Disabled");
        close(fd);
    }
    else
    {
        printf("[*] SELinux: Not detected\n");
    }

    // SELinux context
    fd = open("/proc/self/attr/current", O_RDONLY);
    if (fd >= 0)
    {
        char buf[256] = {0};
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0)
        {
            buf[n] = 0;
            printf("[*] SELinux context: %s\n", buf);
        }
        close(fd);
    }

    // Seccomp
    FILE *f = fopen("/proc/self/status", "r");
    if (f)
    {
        char line[256];
        while (fgets(line, sizeof(line), f))
        {
            if (strncmp(line, "Seccomp:", 8) == 0)
            {
                int mode = atoi(line + 8);
                printf("[*] Seccomp: %s\n",
                       mode == 0 ? "Disabled" : mode == 1 ? "Strict"
                                                          : "Filter");
                break;
            }
        }
        fclose(f);
    }
}

// ============================================================================
// 7. Android-Specific Checks
// ============================================================================
void check_android_specific(void)
{
    print_section("Android-Specific Checks");

    // Check for run-as availability
    printf("[*] run-as binary: ");
    struct stat st;
    if (stat("/system/bin/run-as", &st) == 0)
    {
        printf("exists (mode=%o uid=%d)\n", st.st_mode & 07777, st.st_uid);
    }
    else
    {
        printf("not found\n");
    }

    // Check debuggable flag
    printf("[*] ro.debuggable: ");
    FILE *f = popen("getprop ro.debuggable 2>/dev/null", "r");
    if (f)
    {
        char buf[32] = {0};
        fgets(buf, sizeof(buf), f);
        printf("%s", buf[0] ? buf : "unknown\n");
        pclose(f);
    }
    else
    {
        printf("cannot check\n");
    }

    // Check for adbd running as root
    printf("[*] adbd root: ");
    f = popen("getprop service.adb.root 2>/dev/null", "r");
    if (f)
    {
        char buf[32] = {0};
        fgets(buf, sizeof(buf), f);
        printf("%s", buf[0] ? buf : "not set\n");
        pclose(f);
    }
    else
    {
        printf("cannot check\n");
    }

    // Check for su binary
    const char *su_paths[] = {
        "/system/bin/su",
        "/system/xbin/su",
        "/sbin/su",
        "/data/local/tmp/su",
        "/data/local/su",
        NULL};

    printf("[*] su binary: ");
    int found = 0;
    for (int i = 0; su_paths[i]; i++)
    {
        if (access(su_paths[i], F_OK) == 0)
        {
            printf("Found at %s\n", su_paths[i]);
            found = 1;
            break;
        }
    }
    if (!found)
        printf("not found\n");
}

// ============================================================================
// 8. KGSL Specific Tests
// ============================================================================
void check_kgsl_vulns(void)
{
    print_section("KGSL GPU Driver Tests");

    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
    {
        printf("[-] Cannot open KGSL\n");
        return;
    }
    printf("[+] KGSL opened: fd=%d\n", fd);

    // Device info
    struct
    {
        uint32_t device_id;
        uint32_t chip_id;
        uint32_t mmu_enabled;
        uint64_t gmem_gpubaseaddr;
        uint32_t gpu_id;
        uint32_t gmem_sizebytes;
    } devinfo = {0};

    if (ioctl(fd, 0x80200901, &devinfo) == 0)
    {
        printf("[+] GPU Info:\n");
        printf("    chip_id:     0x%x\n", devinfo.chip_id);
        printf("    gpu_id:      0x%x\n", devinfo.gpu_id);
        printf("    mmu_enabled: %d\n", devinfo.mmu_enabled);
        printf("    gmem_base:   0x%lx\n", devinfo.gmem_gpubaseaddr);
        printf("    gmem_size:   0x%x\n", devinfo.gmem_sizebytes);
    }

    // Test GPU memory allocation
    struct
    {
        uint64_t gpuaddr;
        uint64_t size;
        uint32_t flags;
        uint32_t padding;
    } alloc = {0};
    alloc.size = 0x1000;

    if (ioctl(fd, 0xc018092f, &alloc) == 0)
    {
        printf("[+] GPU alloc: addr=0x%lx size=0x%lx\n", alloc.gpuaddr, alloc.size);

        // Try to map
        uint64_t page_idx = (alloc.gpuaddr - 0x4000000000ULL) / 0x1000;
        uint64_t offset = (page_idx + 1) * 0x1000;
        void *map = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
        if (map != MAP_FAILED)
        {
            printf("[+] GPU mapped at %p (WRITE ACCESS!)\n", map);

            // Test write
            *(uint64_t *)map = 0x4242424242424242ULL;
            if (*(uint64_t *)map == 0x4242424242424242ULL)
            {
                printf("[+] GPU memory write confirmed!\n");
            }
            munmap(map, 0x1000);
        }
    }

    close(fd);
}

// ============================================================================
// 9. Check for Writable System Directories
// ============================================================================
void check_writable_dirs(void)
{
    print_section("Writable Directory Check");

    const char *dirs[] = {
        "/data/local/tmp",
        "/data/local",
        "/data",
        "/system",
        "/vendor",
        "/cache",
        "/mnt",
        "/tmp",
        NULL};

    for (int i = 0; dirs[i]; i++)
    {
        int writable = access(dirs[i], W_OK) == 0;
        printf("[%c] %s\n", writable ? '+' : '-', dirs[i]);
    }
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char **argv)
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  Android Privilege Escalation Scanner                         ║\n");
    printf("║  Target: Rokid AR Glasses                                     ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");

    printf("\n[*] UID=%d GID=%d\n", getuid(), getgid());
    printf("[*] EUID=%d EGID=%d\n", geteuid(), getegid());
    printf("[*] PID=%d\n", getpid());

    check_kernel_info();
    check_security_status();
    check_capabilities();
    check_device_permissions();
    check_proc_access();
    check_suid_binaries();
    check_android_specific();
    check_writable_dirs();
    check_kgsl_vulns();

    printf("\n=== Scan Complete ===\n");
    return 0;
}
