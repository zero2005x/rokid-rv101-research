/*
 * GateServiced Fuzzer for Rokid AR Glasses
 * 
 * Protocol Format (from logcat analysis):
 * [4 bytes: cmd_id (little-endian)]
 * [4 bytes: total_len (little-endian)]
 * [payload...]
 * 
 * Known error codes:
 * 0x300c = "TA is not already loaded"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#define GATESERVICED_PORT 8341
#define GATESERVICED_IP "127.0.0.1"
#define MAX_PAYLOAD_SIZE 65536
#define RECV_TIMEOUT_SEC 2

// Protocol structure
struct gate_packet {
    uint32_t cmd_id;
    uint32_t total_len;
    uint8_t payload[MAX_PAYLOAD_SIZE];
} __attribute__((packed));

// Fuzzing statistics
struct fuzz_stats {
    int total_tests;
    int connections_ok;
    int responses_received;
    int crashes_detected;
    int timeouts;
};

static struct fuzz_stats stats = {0};

// Connect to GateServiced
int connect_to_gate(void) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("[-] socket");
        return -1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(GATESERVICED_PORT);
    inet_pton(AF_INET, GATESERVICED_IP, &addr.sin_addr);

    // Set receive timeout
    struct timeval tv;
    tv.tv_sec = RECV_TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[-] connect");
        close(sock);
        return -1;
    }

    stats.connections_ok++;
    return sock;
}

// Send fuzz packet and check response
int send_fuzz_packet(int sock, struct gate_packet *pkt, size_t pkt_size) {
    ssize_t sent = send(sock, pkt, pkt_size, 0);
    if (sent < 0) {
        perror("[-] send");
        return -1;
    }

    // Try to receive response
    uint8_t recv_buf[4096];
    ssize_t recv_len = recv(sock, recv_buf, sizeof(recv_buf), 0);
    
    if (recv_len > 0) {
        stats.responses_received++;
        printf("[+] Received %zd bytes response!\n", recv_len);
        // Dump first 32 bytes
        printf("    ");
        for (int i = 0; i < (recv_len > 32 ? 32 : recv_len); i++) {
            printf("%02x ", recv_buf[i]);
        }
        printf("\n");
        return 1;
    } else if (recv_len == 0) {
        printf("[*] Connection closed by server\n");
        return 0;
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            stats.timeouts++;
            return 0;
        }
        perror("[-] recv");
        return -1;
    }
}

// Fuzz Test 1: Command ID enumeration
void fuzz_cmd_ids(void) {
    printf("\n[*] Fuzz Test 1: Command ID Enumeration\n");
    printf("    Testing cmd_id 0x00000000 to 0x00000100...\n");

    for (uint32_t cmd = 0; cmd <= 0x100; cmd++) {
        int sock = connect_to_gate();
        if (sock < 0) {
            printf("[-] Connection failed at cmd_id 0x%08x\n", cmd);
            stats.crashes_detected++;
            sleep(1);
            continue;
        }

        struct gate_packet pkt;
        pkt.cmd_id = cmd;
        pkt.total_len = 8;  // header only
        memset(pkt.payload, 0, 4);

        send_fuzz_packet(sock, &pkt, 12);
        close(sock);
        stats.total_tests++;
        usleep(10000);  // 10ms delay
    }
}

// Fuzz Test 2: Special command IDs
void fuzz_special_cmds(void) {
    printf("\n[*] Fuzz Test 2: Special Command IDs\n");
    
    uint32_t special_cmds[] = {
        0xFFFFFFFF,  // Max value
        0x7FFFFFFF,  // Max signed
        0x80000000,  // Min signed (as unsigned)
        0xDEADBEEF,  // Magic values
        0xCAFEBABE,
        0x41414141,
        0x00000001,  // INIT?
        0x00000002,  // LOAD_TA?
        0x00000003,  // OPEN_SESSION?
        0x00000004,  // CLOSE_SESSION?
        0x00000005,  // INVOKE_COMMAND?
        0x00001000,  // Higher ranges
        0x00010000,
        0x01000000,
        0x10000000,
    };

    for (size_t i = 0; i < sizeof(special_cmds)/sizeof(special_cmds[0]); i++) {
        int sock = connect_to_gate();
        if (sock < 0) {
            printf("[-] Connection failed at cmd_id 0x%08x\n", special_cmds[i]);
            stats.crashes_detected++;
            sleep(1);
            continue;
        }

        printf("    Testing cmd_id: 0x%08x\n", special_cmds[i]);

        struct gate_packet pkt;
        pkt.cmd_id = special_cmds[i];
        pkt.total_len = 8;
        memset(pkt.payload, 0, 4);

        send_fuzz_packet(sock, &pkt, 12);
        close(sock);
        stats.total_tests++;
        usleep(50000);
    }
}

// Fuzz Test 3: Length field manipulation
void fuzz_lengths(void) {
    printf("\n[*] Fuzz Test 3: Length Field Manipulation\n");

    uint32_t test_lengths[] = {
        0,           // Zero length
        1,           // Too small
        4,           // Minimum?
        8,           // Header only
        0x7F,        // Small
        0xFF,
        0x100,
        0x1000,
        0x4000,      // 16KB
        0x5000,      // 20KB (output_len)
        0x10000,     // 64KB
        0xFFFF,      // Max 16-bit
        0x7FFFFFFF,  // Max signed 32-bit
        0xFFFFFFFF,  // Max 32-bit
    };

    for (size_t i = 0; i < sizeof(test_lengths)/sizeof(test_lengths[0]); i++) {
        int sock = connect_to_gate();
        if (sock < 0) {
            printf("[-] Connection failed at len 0x%08x\n", test_lengths[i]);
            stats.crashes_detected++;
            sleep(1);
            continue;
        }

        printf("    Testing total_len: 0x%08x\n", test_lengths[i]);

        struct gate_packet pkt;
        pkt.cmd_id = 1;  // Simple command
        pkt.total_len = test_lengths[i];
        
        // Send header + small payload
        size_t send_size = 8;  // Just the header
        if (test_lengths[i] <= 4096) {
            send_size = test_lengths[i] + 4;  // cmd_id(4) + data
            memset(pkt.payload, 'A', test_lengths[i] > 4 ? test_lengths[i] - 4 : 0);
        }

        send_fuzz_packet(sock, &pkt, send_size);
        close(sock);
        stats.total_tests++;
        usleep(50000);
    }
}

// Fuzz Test 4: Buffer overflow attempts
void fuzz_overflow(void) {
    printf("\n[*] Fuzz Test 4: Buffer Overflow Attempts\n");

    int sock = connect_to_gate();
    if (sock < 0) {
        printf("[-] Connection failed\n");
        return;
    }

    // Allocate large buffer
    size_t huge_size = 65536 + 8;
    uint8_t *huge_buf = malloc(huge_size);
    if (!huge_buf) {
        printf("[-] malloc failed\n");
        close(sock);
        return;
    }

    // Set header
    uint32_t *header = (uint32_t*)huge_buf;
    header[0] = 1;       // cmd_id
    header[1] = 65540;   // total_len (lie about size)

    // Fill with pattern
    memset(huge_buf + 8, 'A', 65536);

    printf("    Sending 65KB payload...\n");
    ssize_t sent = send(sock, huge_buf, huge_size, 0);
    printf("    Sent %zd bytes\n", sent);

    // Wait for crash
    sleep(2);

    // Try to reconnect to check if service crashed
    close(sock);
    free(huge_buf);

    sock = connect_to_gate();
    if (sock < 0) {
        printf("[!] Service may have crashed! Cannot reconnect.\n");
        stats.crashes_detected++;
    } else {
        printf("[*] Service still running\n");
        close(sock);
    }

    stats.total_tests++;
}

// Fuzz Test 5: Format string payloads
void fuzz_format_strings(void) {
    printf("\n[*] Fuzz Test 5: Format String Payloads\n");

    const char *format_payloads[] = {
        "%s%s%s%s%s%s%s%s%s%s",
        "%n%n%n%n%n%n%n%n%n%n",
        "%x%x%x%x%x%x%x%x%x%x",
        "%p%p%p%p%p%p%p%p%p%p",
        "AAAA%08x.%08x.%08x.%08x.%08x.%n",
        "%d%d%d%d%d%d%d%d%d%d",
        "%s" "\x00" "%s" "\x00" "%s",
    };

    for (size_t i = 0; i < sizeof(format_payloads)/sizeof(format_payloads[0]); i++) {
        int sock = connect_to_gate();
        if (sock < 0) {
            printf("[-] Connection failed\n");
            stats.crashes_detected++;
            sleep(1);
            continue;
        }

        printf("    Testing format string: %.20s...\n", format_payloads[i]);

        struct gate_packet pkt;
        pkt.cmd_id = 1;
        size_t payload_len = strlen(format_payloads[i]) + 1;
        pkt.total_len = 4 + payload_len;
        memcpy(pkt.payload, format_payloads[i], payload_len);

        send_fuzz_packet(sock, &pkt, 8 + payload_len);
        close(sock);
        stats.total_tests++;
        usleep(100000);
    }
}

// Fuzz Test 6: NULL and special bytes
void fuzz_special_bytes(void) {
    printf("\n[*] Fuzz Test 6: NULL and Special Bytes\n");

    uint8_t patterns[][16] = {
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // All NULL
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},  // All 0xFF
        {0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF},  // Alternating
        {0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A},  // Newlines
        {0x0D, 0x0A, 0x0D, 0x0A, 0x0D, 0x0A, 0x0D, 0x0A},  // CRLF
        {0x7F, 0x45, 0x4C, 0x46, 0x00, 0x00, 0x00, 0x00},  // ELF header
    };

    for (size_t i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
        int sock = connect_to_gate();
        if (sock < 0) {
            printf("[-] Connection failed\n");
            stats.crashes_detected++;
            sleep(1);
            continue;
        }

        printf("    Testing pattern %zu\n", i);

        struct gate_packet pkt;
        pkt.cmd_id = 1;
        pkt.total_len = 20;
        memcpy(pkt.payload, patterns[i], 16);

        send_fuzz_packet(sock, &pkt, 24);
        close(sock);
        stats.total_tests++;
        usleep(50000);
    }
}

// Fuzz Test 7: TEE-style command structures
void fuzz_tee_commands(void) {
    printf("\n[*] Fuzz Test 7: TEE-style Command Structures\n");

    // Try GlobalPlatform TEE API style commands
    // TEEC_OpenSession, TEEC_InvokeCommand, etc.
    
    struct tee_cmd {
        uint32_t cmd_id;
        uint32_t total_len;
        uint32_t session_id;
        uint32_t command_id;
        uint32_t param_types;
        uint32_t params[4];
    } __attribute__((packed));

    struct tee_cmd cmds[] = {
        // Open Session attempt
        {.cmd_id = 0x00000001, .total_len = 32, .session_id = 0, .command_id = 0, .param_types = 0, .params = {0,0,0,0}},
        // Invoke Command attempt
        {.cmd_id = 0x00000002, .total_len = 32, .session_id = 1, .command_id = 1, .param_types = 0, .params = {0,0,0,0}},
        // Close Session
        {.cmd_id = 0x00000003, .total_len = 32, .session_id = 1, .command_id = 0, .param_types = 0, .params = {0,0,0,0}},
        // Load TA (hypothetical)
        {.cmd_id = 0x00000010, .total_len = 32, .session_id = 0, .command_id = 0, .param_types = 0, .params = {0,0,0,0}},
        // Init (hypothetical)
        {.cmd_id = 0x00000100, .total_len = 32, .session_id = 0, .command_id = 0, .param_types = 0, .params = {0,0,0,0}},
    };

    for (size_t i = 0; i < sizeof(cmds)/sizeof(cmds[0]); i++) {
        int sock = connect_to_gate();
        if (sock < 0) {
            printf("[-] Connection failed\n");
            stats.crashes_detected++;
            sleep(1);
            continue;
        }

        printf("    Testing TEE cmd_id: 0x%08x\n", cmds[i].cmd_id);

        send_fuzz_packet(sock, (struct gate_packet*)&cmds[i], sizeof(struct tee_cmd));
        close(sock);
        stats.total_tests++;
        usleep(50000);
    }
}

// Fuzz Test 8: Rapid fire stress test
void fuzz_stress_test(int iterations) {
    printf("\n[*] Fuzz Test 8: Stress Test (%d iterations)\n", iterations);

    for (int i = 0; i < iterations; i++) {
        int sock = connect_to_gate();
        if (sock < 0) {
            printf("[-] Connection failed at iteration %d\n", i);
            stats.crashes_detected++;
            sleep(1);
            continue;
        }

        struct gate_packet pkt;
        pkt.cmd_id = rand() % 0x1000;
        pkt.total_len = 8 + (rand() % 100);
        memset(pkt.payload, rand() % 256, 100);

        send_fuzz_packet(sock, &pkt, 8 + 100);
        close(sock);
        stats.total_tests++;

        if (i % 100 == 0) {
            printf("    Progress: %d/%d\n", i, iterations);
        }
    }
}

void print_stats(void) {
    printf("\n========================================\n");
    printf("         FUZZING STATISTICS\n");
    printf("========================================\n");
    printf("Total tests:        %d\n", stats.total_tests);
    printf("Connections OK:     %d\n", stats.connections_ok);
    printf("Responses received: %d\n", stats.responses_received);
    printf("Crashes detected:   %d\n", stats.crashes_detected);
    printf("Timeouts:           %d\n", stats.timeouts);
    printf("========================================\n");
}

int main(int argc, char *argv[]) {
    printf("=========================================\n");
    printf("  GateServiced Fuzzer for Rokid Glasses\n");
    printf("  Target: %s:%d\n", GATESERVICED_IP, GATESERVICED_PORT);
    printf("=========================================\n");

    srand(time(NULL));

    // Run all fuzz tests
    fuzz_cmd_ids();
    fuzz_special_cmds();
    fuzz_lengths();
    fuzz_overflow();
    fuzz_format_strings();
    fuzz_special_bytes();
    fuzz_tee_commands();
    fuzz_stress_test(500);

    print_stats();

    return 0;
}
