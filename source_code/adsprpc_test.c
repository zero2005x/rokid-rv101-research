#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdint.h>

#define FASTRPC_IOCTL_MAGIC 'R'
#define FASTRPC_IOCTL_GET_INFO _IOWR(FASTRPC_IOCTL_MAGIC, 6, uint32_t)
#define FASTRPC_IOCTL_INIT_CREATE _IOWR(FASTRPC_IOCTL_MAGIC, 5, struct fastrpc_init_create)

struct fastrpc_init_create
{
    uint32_t filelen;
    int32_t filefd;
    uint32_t attrs;
    uint32_t siglen;
    uint64_t file;
    uint64_t sig;
};

int main()
{
    int fd = open("/dev/adsprpc-smd", O_RDWR);
    if (fd < 0)
    {
        printf("[-] Failed to open /dev/adsprpc-smd (O_RDWR): %s\n", strerror(errno));
        printf("[*] Trying O_RDONLY...\n");
        fd = open("/dev/adsprpc-smd", O_RDONLY);
        if (fd < 0)
        {
            printf("[-] Failed to open /dev/adsprpc-smd (O_RDONLY): %s\n", strerror(errno));
            return 1;
        }
        printf("[+] Successfully opened /dev/adsprpc-smd (O_RDONLY)\n");
    }
    else
    {
        printf("[+] Successfully opened /dev/adsprpc-smd (O_RDWR)\n");
    }

    // Try a simple ioctl to check functionality
    uint32_t info = 0;
    int ret = ioctl(fd, FASTRPC_IOCTL_GET_INFO, &info);
    if (ret < 0)
    {
        printf("[-] ioctl FASTRPC_IOCTL_GET_INFO failed: %s\n", strerror(errno));
    }
    else
    {
        printf("[+] ioctl FASTRPC_IOCTL_GET_INFO success\n");
    }

    // Try INIT_CREATE
    struct fastrpc_init_create init = {0};
    init.filefd = -1; // Invalid FD, should return error but confirms IOCTL reaches driver
    init.filelen = 0;

    ret = ioctl(fd, FASTRPC_IOCTL_INIT_CREATE, &init);
    if (ret < 0)
    {
        printf("[-] ioctl FASTRPC_IOCTL_INIT_CREATE failed: %d (%s)\n", errno, strerror(errno));
    }
    else
    {
        printf("[+] ioctl FASTRPC_IOCTL_INIT_CREATE success\n");
    }

    close(fd);
    return 0;
}
