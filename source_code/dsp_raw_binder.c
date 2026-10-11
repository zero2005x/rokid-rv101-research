// dsp_raw_binder.c - minimal Raw HWBinder version
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>

#define BINDER_WRITE_READ 0xc0186201
#define HWBINDER 1

struct binder_write_read {
    unsigned long write_buffer;
    unsigned long write_size;
    unsigned long write_consumed;
    unsigned long read_buffer;
    unsigned long read_size;
    unsigned long read_consumed;
};

int main() {
    int binder_fd = open("/dev/hwbinder", O_RDWR);
    if (binder_fd < 0) {
        perror("open /dev/hwbinder");
        return 1;
    }

    printf("[*] Opened /dev/hwbinder\n");

    // TODO: you must first run the following commands on the device to obtain the information
    // adb shell lshal --debug vendor.qti.hardware.dsp@1.0::IDspService
    // then fill in the interface token and the openSession transaction code below

    // This is only a skeleton; the actual parcel construction is very complex
    printf("[!] Raw binder 需要精確的 interface descriptor 和 method code\n");
    printf("    建議先 reverse vendor.qti.hardware.dsp@1.0.so\n");

    close(binder_fd);
    return 0;
}