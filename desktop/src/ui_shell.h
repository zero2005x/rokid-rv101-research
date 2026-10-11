/*
 * ui_shell.h — ZeroLightRokid Desktop v3.0 custom-drawn shell
 *
 * Included by zero_light_desktop.c after the "device operations" section
 * (it needs the do_* actions, the g_* state and i18n's T()/Tf()).
 *
 * Division of labour:
 *   ui_calc()          compute every rectangle from the client size (shared by paint and hit test)
 *   ui_paint()         WM_PAINT: double-buffered rendering of the whole window
 *   ui_hit()           coordinate -> element id
 *   ui_invoke()        element id -> action
 *   ui_sync_children() move the real child controls into place
 *
 * Only the text inputs and the log use real Win32 controls; everything else is custom-drawn.
 */
#ifndef ZL_UI_SHELL_H
#define ZL_UI_SHELL_H

/* ================= elements ================= */

enum {
    EL_NONE = 0,
    /* clickable elements: [1, EL_LAST_CLICK] */
    EL_THEME, EL_SETTINGS, EL_HELP,
    EL_REFRESH, EL_DETAIL,
    EL_PRIMARY, EL_REACQUIRE, EL_VERIFY, EL_REBOOT, EL_MANUAL,
    EL_STEP1, EL_STEP2, EL_STEP3,
    EL_SW_OTA, EL_SW_SILENT, EL_SW_LED, EL_SHIZUKU,
    EL_OTACHECK, EL_GLLOG, EL_RUNCMD,
    EL_LOGTGL, EL_LOGSAVE, EL_CLEARLOG, EL_LOGPOP,
    EL_CANCEL,
    EL_CLOSE_SET, EL_LANG, EL_THEME_CYCLE, EL_LANG_LIST, EL_BROWSE_APK, EL_CLOSE_LOGPOP,
    EL_LAST_CLICK,
    /* the following are geometry only (for positioning child controls); not clickable */
    EL_RECT_ADB, EL_RECT_SERIAL, EL_RECT_APK, EL_RECT_CMD, EL_RECT_LOG, EL_RECT_LOGPOP,
    EL_COUNT
};

static RECT g_rc[EL_COUNT];
static int  g_rc_ok[EL_COUNT];

/* ================= UI state =================
 * These globals are declared in zero_light_desktop.c's globals section (which must come before
 * this include); they are not redeclared here, to avoid type conflicts.
 */

/* child control IDs */
#define CID_ADB    2001
#define CID_SERIAL 2002
#define CID_APK    2003
#define CID_CMD    2004
#define CID_LOG    2005

/* ================= layout constants ================= */

static void ui_setrc(int id, int x, int y, int w, int h) {
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    g_rc[id].left = x; g_rc[id].top = y;
    g_rc[id].right = x + w; g_rc[id].bottom = y + h;
    g_rc_ok[id] = (w > 0 && h > 0);
}
#define RCW(id) (g_rc[id].right - g_rc[id].left)
#define RCH(id) (g_rc[id].bottom - g_rc[id].top)

static int ui_hit(int x, int y) {
    for (int i = 1; i < EL_LAST_CLICK; i++) {
        if (!g_rc_ok[i]) continue;
        if (x >= g_rc[i].left && x < g_rc[i].right &&
            y >= g_rc[i].top  && y < g_rc[i].bottom) return i;
    }
    return EL_NONE;
}

/* ================= theme application ================= */

static void ui_apply_theme(int mode) {
    theme_apply(mode);
    logwin_apply_theme();
    if (g_log) SendMessageW(g_log, EM_SETBKGNDCOLOR, 0, (LPARAM)TH(TC_CARD));
    if (g_main) InvalidateRect(g_main, NULL, TRUE);
}

/* ================= layout ================= */

