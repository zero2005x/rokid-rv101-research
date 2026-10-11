/* debugmask.c — USB 偵錯防偵測的 native 層（PLT/GOT hook）
 *
 * 為什麼需要：Java 層 hook 只能攔 android.os.SystemProperties；
 * 偵測器若用 native __system_property_get() / __system_property_read_callback()
 * 直讀屬性區，Java hook 完全看不到。這一層把目標行程內「已載入 ELF 模組」的
 * GOT 專案改寫成我們的替身函式，讓 libc 的屬性 API 回傳假值。
 *
 * 關鍵設計：
 *  - 只改目標 App「自己行程」的 GOT：屬性區本體不動，init/usbd/adbd 讀到的仍是真值。
 *  - 跳過本程式庫自身的模組，避免自我遞迴。
 *  - 之後才載入的 .so：以屬性讀取路徑上的「節流重掃」補掛（不 hook dlopen，見下方說明）。
 *  - 遮蔽表由 Java 端傳入（key=value 換行分隔），可熱更新、免重啟。
 */
#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define LOG_TAG "UsbDebugMask/Native"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define MAX_ENTRIES 96
#define KEY_MAX 128
#define VAL_MAX 128

/* ── 遮蔽表（每次更新換一個新物件，讀者用原子指標取用） ──────────────────── */
struct table {
    int count;
    char keys[MAX_ENTRIES][KEY_MAX];
    char vals[MAX_ENTRIES][VAL_MAX];
};

static struct table *g_table; /* atomic */

static const char *mask_lookup(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    struct table *t = __atomic_load_n(&g_table, __ATOMIC_ACQUIRE);
    if (t == NULL) {
        return NULL;
    }
    for (int i = 0; i < t->count; i++) {
        if (strcmp(t->keys[i], name) == 0) {
            return t->vals[i];
        }
    }
    return NULL;
}

static void set_table(const char *blob) {
    struct table *t = calloc(1, sizeof(struct table));
    if (t == NULL) {
        return;
    }
    const char *p = blob;
    while (p != NULL && *p != '\0' && t->count < MAX_ENTRIES) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t) (nl - p) : strlen(p);
        if (len > 0 && len < KEY_MAX + VAL_MAX) {
            char line[KEY_MAX + VAL_MAX];
            memcpy(line, p, len);
            line[len] = '\0';
            char *eq = strchr(line, '=');
            if (eq != NULL) {
                *eq = '\0';
                snprintf(t->keys[t->count], KEY_MAX, "%s", line);
                snprintf(t->vals[t->count], VAL_MAX, "%s", eq + 1);
                t->count++;
            }
        }
        p = nl ? nl + 1 : NULL;
    }
    struct table *old = __atomic_exchange_n(&g_table, t, __ATOMIC_ACQ_REL);
    /* 舊表刻意不 free：可能還有其他執行緒正在讀，洩漏量以「設定變更次數」為上限 */
    (void) old;
    LOGI("遮蔽表更新：%d 個屬性", t->count);
}

/* ── 真函式指標 ────────────────────────────────────────────────────────── */
typedef int (*prop_get_t)(const char *, char *);
typedef int (*prop_read_t)(const void *, char *, char *);
typedef void (*prop_read_cb_fn)(void *, const char *, const char *, unsigned int);
typedef void (*prop_read_callback_t)(const void *, prop_read_cb_fn, void *);

/* bionic 匯出的 loader API：帶明確 caller_addr，能保留「呼叫者所屬 namespace」。
 * 這是 libnativeloader 用的同一組介面；用一般 dlopen 攔截會讓 App 的載入改用
 * 我們自己的 isolated namespace（實測造成 vendor lib 載入失敗）。 */
typedef void *(*loader_dlopen_t)(const char *filename, int flags, const void *caller_addr);
typedef void *(*loader_dlopen_ext_t)(const char *filename, int flags, const void *extinfo,
                                     const void *caller_addr);

static loader_dlopen_t loader_dlopen;
static loader_dlopen_ext_t loader_dlopen_ext;

static prop_get_t real_get;
static prop_read_t real_read;
static prop_read_callback_t real_read_callback;

static void patch_all(void);
static void maybe_repatch(void);
static uintptr_t fix_ptr(struct dl_phdr_info *info, ElfW(Addr) v);

/* ── 替身函式 ──────────────────────────────────────────────────────────── */
static int my_get(const char *name, char *value) {
    maybe_repatch();
    const char *fake = mask_lookup(name);
    if (fake != NULL) {
        if (value != NULL) {
            size_t n = strlen(fake);
            memcpy(value, fake, n + 1);
            return (int) n;
        }
        return (int) strlen(fake);
    }
    return real_get ? real_get(name, value) : 0;
}

