#define _GNU_SOURCE
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>

#define KGSL_GPUOBJ_ALLOC 0xc0300938
#define KGSL_GPUOBJ_FREE  0x4004093b

struct kgsl_gpuobj_alloc {
    uint64_t size;
    uint32_t flags;
    uint32_t va_len;
    uint64_t mmap_size;
    uint64_t id;
    uint64_t offset;
};

static void test_alloc(int fd, uint64_t size, uint32_t flags, int i) {
    struct kgsl_gpuobj_alloc a;
    memset(&a, 0, sizeof(a));
    a.size = size;
    a.flags = flags;
    a.va_len = 0;
    errno = 0;
    int r = ioctl(fd, KGSL_GPUOBJ_ALLOC, &a);
    printf("[%02d] ALLOC size=0x%llx flags=0x%08x align=%u -> r=%d errno=%d(%s) id=%llu\n",
           i, (unsigned long long)size, flags, (flags>>16)&0xff,
           r, errno, strerror(errno), (unsigned long long)a.id);
    if (r == 0) {
        uint64_t id = a.id;
        ioctl(fd, KGSL_GPUOBJ_FREE, &id);
    }
}

int main(void) {
    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    printf("opened fd=%d\n", fd);
    int i = 0;
    test_alloc(fd, 0x1000, 0x00000000, i++);   /* align 0 */
    test_alloc(fd, 0x1000, 0x00010000, i++);   /* align 1 -> 2 */
    test_alloc(fd, 0x1000, 0x00050000, i++);   /* align 5 -> 32 */
    test_alloc(fd, 0x1000, 0x00090000, i++);   /* align 9 -> 512 */
    test_alloc(fd, 0x1000, 0x000c0000, i++);   /* align 12 -> 4K */
    test_alloc(fd, 0x1000, 0x000f0000, i++);   /* align 15 -> 32K */
    test_alloc(fd, 0x1000, 0x00100000, i++);   /* align 16 -> 64K */
    test_alloc(fd, 0x1000, 0x00120000, i++);   /* align 18 -> 256K */
    test_alloc(fd, 0x1000, 0x00140000, i++);   /* align 20 -> 1M */
    test_alloc(fd, 0x1000, 0x00180000, i++);   /* align 24 */
    test_alloc(fd, 0x1000, 0x00190000, i++);   /* align 25 -> should reject */
    test_alloc(fd, 0x1000, 0x001a0000, i++);   /* align 26 */
    test_alloc(fd, 0x200000, 0x00140000, i++); /* 2M size align 20 */
    close(fd);
    return 0;
}