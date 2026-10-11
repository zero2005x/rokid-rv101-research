/*
 * GateServiced Fuzzer (C Version)
 * Target: Root service on 0.0.0.0:8341
 *
 * Protocol: [4 bytes: cmd_id] [4 bytes: total_len] [payload]
 * Known Error: 0x300c = "TA not loaded"
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

const char *get_error_name(unsigned int code)
{
    switch (code)
    {
    case 0x0000:
        return "SUCCESS";
    case 0x3001:
        return "GENERAL_ERROR";
    case 0x3002:
        return "INVALID_PARAMETER";
    case 0x3003:
        return "NOT_SUPPORTED";
    case 0x3004:
        return "OUT_OF_MEMORY";
    case 0x3005:
        return "BUSY";
    case 0x3006:
        return "ACCESS_DENIED";
    case 0x3007:
        return "TIMEOUT";
    case 0x300c:
        return "TA_NOT_LOADED";
    case 0x300d:
        return "TA_DEAD";
    case 0x300e:
        return "SESSION_NOT_FOUND";
    case 0x300f:
        return "CMD_NOT_SUPPORTED";
    default:
        return "UNKNOWN";
    }
}

int connect_to_service(void)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return -1;

    struct timeval tv = {2, 0};
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
        return -1;
    }

    return sock;
}

int send_command(unsigned int cmd_id, unsigned int total_len,
                 const void *payload, size_t payload_len,
                 unsigned char *response, size_t *resp_len)
{
    int sock = connect_to_service();
    if (sock < 0)
        return -1;

    /* Build packet */
    unsigned char header[8];
    memcpy(header, &cmd_id, 4);
    memcpy(header + 4, &total_len, 4);

    /* Send header */
    if (send(sock, header, 8, 0) != 8)
    {
        close(sock);
        return -2;
    }

    /* Send payload if any */
    if (payload && payload_len > 0)
    {
        if (send(sock, payload, payload_len, 0) != (ssize_t)payload_len)
        {
            close(sock);
            return -3;
        }
    }

    /* Receive response */
    *resp_len = 0;
    ssize_t n;
    while ((n = recv(sock, response + *resp_len, 4096 - *resp_len, 0)) > 0)
    {
        *resp_len += n;
        if (*resp_len >= 4096)
            break;
    }

    close(sock);
    return 0;
}

void hexdump(const unsigned char *buf, size_t len)
{
    for (size_t i = 0; i < len && i < 64; i++)
    {
        printf("%02x ", buf[i]);
        if ((i + 1) % 16 == 0)
            printf("\n    ");
    }
    if (len > 64)
        printf("... [%zu more bytes]", len - 64);
    printf("\n");
}

void fuzz_cmd_ids(void)
{
    printf("\n=== Phase 1: Command ID Enumeration ===\n");

    unsigned char response[4096];
    size_t resp_len;

    /* Test ranges */
    unsigned int test_cmds[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
        0x10, 0x20, 0x30, 0x40, 0x50,
        0x100, 0x200, 0x300, 0x400, 0x500,
        0x1000, 0x2000, 0x3000, 0x4000, 0x5000,
        0x10000, 0x20000, 0x100000,
        0x47415445, /* "GATE" */
        0x51534545, /* "QSEE" */
        0xDEADBEEF,
        0xCAFEBABE,
        0xFFFFFFFF};

    for (int i = 0; i < sizeof(test_cmds) / sizeof(test_cmds[0]); i++)
    {
        unsigned int cmd = test_cmds[i];
        int ret = send_command(cmd, 8, NULL, 0, response, &resp_len);

        if (ret < 0)
        {
            printf("  cmd=0x%08x -> CONNECTION FAILED\n", cmd);
            continue;
        }

        if (resp_len >= 4)
        {
            unsigned int err;
            memcpy(&err, response, 4);
            const char *name = get_error_name(err);

            /* Print if not the common TA_NOT_LOADED */
            if (err != 0x300c || resp_len > 8)
            {
                printf("  cmd=0x%08x -> %s (0x%08x) [%zu bytes]\n",
                       cmd, name, err, resp_len);
                if (resp_len > 4)
                {
                    printf("    ");
                    hexdump(response, resp_len);
                }
            }
        }

        usleep(50000); /* 50ms delay */
    }
}

void fuzz_payload_sizes(void)
{
    printf("\n=== Phase 2: Payload Size Fuzzing ===\n");

    unsigned char response[4096];
    size_t resp_len;
    unsigned char payload[65536];
    memset(payload, 'A', sizeof(payload));

    size_t sizes[] = {
        0, 1, 4, 7, 8, 15, 16, 31, 32, 63, 64,
        127, 128, 255, 256, 511, 512, 1023, 1024,
        2047, 2048, 4095, 4096, 8191, 8192,
        16383, 16384, 32767, 32768, 65535};

    for (int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
        size_t size = sizes[i];
        unsigned int total_len = 8 + size;

        int ret = send_command(0, total_len, payload, size, response, &resp_len);

        if (ret < 0)
        {
            printf("  size=%5zu -> CONNECTION FAILED (crash?)\n", size);
            /* Try to reconnect */
            sleep(1);
            int sock = connect_to_service();
            if (sock < 0)
            {
                printf("    !!! SERVICE CRASHED !!!\n");
                return;
            }
            close(sock);
            continue;
        }

        if (resp_len >= 4)
        {
            unsigned int err;
            memcpy(&err, response, 4);
            printf("  size=%5zu -> %s\n", size, get_error_name(err));
        }
        else
        {
            printf("  size=%5zu -> NO RESPONSE\n", size);
        }

        usleep(50000);
    }
}