static void ui_calc(void) {
    memset(g_rc_ok, 0, sizeof(g_rc_ok));
    RECT cr;
    if (!g_main || !GetClientRect(g_main, &cr)) return;
    int cw = cr.right, ch = cr.bottom;
    int x  = S(MARGIN), w = cw - 2 * S(MARGIN);
    int y  = S(HDR_H) + S(14);
    int bh = S(30);

    /* ---- title bar ---- */
    {
        int bw = S(92);
        ui_setrc(EL_HELP,     cw - S(MARGIN) - bw, S(10), bw, S(28));
        ui_setrc(EL_SETTINGS, g_rc[EL_HELP].left - S(8) - S(34), S(10), S(34), S(28));
        ui_setrc(EL_THEME,    g_rc[EL_SETTINGS].left - S(8) - bw, S(10), bw, S(28));
    }

    /* ---- device card ---- */
    {
        int cy = y;
        ui_setrc(EL_REFRESH, x + w - S(PAD) - S(96), cy + S(PAD) - S(6), S(96), bh);
        ui_setrc(EL_DETAIL,  g_rc[EL_REFRESH].left - S(8) - S(76), cy + S(PAD) - S(6), S(76), bh);
        y += S(104) + S(GAP);
    }

    /* ---- main card ---- */
    {
        int cardy = y;
        int mh = g_dev.rooted ? S(136) : S(178);
        int by = cardy + mh - S(PAD) - S(30);       /* bottom row */
        if (g_dev.rooted) {
            ui_setrc(EL_PRIMARY, x + S(PAD), cardy + S(PAD), w - 2 * S(PAD), S(58));
        } else {
            int bw2 = w - 2 * S(PAD);
            if (bw2 > S(430)) bw2 = S(430);
            ui_setrc(EL_PRIMARY, x + (w - bw2) / 2, cardy + S(PAD), bw2, S(60));
        }
        ui_setrc(EL_MANUAL, x + S(PAD), by, S(188), S(30));
        if (g_manual_open) {
            int sw3 = S(140), sx = g_rc[EL_MANUAL].right + S(10);
            ui_setrc(EL_STEP1, sx, by, sw3, S(30)); sx += sw3 + S(8);
            ui_setrc(EL_STEP2, sx, by, sw3, S(30)); sx += sw3 + S(8);
            ui_setrc(EL_STEP3, sx, by, sw3, S(30));
        }
        int rw = S(104), vw = S(92);
        ui_setrc(EL_REBOOT, x + w - S(PAD) - rw, by, rw, S(30));
        ui_setrc(EL_VERIFY, g_rc[EL_REBOOT].left - S(8) - vw, by, vw, S(30));
        if (g_dev.rooted) {
            /* already rooted: demote the one-click button to a secondary "reacquire root" */
            int aw = S(140);
            ui_setrc(EL_REACQUIRE, g_rc[EL_VERIFY].left - S(8) - aw, by, aw, S(30));
        }
        y += mh + S(GAP);
    }

    /* ---- tools card ---- */
    {
        int cardy = y, th = S(196);
        int col1 = x + S(PAD);
        int col2 = x + S(PAD) + (w - 2 * S(PAD)) / 2 + S(16);
        int sw = S(44) + S(230);
        int sy = cardy + S(PAD) + S(20);        /* switch track centre row */
        ui_setrc(EL_SW_OTA,    col1, sy, sw, S(50));
        ui_setrc(EL_SW_SILENT, col2, sy, sw, S(50));
        sy += S(56);                            /* row pitch: keeps the two text lines from crowding */
        ui_setrc(EL_SW_LED,    col1, sy, sw, S(50));
        ui_setrc(EL_SHIZUKU,   col2, sy, sw, S(50));
        ui_setrc(EL_OTACHECK,  col2 + sw + S(12), sy + S(10), S(100), S(30));
        ui_setrc(EL_GLLOG,     g_rc[EL_OTACHECK].right + S(8), sy + S(10), S(104), S(30));
        /* root command row: leave enough space between label and input so the rounded
         * background does not overlap the label */
        int cy = cardy + th - S(PAD) - S(32);
        ui_setrc(EL_RUNCMD, x + w - S(PAD) - S(84), cy, S(84), S(32));
        int edit_x = x + S(PAD) + S(104);
        ui_setrc(EL_RECT_CMD, edit_x, cy,
                 g_rc[EL_RUNCMD].left - S(14) - edit_x, S(32));
        y += th + S(GAP);
    }

    /* ---- log card ---- */
    {
        int cardy = y;
        int avail = ch - S(STATUSH) - S(GAP) - cardy;
        if (avail < S(60)) avail = S(60);
        int tw = S(60);
        int ax = x + w - S(PAD);
        ui_setrc(EL_LOGSAVE,  ax - tw, cardy + S(10), tw, S(24));
        ui_setrc(EL_CLEARLOG, g_rc[EL_LOGSAVE].left - S(10) - tw, cardy + S(10), tw, S(24));
        ui_setrc(EL_LOGTGL,   g_rc[EL_CLEARLOG].left - S(10) - S(74), cardy + S(10), S(74), S(24));
        ui_setrc(EL_LOGPOP,   g_rc[EL_LOGTGL].left - S(10) - S(96), cardy + S(10), S(96), S(24));
        if (g_log_open) {
            ui_setrc(EL_RECT_LOG, x + S(PAD), cardy + S(42),
                     w - 2 * S(PAD), avail - S(42) - S(PAD));
        }
        y += avail + S(GAP);
    }

    /* ---- status bar ---- */
    if (g_busy)
        ui_setrc(EL_CANCEL, cw - S(MARGIN) - S(88), ch - S(STATUSH) + S(3), S(88), S(27));

    /* ---- log popup ---- */
    if (g_log_popup) {
        int pw = cw - 2 * S(60), ph = ch - 2 * S(48);
        if (pw < S(520)) pw = cw - 2 * S(MARGIN);
        if (ph < S(320)) ph = ch - 2 * S(MARGIN);
        int px = (cw - pw) / 2, py = (ch - ph) / 2;
        /* the close button takes the far right first, then save/clear go leftwards — otherwise
         * the ✕ overlaps the save button */
        ui_setrc(EL_CLOSE_LOGPOP, px + pw - S(PAD) - S(30), py + S(12), S(30), S(30));
        int tw = S(60);
        ui_setrc(EL_LOGSAVE,  g_rc[EL_CLOSE_LOGPOP].left - S(12) - tw, py + S(15), tw, S(24));
        ui_setrc(EL_CLEARLOG, g_rc[EL_LOGSAVE].left - S(10) - tw, py + S(15), tw, S(24));
        ui_setrc(EL_RECT_LOGPOP, px + S(PAD), py + S(52),
                 pw - 2 * S(PAD), ph - S(52) - S(PAD));
    }

    /* ---- settings overlay ---- */
    if (g_set_open) {
        int pw = S(600), ph = S(300);
        int px = (cw - pw) / 2, py = (ch - ph) / 2;
        ui_setrc(EL_CLOSE_SET, px + pw - S(42), py + S(12), S(30), S(30));
        int fx = px + S(PAD) + S(96);
        int fw = pw - 2 * S(PAD) - S(96);
        ui_setrc(EL_RECT_ADB,    fx, py + S(58),  fw, S(30));
        ui_setrc(EL_RECT_SERIAL, fx, py + S(98),  fw, S(30));
        /* keep a "Browse…" button to the right of the APK field: typing the path by hand is
         * impractical */
        int bw = S(104);
        ui_setrc(EL_RECT_APK,    fx, py + S(138), fw - bw - S(8), S(30));
        ui_setrc(EL_BROWSE_APK,  fx + fw - bw, py + S(138), bw, S(30));
        ui_setrc(EL_LANG,        px + S(PAD), py + S(186), pw - 2*S(PAD), S(32));
        ui_setrc(EL_THEME_CYCLE, px + S(PAD), py + ph - S(PAD) - S(32), S(176), S(32));
        if (g_lang_open) {
            int n = LANG_COUNT + 1, ih = S(26);
            int lh = n * ih; if (lh > S(208)) lh = S(208);
            ui_setrc(EL_LANG_LIST, g_rc[EL_LANG].left, g_rc[EL_LANG].bottom + S(2),
                     RCW(EL_LANG), lh);
        }
    }
}

/* ================= child controls ================= */

/*
 * Create only the four that must be real controls: adb path / serial / root command / log.
 * All borderless — the rounded background and border are custom-drawn by the parent window so
 * they can follow the theme colours.
 */
