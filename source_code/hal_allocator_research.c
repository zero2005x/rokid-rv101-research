/*
 * hal_allocator_research.c - Graphics Allocator HAL Research
 *
 * Attempts to interact with IAllocator/IQtiAllocator HAL
 * to allocate or import GPU buffers
 *
 * Build: aarch64-linux-android31-clang -static -O2 -o hal_allocator_research hal_allocator_research.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdint.h>

/*
 * DMA-BUF Heap IOCTL definitions
 * From: include/uapi/linux/dma-heap.h
 */
#define DMA_HEAP_IOC_MAGIC 'H'

struct dma_heap_allocation_data
{
    uint64_t len;
    uint32_t fd;
    uint32_t fd_flags;
    uint64_t heap_flags;
};

#define DMA_HEAP_IOCTL_ALLOC _IOWR(DMA_HEAP_IOC_MAGIC, 0x0, struct dma_heap_allocation_data)

/* KGSL for comparison */
#define KGSL_IOC_TYPE 0x09
#define IOCTL_KGSL_GPUOBJ_ALLOC _IOWR(KGSL_IOC_TYPE, 0x42, struct kgsl_gpuobj_alloc)

struct kgsl_gpuobj_alloc
{
    uint64_t size;
    uint64_t flags;
    uint64_t va_len;
    uint64_t mmapsize;
    uint32_t id;
    uint32_t metadata_len;
    uint64_t metadata;
};

/*========== DMA HEAP ANALYSIS ==========*/

void analyze_dma_heaps()
{
    printf("\n=== DMA HEAP ANALYSIS ===\n");

    const char *heaps[] = {
        "/dev/dma_heap/system",
        "/dev/dma_heap/qcom,system",
        "/dev/dma_heap/qcom,user-contig",
        "/dev/dma_heap/qcom,secure-pixel",
        "/dev/dma_heap/qcom,secure-non-pixel",
        "/dev/dma_heap/qcom,qseecom",
        "/dev/dma_heap/qcom,qseecom-ta",
        "/dev/dma_heap/qcom,audio-ml",
        NULL};

    for (int i = 0; heaps[i]; i++)
    {
        struct stat st;
        if (stat(heaps[i], &st) == 0)
        {
            printf("[*] %s\n", heaps[i]);
            printf("    Mode: %o, UID: %d, GID: %d\n",
                   st.st_mode & 0777, st.st_uid, st.st_gid);

            /* Try to open */
            int fd = open(heaps[i], O_RDWR);
            if (fd >= 0)
            {
                printf("    [+] Opened successfully (fd=%d)\n", fd);

                /* Try allocation */
                struct dma_heap_allocation_data alloc = {0};
                alloc.len = 4096;
                alloc.fd_flags = O_RDWR | O_CLOEXEC;

                int ret = ioctl(fd, DMA_HEAP_IOCTL_ALLOC, &alloc);
                if (ret == 0)
                {
                    printf("    [+] ALLOC SUCCESS! buffer_fd=%d\n", alloc.fd);

                    /* Try mmap */
                    void *ptr = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                     MAP_SHARED, alloc.fd, 0);
                    if (ptr != MAP_FAILED)
                    {
                        printf("    [+] mmap SUCCESS: %p\n", ptr);
                        munmap(ptr, 4096);
                    }
                    close(alloc.fd);
                }
                else
                {
                    printf("    [-] ALLOC FAILED: %s\n", strerror(errno));
                }
                close(fd);
            }
            else
            {
                printf("    [-] Open failed: %s\n", strerror(errno));
            }
        }
    }
}

/*========== KGSL BUFFER EXPORT ==========*/

void test_kgsl_export()
{
    printf("\n=== KGSL BUFFER EXPORT TEST ===\n");

    int kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        printf("[-] Cannot open KGSL: %s\n", strerror(errno));
        return;
    }
    printf("[+] KGSL fd: %d\n", kgsl_fd);

    /* Allocate KGSL buffer */
    struct kgsl_gpuobj_alloc alloc = {0};
    alloc.size = 4096;
    alloc.flags = 0;

    int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPUOBJ_ALLOC, &alloc);
    if (ret == 0)
    {
        printf("[+] KGSL buffer allocated: id=%u, mmapsize=%lu\n",
               alloc.id, alloc.mmapsize);

        /* mmap the buffer */
        void *ptr = mmap(NULL, alloc.mmapsize, PROT_READ | PROT_WRITE,
                         MAP_SHARED, kgsl_fd, alloc.id * 0x1000);

        if (ptr != MAP_FAILED)
        {
            printf("[+] KGSL mmap: %p\n", ptr);

            /* Write pattern */
            memset(ptr, 0x41, 64);
            printf("[+] Written pattern 0x41\n");

            munmap(ptr, alloc.mmapsize);
        }
    }
    else
    {
        printf("[-] KGSL alloc failed: %s\n", strerror(errno));
    }

    close(kgsl_fd);
}

