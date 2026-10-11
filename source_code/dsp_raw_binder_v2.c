// dsp_raw_binder_v2.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#define BINDER_WRITE_READ 0xc0186201

struct binder_write_read {
    unsigned long write_buffer;
    unsigned long write_size;
    unsigned long write_consumed;
    unsigned long read_buffer;
    unsigned long read_size;
    unsigned long read_consumed;
};

typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} Parcel;

void parcel_init(Parcel *p, size_t cap) {
    p->data = (char*)malloc(cap);
    p->capacity = cap;
    p->size = 0;
}

void parcel_write_int32(Parcel *p, int32_t val) {
    memcpy(p->data + p->size, &val, 4);
    p->size += 4;
}

void parcel_write_string(Parcel *p, const char *str) {
    int32_t len = strlen(str) + 1;
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

    printf("[+] /dev/hwbinder opened successfully\n");

    const char *interface = "vendor.qti.hardware.dsp@1.0::IDspService/dspservice";
    uint32_t transaction_code = 2;        // <--- 強烈建議用 IDA 確認 getNewDSPSession 的實際 code

    Parcel req;
    parcel_init(&req, 4096);

    // HIDL 標準開頭：Interface Descriptor
    parcel_write_string(&req, interface);

    // Transaction Code (method index)
    parcel_write_int32(&req, transaction_code);

    // 參數：client_id (通常是 0)
    parcel_write_int32(&req, 0);

    // 準備 BWR
    char *read_buffer = (char*)malloc(8192);

    struct binder_write_read bwr = {
        .write_buffer = (unsigned long)req.data,
        .write_size = req.size,
        .read_buffer = (unsigned long)read_buffer,
        .read_size = 8192
    };

    printf("[*] Sending transaction to %s (method=%u)...\n", interface, transaction_code);

    if (ioctl(fd, BINDER_WRITE_READ, &bwr) < 0) {
        perror("[-] ioctl BINDER_WRITE_READ failed");
    } else {
        printf("[+] Transaction completed. Reply size: %lu\n", bwr.read_consumed);
        
        // TODO: 在這裡解析 reply，尋找 hidl_handle (FD)
        if (bwr.read_consumed > 0) {
            printf("[!] Check reply buffer for returned file descriptor\n");
        }
    }

    free(req.data);
    free(read_buffer);
    close(fd);
    return 0;
}