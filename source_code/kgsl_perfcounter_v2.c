/*
 * kgsl_perfcounter_v2.c - retrying perfcounter with a corrected structure
 *
 * Findings:
 * - after sysfs perfcounter=1, errno changed from EPERM to EINVAL
 * - this means the permission check passed but the structure is wrong
 *
 * Build:
 * aarch64-linux-android31-clang -static -O2 -o kgsl_perfcounter_v2 kgsl_perfcounter_v2.c
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

/* different structure versions to try */

void write_sysfs(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY);
    if (fd >= 0)
    {
        write(fd, value, strlen(value));
        close(fd);
    }
}

int g_fd = -1;

void try_perfcounter_get_formats(int fd)
{
    printf("\n=== Trying Different PERFCOUNTER_GET Formats ===\n");

    // Make sure perfcounter is enabled
    write_sysfs("/sys/class/kgsl/kgsl-3d0/perfcounter", "1");

    // Format 1: only groupid and countable (8 bytes)
    struct
    {
        uint32_t groupid;
        uint32_t countable;
    } fmt1 = {0, 0};

    errno = 0;
    int ret = ioctl(fd, 0xc008093b, &fmt1);
    printf("[*] Format 1 (8B): ret=%d, errno=%d\n", ret, errno);

    // Format 2: groupid, countable, offset (12 bytes)
    struct
    {
        uint32_t groupid;
        uint32_t countable;
        uint32_t offset;
    } fmt2 = {0, 0, 0};

    errno = 0;
    ret = ioctl(fd, 0xc00c093b, &fmt2);
    printf("[*] Format 2 (12B): ret=%d, errno=%d\n", ret, errno);
    if (ret == 0)
    {
        printf("    offset=0x%x\n", fmt2.offset);
    }

    // Format 3: groupid, countable, offset, offset_hi (16 bytes)
    struct
    {
        uint32_t groupid;
        uint32_t countable;
        uint32_t offset;
        uint32_t offset_hi;
    } fmt3 = {0, 0, 0, 0};

    errno = 0;
    ret = ioctl(fd, 0xc010093b, &fmt3);
    printf("[*] Format 3 (16B): ret=%d, errno=%d\n", ret, errno);
    if (ret == 0)
    {
        printf("    offset=0x%x, offset_hi=0x%x\n", fmt3.offset, fmt3.offset_hi);
    }

    // Format 4: add flags (20 bytes)
    struct
    {
        uint32_t groupid;
        uint32_t countable;
        uint32_t offset;
        uint32_t offset_hi;
        uint32_t flags;
    } fmt4 = {0, 0, 0, 0, 0};

    errno = 0;
    ret = ioctl(fd, 0xc014093b, &fmt4);
    printf("[*] Format 4 (20B): ret=%d, errno=%d\n", ret, errno);

    // Try different ioctl number variants
    printf("\n[*] Trying different ioctl numbers for PERFCOUNTER:\n");

    for (int size = 8; size <= 32; size += 4)
    {
        for (int nr = 0x3b; nr <= 0x3e; nr++)
        {
            int cmd = 0xc0000000 | (size << 16) | (0x09 << 8) | nr;

            uint32_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};

            errno = 0;
            ret = ioctl(fd, cmd, data);

            if (ret == 0)
            {
                printf("[+] cmd=0x%08x (nr=0x%x, size=%d): SUCCESS!\n",
                       cmd, nr, size);
                printf("    data: %u %u %u %u\n",
                       data[0], data[1], data[2], data[3]);
            }
        }
    }
}

