#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <unistd.h>

#ifndef SYS_futex
#if defined(__NR_futex)
#define SYS_futex __NR_futex
#elif defined(__aarch64__)
#define SYS_futex 98
#elif defined(__x86_64__)
#define SYS_futex 202
#elif defined(__arm__) || defined(__i386__)
#define SYS_futex 240
#else
#define SYS_futex 0
#endif
#endif

#define FUTEX_WAKE 1
#define FUTEX_PRIVATE_FLAG 128

static void trim(char *text)
{
    size_t length;

    if (!text) {
        return;
    }

    length = strlen(text);
    while (length > 0 &&
           (text[length - 1] == '\n' || text[length - 1] == '\r' ||
            text[length - 1] == ' ' || text[length - 1] == '\t')) {
        text[--length] = '\0';
    }
}

static void getprop_value(const char *property, char *output, size_t output_size)
{
    char command[256];
    char buffer[256];
    FILE *pipe;

    if (!property || !output || output_size == 0) {
        return;
    }

    output[0] = '\0';
    snprintf(command, sizeof(command), "getprop %s 2>/dev/null", property);
    pipe = popen(command, "r");
    if (!pipe) {
        snprintf(output, output_size, "N/A");
        return;
    }

    if (fgets(buffer, sizeof(buffer), pipe)) {
        trim(buffer);
        snprintf(output, output_size, "%s", buffer);
    } else {
        snprintf(output, output_size, "N/A");
    }
    pclose(pipe);
}

static void print_getprop(const char *property)
{
    char value[256];

    getprop_value(property, value, sizeof(value));
    printf("[PROP] %-40s : %s\n", property, value);
}

static int kallsym_exact(const char *symbol)
{
    FILE *file;
    char line[512];
    char address[64];
    char type[16];
    char name[256];
    int found = 0;

    file = fopen("/proc/kallsyms", "r");
    if (!file) {
        return -1;
    }

    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "%63s %15s %255s", address, type, name) == 3 &&
            strcmp(name, symbol) == 0) {
            found = 1;
            break;
        }
    }

    fclose(file);
    return found;
}

static void print_symbol(const char *symbol)
{
    int result = kallsym_exact(symbol);

    if (result == 1) {
        printf("[SYM ] %-40s : present\n", symbol);
    } else if (result == 0) {
        printf("[SYM ] %-40s : not found\n", symbol);
    } else {
        printf("[SYM ] %-40s : cannot read /proc/kallsyms\n", symbol);
    }
}

static int test_safe_futex_wake(void)
{
    unsigned int futex_word = 0;
    long result;

    if (SYS_futex == 0) {
        errno = ENOSYS;
        return -1;
    }

    result = syscall(SYS_futex, &futex_word,
                     FUTEX_WAKE | FUTEX_PRIVATE_FLAG,
                     1, NULL, NULL, 0);
    return result < 0 ? -1 : (int)result;
}

static int version_cmp(int major, int minor, int patch,
                       int target_major, int target_minor, int target_patch)
{
    if (major != target_major) {
        return major - target_major;
    }
    if (minor != target_minor) {
        return minor - target_minor;
    }
    return patch - target_patch;
}

int main(void)
{
    struct utsname system_info;
    int major = 0;
    int minor = 0;
    int patch = 0;
    int parsed;
    int in_affected_range = 0;
    int likely_patched = 0;
    int security_patch_year = 0;
    int futex_result;
    char security_patch[256];

    printf("==================================================\n");
    printf("CVE-2026-43499 safe risk detector\n");
    printf("Non-destructive: does NOT trigger the vulnerability\n");
    printf("==================================================\n\n");

    if (uname(&system_info) != 0) {
        perror("uname");
        return 2;
    }

    printf("[INFO] kernel release : %s\n", system_info.release);
    printf("[INFO] kernel version : %s\n", system_info.version);
    printf("[INFO] machine        : %s\n", system_info.machine);
    printf("[INFO] sysname        : %s\n\n", system_info.sysname);

    parsed = sscanf(system_info.release, "%d.%d.%d", &major, &minor, &patch);
    if (parsed < 2) {
        printf("[WARN] Cannot parse kernel release: %s\n", system_info.release);
        printf("[RESULT] UNKNOWN\n");
        return 2;
    }
    if (parsed == 2) {
        patch = 0;
    }

    printf("[INFO] parsed version : %d.%d.%d\n\n", major, minor, patch);
    print_getprop("ro.build.version.security_patch");
    print_getprop("ro.vendor.build.security_patch");
    print_getprop("ro.build.fingerprint");
    print_getprop("ro.product.model");
    print_getprop("ro.build.version.release");
    printf("\n");

    getprop_value("ro.build.version.security_patch", security_patch,
                  sizeof(security_patch));
    if (sscanf(security_patch, "%d", &security_patch_year) == 1 &&
        security_patch_year > 0 && security_patch_year < 2026) {
        printf("[WARN] Android security patch year is %d, older than the CVE timeline.\n",
               security_patch_year);
        printf("[WARN] Vendor confirmation is required to establish patch status.\n\n");
    }

    print_symbol("remove_waiter");
    print_symbol("rt_mutex_start_proxy_lock");
    print_symbol("__rt_mutex_start_proxy_lock");
    print_symbol("futex_requeue");
    printf("\n");

    futex_result = test_safe_futex_wake();
    if (futex_result >= 0) {
        printf("[SYS ] FUTEX_WAKE test                    : OK, woken=%d\n",
               futex_result);
    } else {
        printf("[SYS ] FUTEX_WAKE test                    : FAILED, errno=%d (%s)\n",
               errno, strerror(errno));
    }
    printf("\n");

    if (version_cmp(major, minor, patch, 2, 6, 39) >= 0 &&
        version_cmp(major, minor, patch, 7, 0, 4) < 0) {
        in_affected_range = 1;
    }

    if (version_cmp(major, minor, patch, 7, 0, 4) >= 0 ||
        (major == 6 && minor == 1 && patch >= 175) ||
        (major == 6 && minor == 6 && patch >= 140) ||
        (major == 6 && minor == 12 && patch >= 86) ||
        (major == 6 && minor == 18 && patch >= 27)) {
        likely_patched = 1;
    }

    printf("==================================================\n");
    if (!in_affected_range && !likely_patched) {
        printf("[RESULT] NOT_IN_KNOWN_AFFECTED_RANGE\n");
        printf("Kernel version is outside the stated affected range.\n");
        printf("==================================================\n");
        return 0;
    }

    if (likely_patched) {
        printf("[RESULT] LIKELY_PATCHED_BY_VERSION\n");
        printf("Vendor backports and build metadata may still matter.\n");
        printf("==================================================\n");
        return 0;
    }

    printf("[RESULT] POSSIBLY_VULNERABLE_NEED_VENDOR_CONFIRMATION\n\n");
    printf("This does NOT prove that the vulnerability exists.\n");
    printf("OEM/SoC vendors may backport fixes without changing the kernel version.\n");
    printf("==================================================\n");
    return 1;
}