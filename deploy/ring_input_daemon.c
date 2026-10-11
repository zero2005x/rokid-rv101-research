/*
 * ⚠️ 備援實作（未採用）：本版路徑 B 改用裝置內建的 `getevent`（由 root daemon 背景執行、
 *    App 輪詢增量），因此不需要隨 APK 附帶 native binary。保留本檔是為了未來若需要更低延遲
 *    （事件驅動、可讀取 ABS/手勢而非輪詢）時的現成實作；本機 WSL 無 aarch64 linker 且 NDK 為
 *    Windows 版，故未在此環境建置。
 */
/*
 * ring_input_daemon.c — 以 root 直讀 /dev/input/eventX 的手勢守護程式（路徑 B）。
 *
 * 為什麼需要它：
 *   眼鏡端 App 是 untrusted_app，讀不到 /dev/input/*；但 reboot 後 root_daemon（root）
 *   可以把它拉起來。本程式：
 *     1. 自動挑選輸入裝置（--name 子字串比對，例如戒指 "RGR" 或眼鏡觸控板 "TP"）
 *     2. 讀 evdev 事件 → 辨識手勢（單擊／雙擊／三擊／長按／滾輪）
 *     3. 把結果以行導向文字寫到 UNIX socket 給 App（可多個 client）
 *
 * 用法：
 *   ring_input_daemon --list                     列出所有輸入裝置後結束
 *   ring_input_daemon [--device /dev/input/eventN] [--name 子字串]
 *                      [--socket /data/local/tmp/ring_input.sock]
 *                      [--tap-ms 400] [--double-ms 450] [--long-ms 700]
 *
 * 輸出（每行一筆，ASCII）：
 *   HELLO <device-name>           client 連上時送出
 *   KEY <code> <down|up>          原始按鍵（供「手勢學習」用）
 *   TAP | DOUBLE_TAP | TRIPLE_TAP | LONG_PRESS
 *   SCROLL_UP | SCROLL_DOWN
 *
 * 建置（aarch64）：
 *   <NDK>/toolchains/llvm/prebuilt/<host>/bin/aarch64-linux-android24-clang \
 *       -O2 -o ring_input_daemon ring_input_daemon.c
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdarg.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define SOCK_PATH_DEFAULT "/data/local/tmp/ring_input.sock"
#define MAX_CLIENTS 8
#define NAME_MAX_LEN 128

static int g_clients[MAX_CLIENTS];
static int g_nclients = 0;
static char g_devname[NAME_MAX_LEN] = "unknown";

/* ---- 手勢參數（可由命令列覆寫） ---- */
static long tap_ms = 400;
static long double_ms = 450;
static long long_ms = 700;

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void send_line(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    buf[n++] = '\n';
    for (int i = 0; i < g_nclients; i++) {
        ssize_t w = write(g_clients[i], buf, (size_t)n);
        if (w < 0) {
            close(g_clients[i]);
            g_clients[i] = g_clients[--g_nclients];
            i--;
        }
    }
    /* 也印到 stdout（由 root_daemon 導向 log 檔） */
    fwrite(buf, 1, (size_t)n, stdout);
    fflush(stdout);
}

/* 會被視為「點一下」的鍵：觸控板 BTN_TOUCH、滑鼠鍵、確認鍵、自訂鍵 */
static int is_tap_like(unsigned short code) {
    switch (code) {
        case BTN_TOUCH:      /* 330 觸控板 */
        case BTN_LEFT:       /* 272 滑鼠左鍵（BTN_MOUSE 同值） */
        case BTN_RIGHT:      /* 273 */
        case BTN_MIDDLE:     /* 274 */
        case BTN_0:
        case BTN_1:
        case BTN_TRIGGER:
        case KEY_ENTER:
        case KEY_KPENTER:
        case KEY_SELECT:
        case KEY_OK:
        case KEY_PLAYPAUSE:
        case KEY_HOMEPAGE:
        case KEY_BACK:
        case KEY_FORWARD:
        case KEY_VOLUMEUP:
        case KEY_VOLUMEDOWN:
            return 1;
        default:
            return 0;
    }
}

