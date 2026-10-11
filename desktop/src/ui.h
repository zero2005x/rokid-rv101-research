/*
 * ui.h — ZeroLightRokid Desktop v3.0 custom-drawn widget library
 *
 * Provides cards, buttons, switches, a status panel, chips and icon+text widgets.
 * It only draws; layout and hit testing live in zero_light_desktop.c.
 *
 * All text goes through GDI (DrawTextW) and reuses i18n.h's per-locale font chains, so CJK,
 * Thai and Arabic all render correctly.
 */
#ifndef ZL_UI_H
#define ZL_UI_H

#include "theme.h"

/* button appearance */
typedef enum {
    BTN_PRIMARY,   /* filled with the brand primary: the single visual focal point */
    BTN_GHOST,     /* outline: secondary action */
    BTN_SOFT,      /* tinted fill: a secondary action inside a card */
    BTN_DANGER,    /* red outline: cancel / destructive */
    BTN_SUCCESS    /* green fill: status display (not an action) */
} ui_btnkind;

/* interaction state */
typedef enum { ST_NORMAL = 0, ST_HOVER, ST_PRESSED, ST_DISABLED } ui_state;

typedef struct {
    HDC        dc;
    zl_canvas  cv;
    HFONT      font;      /* regular */
    HFONT      font_b;    /* bold */
    HFONT      font_s;    /* small */
    int        dpi;
} zl_ui;

/* ---- text ---- */

static int ui_text_w(zl_ui *u, HFONT f, const wchar_t *s) {
    if (!s || !*s) return 0;
    HGDIOBJ old = SelectObject(u->dc, f);
    SIZE sz = { 0, 0 };
    GetTextExtentPoint32W(u->dc, s, (int)wcslen(s), &sz);
    SelectObject(u->dc, old);
    return sz.cx;
}

/* draw text inside a rectangle; fmt holds DT_* flags (defaults: single line, v-centred, left) */
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

/* centred single line */
static void ui_text_c(zl_ui *u, HFONT f, const wchar_t *s, int x, int y,
                      int w, int h, COLORREF col) {
    ui_text(u, f, s, x, y, w, h, col, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS);
}
/* left-aligned single line, vertically centred */
static void ui_text_l(zl_ui *u, HFONT f, const wchar_t *s, int x, int y,
                      int w, int h, COLORREF col) {
    ui_text(u, f, s, x, y, w, h, col, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
}
/* right-aligned single line, vertically centred */
static void ui_text_r(zl_ui *u, HFONT f, const wchar_t *s, int x, int y,
                      int w, int h, COLORREF col) {
    ui_text(u, f, s, x, y, w, h, col, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
}

/* ---- card ---- */

#define UI_CARD_R 12

static void ui_card(zl_ui *u, int x, int y, int w, int h) {
    fill_round(&u->cv, x, y, w, h, UI_CARD_R, TH(TC_CARD));
    stroke_round(&u->cv, x, y, w, h, UI_CARD_R, TH(TC_LINE), 1.0f);
}

/* card title (top-left). Sizes must scale with DPI or the text is clipped at high DPI */
static void ui_card_title(zl_ui *u, const wchar_t *s, int x, int y) {
    ui_text_l(u, u->font_s, s, x, y, MulDiv(260, u->dpi, 96), MulDiv(22, u->dpi, 96),
              TH(TC_TEXT2));
}

/* ---- button ---- */

/* scale brightness by a factor (f < 1 darkens, f > 1 lightens) */
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

/* ---- switch (with label and status sub-line) ---- */

/* returns the height of the whole widget */
static void ui_switch(zl_ui *u, int x, int y, int on, const wchar_t *label,
                      const wchar_t *sub, int hovered, int disabled) {
    int tw_ = MulDiv(44, u->dpi, 96);       /* track width */
    int th_ = MulDiv(24, u->dpi, 96);       /* track height */
    int kn  = MulDiv(18, u->dpi, 96);       /* knob diameter */

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
     * Vertical spacing between the label and the sub-line: the two line centres are 24 logical
     * pixels apart. They used to be only 15 apart, which at 200% scaling made the 26px and 24px
     * text nearly touch.
     * The text-area width must scale with DPI, otherwise a long sub-line (e.g. "Not running")
     * is truncated.
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

/* ---- status panel (replaces the big button once rooted) ---- */

static void ui_status_panel(zl_ui *u, int x, int y, int w, int h,
                            const wchar_t *title, const wchar_t *sub, int ok) {
    int r = MulDiv(10, u->dpi, 96);
    COLORREF accent = ok ? TH(TC_OK) : TH(TC_SEC);
    /* tinted background: card colour blended with the accent */
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

/* ---- chip (small device-status label) ---- */

static int ui_chip_w(zl_ui *u, const wchar_t *s) {
    return ui_text_w(u, u->font_s, s) + MulDiv(18, u->dpi, 96);
}
static void ui_chip(zl_ui *u, int x, int y, const wchar_t *s, COLORREF fg) {
    int w = ui_chip_w(u, s), h = MulDiv(21, u->dpi, 96);
    fill_round(&u->cv, x, y, w, h, MulDiv(6, u->dpi, 96), TH(TC_CHIP));
    ui_text_c(u, u->font_s, s, x, y, w, h, fg);
}

/* ---- small dot ---- */
static void ui_dot(zl_ui *u, int cx, int cy, int r, COLORREF col) {
    fill_circle(&u->cv, cx, cy, r, col);
}

/* ---- progress bar ---- */
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
