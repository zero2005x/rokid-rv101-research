#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define KGSL_PATH "/dev/kgsl-3d0"

enum {
    EXIT_ROOT_CONFIRMED = 0,
    AT_RUNTIME_ERROR = 1,
    NOT_ROOT = 10,
    EXIT_USER_ERROR = 64,
    EXIT_EXEC_ERROR = 126
};

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [--try-setuid] [--shell | --exec]\n"
            "\n"
            "  --try-setuid   Test setresgid/setresuid\n"
            "  --shell        Start /system/bin/sh if EUID=0\n"
            "  --exec         Execute program if EUID=0\n",
            program);
}

static void trim_newline(char *text)
{
    size_t length = strlen(text);
    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r')) {
        text[--length] = '\0';
    }
}

static void print_single_line_file(const char *key, const char *path)
{
    char buffer[512];
    FILE *stream = fopen(path, "r");
    if (stream == NULL) {
        printf("%s=<unavailable: %s>\n", key, strerror(errno));
        return;
    }
    if (fgets(buffer, sizeof(buffer), stream) == NULL) {
        printf("%s=<empty or unreadable>\n", key);
        fclose(stream);
        return;
    }
    fclose(stream);
    trim_newline(buffer);
    printf("%s=%s\n", key, buffer);
}

static bool wanted_status_line(const char *line)
{
    static const char *const prefixes[] = {
        "Uid:", "Gid:", "Groups:", "CapInh:", "CapPrm:",
        "CapEff:", "CapBnd", "CapAmb:", "NoNewPrivs:", "Seccomp:"
    };
    size_t i;
    for (i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        size_t length = strlen(prefixes[i]);
        if (strncmp(line, prefixes[i], length) == 0) {
            return true;
        }
    }
    return false;
}

static void print_process_status(void)
{
    char line[1024];
    FILE *stream = fopen("/proc/self/status", "r");
    puts("-- /proc/self/status security fields --");
    if (stream == NULL) {
        printf("STATUS_ERROR=%s\n", strerror(errno));
        return;
    }
    while (fgets(line, sizeof(line), stream) != NULL) {
        if (wanted_status_line(line)) {
            fputs(line, stdout);
        }
    }
    fclose(stream);
}

static void print_ids(const char *phase)
{
    uid_t real_uid = (uid_t)-1;
    uid_t effective_uid = (uid_t)-1;
    uid_t saved_uid = (uid_t)-1;
    gid_t real_gid = (gid_t)-1;
    gid_t effective_gid = (gid_t)-1;
    gid_t saved_gid = (gid_t)-1;

    if (getresuid(&real_uid, &effective_uid, &saved_uid) != 0) {
        printf("%s_UID_ERROR=%s\n", phase, strerror(errno));
    } else {
        printf("%s_UIDS=real:%lu effective:%lu saved:%lu\n",
               phase,
               (unsigned long)real_uid,
               (unsigned long)effective_uid,
               (unsigned long)saved_uid);
    }

    if (getresgid(&real_gid, &effective_gid, &saved_gid) != 0) {
        printf("%s_GID_ERROR=%s\n", phase, strerror(errno));
    } else {
        printf("%s_GIDS=real:%lu effective:%lu saved:%lu\n",
               phase,
               (unsigned long)real_gid,
               (unsigned long)effective_gid,
               (unsigned long)saved_gid);
    }
}

static bool check_one_open_mode(int access_mode, const char *name)
{
    int fd;
    struct stat descriptor_stat;

    errno = 0;
    fd = open(KGSL_PATH, access_mode | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        printf("KGSL_%s=DENIED errno:%d message:%s\n",
               name, errno, strerror(errno));
        return false;
    }

    if (fstat(fd, &descriptor_stat) != 0) {
        printf("KGSL_%s=OPENED fstat_error:%s\n", name, strerror(errno));
        close(fd);
        return true;
    }

    printf("KGSL_%s=ALLOWED descriptor_type: %s\n",
           name,
           S_ISCHR(descriptor_stat.st_mode) ? "character-device" : "unexpected");
    close(fd);
    return true;
}

static void check_kgsl(void)
{
    struct stat node_stat;

    puts("-- KGSL node --");
    errno = 0;
    if (lstat(KGSL_PATH, &node_stat) != 0) {
        printf("KGSL_NODE=MISSING_OR_INACCESSIBLE errno:%d message:%s\n",
               errno, strerror(errno));
        return;
    }

    printf("KGSL_NODE=%s type:%s mode:%04o owner_uid:%lu owner_gid:%lu\n",
           KGSL_PATH,
           S_ISCHR(node_stat.st_mode) ? "character-device" : "unexpected",
           (unsigned int)(node_stat.st_mode & 07777),
           (unsigned long)node_stat.st_uid,
           (unsigned long)node_stat.st_gid);

    (void)check_one_open_mode(O_RDONLY, "RDONLY");
    (void)check_one_open_mode(O_RDWR, "RDWR");
}

