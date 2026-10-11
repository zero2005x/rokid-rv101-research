/*
 * Alternative Attack Surface Exploration
 * =======================================
 *
 * Since KGSL mmap requires GPU context (which is blocked),
 * let's explore other potential attack vectors:
 *
 * 1. DRI/DRM subsystem - /dev/dri/card0, /dev/dri/renderD128
 * 2. Ashmem - Android shared memory
 * 3. ION legacy heap
 * 4. Binder exploitation
 * 5. Any accessible /dev/* interfaces
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <dirent.h>
#include <sys/stat.h>
#include <signal.h>
#include <setjmp.h>

static sigjmp_buf g_jmpbuf;
static volatile sig_atomic_t g_faulted = 0;

void fault_handler(int sig) {
    g_faulted = 1;
    siglongjmp(g_jmpbuf, 1);
}

void setup_handler(void) {
    struct sigaction sa;
    sa.sa_handler = fault_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
}

// ═══════════════════════════════════════════════════════════════════════════
// DRI/DRM Exploration
// ═══════════════════════════════════════════════════════════════════════════

// DRM IOCTL definitions
#define DRM_IOCTL_BASE 'd'
#define DRM_IOCTL_VERSION      _IOWR(DRM_IOCTL_BASE, 0x00, struct drm_version)
#define DRM_IOCTL_GET_CAP      _IOWR(DRM_IOCTL_BASE, 0x0c, struct drm_get_cap)

struct drm_version {
    int version_major;
    int version_minor;
    int version_patchlevel;
    size_t name_len;
    char *name;
    size_t date_len;
    char *date;
    size_t desc_len;
    char *desc;
};

struct drm_get_cap {
    uint64_t capability;
    uint64_t value;
};

void explore_drm(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  DRI/DRM Subsystem Exploration                                    ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");
    
    const char *devs[] = {"/dev/dri/card0", "/dev/dri/renderD128"};
    
    for (int d = 0; d < 2; d++) {
        printf("[*] Trying %s...\n", devs[d]);
        
        int fd = open(devs[d], O_RDWR);
        if (fd < 0) {
            printf("[-] Failed to open: %s\n\n", strerror(errno));
            continue;
        }
        printf("[+] Opened fd=%d\n", fd);
        
        // Get version
        char name[256] = {0};
        char date[256] = {0};
        char desc[256] = {0};
        
        struct drm_version ver = {
            .name_len = sizeof(name) - 1,
            .name = name,
            .date_len = sizeof(date) - 1,
            .date = date,
            .desc_len = sizeof(desc) - 1,
            .desc = desc,
        };
        
        if (ioctl(fd, DRM_IOCTL_VERSION, &ver) == 0) {
            printf("    Version: %d.%d.%d\n", 
                   ver.version_major, ver.version_minor, ver.version_patchlevel);
            printf("    Name:    %s\n", name);
            printf("    Date:    %s\n", date);
            printf("    Desc:    %s\n", desc);
        }
        
        // Get capabilities
        printf("\n    Capabilities:\n");
        
        for (uint64_t cap = 0x1; cap <= 0x20; cap++) {
            struct drm_get_cap gc = {.capability = cap};
            if (ioctl(fd, DRM_IOCTL_GET_CAP, &gc) == 0 && gc.value != 0) {
                printf("    Cap 0x%02llx = 0x%llx\n",
                       (unsigned long long)cap, (unsigned long long)gc.value);
            }
        }
        
        // Try mmap
        printf("\n    Testing mmap...\n");
        
        off_t offsets[] = {0, 0x1000, 0x10000};
        for (int i = 0; i < 3; i++) {
            void *ptr = mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, fd, offsets[i]);
            if (ptr != MAP_FAILED) {
                printf("    [+] Offset 0x%llx: SUCCESS at %p\n",
                       (unsigned long long)offsets[i], ptr);
                munmap(ptr, 0x1000);
            }
        }
        
        close(fd);
        printf("\n");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Ashmem Exploration
// ═══════════════════════════════════════════════════════════════════════════

#define ASHMEM_NAME_LEN 256
#define ASHMEM_SET_NAME      _IOW(0x77, 1, char[ASHMEM_NAME_LEN])
#define ASHMEM_SET_SIZE      _IOW(0x77, 3, size_t)
#define ASHMEM_GET_SIZE      _IO(0x77, 4)

void explore_ashmem(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  Ashmem (Android Shared Memory) Exploration                       ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");
    
    int fd = open("/dev/ashmem", O_RDWR);
    if (fd < 0) {
        printf("[-] Cannot open /dev/ashmem: %s\n", strerror(errno));
        return;
    }
    printf("[+] Opened /dev/ashmem fd=%d\n", fd);
    
    // Set name
    if (ioctl(fd, ASHMEM_SET_NAME, "test_region") != 0) {
        printf("[-] Failed to set name: %s\n", strerror(errno));
    }
    
    // Set size
    size_t size = 4096 * 16;  // 64KB
    if (ioctl(fd, ASHMEM_SET_SIZE, size) != 0) {
        printf("[-] Failed to set size: %s\n", strerror(errno));
        close(fd);
        return;
    }
    printf("[+] Set size to %zu\n", size);
    
    // Get size back
    size_t actual = ioctl(fd, ASHMEM_GET_SIZE);
    printf("[+] Actual size: %zu\n", actual);
    
    // mmap
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    
    if (ptr == MAP_FAILED) {
        printf("[-] mmap failed: %s\n", strerror(errno));
        close(fd);
        return;
    }
    printf("[+] mmap succeeded at %p\n", ptr);
    
    // Write and read
    uint32_t *p = (uint32_t *)ptr;
    p[0] = 0xDEADBEEF;
    p[1] = 0xCAFEBABE;
    
    printf("    Write test: %s\n", 
           (p[0] == 0xDEADBEEF && p[1] == 0xCAFEBABE) ? "SUCCESS" : "FAILED");
    
    // Try to create large allocation for heap spray
    printf("\n[*] Testing large allocations for heap spray...\n");
    
    for (int i = 0; i < 5; i++) {
        size_t spray_size = (1 << 20) * (i + 1);  // 1MB, 2MB, ...
        
        int spray_fd = open("/dev/ashmem", O_RDWR);
        if (spray_fd < 0) break;
        
        if (ioctl(spray_fd, ASHMEM_SET_SIZE, spray_size) == 0) {
            void *spray_ptr = mmap(NULL, spray_size, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, spray_fd, 0);
            if (spray_ptr != MAP_FAILED) {
                printf("    [+] Allocated %zu MB at %p\n", spray_size / (1 << 20), spray_ptr);
                munmap(spray_ptr, spray_size);
            }
        }
        close(spray_fd);
    }
    
    munmap(ptr, size);
    close(fd);
}

// ═══════════════════════════════════════════════════════════════════════════
// Scan /dev for accessible devices
// ═══════════════════════════════════════════════════════════════════════════

void scan_dev_devices(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  Scanning /dev for Accessible Devices                             ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");
    
    // Key devices to check
    const char *devices[] = {
        // GPU
        "/dev/kgsl-3d0",
        "/dev/kgsl",
        "/dev/dri/card0",
        "/dev/dri/renderD128",
        // Memory
        "/dev/ashmem",
        "/dev/ion",
        "/dev/mem",
        "/dev/kmem",
        // Binder
        "/dev/binder",
        "/dev/hwbinder",
        "/dev/vndbinder",
        // Misc interesting
        "/dev/diag",
        "/dev/qseecom",
        "/dev/msm_rotator",
        "/dev/video0",
        "/dev/video1",
        "/dev/media0",
        "/dev/snd/controlC0",
        // USB/Gadget
        "/dev/usb_accessory",
        "/dev/usb/lp0",
        // Debug
        "/dev/log/main",
        "/dev/log/system",
        "/dev/log/radio",
        // Crypto
        "/dev/qce",
        "/dev/msm_qcrypto",
        // DSP
        "/dev/adsprpc-smd",
        "/dev/adsprpc-smd-secure",
        // Camera
        "/dev/v4l-subdev0",
        // Input
        "/dev/input/event0",
        "/dev/input/event1",
        // Misc
        "/dev/ipa",
        "/dev/rtc0",
        "/dev/watchdog",
        "/dev/tty",
        "/dev/ptmx",
        "/dev/null",
        "/dev/zero",
        "/dev/random",
        "/dev/urandom",
    };
    
    printf("[*] Checking %zu devices...\n\n", sizeof(devices)/sizeof(devices[0]));
    
    int accessible = 0;
    
    for (int i = 0; i < sizeof(devices)/sizeof(devices[0]); i++) {
        struct stat st;
        
        if (stat(devices[i], &st) != 0) {
            continue;  // Doesn't exist
        }
        
        int fd = open(devices[i], O_RDWR);
        if (fd >= 0) {
            printf("[+] %s: RW accessible\n", devices[i]);
            accessible++;
            
            // Try some basic operations
            char buf[64];
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n > 0) {
                printf("    read() returned %zd bytes\n", n);
            }
            
            close(fd);
        } else {
            fd = open(devices[i], O_RDONLY);
            if (fd >= 0) {
                printf("[*] %s: RO accessible\n", devices[i]);
                accessible++;
                close(fd);
            }
        }
    }
    
    printf("\n[*] Total accessible: %d devices\n", accessible);
}

// ═══════════════════════════════════════════════════════════════════════════
// Binder Exploration
// ═══════════════════════════════════════════════════════════════════════════

// Binder IOCTL definitions
#define BINDER_WRITE_READ         _IOWR('b', 1, struct binder_write_read)
#define BINDER_VERSION            _IOWR('b', 9, struct binder_version)

struct binder_version {
    int32_t protocol_version;
};

struct binder_write_read {
    size_t write_size;
    size_t write_consumed;
    unsigned long write_buffer;
    size_t read_size;
    size_t read_consumed;
    unsigned long read_buffer;
};

void explore_binder(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  Binder Exploration                                               ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");
    
    const char *binders[] = {"/dev/binder", "/dev/hwbinder", "/dev/vndbinder"};
    
    for (int i = 0; i < 3; i++) {
        printf("[*] Trying %s...\n", binders[i]);
        
        int fd = open(binders[i], O_RDWR);
        if (fd < 0) {
            printf("[-] Cannot open: %s\n\n", strerror(errno));
            continue;
        }
        printf("[+] Opened fd=%d\n", fd);
        
        // Get version
        struct binder_version ver = {0};
        if (ioctl(fd, BINDER_VERSION, &ver) == 0) {
            printf("    Protocol version: %d\n", ver.protocol_version);
        }
        
        // Try mmap (binder requires mmap before use)
        size_t mmap_size = 1024 * 1024;  // 1MB
        void *ptr = mmap(NULL, mmap_size, PROT_READ, MAP_PRIVATE, fd, 0);
        
        if (ptr != MAP_FAILED) {
            printf("    mmap(%zuKB): SUCCESS at %p\n", mmap_size / 1024, ptr);
            munmap(ptr, mmap_size);
        } else {
            printf("    mmap: %s\n", strerror(errno));
        }
        
        close(fd);
        printf("\n");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ADSP RPC Exploration (if accessible)
// ═══════════════════════════════════════════════════════════════════════════

void explore_adsprpc(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  ADSP RPC Exploration                                             ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");
    
    const char *devs[] = {
        "/dev/adsprpc-smd",
        "/dev/adsprpc-smd-secure",
        "/dev/cdsp",
        "/dev/slpi",
    };
    
    for (int i = 0; i < sizeof(devs)/sizeof(devs[0]); i++) {
        int fd = open(devs[i], O_RDWR);
        if (fd >= 0) {
            printf("[+] %s: Opened fd=%d\n", devs[i], fd);
            
            // FASTRPC IOCTLs could be explored here
            // These are complex and require proper structures
            
            close(fd);
        } else if (errno != ENOENT) {
            printf("[-] %s: %s\n", devs[i], strerror(errno));
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Procfs/Sysfs Information Gathering
// ═══════════════════════════════════════════════════════════════════════════

void gather_proc_info(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  Procfs/Sysfs Information                                         ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");
    
    const char *paths[] = {
        "/proc/version",
        "/proc/kallsyms",
        "/proc/modules",
        "/proc/iomem",
        "/sys/kernel/debug/ion/heaps/system",
        "/sys/kernel/debug/kgsl/proc/0/mem",
        "/proc/self/maps",
        "/proc/self/status",
    };
    
    for (int i = 0; i < sizeof(paths)/sizeof(paths[0]); i++) {
        int fd = open(paths[i], O_RDONLY);
        if (fd >= 0) {
            char buf[512];
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                
                // Truncate long output
                char *nl = strchr(buf, '\n');
                if (nl && nl < buf + 80) *nl = '\0';
                else buf[80] = '\0';
                
                printf("[+] %s:\n    %s%s\n\n", paths[i], buf, 
                       n >= 80 ? "..." : "");
            }
            close(fd);
        } else if (errno != ENOENT) {
            printf("[-] %s: %s\n", paths[i], strerror(errno));
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

int main(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║  Alternative Attack Surface Exploration                            ║\n");
    printf("║  Looking beyond KGSL for privilege escalation vectors             ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n\n");
    
    printf("[*] UID=%d, EUID=%d, PID=%d\n", getuid(), geteuid(), getpid());
    
    setup_handler();
    
    explore_drm();
    explore_ashmem();
    explore_binder();
    explore_adsprpc();
    scan_dev_devices();
    gather_proc_info();
    
    printf("\n[*] Exploration complete\n\n");
    
    return 0;
}
