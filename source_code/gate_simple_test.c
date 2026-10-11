/*
 * 簡單 GateServiced 連接測試
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

int main()
{
    printf("[*] GateServiced Simple Test\n");
    printf("[*] Connecting to %s:%d...\n", HOST, PORT);
    fflush(stdout);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        printf("[-] socket() failed: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] Socket created\n");
    fflush(stdout);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, HOST, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        printf("[-] connect() failed: %s\n", strerror(errno));
        close(sock);
        return 1;
    }
    printf("[+] Connected!\n");
    fflush(stdout);

    // 發送簡單測試包
    // cmd_id = 1, len = 8
    unsigned char pkt[] = {0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00};

    printf("[*] Sending test packet...\n");
    fflush(stdout);

    ssize_t sent = send(sock, pkt, sizeof(pkt), 0);
    printf("[+] Sent %zd bytes\n", sent);
    fflush(stdout);

    // 接收響應
    unsigned char buf[256];
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ssize_t received = recv(sock, buf, sizeof(buf), 0);
    if (received > 0)
    {
        printf("[+] Received %zd bytes:\n", received);
        for (int i = 0; i < received; i++)
        {
            printf("%02x ", buf[i]);
            if ((i + 1) % 16 == 0)
                printf("\n");
        }
        printf("\n");
    }
    else if (received == 0)
    {
        printf("[*] Connection closed by server\n");
    }
    else
    {
        printf("[-] recv() failed: %s\n", strerror(errno));
    }

    close(sock);
    printf("[*] Done\n");
    return 0;
}