static int my_read(const void *pi, char *name, char *value) {
    maybe_repatch();
    int r = real_read ? real_read(pi, name, value) : 0;
    if (r > 0 && name != NULL && value != NULL) {
        const char *fake = mask_lookup(name);
        if (fake != NULL) {
            size_t n = strlen(fake);
            memcpy(value, fake, n + 1);
            return (int) n;
        }
    }
    return r;
}

struct cb_wrap {
    prop_read_cb_fn cb;
    void *cookie;
};

static void cb_trampoline(void *cookie, const char *name, const char *value, unsigned int serial) {
    struct cb_wrap *w = (struct cb_wrap *) cookie;
    const char *fake = mask_lookup(name);
    w->cb(w->cookie, name, fake != NULL ? fake : value, serial);
}

static void my_read_callback(const void *pi, prop_read_cb_fn cb, void *cookie) {
    maybe_repatch();
    if (real_read_callback == NULL) {
        return;
    }
    struct cb_wrap w;
    w.cb = cb;
    w.cookie = cookie;
    real_read_callback(pi, cb_trampoline, &w);
}

/* ── 之後才載入的 .so 也要補掛 ──────────────────────────────────────────
 *
 * 為什麼不 hook dlopen：android_dlopen_ext/dlopen 會依「呼叫者所屬的 linker
 * namespace」解析路徑。我們的替身函式位於模組的 isolated namespace，一旦攔下
 * App 的 dlopen，App 之後載入 vendor／私有 .so 就會失敗。實測（台灣行動支付）：
 *   E linker: library "/vendor/lib64/egl/libMEOW_qt.so" ... needed or dlopened by
 *             ".../cache/libdebugmask.so" is not accessible for the namespace
 * 因此改成在屬性讀取路徑上「節流重掃」（每秒最多一次 dl_iterate_phdr），
 * 兼顧覆蓋率與不干擾 App 的動態載入行為。
 */
static long long g_last_patch_ms;

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long) ts.tv_sec * 1000 + (long long) (ts.tv_nsec / 1000000);
}

static void maybe_repatch(void) {
    long long now = now_ms();
    if (now - g_last_patch_ms < 1000) {
        return;
    }
    g_last_patch_ms = now;
    patch_all();
}

/* ── dlopen 攔截：載入完成後立刻補掃新模組 ────────────────────────────── */
static void *my_dlopen(const char *filename, int flags) {
    const void *caller = __builtin_return_address(0);
    if (loader_dlopen == NULL) {
        return NULL;
    }
    void *h = loader_dlopen(filename, flags, caller);
    if (h != NULL) {
        patch_all();
    }
    return h;
}

static void *my_dlopen_ext(const char *filename, int flags, const void *extinfo) {
    const void *caller = __builtin_return_address(0);
    if (loader_dlopen_ext == NULL) {
        return NULL;
    }
    void *h = loader_dlopen_ext(filename, flags, extinfo, caller);
    if (h != NULL) {
        patch_all();
    }
    return h;
}

/* ── 要改寫的符號表 ────────────────────────────────────────────────────── */
struct target {
    const char *name;
    void *replacement;
    void **original;
    int enabled; /* 0 = 本次不安裝（例如取不到 __loader_* 時就不攔 dlopen） */
};

static struct target g_targets[] = {
        {"__system_property_get", (void *) my_get, (void **) &real_get, 1},
        {"__system_property_read", (void *) my_read, (void **) &real_read, 1},
        {"__system_property_read_callback", (void *) my_read_callback, (void **) &real_read_callback, 1},
        /* dlopen 這兩個預設關閉，只有在取到 __loader_* 時才開（見 resolve_loader_api） */
        {"dlopen", (void *) my_dlopen, NULL, 0},
        {"android_dlopen_ext", (void *) my_dlopen_ext, NULL, 0},
};
static const int g_target_count = (int) (sizeof(g_targets) / sizeof(g_targets[0]));

static pthread_mutex_t g_patch_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_patched_slots;

/* 已掃過的模組基底位址：重掃時只處理「新出現」的模組，成本從 O(全部) 降到 O(新的)。 */
#define MAX_MODULES 256
static uintptr_t g_seen_bases[MAX_MODULES];
static int g_seen_count;

static int base_seen(uintptr_t base) {
    for (int i = 0; i < g_seen_count; i++) {
        if (g_seen_bases[i] == base) {
            return 1;
        }
    }
    return 0;
}

