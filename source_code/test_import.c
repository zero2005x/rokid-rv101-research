#define _GNU_SOURCE
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#define KGSL_GPUOBJ_IMPORT 0xc0100958
#define KGSL_GPUOBJ_FREE   0x4004093b

struct kgsl_gpuobj_import {
    uint64_t flags;
    uint64_t va_len;
    uint64_t priv;
    uint64_t id;
    uint64_t offset;
};

#define KGSL_USER_MEM_TYPE_ADDR 1
static volatile int crashed = 0;
static void h(int s){ crashed = s; printf("[!] SIGNAL %d\n", s); }

static void test(int fd, void* ptr, uint64_t size, uint64_t flags, uint64_t va_len, int i) {
    struct kgsl_gpuobj_import imp;
    memset(&imp, 0, sizeof(imp));
    imp.flags = flags;
    imp.va_len = va_len;
    imp.priv = (uint64_t)ptr;
    errno = 0;
    int r = ioctl(fd, KGSL_GPUOBJ_IMPORT, &imp);
    printf("[%02d] IMPORT flags=0x%llx align=%llu size=0x%llx va_len=0x%llx -> r=%d errno=%d(%s) id=%llu off=0x%llx\n",
           i, (unsigned long long)flags, (unsigned long long)((flags>>16)&0xff),
           (unsigned long long)size, (unsigned long long)va_len,
           r, errno, strerror(errno),
           (unsigned long long)imp.id, (unsigned long long)imp.offset);
    if (r == 0) {
        uint64_t id = imp.id;
        ioctl(fd, KGSL_GPUOBJ_FREE, &id);
    }
}

int main(void) {
    signal(SIGSEGV, h); signal(SIGBUS, h); signal(SIGILL, h);
    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    printf("opened fd=%d\n", fd);
    size_t sz = 0x200000;
    void *buf = mmap(NULL, sz, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED) { perror("mmap"); return 1; }
    memset(buf, 0x41, sz);
    int i = 0;
    test(fd, buf, sz, KGSL_USER_MEM_TYPE_ADDR, 0, i++);
    test(fd, buf, sz, KGSL_USER_MEM_TYPE_ADDR, 0x1000, i++);
    test(fd, buf, sz, KGSL_USER_MEM_TYPE_ADDR | (20ULL<<16), 0x200000, i++);
    test(fd, buf, sz, KGSL_USER_MEM_TYPE_ADDR | (25ULL<<16), 0x200000, i++);
    test(fd, buf, sz, KGSL_USER_MEM_TYPE_ADDR | (32ULL<<16), 0x200000, i++);
    test(fd, buf, sz, KGSL_USER_MEM_TYPE_ADDR | (63ULL<<16), 0x200000, i++);
    test(fd, buf, sz, KGSL_USER_MEM_TYPE_ADDR | (128ULL<<16), 0x200000, i++);
    test(fd, buf, sz, KGSL_USER_MEM_TYPE_ADDR | (255ULL<<16), 0x200000, i++);
    printf("crashed=%d\n", crashed);
    munmap(buf, sz);
    close(fd);
    return 0;
}