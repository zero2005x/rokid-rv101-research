/*
 * theme.h — ZeroLightRokid Desktop v3.0 theming and drawing primitives
 *
 * Provides:
 *   - the brand palette (dark / light, colour values matching android/app-phone's
 *     colors.xml)
 *   - theme detection (follows Windows' AppsUseLightTheme, manually overridable)
 *   - GDI+ initialisation and shape drawing (anti-aliased rounded rectangles / circles /
 *     lines / paths)
 *   - geometric icons (drawn purely from paths, with no icon font dependency)
 *
 * Why GDI+: GDI's RoundRect/Ellipse are not anti-aliased, so the cyan filled buttons and
 * switches show visible jaggies on a dark background. gdiplus.dll is a Windows built-in
 * (since XP), so it adds no external dependency and keeps the "single exe, zero
 * dependencies" property.
 *
 * NOTE: under MinGW, GDI+ is usable from plain C (include <propidl.h> first) and linking
 * needs -lgdiplus -lole32 -luuid.
 *
 * Text still goes through GDI (DrawTextW): sharper than GDI+ DrawString and it can reuse
 * i18n.h's existing per-locale font chains.
 */
#ifndef ZL_THEME_H
#define ZL_THEME_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <propidl.h>
#include <gdiplus/gdiplus.h>

/* ================= palette ================= */

typedef enum {
    TC_BG,       /* window background */
    TC_CARD,     /* card background */
    TC_CARD2,    /* card background (secondary: input fields etc.) */
    TC_LINE,     /* border */
    TC_TEXT,     /* primary text */
    TC_TEXT2,    /* secondary text */
    TC_TEXT3,    /* dimmed text */
    TC_PRI,      /* brand primary (cyan) */
    TC_ONPRI,    /* text on the primary colour */
    TC_SEC,      /* brand secondary (amber) */
    TC_ONSEC,
    TC_OK,       /* success */
    TC_ERR,      /* error */
    TC_CHIP,     /* chip background */
    TC_SW_OFF,   /* switch track when off */
    TC_SW_KNOB,  /* switch knob */
    TC_HOVER,    /* hover overlay */
    TC_COUNT
} theme_color;

typedef struct {
    const wchar_t *name;
    int            dark;
    COLORREF       col[TC_COUNT];
} zl_theme;

/* dark: matches the phone brand (bg #0B1220 / primary #22D3EE / secondary #FFB300) */
static const zl_theme g_theme_dark = {
    L"dark", 1, {
    /* BG      */ RGB(0x0B,0x12,0x20),
    /* CARD    */ RGB(0x14,0x1D,0x2F),
    /* CARD2   */ RGB(0x19,0x23,0x3A),
    /* LINE    */ RGB(0x24,0x30,0x4A),
    /* TEXT    */ RGB(0xE6,0xED,0xF3),
    /* TEXT2   */ RGB(0x93,0xA1,0xB1),
    /* TEXT3   */ RGB(0x66,0x74,0x8A),
    /* PRI     */ RGB(0x22,0xD3,0xEE),
    /* ONPRI   */ RGB(0x04,0x25,0x2B),
    /* SEC     */ RGB(0xFF,0xB3,0x00),
    /* ONSEC   */ RGB(0x3A,0x2A,0x00),
    /* OK      */ RGB(0x34,0xD3,0x99),
    /* ERR     */ RGB(0xF8,0x71,0x71),
    /* CHIP    */ RGB(0x1E,0x2A,0x42),
    /* SW_OFF  */ RGB(0x2A,0x35,0x50),
    /* SW_KNOB */ RGB(0xE6,0xED,0xF3),
    /* HOVER   */ RGB(0x22,0x2E,0x48),
    }};

