/*
 * i18n.h — multi-locale runtime (language detection / lookup / fonts / RTL)
 *
 * The string tables themselves are generated into i18n_strings.h by tools/gen_i18n.py
 * (14 locales).
 * This file provides:
 *   - locale detection (settings.ini -> system UI language -> English)
 *   - string lookup T(id) with a three-level fallback (current locale -> en -> zh-TW)
 *   - formatted lookup Tf(id, ...), using a thread-local ring buffer so calls can be chained
 *   - per-locale font selection (CJK / Thai / Arabic / Cyrillic each get a suitable face)
 *   - RTL (Arabic) detection
 *
 * For security research on owned / authorised devices only.
 */
#ifndef ZL_I18N_H
#define ZL_I18N_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "i18n_strings.h"

/* current locale index (LANG_*) */
static int g_lang = LANG_EN;

/* ---------- helpers ---------- */

static const wchar_t *i18n_code(int idx) {
    if (idx < 0 || idx >= LANG_COUNT) idx = LANG_EN;
    return g_lang_info[idx].code;
}

static int i18n_is_rtl(void) {
    return (g_lang >= 0 && g_lang < LANG_COUNT) ? g_lang_info[g_lang].rtl : 0;
}

/* find the index for a locale code; -1 when not found */
static int i18n_index_of_code(const wchar_t *code) {
    if (!code || !*code) return -1;
    for (int i = 0; i < LANG_COUNT; i++) {
        if (_wcsicmp(g_lang_info[i].code, code) == 0) return i;
    }
    return -1;
}

/*
 * System UI language -> locale index.
 * Match "primary language + sublanguage" first, then primary language alone (e.g. Chinese in
 * any region -> zh). Returns -1 when not found.
 */
static int i18n_detect_system(void) {
    LANGID lid = GetUserDefaultUILanguage();
    int primary = (int)(lid & 0x3FF);
    int sub = (int)((lid >> 10) & 0x3F);

    /* 1) exact primary + sublang match */
    for (int i = 0; i < LANG_COUNT; i++) {
        if (g_lang_info[i].win_primary == primary &&
            g_lang_info[i].win_sublang >= 0 &&
            g_lang_info[i].win_sublang == sub) {
            return i;
        }
    }
    /* 2) primary only (take the first; the default Chinese entry is zh-TW) */
    for (int i = 0; i < LANG_COUNT; i++) {
        if (g_lang_info[i].win_primary == primary) return i;
    }
    return -1;
}

/* ---------- lookup ---------- */

/* current locale -> en -> zh-TW -> empty string. Never returns NULL. */
static const wchar_t *T(int id) {
    if (id < 0 || id >= STR_COUNT) return L"";
    const wchar_t *s = g_i18n[g_lang][id];
    if (s && *s) return s;
    s = g_i18n[LANG_EN][id];
    if (s && *s) return s;
    s = g_i18n[LANG_ZH_TW][id];
    if (s && *s) return s;
    return L"";
}

/*
 * Formatted lookup. Returns a thread-local ring buffer (TF_SLOTS slots x TF_LEN wchar), so up to
 * TF_SLOTS calls can safely appear in one expression.
 * NOTE: each argument's Tf() "claims" its own slot, and swprintf is called only after every
 * argument has been evaluated, so there is no self-overwrite as long as the number of calls is
 * <= TF_SLOTS.
 * The device-info panel in do_refresh() uses 11 at once, so TF_SLOTS must be >= 11.
 * Format specifiers come from the string tables; gen_i18n.py has verified they match across all
 * locales.
 */
#define TF_SLOTS 16
#define TF_LEN   2048
static wchar_t *Tf(int id, ...) {
#if defined(__GNUC__)
    static __thread wchar_t slots[TF_SLOTS][TF_LEN];
    static __thread int next = 0;
#else
    static __declspec(thread) wchar_t slots[TF_SLOTS][TF_LEN];
    static __declspec(thread) int next = 0;
#endif
    wchar_t *buf = slots[next];
    next = (next + 1) % TF_SLOTS;

    va_list ap;
    va_start(ap, id);
    /* C99 vswprintf (supported by MinGW and MSVC 2015+); returns -1 on truncation */
    int n = vswprintf(buf, TF_LEN, T(id), ap);
    va_end(ap);
    if (n < 0) buf[TF_LEN - 1] = L'\0';   /* ensure termination even when truncated */
    return buf;
}

/* ---------- fonts ---------- */

static HFONT g_font = NULL;
static HFONT g_font_bold = NULL;
static wchar_t g_font_name[LF_FACESIZE] = L"";

