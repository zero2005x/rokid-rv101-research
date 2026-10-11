// dsp_raw_binder.c - 極簡 Raw HWBinder 版
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

    // TODO: 你需要先在裝置上執行以下指令取得資訊
    // adb shell lshal --debug vendor.qti.hardware.dsp@1.0::IDspService
    // 然後把 interface token 和 openSession 的 transaction code 填入下面

    // 這裡只是骨架，實際 parcel 構造非常複雜
    printf("[!] Raw binder 需要精確的 interface descriptor 和 method code\n");
    printf("    建議先 reverse vendor.qti.hardware.dsp@1.0.so\n");

    close(binder_fd);
    return 0;
}