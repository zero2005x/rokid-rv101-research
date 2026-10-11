/*
 * adb_tcp_relay.c — 把「TCP 進來的 adb client」接到裝置上真正的 adbd。
 *
 * 背景：RV101（Rokid RG-glasses, Android 12 / ro.debuggable=0）的 adbd 不綁 TCP
 *       （service.adb.tcp.port / persist.adb.tcp.port 皆無效，實測 0 listener），
 *       但 **adbd 本身是完好的**——它只是把 transport 綁在 USB（FunctionFS）上。
 *       本程式在裝置端開一個 TCP listener，把每條連線原封不動接到 adbd 的
 *       local socket（/dev/socket/adbd），於是 PC／手機／筆電就能用
 *       標準 `adb connect <ip>:<port>` 連進來，**認證、shell、sync、forward
 *       全部由原廠 adbd 處理**（含 adb_keys 授權），不需自製 ADB 協定。
 *
 * 為什麼不是「重啟 adbd」：本 build 重啟 adbd 會掉 USB 列舉（已記載的坑），
 *       且 init 會把它拉回 USB-only。本程式是**額外**開一個入口，不動原 process。
 *
 * 邊界條件（不是萬靈丹）：
 *   - 若 adbd 根本沒開 local socket（純 FunctionFS），relay 會 connect 失敗。
 *     此時的替代路線是 `adbd_tcp.sh`（自己再跑一個 adbd 只聽 TCP）。
 *   - relay 以誰的身分跑，不影響 shell 的 uid：adbd 一律降成 shell(2000)，
 *     除非 build 允許 `adb root`。要 root shell 請走 root_daemon.sock。
 *   - 沒有加密、沒有來源驗證：能連到這個 port 的人就等同插上 USB。
 *     限定內網（SoftAP 192.168.43.0/24）或搭配 bind 位址使用。
 *
 * 建置（本工作區；靜態 aarch64）：
 *   bash deploy/build_probe_zig.sh deploy/adb_tcp_relay.c adb_tcp_relay
 *
 * 用法：
 *   adb_tcp_relay [-p PORT] [-s UNIX_SOCKET] [-b BIND_ADDR] [-l LOGFILE] [-d]
 *   預設：PORT=5555, UNIX_SOCKET=/dev/socket/adbd, BIND_ADDR=0.0.0.0（-d 前景）
 *   PC：adb connect <glasses-ip>:5555
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define DEF_PORT   5555
#define DEF_SOCK   "/dev/socket/adbd"
#define BUF_SIZE   65536
#define DRAIN_SECS 5    /* 單邊 EOF 後，另一邊最多再撐幾秒把殘檔傳完 */

static volatile sig_atomic_t g_stop = 0;
static void on_term(int sig) { (void)sig; g_stop = 1; }

static void logmsg(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

static int set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return -1;
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

/* 連到裝置上的 adbd local socket。成功回 fd，失敗回 -1。
 *
 * ⚠ 2026-09-20 實測（RV101 / Android 12）：**adbd 的 local socket 是 SOCK_SEQPACKET，
 *   不是 SOCK_STREAM**。用 SOCK_STREAM 連會得到 EPROTOTYPE（errno 91），
 *   即使 socket() 本身正確建立 AF_UNIX/SOCK_STREAM 也一樣。
 *   （`/proc/net/unix` 的型別欄位在本機顯示 `0002`，與此結論不符，**不可作為判準**；
 *     以實際 connect() 結果為準。）
 */
static int connect_unix(const char *path) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) {
        logmsg("[relay] socket(AF_UNIX) 失敗: %s", strerror(errno));
        return -1;
    }

    /* 診斷：確認拿到的真的是 AF_UNIX */
    struct sockaddr_storage ss;
    socklen_t sl = sizeof(ss);
    if (getsockname(fd, (struct sockaddr *)&ss, &sl) == 0) {
        if (ss.ss_family != AF_UNIX) {
            logmsg("[relay] 警告：socket() 回傳 family=%d（預期 AF_UNIX=%d）", ss.ss_family, AF_UNIX);
        }
    }

    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, path, sizeof(sa.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        int e = errno;
        logmsg("[relay] connect(%s) 失敗: %s (errno=%d)", path, strerror(e), e);
        close(fd);
        errno = e;
        return -1;
    }
    return fd;
}