void try_perfcounter_query_formats(int fd)
{
    printf("\n=== Trying Different PERFCOUNTER_QUERY Formats ===\n");

    // nr=0x46 is PERFCOUNTER_QUERY

    // Format 1: only groupid
    struct
    {
        uint32_t groupid;
    } q1 = {0};

    errno = 0;
    int ret = ioctl(fd, 0xc0040946, &q1);
    printf("[*] Query format 1 (4B): ret=%d, errno=%d\n", ret, errno);

    // Format 2: groupid + count pointer
    struct
    {
        uint32_t groupid;
        uint32_t *countables;
        uint32_t count;
        uint32_t max_counters;
    } q2 = {0};

    uint32_t count_buf[256] = {0};
    q2.groupid = 0;
    q2.countables = count_buf;
    q2.count = 256;

    errno = 0;
    ret = ioctl(fd, 0xc0100946, &q2);
    printf("[*] Query format 2 (16B): ret=%d, errno=%d\n", ret, errno);
    if (ret == 0)
    {
        printf("    count=%u, max_counters=%u\n", q2.count, q2.max_counters);
    }

    // Format 3: a different structure
    struct
    {
        uint32_t groupid;
        uint32_t reserved;
        uint64_t countables_ptr;
        uint32_t count;
        uint32_t max_counters;
    } q3 = {0};

    q3.groupid = 0;
    q3.countables_ptr = (uint64_t)count_buf;
    q3.count = 256;

    errno = 0;
    ret = ioctl(fd, 0xc0180946, &q3);
    printf("[*] Query format 3 (24B): ret=%d, errno=%d\n", ret, errno);
}

void explore_all_perfcounter_ioctls(int fd)
{
    printf("\n=== Exploring All Perfcounter-related ioctls ===\n");

    /*
     * Possible perfcounter ioctls:
     * 0x3b - PERFCOUNTER_GET
     * 0x3c - PERFCOUNTER_PUT
     * 0x46 - PERFCOUNTER_QUERY
     * 0x47 - PERFCOUNTER_READ
     */

    uint32_t data[16] = {0};

    // Test 0x3c (PUT)
    data[0] = 0; // groupid
    data[1] = 0; // countable

    for (int size = 8; size <= 16; size += 4)
    {
        int cmd = 0xc0000000 | (size << 16) | (0x09 << 8) | 0x3c;

        errno = 0;
        int ret = ioctl(fd, cmd, data);

        if (ret == 0 || errno != 22)
        {
            printf("[*] PERFCOUNTER_PUT size=%d: ret=%d, errno=%d\n",
                   size, ret, errno);
        }
    }

    // Test 0x47 (READ?)
    for (int size = 8; size <= 32; size += 4)
    {
        int cmd = 0xc0000000 | (size << 16) | (0x09 << 8) | 0x47;

        memset(data, 0, sizeof(data));
        errno = 0;
        int ret = ioctl(fd, cmd, data);

        if (ret == 0 || (errno != 22 && errno != 25))
        {
            printf("[*] ioctl 0x47 size=%d: ret=%d, errno=%d\n",
                   size, ret, errno);
        }
    }
}

void test_with_different_groupids(int fd)
{
    printf("\n=== Testing with Different Group IDs ===\n");

    // Adreno 6xx may have the following groups:
    // 0: CP (Command Processor)
    // 1: RBBM
    // 2: PC
    // 3: VFD
    // 4: HLSQ
    // 5: VPC
    // 6: TSE
    // ...

    struct
    {
        uint32_t groupid;
        uint32_t countable;
        uint32_t offset;
        uint32_t offset_hi;
    } req = {0};

    // Use the correct ioctl size
    for (int grp = 0; grp < 32; grp++)
    {
        req.groupid = grp;
        req.countable = 0;
        req.offset = 0;
        req.offset_hi = 0;

        errno = 0;
        int ret = ioctl(fd, 0xc010093b, &req);

        if (ret == 0)
        {
            printf("[+] Group %d: SUCCESS! offset=0x%x\n", grp, req.offset);
        }
        else if (errno != 22)
        {
            printf("[?] Group %d: errno=%d (%s)\n", grp, errno, strerror(errno));
        }
    }
}

int main(int argc, char **argv)
{
    printf("=== KGSL Perfcounter V2 ===\n");

    g_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (g_fd < 0)
    {
        printf("[-] Cannot open: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Opened fd=%d\n", g_fd);

    // Make sure sysfs is enabled
    write_sysfs("/sys/class/kgsl/kgsl-3d0/perfcounter", "1");

    try_perfcounter_get_formats(g_fd);
    try_perfcounter_query_formats(g_fd);
    explore_all_perfcounter_ioctls(g_fd);
    test_with_different_groupids(g_fd);

    close(g_fd);
    return 0;
}
