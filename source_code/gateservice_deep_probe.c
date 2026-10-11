/*
 * GateServiced Interaction Test via Socket
 * Probe the service more deeply with raw socket interaction
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>

#define GATE_PORT 8341
#define TIMEOUT_MS 500

int connect_to_gate(void)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return -1;

    // Set non-blocking
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(GATE_PORT),
    };
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    connect(sock, (struct sockaddr *)&addr, sizeof(addr));

    // Wait for connection
    struct pollfd pfd = {.fd = sock, .events = POLLOUT};
    if (poll(&pfd, 1, TIMEOUT_MS) <= 0)
    {
        close(sock);
        return -1;
    }

    // Check if connected
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len);
    if (err != 0)
    {
        close(sock);
        return -1;
    }

    // Set back to blocking
    fcntl(sock, F_SETFL, flags);

    return sock;
}

ssize_t send_recv(int sock, const void *data, size_t len, void *resp, size_t resp_len)
{
    if (send(sock, data, len, 0) != (ssize_t)len)
    {
        return -1;
    }

    struct pollfd pfd = {.fd = sock, .events = POLLIN};
    if (poll(&pfd, 1, TIMEOUT_MS) <= 0)
    {
        return 0; // No response (timeout)
    }

    return recv(sock, resp, resp_len, 0);
}

void hex_dump(const char *prefix, const void *data, size_t len)
{
    const uint8_t *p = data;
    printf("%s: ", prefix);
    for (size_t i = 0; i < len && i < 64; i++)
    {
        printf("%02x ", p[i]);
    }
    if (len > 64)
        printf("...");
    printf("\n");
}

void test_length_prefixed_messages(void)
{
    printf("\n=== Length-Prefixed Message Test ===\n");

    // Many Android services use length-prefixed messages
    // Try various length encodings

    // 4-byte big-endian length + payload
    struct
    {
        uint32_t len;
        char data[64];
    } __attribute__((packed)) msg_be = {0};

    const char *payloads[] = {
        "",
        "hello",
        "GateServiced",
        "{\"cmd\":\"ping\"}",
        "\x00\x00\x00\x00",
    };

    for (int i = 0; i < sizeof(payloads) / sizeof(payloads[0]); i++)
    {
        int sock = connect_to_gate();
        if (sock < 0)
        {
            printf("  [X] Connection failed\n");
            continue;
        }

        size_t plen = strlen(payloads[i]);
        msg_be.len = htonl(plen);
        memcpy(msg_be.data, payloads[i], plen);

        char resp[256] = {0};
        ssize_t n = send_recv(sock, &msg_be, 4 + plen, resp, sizeof(resp));

        printf("  BE len=%zu payload='%s': ", plen, plen > 0 ? payloads[i] : "(empty)");
        if (n > 0)
        {
            printf("GOT %zd bytes: ", n);
            hex_dump("", resp, n);
        }
        else if (n == 0)
        {
            printf("no response\n");
        }
        else
        {
            printf("error: %s\n", strerror(errno));
        }

        close(sock);
    }

    // Little-endian length
    printf("\n--- Little-endian ---\n");
    for (int i = 0; i < 3; i++)
    {
        int sock = connect_to_gate();
        if (sock < 0)
            continue;

        size_t plen = strlen(payloads[i]);
        uint32_t len_le = plen;

        char buf[128];
        memcpy(buf, &len_le, 4);
        memcpy(buf + 4, payloads[i], plen);

        char resp[256] = {0};
        ssize_t n = send_recv(sock, buf, 4 + plen, resp, sizeof(resp));

        printf("  LE len=%zu: ", plen);
        if (n > 0)
        {
            printf("GOT %zd bytes\n", n);
        }
        else
        {
            printf("no response\n");
        }
        close(sock);
    }
}

void test_protobuf_like(void)
{
    printf("\n=== Protobuf-like Message Test ===\n");

    // Try protobuf varint encoding
    // Simple message: field 1, type string, value "test"
    uint8_t proto_msgs[][32] = {
        // field 1, wire type 2 (length-delimited), length 4, "test"
        {0x0a, 0x04, 't', 'e', 's', 't'},
        // field 1, wire type 0 (varint), value 1
        {0x08, 0x01},
        // Empty message
        {},
        // field 15, wire type 2, length 5, "hello"
        {0x7a, 0x05, 'h', 'e', 'l', 'l', 'o'},
    };

    size_t proto_lens[] = {6, 2, 0, 7};

    for (int i = 0; i < 4; i++)
    {
        int sock = connect_to_gate();
        if (sock < 0)
            continue;

        char resp[256] = {0};
        ssize_t n = send_recv(sock, proto_msgs[i], proto_lens[i], resp, sizeof(resp));

        printf("  Proto msg %d (len=%zu): ", i, proto_lens[i]);
        if (n > 0)
        {
            printf("GOT %zd bytes: ", n);
            hex_dump("", resp, n);
        }
        else
        {
            printf("no response\n");
        }
        close(sock);
    }
}

void test_binder_like(void)
{
    printf("\n=== Binder-like Transaction Test ===\n");

    // Binder transactions have a specific format
    // Try sending something that looks like a binder call

    struct
    {
        uint32_t strict_mode;
        uint32_t interface_len;
        char interface[32];
    } __attribute__((packed)) binder_header = {
        .strict_mode = 0,
        .interface_len = 12,
    };
    strcpy(binder_header.interface, "IGateService");

    int sock = connect_to_gate();
    if (sock >= 0)
    {
        char resp[256] = {0};
        ssize_t n = send_recv(sock, &binder_header, sizeof(binder_header), resp, sizeof(resp));
        printf("  Binder header: ");
        if (n > 0)
        {
            printf("GOT %zd bytes\n", n);
        }
        else
        {
            printf("no response\n");
        }
        close(sock);
    }
}

void test_json_commands(void)
{
    printf("\n=== JSON Command Test ===\n");

    const char *json_cmds[] = {
        "{}",
        "{\"type\":\"ping\"}",
        "{\"type\":\"connect\",\"id\":1}",
        "{\"action\":\"getInfo\"}",
        "{\"cmd\":\"info\",\"data\":{}}",
        "[{\"op\":\"query\"}]",
    };

    for (int i = 0; i < sizeof(json_cmds) / sizeof(json_cmds[0]); i++)
    {
        int sock = connect_to_gate();
        if (sock < 0)
            continue;

        char resp[256] = {0};
        ssize_t n = send_recv(sock, json_cmds[i], strlen(json_cmds[i]), resp, sizeof(resp));

        printf("  %s: ", json_cmds[i]);
        if (n > 0)
        {
            printf("GOT %zd bytes: %.*s\n", n, (int)n, resp);
        }
        else
        {
            printf("no response\n");
        }
        close(sock);
    }

    // Try with newline terminator
    printf("\n--- With newline ---\n");
    for (int i = 0; i < 3; i++)
    {
        int sock = connect_to_gate();
        if (sock < 0)
            continue;

        char buf[256];
        snprintf(buf, sizeof(buf), "%s\n", json_cmds[i]);

        char resp[256] = {0};
        ssize_t n = send_recv(sock, buf, strlen(buf), resp, sizeof(resp));

        printf("  %s\\n: ", json_cmds[i]);
        if (n > 0)
        {
            printf("GOT %zd bytes\n", n);
        }
        else
        {
            printf("no response\n");
        }
        close(sock);
    }
}

void test_raw_binary(void)
{
    printf("\n=== Raw Binary Patterns Test ===\n");

    // Test various binary patterns that might trigger responses
    uint8_t patterns[][16] = {
        {0x00, 0x00, 0x00, 0x01}, // Minimal length + 1 byte
        {0x01, 0x00, 0x00, 0x00}, // LE 1
        {0xde, 0xad, 0xbe, 0xef}, // Magic
        {0x47, 0x41, 0x54, 0x45}, // "GATE"
        {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07},
        {0xff, 0xff, 0xff, 0xff},                         // All ones
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08}, // Possible header
    };

    for (int i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++)
    {
        int sock = connect_to_gate();
        if (sock < 0)
            continue;

        char resp[256] = {0};
        ssize_t n = send_recv(sock, patterns[i], 8, resp, sizeof(resp));

        printf("  Pattern %d: ", i);
        hex_dump("sent", patterns[i], 8);
        if (n > 0)
        {
            printf("    GOT %zd bytes: ", n);
            hex_dump("", resp, n);
        }
        else
        {
            printf("    no response\n");
        }
        close(sock);
    }
}

void test_keep_alive(void)
{
    printf("\n=== Keep-Alive Behavior Test ===\n");

    int sock = connect_to_gate();
    if (sock < 0)
    {
        printf("  [X] Connection failed\n");
        return;
    }

    printf("  Connected, waiting for server-initiated data...\n");

    struct pollfd pfd = {.fd = sock, .events = POLLIN};
    for (int i = 0; i < 5; i++)
    {
        int ret = poll(&pfd, 1, 1000);
        if (ret > 0)
        {
            char buf[256] = {0};
            ssize_t n = recv(sock, buf, sizeof(buf), 0);
            if (n > 0)
            {
                printf("  [!] Received %zd bytes after %d sec: ", n, i + 1);
                hex_dump("", buf, n);
            }
            else if (n == 0)
            {
                printf("  Connection closed by server after %d sec\n", i + 1);
                break;
            }
        }
        else if (ret == 0)
        {
            printf("  ... %d sec passed, no data\n", i + 1);
        }
    }

    close(sock);
}

int main(void)
{
    printf("==========================================\n");
    printf("GateServiced Deep Protocol Probe\n");
    printf("==========================================\n");

    // First verify connection
    int sock = connect_to_gate();
    if (sock < 0)
    {
        printf("[X] Cannot connect to GateServiced on port %d\n", GATE_PORT);
        return 1;
    }
    printf("[OK] Connected to GateServiced\n");
    close(sock);

    test_length_prefixed_messages();
    test_protobuf_like();
    test_binder_like();
    test_json_commands();
    test_raw_binary();
    test_keep_alive();

    printf("\n==========================================\n");
    printf("Probe Complete\n");
    printf("==========================================\n");

    return 0;
}