/* light: the same brand colours on a bright background (cyan darkened to keep contrast) */
static const zl_theme g_theme_light = {
    L"light", 0, {
    RGB(0xEE,0xF2,0xF7), RGB(0xFF,0xFF,0xFF), RGB(0xF6,0xF9,0xFC),
    RGB(0xD6,0xDE,0xE8), RGB(0x0B,0x12,0x20), RGB(0x5A,0x6B,0x7D),
    RGB(0x87,0x97,0xA8), RGB(0x08,0x91,0xB2), RGB(0xFF,0xFF,0xFF),
    RGB(0xB4,0x53,0x09), RGB(0xFF,0xFF,0xFF), RGB(0x05,0x96,0x69),
    RGB(0xDC,0x26,0x26), RGB(0xE9,0xEF,0xF7), RGB(0xCB,0xD5,0xE1),
    RGB(0xFF,0xFF,0xFF), RGB(0xE2,0xE9,0xF2),
    }};

/* current theme (0 = dark, 1 = light) */
static const zl_theme *g_th = &g_theme_dark;
static int             g_theme_mode = 0;   /* -1 = follow the system, otherwise 0/1 */

static COLORREF TH(int idx) { return g_th->col[idx]; }
static int      TH_IS_DARK(void) { return g_th->dark; }

/* read Windows' app theme preference; returns 1 = light, 0 = dark */
static int theme_detect_system(void) {
    HKEY  k = NULL;
    DWORD v = 0, sz = sizeof(v), type = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &k) == ERROR_SUCCESS) {
        if (RegQueryValueExW(k, L"AppsUseLightTheme", NULL, &type,
                             (LPBYTE)&v, &sz) != ERROR_SUCCESS) {
            v = 0;
        }
        RegCloseKey(k);
    }
    return v ? 1 : 0;
}

/* mode: -1 = follow the system, 0 = force dark, 1 = force light */
static void theme_apply(int mode) {
    g_theme_mode = mode;
    int light = (mode < 0) ? theme_detect_system() : mode;
    g_th = light ? &g_theme_light : &g_theme_dark;
}

/* re-read the system setting (called on WM_SETTINGCHANGE) */
static void theme_refresh(void) {
    if (g_theme_mode < 0) theme_apply(-1);
}

/* ================= GDI+ initialisation ================= */

static ULONG_PTR g_gdip_token = 0;
static int       g_gdip_ok = 0;

static void gdip_init(void) {
    GdiplusStartupInput in;
    in.GdiplusVersion = 1;
    in.DebugEventCallback = NULL;
    in.SuppressBackgroundThread = FALSE;
    in.SuppressExternalCodecs = FALSE;
    if (GdiplusStartup(&g_gdip_token, &in, NULL) == Ok) g_gdip_ok = 1;
}
static void gdip_shutdown(void) {
    if (g_gdip_ok) { GdiplusShutdown(g_gdip_token); g_gdip_ok = 0; }
}

/* ================= drawing primitives ================= */

typedef struct {
    GpGraphics *g;
    HDC         dc;
} zl_canvas;

/* create a canvas from a memory DC; enables anti-aliasing */
static void cv_begin(zl_canvas *c, HDC dc) {
    c->dc = dc;
    c->g  = NULL;
    if (g_gdip_ok && GdipCreateFromHDC(dc, &c->g) == Ok) {
        GdipSetSmoothingMode(c->g, SmoothingModeAntiAlias);
        GdipSetPixelOffsetMode(c->g, PixelOffsetModeHalf);
        GdipSetCompositingQuality(c->g, CompositingQualityHighQuality);
    } else {
        c->g = NULL;
    }
}
static void cv_end(zl_canvas *c) {
    if (c->g) { GdipDeleteGraphics(c->g); c->g = NULL; }
}

static GpSolidFill *mk_brush(COLORREF c) {
    GpSolidFill *b = NULL;
    GdipCreateSolidFill((ARGB)(0xFF000000u | ((ARGB)GetRValue(c) << 16) |
                               ((ARGB)GetGValue(c) << 8) | (ARGB)GetBValue(c)), &b);
    return b;
}

