/*
 * I2C Driver Deep Analysis
 *
 * Previous observation: EEPROM-style reads returned non-zero data
 * that looked like memory content (pointers, ASCII strings)
 *
 * This could be:
 * 1. Uninitialized buffer leak in driver
 * 2. Previous I2C transaction data
 * 3. Kernel heap memory leak
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

void hexdump(const void *data, size_t size, unsigned long base)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < size; i += 16)
    {
        printf("%08lx: ", base + i);
        for (size_t j = 0; j < 16; j++)
        {
            if (i + j < size)
                printf("%02x ", p[i + j]);
            else
                printf("   ");
            if (j == 7)
                printf(" ");
        }
        printf(" |");
        for (size_t j = 0; j < 16 && i + j < size; j++)
        {
            printf("%c", (p[i + j] >= 32 && p[i + j] < 127) ? p[i + j] : '.');
        }
        printf("|\n");
    }
}

void analyze_for_pointers(unsigned char *buf, size_t len)
{
    int ptrs_found = 0;

    for (size_t i = 0; i + 8 <= len; i += 8)
    {
        unsigned long val = *(unsigned long *)(buf + i);

        // Check for kernel pointers
        if ((val & 0xFFFF000000000000ULL) == 0xFFFF000000000000ULL)
        {
            printf("[!] Offset %zu: POTENTIAL KERNEL PTR: 0x%016lx\n", i, val);
            ptrs_found++;
        }
        // Check for user pointers
        else if ((val & 0xFFFF00000000ULL) == 0x700000000000ULL ||
                 (val & 0xFFFF00000000ULL) == 0x7F0000000000ULL)
        {
            printf("[!] Offset %zu: POTENTIAL USER PTR: 0x%016lx\n", i, val);
            ptrs_found++;
        }
    }

    if (ptrs_found == 0)
    {
        printf("    No obvious pointers found\n");
    }
}

int main()
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  I2C Driver Memory Leak Analysis                               ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");

    int fd = open("/dev/i2c-1", O_RDWR);
    if (fd < 0)
    {
        perror("[-] Failed to open /dev/i2c-1");
        return 1;
    }
    printf("[+] /dev/i2c-1 opened\n");

    // Get functionality
    unsigned long funcs = 0;
    if (ioctl(fd, I2C_FUNCS, &funcs) == 0)
    {
        printf("[+] I2C functionality: 0x%lx\n", funcs);
    }

    // ============ Test 1: Read without setting address ============
    printf("\n=== Test 1: Read without setting slave address ===\n");
    {
        unsigned char buf[64] = {0};
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0)
        {
            printf("[+] Read %zd bytes (no slave set):\n", n);
            hexdump(buf, n, 0);
            analyze_for_pointers(buf, n);
        }
        else
        {
            printf("[-] Read failed: %s\n", strerror(errno));
        }
    }

    // ============ Test 2: Multiple reads with different buffer sizes ============
    printf("\n=== Test 2: Buffer size variation test ===\n");
    {
        size_t sizes[] = {8, 16, 32, 64, 128, 256};

        for (int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
        {
            unsigned char *buf = calloc(1, sizes[i]);

            // Set dummy address (shouldn't exist)
            ioctl(fd, I2C_SLAVE_FORCE, 0x50);

            ssize_t n = read(fd, buf, sizes[i]);

            // Check if buffer contains non-zero data (uninitialized?)
            int non_zero = 0;
            for (size_t j = 0; j < sizes[i]; j++)
            {
                if (buf[j] != 0)
                    non_zero++;
            }

            if (non_zero > 0)
            {
                printf("[+] Size %zu: %d non-zero bytes (%.1f%%)\n",
                       sizes[i], non_zero, 100.0 * non_zero / sizes[i]);
                if (n > 0)
                {
                    hexdump(buf, n > 32 ? 32 : n, 0);
                }
            }
            else
            {
                printf("[-] Size %zu: all zeros or read failed\n", sizes[i]);
            }

            free(buf);
        }
    }

    // ============ Test 3: I2C_RDWR with various message configs ============
    printf("\n=== Test 3: I2C_RDWR ioctl test ===\n");
    {
        unsigned char buf[256] = {0};

        struct i2c_msg msgs[2];
        struct i2c_rdwr_ioctl_data rdwr;

        // Test: Read from address 0x50 (common EEPROM)
        msgs[0].addr = 0x50;
        msgs[0].flags = I2C_M_RD;
        msgs[0].len = 64;
        msgs[0].buf = buf;

        rdwr.msgs = msgs;
        rdwr.nmsgs = 1;

        int ret = ioctl(fd, I2C_RDWR, &rdwr);
        if (ret >= 0)
        {
            printf("[+] I2C_RDWR to 0x50 succeeded:\n");
            hexdump(buf, 64, 0);
            analyze_for_pointers(buf, 64);
        }
        else
        {
            printf("[-] I2C_RDWR failed: %s\n", strerror(errno));
        }
    }

    // ============ Test 4: Scan addresses and collect leaked data ============
    printf("\n=== Test 4: Address scan with data collection ===\n");
    {
        unsigned char all_data[256 * 128]; // Collect from up to 128 addresses
        int data_collected = 0;

        for (int addr = 0x03; addr <= 0x77; addr++)
        {
            if (ioctl(fd, I2C_SLAVE_FORCE, addr) < 0)
                continue;

            unsigned char buf[32] = {0};

            // Use smbus read byte
            struct i2c_smbus_ioctl_data args;
            union i2c_smbus_data data;

            args.read_write = I2C_SMBUS_READ;
            args.command = 0;
            args.size = I2C_SMBUS_BYTE;
            args.data = &data;

            if (ioctl(fd, I2C_SMBUS, &args) >= 0)
            {
                printf("[+] Addr 0x%02x responded: data=0x%02x\n", addr, data.byte);

                // Try to read more
                for (int i = 0; i < 8; i++)
                {
                    args.command = i;
                    args.size = I2C_SMBUS_BYTE_DATA;
                    if (ioctl(fd, I2C_SMBUS, &args) >= 0)
                    {
                        buf[i] = data.byte;
                    }
                }
                printf("    Data: ");
                for (int i = 0; i < 8; i++)
                    printf("%02x ", buf[i]);
                printf("\n");
            }
        }
    }

    // ============ Test 5: Large read test ============
    printf("\n=== Test 5: Large buffer read ===\n");
    {
        size_t large_sizes[] = {512, 1024, 4096, 8192};

        for (int i = 0; i < sizeof(large_sizes) / sizeof(large_sizes[0]); i++)
        {
            unsigned char *buf = calloc(1, large_sizes[i]);

            ioctl(fd, I2C_SLAVE_FORCE, 0x50);

            struct i2c_msg msgs[1];
            struct i2c_rdwr_ioctl_data rdwr;

            msgs[0].addr = 0x50;
            msgs[0].flags = I2C_M_RD;
            msgs[0].len = large_sizes[i];
            msgs[0].buf = buf;

            rdwr.msgs = msgs;
            rdwr.nmsgs = 1;

            int ret = ioctl(fd, I2C_RDWR, &rdwr);

            // Count non-zero bytes
            int non_zero = 0;
            int ptr_count = 0;
            for (size_t j = 0; j < large_sizes[i]; j++)
            {
                if (buf[j] != 0)
                    non_zero++;
            }

            // Look for patterns
            for (size_t j = 0; j + 8 <= large_sizes[i]; j += 8)
            {
                unsigned long val = *(unsigned long *)(buf + j);
                if ((val & 0xFFFF000000000000ULL) == 0xFFFF000000000000ULL)
                {
                    ptr_count++;
                }
            }

            printf("[%zu bytes] ret=%d, non-zero=%d, potential_ptrs=%d\n",
                   large_sizes[i], ret, non_zero, ptr_count);

            if (non_zero > 0 && non_zero < 100)
            {
                printf("    First non-zero data:\n");
                hexdump(buf, non_zero > 64 ? 64 : non_zero, 0);
            }

            free(buf);
        }
    }

    // ============ Test 6: Timing-based leak ============
    printf("\n=== Test 6: Repeated reads for timing-based leak ===\n");
    {
        unsigned char prev[64] = {0};
        unsigned char curr[64] = {0};
        int changes = 0;

        ioctl(fd, I2C_SLAVE_FORCE, 0x50);

        for (int round = 0; round < 10; round++)
        {
            memset(curr, 0, sizeof(curr));

            struct i2c_msg msgs[1];
            struct i2c_rdwr_ioctl_data rdwr;

            msgs[0].addr = 0x50;
            msgs[0].flags = I2C_M_RD;
            msgs[0].len = sizeof(curr);
            msgs[0].buf = curr;

            rdwr.msgs = msgs;
            rdwr.nmsgs = 1;

            ioctl(fd, I2C_RDWR, &rdwr);

            // Compare with previous
            int diff = memcmp(prev, curr, sizeof(curr));
            if (diff != 0 && round > 0)
            {
                changes++;
                printf("[Round %d] Data changed!\n", round);
                hexdump(curr, 32, 0);
            }

            memcpy(prev, curr, sizeof(curr));
            usleep(100000); // 100ms
        }

        printf("[*] Total changes detected: %d\n", changes);
    }

    close(fd);
    printf("\n=== Analysis Complete ===\n");

    return 0;
}
