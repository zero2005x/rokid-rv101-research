#define _GNU_SOURCE
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <sys/ioctl.h>

#define KGSL_GPUMEM_ALLOC   0xc0180920
#define KGSL_GPUMEM_FREE_ID 0x40080921

struct kgsl_gpumem_alloc {
    uint64_t size;
    uint32_t flags;
    uint32_t padding;
};
struct kgsl_gpumem_free_id {
    uint32_t id;
};

static volatile int crashed = 0;
static void h(int s){ crashed = s; printf("[!] SIGNAL %d\n", s); }

static void test(int fd, uint64_t size, uint32_t flags, int i) {
    struct kgsl_gpumem_alloc a;
    memset(&a, 0, sizeof(a));
    a.size = size;
    a.flags = flags;
    errno = 0;
    int r = ioctl(fd, KGSL_GPUMEM_ALLOC, &a);
    printf("[%02d] GPUMEM_ALLOC size=0x%llx flags=0x%08x align=%u -> r=%d errno=%d(%s)\n",
           i, (unsigned long long)size, flags, (flags>>16)&0xff,
           r, errno, strerror(errno));
    if (r == 0) {
        struct kgsl_gpumem_free_id f;
        f.id = a.size; /* note: id returned? struct only has size/flags/padding - actual id via GPUMEM_ALLOC_ID? */
        ioctl(fd, KGSL_GPUMEM_FREE_ID, &f);
    }
}

int main(void) {
    signal(SIGSEGV, h); signal(SIGBUS, h); signal(SIGILL, h);
    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    printf("opened fd=%d\n", fd);
    int i = 0;
    test(fd, 0x1000, 0, i++);
    test(fd, 0x1000, 12<<16, i++);
    test(fd, 0x1000, 20<<16, i++);
    test(fd, 0x1000, 25<<16, i++);
    test(fd, 0x1000, 32<<16, i++);
    test(fd, 0x1000, 63<<16, i++);
    test(fd, 0x1000, 128<<16, i++);
    test(fd, 0x1000, 255<<16, i++);
    printf("crashed=%d\n", crashed);
    close(fd);
    return 0;
}