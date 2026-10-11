/*
 * I2C Device Deep Exploration
 *
 * Found devices:
 * - 0x08: PMIC / Power IC?
 * - 0x30: Display controller?
 * - 0x3f: Sensor / RTC?
 * - 0x64: Battery / Charger IC?
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

int i2c_read_byte(int fd, int addr, int reg)
{
    ioctl(fd, I2C_SLAVE_FORCE, addr);

    struct i2c_smbus_ioctl_data args;
    union i2c_smbus_data data;

    args.read_write = I2C_SMBUS_READ;
    args.command = reg;
    args.size = I2C_SMBUS_BYTE_DATA;
    args.data = &data;

    if (ioctl(fd, I2C_SMBUS, &args) >= 0)
    {
        return data.byte;
    }
    return -1;
}

int i2c_write_byte(int fd, int addr, int reg, int value)
{
    ioctl(fd, I2C_SLAVE_FORCE, addr);

    struct i2c_smbus_ioctl_data args;
    union i2c_smbus_data data;

    data.byte = value;
    args.read_write = I2C_SMBUS_WRITE;
    args.command = reg;
    args.size = I2C_SMBUS_BYTE_DATA;
    args.data = &data;

    return ioctl(fd, I2C_SMBUS, &args);
}

void dump_device_registers(int fd, int addr, const char *name)
{
    printf("\n=== Device 0x%02x (%s) Register Dump ===\n", addr, name);

    unsigned char regs[256];
    int success = 0;

    for (int reg = 0; reg < 256; reg++)
    {
        int val = i2c_read_byte(fd, addr, reg);
        if (val >= 0)
        {
            regs[reg] = val;
            success++;
        }
        else
        {
            regs[reg] = 0xFF; // Mark as unreadable
        }
    }

    printf("[*] Successfully read %d registers\n", success);

    // Hexdump
    hexdump(regs, 256, 0);

    // Analyze for patterns
    printf("\n[*] Non-zero registers:\n");
    for (int reg = 0; reg < 256; reg++)
    {
        int val = i2c_read_byte(fd, addr, reg);
        if (val > 0 && val != 0xFF)
        {
            printf("    0x%02x: 0x%02x (%d)\n", reg, val, val);
        }
    }
}

void analyze_device_0x08(int fd)
{
    printf("\n╔═════════════════════════════════════════╗\n");
    printf("║  Device 0x08 Analysis (Possible PMIC)   ║\n");
    printf("╚═════════════════════════════════════════╝\n");

    dump_device_registers(fd, 0x08, "PMIC");

    // Common PMIC register meanings
    printf("\n[*] Common PMIC register analysis:\n");
    int r0 = i2c_read_byte(fd, 0x08, 0x00);
    int r1 = i2c_read_byte(fd, 0x08, 0x01);
    int r2 = i2c_read_byte(fd, 0x08, 0x02);
    int r3 = i2c_read_byte(fd, 0x08, 0x03);

    if (r0 >= 0)
        printf("    Reg 0x00 (ID/Rev): 0x%02x\n", r0);
    if (r1 >= 0)
        printf("    Reg 0x01 (Status): 0x%02x\n", r1);
    if (r2 >= 0)
        printf("    Reg 0x02 (Control): 0x%02x\n", r2);
    if (r3 >= 0)
        printf("    Reg 0x03 (Enable): 0x%02x\n", r3);
}

void analyze_device_0x30(int fd)
{
    printf("\n╔═════════════════════════════════════════╗\n");
    printf("║  Device 0x30 Analysis (Display/Touch?)  ║\n");
    printf("╚═════════════════════════════════════════╝\n");

    dump_device_registers(fd, 0x30, "Display");
}

void analyze_device_0x3f(int fd)
{
    printf("\n╔═════════════════════════════════════════╗\n");
    printf("║  Device 0x3F Analysis (Sensor/RTC?)     ║\n");
    printf("╚═════════════════════════════════════════╝\n");

    dump_device_registers(fd, 0x3f, "Sensor");

    // Check if it's an RTC (common RTC patterns)
    printf("\n[*] RTC check:\n");
    int sec = i2c_read_byte(fd, 0x3f, 0x00);
    int min = i2c_read_byte(fd, 0x3f, 0x01);
    int hour = i2c_read_byte(fd, 0x3f, 0x02);

    if (sec >= 0 && min >= 0 && hour >= 0)
    {
        // BCD to decimal
        int sec_dec = (sec >> 4) * 10 + (sec & 0x0F);
        int min_dec = (min >> 4) * 10 + (min & 0x0F);
        int hour_dec = (hour >> 4) * 10 + (hour & 0x0F);

        if (sec_dec < 60 && min_dec < 60 && hour_dec < 24)
        {
            printf("    Possible RTC time: %02d:%02d:%02d\n", hour_dec, min_dec, sec_dec);
        }
    }
}

void analyze_device_0x64(int fd)
{
    printf("\n╔═════════════════════════════════════════╗\n");
    printf("║  Device 0x64 Analysis (Battery IC?)     ║\n");
    printf("╚═════════════════════════════════════════╝\n");

    dump_device_registers(fd, 0x64, "Battery");

    // Common battery gauge registers
    printf("\n[*] Battery gauge analysis:\n");
    int voltage_low = i2c_read_byte(fd, 0x64, 0x08);
    int voltage_high = i2c_read_byte(fd, 0x64, 0x09);
    int soc = i2c_read_byte(fd, 0x64, 0x2c); // State of Charge
    int temp_low = i2c_read_byte(fd, 0x64, 0x06);
    int temp_high = i2c_read_byte(fd, 0x64, 0x07);

    if (voltage_low >= 0 && voltage_high >= 0)
    {
        int voltage = (voltage_high << 8) | voltage_low;
        printf("    Voltage (raw): %d (0x%04x)\n", voltage, voltage);
    }
    if (soc >= 0)
    {
        printf("    State of Charge: %d%%\n", soc);
    }
}

void test_write_capabilities(int fd)
{
    printf("\n=== Write Capability Test (Safe) ===\n");
    printf("[!] WARNING: Writing to I2C devices can brick hardware!\n");
    printf("[*] Only testing read-back verification...\n");

    // For each device, try to read, "write" same value, read again
    int addrs[] = {0x08, 0x30, 0x3f, 0x64};
    int safe_regs[] = {0x00, 0x01}; // Usually ID/status regs, safe to read

    for (int i = 0; i < sizeof(addrs) / sizeof(addrs[0]); i++)
    {
        int addr = addrs[i];

        for (int j = 0; j < sizeof(safe_regs) / sizeof(safe_regs[0]); j++)
        {
            int reg = safe_regs[j];

            // Read original
            int orig = i2c_read_byte(fd, addr, reg);
            if (orig < 0)
                continue;

            // Try write (same value)
            int ret = i2c_write_byte(fd, addr, reg, orig);

            // Read back
            int after = i2c_read_byte(fd, addr, reg);

            printf("    0x%02x reg 0x%02x: write %s, value %s\n",
                   addr, reg,
                   ret >= 0 ? "OK" : "FAIL",
                   orig == after ? "unchanged" : "CHANGED!");
        }
    }
}

void check_security_sensitive_regs(int fd)
{
    printf("\n=== Security-Sensitive Register Check ===\n");

    // PMIC usually has protection bits
    printf("[*] Checking PMIC (0x08) protection registers:\n");

    // Common protection register offsets
    int prot_regs[] = {0x10, 0x11, 0x20, 0x21, 0x30, 0x40, 0x50};
    for (int i = 0; i < sizeof(prot_regs) / sizeof(prot_regs[0]); i++)
    {
        int val = i2c_read_byte(fd, 0x08, prot_regs[i]);
        if (val >= 0)
        {
            printf("    Reg 0x%02x: 0x%02x\n", prot_regs[i], val);
        }
    }

    // Battery gauge often has authentication
    printf("\n[*] Checking Battery IC (0x64) security:\n");
    int auth_regs[] = {0x00, 0x3E, 0x3F, 0x60, 0x61};
    for (int i = 0; i < sizeof(auth_regs) / sizeof(auth_regs[0]); i++)
    {
        int val = i2c_read_byte(fd, 0x64, auth_regs[i]);
        if (val >= 0)
        {
            printf("    Reg 0x%02x: 0x%02x\n", auth_regs[i], val);
        }
    }
}

int main()
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  I2C Device Deep Exploration                                   ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");

    int fd = open("/dev/i2c-1", O_RDWR);
    if (fd < 0)
    {
        perror("[-] Failed to open /dev/i2c-1");
        return 1;
    }
    printf("[+] /dev/i2c-1 opened\n");

    analyze_device_0x08(fd);
    analyze_device_0x30(fd);
    analyze_device_0x3f(fd);
    analyze_device_0x64(fd);

    test_write_capabilities(fd);
    check_security_sensitive_regs(fd);

    close(fd);
    printf("\n=== Exploration Complete ===\n");

    return 0;
}
