/*
 * GateServiced Protocol Probe v3
 * 更精確的協議探測 - 基於 TrustZone 常見協議模式
 *
 * 編譯: aarch64-linux-android21-clang -o gate_probe gate_probe_v3.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>

#define PORT 8341
#define HOST "127.0.0.1"
#define BUF_SIZE 8192

void hexdump(const void *data, size_t size)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < size; i++)
    {
        if (i % 16 == 0)
            printf("  %04zx: ", i);
        printf("%02x ", p[i]);
        if ((i + 1) % 16 == 0 || i == size - 1)
        {
            // Print ASCII
            if ((i + 1) % 16 != 0)
            {
                for (size_t j = 0; j < 16 - ((i + 1) % 16); j++)
                    printf("   ");
            }
            printf(" |");
            size_t start = (i / 16) * 16;
            for (size_t j = start; j <= i; j++)
            {
                printf("%c", (p[j] >= 32 && p[j] < 127) ? p[j] : '.');
            }
            printf("|\n");
        }
    }
}

int connect_gate(void)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return -1;

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT)};
    inet_pton(AF_INET, HOST, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(sock);
        return -1;
    }
    return sock;
}

// 使用 poll 等待回應，更精確地檢測是否有數據
int recv_with_poll(int sock, void *buf, size_t len, int timeout_ms)
{
    struct pollfd pfd = {.fd = sock, .events = POLLIN};

    int ret = poll(&pfd, 1, timeout_ms);
    if (ret > 0 && (pfd.revents & POLLIN))
    {
        return recv(sock, buf, len, 0);
    }
    return ret == 0 ? -2 : -1; // -2 = timeout
}

// 測試連接並等待服務器先說話
void test_passive(void)
{
    printf("\n=== Passive Connection Test ===\n");
    printf("Connecting and waiting for server to send first...\n");

    int sock = connect_gate();
    if (sock < 0)
    {
        printf("[-] Connection failed\n");
        return;
    }

    char buf[BUF_SIZE];
    int received = recv_with_poll(sock, buf, sizeof(buf), 5000);

    if (received > 0)
    {
        printf("[+] Server sent %d bytes first:\n", received);
        hexdump(buf, received);
    }
    else if (received == -2)
    {
        printf("[-] Timeout - server didn't send data first\n");
    }
    else
    {
        printf("[-] Error receiving\n");
    }

    close(sock);
}

// 測試各種 TrustZone 協議格式
void test_tz_protocols(void)
{
    printf("\n=== TrustZone Protocol Formats ===\n");

    int sock;
    char buf[BUF_SIZE];
    int received;

    // 協議格式 1: [magic:4][version:4][cmd:4][len:4][data...]
    // 協議格式 2: [len:4][cmd:4][data...]
    // 協議格式 3: [cmd:4][len:4][data...]
    // 協議格式 4: [type:2][len:2][data...]

    struct
    {
        const char *name;
        unsigned char data[32];
        size_t len;
    } protocols[] = {
        // QSEECom-like: magic + version + cmd
        {"QSEECom-like", {0x51, 0x53, 0x45, 0x45, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, 12},
        // Simple: len(4) + cmd(4)
        {"Len+Cmd BE", {0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01}, 8},
        {"Len+Cmd LE", {0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}, 8},
        // Cmd first
        {"Cmd+Len", {0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00}, 8},
        // Type + Len (16-bit each)
        {"Type16+Len16", {0x00, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00}, 8},
        // Raw command IDs
        {"Cmd 0x0001", {0x00, 0x00, 0x00, 0x01}, 4},
        {"Cmd 0x0100", {0x00, 0x00, 0x01, 0x00}, 4},
        {"Cmd 0x1000", {0x00, 0x00, 0x10, 0x00}, 4},
        // ioctl-like
        {"IOCTL-like", {0x87, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00}, 8},
        // Handshake patterns
        {"Handshake v1", {0x00, 0x00, 0x00, 0x00}, 4},
        {"Handshake v2", {0xFF, 0xFF, 0xFF, 0xFF}, 4},
        {"Hello", {'H', 'E', 'L', 'O', 0, 0, 0, 0}, 8},
    };

    for (int i = 0; i < sizeof(protocols) / sizeof(protocols[0]); i++)
    {
        sock = connect_gate();
        if (sock < 0)
            continue;

        printf("[*] Testing: %s\n", protocols[i].name);
        printf("    TX: ");
        for (size_t j = 0; j < protocols[i].len; j++)
        {
            printf("%02x ", protocols[i].data[j]);
        }
        printf("\n");

        send(sock, protocols[i].data, protocols[i].len, 0);

        received = recv_with_poll(sock, buf, sizeof(buf), 1000);
        if (received > 0)
        {
            printf("    [+] RX (%d bytes):\n", received);
            hexdump(buf, received > 64 ? 64 : received);
        }
        else if (received == -2)
        {
            printf("    [-] No response (timeout)\n");
        }

        close(sock);
        usleep(100000);
    }
}

// 持續連接測試 - 發送後保持連接
void test_persistent_connection(void)
{
    printf("\n=== Persistent Connection Test ===\n");

    int sock = connect_gate();
    if (sock < 0)
    {
        printf("[-] Connection failed\n");
        return;
    }

    char buf[BUF_SIZE];

    // 發送多個命令在同一連接
    unsigned char cmds[][8] = {
        {0x00, 0x00, 0x00, 0x01}, // cmd 1
        {0x00, 0x00, 0x00, 0x02}, // cmd 2
        {0x00, 0x00, 0x00, 0x03}, // cmd 3
    };

    for (int i = 0; i < 3; i++)
    {
        printf("[*] Sending cmd %d on same connection...\n", i + 1);
        send(sock, cmds[i], 4, 0);

        int received = recv_with_poll(sock, buf, sizeof(buf), 1000);
        if (received > 0)
        {
            printf("    [+] Response: %d bytes\n", received);
            hexdump(buf, received);
        }

        usleep(500000);
    }

    close(sock);
}

// 二進制掃描 - 快速掃描可能的命令
void binary_scan(void)
{
    printf("\n=== Quick Binary Command Scan ===\n");

    char buf[BUF_SIZE];
    int found = 0;

    // 掃描常見的命令範圍
    uint32_t scan_values[] = {
        0,
        1,
        2,
        3,
        4,
        5,
        6,
        7,
        8,
        9,
        10,
        0x10,
        0x20,
        0x30,
        0x40,
        0x50,
        0x100,
        0x200,
        0x300,
        0x1000,
        0x2000,
        0x10000,
        0x20000,
        0x87000001,
        0x87000002, // IOCTL-like
        0x52474154, // "RGAT"
        0x47415445, // "GATE"
    };

    for (int i = 0; i < sizeof(scan_values) / sizeof(scan_values[0]); i++)
    {
        int sock = connect_gate();
        if (sock < 0)
            continue;

        uint32_t cmd = scan_values[i];
        send(sock, &cmd, 4, 0);

        int received = recv_with_poll(sock, buf, sizeof(buf), 500);
        if (received > 0)
        {
            printf("[+] Cmd 0x%08x: Got %d bytes response!\n", cmd, received);
            hexdump(buf, received > 32 ? 32 : received);
            found++;
        }

        close(sock);
    }

    printf("\nFound %d responsive commands\n", found);
}

int main(int argc, char *argv[])
{
    printf("=== GateServiced Protocol Probe v3 ===\n");
    printf("Target: %s:%d\n\n", HOST, PORT);

    // 測試連接
    int sock = connect_gate();
    if (sock < 0)
    {
        printf("[-] Cannot connect to GateServiced\n");
        return 1;
    }
    printf("[+] Connection OK\n");
    close(sock);

    test_passive();
    test_tz_protocols();
    test_persistent_connection();
    binary_scan();

    printf("\n=== Probe Complete ===\n");
    return 0;
}
