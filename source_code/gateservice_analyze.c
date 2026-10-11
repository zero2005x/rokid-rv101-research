/*
 * GateServiced Protocol Analyzer
 * Target: Root service on 127.0.0.1:8341
 *
 * From init.rc: GateServiced runs as root
 * Error 0x300c = TA not loaded (QSEE/TrustZone)
 *
 * This analyzer tries to understand the protocol by
 * sending various probes and analyzing responses.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>

#define HOST "127.0.0.1"
#define PORT 8341
#define TIMEOUT_MS 3000

void hexdump(const unsigned char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        if (i % 16 == 0)
            printf("  %04zx: ", i);
        printf("%02x ", buf[i]);
        if ((i + 1) % 16 == 0)
            printf("\n");
    }
    if (len % 16 != 0)
        printf("\n");
}

int send_raw_and_recv(const unsigned char *data, size_t len,
                      unsigned char *response, size_t *resp_len)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return -1;

    struct timeval tv = {3, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, HOST, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(sock);
        return -2;
    }

    if (send(sock, data, len, 0) != (ssize_t)len)
    {
        close(sock);
        return -3;
    }

    /* Wait for response with poll */
    struct pollfd pfd = {sock, POLLIN, 0};
    *resp_len = 0;

    if (poll(&pfd, 1, TIMEOUT_MS) > 0)
    {
        ssize_t n = recv(sock, response, 4096, 0);
        if (n > 0)
            *resp_len = n;
    }

    close(sock);
    return 0;
}

void test_empty_connection(void)
{
    printf("\n=== Test: Empty Connection ===\n");

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, HOST, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0)
    {
        printf("  Connected successfully\n");

        /* Just wait without sending anything */
        struct pollfd pfd = {sock, POLLIN, 0};
        if (poll(&pfd, 1, 2000) > 0)
        {
            unsigned char buf[256];
            ssize_t n = recv(sock, buf, sizeof(buf), 0);
            if (n > 0)
            {
                printf("  Received %zd bytes (unsolicited):\n", n);
                hexdump(buf, n);
            }
            else
            {
                printf("  Connection closed by server\n");
            }
        }
        else
        {
            printf("  No unsolicited data (server waits for client)\n");
        }
    }
    close(sock);
}

void test_partial_header(void)
{
    printf("\n=== Test: Partial Header ===\n");

    unsigned char response[4096];
    size_t resp_len;

    /* Send only 1 byte */
    unsigned char one_byte[] = {0x00};
    send_raw_and_recv(one_byte, 1, response, &resp_len);
    printf("  1 byte  -> %zu bytes response\n", resp_len);
    if (resp_len > 0)
        hexdump(response, resp_len);

    /* Send 4 bytes */
    unsigned char four_bytes[] = {0x00, 0x00, 0x00, 0x00};
    send_raw_and_recv(four_bytes, 4, response, &resp_len);
    printf("  4 bytes -> %zu bytes response\n", resp_len);
    if (resp_len > 0)
        hexdump(response, resp_len);

    /* Send 7 bytes */
    unsigned char seven_bytes[] = {0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00};
    send_raw_and_recv(seven_bytes, 7, response, &resp_len);
    printf("  7 bytes -> %zu bytes response\n", resp_len);
    if (resp_len > 0)
        hexdump(response, resp_len);
}

void test_basic_commands(void)
{
    printf("\n=== Test: Basic Commands ===\n");

    unsigned char response[4096];
    size_t resp_len;

    /* Standard header: [cmd:4][len:4] */
    struct
    {
        unsigned int cmd;
        unsigned int len;
        const char *desc;
    } tests[] = {
        {0, 8, "cmd=0, len=8 (minimal)"},
        {0, 0, "cmd=0, len=0 (zero len)"},
        {1, 8, "cmd=1"},
        {0x100, 8, "cmd=0x100"},
        {0x1000, 8, "cmd=0x1000"},
        {0xFFFFFFFF, 8, "cmd=max"},
    };

    for (int i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
        unsigned char packet[8];
        memcpy(packet, &tests[i].cmd, 4);
        memcpy(packet + 4, &tests[i].len, 4);

        int ret = send_raw_and_recv(packet, 8, response, &resp_len);
        printf("  %s -> ", tests[i].desc);

        if (ret < 0)
        {
            printf("ERROR %d\n", ret);
        }
        else if (resp_len == 0)
        {
            printf("NO RESPONSE\n");
        }
        else
        {
            unsigned int err_code = 0;
            if (resp_len >= 4)
                memcpy(&err_code, response, 4);
            printf("%zu bytes, err=0x%08x\n", resp_len, err_code);
            if (resp_len > 4)
                hexdump(response, resp_len);
        }

        usleep(100000);
    }
}