static void base_mark(uintptr_t base) {
    if (g_seen_count < MAX_MODULES) {
        g_seen_bases[g_seen_count++] = base;
    }
}

/* ── 由已載入模組的 GOT 反查符號位址 ──────────────────────────────────────
 * dlsym(RTLD_DEFAULT, "__loader_dlopen") 在 Android 13 取不到（0x0），
 * 但 libnativeloader 會匯入 __loader_*，其 GOT 內就是已解析好的真位址。
 */
static const char *g_lookup_name;
static void *g_lookup_result;

static int lookup_cb(struct dl_phdr_info *info, size_t size, void *data) {
    (void) size;
    (void) data;
    ElfW(Dyn) *dyn = NULL;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
            dyn = (ElfW(Dyn) *) (info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
            break;
        }
    }
    if (dyn == NULL) {
        return 0;
    }
    ElfW(Sym) *symtab = NULL;
    const char *strtab = NULL;
    ElfW(Rela) *jmprel = NULL;
    size_t pltrelsz = 0;
    ElfW(Rela) *rela = NULL;
    size_t relasz = 0;
    for (ElfW(Dyn) *d = dyn; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
            case DT_SYMTAB: symtab = (ElfW(Sym) *) fix_ptr(info, d->d_un.d_ptr); break;
            case DT_STRTAB: strtab = (const char *) fix_ptr(info, d->d_un.d_ptr); break;
            case DT_JMPREL: jmprel = (ElfW(Rela) *) fix_ptr(info, d->d_un.d_ptr); break;
            case DT_PLTRELSZ: pltrelsz = (size_t) d->d_un.d_val; break;
            case DT_RELA: rela = (ElfW(Rela) *) fix_ptr(info, d->d_un.d_ptr); break;
            case DT_RELASZ: relasz = (size_t) d->d_un.d_val; break;
            default: break;
        }
    }
    if (symtab == NULL || strtab == NULL) {
        return 0;
    }
    ElfW(Rela) *tables[2] = {jmprel, rela};
    size_t counts[2] = {pltrelsz / sizeof(ElfW(Rela)), relasz / sizeof(ElfW(Rela))};
    for (int t = 0; t < 2; t++) {
        for (size_t i = 0; i < counts[t]; i++) {
            ElfW(Rela) *r = &tables[t][i];
            unsigned int sym_index = (unsigned int) ELF64_R_SYM(r->r_info);
            const char *nm = strtab + symtab[sym_index].st_name;
            if (nm != NULL && strcmp(nm, g_lookup_name) == 0) {
                g_lookup_result = *(void **) fix_ptr(info, r->r_offset);
                return 1; /* 找到就停 */
            }
        }
    }
    return 0;
}

static void *lookup_symbol_via_got(const char *name) {
    g_lookup_name = name;
    g_lookup_result = NULL;
    dl_iterate_phdr(lookup_cb, NULL);
    return g_lookup_result;
}

/* ── GOT 改寫 ──────────────────────────────────────────────────────────── */
static uintptr_t fix_ptr(struct dl_phdr_info *info, ElfW(Addr) v) {
    uintptr_t p = (uintptr_t) v;
    if (p < (uintptr_t) info->dlpi_addr) {
        p += (uintptr_t) info->dlpi_addr; /* 有些 linker 未把動態段指標轉成絕對位址 */
    }
    return p;
}

static int prot_of_addr(struct dl_phdr_info *info, uintptr_t addr) {
    uintptr_t base = (uintptr_t) info->dlpi_addr;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_LOAD) {
            continue;
        }
        uintptr_t start = base + ph->p_vaddr;
        uintptr_t end = start + ph->p_memsz;
        if (addr >= start && addr < end) {
            int prot = 0;
            if (ph->p_flags & PF_R) prot |= PROT_READ;
            if (ph->p_flags & PF_W) prot |= PROT_WRITE;
            if (ph->p_flags & PF_X) prot |= PROT_EXEC;
            return prot;
        }
    }
    return PROT_READ;
}

static void patch_slot(struct dl_phdr_info *info, uintptr_t slot, void *replacement) {
    long page = sysconf(_SC_PAGESIZE);
    uintptr_t page_start = slot & ~((uintptr_t) page - 1);
    size_t page_len = (size_t) ((slot + sizeof(void *) - page_start + page - 1) & ~((uintptr_t) page - 1));
    int prot = prot_of_addr(info, slot);

    if (mprotect((void *) page_start, page_len, (prot & ~PROT_EXEC) | PROT_WRITE) != 0) {
        LOGE("mprotect 失敗 addr=%p", (void *) slot);
        return;
    }
    *(void **) slot = replacement;
    __builtin___clear_cache((char *) slot, (char *) slot + sizeof(void *));
    mprotect((void *) page_start, page_len, prot);
    g_patched_slots++;
}

