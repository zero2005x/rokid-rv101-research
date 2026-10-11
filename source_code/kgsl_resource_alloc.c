/*
 * kgsl_resource_alloc.c - 利用 ioctl 0x40 進行資源分配
 *
 * 發現:
 * - ioctl 0x40 返回遞增的 ID (1, 2, 3...)
 * - 這可能是某種句柄/資源分配
 * - 大量分配可能導致資源耗盡或整數溢出
 *
 * 編譯:
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

/* 資源分配 ioctl */
#define IOCTL_RESOURCE_ALLOC 0xc0040940 // 4 bytes, returns ID

/* ioctl 0x5b - 需要指針 */
#define IOCTL_0x5b 0xc100095b // 256 bytes

/* ioctl 0x5c, 0x5d - 返回 ENODEV */
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

        // 每 1000 個報告一次
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
     * ioctl 0x5b 返回 EFAULT，意味著它期望結構中有有效的用戶空間指針
     *
     * 可能的結構:
     * struct {
     *     void* ptr;      // 用戶空間緩衝區
     *     size_t size;    // 大小
     *     ...
     * };
     */

    // 分配一些用戶空間記憶體
    void *user_buf = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (user_buf == MAP_FAILED)
    {
        printf("[-] mmap failed\n");
        return;
    }
    memset(user_buf, 0, 4096);

    printf("[+] User buffer at %p\n", user_buf);

    // 嘗試結構 1: 第一個字段是指針
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

    // 嘗試結構 2: 不同的偏移
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

    // 嘗試結構 3: 多個指針
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

    // 嘗試結構 4: 帶 ID
    struct
    {
        uint32_t id;
        uint32_t padding1;
        uint64_t ptr;
        uint64_t size;
        uint64_t padding2[28];
    } req4 = {0};

    // 先獲取一個有效的資源 ID
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

    // 嘗試使用大數值觸發整數溢出
    struct
    {
        uint64_t data[32];
    } req = {0};

    // 測試 1: 超大 size
    req.data[0] = 0;
    req.data[1] = 0xFFFFFFFFFFFFFFFFULL; // 最大 size

    errno = 0;
    int ret = ioctl(fd, IOCTL_0x5b, &req);
    printf("[*] Max size: ret=%d, errno=%d\n", ret, errno);

    // 測試 2: 接近整數邊界
    req.data[1] = 0x7FFFFFFFFFFFFFFFULL; // MAX_LONG

    errno = 0;
    ret = ioctl(fd, IOCTL_0x5b, &req);
    printf("[*] MAX_LONG: ret=%d, errno=%d\n", ret, errno);

    // 測試 3: 負值
    req.data[1] = (uint64_t)-1;

    errno = 0;
    ret = ioctl(fd, IOCTL_0x5b, &req);
    printf("[*] -1 as size: ret=%d, errno=%d\n", ret, errno);
}

void analyze_0x5b_structure(int fd)
{
    printf("\n=== Analyzing ioctl 0x5b structure ===\n");

    /*
     * 256 bytes 的結構
     * 需要找出哪些字段被用作指針
     */

    // 創建一個有效的緩衝區
    void *buf = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    uint64_t data[32] = {0};

    // 在每個 64 位位置嘗試放置有效指針
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

    // 設置 ashmem
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

    // 嘗試使用 ashmem 緩衝區
    uint64_t data[32] = {0};
    data[0] = (uint64_t)ptr;
    data[1] = 0x10000;

    errno = 0;
    int ret = ioctl(fd, IOCTL_0x5b, data);
    printf("[*] With ashmem: ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));

    // 嘗試加入 ashmem fd
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

    // 分析 0x5b 結構
    analyze_0x5b_structure(g_kgsl_fd);

    // 測試帶指針的 0x5b
    test_ioctl_5b_with_pointers(g_kgsl_fd);

    // 使用 ashmem 測試
    test_with_ashmem(g_kgsl_fd);

    // 測試整數溢出
    test_integer_overflow(g_kgsl_fd);

    // 資源耗盡測試 (小規模)
    if (argc > 1 && strcmp(argv[1], "-exhaust") == 0)
    {
        test_resource_exhaustion(g_kgsl_fd);
    }

    close(g_kgsl_fd);
    return 0;
}