static void ui_create_children(void) {
    DWORD ed = WS_CHILD | ES_AUTOHSCROLL;
    g_adb_edit = CreateWindowExW(0, L"EDIT", g_adb, ed,
                                 0, 0, 10, 10, g_main,
                                 (HMENU)(INT_PTR)CID_ADB, g_hinst, NULL);
    g_serial_edit = CreateWindowExW(0, L"EDIT", g_serial, ed,
                                    0, 0, 10, 10, g_main,
                                    (HMENU)(INT_PTR)CID_SERIAL, g_hinst, NULL);
    g_apk_edit = CreateWindowExW(0, L"EDIT", g_apk, ed,
                                 0, 0, 10, 10, g_main,
                                 (HMENU)(INT_PTR)CID_APK, g_hinst, NULL);
    g_cmd_edit = CreateWindowExW(0, L"EDIT", T(STR_CMD_DEFAULT), ed,
                                 0, 0, 10, 10, g_main,
                                 (HMENU)(INT_PTR)CID_CMD, g_hinst, NULL);
    g_log = CreateWindowExW(0, LOGCLS, L"",
                            WS_CHILD | ES_MULTILINE | ES_READONLY |
                            WS_VSCROLL | ES_AUTOVSCROLL,
                            0, 0, 10, 10, g_main,
                            (HMENU)(INT_PTR)CID_LOG, g_hinst, NULL);

    if (g_log) {
        SendMessageW(g_log, EM_SETBKGNDCOLOR, 0, (LPARAM)TH(TC_CARD));
        SendMessageW(g_log, EM_EXLIMITTEXT, 0, (LPARAM)(1024 * 1024));
        SendMessageW(g_log, EM_SETREADONLY, TRUE, 0);
        CHARFORMAT2W cf;
        memset(&cf, 0, sizeof(cf));
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR;
        cf.crTextColor = TH(TC_TEXT);
        SendMessageW(g_log, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    }
    HFONT f = g_font_body ? g_font_body : i18n_font_ex(0);
    HWND all[5]; all[0] = g_adb_edit; all[1] = g_serial_edit;
    all[2] = g_cmd_edit; all[3] = g_log; all[4] = g_apk_edit;
    for (int i = 0; i < 5; i++)
        if (all[i]) SendMessageW(all[i], WM_SETFONT, (WPARAM)f, TRUE);
}

static void ui_sync_children(void) {
    if (!g_main) return;

    /*
     * Child controls always draw above the parent's client area; a custom-drawn overlay cannot
     * cover them.
     *
     * Deliberately "hide everything first, then show the ones that should be shown" rather than
     * deciding show/hide per control: with per-control decisions a single wrong condition leaves
     * that control floating above the overlay (measured in turn with the root command box and
     * the Shizuku APK field surviving over the log popup).
     * Zero everything first, then set, and no residue is possible.
     */
    HWND all[] = { g_adb_edit, g_serial_edit, g_apk_edit, g_cmd_edit, g_log };
    for (int i = 0; i < (int)(sizeof(all) / sizeof(all[0])); i++) {
        if (!all[i]) continue;
        /*
         * SW_HIDE and move off-screen at the same time: ShowWindow(SW_HIDE) alone was measured
         * to leave ghosts on the custom-drawn overlay (the EDIT control's text stays on screen
         * until it is clicked). Moving off-screen guarantees invisibility even if hiding fails.
         */
        ShowWindow(all[i], SW_HIDE);
        SetWindowPos(all[i], NULL, -32000, -32000, 0, 0,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE | SWP_HIDEWINDOW);
    }

    const int overlay = (g_set_open || g_log_popup);

    struct { HWND h; int id; int vis; } ctl[] = {
        { g_adb_edit,    EL_RECT_ADB,    0 },   /* 0 = only in the settings overlay */
        { g_serial_edit, EL_RECT_SERIAL, 0 },
        { g_apk_edit,    EL_RECT_APK,    0 },
        { g_cmd_edit,    EL_RECT_CMD,    1 },   /* 1 = only when no overlay is open */
        { g_log,         EL_RECT_LOG,    2 },   /* 2 = when the log is expanded or popped up */
    };
    for (int i = 0; i < (int)(sizeof(ctl) / sizeof(ctl[0])); i++) {
        HWND h = ctl[i].h;
        if (!h) continue;
        int rid = ctl[i].id;
        if (rid == EL_RECT_LOG && g_log_popup && g_rc_ok[EL_RECT_LOGPOP]) rid = EL_RECT_LOGPOP;
        int vis = ctl[i].vis == 1 ? !overlay
                : (ctl[i].vis == 2 ? (g_log_open || g_log_popup) : g_set_open);
        if (vis && g_rc_ok[rid]) {
            SetWindowPos(h, HWND_TOP, g_rc[rid].left, g_rc[rid].top,
                         RCW(rid), RCH(rid), SWP_NOACTIVATE);
            ShowWindow(h, SW_SHOW);
        }
    }
}


/* ================= state queries ================= */

/* only cancel is available while busy */
static int ui_enabled(int id) {
    if (!g_busy) return 1;
    return id == EL_CANCEL;
}
static ui_state ui_st(int id) {
    if (!ui_enabled(id)) return ST_DISABLED;
    if (g_press == id && g_hover == id) return ST_PRESSED;
    if (g_hover == id) return ST_HOVER;
    return ST_NORMAL;
}

/* ================= painting ================= */

static void ui_paint_body(void) {
    zl_ui *u = &g_ui;
    RECT cr; GetClientRect(g_main, &cr);
    int cw = cr.right, ch = cr.bottom;
    int x = S(MARGIN), w = cw - 2 * S(MARGIN);
    int cy;

    /* ---- title bar ---- */
    fill_round(&u->cv, 0, 0, cw, S(HDR_H), 0, TH(TC_CARD));
    {
        int dotr = S(5);
        ui_dot(u, S(22) + dotr, S(HDR_H) / 2, dotr, TH(TC_PRI));
        int tx = S(22) + dotr * 2 + S(12);
        ui_text_l(u, u->font_b, L"ZeroLightRokid Desktop", tx, 0, S(260), S(HDR_H), TH(TC_TEXT));
        tx += ui_text_w(u, u->font_b, L"ZeroLightRokid Desktop") + S(8);
        wchar_t ver[24]; swprintf(ver, 24, L"v%ls", ZL_VERSION);
        int vw = ui_text_w(u, u->font_s, ver) + S(14);
        fill_round(&u->cv, tx, S(HDR_H)/2 - S(9), vw, S(18), S(5), TH(TC_CHIP));
        ui_text_c(u, u->font_s, ver, tx, S(HDR_H)/2 - S(9), vw, S(18), TH(TC_TEXT2));
    }
    {
        int th_mode = g_theme_mode < 0 ? 2 : g_theme_mode;
        const wchar_t *tl = th_mode == 2 ? T(STR_THEME_SYSTEM)
                          : (th_mode == 1 ? T(STR_THEME_LIGHT) : T(STR_THEME_DARK));
        ui_button(u, g_rc[EL_THEME].left, g_rc[EL_THEME].top, RCW(EL_THEME), RCH(EL_THEME),
                  tl, BTN_GHOST, -1, ui_st(EL_THEME));
        ui_button(u, g_rc[EL_SETTINGS].left, g_rc[EL_SETTINGS].top, RCW(EL_SETTINGS), RCH(EL_SETTINGS),
                  L"", BTN_GHOST, -1, ui_st(EL_SETTINGS));
        draw_icon(&u->cv, IC_MORE, g_rc[EL_SETTINGS].left + (RCW(EL_SETTINGS) - S(15))/2,
                  g_rc[EL_SETTINGS].top + (RCH(EL_SETTINGS) - S(15))/2, S(15), TH(TC_TEXT2));
        ui_button(u, g_rc[EL_HELP].left, g_rc[EL_HELP].top, RCW(EL_HELP), RCH(EL_HELP),
                  T(STR_BTN_ABOUT), BTN_GHOST, -1, ui_st(EL_HELP));
    }

    /* ---- device card ---- */
    cy = S(HDR_H) + S(14);
    ui_card(u, x, cy, w, S(104));
    ui_card_title(u, T(STR_CARD_DEVICE), x + S(PAD), cy + S(12));
    ui_button(u, g_rc[EL_REFRESH].left, g_rc[EL_REFRESH].top, RCW(EL_REFRESH), RCH(EL_REFRESH),
              T(STR_BTN_REFRESH), BTN_GHOST, IC_REFRESH, ui_st(EL_REFRESH));
    ui_button(u, g_rc[EL_DETAIL].left, g_rc[EL_DETAIL].top, RCW(EL_DETAIL), RCH(EL_DETAIL),
              g_detail_open ? T(STR_BTN_COLLAPSE) : T(STR_BTN_DETAIL), BTN_GHOST, -1, ui_st(EL_DETAIL));
    {
        int ry = cy + S(42);
        int ok = (g_dev.state == 1);
        int dcx = x + S(PAD) + S(4);
        ui_dot(u, dcx, ry + S(9), S(5), ok ? TH(TC_OK) : TH(TC_ERR));
        int tx = dcx + S(14);
        const wchar_t *ct = ok ? T(STR_LBL_ONLINE)
                          : (g_dev.state == -1 ? T(STR_ST_UNAUTHORIZED)
                          : (g_dev.state == -2 ? T(STR_ST_OFFLINE) : T(STR_LBL_OFFLINE)));
        int cwid = ui_text_w(u, u->font_b, ct);
        ui_text_l(u, u->font_b, ct, tx, ry, cwid + S(4), S(20), ok ? TH(TC_OK) : TH(TC_ERR));
        tx += cwid + S(14);
        ui_text_l(u, u->font, g_dev.serial, tx, ry, S(220), S(20), TH(TC_TEXT));

        /* chips: right-aligned */
        int cx = x + w - S(PAD);
        struct { const wchar_t *s; COLORREF c; } ch[4];
        int nc = 0;
        if (g_dev.fw_state == 2)      ch[nc].s = T(STR_FW_PATCHED),    ch[nc++].c = TH(TC_ERR);
        else if (g_dev.fw_state == 1) ch[nc].s = T(STR_FW_VULNERABLE), ch[nc++].c = TH(TC_OK);
        else                          ch[nc].s = T(STR_FW_UNKNOWN),    ch[nc++].c = TH(TC_TEXT2);
        if (g_dev.spl[0])     ch[nc].s = g_dev.spl,     ch[nc++].c = TH(TC_TEXT2);
        if (g_dev.selinux[0]) ch[nc].s = g_dev.selinux, ch[nc++].c = TH(TC_SEC);
        for (int i = nc - 1; i >= 0; i--) {
            int cwid2 = ui_chip_w(u, ch[i].s);
            cx -= cwid2;
            ui_chip(u, cx, ry + S(1), ch[i].s, ch[i].c);
            cx -= S(7);
        }
        /* second line: model / firmware / Android / uptime */
        wchar_t line2[512];
        long up = g_dev.uptime;
        swprintf(line2, 512, L"%ls · %ls · Android %ls · %ld:%02ld:%02ld",
                 g_dev.model[0] ? g_dev.model : L"-",
                 g_dev.fw[0] ? g_dev.fw : L"-",
                 g_dev.android[0] ? g_dev.android : L"-",
                 up / 3600, (up % 3600) / 60, up % 60);
        ui_text_l(u, u->font_s, line2, x + S(PAD), ry + S(24), w - 2*S(PAD), S(18), TH(TC_TEXT2));
    }

    /* ---- main card ---- */
    {
        int cardy = S(HDR_H) + S(14) + S(104) + S(GAP);
        int mh = g_dev.rooted ? S(136) : S(178);
        ui_card(u, x, cardy, w, mh);
        ui_card_title(u, T(STR_CARD_MAIN), x + S(PAD), cardy + S(12));
        if (g_dev.rooted) {
            wchar_t sub[256];
            swprintf(sub, 256, L"%ls · %ls",
                     g_dev.selinux[0] ? g_dev.selinux : L"SELinux -",
                     g_dev.ota_locked ? T(STR_SW_OTA_LOCKED) : T(STR_SW_OTA_UNLOCKED));
            ui_status_panel(u, g_rc[EL_PRIMARY].left, g_rc[EL_PRIMARY].top,
                            RCW(EL_PRIMARY), RCH(EL_PRIMARY), T(STR_ST_ROOT_TITLE), sub, 1);
            ui_button(u, g_rc[EL_REACQUIRE].left, g_rc[EL_REACQUIRE].top,
                      RCW(EL_REACQUIRE), RCH(EL_REACQUIRE),
                      T(STR_BTN_REACQUIRE), BTN_GHOST, -1, ui_st(EL_REACQUIRE));
        } else {
            ui_button(u, g_rc[EL_PRIMARY].left, g_rc[EL_PRIMARY].top,
                      RCW(EL_PRIMARY), RCH(EL_PRIMARY),
                      T(STR_BTN_ONECLICK), BTN_PRIMARY, IC_BOLT, ui_st(EL_PRIMARY));
            ui_text_c(u, u->font_s, T(STR_GUIDE_NOT_ROOTED),
                      x, g_rc[EL_PRIMARY].bottom + S(8), w, S(20), TH(TC_TEXT2));
        }
        ui_button(u, g_rc[EL_MANUAL].left, g_rc[EL_MANUAL].top, RCW(EL_MANUAL), RCH(EL_MANUAL),
                  T(STR_BTN_MANUAL), BTN_GHOST, -1, ui_st(EL_MANUAL));
        if (g_manual_open) {
            ui_button(u, g_rc[EL_STEP1].left, g_rc[EL_STEP1].top, RCW(EL_STEP1), RCH(EL_STEP1),
                      T(STR_BTN_PUSH), BTN_SOFT, -1, ui_st(EL_STEP1));
            ui_button(u, g_rc[EL_STEP2].left, g_rc[EL_STEP2].top, RCW(EL_STEP2), RCH(EL_STEP2),
                      T(STR_BTN_HELPER), BTN_SOFT, -1, ui_st(EL_STEP2));
            ui_button(u, g_rc[EL_STEP3].left, g_rc[EL_STEP3].top, RCW(EL_STEP3), RCH(EL_STEP3),
                      T(STR_BTN_TRIGGER), BTN_SOFT, -1, ui_st(EL_STEP3));
        }
        ui_button(u, g_rc[EL_VERIFY].left, g_rc[EL_VERIFY].top, RCW(EL_VERIFY), RCH(EL_VERIFY),
                  T(STR_BTN_VERIFY), BTN_GHOST, IC_CHECK, ui_st(EL_VERIFY));
        ui_button(u, g_rc[EL_REBOOT].left, g_rc[EL_REBOOT].top, RCW(EL_REBOOT), RCH(EL_REBOOT),
                  T(STR_BTN_REBOOT), BTN_GHOST, IC_POWER, ui_st(EL_REBOOT));
    }

    /* ---- tools card ---- */
    {
        int cardy = S(HDR_H) + S(14) + S(104) + S(GAP) + (g_dev.rooted ? S(136) : S(178)) + S(GAP);
        int th = S(196);            /* must match ui_calc() */
        ui_card(u, x, cardy, w, th);
        ui_card_title(u, T(STR_CARD_TOOLS), x + S(PAD), cardy + S(12));
        ui_switch(u, g_rc[EL_SW_OTA].left, g_rc[EL_SW_OTA].top + S(13), g_dev.ota_locked,
                  T(STR_BTN_OTALOCK),
                  g_dev.ota_locked ? T(STR_SW_OTA_LOCKED) : T(STR_SW_OTA_UNLOCKED),
                  g_hover == EL_SW_OTA, !ui_enabled(EL_SW_OTA));
        ui_switch(u, g_rc[EL_SW_SILENT].left, g_rc[EL_SW_SILENT].top + S(13), g_dev.silent_on,
                  T(STR_BTN_SILENT_ON),
                  g_dev.silent_on ? T(STR_SW_ON) : T(STR_SW_OFF),
                  g_hover == EL_SW_SILENT, !ui_enabled(EL_SW_SILENT));
        ui_switch(u, g_rc[EL_SW_LED].left, g_rc[EL_SW_LED].top + S(13), g_dev.led_off,
                  T(STR_BTN_LED_OFF),
                  g_dev.led_off ? T(STR_SW_LED_SUPPRESSED) : T(STR_SW_LED_NORMAL),
                  g_hover == EL_SW_LED, !ui_enabled(EL_SW_LED));
        ui_switch(u, g_rc[EL_SHIZUKU].left, g_rc[EL_SHIZUKU].top + S(13), g_dev.shizuku,
                  L"Shizuku",
                  g_dev.shizuku ? T(STR_VAL_RUNNING) : T(STR_VAL_STOPPED),
                  g_hover == EL_SHIZUKU, !ui_enabled(EL_SHIZUKU));
        ui_button(u, g_rc[EL_OTACHECK].left, g_rc[EL_OTACHECK].top, RCW(EL_OTACHECK), RCH(EL_OTACHECK),
                  T(STR_BTN_OTACHECK), BTN_GHOST, -1, ui_st(EL_OTACHECK));
        ui_button(u, g_rc[EL_GLLOG].left, g_rc[EL_GLLOG].top, RCW(EL_GLLOG), RCH(EL_GLLOG),
                  T(STR_BTN_GLLOG), BTN_GHOST, -1, ui_st(EL_GLLOG));
        /* root command row */
        int cyy = cardy + th - S(PAD) - S(30);
        ui_text_l(u, u->font_s, T(STR_LBL_ROOT_CMD), x + S(PAD), cyy - S(2),
                  S(96), S(32), TH(TC_TEXT2));
        fill_round(&u->cv, g_rc[EL_RECT_CMD].left - S(10), cyy - S(3),
                   RCW(EL_RECT_CMD) + S(20), S(38), S(8), TH(TC_CARD2));
        stroke_round(&u->cv, g_rc[EL_RECT_CMD].left - S(10), cyy - S(3),
                     RCW(EL_RECT_CMD) + S(20), S(38), S(8), TH(TC_LINE), 1.0f);
        ui_button(u, g_rc[EL_RUNCMD].left, g_rc[EL_RUNCMD].top, RCW(EL_RUNCMD), RCH(EL_RUNCMD),
                  T(STR_BTN_CUSTOM), BTN_GHOST, -1, ui_st(EL_RUNCMD));
    }

    /* ---- log card ---- */
    {
        int cardy = S(HDR_H) + S(14) + S(104) + S(GAP) + (g_dev.rooted ? S(136) : S(178))
                    + S(GAP) + S(196) + S(GAP);
        int avail = ch - S(STATUSH) - S(GAP) - cardy;
        if (avail < S(60)) avail = S(60);
        ui_card(u, x, cardy, w, avail);
        ui_card_title(u, T(STR_CARD_LOG), x + S(PAD), cardy + S(12));
        ui_button(u, g_rc[EL_LOGTGL].left, g_rc[EL_LOGTGL].top, RCW(EL_LOGTGL), RCH(EL_LOGTGL),
                  g_log_open ? T(STR_BTN_COLLAPSE) : T(STR_BTN_EXPAND), BTN_GHOST, -1, ui_st(EL_LOGTGL));
        ui_button(u, g_rc[EL_LOGPOP].left, g_rc[EL_LOGPOP].top, RCW(EL_LOGPOP), RCH(EL_LOGPOP),
                  T(STR_BTN_LOGPOP), BTN_GHOST, -1, ui_st(EL_LOGPOP));
        ui_button(u, g_rc[EL_LOGSAVE].left, g_rc[EL_LOGSAVE].top, RCW(EL_LOGSAVE), RCH(EL_LOGSAVE),
                  T(STR_BTN_SAVELOG), BTN_GHOST, -1, ui_st(EL_LOGSAVE));
        ui_button(u, g_rc[EL_CLEARLOG].left, g_rc[EL_CLEARLOG].top, RCW(EL_CLEARLOG), RCH(EL_CLEARLOG),
                  T(STR_BTN_CLEARLOG), BTN_GHOST, -1, ui_st(EL_CLEARLOG));
        if (!g_log_open) {
            int ry = cardy + S(42);
            int have = (g_last_result[0] != L'\0');
            ui_dot(u, x + S(PAD) + S(5), ry + S(9), S(5),
                   have ? (g_last_ok ? TH(TC_OK) : TH(TC_ERR)) : TH(TC_TEXT3));
            ui_text_l(u, u->font, have ? g_last_result : T(STR_ST_READY),
                      x + S(PAD) + S(18), ry, w - 2*S(PAD) - S(18), S(20),
                      have ? TH(TC_TEXT) : TH(TC_TEXT2));
            /* preview the last two lines (dimmed), turning the collapsed blank space into useful
             * information */
            int py = ry + S(22);
            for (int i = 0; i < g_logtail_n && i < 2; i++) {
                const wchar_t *ln = g_logtail[i];
                if (!ln[0]) continue;
                ui_text_l(u, u->font_s, ln, x + S(PAD) + S(18), py,
                          w - 2 * S(PAD) - S(64), S(18), TH(TC_TEXT3));
                py += S(18);
            }
        }
    }

    /* ---- status bar ---- */
    {
        int sy = ch - S(STATUSH);
        d_line(&u->cv, 0, sy, cw, sy, TH(TC_LINE));
        int ok = (g_dev.state == 1);
        ui_dot(u, S(22) + S(5), sy + S(17), S(5),
               ok ? TH(TC_OK) : (g_dev.state == 0 ? TH(TC_TEXT3) : TH(TC_ERR)));
        if (g_prog_show) {
            int pw = S(200), px = cw - S(MARGIN) - pw - (g_busy ? S(96) : 0);
            ui_text_r(u, u->font_s, g_prog_text, px - S(10) - S(240), sy + S(2),
                      S(240), S(STATUSH), TH(TC_TEXT2));
            ui_progress(u, px, sy + S(13), pw, S(8), g_prog_frac);
        } else {
            ui_text_l(u, u->font_s, g_status_text, S(22) + S(16), sy, cw - S(300), S(STATUSH), TH(TC_TEXT2));
        }
        if (g_busy)
            ui_button(u, g_rc[EL_CANCEL].left, g_rc[EL_CANCEL].top, RCW(EL_CANCEL), RCH(EL_CANCEL),
                      T(STR_BTN_CANCEL), BTN_DANGER, -1, ui_st(EL_CANCEL));
    }

    /* ---- log popup ---- */
    if (g_log_popup) {
        /* scrim: same as the settings overlay, needs a 32bpp DIB for alpha to apply */
        fill_round_a(&u->cv, 0, 0, cw, ch, 0, TH(TC_BG), 210);
        int pw = RCW(EL_RECT_LOGPOP) + 2 * S(PAD);
        int ph = RCH(EL_RECT_LOGPOP) + S(52) + S(PAD);
        int px = g_rc[EL_RECT_LOGPOP].left - S(PAD);
        int py = g_rc[EL_RECT_LOGPOP].top - S(52);
        fill_round(&u->cv, px, py, pw, ph, S(12), TH(TC_CARD));
        stroke_round(&u->cv, px, py, pw, ph, S(12), TH(TC_LINE), 1.0f);
        ui_text_l(u, u->font_b, T(STR_CARD_LOG), px + S(PAD), py + S(14), S(300), S(24),
                  TH(TC_TEXT));
        ui_button(u, g_rc[EL_CLOSE_LOGPOP].left, g_rc[EL_CLOSE_LOGPOP].top,
                  RCW(EL_CLOSE_LOGPOP), RCH(EL_CLOSE_LOGPOP), L"\u2715",
                  BTN_GHOST, -1, ui_st(EL_CLOSE_LOGPOP));
        ui_button(u, g_rc[EL_LOGSAVE].left, g_rc[EL_LOGSAVE].top, RCW(EL_LOGSAVE), RCH(EL_LOGSAVE),
                  T(STR_BTN_SAVELOG), BTN_GHOST, -1, ui_st(EL_LOGSAVE));
        ui_button(u, g_rc[EL_CLEARLOG].left, g_rc[EL_CLEARLOG].top, RCW(EL_CLEARLOG), RCH(EL_CLEARLOG),
                  T(STR_BTN_CLEARLOG), BTN_GHOST, -1, ui_st(EL_CLEARLOG));
    }

    /* ---- settings overlay ---- */
    if (g_set_open) {
        /* scrim: dim the background so the overlay draws focus */
        fill_round_a(&u->cv, 0, 0, cw, ch, 0, TH(TC_BG), 200);
        int pw = S(600), ph = S(292);
        int px = (cw - pw) / 2, py = (ch - ph) / 2;
        fill_round(&u->cv, px, py, pw, ph, S(12), TH(TC_CARD));
        stroke_round(&u->cv, px, py, pw, ph, S(12), TH(TC_LINE), 1.0f);
        ui_text_l(u, u->font_b, T(STR_LBL_SETTINGS), px + S(PAD), py + S(14), S(300), S(22), TH(TC_TEXT));
        ui_button(u, g_rc[EL_CLOSE_SET].left, g_rc[EL_CLOSE_SET].top,
                  RCW(EL_CLOSE_SET), RCH(EL_CLOSE_SET), L"\u2715", BTN_GHOST, -1, ui_st(EL_CLOSE_SET));
        ui_text_l(u, u->font_s, T(STR_LBL_ADB_PATH), px + S(PAD), py + S(58), S(104), S(30), TH(TC_TEXT2));
        ui_text_l(u, u->font_s, T(STR_LBL_SERIAL), px + S(PAD), py + S(98), S(104), S(30), TH(TC_TEXT2));
        ui_text_l(u, u->font_s, T(STR_LBL_APK_PATH), px + S(PAD), py + S(138), S(104), S(30), TH(TC_TEXT2));
        fill_round(&u->cv, g_rc[EL_RECT_APK].left - S(8), py + S(138) - S(3),
                   RCW(EL_RECT_APK) + S(16), S(36), S(8), TH(TC_CARD2));
        stroke_round(&u->cv, g_rc[EL_RECT_APK].left - S(8), py + S(138) - S(3),
                     RCW(EL_RECT_APK) + S(16), S(36), S(8), TH(TC_LINE), 1.0f);
        ui_button(u, g_rc[EL_BROWSE_APK].left, g_rc[EL_BROWSE_APK].top,
                  RCW(EL_BROWSE_APK), RCH(EL_BROWSE_APK),
                  T(STR_BTN_BROWSE), BTN_GHOST, -1, ui_st(EL_BROWSE_APK));
        ui_text_l(u, u->font_s, T(STR_LBL_LANGUAGE), px + S(PAD), py + S(186), S(200), S(32), TH(TC_TEXT2));
        {
            wchar_t cur[128];
            if (g_lang_setting < 0) wcscpy(cur, T(STR_LANG_SYSTEM));
            else wcscpy(cur, g_lang_info[g_lang_setting].endonym);
            fill_round(&u->cv, g_rc[EL_LANG].left, g_rc[EL_LANG].top, RCW(EL_LANG), RCH(EL_LANG),
                       S(8), TH(TC_CARD2));
            stroke_round(&u->cv, g_rc[EL_LANG].left, g_rc[EL_LANG].top, RCW(EL_LANG), RCH(EL_LANG),
                         S(8), g_hover == EL_LANG ? TH(TC_PRI) : TH(TC_LINE), 1.0f);
            ui_text_l(u, u->font, cur, g_rc[EL_LANG].left + S(12), g_rc[EL_LANG].top,
                      RCW(EL_LANG) - S(40), RCH(EL_LANG), TH(TC_TEXT));
            draw_icon(&u->cv, IC_CHEVRON, g_rc[EL_LANG].right - S(26),
                      g_rc[EL_LANG].top + (RCH(EL_LANG) - S(14)) / 2, S(14), TH(TC_TEXT2));
        }
        {
            int tm = g_theme_mode < 0 ? 2 : g_theme_mode;
            const wchar_t *tl = tm == 2 ? T(STR_THEME_SYSTEM)
                              : (tm == 1 ? T(STR_THEME_LIGHT) : T(STR_THEME_DARK));
            ui_button(u, g_rc[EL_THEME_CYCLE].left, g_rc[EL_THEME_CYCLE].top,
                      RCW(EL_THEME_CYCLE), RCH(EL_THEME_CYCLE), tl, BTN_SOFT, -1, ui_st(EL_THEME_CYCLE));
        }
        /* language dropdown list */
        if (g_lang_open) {
            int lx = g_rc[EL_LANG_LIST].left, ly = g_rc[EL_LANG_LIST].top;
            int lw = RCW(EL_LANG_LIST), lh = RCH(EL_LANG_LIST), ih = S(26);
            fill_round(&u->cv, lx, ly, lw, lh, S(8), TH(TC_CARD2));
            stroke_round(&u->cv, lx, ly, lw, lh, S(8), TH(TC_LINE), 1.0f);
            int n = LANG_COUNT + 1;
            for (int i = 0; i < n; i++) {
                int iy = ly + i * ih;
                if (iy + ih > ly + lh) break;
                const wchar_t *nm = (i == 0) ? T(STR_LANG_SYSTEM) : g_lang_info[i-1].endonym;
                int sel = (i == 0) ? (g_lang_setting < 0) : (g_lang_setting == i - 1);
                if (sel) fill_round(&u->cv, lx + S(3), iy + S(1), lw - S(6), ih - S(2), S(6), TH(TC_CHIP));
                ui_text_l(u, u->font, nm, lx + S(12), iy, lw - S(24), ih,
                          sel ? TH(TC_PRI) : TH(TC_TEXT));
            }
        }
    }
}

/* double-buffered painting */
static void ui_paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT cr; GetClientRect(hwnd, &cr);
    int cw = cr.right, ch = cr.bottom;
    if (cw <= 0 || ch <= 0) { EndPaint(hwnd, &ps); return; }

    HDC     mem = CreateCompatibleDC(dc);
    /*
     * Use a 32bpp DIB section rather than CreateCompatibleBitmap: GDI+'s translucent fills (the
     * settings-overlay scrim) only alpha-blend correctly on a 32bpp surface; on a compatible
     * bitmap they fall back to opaque and the scrim has no visible effect at all.
     */
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = cw;
    bmi.bmiHeader.biHeight      = -ch;          /* top-down */
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp) { EndPaint(hwnd, &ps); DeleteDC(mem); return; }
    HGDIOBJ ob  = SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(TH(TC_BG));
    RECT   all; all.left = 0; all.top = 0; all.right = cw; all.bottom = ch;
    FillRect(mem, &all, bg);
    DeleteObject(bg);

    g_ui.dc = mem;
    g_ui.dpi = g_dpi;
    g_ui.font = g_font_body;
    g_ui.font_b = g_font_bold2;
    g_ui.font_s = g_font_small;
    cv_begin(&g_ui.cv, mem);
    ui_paint_body();
    cv_end(&g_ui.cv);

    BitBlt(dc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}


