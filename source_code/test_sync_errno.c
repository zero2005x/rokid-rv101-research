#define _GNU_SOURCE
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define IOCTL_KGSL_SYNCSOURCE_CREATE       0xc0080930
#define IOCTL_KGSL_SYNCSOURCE_CREATE_FENCE 0xc0180932
#define IOCTL_KGSL_SYNCSOURCE_DESTROY      0x40040931

struct kgsl_syncsource_create { uint32_t id; uint32_t padding; };
struct kgsl_syncsource_create_fence { uint32_t id; uint32_t padding; uint64_t fence_fd; };
struct kgsl_syncsource_destroy { uint32_t id; };

int main(void) {
    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    printf("opened fd=%d\n", fd);

    struct kgsl_syncsource_create c;
    memset(&c, 0, sizeof(c));
    errno = 0;
    int r = ioctl(fd, IOCTL_KGSL_SYNCSOURCE_CREATE, &c);
    printf("CREATE: r=%d errno=%d(%s) id=%u\n", r, errno, strerror(errno), c.id);

    if (r == 0) {
        struct kgsl_syncsource_create_fence f;
        memset(&f, 0, sizeof(f));
        f.id = c.id;
        errno = 0;
        r = ioctl(fd, IOCTL_KGSL_SYNCSOURCE_CREATE_FENCE, &f);
        printf("FENCE: r=%d errno=%d(%s) ffd=%llu\n", r, errno, strerror(errno),
               (unsigned long long)f.fence_fd);

        struct kgsl_syncsource_destroy d;
        d.id = c.id;
        ioctl(fd, IOCTL_KGSL_SYNCSOURCE_DESTROY, &d);
    }
    close(fd);
    return 0;
}