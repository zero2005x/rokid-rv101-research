/*
 * ui.h — ZeroLightRokid Desktop v3.0 自繪元件庫
 *
 * 提供卡片、按鈕、開關、狀態面板、chip、圖示文字等元件。
 * 只負責「畫」，不做版面與命中測試（那在 zero_light_desktop.c）。
 *
 * 文字一律走 GDI（DrawTextW），沿用 i18n.h 的語系字型鏈，
 * 因此中日韓泰阿拉伯文都能正確顯示。
 */
#ifndef ZL_UI_H
#define ZL_UI_H

#include "theme.h"

/* 按鈕外觀 */
typedef enum {
    BTN_PRIMARY,   /* 填色品牌主色：畫面上唯一的視覺重心 */
    BTN_GHOST,     /* 外框：次要動作 */
    BTN_SOFT,      /* 淡底：卡片內的次要動作 */
    BTN_DANGER,    /* 紅框：取消／破壞性 */
    BTN_SUCCESS    /* 綠底：狀態顯示（非動作） */
} ui_btnkind;

/* 互動狀態 */
typedef enum { ST_NORMAL = 0, ST_HOVER, ST_PRESSED, ST_DISABLED } ui_state;

typedef struct {
    HDC        dc;
    zl_canvas  cv;
    HFONT      font;      /* 一般 */
    HFONT      font_b;    /* 粗體 */
    HFONT      font_s;    /* 小字 */
    int        dpi;
} zl_ui;

/* ---- 文字 ---- */

static int ui_text_w(zl_ui *u, HFONT f, const wchar_t *s) {
    if (!s || !*s) return 0;
    HGDIOBJ old = SelectObject(u->dc, f);
    SIZE sz = { 0, 0 };
    GetTextExtentPoint32W(u->dc, s, (int)wcslen(s), &sz);
    SelectObject(u->dc, old);
    return sz.cx;
}

/* 在矩形內繪製文字；fmt 為 DT_* 旗標（預設單行、垂直置中、左靠） */
static void ui_text(zl_ui *u, HFONT f, const wchar_t *s, int x, int y,
                    int w, int h, COLORREF col, UINT fmt) {
    if (!s || !*s) return;
    HGDIOBJ old = SelectObject(u->dc, f);
    int om = SetBkMode(u->dc, TRANSPARENT);
    COLORREF oc = SetTextColor(u->dc, col);
    RECT r; r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    DrawTextW(u->dc, s, -1, &r, fmt | DT_NOPREFIX);
    SetTextColor(u->dc, oc);
    SetBkMode(u->dc, om);
    SelectObject(u->dc, old);
}