/* filled rounded rectangle (r = corner radius in pixels) */
static void fill_round(zl_canvas *c, int x, int y, int w, int h, int r, COLORREF col) {
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (c->g) {
        GpSolidFill *b = mk_brush(col);
        if (!b) return;
        if (r <= 0) {
            GdipFillRectangleI(c->g, (GpBrush *)b, x, y, w, h);
        } else {
            GpPath *p = NULL;
            if (GdipCreatePath(FillModeWinding, &p) == Ok && p) {
                int d = r * 2;
                GdipAddPathArcI(p, x,           y,           d, d, 180.0f, 90.0f);
                GdipAddPathArcI(p, x + w - d,   y,           d, d, 270.0f, 90.0f);
                GdipAddPathArcI(p, x + w - d,   y + h - d,   d, d,   0.0f, 90.0f);
                GdipAddPathArcI(p, x,           y + h - d,   d, d,  90.0f, 90.0f);
                GdipClosePathFigure(p);
                GdipFillPath(c->g, (GpBrush *)b, p);
                GdipDeletePath(p);
            }
        }
        GdipDeleteBrush((GpBrush *)b);
    } else {
        /* fallback when GDI+ is unavailable (no anti-aliasing) */
        HBRUSH br = CreateSolidBrush(col);
        HPEN   pn = CreatePen(PS_SOLID, 1, col);
        HGDIOBJ ob = SelectObject(c->dc, br), op = SelectObject(c->dc, pn);
        RoundRect(c->dc, x, y, x + w, y + h, r * 2, r * 2);
        SelectObject(c->dc, ob); SelectObject(c->dc, op);
        DeleteObject(br); DeleteObject(pn);
    }
}

/* rounded rectangle outline */
static void stroke_round(zl_canvas *c, int x, int y, int w, int h, int r,
                         COLORREF col, float lw) {
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (c->g) {
        GpPen *pen = NULL;
        if (GdipCreatePen1((ARGB)(0xFF000000u | ((ARGB)GetRValue(col) << 16) |
                                  ((ARGB)GetGValue(col) << 8) | (ARGB)GetBValue(col)),
                           lw, UnitPixel, &pen) != Ok) return;
        if (r <= 0) {
            GdipDrawRectangleI(c->g, pen, x, y, w, h);
        } else {
            GpPath *p = NULL;
            if (GdipCreatePath(FillModeWinding, &p) == Ok && p) {
                int d = r * 2;
                float ix = x + lw / 2.0f, iy = y + lw / 2.0f;
                int iw = w - (int)lw, ih = h - (int)lw;
                GdipAddPathArcI(p, (int)ix, (int)iy, d, d, 180.0f, 90.0f);
                GdipAddPathArcI(p, (int)ix + iw - d, (int)iy, d, d, 270.0f, 90.0f);
                GdipAddPathArcI(p, (int)ix + iw - d, (int)iy + ih - d, d, d, 0.0f, 90.0f);
                GdipAddPathArcI(p, (int)ix, (int)iy + ih - d, d, d, 90.0f, 90.0f);
                GdipClosePathFigure(p);
                GdipDrawPath(c->g, pen, p);
                GdipDeletePath(p);
            }
        }
        GdipDeletePen(pen);
    } else {
        HPEN pn = CreatePen(PS_SOLID, (int)lw, col);
        HGDIOBJ op = SelectObject(c->dc, pn);
        HGDIOBJ ob = SelectObject(c->dc, GetStockObject(NULL_BRUSH));
        RoundRect(c->dc, x, y, x + w, y + h, r * 2, r * 2);
        SelectObject(c->dc, op); SelectObject(c->dc, ob);
        DeleteObject(pn);
    }
}

/* straight line */
static void d_line(zl_canvas *c, int x1, int y1, int x2, int y2, COLORREF col) {
    if (c->g) {
        GpPen *pen = NULL;
        if (GdipCreatePen1((ARGB)(0xFF000000u | ((ARGB)GetRValue(col) << 16) |
                                  ((ARGB)GetGValue(col) << 8) | (ARGB)GetBValue(col)),
                           1.0f, UnitPixel, &pen) != Ok) return;
        GdipDrawLineI(c->g, pen, x1, y1, x2, y2);
        GdipDeletePen(pen);
    } else {
        HPEN pn = CreatePen(PS_SOLID, 1, col);
        HGDIOBJ op = SelectObject(c->dc, pn);
        MoveToEx(c->dc, x1, y1, NULL); LineTo(c->dc, x2, y2);
        SelectObject(c->dc, op); DeleteObject(pn);
    }
}

