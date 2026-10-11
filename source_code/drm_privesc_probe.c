/*
 * drm_privesc_probe.c - DRM/MSM Driver Privilege Escalation Probe
 *
 * Explores DRM driver for potential privilege escalation vectors
 *
 * Build: aarch64-linux-android21-clang -static -O2 -o drm_privesc_probe drm_privesc_probe.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>

/* DRM IOCTL definitions */
#define DRM_IOCTL_BASE 'd'
#define DRM_COMMAND_BASE 0x40

/* DRM version */
struct drm_version
{
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

#define DRM_IOCTL_VERSION _IOWR(DRM_IOCTL_BASE, 0x00, struct drm_version)

/* DRM Auth */
struct drm_auth
{
    uint32_t magic;
};
#define DRM_IOCTL_GET_MAGIC _IOR(DRM_IOCTL_BASE, 0x02, struct drm_auth)
#define DRM_IOCTL_AUTH_MAGIC _IOW(DRM_IOCTL_BASE, 0x11, struct drm_auth)

/* DRM Client */
struct drm_client
{
    int idx;
    int auth;
    unsigned long pid;
    unsigned long uid;
    unsigned long magic;
    unsigned long iocs;
};
#define DRM_IOCTL_GET_CLIENT _IOWR(DRM_IOCTL_BASE, 0x05, struct drm_client)

/* DRM GEM */
struct drm_gem_close
{
    uint32_t handle;
    uint32_t pad;
};
#define DRM_IOCTL_GEM_CLOSE _IOW(DRM_IOCTL_BASE, 0x09, struct drm_gem_close)

struct drm_gem_flink
{
    uint32_t handle;
    uint32_t name;
};
#define DRM_IOCTL_GEM_FLINK _IOWR(DRM_IOCTL_BASE, 0x0a, struct drm_gem_flink)

struct drm_gem_open
{
    uint32_t name;
    uint32_t handle;
    uint64_t size;
};
#define DRM_IOCTL_GEM_OPEN _IOWR(DRM_IOCTL_BASE, 0x0b, struct drm_gem_open)

/* DRM Prime */
struct drm_prime_handle
{
    uint32_t handle;
    uint32_t flags;
    int32_t fd;
};
#define DRM_IOCTL_PRIME_HANDLE_TO_FD _IOWR(DRM_IOCTL_BASE, 0x2d, struct drm_prime_handle)
#define DRM_IOCTL_PRIME_FD_TO_HANDLE _IOWR(DRM_IOCTL_BASE, 0x2e, struct drm_prime_handle)

/* MSM-specific IOCTLs */
#define DRM_MSM_GET_PARAM 0x00
#define DRM_MSM_GEM_NEW 0x02
#define DRM_MSM_GEM_INFO 0x03
#define DRM_MSM_GEM_CPU_PREP 0x04
#define DRM_MSM_GEM_CPU_FINI 0x05
#define DRM_MSM_GEM_SUBMIT 0x06
#define DRM_MSM_WAIT_FENCE 0x07
#define DRM_MSM_GEM_MADVISE 0x08
#define DRM_MSM_SUBMITQUEUE_NEW 0x0A
#define DRM_MSM_SUBMITQUEUE_CLOSE 0x0B
#define DRM_MSM_SUBMITQUEUE_QUERY 0x0C

struct drm_msm_param
{
    uint32_t pipe;
    uint32_t param;
    uint64_t value;
};
#define DRM_IOCTL_MSM_GET_PARAM _IOWR(DRM_IOCTL_BASE, DRM_COMMAND_BASE + DRM_MSM_GET_PARAM, struct drm_msm_param)

struct drm_msm_gem_new
{
    uint64_t size;
    uint32_t flags;
    uint32_t handle;
};
#define DRM_IOCTL_MSM_GEM_NEW _IOWR(DRM_IOCTL_BASE, DRM_COMMAND_BASE + DRM_MSM_GEM_NEW, struct drm_msm_gem_new)

struct drm_msm_gem_info
{
    uint32_t handle;
    uint32_t info;
    uint64_t value;
    uint32_t len;
    uint32_t pad;
};
#define DRM_IOCTL_MSM_GEM_INFO _IOWR(DRM_IOCTL_BASE, DRM_COMMAND_BASE + DRM_MSM_GEM_INFO, struct drm_msm_gem_info)

/* MSM GEM flags */
#define MSM_BO_SCANOUT 0x00000001
#define MSM_BO_GPU_READONLY 0x00000002
#define MSM_BO_CACHED 0x00010000
#define MSM_BO_WC 0x00020000
#define MSM_BO_UNCACHED 0x00040000

/* MSM Params */
#define MSM_PARAM_GPU_ID 0x01
#define MSM_PARAM_GMEM_SIZE 0x02
#define MSM_PARAM_CHIP_ID 0x03
#define MSM_PARAM_MAX_FREQ 0x04
#define MSM_PARAM_TIMESTAMP 0x05
#define MSM_PARAM_GMEM_BASE 0x06
#define MSM_PARAM_PRIORITIES 0x07
#define MSM_PARAM_PP_PGTABLE 0x08
#define MSM_PARAM_FAULTS 0x09
#define MSM_PARAM_SUSPENDS 0x0a

/* Submit queue */
struct drm_msm_submitqueue
{
    uint32_t flags;
    uint32_t prio;
    uint32_t id;
};
#define DRM_IOCTL_MSM_SUBMITQUEUE_NEW _IOWR(DRM_IOCTL_BASE, DRM_COMMAND_BASE + DRM_MSM_SUBMITQUEUE_NEW, struct drm_msm_submitqueue)
#define DRM_IOCTL_MSM_SUBMITQUEUE_CLOSE _IOW(DRM_IOCTL_BASE, DRM_COMMAND_BASE + DRM_MSM_SUBMITQUEUE_CLOSE, uint32_t)

/*========== HELPER FUNCTIONS ==========*/

void print_hex(const char *prefix, void *data, size_t len)
{
    printf("%s: ", prefix);
    unsigned char *p = (unsigned char *)data;
    for (size_t i = 0; i < len && i < 32; i++)
    {
        printf("%02x ", p[i]);
    }
    printf("\n");
}

/*========== DRM TESTS ==========*/

void test_drm_version(int fd)
{
    printf("\n=== DRM VERSION ===\n");

    char name[128] = {0};
    char date[128] = {0};
    char desc[256] = {0};

    struct drm_version ver = {0};
    ver.name_len = sizeof(name);
    ver.name = name;
    ver.date_len = sizeof(date);
    ver.date = date;
    ver.desc_len = sizeof(desc);
    ver.desc = desc;

    if (ioctl(fd, DRM_IOCTL_VERSION, &ver) == 0)
    {
        printf("[+] DRM Driver: %s v%d.%d.%d\n", name,
               ver.version_major, ver.version_minor, ver.version_patchlevel);
        printf("[+] Date: %s\n", date);
        printf("[+] Desc: %s\n", desc);
    }
    else
    {
        printf("[-] DRM_IOCTL_VERSION failed: %s\n", strerror(errno));
    }
}

void test_drm_client(int fd)
{
    printf("\n=== DRM CLIENT INFO ===\n");

    for (int i = 0; i < 5; i++)
    {
        struct drm_client client = {0};
        client.idx = i;

        if (ioctl(fd, DRM_IOCTL_GET_CLIENT, &client) == 0)
        {
            printf("[+] Client %d: auth=%d pid=%lu uid=%lu magic=%lu\n",
                   i, client.auth, client.pid, client.uid, client.magic);
        }
    }
}

void test_drm_auth(int fd)
{
    printf("\n=== DRM AUTH ===\n");

    struct drm_auth auth = {0};
    if (ioctl(fd, DRM_IOCTL_GET_MAGIC, &auth) == 0)
    {
        printf("[+] Magic token: 0x%x\n", auth.magic);

        /* Try to self-authenticate */
        if (ioctl(fd, DRM_IOCTL_AUTH_MAGIC, &auth) == 0)
        {
            printf("[!] SELF-AUTH SUCCESS! (potential vuln)\n");
        }
        else
        {
            printf("[-] Self-auth failed: %s\n", strerror(errno));
        }
    }
    else
    {
        printf("[-] GET_MAGIC failed: %s\n", strerror(errno));
    }
}

void test_msm_params(int fd)
{
    printf("\n=== MSM PARAMS ===\n");

    struct
    {
        uint32_t id;
        const char *name;
    } params[] = {
        {MSM_PARAM_GPU_ID, "GPU_ID"},
        {MSM_PARAM_GMEM_SIZE, "GMEM_SIZE"},
        {MSM_PARAM_CHIP_ID, "CHIP_ID"},
        {MSM_PARAM_MAX_FREQ, "MAX_FREQ"},
        {MSM_PARAM_TIMESTAMP, "TIMESTAMP"},
        {MSM_PARAM_GMEM_BASE, "GMEM_BASE"},
        {MSM_PARAM_PRIORITIES, "PRIORITIES"},
        {MSM_PARAM_PP_PGTABLE, "PP_PGTABLE"},
        {MSM_PARAM_FAULTS, "FAULTS"},
        {MSM_PARAM_SUSPENDS, "SUSPENDS"},
    };

    for (size_t i = 0; i < sizeof(params) / sizeof(params[0]); i++)
    {
        struct drm_msm_param p = {0};
        p.param = params[i].id;

        if (ioctl(fd, DRM_IOCTL_MSM_GET_PARAM, &p) == 0)
        {
            printf("[+] %s: 0x%lx\n", params[i].name, (unsigned long)p.value);
        }
        else
        {
            printf("[-] %s: %s\n", params[i].name, strerror(errno));
        }
    }
}

void test_msm_gem_alloc(int fd)
{
    printf("\n=== MSM GEM ALLOCATION ===\n");

    struct drm_msm_gem_new gem = {0};
    gem.size = 0x10000; /* 64KB */
    gem.flags = MSM_BO_WC;

    if (ioctl(fd, DRM_IOCTL_MSM_GEM_NEW, &gem) == 0)
    {
        printf("[+] GEM allocated: handle=%u, size=%lu\n",
               gem.handle, (unsigned long)gem.size);

        /* Get info */
        struct drm_msm_gem_info info = {0};
        info.handle = gem.handle;
        info.info = 0; /* MSM_INFO_GET_OFFSET */

        if (ioctl(fd, DRM_IOCTL_MSM_GEM_INFO, &info) == 0)
        {
            printf("[+] GEM offset: 0x%lx\n", (unsigned long)info.value);

            /* mmap the buffer */
            void *ptr = mmap(NULL, gem.size, PROT_READ | PROT_WRITE,
                             MAP_SHARED, fd, info.value);
            if (ptr != MAP_FAILED)
            {
                printf("[+] mmap: %p\n", ptr);

                /* Write and read */
                memset(ptr, 0x55, 64);
                unsigned char *bytes = (unsigned char *)ptr;
                printf("[+] Write/read OK: %02x %02x %02x %02x\n",
                       bytes[0], bytes[1], bytes[2], bytes[3]);

                /* Check for leaked data in rest of buffer */
                int leaked = 0;
                for (size_t i = 64; i < gem.size && i < 4096; i++)
                {
                    if (bytes[i] != 0)
                    {
                        leaked++;
                    }
                }
                if (leaked > 0)
                {
                    printf("[!] Found %d non-zero bytes (potential info leak!)\n", leaked);
                    print_hex("Sample", ptr + 64, 32);
                }
                else
                {
                    printf("[*] Buffer is zeroed\n");
                }

                munmap(ptr, gem.size);
            }
            else
            {
                printf("[-] mmap failed: %s\n", strerror(errno));
            }
        }

        /* Export to DMA-BUF fd */
        struct drm_prime_handle prime = {0};
        prime.handle = gem.handle;
        prime.flags = O_RDWR;

        if (ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime) == 0)
        {
            printf("[+] PRIME export: fd=%d\n", prime.fd);

            /* mmap via PRIME fd */
            void *prime_ptr = mmap(NULL, gem.size, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, prime.fd, 0);
            if (prime_ptr != MAP_FAILED)
            {
                printf("[+] PRIME mmap: %p\n", prime_ptr);
                munmap(prime_ptr, gem.size);
            }
            close(prime.fd);
        }
        else
        {
            printf("[-] PRIME export failed: %s\n", strerror(errno));
        }

        /* Clean up */
        struct drm_gem_close close_req = {0};
        close_req.handle = gem.handle;
        ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close_req);
    }
    else
    {
        printf("[-] GEM_NEW failed: %s (errno=%d)\n", strerror(errno), errno);
    }
}

