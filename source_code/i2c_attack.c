/*
 * I2C Bus Attack Surface Exploration
 * ===================================
 * /dev/i2c-1 is world-writable (rw-rw-rw-)
 *
 * I2C can potentially:
 * 1. Read/write to sensor chips
 * 2. Modify PMIC (power management) settings
 * 3. Access secure elements
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

void hexdump(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        if (i % 16 == 0)
            printf("  %04zx: ", i);
        printf("%02x ", data[i]);
        if ((i + 1) % 16 == 0)
            printf("\n");
    }
    if (len % 16 != 0)
        printf("\n");
}

// Scan I2C bus for devices
void scan_i2c_bus(int fd)
{
    printf("\n=== I2C Bus Scan ===\n");
    printf("    ");
    for (int i = 0; i < 16; i++)
        printf(" %x ", i);
    printf("\n");

    for (int addr = 0; addr < 0x80; addr++)
    {
        if (addr % 16 == 0)
            printf("%02x: ", addr);

        // Try to set slave address
        if (ioctl(fd, I2C_SLAVE, addr) < 0)
        {
            printf("-- ");
            continue;
        }

        // Try to read one byte
        uint8_t buf[1];
        if (read(fd, buf, 1) == 1)
        {
            printf("%02x ", addr);
        }
        else
        {
            printf("-- ");
        }

        if ((addr + 1) % 16 == 0)
            printf("\n");
    }
    printf("\n");
}

// Read registers from a device
int read_device_registers(int fd, uint8_t dev_addr, uint8_t start_reg, uint8_t *buf, size_t len)
{
    if (ioctl(fd, I2C_SLAVE, dev_addr) < 0)
    {
        return -1;
    }

    // Write register address
    if (write(fd, &start_reg, 1) != 1)
    {
        return -1;
    }

    // Read data
    if (read(fd, buf, len) != (ssize_t)len)
    {
        return -1;
    }

    return 0;
}

// Try to detect common I2C chips
void detect_chips(int fd)
{
    printf("\n=== Chip Detection ===\n");

    struct
    {
        uint8_t addr;
        const char *name;
        uint8_t id_reg;
        uint8_t expected_id;
    } chips[] = {
        {0x18, "LIS2DH (Accelerometer)", 0x0F, 0x33},
        {0x19, "LIS2DH12 (Accelerometer)", 0x0F, 0x33},
        {0x1C, "MMA8452Q (Accelerometer)", 0x0D, 0x2A},
        {0x1E, "HMC5883L (Magnetometer)", 0x0A, 0x48},
        {0x20, "STMPE811 (Touch Controller)", 0x00, 0x08},
        {0x3C, "SSD1306 (OLED)", 0x00, 0x00},
        {0x48, "TMP102 (Temperature)", 0x00, 0x00},
        {0x50, "EEPROM (24C02)", 0x00, 0x00},
        {0x51, "PCF8563 (RTC)", 0x00, 0x00},
        {0x68, "MPU6050 (Gyro/Accel)", 0x75, 0x68},
        {0x69, "MPU6050 (Gyro/Accel)", 0x75, 0x68},
        {0x76, "BME280 (Environmental)", 0xD0, 0x60},
        {0x77, "BMP280 (Pressure)", 0xD0, 0x58},
    };

    for (size_t i = 0; i < sizeof(chips) / sizeof(chips[0]); i++)
    {
        if (ioctl(fd, I2C_SLAVE, chips[i].addr) < 0)
        {
            continue;
        }

        uint8_t buf[1] = {0};
        if (write(fd, &chips[i].id_reg, 1) == 1)
        {
            if (read(fd, buf, 1) == 1)
            {
                printf("[+] Found device at 0x%02x: %s (ID: 0x%02x)\n",
                       chips[i].addr, chips[i].name, buf[0]);
            }
        }
    }
}

// Read EEPROM content (if present)
void read_eeprom(int fd, uint8_t addr)
{
    printf("\n=== EEPROM Read (0x%02x) ===\n", addr);

    if (ioctl(fd, I2C_SLAVE, addr) < 0)
    {
        printf("[-] Cannot set slave address\n");
        return;
    }

    uint8_t buf[256];
    for (int page = 0; page < 2; page++)
    {
        uint8_t reg = page * 128;
        if (write(fd, &reg, 1) != 1)
        {
            printf("[-] Write failed at offset %d\n", reg);
            continue;
        }

        usleep(10000); // Wait for EEPROM

        if (read(fd, &buf[page * 128], 128) != 128)
        {
            printf("[-] Read failed at offset %d\n", reg);
            continue;
        }
    }

    // Check if EEPROM has data
    int has_data = 0;
    for (int i = 0; i < 256; i++)
    {
        if (buf[i] != 0xFF && buf[i] != 0x00)
        {
            has_data = 1;
            break;
        }
    }

    if (has_data)
    {
        printf("[+] EEPROM content:\n");
        hexdump(buf, 256);
    }
    else
    {
        printf("[-] EEPROM appears empty or all 0xFF\n");
    }
}

// Try to read all registers from a detected device
void dump_device_registers(int fd, uint8_t addr)
{
    printf("\n=== Device 0x%02x Register Dump ===\n", addr);

    if (ioctl(fd, I2C_SLAVE, addr) < 0)
    {
        printf("[-] Cannot set slave address\n");
        return;
    }

    uint8_t regs[256];
    int readable = 0;

    for (int reg = 0; reg < 256; reg++)
    {
        uint8_t r = (uint8_t)reg;
        if (write(fd, &r, 1) == 1)
        {
            if (read(fd, &regs[reg], 1) == 1)
            {
                readable++;
            }
            else
            {
                regs[reg] = 0xFF;
            }
        }
        else
        {
            regs[reg] = 0xFF;
            break; // Device doesn't respond
        }
    }

    if (readable > 0)
    {
        printf("[+] Read %d registers:\n", readable);
        hexdump(regs, readable < 256 ? readable : 256);
    }
}

int main(int argc, char **argv)
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  I2C Bus Attack Surface Explorer                               ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    // Try multiple I2C buses
    const char *buses[] = {
        "/dev/i2c-0",
        "/dev/i2c-1",
        "/dev/i2c-2",
        "/dev/i2c-3",
        NULL};

    for (int b = 0; buses[b]; b++)
    {
        printf("\n=== Trying %s ===\n", buses[b]);

        int fd = open(buses[b], O_RDWR);
        if (fd < 0)
        {
            printf("[-] Cannot open: %s\n", strerror(errno));
            continue;
        }
        printf("[+] Opened fd=%d\n", fd);

        // Get functionality
        unsigned long funcs = 0;
        if (ioctl(fd, I2C_FUNCS, &funcs) == 0)
        {
            printf("[*] Functionality: 0x%lx\n", funcs);
            if (funcs & 0x01)
                printf("    - I2C\n");
            if (funcs & 0x02)
                printf("    - 10-bit addressing\n");
            if (funcs & 0x40000)
                printf("    - Block read\n");
            if (funcs & 0x80000)
                printf("    - Block write\n");
        }

        // Scan the bus
        scan_i2c_bus(fd);

        // Try to detect specific chips
        detect_chips(fd);

        // Try to read EEPROM at common addresses
        read_eeprom(fd, 0x50);
        read_eeprom(fd, 0x51);
        read_eeprom(fd, 0x54);
        read_eeprom(fd, 0x57);

        close(fd);
    }

    printf("\n=== I2C Exploration Complete ===\n");
    return 0;
}
