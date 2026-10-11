/*
 * kgsl_resource_alloc.c - resource allocation through ioctl 0x40
 *
 * Findings:
 * - ioctl 0x40 returns an incrementing ID (1, 2, 3...)
 * - this may be some kind of handle/resource allocation
 * - massive allocation may cause resource exhaustion or an integer overflow
 *
 * Build:
 * aarch64-linux-android31-clang -static -O2 -o kgsl_resource_alloc kgsl_resource_alloc.c
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <stdint.h>
#include <signal.h>

/* resource allocation ioctl */
#define IOCTL_RESOURCE_ALLOC 0xc0040940 // 4 bytes, returns ID

/* ioctl 0x5b - requires a pointer */
#define IOCTL_0x5b 0xc100095b // 256 bytes

/* ioctl 0x5c, 0x5d - return ENODEV */
#define IOCTL_0x5c 0xc010095c
#define IOCTL_0x5d 0xc010095d

int g_kgsl_fd = -1;
volatile int g_running = 1;

void signal_handler(int sig)
{
    g_running = 0;
    printf("\n[!] Signal received, stopping...\n");
}

void test_resource_exhaustion(int fd)
{
    printf("\n=== Testing Resource Exhaustion ===\n");

    uint32_t id;
    int count = 0;
    int last_id = 0;

    printf("[*] Allocating resources until failure...\n");

    while (g_running && count < 100000)
    {
        id = 0;
        errno = 0;
        int ret = ioctl(fd, IOCTL_RESOURCE_ALLOC, &id);

        if (ret < 0)
        {
            printf("[!] Allocation failed at count=%d, errno=%d (%s)\n",
                   count, errno, strerror(errno));
            break;
        }

        if (id != last_id + 1)
        {
            printf("[!] ID gap: expected %d, got %d at count=%d\n",
                   last_id + 1, id, count);
        }

        last_id = id;
        count++;

        // Report every 1000
        if (count % 1000 == 0)
        {
            printf("[*] Allocated %d resources, latest ID=%u\n", count, id);
        }
    }

    printf("[+] Total allocations: %d, last ID: %d\n", count, last_id);
}

