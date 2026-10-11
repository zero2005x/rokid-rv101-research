#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <android/log.h>

#define TAG "GhostLockNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/* ── 输出缓冲区 ────────────────────────────────────────────────── */

#define OUTBUF_SIZE 16384

static char g_outbuf[OUTBUF_SIZE];
static size_t g_outbuf_len;

static void outbuf_clear(void) {
    g_outbuf[0] = '\0';
    g_outbuf_len = 0;
}

static void outbuf_append(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

static void outbuf_append(const char *fmt, ...) {
    if (g_outbuf_len >= OUTBUF_SIZE - 256) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(g_outbuf + g_outbuf_len, OUTBUF_SIZE - g_outbuf_len, fmt, ap);
    va_end(ap);
    if (n > 0) g_outbuf_len += (size_t)n;
}

/* ── 辅助：获取 ELF 头信息 ──────────────────────────────────── */

static void print_elf_info(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) return;

    unsigned char e_ident[16];
    if (fread(e_ident, 1, 16, fp) != 16) { fclose(fp); return; }

    /* ELF magic check */
    if (e_ident[0] != 0x7f || e_ident[1] != 'E' ||
        e_ident[2] != 'L'  || e_ident[3] != 'F') {
        fclose(fp);
        outbuf_append("  → 文件不是 ELF 格式 (magic: %02X %02X %02X %02X)\n",
                      e_ident[0], e_ident[1], e_ident[2], e_ident[3]);
        return;
    }

    const char *class_str = (e_ident[4] == 2) ? "64-bit" :
                            (e_ident[4] == 1) ? "32-bit" : "unknown";
    const char *endian_str = (e_ident[5] == 1) ? "little-endian" :
                             (e_ident[5] == 2) ? "big-endian" : "unknown";

    /* read e_type at offset 16 (2 bytes, little-endian) */
    fseek(fp, 16, SEEK_SET);
    unsigned char type_buf[2];
    fread(type_buf, 1, 2, fp);
    unsigned int e_type = type_buf[0] | (type_buf[1] << 8);

    const char *type_str;
    switch (e_type) {
        case 1: type_str = "ET_REL (可重定位)"; break;
        case 2: type_str = "ET_EXEC (可执行)"; break;
        case 3: type_str = "ET_DYN (共享对象/PIE)"; break;
        default: type_str = "unknown"; break;
    }

    /* read e_machine at offset 18 */
    fseek(fp, 18, SEEK_SET);
    unsigned char mach_buf[2];
    fread(mach_buf, 1, 2, fp);
    unsigned int e_machine = mach_buf[0] | (mach_buf[1] << 8);

    const char *mach_str;
    switch (e_machine) {
        case 0x28: mach_str = "ARM"; break;
        case 0xB7: mach_str = "AArch64"; break;
        case 0x03: mach_str = "x86"; break;
        case 0x3E: mach_str = "x86-64"; break;
        default: mach_str = "unknown"; break;
    }

    outbuf_append("  → ELF: %s, %s, %s, 机器=%s (0x%X)\n",
                  class_str, endian_str, type_str, mach_str, e_machine);

    if (e_machine != 0xB7) {
        outbuf_append("  ⚠ 警告: 文件架构(%s) 与当前设备(AArch64) 不匹配! dlopen 会失败\n",
                      mach_str);
    }
    if (e_type == 2) {
        outbuf_append("  ⚠ 警告: ET_EXEC 类型不能 dlopen，需要 PIE (ET_DYN) 或 SO\n");
    }

    fclose(fp);
}

/* ── GhostLock 子命令路由 ─────────────────────────────────────── */

static void cmd_load(int argc, char *argv[]);
static void cmd_info(int argc, char *argv[]);

int ghostlock_main(int argc, char *argv[]) {
    outbuf_clear();
    outbuf_append("ghostlock-bridge v2  "
                   __DATE__ " " __TIME__ "\n");

    if (argc < 1) {
        cmd_info(0, NULL);
        return 0;
    }

    const char *cmd = argv[0];

    if (strcmp(cmd, "load") == 0) {
        cmd_load(argc - 1, argv + 1);
    } else if (strcmp(cmd, "info") == 0) {
        cmd_info(argc - 1, argv + 1);
    } else if (strcmp(cmd, "trigger") == 0 ||
               strcmp(cmd, "spray") == 0  ||
               strcmp(cmd, "write") == 0   ||
               strcmp(cmd, "pwn") == 0) {
        outbuf_append("ghostlock: '%s' — 尚未实现\n等待 GhostLock exploit 源文件接入。\n", cmd);
    } else {
        outbuf_append("用法: ghostlock load <path> [entry] | trigger | spray | write | pwn | info\n");
    }

    return 0;
}

/* ── load: 动态加载 SO/ELF（含详细诊断） ────────────────────── */