/* 單向搬運。回傳 1 = 來源 EOF（已 shutdown 目的端寫入），0 = 無事，-1 = 致命錯誤。 */
static int pump(int from, int to, int *eof) {
    char buf[BUF_SIZE];
    for (;;) {
        ssize_t n = read(from, buf, sizeof(buf));
        if (n == 0) {
            *eof = 1;
            shutdown(to, SHUT_WR);
            return 1;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            return -1;
        }
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(to, buf + off, (size_t)(n - off));
            if (w < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    /* 對端塞住：短暫等待後重試（TCP 對 Unix socket 都適用） */
                    fd_set wf;
                    FD_ZERO(&wf);
                    FD_SET(to, &wf);
                    struct timeval tv = {1, 0};
                    if (select(to + 1, NULL, &wf, NULL, &tv) < 0 && errno != EINTR)
                        return -1;
                    continue;
                }
                return -1;
            }
            off += w;
        }
    }
}

/* 一條連線：雙向搬運直到兩邊都收完或逾時。 */
static void handle(int client, const char *unix_path) {
    struct sockaddr_storage peer;
    socklen_t plen = sizeof(peer);
    char peerstr[64] = "?";
    if (getpeername(client, (struct sockaddr *)&peer, &plen) == 0) {
        if (peer.ss_family == AF_INET) {
            struct sockaddr_in *in = (struct sockaddr_in *)&peer;
            inet_ntop(AF_INET, &in->sin_addr, peerstr, sizeof(peerstr));
        } else if (peer.ss_family == AF_INET6) {
            struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)&peer;
            inet_ntop(AF_INET6, &in6->sin6_addr, peerstr, sizeof(peerstr));
        }
    }

    int up = connect_unix(unix_path);
    if (up < 0) {
        logmsg("[relay] %s: connect %s 失敗: %s", peerstr, unix_path, strerror(errno));
        const char *msg = "relay: adbd local socket unavailable\n";
        (void)!write(client, msg, strlen(msg));
        close(client);
        return;
    }
    logmsg("[relay] %s: 已連上 %s", peerstr, unix_path);

    set_nonblock(client);
    set_nonblock(up);

    int eof_c2u = 0, eof_u2c = 0;
    time_t last_eof = 0;

    for (;;) {
        fd_set rf;
        FD_ZERO(&rf);
        int maxfd = -1;
        if (!eof_c2u) { FD_SET(client, &rf); if (client > maxfd) maxfd = client; }
        if (!eof_u2c) { FD_SET(up, &rf);     if (up > maxfd)     maxfd = up; }
        if (maxfd < 0) break;   /* 兩邊都 EOF */

        struct timeval tv = {1, 0};
        int rc = select(maxfd + 1, &rf, NULL, NULL, &tv);
        if (rc < 0) {
            if (errno == EINTR) { if (g_stop) break; continue; }
            break;
        }
        if (rc == 0) {
            if (last_eof && time(NULL) - last_eof > DRAIN_SECS) break;
            continue;
        }

        if (!eof_c2u && FD_ISSET(client, &rf)) {
            int r = pump(client, up, &eof_c2u);
            if (r < 0) break;
            if (r == 1) last_eof = time(NULL);
        }
        if (!eof_u2c && FD_ISSET(up, &rf)) {
            int r = pump(up, client, &eof_u2c);
            if (r < 0) break;
            if (r == 1) last_eof = time(NULL);
        }
        if (g_stop) break;
    }

    close(up);
    close(client);
    logmsg("[relay] %s: 連線結束", peerstr);
}

