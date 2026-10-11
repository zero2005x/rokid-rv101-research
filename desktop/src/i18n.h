/*
 * i18n.h — 多國語系執行期（語言偵測 / 查表 / 字型 / RTL）
 *
 * 字串表本身由 tools/gen_i18n.py 產生為 i18n_strings.h（14 個語系）。
 * 本檔提供：
 *   - 語系偵測（settings.ini → 系統 UI 語言 → 英文）
 *   - 字串查表 T(id)，含 3 層回退（目前語系 → en → zh-TW）
 *   - 格式化查表 Tf(id, ...)，使用執行緒區域環狀緩衝，可安全連續呼叫
 *   - 依語系選擇字型（CJK / 泰文 / 阿拉伯文 / 西里爾各有合適字型）
 *   - RTL（阿拉伯文）判斷
 *
 * 僅限自有 / 授權裝置之安全研究使用。
 */
#ifndef ZL_I18N_H
#define ZL_I18N_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "i18n_strings.h"

/* 目前語系索引（LANG_*） */
static int g_lang = LANG_EN;

/* ---------- 小工具 ---------- */

static const wchar_t *i18n_code(int idx) {
    if (idx < 0 || idx >= LANG_COUNT) idx = LANG_EN;
    return g_lang_info[idx].code;
}

static int i18n_is_rtl(void) {
    return (g_lang >= 0 && g_lang < LANG_COUNT) ? g_lang_info[g_lang].rtl : 0;
}

/* 以語系代碼找索引；找不到回 -1 */
static int i18n_index_of_code(const wchar_t *code) {
    if (!code || !*code) return -1;
    for (int i = 0; i < LANG_COUNT; i++) {
        if (_wcsicmp(g_lang_info[i].code, code) == 0) return i;
    }
    return -1;
}

/*
 * 系統 UI 語言 → 語系索引。
 * 先比對「主要語言 + 子語言」，再只比對主要語言（例如任何地區的中文 → zh）。
 * 找不到回 -1。
 */
static int i18n_detect_system(void) {
    LANGID lid = GetUserDefaultUILanguage();
    int primary = (int)(lid & 0x3FF);
    int sub = (int)((lid >> 10) & 0x3F);

    /* 1) 精確符合 primary + sublang */
    for (int i = 0; i < LANG_COUNT; i++) {
        if (g_lang_info[i].win_primary == primary &&
            g_lang_info[i].win_sublang >= 0 &&
            g_lang_info[i].win_sublang == sub) {
            return i;
        }
    }
    /* 2) 只符合 primary（取第一個；中文的預設項即 zh-TW） */
    for (int i = 0; i < LANG_COUNT; i++) {
        if (g_lang_info[i].win_primary == primary) return i;
    }
    return -1;
}

/* ---------- 查表 ---------- */

/* 目前語系 → en → zh-TW → 代碼字串。永不回傳 NULL。 */
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
 * 格式化查表。回傳執行緒區域環狀緩衝（TF_SLOTS 槽 × TF_LEN wchar），
 * 因此同一個運算式裡最多可安全使用 TF_SLOTS 次。
 * 注意：每個引數的 Tf() 都會「先佔用」自己的槽位，全部求值完才呼叫
 * swprintf，所以只要呼叫次數 ≤ TF_SLOTS 就不會自我覆蓋。
 * do_refresh() 的裝置資訊面板一次用到 11 個，故 TF_SLOTS 必須 ≥ 11。
 * 格式符由字串表提供；gen_i18n.py 已驗證所有語系的格式符一致。
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
    /* C99 vswprintf（MinGW 與 MSVC 2015+ 皆支援）；截斷時回傳 -1 */
    int n = vswprintf(buf, TF_LEN, T(id), ap);
    va_end(ap);
    if (n < 0) buf[TF_LEN - 1] = L'\0';   /* 截斷時仍確保結尾 */
    return buf;
}

/* ---------- 字型 ---------- */

static HFONT g_font = NULL;
static HFONT g_font_bold = NULL;
static wchar_t g_font_name[LF_FACESIZE] = L"";

/* 依序嘗試候選字型，回傳第一個存在的；都沒有則回 NULL */
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
            /* 只有真正選到該字型才算命中（否則 GDI 會代換成別的字型） */
            if (_wcsicmp(got, cands[i]) == 0) found = cands[i];
        }
    }
    ReleaseDC(NULL, dc);
    return found;
}

/*
 * 建立目前語系的 UI 字型。優先序：
 *   1. 該語系建議字型（i18n_strings.h 的 font 欄位）
 *   2. 每個語系的通用後備清單
 *   3. Segoe UI → Tahoma → 系統預設
 */
static void i18n_make_font(HWND hwnd) {
    if (g_font) { DeleteObject(g_font); g_font = NULL; }
    if (g_font_bold) { DeleteObject(g_font_bold); g_font_bold = NULL; }

    int dpi = 96;
    HDC dc = GetDC(hwnd);
    if (dc) { dpi = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(hwnd, dc); }

    const wchar_t *face = NULL;
    const wchar_t *pref = (g_lang >= 0 && g_lang < LANG_COUNT) ? g_lang_info[g_lang].font : NULL;

    /* 依語系排列的後備字型鏈 */
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

    /* 先試語系專屬字型，再試後備鏈 */
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

/* 目前語系的字型；bold=1 取粗體 */
static HFONT i18n_font_ex(int bold) {
    return bold ? g_font_bold : g_font;
}

#endif /* ZL_I18N_H */