void fuzz_length_mismatch(void)
{
    printf("\n=== Phase 3: Length Field Mismatch ===\n");

    unsigned char response[4096];
    size_t resp_len;
    unsigned char payload[64];
    memset(payload, 'X', sizeof(payload));

    /* Actual payload is 64 bytes, fake different lengths */
    unsigned int fake_lens[] = {
        0,          /* Zero */
        4,          /* Less than header */
        8,          /* Header only */
        64,         /* Payload only, no header */
        72,         /* Correct */
        71,         /* Off by one short */
        73,         /* Off by one long */
        144,        /* Double */
        0x7FFFFFFF, /* Max signed */
        0x80000000, /* Sign bit */
        0xFFFFFFFF, /* Max unsigned */
        0xFFFFFFFE, /* Max - 1 */
    };

    for (int i = 0; i < sizeof(fake_lens) / sizeof(fake_lens[0]); i++)
    {
        unsigned int fake_len = fake_lens[i];

        int ret = send_command(0, fake_len, payload, 64, response, &resp_len);

        if (ret < 0)
        {
            printf("  total_len=0x%08x -> CONNECTION FAILED\n", fake_len);
            sleep(1);
            continue;
        }

        if (resp_len >= 4)
        {
            unsigned int err;
            memcpy(&err, response, 4);
            printf("  total_len=0x%08x -> %s [%zu bytes]\n",
                   fake_len, get_error_name(err), resp_len);
        }
        else
        {
            printf("  total_len=0x%08x -> NO RESPONSE\n", fake_len);
        }

        usleep(50000);
    }
}

void fuzz_ta_names(void)
{
    printf("\n=== Phase 4: TA Name Probing ===\n");

    unsigned char response[4096];
    size_t resp_len;

    /* Common TA names on Qualcomm */
    const char *ta_names[] = {
        "fingerprint",
        "keymaster",
        "gatekeeper",
        "widevine",
        "hdcp",
        "sec_ui",
        "rokid",
        "camera",
        "secure_touch",
        "playready",
        "SampleClient",
        "sampleta",
        "qseecom",
        "tzlog",
        "tzdrm",
        "drm",
    };

    for (int i = 0; i < sizeof(ta_names) / sizeof(ta_names[0]); i++)
    {
        unsigned char payload[48];
        memset(payload, 0, sizeof(payload));

        /* Copy TA name (padded to 32 bytes) */
        strncpy((char *)payload, ta_names[i], 31);

        /* Add sub-command ID */
        unsigned int sub_cmd = 0;
        memcpy(payload + 32, &sub_cmd, 4);

        unsigned int total_len = 8 + sizeof(payload);
        int ret = send_command(0, total_len, payload, sizeof(payload), response, &resp_len);

        if (ret >= 0 && resp_len >= 4)
        {
            unsigned int err;
            memcpy(&err, response, 4);

            /* Print if different from TA_NOT_LOADED */
            if (err != 0x300c)
            {
                printf("  TA='%s' -> %s (0x%08x)\n",
                       ta_names[i], get_error_name(err), err);
            }
        }

        usleep(50000);
    }
}

void probe_protocol(void)
{
    printf("\n=== Protocol Probe ===\n");

    int sock = connect_to_service();
    if (sock < 0)
    {
        printf("Cannot connect to service\n");
        return;
    }
    printf("Connected to %s:%d\n", HOST, PORT);

    /* Send minimal header */
    unsigned char header[8] = {0, 0, 0, 0, 8, 0, 0, 0}; /* cmd=0, len=8 */
    send(sock, header, 8, 0);

    unsigned char response[4096];
    ssize_t n = recv(sock, response, sizeof(response), 0);

    if (n > 0)
    {
        printf("Response (%zd bytes):\n  ", n);
        hexdump(response, n);

        if (n >= 4)
        {
            unsigned int err;
            memcpy(&err, response, 4);
            printf("Error code: 0x%08x = %s\n", err, get_error_name(err));
        }
    }
    else
    {
        printf("No response (errno=%d)\n", errno);
    }

    close(sock);
}

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);

    printf("==========================================\n");
    printf("GateServiced Fuzzer\n");
    printf("Target: %s:%d (root service)\n", HOST, PORT);
    printf("==========================================\n");

    /* Verify connectivity */
    int sock = connect_to_service();
    if (sock < 0)
    {
        printf("[!] Cannot connect to service: %s\n", strerror(errno));
        return 1;
    }
    close(sock);
    printf("[+] Connection successful\n");

    /* Run phases */
    probe_protocol();
    fuzz_cmd_ids();
    fuzz_payload_sizes();
    fuzz_length_mismatch();
    fuzz_ta_names();

    printf("\n==========================================\n");
    printf("Fuzzing Complete\n");
    printf("==========================================\n");

    return 0;
}