/* try the candidate faces in order and return the first one present; NULL if none is */
static const wchar_t *i18n_pick_face(const wchar_t *const *cands, int n) {
    HDC dc = GetDC(NULL);
    if (!dc) return NULL;
    const wchar_t *found = NULL;
    for (int i = 0; i < n && !found; i++) {
        LOGFONTW lf;
        memset(&lf, 0, sizeof(lf));
        wcsncpy(lf.lfFaceName, cands[i], LF_FACESIZE - 1);
        lf.lfCharSet = DEFAULT_CHARSET;
        HFONT f = CreateFontIndirectW(&lf);
        if (f) {
            HGDIOBJ old = SelectObject(dc, f);
            wchar_t got[LF_FACESIZE] = L"";
            GetTextFaceW(dc, LF_FACESIZE, got);
            SelectObject(dc, old);
            DeleteObject(f);
            /* only a real selection of that face counts as a hit (GDI substitutes otherwise) */
            if (_wcsicmp(got, cands[i]) == 0) found = cands[i];
        }
    }
    ReleaseDC(NULL, dc);
    return found;
}

/*
 * Create the UI font for the current locale. Priority:
 *   1. the locale's recommended face (the font field in i18n_strings.h)
 *   2. the generic fallback list for each locale
 *   3. Segoe UI -> Tahoma -> system default
 */
static void i18n_make_font(HWND hwnd) {
    if (g_font) { DeleteObject(g_font); g_font = NULL; }
    if (g_font_bold) { DeleteObject(g_font_bold); g_font_bold = NULL; }

    int dpi = 96;
    HDC dc = GetDC(hwnd);
    if (dc) { dpi = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(hwnd, dc); }

    const wchar_t *face = NULL;
    const wchar_t *pref = (g_lang >= 0 && g_lang < LANG_COUNT) ? g_lang_info[g_lang].font : NULL;

    /* per-locale fallback font chains */
    static const wchar_t *cjk_tw[] = { L"Microsoft JhengHei UI", L"Microsoft JhengHei", L"MingLiU", L"Segoe UI", L"Tahoma" };
    static const wchar_t *cjk_cn[] = { L"Microsoft YaHei UI", L"Microsoft YaHei", L"SimSun", L"Segoe UI", L"Tahoma" };
    static const wchar_t *cjk_jp[] = { L"Yu Gothic UI", L"Meiryo UI", L"MS UI Gothic", L"Segoe UI", L"Tahoma" };
    static const wchar_t *cjk_kr[] = { L"Malgun Gothic", L"Gulim", L"Segoe UI", L"Tahoma" };
    static const wchar_t *thai[]   = { L"Leelawadee UI", L"Leelawadee", L"Tahoma", L"Segoe UI" };
    static const wchar_t *arab[]   = { L"Segoe UI", L"Tahoma", L"Arial" };
    static const wchar_t *generic[]= { L"Segoe UI", L"Tahoma", L"Arial" };

    const wchar_t *const *chain = generic;
    int chain_n = (int)(sizeof(generic) / sizeof(generic[0]));
    switch (g_lang) {
    case LANG_ZH_TW: chain = cjk_tw; chain_n = (int)(sizeof(cjk_tw)/sizeof(cjk_tw[0])); break;
    case LANG_ZH_CN: chain = cjk_cn; chain_n = (int)(sizeof(cjk_cn)/sizeof(cjk_cn[0])); break;
    case LANG_JA:    chain = cjk_jp; chain_n = (int)(sizeof(cjk_jp)/sizeof(cjk_jp[0])); break;
    case LANG_KO:    chain = cjk_kr; chain_n = (int)(sizeof(cjk_kr)/sizeof(cjk_kr[0])); break;
    case LANG_TH:    chain  = thai;  chain_n = (int)(sizeof(thai)/sizeof(thai[0]));     break;
    case LANG_AR:    chain  = arab;  chain_n = (int)(sizeof(arab)/sizeof(arab[0]));     break;
    default: break;
    }

    /* try the locale-specific face first, then the fallback chain */
    if (pref && *pref) {
        const wchar_t *one[1] = { pref };
        face = i18n_pick_face(one, 1);
    }
    if (!face) face = i18n_pick_face(chain, chain_n);

    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -MulDiv(9, dpi, 72);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    if (face) wcsncpy(lf.lfFaceName, face, LF_FACESIZE - 1);
    g_font = CreateFontIndirectW(&lf);

    lf.lfWeight = FW_BOLD;
    g_font_bold = CreateFontIndirectW(&lf);

    wcsncpy(g_font_name, face ? face : L"(system)", LF_FACESIZE - 1);
}

/* the current locale's font; bold=1 selects the bold face */
static HFONT i18n_font_ex(int bold) {
    return bold ? g_font_bold : g_font;
}

#endif /* ZL_I18N_H */
