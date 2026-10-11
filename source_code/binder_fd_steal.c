/*
 * binder_fd_steal.c - Binder FD Passing Research Tool
 *
 * Attempts to obtain SurfaceFlinger's KGSL FD through Binder IPC
 *
 * Research vectors:
 * 1. Direct binder transaction to SurfaceFlinger
 * 2. GraphicBufferAllocator service
 * 3. GPU service enumeration
 * 4. HWC (Hardware Composer) service
 *
 * Build: aarch64-linux-android31-clang -static -O2 -o binder_fd_steal binder_fd_steal.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/android/binder.h>

#define BINDER_DEV "/dev/binder"

/* Service Manager interface */
#define SVC_MGR_NAME "android.os.IServiceManager"
#define SVC_MGR_HANDLE 0

/* Transaction codes */
#define GET_SERVICE_TRANSACTION 1
#define CHECK_SERVICE_TRANSACTION 2
#define ADD_SERVICE_TRANSACTION 3
#define LIST_SERVICES_TRANSACTION 4

/* Binder buffer size */
#define BINDER_VM_SIZE (1024 * 1024)

static int binder_fd = -1;
static void *binder_mmap = MAP_FAILED;

/*========== BINDER PROTOCOL HELPERS ==========*/

int binder_open()
{
    binder_fd = open(BINDER_DEV, O_RDWR | O_CLOEXEC);
    if (binder_fd < 0)
    {
        printf("[-] Cannot open %s: %s\n", BINDER_DEV, strerror(errno));
        return -1;
    }

    /* Get version */
    struct binder_version ver;
    if (ioctl(binder_fd, BINDER_VERSION, &ver) < 0)
    {
        printf("[-] BINDER_VERSION failed: %s\n", strerror(errno));
        close(binder_fd);
        return -1;
    }
    printf("[+] Binder version: %d (protocol %d)\n",
           ver.protocol_version, ver.protocol_version);

    /* mmap binder memory */
    binder_mmap = mmap(NULL, BINDER_VM_SIZE, PROT_READ,
                       MAP_PRIVATE | MAP_NORESERVE, binder_fd, 0);
    if (binder_mmap == MAP_FAILED)
    {
        printf("[-] Binder mmap failed: %s\n", strerror(errno));
        close(binder_fd);
        return -1;
    }
    printf("[+] Binder mmap: %p\n", binder_mmap);

    return 0;
}

void binder_close()
{
    if (binder_mmap != MAP_FAILED)
    {
        munmap(binder_mmap, BINDER_VM_SIZE);
    }
    if (binder_fd >= 0)
    {
        close(binder_fd);
    }
}

int binder_write_read(void *write_buf, size_t write_size,
                      void *read_buf, size_t read_size)
{
    struct binder_write_read bwr;
    memset(&bwr, 0, sizeof(bwr));

    bwr.write_buffer = (binder_uintptr_t)write_buf;
    bwr.write_size = write_size;
    bwr.read_buffer = (binder_uintptr_t)read_buf;
    bwr.read_size = read_size;

    int ret = ioctl(binder_fd, BINDER_WRITE_READ, &bwr);
    if (ret < 0)
    {
        return -1;
    }

    return bwr.read_consumed;
}

/*========== SERVICE MANAGER LOOKUP ==========*/