static void patch_rela_table(struct dl_phdr_info *info, ElfW(Rela) *rela, size_t count,
                             ElfW(Sym) * symtab, const char *strtab) {
    if (rela == NULL || symtab == NULL || strtab == NULL) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        ElfW(Rela) *r = &rela[i];
#if defined(__aarch64__)
        unsigned int type = (unsigned int) ELF64_R_TYPE(r->r_info);
        if (type != R_AARCH64_JUMP_SLOT && type != R_AARCH64_GLOB_DAT) {
            continue;
        }
#elif defined(__arm__)
        unsigned int type = (unsigned int) ELF32_R_TYPE(r->r_info);
        if (type != R_ARM_JUMP_SLOT && type != R_ARM_GLOB_DAT) {
            continue;
        }
#endif
        unsigned int sym_index = (unsigned int) ELF64_R_SYM(r->r_info);
        const char *sym_name = strtab + symtab[sym_index].st_name;
        if (sym_name == NULL || sym_name[0] == '\0') {
            continue;
        }
        for (int t = 0; t < g_target_count; t++) {
            if (strcmp(sym_name, g_targets[t].name) != 0) {
                continue;
            }
            if (!g_targets[t].enabled) {
                break;
            }
            uintptr_t slot = fix_ptr(info, r->r_offset);
            if (g_targets[t].original != NULL && *g_targets[t].original == NULL) {
                *g_targets[t].original = *(void **) slot; /* 記下真函式 */
            }
            if (*(void **) slot == g_targets[t].replacement) {
                continue; /* 已經改過 */
            }
            patch_slot(info, slot, g_targets[t].replacement);
            break;
        }
    }
}

static int patch_cb(struct dl_phdr_info *info, size_t size, void *data) {
    (void) size;
    (void) data;

    const char *name = info->dlpi_name ? info->dlpi_name : "";
    if (strstr(name, "libdebugmask.so") != NULL || strstr(name, "linker") != NULL) {
        return 0; /* 自己與 linker 不碰 */
    }
    if (base_seen((uintptr_t) info->dlpi_addr)) {
        return 0; /* 這一顆掃過了 */
    }
    base_mark((uintptr_t) info->dlpi_addr);

    ElfW(Dyn) *dyn = NULL;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
            dyn = (ElfW(Dyn) *) (info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
            break;
        }
    }
    if (dyn == NULL) {
        return 0;
    }

    ElfW(Sym) *symtab = NULL;
    const char *strtab = NULL;
    ElfW(Rela) *jmprel = NULL;
    size_t pltrelsz = 0;
    ElfW(Rela) *rela = NULL;
    size_t relasz = 0;

    for (ElfW(Dyn) *d = dyn; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
            case DT_SYMTAB:
                symtab = (ElfW(Sym) *) fix_ptr(info, d->d_un.d_ptr);
                break;
            case DT_STRTAB:
                strtab = (const char *) fix_ptr(info, d->d_un.d_ptr);
                break;
            case DT_JMPREL:
                jmprel = (ElfW(Rela) *) fix_ptr(info, d->d_un.d_ptr);
                break;
            case DT_PLTRELSZ:
                pltrelsz = (size_t) d->d_un.d_val;
                break;
            case DT_RELA:
                rela = (ElfW(Rela) *) fix_ptr(info, d->d_un.d_ptr);
                break;
            case DT_RELASZ:
                relasz = (size_t) d->d_un.d_val;
                break;
            default:
                break;
        }
    }

    if (symtab == NULL || strtab == NULL) {
        return 0;
    }
    size_t rela_ent = sizeof(ElfW(Rela));
    patch_rela_table(info, jmprel, pltrelsz / rela_ent, symtab, strtab);
    patch_rela_table(info, rela, relasz / rela_ent, symtab, strtab);
    return 0;
}

/* 背景重掃：之後才載入的 .so（例如偵測器自己的 native 檢查庫）會在 1 秒內被補掛。
 * 不去 hook dlopen —— 那會讓 App 的 dlopen 改用我們的 isolated namespace 解析。 */
static void *repatch_loop(void *arg) {
    (void) arg;
    for (;;) {
        struct timespec ts;
        ts.tv_sec = 1;
        ts.tv_nsec = 0;
        nanosleep(&ts, NULL);
        patch_all();
    }
    return NULL;
}

