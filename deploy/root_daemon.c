/*
 * root_daemon.c — 常駐 root daemon（單次提權後啟動）
 *
 * 協定：UNIX domain socket (SOCK_STREAM)，純文字一行一指令。
 *   client 連上 → 寫入指令（可含多行/結尾換行）→ shutdownOutput
 *   → daemon 讀到 EOF → 以 /system/bin/sh -c 執行 → 把 stdout 寫回 → 關閉連線。
 *
 * 啟動後會先 echo 0 > /sys/fs/selinux/enforce 維持 Permissive。
 * socket 檔設 0777，讓 untrusted_app 也能連線。
 *
 * 設計重點（避免單一指令卡死整個 daemon）：
 *   - 每條連線 fork 一個 handler 子行程，父行程立刻回到 accept()，
 *     即使某個指令 hang 住也不影響後續連線。
 *   - stdout/stderr 導向暫存檔（而非 pipe），避免「指令背景化子行程繼承
 *     pipe 寫端、導致讀端永不 EOF、daemon 卡在 fgets/pclose」的經典問題。
 *   - 背景化（`... &`）的 payload 只會延遲 handler 子行程的 waitpid，
 *     但 `sh -c` 本身在 background 後即返回，waitpid 立即回收。
 *
 * 建置（Android NDK, aarch64）：
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

/* 讀 client 直到 EOF，去掉結尾換行。回傳實際長度（0 表示空/錯誤）。 */
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

/* 單一連線的處理（在 fork 出來的 handler 子行程中執行）。 */
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
        /* 子行程：執行指令，stdout/stderr → 暫存檔，stdin ← /dev/null */
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
        /* 關鍵：關閉所有繼承的 fd（尤其 client socket `c`），
           否則指令 background 的子行程會持有連線，client 永遠收不到 EOF。 */
        for (int fd = 3; fd < 128; fd++) close(fd);
        execl("/system/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    /* 父（handler）：等 `sh -c` 結束。background 子行程不會拖住這裡，
     * 因為 `sh -c` 在 `cmd &` 後就返回。 */
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
    /* 避免寫已斷線 client 時被 SIGPIPE 終止（fork 後繼承）。 */
    signal(SIGPIPE, SIG_IGN);

    /* 防禦性關閉可能繼承的 fd（例如被 `setsid ... &` 從 shell 啟動時帶入
       的連線 fd），避免新 daemon 持有舊連線導致 client 收不到 EOF。 */
    for (int fd = 3; fd < 128; fd++) close(fd);

    /* 維持 SELinux Permissive */
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
            /* handler 子行程 */
            close(srv);
            handle(c);
            _exit(0);
        }
        close(c); /* 父行程不需要 */

        /* 非阻塞回收已結束的 handler 子行程，避免累積 zombie。 */
        while (waitpid(-1, NULL, WNOHANG) > 0) { }
    }
    return 0;
}