void test_msm_submitqueue(int fd)
{
    printf("\n=== MSM SUBMITQUEUE ===\n");

    struct drm_msm_submitqueue sq = {0};
    sq.flags = 0;
    sq.prio = 0;

    if (ioctl(fd, DRM_IOCTL_MSM_SUBMITQUEUE_NEW, &sq) == 0)
    {
        printf("[+] Submitqueue created: id=%u\n", sq.id);

        /* Try different priorities */
        for (int prio = 0; prio < 4; prio++)
        {
            sq.prio = prio;
            sq.id = 0;
            if (ioctl(fd, DRM_IOCTL_MSM_SUBMITQUEUE_NEW, &sq) == 0)
            {
                printf("[+] Queue prio=%d: id=%u\n", prio, sq.id);
                ioctl(fd, DRM_IOCTL_MSM_SUBMITQUEUE_CLOSE, &sq.id);
            }
        }

        /* Close original */
        ioctl(fd, DRM_IOCTL_MSM_SUBMITQUEUE_CLOSE, &sq.id);
    }
    else
    {
        printf("[-] SUBMITQUEUE_NEW failed: %s\n", strerror(errno));
    }
}

void test_render_node(int fd)
{
    printf("\n=== RENDER NODE TEST ===\n");

    /* Try to allocate and use directly */
    struct drm_msm_gem_new gem = {0};
    gem.size = 0x1000;
    gem.flags = MSM_BO_CACHED;

    if (ioctl(fd, DRM_IOCTL_MSM_GEM_NEW, &gem) == 0)
    {
        printf("[+] Render node GEM OK: handle=%u\n", gem.handle);

        /* Cleanup */
        struct drm_gem_close c = {.handle = gem.handle};
        ioctl(fd, DRM_IOCTL_GEM_CLOSE, &c);
    }
    else
    {
        printf("[-] Render node GEM failed: %s\n", strerror(errno));
    }
}

