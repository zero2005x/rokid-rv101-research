/* mini_sqlite3.c — 用 dlopen("/system/lib64/libsqlite.so") 跑 SQL 的極簡 CLI
 *
 * 為什麼需要：Android 不附 sqlite3 CLI，busybox 也沒有；但要自動化設定
 * LSPosed / Vector 的模組 scope（設定 DB 放在 /data/adb 下的 config 目錄）就必須能寫 SQLite。
 * 與其下載 sqlite amalgamation 靜態編譯，這裡直接借系統既有的 libsqlite.so。
 *
 * 用法（裝置上，需 root）：
 *   mini_sqlite3 <db路徑> "<SQL>"            # 執行 SQL，SELECT 以 col|col 逐列印出
 *   mini_sqlite3 <db路徑> "<SQL>" --readonly # 以唯讀開啟
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct sqlite3 sqlite3;

typedef int (*fn_open_v2)(const char *, sqlite3 **, int, const char *);
typedef int (*fn_close)(sqlite3 *);
typedef int (*fn_exec)(sqlite3 *, const char *, int (*)(void *, int, char **, char **), void *, char **);
typedef const char *(*fn_errmsg)(sqlite3 *);

static int print_row(void *unused, int argc, char **argv, char **col) {
    (void) unused;
    (void) col;
    for (int i = 0; i < argc; i++) {
        printf("%s%s", i ? "|" : "", argv[i] ? argv[i] : "NULL");
    }
    printf("\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <db> <sql> [--readonly]\n", argv[0]);
        return 2;
    }

    void *h = dlopen("libsqlite.so", RTLD_NOW);
    if (h == NULL) {
        h = dlopen("/system/lib64/libsqlite.so", RTLD_NOW);
    }
    if (h == NULL) {
        fprintf(stderr, "dlopen libsqlite.so failed: %s\n", dlerror());
        return 3;
    }

    fn_open_v2 p_open = (fn_open_v2) dlsym(h, "sqlite3_open_v2");
    fn_close p_close = (fn_close) dlsym(h, "sqlite3_close");
    fn_exec p_exec = (fn_exec) dlsym(h, "sqlite3_exec");
    fn_errmsg p_errmsg = (fn_errmsg) dlsym(h, "sqlite3_errmsg");
    if (p_open == NULL || p_close == NULL || p_exec == NULL) {
        fprintf(stderr, "dlsym failed (libsqlite.so 未匯出公開 API)\n");
        return 4;
    }

    /* SQLITE_OPEN_READONLY=1 / READWRITE=2 / CREATE=4（唯讀與讀寫互斥，不可同時給） */
    int readonly = (argc > 3 && strcmp(argv[3], "--readonly") == 0);
    int flags = readonly ? 0x00000001 : (0x00000002 | 0x00000004);

    sqlite3 *db = NULL;
    if (p_open(argv[1], &db, flags, NULL) != 0) {
        fprintf(stderr, "open %s failed: %s\n", argv[1], db ? p_errmsg(db) : "?");
        return 5;
    }

    char *err = NULL;
    int rc = p_exec(db, argv[2], print_row, NULL, &err);
    if (rc != 0) {
        fprintf(stderr, "sql error: %s\n", err ? err : "?");
        p_close(db);
        return 6;
    }
    p_close(db);
    dlclose(h);
    return 0;
}
