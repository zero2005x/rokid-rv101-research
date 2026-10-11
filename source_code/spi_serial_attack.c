/*
 * SPI/Serial Attack Surface Exploration
 *
 * Targets:
 * - /dev/rt600_spidev - SPI device (0666)
 * - /dev/ttyHS0 - High-speed serial (writable)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>
#include <poll.h>
#include <linux/spi/spidev.h>

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

void test_spi()
{
    printf("\n=== rt600_spidev Exploration ===\n");

    int fd = open("/dev/rt600_spidev", O_RDWR);
    if (fd < 0)
    {
        printf("[-] /dev/rt600_spidev: %s\n", strerror(errno));
        return;
    }
    printf("[+] /dev/rt600_spidev opened\n");

    // Get SPI mode
    unsigned char mode = 0;
    if (ioctl(fd, SPI_IOC_RD_MODE, &mode) == 0)
    {
        printf("    SPI Mode: 0x%02x\n", mode);
    }

    // Get bits per word
    unsigned char bits = 0;
    if (ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits) == 0)
    {
        printf("    Bits per word: %d\n", bits);
    }

    // Get max speed
    unsigned int speed = 0;
    if (ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed) == 0)
    {
        printf("    Max speed: %u Hz\n", speed);
    }

    // Get LSB first setting
    unsigned char lsb = 0;
    if (ioctl(fd, SPI_IOC_RD_LSB_FIRST, &lsb) == 0)
    {
        printf("    LSB first: %d\n", lsb);
    }

    // Try a simple SPI read
    printf("\n[*] Attempting SPI read...\n");
    unsigned char tx_buf[64] = {0};
    unsigned char rx_buf[64] = {0};

    struct spi_ioc_transfer xfer = {0};
    xfer.tx_buf = (unsigned long)tx_buf;
    xfer.rx_buf = (unsigned long)rx_buf;
    xfer.len = sizeof(tx_buf);
    xfer.speed_hz = speed > 0 ? speed : 1000000;
    xfer.bits_per_word = bits > 0 ? bits : 8;

    int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &xfer);
    if (ret >= 0)
    {
        printf("[+] SPI transfer succeeded (%d bytes)\n", ret);
        printf("[+] Response:\n");
        hexdump(rx_buf, sizeof(rx_buf), 0);
    }
    else
    {
        printf("[-] SPI transfer failed: %s\n", strerror(errno));
    }

    // Try reading the device identification
    printf("\n[*] Attempting to read device ID...\n");
    tx_buf[0] = 0x9F; // Common JEDEC ID command
    memset(&xfer, 0, sizeof(xfer));
    memset(rx_buf, 0, sizeof(rx_buf));

    xfer.tx_buf = (unsigned long)tx_buf;
    xfer.rx_buf = (unsigned long)rx_buf;
    xfer.len = 4;
    xfer.speed_hz = 1000000;
    xfer.bits_per_word = 8;

    ret = ioctl(fd, SPI_IOC_MESSAGE(1), &xfer);
    if (ret >= 0)
    {
        printf("[+] JEDEC ID response: %02x %02x %02x %02x\n",
               rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3]);
    }

    // Try to mmap
    void *map = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);
    if (map != MAP_FAILED)
    {
        printf("[+] mmap succeeded\n");
        hexdump(map, 64, 0);
        munmap(map, 4096);
    }
    else
    {
        printf("[-] mmap failed: %s\n", strerror(errno));
    }

    close(fd);
}

void test_tty()
{
    printf("\n=== ttyHS0 Serial Exploration ===\n");

    int fd = open("/dev/ttyHS0", O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
    {
        printf("[-] /dev/ttyHS0: %s\n", strerror(errno));
        return;
    }
    printf("[+] /dev/ttyHS0 opened\n");

    // Get terminal attributes
    struct termios tio;
    if (tcgetattr(fd, &tio) == 0)
    {
        printf("    c_cflag: 0x%x\n", tio.c_cflag);
        printf("    c_iflag: 0x%x\n", tio.c_iflag);
        printf("    c_oflag: 0x%x\n", tio.c_oflag);
        printf("    c_lflag: 0x%x\n", tio.c_lflag);

        speed_t ispeed = cfgetispeed(&tio);
        speed_t ospeed = cfgetospeed(&tio);
        printf("    Input speed: B%d\n", ispeed);
        printf("    Output speed: B%d\n", ospeed);
    }

    // Try to read any pending data
    printf("\n[*] Checking for pending data...\n");

    struct pollfd pfd = {
        .fd = fd,
        .events = POLLIN,
    };

    if (poll(&pfd, 1, 100) > 0)
    {
        if (pfd.revents & POLLIN)
        {
            unsigned char buf[256];
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n > 0)
            {
                printf("[+] Read %zd bytes from ttyHS0:\n", n);
                hexdump(buf, n, 0);
            }
        }
    }
    else
    {
        printf("    No data available\n");
    }

    // Try sending a test message (be careful!)
    printf("\n[*] Serial port info gathered. Not sending data to avoid disruption.\n");

    close(fd);
}

void test_other_ttys()
{
    printf("\n=== Other Serial Ports ===\n");

    const char *ttys[] = {
        "/dev/ttyMSM0",
        "/dev/ttyMSM1",
        "/dev/ttyMSM2",
        "/dev/ttyUSB0",
        "/dev/ttyACM0",
        "/dev/ttyGS0",
        "/dev/ttyGS1",
        "/dev/ttyGS2",
        "/dev/ttyGS3",
        "/dev/console",
        NULL};

    for (int i = 0; ttys[i]; i++)
    {
        int fd = open(ttys[i], O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd >= 0)
        {
            printf("[+] %s: ACCESSIBLE\n", ttys[i]);
            close(fd);
        }
    }
}

void test_spi_flash_commands()
{
    printf("\n=== SPI Flash Command Testing ===\n");

    int fd = open("/dev/rt600_spidev", O_RDWR);
    if (fd < 0)
    {
        printf("[-] Cannot open SPI device\n");
        return;
    }

    // Common SPI Flash commands
    struct
    {
        const char *name;
        unsigned char cmd;
        int response_len;
    } commands[] = {
        {"Read Status Register 1", 0x05, 2},
        {"Read Status Register 2", 0x35, 2},
        {"Read Status Register 3", 0x15, 2},
        {"Read JEDEC ID", 0x9F, 4},
        {"Read Unique ID", 0x4B, 13},
        {"Read SFDP", 0x5A, 16},
        {"Read Manufacturer ID", 0x90, 6},
        {NULL, 0, 0}};

    for (int i = 0; commands[i].name; i++)
    {
        unsigned char tx_buf[16] = {commands[i].cmd};
        unsigned char rx_buf[16] = {0};

        struct spi_ioc_transfer xfer = {0};
        xfer.tx_buf = (unsigned long)tx_buf;
        xfer.rx_buf = (unsigned long)rx_buf;
        xfer.len = commands[i].response_len;
        xfer.speed_hz = 1000000;
        xfer.bits_per_word = 8;

        int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &xfer);
        if (ret >= 0)
        {
            printf("[+] %s (0x%02x): ", commands[i].name, commands[i].cmd);
            for (int j = 0; j < commands[i].response_len; j++)
            {
                printf("%02x ", rx_buf[j]);
            }
            printf("\n");
        }
        else
        {
            printf("[-] %s failed: %s\n", commands[i].name, strerror(errno));
        }
    }

    close(fd);
}

void test_spi_memory_read()
{
    printf("\n=== SPI Flash Memory Read Attempt ===\n");

    int fd = open("/dev/rt600_spidev", O_RDWR);
    if (fd < 0)
        return;

    // Try to read from address 0x000000
    printf("[*] Attempting to read first 64 bytes of flash...\n");

    unsigned char tx_buf[68] = {0x03, 0x00, 0x00, 0x00}; // READ command + 3-byte address
    unsigned char rx_buf[68] = {0};

    struct spi_ioc_transfer xfer = {0};
    xfer.tx_buf = (unsigned long)tx_buf;
    xfer.rx_buf = (unsigned long)rx_buf;
    xfer.len = 68; // 4 bytes command + 64 bytes data
    xfer.speed_hz = 1000000;
    xfer.bits_per_word = 8;

    int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &xfer);
    if (ret >= 0)
    {
        printf("[+] Flash read succeeded:\n");
        hexdump(rx_buf + 4, 64, 0); // Skip command bytes
    }
    else
    {
        printf("[-] Flash read failed: %s\n", strerror(errno));
    }

    close(fd);
}

int main()
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  SPI/Serial Attack Surface Exploration                         ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");

    test_spi();
    test_spi_flash_commands();
    test_spi_memory_read();
    test_tty();
    test_other_ttys();

    printf("\n=== Exploration Complete ===\n");
    return 0;
}
