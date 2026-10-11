/*
 * cve_2024_43047_verify.c - defensive FastRPC exposure verifier
 *
 * This tool does not attempt exploitation. It avoids MMAP, MUNMAP, INVOKE,
 * INIT, heap spraying, or any UAF trigger path. It only checks firmware
 * patch properties, device-node accessibility, and low-risk informational
 * ioctls that were already used by the local probe set.
 *
 * Build:
 *   aarch64-linux-android31-clang -static -O2 -Wall -Wextra \
 *     -o cve_2024_43047_verify cve_2024_43047_verify.c
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/system_properties.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#define IOCTL_GETINFO 0xc0045208U
#define IOCTL_GET_DSP_INFO 0xc00c5211U

struct fastrpc_ioctl_dsp_info {
    uint32_t domain;
    uint32_t attribute_ID;
    uint32_t capability;
};

static void print_prop(const char *name)
{
    char value[PROP_VALUE_MAX];
    int len = __system_property_get(name, value);

    if (len <= 0) {
        printf("[prop] %-40s <unset>\n", name);
        return;
    }

    printf("[prop] %-40s %s\n", name, value);
}

static void print_stat(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0) {
        printf("[node] %-24s missing (%s)\n", path, strerror(errno));
        return;
    }

    printf("[node] %-24s mode=%04o uid=%ld gid=%ld major=%u minor=%u\n",
           path,
           (unsigned)(st.st_mode & 07777),
           (long)st.st_uid,
           (long)st.st_gid,
           (unsigned)((st.st_rdev >> 8) & 0xfff),
           (unsigned)(st.st_rdev & 0xff));
}

static int try_open(const char *path, int flags, const char *label)
{
    int fd = open(path, flags | O_CLOEXEC);

    if (fd < 0) {
        printf("[open] %-24s %-8s FAIL errno=%d (%s)\n",
               path, label, errno, strerror(errno));
        return -1;
    }

    printf("[open] %-24s %-8s OK fd=%d\n", path, label, fd);
    return fd;
}

static void probe_info_ioctls(const char *path)
{
    int fd = try_open(path, O_RDONLY, "O_RDONLY");
    uint32_t info = 0;
    int ret;

    if (fd < 0)
        return;

    errno = 0;
    ret = ioctl(fd, IOCTL_GETINFO, &info);
    printf("[ioctl] %-24s GETINFO      ret=%d errno=%d (%s) info=0x%x\n",
           path, ret, errno, ret == 0 ? "OK" : strerror(errno), info);

    for (uint32_t domain = 0; domain <= 3; domain++) {
        struct fastrpc_ioctl_dsp_info dsp_info;
        memset(&dsp_info, 0, sizeof(dsp_info));
        dsp_info.domain = domain;
        dsp_info.attribute_ID = 0;

        errno = 0;
        ret = ioctl(fd, IOCTL_GET_DSP_INFO, &dsp_info);
        printf("[ioctl] %-24s DSP_INFO d=%u ret=%d errno=%d (%s) cap=0x%x\n",
               path,
               domain,
               ret,
               errno,
               ret == 0 ? "OK" : strerror(errno),
               dsp_info.capability);
    }

    close(fd);
}

static void check_rw_access(const char *path)
{
    int fd = try_open(path, O_RDWR, "O_RDWR");
    if (fd >= 0)
        close(fd);
}

int main(void)
{
    const char *nodes[] = {
        "/dev/adsprpc-smd",
        "/dev/adsprpc-smd-secure",
        "/dev/cdsprpc-smd",
        "/dev/mdsprpc-smd",
        "/dev/fastrpc-adsp",
        "/dev/fastrpc-cdsp",
        NULL,
    };
    struct utsname uts;

    printf("=== CVE-2024-43047 Defensive Verifier ===\n");
    printf("Mode: non-exploit exposure and patch-baseline check\n\n");

    if (uname(&uts) == 0)
        printf("[kernel] %s %s %s %s\n", uts.sysname, uts.release, uts.version, uts.machine);

    print_prop("ro.build.version.security_patch");
    print_prop("ro.vendor.build.security_patch");
    print_prop("ro.build.fingerprint");
    print_prop("ro.vendor.build.fingerprint");

    printf("\n--- FastRPC device nodes ---\n");
    for (int i = 0; nodes[i]; i++)
        print_stat(nodes[i]);

    printf("\n--- Accessibility ---\n");
    for (int i = 0; nodes[i]; i++) {
        int fd = try_open(nodes[i], O_RDONLY, "O_RDONLY");
        if (fd >= 0)
            close(fd);
        check_rw_access(nodes[i]);
    }

    printf("\n--- Informational ioctl probe ---\n");
    probe_info_ioctls("/dev/adsprpc-smd");
    probe_info_ioctls("/dev/adsprpc-smd-secure");

    printf("\n--- Interpretation ---\n");
    printf("If security_patch is earlier than 2024-11-05, the Android ASB baseline does not include the CVE-2024-43047 fix.\n");
    printf("Successful device-node access and informational ioctls confirm reachable attack surface, not exploitation.\n");

    return 0;
}