void test_cross_fd_gem()
{
    printf("\n=== CROSS-FD GEM TEST ===\n");

    int fd1 = open("/dev/dri/card0", O_RDWR);
    int fd2 = open("/dev/dri/card0", O_RDWR);

    if (fd1 < 0 || fd2 < 0)
    {
        printf("[-] Failed to open two DRM fds\n");
        if (fd1 >= 0)
            close(fd1);
        if (fd2 >= 0)
            close(fd2);
        return;
    }

    printf("[+] fd1=%d, fd2=%d\n", fd1, fd2);

    /* Allocate on fd1 */
    struct drm_msm_gem_new gem = {0};
    gem.size = 0x1000;
    gem.flags = MSM_BO_WC;

    if (ioctl(fd1, DRM_IOCTL_MSM_GEM_NEW, &gem) != 0)
    {
        printf("[-] fd1 GEM_NEW failed\n");
        close(fd1);
        close(fd2);
        return;
    }

    printf("[+] fd1 allocated handle=%u\n", gem.handle);

    /* Export to PRIME fd */
    struct drm_prime_handle prime = {0};
    prime.handle = gem.handle;
    prime.flags = O_RDWR;

    if (ioctl(fd1, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime) != 0)
    {
        printf("[-] PRIME export failed\n");
        close(fd1);
        close(fd2);
        return;
    }

    int dma_fd = prime.fd;
    printf("[+] PRIME fd=%d\n", dma_fd);

    /* Import on fd2 */
    struct drm_prime_handle import = {0};
    import.fd = dma_fd;

    if (ioctl(fd2, DRM_IOCTL_PRIME_FD_TO_HANDLE, &import) == 0)
    {
        printf("[+] fd2 imported: handle=%u\n", import.handle);

        /* Get offset for mmap on fd2 */
        struct drm_msm_gem_info info = {0};
        info.handle = import.handle;

        if (ioctl(fd2, DRM_IOCTL_MSM_GEM_INFO, &info) == 0)
        {
            void *ptr2 = mmap(NULL, gem.size, PROT_READ | PROT_WRITE,
                              MAP_SHARED, fd2, info.value);
            if (ptr2 != MAP_FAILED)
            {
                printf("[+] fd2 mmap: %p\n", ptr2);

                /* Write on fd2, read on fd1's mmap */
                memset(ptr2, 0xAA, 64);

                /* mmap on fd1 */
                info.handle = gem.handle;
                if (ioctl(fd1, DRM_IOCTL_MSM_GEM_INFO, &info) == 0)
                {
                    void *ptr1 = mmap(NULL, gem.size, PROT_READ | PROT_WRITE,
                                      MAP_SHARED, fd1, info.value);
                    if (ptr1 != MAP_FAILED)
                    {
                        printf("[+] fd1 mmap: %p\n", ptr1);
                        unsigned char *bytes = (unsigned char *)ptr1;
                        if (bytes[0] == 0xAA)
                        {
                            printf("[!] CROSS-FD MEMORY SHARING CONFIRMED!\n");
                        }
                        munmap(ptr1, gem.size);
                    }
                }

                munmap(ptr2, gem.size);
            }
        }

        /* Cleanup fd2 handle */
        struct drm_gem_close c = {.handle = import.handle};
        ioctl(fd2, DRM_IOCTL_GEM_CLOSE, &c);
    }
    else
    {
        printf("[-] fd2 import failed: %s\n", strerror(errno));
    }

    /* Cleanup */
    close(dma_fd);
    struct drm_gem_close c = {.handle = gem.handle};
    ioctl(fd1, DRM_IOCTL_GEM_CLOSE, &c);
    close(fd1);
    close(fd2);
}

/*========== MAIN ==========*/

int main(int argc, char *argv[])
{
    printf("=== DRM/MSM Privilege Escalation Probe ===\n");

    /* Try card0 first, then renderD128 */
    const char *devices[] = {"/dev/dri/card0", "/dev/dri/renderD128"};

    for (int i = 0; i < 2; i++)
    {
        printf("\n========== Testing %s ==========\n", devices[i]);

        int fd = open(devices[i], O_RDWR);
        if (fd < 0)
        {
            printf("[-] Cannot open %s: %s\n", devices[i], strerror(errno));
            continue;
        }

        printf("[+] Opened %s: fd=%d\n", devices[i], fd);

        test_drm_version(fd);
        test_drm_client(fd);
        test_drm_auth(fd);
        test_msm_params(fd);
        test_msm_gem_alloc(fd);
        test_msm_submitqueue(fd);

        if (i == 1)
        {
            test_render_node(fd);
        }

        close(fd);
    }

    /* Cross-fd test */
    test_cross_fd_gem();

    printf("\n=== SUMMARY ===\n");
    printf("[*] DRM/MSM driver fully accessible\n");
    printf("[*] Check for info leaks, use-after-free, or auth bypass\n");

    return 0;
}
