/*
 * theme.h — ZeroLightRokid Desktop v3.0 主題與繪製基礎
 *
 * 提供：
 *   - 品牌調色盤（深／淺兩套，色值與 android/app-phone 的 colors.xml 一致）
 *   - 主題偵測（跟隨 Windows 的 AppsUseLightTheme，可手動覆寫）
 *   - GDI+ 初始化與形狀繪製（抗鋸齒圓角矩形／圓／線／路徑）
 *   - 幾何圖示（純路徑繪製，不依賴任何圖示字型）
 *
 * 為什麼用 GDI+：GDI 的 RoundRect/Ellipse 沒有抗鋸齒，青色的填色按鈕與開關
 * 在深色底上會有明顯鋸齒。gdiplus.dll 是 Windows 內建元件（XP 起），
 * 不引入任何外部依賴，仍符合「單一 exe、零依賴」。
 *
 * 注意：GDI+ 在 MinGW 下可由純 C 使用（需先 include <propidl.h>），
 * 連結需加 -lgdiplus -lole32 -luuid。
 *
 * 文字仍走 GDI（DrawTextW）：比 GDI+ DrawString 銳利，且能沿用 i18n.h
 * 既有的各語系字型鏈。
 */
#ifndef ZL_THEME_H
#define ZL_THEME_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <propidl.h>
#include <gdiplus/gdiplus.h>

/* ================= 調色盤 ================= */

typedef enum {
    TC_BG,       /* 視窗底色 */
    TC_CARD,     /* 卡片底 */
    TC_CARD2,    /* 卡片底（次階，輸入框等） */
    TC_LINE,     /* 邊框 */
    TC_TEXT,     /* 主文字 */
    TC_TEXT2,    /* 次要文字 */
    TC_TEXT3,    /* 淡化文字 */
    TC_PRI,      /* 品牌主色（青） */
    TC_ONPRI,    /* 主色上的文字 */
    TC_SEC,      /* 品牌輔色（琥珀） */
    TC_ONSEC,
    TC_OK,       /* 成功 */
    TC_ERR,      /* 錯誤 */
    TC_CHIP,     /* chip 底色 */
    TC_SW_OFF,   /* 開關關閉時的軌道 */
    TC_SW_KNOB,  /* 開關旋鈕 */
    TC_HOVER,    /* 滑鼠移入疊色 */
    TC_COUNT
} theme_color;

typedef struct {
    const wchar_t *name;
    int            dark;
    COLORREF       col[TC_COUNT];
} zl_theme;

/* 深色：與手機板品牌一致（bg #0B1220 / primary #22D3EE / secondary #FFB300） */
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

/* 淺色：同一組品牌色在亮底的對應（青色加深以維持對比） */
static const zl_theme g_theme_light = {
    L"light", 0, {
    RGB(0xEE,0xF2,0xF7), RGB(0xFF,0xFF,0xFF), RGB(0xF6,0xF9,0xFC),
    RGB(0xD6,0xDE,0xE8), RGB(0x0B,0x12,0x20), RGB(0x5A,0x6B,0x7D),
    RGB(0x87,0x97,0xA8), RGB(0x08,0x91,0xB2), RGB(0xFF,0xFF,0xFF),
    RGB(0xB4,0x53,0x09), RGB(0xFF,0xFF,0xFF), RGB(0x05,0x96,0x69),
    RGB(0xDC,0x26,0x26), RGB(0xE9,0xEF,0xF7), RGB(0xCB,0xD5,0xE1),
    RGB(0xFF,0xFF,0xFF), RGB(0xE2,0xE9,0xF2),
    }};

/* 目前主題（0 = 深色，1 = 淺色） */
static const zl_theme *g_th = &g_theme_dark;
static int             g_theme_mode = 0;   /* -1 = 跟隨系統，否則 0/1 */

static COLORREF TH(int idx) { return g_th->col[idx]; }
static int      TH_IS_DARK(void) { return g_th->dark; }

/* 讀取 Windows 的應用程式主題偏好；回傳 1 = 淺色，0 = 深色 */
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

/* mode: -1 = 跟隨系統，0 = 強制深色，1 = 強制淺色 */
static void theme_apply(int mode) {
    g_theme_mode = mode;
    int light = (mode < 0) ? theme_detect_system() : mode;
    g_th = light ? &g_theme_light : &g_theme_dark;
}

/* 重新讀取系統設定（WM_SETTINGCHANGE 時呼叫） */
static void theme_refresh(void) {
    if (g_theme_mode < 0) theme_apply(-1);
}

/* ================= GDI+ 初始化 ================= */

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

/* ================= 繪製基礎 ================= */

typedef struct {
    GpGraphics *g;
    HDC         dc;
} zl_canvas;

/* 由記憶體 DC 建立畫布；開啟抗鋸齒 */
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

/* 圓角矩形填色（r = 圓角半徑，像素） */
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
        /* GDI+ 不可用時的後備（無抗鋸齒） */
        HBRUSH br = CreateSolidBrush(col);
        HPEN   pn = CreatePen(PS_SOLID, 1, col);
        HGDIOBJ ob = SelectObject(c->dc, br), op = SelectObject(c->dc, pn);
        RoundRect(c->dc, x, y, x + w, y + h, r * 2, r * 2);
        SelectObject(c->dc, ob); SelectObject(c->dc, op);
        DeleteObject(br); DeleteObject(pn);
    }
}

/* 圓角矩形外框 */
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

/* 直線 */
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

/* 圓角矩形填色（可指定透明度 alpha 0-255；僅 GDI+ 路徑支援） */
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

/* 圓 */
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

/* ================= 幾何圖示（純路徑，不依賴圖示字型） ================= */

typedef enum { IC_BOLT, IC_CHECK, IC_REFRESH, IC_POWER, IC_CHEVRON, IC_LOCK,
               IC_BULB, IC_MUTE, IC_INFO, IC_MORE } zl_icon;

/* 以 (x,y) 為左上角、邊長 s 的方形內繪製圖示；col 為線/填色 */
static void draw_icon(zl_canvas *c, zl_icon ic, int x, int y, int s, COLORREF col) {
    if (s < 6) s = 6;
    float u = s / 16.0f;                       /* 以 16 格為設計單位 */
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
