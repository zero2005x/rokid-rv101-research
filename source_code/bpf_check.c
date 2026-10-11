#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/bpf.h>
#include <string.h>
#include <errno.h>

#ifndef __NR_bpf
#define __NR_bpf 280 // AArch64
#endif

int main()
{
    // Check sysctl
    FILE *f = fopen("/proc/sys/kernel/unprivileged_bpf_disabled", "r");
    if (f)
    {
        char buf[32];
        if (fgets(buf, sizeof(buf), f))
        {
            printf("[*] kernel.unprivileged_bpf_disabled: %s", buf);
        }
        fclose(f);
    }
    else
    {
        printf("[-] Failed to read sysctl: %s\n", strerror(errno));
    }

    // Try to create a map
    union bpf_attr attr = {
        .map_type = 1, // BPF_MAP_TYPE_HASH
        .key_size = 4,
        .value_size = 4,
        .max_entries = 10,
    };

    int fd = syscall(__NR_bpf, BPF_MAP_CREATE, &attr, sizeof(attr));
    if (fd < 0)
    {
        printf("[-] BPF Map Create failed: %d (%s)\n", errno, strerror(errno));
        if (errno == EPERM)
        {
            printf("[!] EPERM: Likely unprivileged BPF is disabled or SEBlocked.\n");
        }
    }
    else
    {
        printf("[+] BPF Map Create success! FD: %d\n", fd);
        close(fd);
    }
    return 0;
}
