/*
 * ZlTest.cs — ZeroLightRokid Desktop 的 Windows 測試輔助（獨立主控台程式）
 *
 * 為什麼不是 PowerShell 腳本：
 *   先前用 PowerShell 直接 P/Invoke，連續踩到四個假訊號 ——
 *     1. $p.MainWindowHandle 在行程有第二個 top-level 視窗後會指向它，
 *        「移動主視窗」變成移動錯的視窗。
 *     2. PowerShell 把 $null 封送成空字串 ""，FindWindowW(cls, $null) 於是去找
 *        「標題為空字串」的視窗而回傳 0，看起來像視窗不存在。
 *     3. GetCapture() 只回報「呼叫執行緒」的捕獲視窗，從 PowerShell 呼叫永遠是
 *        NULL，拿它判斷點擊是否被接收完全無效。
 *     4. Add-Type 預設編譯成執行檔，public static IntPtr Main() 會被當成進入點
 *        候選，「警告視為錯誤」導致型別根本沒建立。
 *   改成獨立主控台程式後，全部 Win32 呼叫都在 C# 內、字串是 C# 的 null，
 *   輸出是純 stdout，可以從 bash 直接跑，沒有封送與編譯器特殊規則。
 *
 * 編譯（一次即可，之後重複使用）：
 *   csc.exe /nologo /optimize+ /out:ZlTest.exe ZlTest.cs /r:System.Drawing.dll
 *
 * 用法：
 *   ZlTest.exe info                     視窗狀態
 *   ZlTest.exe place                    把主視窗放回工作區中央
 *   ZlTest.exe origin                   客戶區原點在畫面何處
 *   ZlTest.exe front                    把主視窗帶到前景
 *   ZlTest.exe click X Y                對主視窗客戶區座標點擊
 *   ZlTest.exe esc                      送 Esc
 *   ZlTest.exe children main|log        列出子控制項
 *   ZlTest.exe childclick ID            對紀錄視窗的子控制項送 BM_CLICK
 *   ZlTest.exe logtext [max]            讀紀錄視窗 Rich Edit 的內容摘要
 *   ZlTest.exe logopen                  紀錄視窗是否已建立
 *   ZlTest.exe shot PATH                截取主視窗
 *
 * 退出碼：0 成功、1 失敗（找不到視窗等）
 */
using System;
using System.Text;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Threading;