int main(int argc, char **argv) {
    char device[256] = "";
    char want_name[NAME_MAX_LEN] = "";
    char sock_path[256] = SOCK_PATH_DEFAULT;
    int list_only = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--list")) list_only = 1;
        else if (!strcmp(argv[i], "--device") && i + 1 < argc) snprintf(device, sizeof(device), "%s", argv[++i]);
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) snprintf(want_name, sizeof(want_name), "%s", argv[++i]);
        else if (!strcmp(argv[i], "--socket") && i + 1 < argc) snprintf(sock_path, sizeof(sock_path), "%s", argv[++i]);
        else if (!strcmp(argv[i], "--tap-ms") && i + 1 < argc) tap_ms = atol(argv[++i]);
        else if (!strcmp(argv[i], "--double-ms") && i + 1 < argc) double_ms = atol(argv[++i]);
        else if (!strcmp(argv[i], "--long-ms") && i + 1 < argc) long_ms = atol(argv[++i]);
    }

    /* ---- 挑裝置 ---- */
    char found[256] = "";
    for (int i = 0; i < 32; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        char name[NAME_MAX_LEN] = "";
        if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0) name[0] = 0;
        close(fd);
        if (list_only) {
            printf("EVENT %s %s\n", path, name);
            continue;
        }
        if (!strlen(name)) continue;
        if (strlen(device)) {
            if (!strcmp(path, device)) { snprintf(found, sizeof(found), "%s", path); snprintf(g_devname, sizeof(g_devname), "%s", name); break; }
            continue;
        }
        if (strlen(want_name)) {
            /* 大小寫無關的子字串比對 */
            char ln[NAME_MAX_LEN], lw[NAME_MAX_LEN];
            snprintf(ln, sizeof(ln), "%s", name);
            snprintf(lw, sizeof(lw), "%s", want_name);
            for (char *p = ln; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
            for (char *p = lw; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
            if (!strstr(ln, lw)) continue;
        }
        snprintf(found, sizeof(found), "%s", path);
        snprintf(g_devname, sizeof(g_devname), "%s", name);
        break;
    }
    if (list_only) return 0;
    if (!strlen(found)) {
        fprintf(stderr, "ERR no matching input device (--name '%s' --device '%s')\n", want_name, device);
        return 2;
    }

    int dev = open(found, O_RDONLY | O_NONBLOCK);
    if (dev < 0) {
        fprintf(stderr, "ERR open %s: %s\n", found, strerror(errno));
        return 3;
    }
    printf("INFO using %s (%s)\n", found, g_devname);
    fflush(stdout);

    /* ---- socket ---- */
    signal(SIGPIPE, SIG_IGN);
    unlink(sock_path);
    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 4; }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock_path);
    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 5; }
    /*
     * 權限：0600（原本是 0777）。
     *
     * 0777 會讓**任何 uid**（含其他 App 與 adb shell）連上這個 socket 並讀走
     * 使用者的手勢串流（等同旁路得知使用者何時按了戒指／觸控板），
     * 見 reports/ring_capture_security_audit_2026-09-13.md §2 R7。
     *
     * ⚠ 本檔是**未採用的備援實作**（正式路徑 B 走 root daemon + `getevent`）。
     *   若日後真的要採用，0600 會讓 untrusted_app 連不上——屆時需要的是一個
     *   **真正的存取控制設計**（專用 group／SELinux type／每次啟動的權杖），
     *   而不是把它改回 0777。
     */
    chmod(sock_path, 0600);
    if (listen(srv, 8) < 0) { perror("listen"); return 6; }
    printf("INFO listening on %s\n", sock_path);
    fflush(stdout);

    /* ---- 手勢狀態 ---- */
    long press_at = 0, last_tap_at = 0;
    int pressed = 0, tap_count = 0, long_fired = 0;

    struct input_event ev;
    for (;;) {
        struct pollfd fds[2];
        fds[0].fd = srv;    fds[0].events = POLLIN;
        fds[1].fd = dev;    fds[1].events = POLLIN;
        int pr = poll(fds, 2, 200);
        if (pr < 0) { if (errno == EINTR) continue; break; }

        if (fds[0].revents & POLLIN) {
            int c = accept(srv, NULL, NULL);
            if (c >= 0) {
                if (g_nclients < MAX_CLIENTS) {
                    g_clients[g_nclients++] = c;
                    char hello[256];
                    int n = snprintf(hello, sizeof(hello), "HELLO %s\n", g_devname);
                    write(c, hello, (size_t)n);
                } else {
                    close(c);
                }
            }
        }

        if (fds[1].revents & POLLIN) {
            while (read(dev, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                long t = now_ms();
                if (ev.type == EV_KEY) {
                    if (is_tap_like(ev.code)) {
                        if (ev.value == 1) {  /* down */
                            pressed = 1; press_at = t; long_fired = 0;
                            send_line("KEY %u down", ev.code);
                        } else if (ev.value == 0) {  /* up */
                            if (!pressed) continue;
                            pressed = 0;
                            long held = t - press_at;
                            send_line("KEY %u up", ev.code);
                            if (long_fired) continue;
                            if (held > tap_ms) continue;         /* 按太久：不視為點擊 */
                            if (tap_count > 0 && t - last_tap_at <= double_ms) {
                                tap_count++;
                            } else {
                                tap_count = 1;
                            }
                            last_tap_at = t;
                        }
                    } else if (ev.value == 1 || ev.value == 0) {
                        /* 其他按鍵只回報原始碼（供學習模式） */
                        send_line("KEY %u %s", ev.code, ev.value ? "down" : "up");
                    }
                } else if (ev.type == EV_REL && (ev.code == REL_WHEEL || ev.code == REL_HWHEEL)) {
                    if (ev.value > 0) send_line("SCROLL_UP");
                    else if (ev.value < 0) send_line("SCROLL_DOWN");
                }
            }
        }

        /* 週期性判定：長按 / 連續點擊結束 */
        long t = now_ms();
        if (pressed && !long_fired && t - press_at >= long_ms) {
            long_fired = 1;
            send_line("LONG_PRESS");
        }
        if (!pressed && tap_count > 0 && t - last_tap_at > double_ms) {
            int n = tap_count;
            tap_count = 0;
            if (n == 1) send_line("TAP");
            else if (n == 2) send_line("DOUBLE_TAP");
            else send_line("TRIPLE_TAP");
        }
    }
    close(dev);
    close(srv);
    unlink(sock_path);
    return 0;
}