static void print_selinux_state(void)
{
    char buffer[32];
    FILE *stream;

    puts("-- SELinux --");
    print_single_line_file("SELINUX_CONTEXT", "/proc/self/attr/current");

    stream = fopen("/sys/fs/selinux/enforce", "r");
    if (stream == NULL) {
        printf("SELINUX_MODE=<unavailable:%s>\n", strerror(errno));
        return;
    }

    if (fgets(buffer, sizeof(buffer), stream) == NULL) {
        puts("SELINUX_MODE=<unreadable>");
    } else if (buffer[0] == '1') {
        puts("SELINUX_MODE=Enforcing");
    } else if (buffer[0] == '0') {
        puts("SELINUX_MODE=Permissive");
    } else {
        trim_newline(buffer);
        printf("SELINUX_MODE=<unexpected:%s>\n", buffer);
    }

    fclose(stream);
}

static bool root_identity_confirmed(void)
{
    return geteuid() == 0;
}

static bool try_set_root_identity(void)
{
    int gid_result;
    int uid_result;
    int gid_errno = 0;
    int uid_errno = 0;

    puts("-- Explicit setresgid/setresuid test --");

    errno = 0;
    gid_result = setresgid(0, 0, 0);
    if (gid_result != 0) {
        gid_errno = errno;
    }

    errno = 0;
    uid_result = setresuid(0, 0, 0);
    if (uid_result != 0) {
        uid_errno = errno;
    }

    if (gid_result == 0) {
        puts("SETRESGID_0=SUCCESS");
    } else {
        printf("SETRESGID_0=FAILED errno:%d message:%s\n",
               gid_errno, strerror(gid_errno));
    }

    if (uid_result == 0) {
        puts("SETRESUID_0=SUCCESS");
    } else {
        printf("SETRESUID_0=FAILED errno:%d message:%s\n",
               uid_errno, strerror(uid_errno));
    }

    return root_identity_confirmed();
}

int main(int argc, char **argv)
{
    bool try_setuid = false;
    bool request_shell = false;
    int exec_index = -1;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--try-setuid") == 0) {
            try_setuid = true;
        } else if (strcmp(argv[i], "--shell") == 0) {
            request_shell = true;
        } else if (strcmp(argv[0], "--exec") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return EXIT_USER_ERROR;
            }
            exec_index = i + 1;
            break;
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return EXIT_ROOT_CONFIRMED;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return EXIT_USER_ERROR;
        }
    }

    if (request_shell && exec_index >= 0) {
        fprintf(stderr, "Choose either --shell or --exec, not both.\n");
        return EXIT_USER_ERROR;
    }

    puts("========================================");
    puts("  ROKID RV101 KGSL / ROOT STATUS CHECK");
    puts("========================================");

    check_kgsl();
    puts("\n-- Initial identity --");
    print_ids("INITIAL");
    print_selinux_state();
    print_process_status();

    if (try_setuid && !root_identity_confirmed()) {
        (void)try_set_root_identity();
        puts("\n-- Identity after setres* test --");
        print_ids("FINAL");
        print_process_status();
    }

    puts("\n-- Final result --");
    if (!root_identity_confirmed()) {
        printf("ROOT_STATUS=NOT_ROOT effective_uid:%lu effective_gid:%lu\n",
               (unsigned long)geteuid(),
               (unsigned long)getegid());
        puts("DIAGNOSIS=UID 0 needs CAP_SETUID for standard path.");
        puts("REASONS=No CAP_SETUID, SELinux Enforcing, NoNewPrivs=0");
        puts("");
        puts("TO_GET_ROOT:");
        puts("  1. Push su binary (SUID) -> run su");
        puts("  2. Use KGSL driver to");
        puts("     modify kernel memory");
        puts("  3. Use mount -o remount");

        if (request_shell || exec_index >= 0) {
            fprintf(stderr,
                   "Refusing to start a shell/program because EUID is not 0.\n");
        }
        return NOT_ROOT;
    }

    printf("ROOT_STATUS=CONFIRMED effective_uid:%lu effective_gid:%lu\n",
           (unsigned long)geteuid(),
           (unsigned long)getegid());
    puts("NOTE=UID 0 does not bypass SELinux; "
         "check SELINUX_CONTEXT and CapEff.");

    if (exec_index >= 0) {
        printf("EXEC_PROGRAM=%s\n", argv[exec_index]);
        fflush(NULL);
        execvp(argv[exec_index], &argv[exec_index]);
        fprintf(stderr, "execvp(%s) failed: %s\n",
                argv[exec_index], strerror(errno));
        return EXIT_EXEC_ERROR;
    }

    if (request_shell) {
        puts("Starting /system/bin/sh with the already-confirmed EUID 0 identity.");
        fflush(NULL);
        execl("/system/bin/sh", "sh", (char *)NULL);
        fprintf(stderr, "execl(/system/bin/sh) failed: %s\n", strerror(errno));
        return EXIT_EXEC_ERROR;
    }

    return EXIT_ROOT_CONFIRMED;
}