static void patch_all(void) {
    pthread_mutex_lock(&g_patch_lock);
    dl_iterate_phdr(patch_cb, NULL);
    pthread_mutex_unlock(&g_patch_lock);
}

/* ── JNI 介面 ──────────────────────────────────────────────────────────── */
JNIEXPORT jboolean JNICALL
Java_com_rokid_rc_usbdebughider_NativeMask_nativeInstall(JNIEnv *env, jclass clazz, jstring blob) {
    (void) clazz;
    if (blob == NULL) {
        return JNI_FALSE;
    }
    const char *utf = (*env)->GetStringUTFChars(env, blob, NULL);
    if (utf == NULL) {
        return JNI_FALSE;
    }
    set_table(utf);
    (*env)->ReleaseStringUTFChars(env, blob, utf);

    /* 先解析真函式，再改 GOT（順序不能顛倒） */
    if (real_get == NULL) {
        real_get = (prop_get_t) dlsym(RTLD_DEFAULT, "__system_property_get");
        real_read = (prop_read_t) dlsym(RTLD_DEFAULT, "__system_property_read");
        real_read_callback = (prop_read_callback_t) dlsym(RTLD_DEFAULT, "__system_property_read_callback");
        loader_dlopen = (loader_dlopen_t) dlsym(RTLD_DEFAULT, "__loader_dlopen");
        if (loader_dlopen == NULL) {
            loader_dlopen = (loader_dlopen_t) lookup_symbol_via_got("__loader_dlopen");
        }
        loader_dlopen_ext = (loader_dlopen_ext_t) dlsym(RTLD_DEFAULT, "__loader_android_dlopen_ext");
        if (loader_dlopen_ext == NULL) {
            loader_dlopen_ext = (loader_dlopen_ext_t) lookup_symbol_via_got("__loader_android_dlopen_ext");
        }
        /* 兩個都取到才攔 dlopen；取不到就完全不裝（絕不讓 dlopen 回 NULL） */
        g_targets[3].enabled = (loader_dlopen != NULL);
        g_targets[4].enabled = (loader_dlopen_ext != NULL);
        LOGI("真函式: get=%p read=%p read_cb=%p loader_dlopen=%p loader_dlopen_ext=%p",
             (void *) real_get, (void *) real_read, (void *) real_read_callback,
             (void *) loader_dlopen, (void *) loader_dlopen_ext);
    }

    g_patched_slots = 0;
    patch_all();
    LOGI("PLT/GOT 改寫完成：%d 個 slot", g_patched_slots);

    static int thread_started = 0;
    if (!thread_started) {
        thread_started = 1;
        pthread_t tid;
        if (pthread_create(&tid, NULL, repatch_loop, NULL) == 0) {
            pthread_detach(tid);
            LOGI("背景重掃執行緒已啟動（每秒一次）");
        }
    }
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_rokid_rc_usbdebughider_NativeMask_nativeSetTable(JNIEnv *env, jclass clazz, jstring blob) {
    (void) clazz;
    if (blob == NULL) {
        return JNI_FALSE;
    }
    const char *utf = (*env)->GetStringUTFChars(env, blob, NULL);
    if (utf == NULL) {
        return JNI_FALSE;
    }
    set_table(utf);
    (*env)->ReleaseStringUTFChars(env, blob, utf);
    return JNI_TRUE;
}

JNIEXPORT jint JNICALL
Java_com_rokid_rc_usbdebughider_NativeMask_nativeRepatch(JNIEnv *env, jclass clazz) {
    (void) env;
    (void) clazz;
    patch_all();
    return (jint) g_patched_slots;
}

JNIEXPORT jint JNICALL
Java_com_rokid_rc_usbdebughider_NativeMask_nativePatchedSlots(JNIEnv *env, jclass clazz) {
    (void) env;
    (void) clazz;
    return (jint) g_patched_slots;
}

/* 讓 Java 端能驗證 native 層是否真的生效（讀的是被 hook 過的 __system_property_get） */
JNIEXPORT jstring JNICALL
Java_com_rokid_rc_usbdebughider_NativeMask_nativeGetProp(JNIEnv *env, jclass clazz, jstring key) {
    (void) clazz;
    const char *k = (*env)->GetStringUTFChars(env, key, NULL);
    if (k == NULL) {
        return (*env)->NewStringUTF(env, "(oom)");
    }
    char value[VAL_MAX];
    memset(value, 0, sizeof(value));
    int n = my_get(k, value);
    (*env)->ReleaseStringUTFChars(env, key, k);
    if (n <= 0) {
        return (*env)->NewStringUTF(env, "(empty)");
    }
    return (*env)->NewStringUTF(env, value);
}
