#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <stdlib.h>

#define KGSL_IOC_TYPE 0x09

struct kgsl_syncsource_create
{
    unsigned int id; /* Out */
};

struct kgsl_syncsource_destroy
{
    unsigned int id; /* In */
};

struct kgsl_syncsource_create_fence
{
    unsigned int id;      /* In */
    unsigned int fencefd; /* Out */
};

struct kgsl_syncsource_signal_fence
{
    unsigned int id;      /* In */
    unsigned int fencefd; /* In */
};

#define KGSL_IOCTL_SYNCSOURCE_CREATE \
    _IOWR(KGSL_IOC_TYPE, 0x40, struct kgsl_syncsource_create)
#define KGSL_IOCTL_SYNCSOURCE_DESTROY \
    _IOW(KGSL_IOC_TYPE, 0x41, struct kgsl_syncsource_destroy)
#define KGSL_IOCTL_SYNCSOURCE_CREATE_FENCE \
    _IOWR(KGSL_IOC_TYPE, 0x42, struct kgsl_syncsource_create_fence)
#define KGSL_IOCTL_SYNCSOURCE_SIGNAL_FENCE \
    _IOWR(KGSL_IOC_TYPE, 0x43, struct kgsl_syncsource_signal_fence)

int main()
{
    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
    {
        printf("[-] Failed to open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Successfully opened /dev/kgsl-3d0\n");

    // 1. Create Syncsource
    struct kgsl_syncsource_create create = {0};
    int ret = ioctl(fd, KGSL_IOCTL_SYNCSOURCE_CREATE, &create);
    if (ret < 0)
    {
        printf("[-] ioctl SYNCSOURCE_CREATE failed: %d (%s)\n", errno, strerror(errno));
        close(fd);
        return 1;
    }
    printf("[+] Created SyncSource ID: %u\n", create.id);

    // 2. Create Fence
    struct kgsl_syncsource_create_fence fence_create = {0};
    fence_create.id = create.id;
    ret = ioctl(fd, KGSL_IOCTL_SYNCSOURCE_CREATE_FENCE, &fence_create);
    if (ret < 0)
    {
        printf("[-] ioctl SYNCSOURCE_CREATE_FENCE failed: %d (%s)\n", errno, strerror(errno));
        // Don't exit, try destroy
    }
    else
    {
        printf("[+] Created Fence FD: %d\n", fence_create.fencefd);
    }

    // 3. Signal Fence
    if (fence_create.fencefd > 0)
    {
        struct kgsl_syncsource_signal_fence signal = {0};
        signal.id = create.id;
        signal.fencefd = fence_create.fencefd;
        ret = ioctl(fd, KGSL_IOCTL_SYNCSOURCE_SIGNAL_FENCE, &signal);
        if (ret < 0)
        {
            printf("[-] ioctl SYNCSOURCE_SIGNAL_FENCE failed: %d (%s)\n", errno, strerror(errno));
        }
        else
        {
            printf("[+] Signaled Fence successfully\n");
        }
    }

    // 4. Destroy Syncsource
    struct kgsl_syncsource_destroy destroy = {0};
    destroy.id = create.id;
    ret = ioctl(fd, KGSL_IOCTL_SYNCSOURCE_DESTROY, &destroy);
    if (ret < 0)
    {
        printf("[-] ioctl SYNCSOURCE_DESTROY failed: %d (%s)\n", errno, strerror(errno));
    }
    else
    {
        printf("[+] Destroyed SyncSource ID: %u\n", destroy.id);
    }

    close(fd);
    return 0;
}
