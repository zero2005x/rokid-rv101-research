#define _GNU_SOURCE
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>

int main(int argc, char** argv) {
    const char* path = "/dev/adsprpc-smd";
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        printf("open O_RDWR failed: %s\n", strerror(errno));
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            printf("open O_RDONLY failed: %s\n", strerror(errno));
            return 1;
        }
        printf("open O_RDONLY ok fd=%d\n", fd);
    } else {
        printf("open O_RDWR ok fd=%d\n", fd);
    }

    /* try basic ioctl read device info - FAST_RPC_INVOKE is 0x09 << _IOC */
    unsigned long cmds[] = {
        0xc0080901, /* FASTRPC_GETINFO? */
        0xc0100902,
        0x40040903,
    };
    char buf[256];
    for (int i = 0; i < 3; i++) {
        memset(buf, 0, sizeof(buf));
        errno = 0;
        int r = ioctl(fd, cmds[i], buf);
        printf("ioctl 0x%08lx -> r=%d errno=%d(%s)\n", cmds[i], r, errno, strerror(errno));
    }
    close(fd);
    return 0;
}