int lookup_service(const char *name, uint32_t *handle)
{
    /* Build transaction data */
    uint8_t write_buf[512];
    uint8_t read_buf[512];
    size_t write_pos = 0;

    /* BC_TRANSACTION command */
    uint32_t cmd = BC_TRANSACTION;
    memcpy(write_buf + write_pos, &cmd, sizeof(cmd));
    write_pos += sizeof(cmd);

    /* Transaction data header */
    struct binder_transaction_data tr;
    memset(&tr, 0, sizeof(tr));
    tr.target.handle = SVC_MGR_HANDLE;
    tr.code = CHECK_SERVICE_TRANSACTION;
    tr.flags = TF_ACCEPT_FDS;

    /* Transaction payload: service name as string16 */
    size_t name_len = strlen(name);
    uint8_t payload[256];
    size_t payload_pos = 0;

    /* Strict mode header (required by Android) */
    uint32_t strict_policy = 0;
    memcpy(payload + payload_pos, &strict_policy, 4);
    payload_pos += 4;

    /* String16 length */
    int32_t str_len = name_len;
    memcpy(payload + payload_pos, &str_len, 4);
    payload_pos += 4;

    /* String16 data (UTF-16) */
    for (size_t i = 0; i <= name_len; i++)
    {
        uint16_t c = (i < name_len) ? name[i] : 0;
        memcpy(payload + payload_pos, &c, 2);
        payload_pos += 2;
    }

    /* Align to 4 bytes */
    while (payload_pos % 4)
        payload_pos++;

    tr.data_size = payload_pos;
    tr.data.ptr.buffer = (binder_uintptr_t)payload;
    tr.data.ptr.offsets = 0;
    tr.offsets_size = 0;

    memcpy(write_buf + write_pos, &tr, sizeof(tr));
    write_pos += sizeof(tr);

    /* Execute transaction */
    int consumed = binder_write_read(write_buf, write_pos, read_buf, sizeof(read_buf));
    if (consumed < 0)
    {
        printf("[-] Transaction failed: %s\n", strerror(errno));
        return -1;
    }

    /* Parse response */
    size_t read_pos = 0;
    while (read_pos < (size_t)consumed)
    {
        uint32_t resp_cmd;
        memcpy(&resp_cmd, read_buf + read_pos, 4);
        read_pos += 4;

        printf("    Response cmd: 0x%x\n", resp_cmd);

        if (resp_cmd == BR_REPLY)
        {
            struct binder_transaction_data reply;
            memcpy(&reply, read_buf + read_pos, sizeof(reply));
            printf("    Reply: target.ptr=%lx, code=%u\n",
                   (unsigned long)reply.target.ptr, reply.code);

            if (reply.data_size >= 4)
            {
                uint8_t *reply_data = (uint8_t *)reply.data.ptr.buffer;
                if (reply_data)
                {
                    uint32_t reply_handle;
                    memcpy(&reply_handle, reply_data, 4);
                    printf("    Handle: %u\n", reply_handle);
                    if (handle)
                        *handle = reply_handle;
                    return 0;
                }
            }
            break;
        }
        else if (resp_cmd == BR_NOOP)
        {
            continue;
        }
        else if (resp_cmd == BR_TRANSACTION_COMPLETE)
        {
            continue;
        }
        else
        {
            printf("    Unknown response: 0x%x\n", resp_cmd);
            break;
        }
    }

    return -1;
}

/*========== ALTERNATIVE: PARSE SERVICE LIST FROM DUMPSYS ==========*/

void enumerate_gpu_services()
{
    printf("\n=== GPU-RELATED SERVICES ===\n");

    FILE *fp = popen("service list 2>&1", "r");
    if (!fp)
    {
        printf("[-] Cannot run service list\n");
        return;
    }

    char line[512];
    int count = 0;

    printf("[*] Services containing 'gpu', 'graphics', 'surface', 'hwc', 'display':\n");

    while (fgets(line, sizeof(line), fp))
    {
        if (strstr(line, "gpu") || strstr(line, "GPU") ||
            strstr(line, "graphics") || strstr(line, "Graphics") ||
            strstr(line, "surface") || strstr(line, "Surface") ||
            strstr(line, "hwc") || strstr(line, "HWC") ||
            strstr(line, "display") || strstr(line, "Display") ||
            strstr(line, "composer") || strstr(line, "Composer") ||
            strstr(line, "allocator") || strstr(line, "gralloc"))
        {
            printf("    %s", line);
            count++;
        }
    }

    pclose(fp);
    printf("[*] Found %d potentially interesting services\n", count);
}

/*========== DUMPSYS EXPLORATION ==========*/

void explore_surfaceflinger()
{
    printf("\n=== SURFACEFLINGER EXPLORATION ===\n");

    /* Get SurfaceFlinger display info (might leak GPU handles) */
    printf("[*] SurfaceFlinger display info:\n");
    FILE *fp = popen("dumpsys SurfaceFlinger --display-id 2>&1 | head -50", "r");
    if (fp)
    {
        char line[512];
        while (fgets(line, sizeof(line), fp))
        {
            if (strstr(line, "GPU") || strstr(line, "gpu") ||
                strstr(line, "handle") || strstr(line, "buffer") ||
                strstr(line, "fd") || strstr(line, "kgsl"))
            {
                printf("    %s", line);
            }
        }
        pclose(fp);
    }

    /* Check HWC info */
    printf("\n[*] Hardware Composer info:\n");
    fp = popen("dumpsys SurfaceFlinger --hwc 2>&1 | head -30", "r");
    if (fp)
    {
        char line[512];
        int printed = 0;
        while (fgets(line, sizeof(line), fp) && printed < 20)
        {
            printf("    %s", line);
            printed++;
        }
        pclose(fp);
    }
}

/*========== GRALLOC EXPLORATION ==========*/

void explore_gralloc()
{
    printf("\n=== GRALLOC EXPLORATION ===\n");

    /* Check if we can access gralloc through HAL */
    FILE *fp = popen("ls -la /dev/ion /dev/dma_heap/* 2>&1", "r");
    if (fp)
    {
        char line[256];
        while (fgets(line, sizeof(line), fp))
        {
            printf("    %s", line);
        }
        pclose(fp);
    }

    /* Check hwservicemanager for graphics HALs */
    printf("\n[*] Graphics HALs:\n");
    fp = popen("service list 2>&1 | grep -E 'gralloc|allocator|graphics|mapper'", "r");
    if (fp)
    {
        char line[256];
        while (fgets(line, sizeof(line), fp))
        {
            printf("    %s", line);
        }
        pclose(fp);
    }
}

