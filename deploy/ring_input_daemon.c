/*
 * ⚠️ Fallback implementation (not adopted): this version's path B uses the device's built-in
 *    `getevent` instead (run in the background by the root daemon, with the App polling for
 *    increments), so no native binary needs to ship with the APK. This file is kept as a
 *    ready-made implementation in case lower latency is needed later (event-driven, able to
 *    read ABS/gestures instead of polling); it was not built here because the local WSL has no
 *    aarch64 linker and the NDK is the Windows build.
 */
/*
 * ring_input_daemon.c — gesture daemon that reads /dev/input/eventX directly as root (path B).
 *
 * Why it exists:
 *   The glasses-side App is untrusted_app and cannot read /dev/input/*, but after a reboot
 *   root_daemon (root) can start this. The program:
 *     1. picks an input device automatically (--name substring match, e.g. "RGR" for the ring
 *        or "TP" for the glasses touchpad)
 *     2. reads evdev events -> recognises gestures (tap / double tap / triple tap / long press
 *        / wheel)
 *     3. writes the result as line-oriented text to a UNIX socket for the App (multiple
 *        clients allowed)
 *
 * Usage:
 *   ring_input_daemon --list                     list every input device, then exit
 *   ring_input_daemon [--device /dev/input/eventN] [--name substring]
 *                      [--socket /data/local/tmp/ring_input.sock]
 *                      [--tap-ms 400] [--double-ms 450] [--long-ms 700]
 *
 * Output (one entry per line, ASCII):
 *   HELLO <device-name>           sent when a client connects
 *   KEY <code> <down|up>          raw key (for "gesture learning")
 *   TAP | DOUBLE_TAP | TRIPLE_TAP | LONG_PRESS
 *   SCROLL_UP | SCROLL_DOWN
 *
 * Build (aarch64):
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

/* ---- gesture parameters (overridable from the command line) ---- */
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
    /* also print to stdout (redirected to a log file by root_daemon) */
    fwrite(buf, 1, (size_t)n, stdout);
    fflush(stdout);
}

/* Keys treated as a "tap": touchpad BTN_TOUCH, mouse buttons, confirm keys, custom keys */
static int is_tap_like(unsigned short code) {
    switch (code) {
        case BTN_TOUCH:      /* 330 touchpad */
        case BTN_LEFT:       /* 272 mouse left button (same value as BTN_MOUSE) */
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

    /* ---- pick a device ---- */
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
            /* case-insensitive substring match */
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
     * Permissions: 0600 (was 0777).
     *
     * 0777 lets **any uid** (including other apps and adb shell) connect to this socket and
     * read the user's gesture stream (equivalent to learning out of band when the user pressed
     * the ring or touchpad); see reports/ring_capture_security_audit_2026-09-13.md §2 R7.
     *
     * ⚠ This file is the **fallback implementation, not adopted** (the shipping path B uses the
     *   root daemon + `getevent`). If it is ever adopted, 0600 will keep untrusted_app out —
     *   what is needed then is a **real access-control design** (a dedicated group / SELinux
     *   type / per-start token), not a revert to 0777.
     */
    chmod(sock_path, 0600);
    if (listen(srv, 8) < 0) { perror("listen"); return 6; }
    printf("INFO listening on %s\n", sock_path);
    fflush(stdout);

    /* ---- gesture state ---- */
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
                            if (held > tap_ms) continue;         /* held too long: not a tap */
                            if (tap_count > 0 && t - last_tap_at <= double_ms) {
                                tap_count++;
                            } else {
                                tap_count = 1;
                            }
                            last_tap_at = t;
                        }
                    } else if (ev.value == 1 || ev.value == 0) {
                        /* other keys only report the raw code (for learning mode) */
                        send_line("KEY %u %s", ev.code, ev.value ? "down" : "up");
                    }
                } else if (ev.type == EV_REL && (ev.code == REL_WHEEL || ev.code == REL_HWHEEL)) {
                    if (ev.value > 0) send_line("SCROLL_UP");
                    else if (ev.value < 0) send_line("SCROLL_DOWN");
                }
            }
        }

        /* periodic evaluation: long press / end of a tap run */
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