void test_with_payload(void)
{
    printf("\n=== Test: Commands with Payload ===\n");

    unsigned char response[4096];
    size_t resp_len;
    unsigned char packet[1024];

    /* Try cmd=0 with various payloads */
    struct
    {
        const char *payload;
        int payload_len;
        const char *desc;
    } tests[] = {
        {"A", 1, "1 byte payload"},
        {"AAAA", 4, "4 byte payload"},
        {"AAAAAAAA", 8, "8 byte payload"},
        {"AAAAAAAAAAAAAAAA", 16, "16 byte payload"},
        {"fingerprint\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 32, "TA name: fingerprint"},
        {"keymaster\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 32, "TA name: keymaster"},
        {"rokid\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 32, "TA name: rokid"},
        {"gate\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 32, "TA name: gate"},
    };

    for (int i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
        unsigned int cmd = 0;
        unsigned int len = 8 + tests[i].payload_len;

        memcpy(packet, &cmd, 4);
        memcpy(packet + 4, &len, 4);
        memcpy(packet + 8, tests[i].payload, tests[i].payload_len);

        int ret = send_raw_and_recv(packet, len, response, &resp_len);
        printf("  %s -> ", tests[i].desc);

        if (ret < 0)
        {
            printf("ERROR %d\n", ret);
        }
        else if (resp_len == 0)
        {
            printf("NO RESPONSE\n");
        }
        else
        {
            unsigned int err_code = 0;
            if (resp_len >= 4)
                memcpy(&err_code, response, 4);
            printf("%zu bytes, err=0x%08x\n", resp_len, err_code);
        }

        usleep(100000);
    }
}

void test_big_endian(void)
{
    printf("\n=== Test: Big Endian Format ===\n");

    unsigned char response[4096];
    size_t resp_len;

    /* Try big-endian header */
    unsigned char be_packet[] = {
        0x00,
        0x00,
        0x00,
        0x00, /* cmd=0 BE */
        0x00,
        0x00,
        0x00,
        0x08, /* len=8 BE */
    };

    send_raw_and_recv(be_packet, 8, response, &resp_len);
    printf("  Big-endian header -> %zu bytes\n", resp_len);
    if (resp_len > 0)
        hexdump(response, resp_len);
}

void test_magic_values(void)
{
    printf("\n=== Test: Magic Values ===\n");

    unsigned char response[4096];
    size_t resp_len;

    /* Try various magic patterns that might be recognized */
    unsigned char tests[][16] = {
        /* QSEE-like patterns */
        {0x51, 0x53, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00}, /* "QSEE" + len */
        {0x51, 0x43, 0x4F, 0x4D, 0x10, 0x00, 0x00, 0x00}, /* "QCOM" + len */
        {0x47, 0x41, 0x54, 0x45, 0x10, 0x00, 0x00, 0x00}, /* "GATE" + len */
        /* TrustZone patterns */
        {0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00}, /* Version 1 */
        {0x02, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00}, /* Version 2 */
    };

    const char *names[] = {"QSEE", "QCOM", "GATE", "Ver1", "Ver2"};

    for (int i = 0; i < 5; i++)
    {
        send_raw_and_recv(tests[i], 8, response, &resp_len);
        printf("  %s pattern -> %zu bytes", names[i], resp_len);
        if (resp_len >= 4)
        {
            unsigned int err;
            memcpy(&err, response, 4);
            printf(" (err=0x%08x)", err);
        }
        printf("\n");
        usleep(100000);
    }
}

void keep_alive_test(void)
{
    printf("\n=== Test: Keep-Alive / Multi-Message ===\n");

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, HOST, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        printf("  Connect failed\n");
        close(sock);
        return;
    }

    unsigned char packet[8] = {0, 0, 0, 0, 8, 0, 0, 0};
    unsigned char response[4096];

    for (int i = 0; i < 3; i++)
    {
        if (send(sock, packet, 8, 0) != 8)
        {
            printf("  Send %d failed\n", i);
            break;
        }

        struct pollfd pfd = {sock, POLLIN, 0};
        if (poll(&pfd, 1, 2000) > 0)
        {
            ssize_t n = recv(sock, response, sizeof(response), 0);
            printf("  Message %d: sent 8, received %zd bytes\n", i, n);
        }
        else
        {
            printf("  Message %d: no response\n", i);
        }

        usleep(500000);
    }

    close(sock);
}

int main(void)
{
    setbuf(stdout, NULL);

    printf("==========================================\n");
    printf("GateServiced Protocol Analyzer\n");
    printf("Target: %s:%d\n", HOST, PORT);
    printf("==========================================\n");

    /* Verify service is running */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, HOST, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        printf("[!] Cannot connect to service\n");
        close(sock);
        return 1;
    }
    close(sock);
    printf("[+] Service is running\n");

    /* Run tests */
    test_empty_connection();
    test_partial_header();
    test_basic_commands();
    test_with_payload();
    test_big_endian();
    test_magic_values();
    keep_alive_test();

    printf("\n==========================================\n");
    printf("Analysis Complete\n");
    printf("==========================================\n");

    return 0;
}
