/*
 * GateServiced 協議探測器
 * 發送各種命令格式來分析回應
 *
 * 編譯: NDK cross-compile for ARM64
 * clang --target=aarch64-linux-android21 -o gate_proto_probe gate_proto_probe.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define PORT 8341
#define HOST "127.0.0.1"
#define BUF_SIZE 4096

// 協議結構假設
struct gate_cmd
{
    uint32_t cmd_id;
    uint32_t payload_len;
    char payload[0];
};

void hexdump(const void *data, size_t size)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < size; i++)
    {
        if (i % 16 == 0)
            printf("%08zx: ", i);
        printf("%02x ", p[i]);
        if ((i + 1) % 16 == 0 || i == size - 1)
        {
            // 填充空格
            for (size_t j = (i % 16) + 1; j < 16; j++)
                printf("   ");
            printf(" |");
            // ASCII
            size_t start = i - (i % 16);
            for (size_t j = start; j <= i; j++)
            {
                printf("%c", (p[j] >= 32 && p[j] <= 126) ? p[j] : '.');
            }
            printf("|\n");
        }
    }
}

int connect_gate(void)
{
    int sock;
    struct sockaddr_in addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("socket");
        return -1;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, HOST, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("connect");
        close(sock);
        return -1;
    }

    return sock;
}

int send_and_recv(int sock, const void *data, size_t len, void *resp, size_t resp_size)
{
    printf("\n[TX] Sending %zu bytes:\n", len);
    hexdump(data, len);

    ssize_t sent = send(sock, data, len, 0);
    if (sent < 0)
    {
        perror("send");
        return -1;
    }

    // 等待回應
    struct timeval tv = {2, 0}; // 2秒超時
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ssize_t received = recv(sock, resp, resp_size, 0);
    if (received < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            printf("[RX] Timeout - no response\n");
            return 0;
        }
        perror("recv");
        return -1;
    }

    if (received > 0)
    {
        printf("[RX] Received %zd bytes:\n", received);
        hexdump(resp, received);
    }
    else
    {
        printf("[RX] Connection closed\n");
    }

    return received;
}

// 探測函數
void probe_cmd_format(void)
{
    printf("\n=== Testing Command Formats ===\n");
    char resp[BUF_SIZE];

    // 測試 1: 簡單 4 字節命令 ID
    printf("\n--- Test 1: Simple 4-byte command IDs ---\n");
    uint32_t simple_cmds[] = {0, 1, 2, 0x100, 0x1000, 0x10000, 0xFFFFFFFF};
    for (int i = 0; i < sizeof(simple_cmds) / sizeof(simple_cmds[0]); i++)
    {
        int sock = connect_gate();
        if (sock < 0)
            continue;

        uint32_t cmd = simple_cmds[i];
        printf("\nCmd ID: 0x%08x\n", cmd);
        send_and_recv(sock, &cmd, sizeof(cmd), resp, sizeof(resp));
        close(sock);
        usleep(100000);
    }

    // 測試 2: 8 字節頭 (cmd_id + length)
    printf("\n--- Test 2: 8-byte header (cmd_id + length) ---\n");
    struct
    {
        uint32_t cmd_id;
        uint32_t length;
    } header_cmds[] = {
        {0x00000001, 0},
        {0x00000001, 8},
        {0x00000002, 0},
        {0x00000010, 0},
        {0x00000100, 0},
        {0x47415445, 0}, // "GATE" in hex
    };

    for (int i = 0; i < sizeof(header_cmds) / sizeof(header_cmds[0]); i++)
    {
        int sock = connect_gate();
        if (sock < 0)
            continue;

        printf("\nCmd: 0x%08x, Len: 0x%08x\n", header_cmds[i].cmd_id, header_cmds[i].length);
        send_and_recv(sock, &header_cmds[i], sizeof(header_cmds[i]), resp, sizeof(resp));
        close(sock);
        usleep(100000);
    }

    // 測試 3: 帶 payload 的命令
    printf("\n--- Test 3: Commands with payload ---\n");
    char cmd_with_payload[256];
    memset(cmd_with_payload, 0, sizeof(cmd_with_payload));

    // 格式: cmd_id(4) + length(4) + payload
    uint32_t *p = (uint32_t *)cmd_with_payload;
    p[0] = 0x00000001; // cmd_id = 1
    p[1] = 16;         // payload length
    strcpy(cmd_with_payload + 8, "TEST");

    int sock = connect_gate();
    if (sock >= 0)
    {
        printf("\nCmd with payload:\n");
        send_and_recv(sock, cmd_with_payload, 24, resp, sizeof(resp));
        close(sock);
    }

    // 測試 4: 字符串命令
    printf("\n--- Test 4: String commands ---\n");
    const char *str_cmds[] = {
        "hello",
        "status",
        "info",
        "version",
        "help",
        "GATE",
        "{\"cmd\":\"info\"}",
    };

    for (int i = 0; i < sizeof(str_cmds) / sizeof(str_cmds[0]); i++)
    {
        sock = connect_gate();
        if (sock < 0)
            continue;

        printf("\nString cmd: %s\n", str_cmds[i]);
        send_and_recv(sock, str_cmds[i], strlen(str_cmds[i]), resp, sizeof(resp));
        close(sock);
        usleep(100000);
    }
}

// 深度命令 ID 枚舉
void deep_cmd_enum(uint32_t start, uint32_t count)
{
    printf("\n=== Deep Command ID Enumeration ===\n");
    printf("Range: 0x%08x to 0x%08x\n", start, start + count);

    char resp[BUF_SIZE];
    struct
    {
        uint32_t cmd_id;
        uint32_t length;
    } cmd;

    for (uint32_t i = start; i < start + count; i++)
    {
        int sock = connect_gate();
        if (sock < 0)
        {
            usleep(100000);
            continue;
        }

        cmd.cmd_id = i;
        cmd.length = 0;

        ssize_t sent = send(sock, &cmd, sizeof(cmd), 0);
        if (sent < 0)
        {
            close(sock);
            continue;
        }

        struct timeval tv = {1, 0};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        ssize_t received = recv(sock, resp, sizeof(resp), 0);
        if (received > 0)
        {
            printf("\n[HIT] cmd_id=0x%08x responded with %zd bytes:\n", i, received);
            hexdump(resp, received > 64 ? 64 : received);
        }

        close(sock);
        usleep(10000);
    }
}

int main(int argc, char *argv[])
{
    printf("=== GateServiced Protocol Probe ===\n");
    printf("Target: %s:%d\n", HOST, PORT);

    // 測試連接
    int sock = connect_gate();
    if (sock < 0)
    {
        printf("[-] Cannot connect to GateServiced\n");
        return 1;
    }
    printf("[+] Connected successfully!\n");
    close(sock);

    // 執行探測
    probe_cmd_format();

    // 深度枚舉 (可選)
    if (argc > 1 && strcmp(argv[1], "-deep") == 0)
    {
        uint32_t start = 0;
        uint32_t count = 256;
        if (argc > 2)
            start = strtoul(argv[2], NULL, 0);
        if (argc > 3)
            count = strtoul(argv[3], NULL, 0);
        deep_cmd_enum(start, count);
    }

    printf("\n=== Probe Complete ===\n");
    return 0;
}
