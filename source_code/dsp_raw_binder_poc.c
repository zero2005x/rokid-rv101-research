// dsp_raw_binder_poc.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>

#define BINDER_WRITE_READ 0xc0186201

struct binder_write_read {
    unsigned long write_buffer;
    unsigned long write_size;
    unsigned long write_consumed;
    unsigned long read_buffer;
    unsigned long read_size;
    unsigned long read_consumed;
};

// Simple Parcel structure (real code needs more complex buffer management)
typedef struct {
    char *data;
    size_t size;
    size_t capacity;
    size_t pos;
} Parcel;

void parcel_init(Parcel *p, size_t cap) {
    p->data = (char*)malloc(cap);
    p->capacity = cap;
    p->size = 0;
    p->pos = 0;
}

void parcel_write_string(Parcel *p, const char *str) {
    int len = strlen(str) + 1;
    memcpy(p->data + p->size, &len, 4);
    p->size += 4;
    memcpy(p->data + p->size, str, len);
    p->size += len;
}

int main() {
    int fd = open("/dev/hwbinder", O_RDWR);
    if (fd < 0) {
        perror("[-] open /dev/hwbinder");
        return 1;
    }

    printf("[+] /dev/hwbinder opened\n");

    // TODO: first run the following commands on the device to obtain the key information
    // adb shell lshal -l | grep dsp
    // adb shell lshal --debug vendor.qti.hardware.dsp@1.0::IDspService/default

    const char *interface = "vendor.qti.hardware.dsp@1.0::IDspService";
    // Assumed openSession transaction code (must be obtained by reversing, usually 1~N)
    uint32_t transaction_code = 1;   // <--- needs to be changed after reversing

    Parcel request;
    parcel_init(&request, 4096);

    // Write the interface descriptor
    parcel_write_string(&request, interface);

    // Write the method code
    memcpy(request.data + request.size, &transaction_code, 4);
    request.size += 4;

    // TODO: add arguments (for example client_id = 0)

    struct binder_write_read bwr = {
        .write_buffer = (unsigned long)request.data,
        .write_size = request.size,
        .read_buffer = (unsigned long)malloc(4096),
        .read_size = 4096
    };

    printf("[*] Sending transaction to %s (code=%u)...\n", interface, transaction_code);

    if (ioctl(fd, BINDER_WRITE_READ, &bwr) < 0) {
        perror("[-] ioctl BINDER_WRITE_READ");
    } else {
        printf("[+] Transaction sent. Check if FD was returned in reply.\n");
    }

    free(request.data);
    close(fd);
    return 0;
}