/*========== GRALLOC SERVICE CHECK ==========*/

void check_gralloc_service()
{
    printf("\n=== GRALLOC SERVICE CHECK ===\n");

    /* Check service manager */
    FILE *fp = popen("service list 2>&1 | grep -i 'allocator\\|mapper\\|gralloc'", "r");
    if (fp)
    {
        char line[256];
        printf("[*] Related services:\n");
        while (fgets(line, sizeof(line), fp))
        {
            printf("    %s", line);
        }
        pclose(fp);
    }

    /* Check HAL manifest */
    fp = popen("cat /vendor/etc/vintf/manifest/vendor.qti.hardware.display.allocator-service.xml 2>&1 | grep -E '<name>|<version>|<instance>'", "r");
    if (fp)
    {
        char line[256];
        printf("\n[*] HAL manifest:\n");
        while (fgets(line, sizeof(line), fp))
        {
            printf("    %s", line);
        }
        pclose(fp);
    }
}

/*========== HWSERVICEMANAGER CHECK ==========*/

void check_hwservicemanager()
{
    printf("\n=== HWSERVICEMANAGER CHECK ===\n");

    /* Try to access hwservicemanager */
    int hwsm_fd = open("/dev/hwbinder", O_RDWR);
    if (hwsm_fd >= 0)
    {
        printf("[+] HWBinder opened: fd=%d\n", hwsm_fd);
        close(hwsm_fd);
    }
    else
    {
        printf("[-] Cannot open HWBinder: %s\n", strerror(errno));
    }

    /* List graphics HALs via lshal */
    FILE *fp = popen("lshal 2>&1 | grep -i 'graphics\\|allocator\\|mapper' | head -10", "r");
    if (fp)
    {
        char line[512];
        printf("\n[*] Graphics HALs:\n");
        while (fgets(line, sizeof(line), fp))
        {
            printf("    %s", line);
        }
        pclose(fp);
    }
}

/*========== GRAPHICS ALLOCATOR PID ANALYSIS ==========*/

void analyze_allocator_process()
{
    printf("\n=== ALLOCATOR PROCESS ANALYSIS ===\n");

    /* Find allocator PID */
    FILE *fp = popen("ps -A | grep allocator", "r");
    if (!fp)
        return;

    char line[256];
    int allocator_pid = 0;

    while (fgets(line, sizeof(line), fp))
    {
        printf("[*] %s", line);

        /* Parse PID */
        int pid;
        if (sscanf(line, "%*s %d", &pid) == 1)
        {
            allocator_pid = pid;
        }
    }
    pclose(fp);

    if (allocator_pid == 0)
    {
        printf("[-] Allocator process not found\n");
        return;
    }

    printf("\n[*] Allocator PID: %d\n", allocator_pid);

    /* Check if we can access its fds */
    char fd_path[64];
    snprintf(fd_path, sizeof(fd_path), "/proc/%d/fd", allocator_pid);

    DIR *dir = opendir(fd_path);
    if (dir)
    {
        printf("[+] Can access %s (unexpected!)\n", fd_path);

        struct dirent *entry;
        int count = 0;
        while ((entry = readdir(dir)) != NULL && count < 20)
        {
            if (entry->d_name[0] == '.')
                continue;

            char link_path[128], link_target[256];
            snprintf(link_path, sizeof(link_path), "%s/%s", fd_path, entry->d_name);

            ssize_t len = readlink(link_path, link_target, sizeof(link_target) - 1);
            if (len > 0)
            {
                link_target[len] = '\0';
                printf("    fd %s -> %s\n", entry->d_name, link_target);
                count++;
            }
        }
        closedir(dir);
    }
    else
    {
        printf("[-] Cannot access %s: %s\n", fd_path, strerror(errno));
    }

    /* Check cmdline */
    char cmdline_path[64];
    snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", allocator_pid);

    int fd = open(cmdline_path, O_RDONLY);
    if (fd >= 0)
    {
        char cmdline[256] = {0};
        read(fd, cmdline, sizeof(cmdline) - 1);
        printf("[*] Cmdline: %s\n", cmdline);
        close(fd);
    }
}