/* ================= action dispatch ================= */

/* read back the settings overlay's input fields */
static void ui_commit_settings(void) {
    if (g_adb_edit)    GetWindowTextW(g_adb_edit, g_adb, MAX_PATH);
    if (g_serial_edit) GetWindowTextW(g_serial_edit, g_serial, 128);
    if (g_apk_edit)    GetWindowTextW(g_apk_edit, g_apk, MAX_PATH);
    settings_save();
}

static void ui_toggle_theme(void) {
    int m = g_theme_mode;
    m = (m < 0) ? 0 : (m == 0 ? 1 : -1);
    g_theme_mode = m;
    ui_apply_theme(m);
    settings_save();
}

static void ui_invoke(int id) {
    if (id == EL_NONE) return;

    switch (id) {
    case EL_THEME:
    case EL_THEME_CYCLE: ui_toggle_theme(); return;

    case EL_SETTINGS:
        g_set_open = !g_set_open;
        g_lang_open = 0;
        g_log_popup = 0;          /* the two overlays are mutually exclusive */
        if (g_set_open) {
            if (g_adb_edit)    SetWindowTextW(g_adb_edit, g_adb);
            if (g_serial_edit) SetWindowTextW(g_serial_edit, g_serial);
            if (g_apk_edit)    SetWindowTextW(g_apk_edit, g_apk);
        } else {
            ui_commit_settings();
        }
        ui_calc(); ui_sync_children();
        InvalidateRect(g_main, NULL, TRUE);
        return;

    case EL_BROWSE_APK: {
        wchar_t f[MAX_PATH];
        if (pick_file(f, MAX_PATH, T(STR_DLG_PICK_APK_TITLE), T(STR_FILTER_APK), L"*.apk")) {
            wcsncpy(g_apk, f, MAX_PATH - 1);
            g_apk[MAX_PATH - 1] = L'\0';
            if (g_apk_edit) SetWindowTextW(g_apk_edit, g_apk);
            settings_save();
        }
        return; }

    case EL_CLOSE_SET:
        g_set_open = 0; g_lang_open = 0;
        ui_commit_settings();
        ui_calc(); ui_sync_children();
        InvalidateRect(g_main, NULL, TRUE);
        return;

    case EL_LANG:
        g_lang_open = !g_lang_open;
        ui_calc();
        InvalidateRect(g_main, NULL, TRUE);
        return;

    case EL_HELP: {
        MessageBoxW(g_main, Tf(STR_DLG_ABOUT_TEXT, ZL_VERSION),
                    T(STR_DLG_ABOUT_TITLE), MB_OK | MB_ICONINFORMATION);
        return; }

    case EL_DETAIL:
        g_detail_open = !g_detail_open;
        InvalidateRect(g_main, NULL, TRUE);
        return;

    case EL_MANUAL:
        g_manual_open = !g_manual_open;
        ui_calc();
        InvalidateRect(g_main, NULL, TRUE);
        return;

    case EL_LOGTGL:
        g_log_open = !g_log_open;
        ui_calc(); ui_sync_children();
        InvalidateRect(g_main, NULL, TRUE);
        return;

    case EL_LOGPOP:
        /* switch to a separate window: no more overlay, avoiding child-control z-order and hit
         * problems entirely */
        logwin_open();
        return;

    case EL_CLOSE_LOGPOP:
        g_log_popup = 0;
        ui_calc(); ui_sync_children();
        InvalidateRect(g_main, NULL, TRUE);
        return;

    case EL_LOGSAVE:  save_log(); return;
    case EL_CLEARLOG:
        logbuf_clear();
        if (g_log) SetWindowTextW(g_log, L"");
        g_last_result[0] = L'\0';
        g_last_ok = 1;
        InvalidateRect(g_main, NULL, TRUE);
        return;

    case EL_CANCEL:
        if (InterlockedCompareExchange(&g_busy, 0, 0) != 0) {
            log_msg(STR_LOG_CANCELLING);
            InterlockedExchange(&g_cancel, 1);
        }
        return;

    case EL_REFRESH: start_op(OP_REFRESH); return;
    case EL_VERIFY:  start_op(OP_VERIFY);  return;
    case EL_PRIMARY:
    case EL_REACQUIRE: start_op(OP_ONECLICK); return;
    case EL_STEP1:   start_op(OP_PUSH);    return;
    case EL_STEP2:   start_op(OP_HELPER);  return;
    case EL_STEP3:   start_op(OP_TRIGGER); return;
    case EL_OTACHECK: start_op(OP_OTACHECK); return;
    case EL_GLLOG:    start_op(OP_GLLOG);    return;
    case EL_SHIZUKU:
        /*
         * The file picker is only needed when the Shizuku manager is known not to be installed and
         * the user has not yet chosen an APK. The old code popped it whenever g_apk was empty,
         * interrupting every click even when Shizuku had long been installed and no APK was needed.
         *
         * shizuku_installed is filled in by a background poll (1=installed 0=not installed -1=not
         * probed yet), so no adb call is needed on the UI thread here. It still pops the picker
         * when the value is -1, covering the brief window right after app start before the first
         * poll returns.
         */
        if (!g_apk[0] && g_dev.shizuku_installed != 1) {
            wchar_t f[MAX_PATH];
            if (pick_file(f, MAX_PATH, T(STR_DLG_PICK_APK_TITLE),
                          T(STR_FILTER_APK), L"*.apk")) {
                wcsncpy(g_apk, f, MAX_PATH - 1);
                g_apk[MAX_PATH - 1] = L'\0';
                settings_save();
            }
        }
        start_op(OP_SHIZUKU);
        return;

    case EL_REBOOT:
        if (MessageBoxW(g_main, T(STR_DLG_REBOOT_TEXT), T(STR_DLG_REBOOT_TITLE),
                        MB_YESNO | MB_ICONQUESTION) == IDYES)
            start_op(OP_REBOOT);
        return;

    case EL_SW_OTA:
        /* the switch is the action: currently locked -> unlock; unlocked -> lock */
        start_op(g_dev.ota_locked ? OP_OTAUNLOCK : OP_OTALOCK);
        return;

    case EL_SW_SILENT:
        if (!g_dev.silent_on) {
            if (MessageBoxW(g_main, T(STR_DLG_SILENT_CONFIRM), Tf(STR_APP_TITLE, ZL_VERSION),
                            MB_YESNO | MB_ICONWARNING) != IDYES) return;
            start_op(OP_SILENT_ON);
        } else {
            start_op(OP_SILENT_OFF);
        }
        return;

    case EL_SW_LED:
        if (!g_dev.led_off) {
            if (MessageBoxW(g_main, T(STR_DLG_LED_CONFIRM), Tf(STR_APP_TITLE, ZL_VERSION),
                            MB_YESNO | MB_ICONWARNING) != IDYES) return;
            start_op(OP_LED_OFF);
        } else {
            start_op(OP_LED_ON);
        }
        return;

    case EL_RUNCMD: {
        wchar_t tmp[4096];
        tmp[0] = L'\0';
        if (g_cmd_edit) GetWindowTextW(g_cmd_edit, tmp, 4096);
        if (g_pending_cmd) { free(g_pending_cmd); g_pending_cmd = NULL; }
        g_pending_cmd = xwcsdup(tmp);
        if (!start_op(OP_CUSTOM) && g_pending_cmd) {
            free(g_pending_cmd);
            g_pending_cmd = NULL;
        }
        return; }

    case EL_LANG_LIST: {
        /* work out which entry was selected from the click's y coordinate */
        POINT pt; GetCursorPos(&pt); ScreenToClient(g_main, &pt);
        int ih = S(26), idx = (pt.y - g_rc[EL_LANG_LIST].top) / (ih ? ih : 1);
        if (idx < 0) idx = 0;
        if (idx > LANG_COUNT) idx = LANG_COUNT;
        g_lang_open = 0;
        apply_language(idx == 0 ? -1 : idx - 1);
        ui_calc(); ui_sync_children();
        InvalidateRect(g_main, NULL, TRUE);
        return; }
    }
}

#endif /* ZL_UI_SHELL_H */