/*========== HWBINDER EXPLORATION ==========*/

void explore_hwbinder()
{
    printf("\n=== HWBINDER EXPLORATION ===\n");

    int hw_fd = open("/dev/hwbinder", O_RDWR);
    if (hw_fd < 0)
    {
        printf("[-] Cannot open /dev/hwbinder: %s\n", strerror(errno));
        return;
    }

    struct binder_version ver;
    if (ioctl(hw_fd, BINDER_VERSION, &ver) == 0)
    {
        printf("[+] HWBinder version: %d\n", ver.protocol_version);
    }

    /* Try to enter looper (required for receiving transactions) */
    uint32_t cmd = BC_ENTER_LOOPER;
    struct binder_write_read bwr;
    memset(&bwr, 0, sizeof(bwr));
    bwr.write_buffer = (binder_uintptr_t)&cmd;
    bwr.write_size = sizeof(cmd);

    if (ioctl(hw_fd, BINDER_WRITE_READ, &bwr) == 0)
    {
        printf("[+] Entered HWBinder looper\n");
    }

    close(hw_fd);
}

/*========== VNDBINDER EXPLORATION ==========*/

void explore_vndbinder()
{
    printf("\n=== VNDBINDER EXPLORATION ===\n");

    int vnd_fd = open("/dev/vndbinder", O_RDWR);
    if (vnd_fd < 0)
    {
        printf("[-] Cannot open /dev/vndbinder: %s\n", strerror(errno));
        return;
    }

    struct binder_version ver;
    if (ioctl(vnd_fd, BINDER_VERSION, &ver) == 0)
    {
        printf("[+] VndBinder version: %d\n", ver.protocol_version);
    }

    close(vnd_fd);
}

/*========== CHECK GRAPHICS BUFFER HANDLES ==========*/

void check_graphics_handles()
{
    printf("\n=== GRAPHICS BUFFER HANDLE CHECK ===\n");

    /* Check /dev/graphics/ */
    FILE *fp = popen("ls -la /dev/graphics/ 2>&1", "r");
    if (fp)
    {
        printf("[*] /dev/graphics/:\n");
        char line[256];
        while (fgets(line, sizeof(line), fp))
        {
            printf("    %s", line);
        }
        pclose(fp);
    }

    /* Check framebuffer */
    fp = popen("ls -la /dev/fb* 2>&1", "r");
    if (fp)
    {
        printf("[*] Framebuffer devices:\n");
        char line[256];
        while (fgets(line, sizeof(line), fp))
        {
            printf("    %s", line);
        }
        pclose(fp);
    }

    /* Try to open fb0 */
    int fb_fd = open("/dev/graphics/fb0", O_RDWR);
    if (fb_fd >= 0)
    {
        printf("[+] Opened /dev/graphics/fb0 fd=%d\n", fb_fd);
        close(fb_fd);
    }
    else
    {
        printf("[-] Cannot open /dev/graphics/fb0: %s\n", strerror(errno));
    }
}

/*========== MAIN ==========*/

int main(int argc, char *argv[])
{
    printf("=== Binder FD Steal Research Tool ===\n");
    printf("Attempting to obtain SurfaceFlinger GPU handles\n\n");

    /* Basic service enumeration */
    enumerate_gpu_services();

    /* Explore SurfaceFlinger */
    explore_surfaceflinger();

    /* Explore Gralloc */
    explore_gralloc();

    /* Check graphics handles */
    check_graphics_handles();

    /* Try to open binder and lookup services */
    printf("\n=== BINDER TRANSACTION TEST ===\n");
    if (binder_open() == 0)
    {
        /* Try to lookup SurfaceFlinger */
        printf("[*] Looking up SurfaceFlinger...\n");
        uint32_t sf_handle = 0;
        if (lookup_service("SurfaceFlinger", &sf_handle) == 0)
        {
            printf("[+] SurfaceFlinger handle: %u\n", sf_handle);
        }
        else
        {
            printf("[-] Failed to lookup SurfaceFlinger\n");
        }

        binder_close();
    }

    /* Explore hwbinder */
    explore_hwbinder();

    /* Explore vndbinder */
    explore_vndbinder();

    printf("\n=== SUMMARY ===\n");
    printf("[*] Binder FD passing requires:\n");
    printf("    1. Valid service handle from ServiceManager\n");
    printf("    2. Correct transaction code for the target method\n");
    printf("    3. Proper Parcel serialization\n");
    printf("    4. SELinux policy allowing the transaction\n");
    printf("[*] Alternative: Find a service that returns GPU buffer FDs\n");

    return 0;
}