/* filled rounded rectangle (alpha 0-255; only the GDI+ path supports it) */
static void fill_round_a(zl_canvas *c, int x, int y, int w, int h, int r,
                         COLORREF col, int alpha) {
    if (w <= 0 || h <= 0) return;
    if (!c->g) { fill_round(c, x, y, w, h, r, col); return; }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    GpSolidFill *b = NULL;
    GdipCreateSolidFill((ARGB)(((ARGB)alpha << 24) | ((ARGB)GetRValue(col) << 16) |
                               ((ARGB)GetGValue(col) << 8) | (ARGB)GetBValue(col)), &b);
    if (!b) return;
    GpPath *p = NULL;
    if (GdipCreatePath(FillModeWinding, &p) == Ok && p) {
        int d = r * 2;
        GdipAddPathArcI(p, x, y, d, d, 180.0f, 90.0f);
        GdipAddPathArcI(p, x + w - d, y, d, d, 270.0f, 90.0f);
        GdipAddPathArcI(p, x + w - d, y + h - d, d, d, 0.0f, 90.0f);
        GdipAddPathArcI(p, x, y + h - d, d, d, 90.0f, 90.0f);
        GdipClosePathFigure(p);
        GdipFillPath(c->g, (GpBrush *)b, p);
        GdipDeletePath(p);
    }
    GdipDeleteBrush((GpBrush *)b);
}

/* circle */
static void fill_circle(zl_canvas *c, int cx, int cy, int r, COLORREF col) {
    if (r <= 0) return;
    if (c->g) {
        GpSolidFill *b = mk_brush(col);
        if (!b) return;
        GdipFillEllipseI(c->g, (GpBrush *)b, cx - r, cy - r, r * 2, r * 2);
        GdipDeleteBrush((GpBrush *)b);
    } else {
        HBRUSH br = CreateSolidBrush(col);
        HPEN   pn = CreatePen(PS_SOLID, 1, col);
        HGDIOBJ ob = SelectObject(c->dc, br), op = SelectObject(c->dc, pn);
        Ellipse(c->dc, cx - r, cy - r, cx + r + 1, cy + r + 1);
        SelectObject(c->dc, ob); SelectObject(c->dc, op);
        DeleteObject(br); DeleteObject(pn);
    }
}

/* ================= geometric icons (pure paths, no icon font) ================= */

typedef enum { IC_BOLT, IC_CHECK, IC_REFRESH, IC_POWER, IC_CHEVRON, IC_LOCK,
               IC_BULB, IC_MUTE, IC_INFO, IC_MORE } zl_icon;

