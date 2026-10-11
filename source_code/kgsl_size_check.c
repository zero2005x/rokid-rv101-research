/*
 * kgsl_size_check.c
 * Tool to determine the slab cache used by kgsl_syncsource
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>

#define KGSL_IOC_TYPE 0x09
#define KGSL_IOCTL_SYNCSOURCE_CREATE _IOWR(KGSL_IOC_TYPE, 0x40, struct kgsl_syncsource_create)
#define KGSL_IOCTL_SYNCSOURCE_DESTROY _IOW(KGSL_IOC_TYPE, 0x41, struct kgsl_syncsource_destroy)

struct kgsl_syncsource_create
{
    unsigned int id;
};

struct kgsl_syncsource_destroy
{
    unsigned int id;
};

int main()
{
    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0)
    {
        perror("open");
        return 1;
    }

    printf("Creating 2000 syncsources...\n");
    // Hold them open
    unsigned int *ids = malloc(sizeof(unsigned int) * 2000);

    for (int i = 0; i < 2000; i++)
    {
        struct kgsl_syncsource_create create = {0};
        if (ioctl(fd, KGSL_IOCTL_SYNCSOURCE_CREATE, &create) < 0)
        {
            perror("ioctl");
            break;
        }
        ids[i] = create.id;
    }

    printf("Created. Sleeping for 10 seconds to allow slab inspection...\n");
    sleep(10);

    for (int i = 0; i < 2000; i++)
    {
        struct kgsl_syncsource_destroy destroy;
        destroy.id = ids[i];
        ioctl(fd, KGSL_IOCTL_SYNCSOURCE_DESTROY, &destroy);
    }

    close(fd);
    return 0;
}
