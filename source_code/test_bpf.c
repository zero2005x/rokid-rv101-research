#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/syscall.h>

int main(void) {
    /* bpf(BPF_MAP_CREATE, ...) */
    union bpf_attr {
        struct {
            uint32_t map_type;
            uint32_t key_size;
            uint32_t value_size;
            uint32_t max_entries;
            uint32_t map_flags;
            uint32_t inner_map_fd;
            uint32_t numa_node;
            char map_name[16];
            uint32_t map_ifindex;
            uint32_t btf_fd;
            uint32_t btf_key_type_id;
            uint32_t btf_value_type_id;
            uint32_t btf_vmlinux_value_type_id;
        } map_create;
    } attr;
    memset(&attr, 0, sizeof(attr));
    attr.map_create.map_type = 1; /* BPF_MAP_TYPE_HASH */
    attr.map_create.key_size = 4;
    attr.map_create.value_size = 8;
    attr.map_create.max_entries = 10;
    errno = 0;
    long fd = syscall(__NR_bpf, 0, &attr, sizeof(attr.map_create));
    printf("BPF_MAP_CREATE(HASH) -> fd=%ld errno=%d(%s)\n", fd, errno, strerror(errno));
    if (fd > 0) close(fd);

    /* try array map */
    memset(&attr, 0, sizeof(attr));
    attr.map_create.map_type = 2; /* BPF_MAP_TYPE_ARRAY */
    attr.map_create.key_size = 4;
    attr.map_create.value_size = 8;
    attr.map_create.max_entries = 10;
    errno = 0;
    fd = syscall(__NR_bpf, 0, &attr, sizeof(attr.map_create));
    printf("BPF_MAP_CREATE(ARRAY) -> fd=%ld errno=%d(%s)\n", fd, errno, strerror(errno));
    if (fd > 0) close(fd);

    /* unprivileged probe */
    memset(&attr, 0, sizeof(attr));
    attr.map_create.map_type = 1;
    attr.map_create.key_size = 4;
    attr.map_create.value_size = 8;
    attr.map_create.max_entries = 1;
    attr.map_create.map_flags = 1; /* BPF_F_NO_PREALLOC */
    errno = 0;
    fd = syscall(__NR_bpf, 0, &attr, sizeof(attr.map_create));
    printf("BPF_MAP_CREATE(NO_PREALLOC) -> fd=%ld errno=%d(%s)\n", fd, errno, strerror(errno));
    if (fd > 0) close(fd);
    return 0;
}