/* draw the icon inside a square of side s whose top-left is (x,y); col is the line/fill colour */
static void draw_icon(zl_canvas *c, zl_icon ic, int x, int y, int s, COLORREF col) {
    if (s < 6) s = 6;
    float u = s / 16.0f;                       /* the design grid is 16 units */
    #define PX(v) ((int)(x + (v) * u))
    #define PY(v) ((int)(y + (v) * u))
    GpSolidFill *b = c->g ? mk_brush(col) : NULL;
    GpPen *pen = NULL;
    if (c->g) {
        GdipCreatePen1((ARGB)(0xFF000000u | ((ARGB)GetRValue(col) << 16) |
                              ((ARGB)GetGValue(col) << 8) | (ARGB)GetBValue(col)),
                       1.7f * u, UnitPixel, &pen);
        if (pen) {
            GdipSetPenStartCap(pen, LineCapRound);
            GdipSetPenEndCap(pen, LineCapRound);
            GdipSetPenLineJoin(pen, LineJoinRound);
        }
    }
    switch (ic) {
    case IC_BOLT: {
        GpPointF p[6] = {
            {(REAL)PX(9.5f),(REAL)PY(1)},  {(REAL)PX(4),(REAL)PY(9)},
            {(REAL)PX(7.5f),(REAL)PY(9)},  {(REAL)PX(6.5f),(REAL)PY(15)},
            {(REAL)PX(12),(REAL)PY(7)},    {(REAL)PX(8.5f),(REAL)PY(7)} };
        if (c->g && b) GdipFillPolygonI(c->g, (GpBrush *)b, (GpPoint *)p, 6, FillModeWinding);
        break; }
    case IC_CHECK:
        if (pen) {
            GdipDrawLineI(c->g, pen, PX(3), PY(8.5f), PX(6.5f), PY(12));
            GdipDrawLineI(c->g, pen, PX(6.5f), PY(12), PX(13), PY(4.5f));
        }
        break;
    case IC_REFRESH:
        if (pen) {
            GdipDrawArcI(c->g, pen, PX(2.5f), PY(2.5f), (int)(11*u), (int)(11*u), 40.0f, 280.0f);
            GpPointF a[3] = { {(REAL)PX(13.5f),(REAL)PY(2)}, {(REAL)PX(9.5f),(REAL)PY(2.5f)},
                              {(REAL)PX(13),(REAL)PY(6)} };
            if (b) GdipFillPolygonI(c->g, (GpBrush *)b, (GpPoint *)a, 3, FillModeWinding);
        }
        break;
    case IC_POWER:
        if (pen) {
            GdipDrawArcI(c->g, pen, PX(2.5f), PY(4), (int)(11*u), (int)(11*u), 300.0f, 300.0f);
            GdipDrawLineI(c->g, pen, PX(8), PY(1.5f), PX(8), PY(7.5f));
        }
        break;
    case IC_CHEVRON: {
        GpPointF p[3] = { {(REAL)PX(4),(REAL)PY(6.5f)}, {(REAL)PX(8),(REAL)PY(10.5f)},
                          {(REAL)PX(12),(REAL)PY(6.5f)} };
        if (pen) GdipDrawLinesI(c->g, pen, (GpPoint *)p, 3);
        break; }
    case IC_LOCK:
        if (pen) {
            GdipDrawArcI(c->g, pen, PX(4.5f), PY(2), (int)(7*u), (int)(8*u), 180.0f, 180.0f);
            GdipDrawRectangleI(c->g, pen, PX(3), PY(7), (int)(10*u), (int)(7.5f*u));
        }
        break;
    case IC_BULB:
        if (pen) {
            GdipDrawEllipseI(c->g, pen, PX(4.5f), PY(2), (int)(7*u), (int)(7*u));
            GdipDrawLineI(c->g, pen, PX(6), PY(11.5f), PX(10), PY(11.5f));
            GdipDrawLineI(c->g, pen, PX(6.8f), PY(14), PX(9.2f), PY(14));
        }
        break;
    case IC_MUTE:
        if (pen) {
            GdipDrawLineI(c->g, pen, PX(2), PY(8), PX(2), PY(8));
            GpPointF p[4] = { {(REAL)PX(3),(REAL)PY(6)}, {(REAL)PX(6),(REAL)PY(6)},
                              {(REAL)PX(9),(REAL)PY(3)}, {(REAL)PX(9),(REAL)PY(13)} };
            if (b) GdipFillPolygonI(c->g, (GpBrush *)b, (GpPoint *)p, 4, FillModeWinding);
            GdipDrawLineI(c->g, pen, PX(11.5f), PY(6), PX(14.5f), PY(10));
            GdipDrawLineI(c->g, pen, PX(14.5f), PY(6), PX(11.5f), PY(10));
        }
        break;
    case IC_INFO:
        if (pen) {
            GdipDrawEllipseI(c->g, pen, PX(2), PY(2), (int)(12*u), (int)(12*u));
            GdipDrawLineI(c->g, pen, PX(8), PY(7), PX(8), PY(11.5f));
            GdipDrawLineI(c->g, pen, PX(8), PY(4.5f), PX(8), PY(4.6f));
        }
        break;
    case IC_MORE:
        if (b) {
            GdipFillEllipseI(c->g, (GpBrush *)b, PX(3), PY(7), (int)(2.4f*u), (int)(2.4f*u));
            GdipFillEllipseI(c->g, (GpBrush *)b, PX(6.8f), PY(7), (int)(2.4f*u), (int)(2.4f*u));
            GdipFillEllipseI(c->g, (GpBrush *)b, PX(10.6f), PY(7), (int)(2.4f*u), (int)(2.4f*u));
        }
        break;
    }
    if (pen) GdipDeletePen(pen);
    if (b)   GdipDeleteBrush((GpBrush *)b);
    #undef PX
    #undef PY
}

#endif /* ZL_THEME_H */
