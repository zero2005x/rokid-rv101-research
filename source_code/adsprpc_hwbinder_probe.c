/*
 * adsprpc_hwbinder_probe.c - 探測通過 HWBinder 獲取 ADSPRPC fd 的可能性
 *
 * 目標: 測試 vendor.qti.hardware.dsp@1.0::IDspService 介面
 *
 * 編譯:
 * $CC -static -O2 -o adsprpc_probe adsprpc_hwbinder_probe.c
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <linux/un.h>
#include <dirent.h>
#include <dlfcn.h>

#include "adsprpc_probe_common.h"

// 嘗試直接打開設備
void test_direct_open(void)
{
    printf("\n[*] Testing direct open of /dev/adsprpc-smd...\n");

    int fd = -1;
    char errbuf[128];

    fd = open("/dev/adsprpc-smd", O_RDWR);
    if (fd != -1)
    {
        printf("[!] SUCCESS: Direct open worked! fd=%d\n", fd);
        printf("[!] This is unexpected - we have direct access!\n");
        close_if_open(&fd);
    }
    else
    {
        int saved_errno = errno;
        printf("[-] Direct open failed: %s (errno=%d)\n",
               probe_strerror(saved_errno, errbuf, sizeof(errbuf)), saved_errno);
    }

    fd = open("/dev/adsprpc-smd", O_RDONLY);
    if (fd != -1)
    {
        printf("[!] Read-only open worked! fd=%d\n", fd);
        close_if_open(&fd);
    }
    else
    {
        printf("[-] Read-only open failed: %s\n", probe_strerror(errno, errbuf, sizeof(errbuf)));
    }
}

// 檢查 /dev 下的 adsprpc 相關設備
void enumerate_adsprpc_devices(void)
{
    printf("\n[*] Enumerating ADSPRPC-related devices...\n");

    for (int i = 0; ADSPRPC_DEVICES[i] != NULL; i++)
    {
        struct stat st;
        if (stat(ADSPRPC_DEVICES[i], &st) == 0)
        {
            printf("  [+] %s: mode=%o, major=%d, minor=%d\n",
                   ADSPRPC_DEVICES[i], st.st_mode & 0777,
                   (int)(st.st_rdev >> 8), (int)(st.st_rdev & 0xff));
        }
    }
}

// 嘗試連接 HWBinder
void test_hwbinder_access(void)
{
    printf("\n[*] Testing HWBinder access...\n");

    for (int i = 0; ANDROID_BINDER_DEVICES[i] != NULL; i++)
    {
        char errbuf[128];
        int fd = open(ANDROID_BINDER_DEVICES[i], O_RDWR);
        if (fd != -1)
        {
            printf("[+] %s opened: fd=%d\n", ANDROID_BINDER_DEVICES[i], fd);
            close_if_open(&fd);
        }
        else
        {
            printf("[-] Cannot open %s: %s\n",
                   ANDROID_BINDER_DEVICES[i], probe_strerror(errno, errbuf, sizeof(errbuf)));
        }
    }
}

// 檢查進程的 fd 情況
void check_process_fds(pid_t pid)
{
    printf("\n[*] Checking fds of dspservice (pid=%d)...\n", pid);

    char path[256];
    char errbuf[128];
    if (probe_snprintf(path, sizeof(path), "/proc/%d/fd", pid) != 0)
    {
        fprintf(stderr, "[-] /proc fd path truncated for pid=%d\n", pid);
        return;
    }

    // 這需要 root 或相同 UID
    if (access(path, R_OK) != 0)
    {
        printf("[-] Cannot access %s: %s\n", path, probe_strerror(errno, errbuf, sizeof(errbuf)));
        return;
    }
}

// 嘗試通過 abstract socket 連接
void test_socket_access(void)
{
    printf("\n[*] Testing socket-based access paths...\n");

    // 檢查常見的 Android socket
    for (int i = 0; ADSPRPC_SOCKET_PATHS[i] != NULL; i++)
    {
        struct stat st;
        if (stat(ADSPRPC_SOCKET_PATHS[i], &st) == 0)
        {
            printf("  [+] Found: %s (mode=%o)\n", ADSPRPC_SOCKET_PATHS[i], st.st_mode & 0777);
        }
    }
}

typedef int (*getfilecon_t)(const char *, char **);
typedef void (*freecon_t)(char *);

void show_device_contexts(void)
{
    DIR *dir = opendir("/dev");
    if (!dir)
    {
        char errbuf[128];
        fprintf(stderr, "[-] Failed to open /dev: %s\n", probe_strerror(errno, errbuf, sizeof(errbuf)));
        return;
    }

    // Try to load libselinux dynamically
    void *lib = dlopen("libselinux.so", RTLD_NOW);
    getfilecon_t getfilecon_fn = NULL;
    freecon_t freecon_fn = NULL;
    if (lib)
    {
        getfilecon_fn = (getfilecon_t)dlsym(lib, "getfilecon");
        freecon_fn = (freecon_t)dlsym(lib, "freecon");
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strncmp(entry->d_name, "adsprpc", 7) == 0)
        {
            char path[512];
            if (probe_snprintf(path, sizeof(path), "/dev/%s", entry->d_name) != 0)
            {
                fprintf(stderr, "[-] Skipping truncated /dev path for %s\n", entry->d_name);
                continue;
            }
            struct stat st;
            if (stat(path, &st) == 0)
            {
                char *con = NULL;
                if (getfilecon_fn && freecon_fn)
                {
                    int rc = getfilecon_fn(path, &con);
                    if (rc >= 0 && con)
                    {
                        printf("  %s: context=%s, mode=%o\n", path, con, st.st_mode & 0777);
                    }
                    else
                    {
                        printf("  %s: mode=%o (SELinux lookup failed)\n", path, st.st_mode & 0777);
                    }

                    if (con)
                    {
                        freecon_fn(con);
                    }
                }
                else
                {
                    printf("  %s: mode=%o (SELinux lookup unavailable)\n", path, st.st_mode & 0777);
                }
            }
        }
    }
    if (lib)
    {
        dlclose(lib);
    }
    closedir(dir);
}

// 檢查 SELinux 上下文
void check_selinux_context(void)
{
    printf("\n[*] Checking SELinux context...\n");

    FILE *f = fopen("/proc/self/attr/current", "r");
    if (f)
    {
        char ctx[256];
        if (fgets(ctx, sizeof(ctx), f))
        {
            ctx[strcspn(ctx, "\n")] = '\0';
            printf("  Current context: %s\n", ctx);
        }
        else
        {
            char errbuf[128];
            fprintf(stderr, "[-] Failed to read /proc/self/attr/current: %s\n",
                    probe_strerror(errno, errbuf, sizeof(errbuf)));
        }
        fclose(f);
    }
    else
    {
        char errbuf[128];
        fprintf(stderr, "[-] Failed to open /proc/self/attr/current: %s\n",
                probe_strerror(errno, errbuf, sizeof(errbuf)));
    }

    // 檢查設備的上下文
    printf("  Device contexts:\n");
    show_device_contexts();
}

// 檢查 /vendor/etc/selinux 相關檔案
void check_vendor_selinux(void)
{
    printf("\n[*] Checking vendor SELinux policy hints...\n");

    DIR *dir = opendir("/vendor/etc/selinux");
    if (!dir)
    {
        char errbuf[128];
        printf("  [-] Cannot open /vendor/etc/selinux: %s\n", probe_strerror(errno, errbuf, sizeof(errbuf)));
        return;
    }

    struct dirent *entry;
    int matches = 0;
    while ((entry = readdir(dir)) != NULL && matches < 5)
    {
        size_t len = strlen(entry->d_name);
        if (len > 4 && strcmp(entry->d_name + len - 4, ".cil") == 0)
        {
            char filepath[512];
            if (probe_snprintf(filepath, sizeof(filepath), "/vendor/etc/selinux/%s", entry->d_name) != 0)
            {
                fprintf(stderr, "  [-] Skipping truncated SELinux path for %s\n", entry->d_name);
                continue;
            }
            FILE *f = fopen(filepath, "r");
            if (f)
            {
                char line[1024];
                int found = 0;
                while (fgets(line, sizeof(line), f))
                {
                    if (strstr(line, "adsprpc") != NULL)
                    {
                        found = 1;
                        break;
                    }
                }
                fclose(f);
                if (found)
                {
                    printf("  [+] Matches: %s\n", filepath);
                    matches++;
                }
            }
        }
    }
    closedir(dir);
}

int main(int argc, char **argv)
{
    printf("=== ADSPRPC Access Probe ===\n");
    printf("Target: CVE-2024-49848 / CVE-2024-43047\n");
    printf("Goal: Find path to get ADSPRPC fd via HWBinder\n");
    printf("Reference: Google Project Zero report\n");
    printf("============================\n");

    // 基本信息
    printf("\n[*] Process info:\n");
    printf("  UID: %d, GID: %d\n", getuid(), getgid());
    printf("  PID: %d\n", getpid());

    // 運行測試
    enumerate_adsprpc_devices();
    test_direct_open();
    test_hwbinder_access();
    check_selinux_context();
    test_socket_access();

    // 總結
    printf("\n=== Summary ===\n");
    printf("To exploit CVE-2024-49848/43047, we need:\n");
    printf("1. Access to /dev/adsprpc-smd (via HWBinder IDspService)\n");
    printf("2. Ability to call fastrpc ioctls\n");
    printf("3. Race condition or map collision trigger\n");
    printf("\nNext steps:\n");
    printf("- Develop HWBinder client to connect to IDspService\n");
    printf("- Or find alternative path through audio/media services\n");

    return 0;
}