/*========== NATIVE HANDLE STRUCTURE ==========*/

void explain_native_handle()
{
    printf("\n=== NATIVE HANDLE STRUCTURE ===\n");
    printf("[*] native_handle_t structure:\n");
    printf("    int version;      // sizeof(native_handle_t)\n");
    printf("    int numFds;       // number of file descriptors\n");
    printf("    int numInts;      // number of ints\n");
    printf("    int data[0];      // numFds fds followed by numInts ints\n");
    printf("\n");
    printf("[*] private_handle_t (Qualcomm gralloc):\n");
    printf("    Extends native_handle_t with:\n");
    printf("    - Buffer FD (from ION/DMA-BUF)\n");
    printf("    - Metadata FD\n");
    printf("    - Width, Height, Format\n");
    printf("    - Usage flags\n");
    printf("    - GPU address\n");
    printf("    - Size\n");
    printf("\n");
    printf("[*] To import a buffer, we need:\n");
    printf("    1. A valid buffer FD (from DMA-BUF/ION)\n");
    printf("    2. Call IMapper::importBuffer(hidl_handle)\n");
    printf("    3. Get back void* buffer pointer\n");
    printf("    4. Use IMapper::lock() to map for CPU access\n");
}

/*========== ALTERNATIVE: AHardwareBuffer ==========*/

void check_ahardwarebuffer()
{
    printf("\n=== AHARDWAREBUFFER CHECK ===\n");

    /* Check if libandroid.so exports AHardwareBuffer functions */
    FILE *fp = popen("strings /system/lib64/libandroid.so 2>&1 | grep -i 'AHardwareBuffer' | head -20", "r");
    if (fp)
    {
        char line[256];
        printf("[*] AHardwareBuffer exports:\n");
        while (fgets(line, sizeof(line), fp))
        {
            printf("    %s", line);
        }
        pclose(fp);
    }

    printf("\n[*] AHardwareBuffer API:\n");
    printf("    AHardwareBuffer_allocate() - Allocate new buffer\n");
    printf("    AHardwareBuffer_acquire() - Acquire reference\n");
    printf("    AHardwareBuffer_release() - Release reference\n");
    printf("    AHardwareBuffer_lock() - Lock for CPU access\n");
    printf("    AHardwareBuffer_unlock() - Unlock\n");
    printf("    AHardwareBuffer_sendHandleToUnixSocket() - Export FD\n");
    printf("    AHardwareBuffer_recvHandleFromUnixSocket() - Import FD\n");
}

/*========== GRAPHICS MAPPER PASSTHROUGH ==========*/

void check_mapper_passthrough()
{
    printf("\n=== MAPPER PASSTHROUGH CHECK ===\n");

    /* Check mapper libraries */
    const char *mappers[] = {
        "/vendor/lib64/hw/gralloc.default.so",
        "/vendor/lib64/hw/android.hardware.graphics.mapper@4.0-impl-qti-display.so",
        NULL};

    for (int i = 0; mappers[i]; i++)
    {
        struct stat st;
        if (stat(mappers[i], &st) == 0)
        {
            printf("[+] Found: %s\n", mappers[i]);
            printf("    Size: %ld bytes\n", (long)st.st_size);
        }
    }

    /* Check for passthrough service mode */
    FILE *fp = popen("getprop ro.hardware.gralloc 2>&1", "r");
    if (fp)
    {
        char line[64] = {0};
        fgets(line, sizeof(line), fp);
        printf("[*] ro.hardware.gralloc: %s", line[0] ? line : "(empty)\n");
        pclose(fp);
    }
}

/*========== MAIN ==========*/

int main(int argc, char *argv[])
{
    printf("=== HAL Allocator Research Tool ===\n");
    printf("Qualcomm Display Allocator Analysis\n\n");

    analyze_dma_heaps();
    test_kgsl_export();
    check_gralloc_service();
    check_hwservicemanager();
    analyze_allocator_process();
    explain_native_handle();
    check_ahardwarebuffer();
    check_mapper_passthrough();

    printf("\n=== SUMMARY ===\n");
    printf("[*] DMA-BUF heaps: read-only (need system group)\n");
    printf("[*] KGSL: Can allocate, but context creation blocked\n");
    printf("[*] IAllocator HAL: Requires HWBinder transaction\n");
    printf("[*] IMapper: importBuffer needs valid native_handle\n");
    printf("[*] AHardwareBuffer: Possible alternative API\n");

    return 0;
}
