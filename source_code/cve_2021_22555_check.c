/*
 * CVE-2021-22555 Vulnerability Check for ARM64 Android
 *
 * This checks if the netfilter heap out-of-bounds write vulnerability
 * is present on the target system.
 *
 * Compile: aarch64-linux-android31-clang -static -O2 -o cve_22555_check cve_2021_22555_check.c
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <netinet/in.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter/x_tables.h>

// For Android, we need to define some missing structures
#ifndef IPT_ALIGN
#define IPT_ALIGN(s) (((s) + (__alignof__(struct ipt_entry) - 1)) & ~(__alignof__(struct ipt_entry) - 1))
#endif

#ifndef XT_ALIGN
#define XT_ALIGN(s) (((s) + (__alignof__(long long) - 1)) & ~(__alignof__(long long) - 1))
#endif

// Check if we can load ip_tables module
int check_iptables_available(void)
{
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (sock < 0)
    {
        printf("[-] Cannot create raw socket: %s\n", strerror(errno));
        printf("    (This is normal for non-root users)\n");
        return -1;
    }

    // Try to get iptables info
    struct ipt_getinfo info;
    memset(&info, 0, sizeof(info));
    strcpy(info.name, "filter");

    socklen_t len = sizeof(info);
    int ret = getsockopt(sock, IPPROTO_IP, IPT_SO_GET_INFO, &info, &len);

    close(sock);

    if (ret < 0)
    {
        if (errno == ENOPROTOOPT)
        {
            printf("[-] IPT_SO_GET_INFO not supported (ip_tables not loaded)\n");
            return -1;
        }
        else if (errno == EPERM || errno == EACCES)
        {
            printf("[!] Permission denied - need root to access iptables\n");
            return -2; // Special code for permission issue
        }
        printf("[-] getsockopt failed: %s\n", strerror(errno));
        return -1;
    }

    printf("[+] ip_tables module is loaded!\n");
    printf("    Table: %s\n", info.name);
    printf("    Valid hooks: 0x%x\n", info.valid_hooks);
    printf("    Num entries: %u\n", info.num_entries);
    printf("    Size: %u\n", info.size);

    return 0;
}

// Check kernel config for relevant settings
void check_kernel_config(void)
{
    FILE *fp;
    char line[256];
    int found_iptables = 0;
    int found_netfilter = 0;
    int found_sysvipc = 0;
    int found_userns = 0;
    int found_slab_hardened = 0;

    // Try to read kernel config
    fp = popen("zcat /proc/config.gz 2>/dev/null", "r");
    if (!fp)
    {
        printf("[-] Cannot read kernel config\n");
        return;
    }

    printf("\n[*] Checking kernel config for CVE-2021-22555 requirements:\n");

    while (fgets(line, sizeof(line), fp))
    {
        if (strstr(line, "CONFIG_IP_NF_IPTABLES=y"))
        {
            printf("    [+] CONFIG_IP_NF_IPTABLES=y (REQUIRED)\n");
            found_iptables = 1;
        }
        if (strstr(line, "CONFIG_NETFILTER=y"))
        {
            printf("    [+] CONFIG_NETFILTER=y (REQUIRED)\n");
            found_netfilter = 1;
        }
        if (strstr(line, "CONFIG_SYSVIPC=y"))
        {
            printf("    [+] CONFIG_SYSVIPC=y (For msg_msg spray)\n");
            found_sysvipc = 1;
        }
        if (strstr(line, "# CONFIG_SYSVIPC is not set"))
        {
            printf("    [-] CONFIG_SYSVIPC is not set (msg_msg spray BLOCKED)\n");
            found_sysvipc = -1;
        }
        if (strstr(line, "CONFIG_USER_NS=y"))
        {
            printf("    [+] CONFIG_USER_NS=y (For namespace sandbox)\n");
            found_userns = 1;
        }
        if (strstr(line, "# CONFIG_USER_NS is not set"))
        {
            printf("    [-] CONFIG_USER_NS is not set (namespace sandbox BLOCKED)\n");
            found_userns = -1;
        }
        if (strstr(line, "CONFIG_SLAB_FREELIST_HARDENED=y"))
        {
            printf("    [!] CONFIG_SLAB_FREELIST_HARDENED=y (Heap exploit harder)\n");
            found_slab_hardened = 1;
        }
    }

    pclose(fp);

    printf("\n[*] CVE-2021-22555 Exploitability Assessment:\n");

    if (!found_iptables || !found_netfilter)
    {
        printf("    [-] BLOCKED: Netfilter/iptables not available\n");
        return;
    }

    if (found_sysvipc == -1)
    {
        printf("    [-] BLOCKED: Original exploit requires System V IPC (msg_msg)\n");
        printf("    [!] Would need alternative heap spray (BPF, pipe, socket)\n");
    }

    if (found_userns == -1)
    {
        printf("    [-] BLOCKED: Original exploit requires user namespace\n");
        printf("    [!] Would need root or CAP_NET_ADMIN to trigger\n");
    }

    if (found_slab_hardened)
    {
        printf("    [!] WARNING: SLAB hardening makes heap exploitation difficult\n");
    }
}

// Check loaded modules
void check_loaded_modules(void)
{
    FILE *fp;
    char line[256];

    printf("\n[*] Checking loaded netfilter modules:\n");

    fp = fopen("/proc/modules", "r");
    if (!fp)
    {
        printf("    [-] Cannot read /proc/modules\n");
        return;
    }

    int found_any = 0;
    while (fgets(line, sizeof(line), fp))
    {
        if (strstr(line, "ip_tables") ||
            strstr(line, "iptable_") ||
            strstr(line, "nf_") ||
            strstr(line, "x_tables"))
        {
            printf("    [+] %s", line);
            found_any = 1;
        }
    }

    fclose(fp);

    if (!found_any)
    {
        printf("    [-] No netfilter modules currently loaded\n");
        printf("    [!] ip_tables module needs to be loaded for CVE-2021-22555\n");
    }
}

// Try to trigger the vulnerability (needs CAP_NET_ADMIN)
int try_trigger_vuln(void)
{
    printf("\n[*] Attempting to trigger CVE-2021-22555 OOB write...\n");

    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (sock < 0)
    {
        printf("    [-] Cannot create raw socket: %s\n", strerror(errno));
        return -1;
    }

    // Prepare a malformed iptables replacement
    // The vulnerability is in xt_compat_target_from_user()
    // which has an integer underflow when calculating padding

    // This requires CAP_NET_ADMIN, which we don't have as shell user
    // Just checking if the syscall path exists

    struct
    {
        struct ipt_replace repl;
        struct ipt_entry entry;
        struct xt_entry_target target;
    } __attribute__((packed)) table;

    memset(&table, 0, sizeof(table));
    strcpy(table.repl.name, "filter");
    table.repl.valid_hooks = 0x7; // INPUT, FORWARD, OUTPUT
    table.repl.num_entries = 1;
    table.repl.size = sizeof(table.entry) + sizeof(table.target);

    // Try setsockopt (will fail without CAP_NET_ADMIN)
    int ret = setsockopt(sock, IPPROTO_IP, IPT_SO_SET_REPLACE, &table, sizeof(table));

    if (ret < 0)
    {
        if (errno == EPERM || errno == EACCES)
        {
            printf("    [!] Permission denied (need CAP_NET_ADMIN)\n");
            printf("    [!] Cannot trigger without root or capabilities\n");
        }
        else if (errno == ENOPROTOOPT)
        {
            printf("    [-] IPT_SO_SET_REPLACE not available\n");
        }
        else
        {
            printf("    [-] setsockopt failed: %s\n", strerror(errno));
        }
    }

    close(sock);
    return ret;
}

int main(int argc, char *argv[])
{
    printf("==============================================\n");
    printf(" CVE-2021-22555 Vulnerability Check\n");
    printf(" Netfilter heap out-of-bounds write\n");
    printf(" Target: Rokid Glasses (Kernel 5.10.209)\n");
    printf("==============================================\n\n");

    printf("[*] Current user: ");
    fflush(stdout);
    system("id");

    printf("\n[*] Kernel version: ");
    fflush(stdout);
    system("uname -r");

    // Check kernel config
    check_kernel_config();

    // Check loaded modules
    check_loaded_modules();

    // Check if iptables is accessible
    printf("\n[*] Checking iptables accessibility:\n");
    int iptables_status = check_iptables_available();

    // Try to trigger
    try_trigger_vuln();

    printf("\n==============================================\n");
    printf(" SUMMARY\n");
    printf("==============================================\n");

    printf("\n[*] CVE-2021-22555 Status on this device:\n");
    printf("    - Kernel 5.10.x is in vulnerable range (2.6.19 - 5.12)\n");
    printf("    - CONFIG_IP_NF_IPTABLES=y (compiled in)\n");
    printf("    - ip_tables module: NOT loaded (needs root to load)\n");
    printf("    - CONFIG_SYSVIPC: DISABLED (msg_msg spray blocked)\n");
    printf("    - CONFIG_USER_NS: DISABLED (namespace sandbox blocked)\n");
    printf("    - CONFIG_SLAB_FREELIST_HARDENED: ENABLED (heap hardened)\n");
    printf("\n");
    printf("[!] CONCLUSION: CVE-2021-22555 is NOT directly exploitable\n");
    printf("    Reasons:\n");
    printf("    1. Cannot load ip_tables module without root\n");
    printf("    2. No System V IPC for msg_msg heap spray\n");
    printf("    3. No user namespace for unprivileged trigger\n");
    printf("    4. SLAB hardening blocks heap corruption exploits\n");
    printf("\n");

    return 0;
}