void test_ioctl_5b_with_pointers(int fd)
{
    printf("\n=== Testing ioctl 0x5b with valid pointers ===\n");

    /*
     * ioctl 0x5b returns EFAULT, meaning it expects a valid userspace pointer in the structure
     *
     * Possible structure:
     * struct {
     *     void* ptr;      // userspace buffer
     *     size_t size;    // size
     *     ...
     * };
     */

    // Allocate some userspace memory
    void *user_buf = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (user_buf == MAP_FAILED)
    {
        printf("[-] mmap failed\n");
        return;
    }
    memset(user_buf, 0, 4096);

    printf("[+] User buffer at %p\n", user_buf);

    // Try structure 1: the first field is a pointer
    struct
    {
        uint64_t ptr;
        uint64_t size;
        uint64_t padding[30];
    } req1 = {0};

    req1.ptr = (uint64_t)user_buf;
    req1.size = 4096;

    errno = 0;
    int ret = ioctl(fd, IOCTL_0x5b, &req1);
    printf("[*] struct{ptr, size}: ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));

    // Try structure 2: different offsets
    struct
    {
        uint64_t field0;
        uint64_t ptr;
        uint64_t size;
        uint64_t padding[29];
    } req2 = {0};

    req2.field0 = 0;
    req2.ptr = (uint64_t)user_buf;
    req2.size = 4096;

    errno = 0;
    ret = ioctl(fd, IOCTL_0x5b, &req2);
    printf("[*] struct{0, ptr, size}: ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));

    // Try structure 3: multiple pointers
    struct
    {
        uint64_t ptr1;
        uint64_t ptr2;
        uint64_t size;
        uint64_t padding[29];
    } req3 = {0};

    req3.ptr1 = (uint64_t)user_buf;
    req3.ptr2 = (uint64_t)user_buf;
    req3.size = 4096;

    errno = 0;
    ret = ioctl(fd, IOCTL_0x5b, &req3);
    printf("[*] struct{ptr, ptr, size}: ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));

    // Try structure 4: with an ID
    struct
    {
        uint32_t id;
        uint32_t padding1;
        uint64_t ptr;
        uint64_t size;
        uint64_t padding2[28];
    } req4 = {0};

    // First obtain a valid resource ID
    uint32_t valid_id;
    ioctl(fd, IOCTL_RESOURCE_ALLOC, &valid_id);
    printf("[+] Got resource ID: %u\n", valid_id);

    req4.id = valid_id;
    req4.ptr = (uint64_t)user_buf;
    req4.size = 4096;

    errno = 0;
    ret = ioctl(fd, IOCTL_0x5b, &req4);
    printf("[*] struct{id, ptr, size}: ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));

    munmap(user_buf, 4096);
}

void test_integer_overflow(int fd)
{
    printf("\n=== Testing Integer Overflow ===\n");

    // Try large values to trigger an integer overflow
    struct
    {
        uint64_t data[32];
    } req = {0};

    // Test 1: oversized size
    req.data[0] = 0;
    req.data[1] = 0xFFFFFFFFFFFFFFFFULL; // maximum size

    errno = 0;
    int ret = ioctl(fd, IOCTL_0x5b, &req);
    printf("[*] Max size: ret=%d, errno=%d\n", ret, errno);

    // Test 2: near the integer boundary
    req.data[1] = 0x7FFFFFFFFFFFFFFFULL; // MAX_LONG

    errno = 0;
    ret = ioctl(fd, IOCTL_0x5b, &req);
    printf("[*] MAX_LONG: ret=%d, errno=%d\n", ret, errno);

    // Test 3: negative values
    req.data[1] = (uint64_t)-1;

    errno = 0;
    ret = ioctl(fd, IOCTL_0x5b, &req);
    printf("[*] -1 as size: ret=%d, errno=%d\n", ret, errno);
}

void analyze_0x5b_structure(int fd)
{
    printf("\n=== Analyzing ioctl 0x5b structure ===\n");

    /*
     * The 256-byte structure
     * We need to find out which fields are used as pointers
     */

    // Create a valid buffer
    void *buf = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    uint64_t data[32] = {0};

    // Try placing a valid pointer at every 64-bit position
    for (int pos = 0; pos < 8; pos++)
    {
        memset(data, 0, sizeof(data));
        data[pos] = (uint64_t)buf;

        errno = 0;
        int ret = ioctl(fd, IOCTL_0x5b, data);

        if (errno != 14)
        { // EFAULT
            printf("[!] Pointer at position %d changes behavior: ret=%d, errno=%d (%s)\n",
                   pos, ret, errno, strerror(errno));
        }
    }

    munmap(buf, 0x10000);
}

void test_with_ashmem(int fd)
{
    printf("\n=== Testing with ashmem buffer ===\n");

    int ashmem_fd = open("/dev/ashmem", O_RDWR);
    if (ashmem_fd < 0)
    {
        printf("[-] Cannot open ashmem\n");
        return;
    }

    // Set up ashmem
    ioctl(ashmem_fd, 0x41007701, "kgsl_test"); // SET_NAME
    ioctl(ashmem_fd, 0x40087703, 0x10000);     // SET_SIZE

    void *ptr = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE,
                     MAP_SHARED, ashmem_fd, 0);
    if (ptr == MAP_FAILED)
    {
        printf("[-] mmap ashmem failed\n");
        close(ashmem_fd);
        return;
    }

    printf("[+] ashmem mapped at %p\n", ptr);

    // Try using an ashmem buffer
    uint64_t data[32] = {0};
    data[0] = (uint64_t)ptr;
    data[1] = 0x10000;

    errno = 0;
    int ret = ioctl(fd, IOCTL_0x5b, data);
    printf("[*] With ashmem: ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));

    // Try including the ashmem fd
    data[2] = ashmem_fd;

    errno = 0;
    ret = ioctl(fd, IOCTL_0x5b, data);
    printf("[*] With ashmem + fd: ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));

    munmap(ptr, 0x10000);
    close(ashmem_fd);
}

int main(int argc, char **argv)
{
    printf("=== KGSL Resource Allocation Analysis ===\n");
    printf("==========================================\n");

    signal(SIGINT, signal_handler);

    g_kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (g_kgsl_fd < 0)
    {
        printf("[-] Cannot open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Opened /dev/kgsl-3d0: fd=%d\n", g_kgsl_fd);

    // Analyze the 0x5b structure
    analyze_0x5b_structure(g_kgsl_fd);

    // Test 0x5b with a pointer
    test_ioctl_5b_with_pointers(g_kgsl_fd);

    // Test with ashmem
    test_with_ashmem(g_kgsl_fd);

    // Test the integer overflow
    test_integer_overflow(g_kgsl_fd);

    // Resource exhaustion test (small scale)
    if (argc > 1 && strcmp(argv[1], "-exhaust") == 0)
    {
        test_resource_exhaustion(g_kgsl_fd);
    }

    close(g_kgsl_fd);
    return 0;
}
