/*
 * GateServiced command enumeration test
 * Try different cmd_id values to find valid commands
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

int test_cmd(uint32_t cmd_id, uint8_t *payload, size_t payload_len)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, HOST, &addr.sin_addr);

    struct timeval tv = {1, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(sock);
        return -1;
    }

    // Build the packet
    size_t total_len = 8 + payload_len;
    uint8_t *pkt = malloc(total_len);
    memcpy(pkt, &cmd_id, 4);
    uint32_t len = total_len;
    memcpy(pkt + 4, &len, 4);
    if (payload_len > 0)
    {
        memcpy(pkt + 8, payload, payload_len);
    }

    send(sock, pkt, total_len, 0);
    free(pkt);

    uint8_t buf[1024];
    ssize_t received = recv(sock, buf, sizeof(buf), 0);

    if (received > 0)
    {
        printf("[+] CMD 0x%04x: Response %zd bytes: ", cmd_id, received);
        for (int i = 0; i < (received > 16 ? 16 : received); i++)
        {
            printf("%02x ", buf[i]);
        }
        printf("\n");
        close(sock);
        return 1;
    }

    close(sock);
    return 0;
}

int main()
{
    printf("[*] GateServiced Command Enumeration\n");
    printf("[*] Target: %s:%d\n\n", HOST, PORT);
    fflush(stdout);

    // Try common TrustZone commands
    uint32_t tz_cmds[] = {
        0x0001, 0x0002, 0x0003, 0x0004, 0x0005,
        0x0100, 0x0101, 0x0102, 0x0103,
        0x0200, 0x0201, 0x0202,
        0x0300, 0x0301, 0x0302, 0x0303,
        0x1000, 0x1001, 0x1002,
        0x2000, 0x2001,
        0x3000, 0x3001, 0x300c, // 0x300c is a known error code
        0x4000, 0x4001,
        0x5000, 0x5001,
        0x6000, 0x6001,
        0x7000, 0x7001,
        0x8000, 0x8001,
        0xFFFF, 0xFFFFFFFF};

    int num_cmds = sizeof(tz_cmds) / sizeof(tz_cmds[0]);
    printf("[*] Testing %d command IDs...\n\n", num_cmds);

    int responses = 0;
    for (int i = 0; i < num_cmds; i++)
    {
        int result = test_cmd(tz_cmds[i], NULL, 0);
        if (result > 0)
            responses++;
    }

    printf("\n[*] Testing with TA class IDs from whitelist...\n\n");

    // Use the class IDs from the TZ whitelist
    uint32_t class_ids[] = {3, 34, 62, 70, 71, 78, 101, 102, 119, 120, 146};
    uint8_t payload[64];

    for (int i = 0; i < sizeof(class_ids) / sizeof(class_ids[0]); i++)
    {
        // cmd = 0x0100 (possibly load TA), payload = class_id
        memset(payload, 0, sizeof(payload));
        memcpy(payload, &class_ids[i], 4);

        printf("[*] Trying TA class %d...\n", class_ids[i]);
        test_cmd(0x0100, payload, 4);
        test_cmd(0x0001, payload, 4);
        test_cmd(0x0003, payload, 4);
    }

    printf("\n[*] Enumeration complete. Found %d responses.\n", responses);
    return 0;
}