int main(int argc, char **argv) {
    int port = DEF_PORT;
    const char *unix_path = DEF_SOCK;
    const char *bind_addr = "0.0.0.0";
    const char *logfile = NULL;
    int foreground = 0;

    int opt;
    while ((opt = getopt(argc, argv, "p:s:b:l:dh")) != -1) {
        switch (opt) {
        case 'p': port = atoi(optarg); break;
        case 's': unix_path = optarg; break;
        case 'b': bind_addr = optarg; break;
        case 'l': logfile = optarg; break;
        case 'd': foreground = 1; break;
        default:
            fprintf(stderr,
                "用法: %s [-p PORT] [-s UNIX_SOCKET] [-b BIND_ADDR] [-l LOG] [-d]\n"
                "  預設 PORT=%d UNIX_SOCKET=%s BIND_ADDR=0.0.0.0\n",
                argv[0], DEF_PORT, DEF_SOCK);
            return 2;
        }
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);
    signal(SIGHUP, SIG_IGN);

    /* 守護化（未指定 -d 時） */
    if (!foreground) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return 1; }
        if (pid > 0) return 0;          /* 父行程立刻返回 */
        setsid();
        if (logfile) {
            int lf = open(logfile, O_WRONLY | O_CREAT | O_APPEND, 0666);
            if (lf >= 0) { dup2(lf, 1); dup2(lf, 2); if (lf > 2) close(lf); }
        } else {
            int dn = open("/dev/null", O_RDWR);
            if (dn >= 0) { dup2(dn, 0); dup2(dn, 1); dup2(dn, 2); if (dn > 2) close(dn); }
        }
    }

    /* 先驗證 adbd local socket 是否真的在（早失敗、清楚回報） */
    int probe = connect_unix(unix_path);
    if (probe < 0) {
        logmsg("[relay] 致命：adbd socket %s 不可連（%s）", unix_path, strerror(errno));
        logmsg("[relay] → 本 build 的 adbd 可能只用 USB FunctionFS；改用 adbd_tcp.sh 自行起 adbd");
        return 3;
    }
    close(probe);

    int srv = socket(AF_INET6, SOCK_STREAM, 0);
    int v6 = 1;
    if (srv < 0) { srv = socket(AF_INET, SOCK_STREAM, 0); v6 = 0; }
    if (srv < 0) { perror("socket"); return 1; }

    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (v6) {
        int off = 0;
        setsockopt(srv, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
    }

    int rc;
    if (v6) {
        struct sockaddr_in6 sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin6_family = AF_INET6;
        sa.sin6_port = htons((uint16_t)port);
        if (strcmp(bind_addr, "0.0.0.0") == 0) {
            sa.sin6_addr = in6addr_any;
        } else if (inet_pton(AF_INET6, bind_addr, &sa.sin6_addr) != 1) {
            struct in_addr a4;
            if (inet_pton(AF_INET, bind_addr, &a4) != 1) {
                logmsg("[relay] 無法解析 bind 位址 %s", bind_addr);
                return 2;
            }
            /* v4-mapped */
            unsigned char *p = sa.sin6_addr.s6_addr;
            memset(p, 0, 16); p[10] = 0xff; p[11] = 0xff;
            memcpy(p + 12, &a4, 4);
        }
        rc = bind(srv, (struct sockaddr *)&sa, sizeof(sa));
    } else {
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons((uint16_t)port);
        sa.sin_addr.s_addr = htonl(INADDR_ANY);
        rc = bind(srv, (struct sockaddr *)&sa, sizeof(sa));
    }
    if (rc < 0) { logmsg("[relay] bind :%d 失敗: %s", port, strerror(errno)); return 1; }
    if (listen(srv, 16) < 0) { perror("listen"); return 1; }

    logmsg("[relay] 開始監聽 TCP :%d → %s (pid=%d)", port, unix_path, (int)getpid());

    for (;;) {
        int c = accept(srv, NULL, NULL);
        if (c < 0) {
            if (errno == EINTR) { if (g_stop) break; continue; }
            continue;
        }

        pid_t p = fork();
        if (p == 0) {
            close(srv);
            handle(c, unix_path);
            _exit(0);
        }
        close(c);
        while (waitpid(-1, NULL, WNOHANG) > 0) { }
        if (g_stop) break;
    }

    close(srv);
    logmsg("[relay] 結束");
    return 0;
}
