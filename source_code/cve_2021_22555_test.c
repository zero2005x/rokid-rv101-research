/*
 * CVE-2021-22555 test — check whether the IPT_SO_SET_REPLACE trigger is
 * reachable from the current (unprivileged) context.
 *
 * CVE-2021-22555 requires CAP_NET_ADMIN. On this device CapEff == 0 and
 * CONFIG_USER_NS / CONFIG_SYSVIPC are disabled, so the trigger is unreachable
 * without root.
 *
 * Compile:
 *   aarch64-linux-android24-clang -O2 -o cve_2021_22555_test cve_2021_22555_test.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <linux/netfilter_ipv4/ip_tables.h>

#define CAP_NET_ADMIN 12

static void print_caps(void)
{
    char buf[4096];
    int fd = open("/proc/self/status", O_RDONLY);
    if (fd < 0)
    {
        printf("[-] Cannot open /proc/self/status: %s\n", strerror(errno));
        return;
    }
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return;
    buf[n] = 0;

    unsigned long cap_eff = 0, cap_bnd = 0;
    for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n"))
    {
        if (strncmp(line, "CapEff:", 7) == 0)
            cap_eff = strtoul(line + 7, NULL, 16);
        else if (strncmp(line, "CapBnd:", 7) == 0)
            cap_bnd = strtoul(line + 7, NULL, 16);
    }

    printf("[*] CapEff = 0x%lx\n", cap_eff);
    printf("[*] CapBnd = 0x%lx\n", cap_bnd);
    printf("[*] CAP_NET_ADMIN (bit %d) effective: %s\n", CAP_NET_ADMIN,
           (cap_eff & (1UL << CAP_NET_ADMIN)) ? "YES" : "NO");
    printf("[*] CAP_NET_ADMIN in bounding set: %s\n",
           (cap_bnd & (1UL << CAP_NET_ADMIN)) ? "YES" : "NO");
}

int main(void)
{
    printf("[*] CVE-2021-22555 Vulnerability Test\n");
    printf("[*] uid=%d euid=%d\n\n", getuid(), geteuid());

    struct utsname uts;
    if (uname(&uts) == 0)
        printf("[*] Kernel: %s %s\n", uts.sysname, uts.release);

    print_caps();

    /* 1. RAW socket (informational; needs CAP_NET_RAW) */
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (sock < 0)
        printf("[-] socket(SOCK_RAW) failed: %s (expected without CAP_NET_RAW)\n",
               strerror(errno));
    else
    {
        printf("[+] SOCK_RAW created (fd=%d)\n", sock);
        close(sock);
    }

    /* 2. DGRAM socket for the IPT_SO_SET_REPLACE trigger */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        printf("[-] socket(SOCK_DGRAM) failed: %s\n", strerror(errno));
        return 1;
    }

    struct ipt_replace repl;
    memset(&repl, 0, sizeof(repl));
    strncpy(repl.name, "filter", sizeof(repl.name) - 1);

    printf("\n[*] Calling setsockopt(IPT_SO_SET_REPLACE)...\n");
    int ret = setsockopt(sock, SOL_IP, IPT_SO_SET_REPLACE, &repl, sizeof(repl));
    if (ret < 0)
    {
        printf("[-] setsockopt(IPT_SO_SET_REPLACE) failed: %s (errno=%d)\n",
               strerror(errno), errno);
        switch (errno)
        {
        case EPERM:
            printf("[!] EPERM: CAP_NET_ADMIN required → CVE-2021-22555 NOT reachable\n");
            printf("[!] (CONFIG_USER_NS disabled ⇒ cannot create a userns to gain it)\n");
            break;
        case ENOPROTOOPT:
            printf("[!] ENOPROTOOPT: netfilter/x_tables not available\n");
            break;
        case EINVAL:
            printf("[+] EINVAL: netfilter is processing the request (vuln code reachable)\n");
            break;
        default:
            printf("[*] Unrecognised errno — check kernel config\n");
        }
    }
    else
    {
        printf("[+] setsockopt succeeded unexpectedly!\n");
    }

    close(sock);
    return 0;
}
