#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define KGSL_IOC_TYPE 0x09

struct kgsl_device_getproperty {
    unsigned int type;
    void *value;
    size_t sizebytes;
};

struct kgsl_version {
    unsigned int drv_major;
    unsigned int drv_minor;
    unsigned int drv_patch;
    unsigned int api_major;
    unsigned int api_minor;
    unsigned int api_patch;
};

#define IOCTL_KGSL_DEVICE_GETPROPERTY _IOWR(KGSL_IOC_TYPE, 0x2, struct kgsl_device_getproperty)
#define KGSL_PROP_VERSION 1

int main() {
    printf("[*] Probing /dev/kgsl-3d0...\n");
    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0) {
        perror("[-] open /dev/kgsl-3d0 failed");
        return 1;
    }
    printf("[+] opened /dev/kgsl-3d0 successfully (FD: %d)\n", fd);

    struct kgsl_version version = {0};
    struct kgsl_device_getproperty prop = {0};
    
    prop.type = KGSL_PROP_VERSION;
    prop.value = (void *)&version;
    prop.sizebytes = sizeof(version);

    int ret = ioctl(fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop);
    if (ret < 0) {
        perror("[-] ioctl IOCTL_KGSL_DEVICE_GETPROPERTY failed");
    } else {
        printf("[+] KGSL Version: Driver %u.%u.%u, API %u.%u.%u\n",
               version.drv_major, version.drv_minor, version.drv_patch,
               version.api_major, version.api_minor, version.api_patch);
    }

    close(fd);
    printf("[*] Probe completed.\n");
    return 0;
}