static void cmd_load(int argc, char *argv[]) {
    if (argc < 1) {
        outbuf_append("用法: ghostlock load <文件路径> [入口符号名]\n");
        return;
    }

    const char *path = argv[0];
    const char *entry_sym = (argc >= 2) ? argv[1] : "ghostlock_entry";

    outbuf_append("═══════════════════════════════════\n");
    outbuf_append("→ 文件: %s\n", path);
    outbuf_append("→ 入口符号: %s\n", entry_sym);

    /* ── stat 详细信息 ── */
    struct stat st;
    if (stat(path, &st) != 0) {
        outbuf_append("✗ stat 失败: %s (errno=%d)\n", strerror(errno), errno);
        outbuf_append("  可能原因:\n");
        outbuf_append("  - 文件不存在\n");
        outbuf_append("  - 路径错误（检查空格和特殊字符）\n");
        outbuf_append("  - 当前进程无权限访问此目录\n");
        return;
    }

    outbuf_append("→ 大小: %lld 字节 (%.1f KB)\n",
                  (long long)st.st_size, st.st_size / 1024.0);
    outbuf_append("→ 权限: %o (owner=%d, group=%d)\n",
                  st.st_mode & 0777, st.st_uid, st.st_gid);
    outbuf_append("→ UID/GID: 当前进程 uid=%d gid=%d\n",
                  getuid(), getgid());

    /* 检查是否目录 */
    if (S_ISDIR(st.st_mode)) {
        outbuf_append("✗ 错误: %s 是目录，不是文件\n", path);
        return;
    }

    /* ELF 头分析 */
    print_elf_info(path);

    /* ── fopen 详细检查 ── */
    FILE *test = fopen(path, "rb");
    if (test == NULL) {
        outbuf_append("✗ fopen 失败: %s (errno=%d)\n", strerror(errno), errno);
        outbuf_append("  检查:\n");
        outbuf_append("  - SELinux 上下文是否允许当前进程读此文件\n");
        outbuf_append("  - 父目录是否有 +x 权限\n");
        return;
    }
    fclose(test);
    outbuf_append("→ fopen 检查: ✓ 可读\n");

    /* ── dlopen ── */
    dlerror();
    void *handle = dlopen(path, RTLD_NOW);
    if (handle == NULL) {
        const char *err = dlerror();
        outbuf_append("✗ dlopen 失败:\n  %s\n", err ? err : "unknown");
        outbuf_append("  常见原因:\n");
        outbuf_append("  - 文件架构与设备不匹配（如 x86 SO 在 ARM 设备上）\n");
        outbuf_append("  - 缺少依赖的 .so（ldd 可查）\n");
        outbuf_append("  - 文件损坏或不是有效的 ELF/共享库\n");
        return;
    }
    outbuf_append("✓ dlopen 成功 (handle=%p)\n", handle);

    /* ── dlsym ── */
    dlerror();
    typedef int (*entry_fn_t)(int, char *[]);
    entry_fn_t entry = (entry_fn_t)dlsym(handle, entry_sym);
    const char *sym_err = dlerror();
    if (sym_err != NULL) {
        outbuf_append("✗ dlsym('%s'): %s\n", entry_sym, sym_err);
        outbuf_append("  提示: 文件加载成功但未找到指定符号\n");
        outbuf_append("  可用 nm -D <文件> 查看所有导出符号\n");
        dlclose(handle);
        return;
    }
    outbuf_append("✓ dlsym('%s') → %p\n", entry_sym, entry);

    /* ── 调用入口 ── */
    outbuf_append("→ 调用入口函数...\n");
    outbuf_append("═══════════════════════════════════\n");
    char *load_args[] = {"ghostlock-payload", NULL};
    int ret = entry(1, load_args);
    outbuf_append("\n═══════════════════════════════════\n");
    outbuf_append("→ 入口返回: %d\n", ret);

    dlclose(handle);
    outbuf_append("✓ 卸载完成\n");
}

/* ── info ─────────────────────────────────────────────────────── */

static void cmd_info(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    outbuf_append(
        "GhostLock Native Bridge\n"
        "═══════════════════════\n"
        "CVE:     CVE-2026-43499 (GhostLock)\n"
        "类型:    Linux 内核 rt_mutex futex PI 栈 UAF 提权\n"
        "影响:    2.6.39 ~ 7.1, CVSS 7.8, kernelCTF $92,337\n"
        "状态:    loader 就绪, exploit 阶段待接入\n"
        "═══════════════════════\n"
#ifdef __aarch64__
        "架构:    aarch64 (arm64-v8a)\n"
#else
        "架构:    unknown\n"
#endif
        "编译:    " __DATE__ " " __TIME__ "\n"
        "工具链:  " __VERSION__ "\n"
        "UID/GID: %d / %d\n"
        "\n"
        "命令:\n"
        "  load <path> [entry]  加载 SO/ELF 并调用入口\n"
        "  trigger | spray | write | pwn | info\n",
        getuid(), getgid()
    );
}

/* ── JNI 入口 ────────────────────────────────────────────────── */

JNIEXPORT jstring JNICALL
Java_com_ghostlock_skeleton_NativeBridge_execute(JNIEnv *env, jclass clazz, jobjectArray args) {
    if (args == NULL) {
        return (*env)->NewStringUTF(env, "✗ execute: args is null");
    }

    jint argc = (*env)->GetArrayLength(env, args);
    if (argc == 0) {
        return (*env)->NewStringUTF(env, "");
    }

    char **argv = (char **)calloc((size_t)(argc + 1), sizeof(char *));
    if (argv == NULL) {
        return (*env)->NewStringUTF(env, "✗ calloc 失败");
    }

    for (jint i = 0; i < argc; i++) {
        jstring js = (jstring)(*env)->GetObjectArrayElement(env, args, i);
        const char *str = (*env)->GetStringUTFChars(env, js, NULL);
        argv[i] = strdup(str);
        (*env)->ReleaseStringUTFChars(env, js, str);
        (*env)->DeleteLocalRef(env, js);
    }

    LOGI("execute: argc=%d", argc);
    for (jint i = 0; i < argc; i++) {
        LOGI("  argv[%d] = %s", i, argv[i]);
    }

    ghostlock_main((int)argc, argv);

    for (jint i = 0; i < argc; i++) free(argv[i]);
    free(argv);

    return (*env)->NewStringUTF(env, g_outbuf);
}