/* 置中單行 */
static void ui_text_c(zl_ui *u, HFONT f, const wchar_t *s, int x, int y,
                      int w, int h, COLORREF col) {
    ui_text(u, f, s, x, y, w, h, col, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS);
}
/* 左靠單行垂直置中 */
static void ui_text_l(zl_ui *u, HFONT f, const wchar_t *s, int x, int y,
                      int w, int h, COLORREF col) {
    ui_text(u, f, s, x, y, w, h, col, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
}
/* 右靠單行垂直置中 */
static void ui_text_r(zl_ui *u, HFONT f, const wchar_t *s, int x, int y,
                      int w, int h, COLORREF col) {
    ui_text(u, f, s, x, y, w, h, col, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
}

/* ---- 卡片 ---- */

#define UI_CARD_R 12

static void ui_card(zl_ui *u, int x, int y, int w, int h) {
    fill_round(&u->cv, x, y, w, h, UI_CARD_R, TH(TC_CARD));
    stroke_round(&u->cv, x, y, w, h, UI_CARD_R, TH(TC_LINE), 1.0f);
}

/* 卡片標題（左上）。尺寸必須依 DPI 縮放，否則高 DPI 下字被裁掉 */
static void ui_card_title(zl_ui *u, const wchar_t *s, int x, int y) {
    ui_text_l(u, u->font_s, s, x, y, MulDiv(260, u->dpi, 96), MulDiv(22, u->dpi, 96),
              TH(TC_TEXT2));
}

/* ---- 按鈕 ---- */

/* 依比例調整亮度（f < 1 變暗、f > 1 變亮） */
static COLORREF ui_shade(COLORREF c, double f) {
    int r = (int)(GetRValue(c) * f), g = (int)(GetGValue(c) * f), b = (int)(GetBValue(c) * f);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return RGB(r, g, b);
}

static void ui_button(zl_ui *u, int x, int y, int w, int h, const wchar_t *label,
                      ui_btnkind kind, int icon, ui_state st) {
    int r = MulDiv(9, u->dpi, 96);
    COLORREF bg = 0, fg = TH(TC_TEXT), brd = 0;
    int filled = 0, bordered = 0;

    switch (kind) {
    case BTN_PRIMARY: bg = TH(TC_PRI); fg = TH(TC_ONPRI); filled = 1; break;
    case BTN_SUCCESS: bg = TH(TC_OK);  fg = TH(TC_BG);    filled = 1; break;
    case BTN_SOFT:    bg = TH(TC_CHIP); fg = TH(TC_TEXT); filled = 1; break;
    case BTN_DANGER:  brd = TH(TC_ERR); fg = TH(TC_ERR);  bordered = 1; break;
    case BTN_GHOST:
    default:          brd = TH(TC_LINE); fg = TH(TC_TEXT); bordered = 1; break;
    }

    if (st == ST_DISABLED) {
        if (filled) { bg = TH(TC_CHIP); fg = TH(TC_TEXT3); }
        else        { brd = TH(TC_LINE); fg = TH(TC_TEXT3); }
    } else if (st == ST_PRESSED) {
        if (filled) bg = ui_shade(bg, TH_IS_DARK() ? 0.80 : 0.86);
        else        { bg = TH(TC_CHIP); filled = 1; fg = TH(TC_TEXT); }
    } else if (st == ST_HOVER) {
        if (filled) bg = ui_shade(bg, TH_IS_DARK() ? 1.18 : 0.93);
        else        { bg = TH(TC_HOVER); filled = 1; }
    }

    if (filled) fill_round(&u->cv, x, y, w, h, r, bg);
    if (bordered) stroke_round(&u->cv, x, y, w, h, r, brd, 1.0f);

    int isz = MulDiv(15, u->dpi, 96);
    int gap = icon >= 0 ? MulDiv(7, u->dpi, 96) : 0;
    int tw_ = ui_text_w(u, u->font, label);
    int total = tw_ + (icon >= 0 ? isz + gap : 0);
    int sx = x + (w - total) / 2;
    if (icon >= 0) {
        draw_icon(&u->cv, (zl_icon)icon, sx, y + (h - isz) / 2, isz, fg);
        sx += isz + gap;
    }
    ui_text_l(u, u->font, label, sx, y, tw_ + 2, h, fg);
}

/* ---- 開關（含標題與狀態副標） ---- */

/* 回傳整個元件的高度 */
static void ui_switch(zl_ui *u, int x, int y, int on, const wchar_t *label,
                      const wchar_t *sub, int hovered, int disabled) {
    int tw_ = MulDiv(44, u->dpi, 96);       /* 軌道寬 */
    int th_ = MulDiv(24, u->dpi, 96);       /* 軌道高 */
    int kn  = MulDiv(18, u->dpi, 96);       /* 旋鈕直徑 */

    COLORREF track = on ? TH(TC_PRI) : TH(TC_SW_OFF);
    if (disabled) track = TH(TC_CHIP);
    if (hovered && !disabled) {
        track = on ? ui_shade(track, 0.9) : TH(TC_HOVER);
    }
    fill_round(&u->cv, x, y, tw_, th_, th_ / 2, track);

    int kx = on ? (x + tw_ - kn - MulDiv(3, u->dpi, 96)) : (x + MulDiv(3, u->dpi, 96));
    COLORREF knob = disabled ? TH(TC_TEXT3)
                  : (on ? TH(TC_ONPRI)
                        : (TH_IS_DARK() ? TH(TC_SW_KNOB) : RGB(0xFF, 0xFF, 0xFF)));
    fill_circle(&u->cv, kx + kn / 2, y + th_ / 2, kn / 2, knob);

    /*
     * 標題與副標的垂直間距：兩行中心相隔 24 邏輯像素。
     * 先前只隔 15，在 200% 縮放下 26px 與 24px 的字幾乎貼在一起。
     * 文字區寬度必須依 DPI 縮放，否則長副標（如 Not running）會被截斷。
     */
    int tx  = x + tw_ + MulDiv(14, u->dpi, 96);
    int twx = MulDiv(230, u->dpi, 96);
    ui_text_l(u, u->font, label, tx, y - MulDiv(8, u->dpi, 96), twx,
              MulDiv(22, u->dpi, 96), disabled ? TH(TC_TEXT3) : TH(TC_TEXT));
    if (sub && *sub) {
        ui_text_l(u, u->font_s, sub, tx, y + MulDiv(16, u->dpi, 96), twx,
                  MulDiv(22, u->dpi, 96), on ? TH(TC_OK) : TH(TC_TEXT3));
    }
}

/* ---- 狀態面板（已 root 時取代大按鈕） ---- */

static void ui_status_panel(zl_ui *u, int x, int y, int w, int h,
                            const wchar_t *title, const wchar_t *sub, int ok) {
    int r = MulDiv(10, u->dpi, 96);
    COLORREF accent = ok ? TH(TC_OK) : TH(TC_SEC);
    /* 淡色底：以卡片色與強調色混合 */
    fill_round(&u->cv, x, y, w, h, r, TH(TC_CHIP));
    stroke_round(&u->cv, x, y, w, h, r, accent, 1.0f);

    int isz = MulDiv(22, u->dpi, 96);
    draw_icon(&u->cv, ok ? IC_CHECK : IC_INFO, x + MulDiv(18, u->dpi, 96),
              y + (h - isz) / 2, isz, accent);

    int tx = x + MulDiv(18, u->dpi, 96) + isz + MulDiv(14, u->dpi, 96);
    int avail = w - (tx - x) - MulDiv(18, u->dpi, 96);
    ui_text_l(u, u->font_b, title, tx, y + MulDiv(8, u->dpi, 96), avail,
              MulDiv(22, u->dpi, 96), TH(TC_TEXT));
    if (sub && *sub) {
        ui_text_l(u, u->font_s, sub, tx, y + MulDiv(30, u->dpi, 96), avail,
                  MulDiv(18, u->dpi, 96), TH(TC_TEXT2));
    }
}

/* ---- chip（裝置狀態小標籤） ---- */

static int ui_chip_w(zl_ui *u, const wchar_t *s) {
    return ui_text_w(u, u->font_s, s) + MulDiv(18, u->dpi, 96);
}
static void ui_chip(zl_ui *u, int x, int y, const wchar_t *s, COLORREF fg) {
    int w = ui_chip_w(u, s), h = MulDiv(21, u->dpi, 96);
    fill_round(&u->cv, x, y, w, h, MulDiv(6, u->dpi, 96), TH(TC_CHIP));
    ui_text_c(u, u->font_s, s, x, y, w, h, fg);
}

/* ---- 小圓點 ---- */
static void ui_dot(zl_ui *u, int cx, int cy, int r, COLORREF col) {
    fill_circle(&u->cv, cx, cy, r, col);
}

/* ---- 進度條 ---- */
static void ui_progress(zl_ui *u, int x, int y, int w, int h, double frac) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    int r = h / 2;
    fill_round(&u->cv, x, y, w, h, r, TH(TC_CHIP));
    int fw = (int)(w * frac);
    if (fw > h) fill_round(&u->cv, x, y, fw, h, r, TH(TC_PRI));
    else if (fw > 0) fill_circle(&u->cv, x + h / 2, y + h / 2, h / 2, TH(TC_PRI));
}

#endif /* ZL_UI_H */
