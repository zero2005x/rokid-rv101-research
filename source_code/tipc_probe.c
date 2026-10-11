/*
 * TIPC (Transparent Inter-Process Communication) Vulnerability Probe
 * ===================================================================
 *
 * TIPC is enabled in this kernel (CONFIG_TIPC=y).
 * Multiple TIPC vulnerabilities exist in 5.10.x kernels:
 * - CVE-2021-43267: Heap overflow in tipc_crypto_key_rcv
 * - CVE-2022-0435: Remote stack overflow in tipc_name_table_distr
 * - CVE-2024-36886: tipc_udp_nl_bearer_add() UAF
 *
 * This probe tests if TIPC is accessible and functional.
 *
 * Target: Rokid AR Glasses (Android 12, Kernel 5.10.209)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/tipc.h>

// TIPC socket constants
#ifndef AF_TIPC
#define AF_TIPC 30
#endif

#ifndef SOL_TIPC
#define SOL_TIPC 271
#endif

#ifndef TIPC_ADDR_MCAST
#define TIPC_ADDR_MCAST 1
#endif

#ifndef TIPC_NODE_STATE
#define TIPC_NODE_STATE 0x100
#endif

void test_tipc_socket(void)
{
    printf("[*] Testing TIPC socket creation...\n");

    // Try different TIPC socket types
    int types[] = {SOCK_STREAM, SOCK_DGRAM, SOCK_SEQPACKET, SOCK_RDM};
    const char *type_names[] = {"STREAM", "DGRAM", "SEQPACKET", "RDM"};

    for (int i = 0; i < 4; i++)
    {
        int sock = socket(AF_TIPC, types[i], 0);
        if (sock >= 0)
        {
            printf("[+] AF_TIPC/%s socket created (fd=%d)\n", type_names[i], sock);

            // Try to get socket info
            struct sockaddr_tipc addr;
            socklen_t len = sizeof(addr);
            if (getsockname(sock, (struct sockaddr *)&addr, &len) == 0)
            {
                printf("    Family: %d, AddrType: %d\n",
                       addr.family, addr.addrtype);
            }

            // Try some socket options
            int val = 0;
            socklen_t optlen = sizeof(val);
            if (getsockopt(sock, SOL_TIPC, TIPC_IMPORTANCE, &val, &optlen) == 0)
            {
                printf("    TIPC_IMPORTANCE: %d\n", val);
            }

            close(sock);
        }
        else
        {
            printf("[-] AF_TIPC/%s failed: %s\n", type_names[i], strerror(errno));
        }
    }
}

void test_tipc_bind(void)
{
    printf("\n[*] Testing TIPC bind operations...\n");

    int sock = socket(AF_TIPC, SOCK_RDM, 0);
    if (sock < 0)
    {
        printf("[-] Cannot create TIPC socket: %s\n", strerror(errno));
        return;
    }

    // Try binding to a TIPC address
    struct sockaddr_tipc addr = {0};
    addr.family = AF_TIPC;
    addr.addrtype = TIPC_ADDR_NAMESEQ;
    addr.addr.nameseq.type = 0x12345;
    addr.addr.nameseq.lower = 1;
    addr.addr.nameseq.upper = 100;
    addr.scope = TIPC_CLUSTER_SCOPE;

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0)
    {
        printf("[+] TIPC bind succeeded!\n");
        printf("    Type: 0x%x, Lower: %u, Upper: %u\n",
               addr.addr.nameseq.type,
               addr.addr.nameseq.lower,
               addr.addr.nameseq.upper);
    }
    else
    {
        printf("[-] TIPC bind failed: %s\n", strerror(errno));
    }

    close(sock);
}

void test_tipc_netlink(void)
{
    printf("\n[*] Testing TIPC netlink interface...\n");

    // TIPC uses generic netlink
    int sock = socket(AF_NETLINK, SOCK_RAW, 16); // NETLINK_GENERIC
    if (sock >= 0)
    {
        printf("[+] NETLINK_GENERIC socket created\n");
        // Would need to send proper genetlink messages to interact
        close(sock);
    }
    else
    {
        printf("[-] NETLINK_GENERIC failed: %s\n", strerror(errno));
    }
}

void test_tipc_multicast(void)
{
    printf("\n[*] Testing TIPC multicast (potential vuln vector)...\n");

    int sock = socket(AF_TIPC, SOCK_RDM, 0);
    if (sock < 0)
    {
        printf("[-] Cannot create socket: %s\n", strerror(errno));
        return;
    }

    // Prepare multicast address
    struct sockaddr_tipc mcast_addr = {0};
    mcast_addr.family = AF_TIPC;
    mcast_addr.addrtype = TIPC_ADDR_MCAST;
    mcast_addr.addr.nameseq.type = 0xDEAD;
    mcast_addr.addr.nameseq.lower = 1;
    mcast_addr.addr.nameseq.upper = 0xFFFFFFFF; // Wide range

    // Try to send (might trigger interesting code paths)
    char buf[256];
    memset(buf, 'A', sizeof(buf));

    ssize_t sent = sendto(sock, buf, sizeof(buf), 0,
                          (struct sockaddr *)&mcast_addr, sizeof(mcast_addr));
    if (sent >= 0)
    {
        printf("[+] Multicast send succeeded: %zd bytes\n", sent);
    }
    else
    {
        printf("[-] Multicast send failed: %s\n", strerror(errno));
    }

    close(sock);
}

void check_tipc_module(void)
{
    printf("[*] Checking TIPC module status...\n");

    FILE *f = fopen("/proc/modules", "r");
    if (!f)
    {
        printf("[-] Cannot read /proc/modules\n");
        return;
    }

    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), f))
    {
        if (strstr(line, "tipc"))
        {
            printf("[+] Found: %s", line);
            found = 1;
        }
    }
    fclose(f);

    if (!found)
    {
        printf("[*] TIPC module not currently loaded\n");
        printf("[*] Try: socket(AF_TIPC, ...) to auto-load\n");
    }
}

int main(void)
{
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  TIPC Vulnerability Probe                                     \n");
    printf("  Target: Rokid AR Glasses (Kernel 5.10.209)                   \n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    check_tipc_module();
    test_tipc_socket();
    test_tipc_bind();
    test_tipc_netlink();
    test_tipc_multicast();

    printf("\n[*] TIPC probe complete.\n");
    printf("[*] Check: dmesg | grep -i tipc\n");

    return 0;
}
