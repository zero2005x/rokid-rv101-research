/* mini_sqlite3.c — a minimal CLI that runs SQL through dlopen("/system/lib64/libsqlite.so")
 *
 * Why: Android ships no sqlite3 CLI and busybox has none either, but automating the
 * LSPosed / Vector module scope (its config DB lives in a config directory under /data/adb)
 * requires writing SQLite. Instead of downloading the sqlite amalgamation and building it
 * statically, this borrows the system's existing libsqlite.so.
 *
 * Usage (on the device, root required):
 *   mini_sqlite3 <db-path> "<SQL>"            # run SQL; SELECT rows print as col|col
 *   mini_sqlite3 <db-path> "<SQL>" --readonly # open read-only
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

    /* SQLITE_OPEN_READONLY=1 / READWRITE=2 / CREATE=4 (read-only and read-write are mutually exclusive) */
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