static class ZlTest
{
    // ---------------- Win32 ----------------
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern IntPtr FindWindowW(string cls, string name);
    [DllImport("user32.dll")] static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern bool IsWindowEnabled(IntPtr h);
    [DllImport("user32.dll")] static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h, int c);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
    [DllImport("user32.dll")] static extern bool GetMonitorInfoW(IntPtr m, ref MONITORINFO mi);
    [DllImport("user32.dll")] static extern IntPtr MonitorFromWindow(IntPtr h, uint f);
    [DllImport("user32.dll")] static extern bool ScreenToClient(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowTextLengthW(IntPtr h);
    [DllImport("user32.dll")] static extern bool SetProcessDpiAwarenessContext(IntPtr c);

    struct RECT { public int Left, Top, Right, Bottom; }
    struct POINT { public int X, Y; }
    struct MONITORINFO { public int cbSize; public RECT rcMonitor; public RECT rcWork; public uint dwFlags; }
    delegate bool EnumProc(IntPtr h, IntPtr l);

    const string MAIN_CLASS = "ZeroLightDesktop";
    const string LOG_CLASS  = "ZeroLightLogWnd";
    const int    LOG_EDIT   = 3101;

    static IntPtr MainWnd() { return FindWindowW(MAIN_CLASS, null); }
    static IntPtr LogWnd()  { return FindWindowW(LOG_CLASS,  null); }

    static int Main(string[] args)
    {
        try { SetProcessDpiAwarenessContext(new IntPtr(-4)); } catch { }
        if (args.Length == 0) { Usage(); return 1; }

        string cmd = args[0].ToLowerInvariant();
        IntPtr h = MainWnd();

        switch (cmd)
        {
        case "info":
            if (h == IntPtr.Zero) { Console.WriteLine("MAIN_NOT_FOUND"); return 1; }
            {
                RECT w, c;
                GetWindowRect(h, out w);
                GetClientRect(h, out c);
                Console.WriteLine("main hwnd = " + h);
                Console.WriteLine("visible=" + IsWindowVisible(h) + " enabled=" + IsWindowEnabled(h)
                                  + " iconic=" + IsIconic(h));
                Console.WriteLine("window = (" + w.Left + "," + w.Top + ") "
                                  + (w.Right - w.Left) + "x" + (w.Bottom - w.Top));
                Console.WriteLine("client = " + (c.Right - c.Left) + "x" + (c.Bottom - c.Top));
                IntPtr lw = LogWnd();
                if (lw == IntPtr.Zero) Console.WriteLine("log hwnd = (none)");
                else Console.WriteLine("log hwnd = " + lw + " visible=" + IsWindowVisible(lw));
            }
            return 0;

        case "logopen":
            Console.WriteLine(LogWnd() != IntPtr.Zero ? "YES" : "NO");
            return 0;

        case "place":
            if (h == IntPtr.Zero) { Console.WriteLine("MAIN_NOT_FOUND"); return 1; }
            {
                // 一定要先還原：最小化的視窗量到的是 314x50 的虛擬尺寸，
                // 直接 SetWindowPos 也不會還原（實測踩過）。
                if (IsIconic(h)) { ShowWindow(h, 9); Thread.Sleep(500); }
                MONITORINFO mi = new MONITORINFO();
                mi.cbSize = Marshal.SizeOf(mi);
                GetMonitorInfoW(MonitorFromWindow(h, 2), ref mi);
                int ww = mi.rcWork.Right - mi.rcWork.Left;
                int wh = mi.rcWork.Bottom - mi.rcWork.Top;
                RECT wr;
                GetWindowRect(h, out wr);
                int cw = wr.Right - wr.Left, ch = wr.Bottom - wr.Top;
                if (cw > ww) cw = ww;
                if (ch > wh) ch = wh;
                int nx = mi.rcWork.Left + (ww - cw) / 2;
                int ny = mi.rcWork.Top + (wh - ch) / 2;
                SetWindowPos(h, IntPtr.Zero, nx, ny, cw, ch, 0x0040);
                ShowWindow(h, 9);
                BringWindowToTop(h);
                SetForegroundWindow(h);
                Thread.Sleep(600);
                Console.WriteLine("placed " + nx + "," + ny + " " + cw + "x" + ch);
            }
            return 0;

        case "front":
            if (h == IntPtr.Zero) { Console.WriteLine("MAIN_NOT_FOUND"); return 1; }
            ShowWindow(h, 9);
            BringWindowToTop(h);
            SetForegroundWindow(h);
            Thread.Sleep(400);
            Console.WriteLine("front");
            return 0;

        case "origin":
            if (h == IntPtr.Zero) { Console.WriteLine("MAIN_NOT_FOUND"); return 1; }
            {
                RECT w, c;
                GetWindowRect(h, out w);
                GetClientRect(h, out c);
                POINT p = new POINT(); p.X = 0; p.Y = 0;
                ScreenToClient(h, ref p);
                int dx = (w.Right - w.Left) - (c.Right - c.Left);
                int dy = (w.Bottom - w.Top) - (c.Bottom - c.Top);
                Console.WriteLine("origin=" + p.X + "," + p.Y + " frame=" + dx + "x" + dy
                                  + " win=(" + w.Left + "," + w.Top + ")");
            }
            return 0;

        case "click":
            if (h == IntPtr.Zero) { Console.WriteLine("MAIN_NOT_FOUND"); return 1; }
            if (args.Length < 3) { Console.WriteLine("usage: click X Y"); return 1; }
            {
                int x = int.Parse(args[1]), y = int.Parse(args[2]);
                if (IsIconic(h)) { ShowWindow(h, 9); Thread.Sleep(400); }
                IntPtr lp = (IntPtr)((y << 16) | (x & 0xFFFF));
                PostMessageW(h, 0x0200, IntPtr.Zero, lp); Thread.Sleep(140);
                PostMessageW(h, 0x0201, (IntPtr)1, lp);   Thread.Sleep(130);
                PostMessageW(h, 0x0202, IntPtr.Zero, lp); Thread.Sleep(1500);
                Console.WriteLine("clicked " + x + "," + y);
            }
            return 0;

        case "esc":
            if (h == IntPtr.Zero) { Console.WriteLine("MAIN_NOT_FOUND"); return 1; }
            PostMessageW(h, 0x0100, (IntPtr)27, IntPtr.Zero);
            Thread.Sleep(900);
            Console.WriteLine("esc");
            return 0;

        case "children":
            {
                bool logWin = args.Length > 1 && args[1].ToLowerInvariant() == "log";
                IntPtr t = logWin ? LogWnd() : MainWnd();
                if (t == IntPtr.Zero) { Console.WriteLine("WINDOW_NOT_FOUND"); return 1; }
                int n = 0;
                EnumChildWindows(t, delegate(IntPtr c, IntPtr l)
                {
                    StringBuilder cn = new StringBuilder(64);
                    GetClassNameW(c, cn, 64);
                    StringBuilder tx = new StringBuilder(64);
                    GetWindowTextW(c, tx, 64);
                    RECT r;
                    GetWindowRect(c, out r);
                    string s = tx.ToString();
                    if (s.Length > 34) s = s.Substring(0, 34);
                    Console.WriteLine("  " + cn.ToString().PadRight(14)
                                      + " vis=" + (IsWindowVisible(c) ? "Y" : "n")
                                      + " (" + r.Left + "," + r.Top + ") [" + s + "]");
                    n++;
                    return true;
                }, IntPtr.Zero);
                Console.WriteLine("  (" + n + " children)");
            }
            return 0;

        case "childclick":
            {
                IntPtr lw = LogWnd();
                if (lw == IntPtr.Zero) { Console.WriteLine("LOG_NOT_FOUND"); return 1; }
                if (args.Length < 2) { Console.WriteLine("usage: childclick ID"); return 1; }
                int id = int.Parse(args[1]);
                IntPtr b = GetDlgItem(lw, id);
                if (b == IntPtr.Zero) { Console.WriteLine("CHILD_NOT_FOUND " + id); return 1; }
                SendMessageW(b, 0x00F5, IntPtr.Zero, IntPtr.Zero);   // BM_CLICK
                Thread.Sleep(900);
                Console.WriteLine("clicked child " + id);
            }
            return 0;

        case "logtext":
            {
                IntPtr lw = LogWnd();
                if (lw == IntPtr.Zero) { Console.WriteLine("LOG_NOT_FOUND"); return 1; }
                IntPtr e = GetDlgItem(lw, LOG_EDIT);
                if (e == IntPtr.Zero) { Console.WriteLine("EDIT_NOT_FOUND"); return 1; }
                int max = args.Length > 1 ? int.Parse(args[1]) : 200000;
                /*
                 * 不要先用 GetWindowTextLengthW 判斷有沒有內容：
                 * Rich Edit 不一定支援 WM_GETTEXTLENGTH（會回 0），
                 * 先前因此在「明明有內容」時回報 chars=0，害我以為篩選壞了。
                 * 直接開大緩衝區送 WM_GETTEXT 才是可靠做法。
                 */
                StringBuilder sb = new StringBuilder(max + 2);
                int n = GetWindowTextW(e, sb, max + 1);
                if (n <= 0) {
                    /*
                     * 已知限制：Rich Edit 的內容無法可靠地用 WM_GETTEXT 從「另一個行程」
                     * 讀取（跨行程只保證 marshalling WM_USER 以下的系統訊息，而 Rich Edit
                     * 的取文字路徑不是走這個）。主視窗那份之所以讀得到，是靠 App 自己
                     * 另闢的管道。要驗證紀錄視窗內容請用 shot 截圖，或請使用者目視確認。
                     */
                    Console.WriteLine("chars=0 (跨行程無法讀取 Rich Edit；請改用 shot 截圖)");
                    return 0;
                }
                string t = sb.ToString();
                int lines = 1;
                for (int i = 0; i < t.Length; i++) if (t[i] == '\n') lines++;
                string head = t.Replace("\r", "").Replace("\n", "|");
                if (head.Length > 70) head = head.Substring(0, 70);
                Console.WriteLine("chars=" + t.Length + " lines=" + lines + " head=[" + head + "]");
            }
            return 0;

        case "shot":
            if (h == IntPtr.Zero) { Console.WriteLine("MAIN_NOT_FOUND"); return 1; }
            if (args.Length < 2) { Console.WriteLine("usage: shot PATH"); return 1; }
            {
                ShowWindow(h, 9);
                Thread.Sleep(350);
                RECT r;
                GetWindowRect(h, out r);
                int w = r.Right - r.Left, hh = r.Bottom - r.Top;
                using (Bitmap bmp = new Bitmap(w, hh))
                using (Graphics g = Graphics.FromImage(bmp))
                {
                    g.CopyFromScreen(r.Left, r.Top, 0, 0, bmp.Size);
                    bmp.Save(args[1], ImageFormat.Png);
                }
                Console.WriteLine("saved " + args[1] + " " + w + "x" + hh);
            }
            return 0;

        default:
            Usage();
            return 1;
        }
    }

    static void Usage()
    {
        Console.WriteLine("ZlTest.exe info|logopen|place|front|origin|click X Y|esc|"
                          + "children main|log|childclick ID|logtext [max]|shot PATH");
    }
}
