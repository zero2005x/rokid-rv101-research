# ZeroLightRokid Desktop — macOS Port Feasibility Study

> 中文版：[macos-port-feasibility.md](macos-port-feasibility.md)

> ⚠ **Line numbers are for reference only**: this document cites line numbers from `zero_light_desktop.c` extensively, and those line numbers come from a **snapshot taken during the v2.0 era**.
> After the v3.0 owner-drawn UI rewrite (and the 2026-09-13 boot loop protection), the line numbers have shifted substantially,
> so when citing them, search by **function name** rather than jumping straight to a line number.
> The string count given in the text as "190 entries" has also been updated to 241 entries (see the note below).


**Scope:** `/home/kali/ZeroLightRokidDesktop/src/zero_light_desktop.c` (826 lines, pure Win32 C), its two Android payloads, and the equivalent Bash tooling in `/home/kali/rokid_root/deploy/`.
**Verdict up front:** a native AppKit port is **viable and unusually cheap** (~10–15 working days) because this app's Win32 surface is tiny and contains none of the usual port-killers. A CLI/`.command` port is viable **today** (~4 hours) and delivers most of the functional value. Wine is **not viable**. Cross-platform GUI toolkits are **the wrong axis** — the constraint is *self-contained*, not *portable*.

---

> ## ⚠ Version Note (added 2026-09-11)
>
> This report analyses **v1.0** (`src/zero_light_desktop.c`, 826 lines).
> That file has since been rewritten as **v2.0** (roughly 2,300 lines), and the porting assessment's **conclusions are unchanged**,
> but the current state of the following items needs updating:
>
> | Statement in the report | Current state in v2.0 |
> |---|---|
> | "No `.rc`, no manifest, no icon, no version information" (§1.4) | **Added**: `src/app.rc` + `src/app.manifest`: version information, v6 common controls, Per-Monitor V2 high DPI |
> | "No DPI awareness, blurry at 150% scaling" (§1.10) | **Changed to Per-Monitor V2**, and `WM_DPICHANGED` is handled; all coordinates are scaled through `S()` |
> | "No progress bar, no dropdown menu" (§1.2) | **Added**: progress bar and language dropdown menu (14 locales) |
> | "`run_process` writes all of stdin before reading stdout, a potential deadlock" (§3.2) | **Fixed**: changed to a separate reader thread + `CancelSynchronousIo` for teardown |
> | "No cancellation mechanism; `do_oneclick` can block for 6 × 13 minutes" (§3.2) | **Added**: `g_cancel` and a "Cancel" button; every wait loop can be interrupted |
> | "Roughly 40–60 strings need to be extracted" (§5) | Now **241 strings × 14 locales**, with a complete generation/verification pipeline (`tools/`) |
> | "`GetPrivateProfileStringW` has no macOS equivalent" (§1.7) | Conclusion unchanged; still a point that must be changed when porting |
>
> **New porting considerations**: v2.0 additionally uses `winhttp` (OTA queries), `bcrypt` (MD5 signature),
> `comdlg32` (file dialogs), and the `comctl32` progress bar. The macOS counterparts are
> `NSURLSession`, `CommonCrypto`, `NSOpenPanel`/`NSSavePanel`, and `NSProgressIndicator` respectively.
> These are all built into AppKit/Foundation and **add no external dependencies whatsoever**, so the
> "ObjC + AppKit" recommendation in §2(a) still holds.
>
> In addition, v2.0's i18n design (a `g_i18n[locale][id]` table + `T()`/`Tf()` lookups)
> maps directly onto macOS's `NSLocalizedString` or `localizedStringForKey:`;
> Arabic RTL is mirrored automatically on macOS by `leadingAnchor`/`trailingAnchor`,
> and **does not need** the Win32 `WS_EX_LAYOUTRTL` machinery.



---

## 1. Win32 API surface inventory

Total: **43 Win32 functions + ~45 structs/constants ≈ 88 symbols**, spread across all 826 lines. Grouped:

### 1.1 Window / message loop / class registration
| Win32 | Lines | macOS equivalent |
|---|---|---|
| `WNDCLASSW`, `RegisterClassW` | 791–798 | none needed — `NSApplication` + `NSWindow`; no class registration API exists |
| `CreateWindowExW` (13 calls) | 634, 641, 643, 646, 648, 651, 653, 657, 673, 675, 680, 686 | `NSWindow`, `NSButton`, `NSTextField`, `NSTextView` allocation |
| `wWinMain` | 767 | `main()` + `NSApplicationMain()` / `[NSApp run]` |
| `GetMessageW` / `TranslateMessage` / `DispatchMessageW` | 820–824 | **deleted** — AppKit owns the run loop |
| `DefWindowProcW` | 764 | **deleted** — no unhandled-message forwarding concept |
| `WM_CLOSE` / `WM_DESTROY` / `PostQuitMessage` | 712–717 | `NSWindowDelegate windowWillClose:`, `applicationShouldTerminateAfterLastWindowClosed:` |
| `WM_COMMAND` | 718–742 | `NSButton` target/action selector |
| `WM_APP+1..+5` (`WM_LOG`…`WM_SERIAL`) + `PostMessageW` | 37–41, 100, 104, 108, 112, 614 | **`dispatch_async(dispatch_get_main_queue(), …)`** — the key structural mapping; pure-C `dispatch_async_f()` keeps the core in C |
| `HWND` globals | 59–60 | `NSWindow*` / `NSView*` / `NSTextView*` |

### 1.2 Controls / widgets
| Win32 | Lines | macOS equivalent |
|---|---|---|
| `"BUTTON"` + `BS_PUSHBUTTON` (10 buttons) | 634, 651, 653, 662–671 | `NSButton` |
| `"EDIT"` single-line + `ES_AUTOHSCROLL` (3) | 643, 648, 675 | `NSTextField` (editable, `bezeled`) |
| `"EDIT"` multiline + `ES_MULTILINE\|ES_AUTOVSCROLL\|ES_READONLY\|WS_VSCROLL` | 680–683 | `NSTextView` in `NSScrollView` |
| `EM_SETSEL` / `EM_REPLACESEL` / `EM_SCROLLCARET` | 695–701 | `NSTextStorage appendAttributedString:` + `scrollRangeToVisible:` (or `scrollToEndOfDocument:`) |
| `"STATIC"` (5 labels) | 641, 646, 657, 673, 686 | `NSTextField` label (`bezeled=NO, editable=NO, drawsBackground=NO`) |
| `GetWindowTextW` / `SetWindowTextW` / `GetWindowTextLengthW` | 591, 695, 699, 722, 723, 747, 750, 754 | `stringValue` / `string` |
| `EnableWindow` | 706 | `setEnabled:` |
| `InitCommonControlsEx` | 786–789 | **deleted** — no comctl32 |
| `WM_CTLCOLORSTATIC` + `GetStockObject(WHITE_BRUSH)` | 761–762 | `NSColor.whiteColor` / `controlBackgroundColor` |

**No listbox, no rich edit, no combo box, no tree view, no progress bar, no status bar control.** The only "editor" is a plain `EDIT` used as a read-only log.

### 1.3 GDI drawing / text
**Effectively absent.** Zero `BeginPaint`, `TextOut`, `DrawText`, `CreateFont`, `CreateSolidBrush`, `CreatePen`, `FillRect`, `BitBlt`, `Polygon`. The only GDI touch is `GetStockObject(WHITE_BRUSH)` (762) and `LoadCursor` (795). All text is child-control-rendered. **This is the single biggest de-risking fact in the whole study** — the usual "we have 2,000 lines of custom GDI rendering" wall does not exist here.

### 1.4 Dialog / menu
`MessageBoxW` (735) only. **No `DialogBox`, no `CreateDialog`, no `MAKEINTRESOURCE`, no menu bar, no accelerators, no `.rc` file, no `windres`** — confirmed by `build_mingw.bat` / `build_msvc.bat`, which link only `-lgdi32 -luser32 -lcomctl32` and compile a single `.c`. Consequence: **the `.exe` has no icon, no version resource, and no manifest.** → `NSAlert` for the one confirmation; but on macOS you *cannot* ship a double-clickable artifact without a `.app` bundle + `Info.plist` + `.icns`.

### 1.5 Threading & synchronization
| Win32 | Lines | macOS |
|---|---|---|
| `CreateThread` | 623 | `pthread_create` or `dispatch_async` global queue |
| `InterlockedCompareExchange` / `InterlockedExchange` on `volatile LONG` | 64, 619, 627 | C11 `_Atomic int` + `atomic_compare_exchange_strong` — a correctness upgrade (`volatile` is not a barrier) |
| `Sleep` | 386, 419, 495 | `nanosleep` (make it cancel-interruptible) |
| `WaitForSingleObject(hProcess, 200)` | 188 | `waitpid(WNOHANG)` + `poll()` timeout |
| `PostMessageW` cross-thread | 100–112, 614 | `dispatch_async(main_queue)` |

**No mutexes, critical sections, events, or semaphores.** One worker at a time, gated by an atomic flag.

### 1.6 Process creation & pipe I/O — the big chunk (`run_process`, 116–231)
| Win32 | Lines | macOS |
|---|---|---|
| `CreatePipe` ×2 | 131, 134 | `pipe(2)` |
| `SECURITY_ATTRIBUTES`/`bInheritHandle` | 119–128 | `posix_spawn_file_actions_adddup2` + `POSIX_SPAWN_CLOEXEC_DEFAULT` |
| `SetHandleInformation(..., HANDLE_FLAG_INHERIT, 0)` | 132, 135 | `fcntl(F_SETFD, FD_CLOEXEC)` (or Darwin's CLOEXEC_DEFAULT) |
| `CreateProcessW` + `STARTUPINFOW` + `PROCESS_INFORMATION` + `CREATE_NO_WINDOW` | 138–152 | **`posix_spawn`** (never `fork`+`exec` — see §3) |
| `WriteFile` (child stdin) | 161 | `write(2)` + SIGPIPE handling |
| `PeekNamedPipe` | 174 | **no equivalent, not needed** — `poll()` reports readability |
| `ReadFile` | 176, 199 | `read(2)` |
| `GetTickCount` | 170, 189 | `clock_gettime(CLOCK_MONOTONIC)` (no 49.7-day wrap) |
| `TerminateProcess` | 190 | `kill(SIGTERM)` → grace → `kill(SIGKILL)` |
| `GetExitCodeProcess` | 213 | `WIFEXITED ? WEXITSTATUS : 128+WTERMSIG` |
| `CloseHandle` (10 sites) | 154–155, 164, 214–215, 225–228 | `close(2)` |

### 1.7 File / INI config
`GetPrivateProfileStringW` / `WritePrivateProfileStringW` (355–364) → **no macOS equivalent**. `GetModuleFileNameW` (770) → `[[NSBundle mainBundle] executablePath]`. `GetFileAttributesW` (782) → `stat()`/`access(X_OK)`. `MAX_PATH` → `PATH_MAX` (1024 bytes). The `"%s\\assets\\..."` paths (395–396) → `[[NSBundle mainBundle] pathForResource:]` (resources live in `Contents/Resources/`). **See §4: "settings.ini next to the exe" is actively wrong on macOS** (app translocation + read-only `/Applications`).

### 1.8 String / encoding (UTF-8 ↔ UTF-16)
`MultiByteToWideChar` / `WideCharToMultiByte` (69–87), all `wchar_t`, `swprintf`, `_wcsdup`, `%ls`/`%hs` (235–241, 399, 404, 552–554, 725). → **deleted entirely.** macOS `char*` *is* UTF-8; `wchar_t` is 4-byte UTF-32 (different `%ls` semantics). `strtok_s` → `strtok_r`; `_strdup` → `strdup`; `SYSTEMTIME`/`GetLocalTime` (96–97, 810–811) → `localtime_r` + `strftime`. **Net deletion of ~20 lines and one entire bug class.**

### 1.9 Timers — **none.** No `SetTimer`, no `WM_TIMER`, no `timeSetEvent`. All pacing is `Sleep` + `GetTickCount` inside the worker thread. Maps cleanly.

### 1.10 DPI awareness — **none.** No `SetProcessDpiAwarenessContext`, no manifest, no `GetDpiForWindow`. The app is DPI-*unaware* on Windows and gets bitmap-stretched on HiDPI (blurry at 150%). On macOS there is no opt-in: `NSHighResolutionCapable` (default) gives Retina for free — but only with Auto Layout, not the hard-coded pixel grid at lines 31–35 / 638–688.

### 1.11 Registry / shell — **none.** No `RegOpenKeyEx`, no `SHGetFolderPath`, no `ShellExecute`, no `SHBrowseForFolder` (the user *types* the adb path, 643–645). On macOS you'll want `NSOpenPanel` for locating `adb` anyway.

### 1.12 Resources / manifest — **none** (see §1.4). macOS **requires** `Contents/{Info.plist, MacOS/, Resources/}` plus `CFBundleIdentifier`, `CFBundleExecutable`, `CFBundlePackageType=APPL`, `LSMinimumSystemVersion`, `NSHighResolutionCapable`, `CFBundleLocalizations`, `CFBundleIconFile` — and on Apple Silicon an **ad-hoc signature is mandatory**; an unsigned arm64 Mach-O will not execute.

### 1.13 Portable content
The two device-side payloads (275–348, 74 lines of `/system/bin/sh`) and the whole orchestration layer (`adb_available`, `wait_device`, `do_push`, `do_helper`, `do_trigger`, `do_verify`, `poll_once`, `do_oneclick`, `do_refresh`, `worker_thread`; lines 369–616, **~248 lines**) are **already portable C** modulo `wchar_t`→`char` and `%ls`→`%s`. The payload binaries themselves are `ELF 64-bit LSB pie executable, ARM aarch64 … for Android` (`ghostlock_rv101` 60,272 B, `root_daemon` 9,304 B) — **host-agnostic; zero porting work.**

**Bottom line:** ~39% of the file is portable domain logic, ~19% is adb plumbing a shell script gets free, ~28% is Win32 UI, ~8% is glue.

---

## 2. Porting strategy options

### (a) Objective-C / Swift + AppKit, keep the C core — ✅ **RECOMMENDED**
Split into `zero_light_core.c` (portable orchestration + POSIX process layer, exposing `zl_start_op(int)` and four callbacks `zl_log/zl_status/zl_info/zl_serial`) and `main.m` (~400 lines of programmatic AppKit; **no XIB, no Xcode project** — a 40-line Makefile driving `clang -fobjc-arc -framework Cocoa`).
- **Preserves C core: yes** (~250 LOC unchanged).
- **Pros:** the only GUI toolkit already installed on every Mac → **0 MB of bundle**, satisfying "no runtime deps" better than any cross-platform toolkit. Native RTL/i18n/accessibility for free (§5). `NSTextView` is a superset of the log `EDIT`. `dispatch_async` maps 1:1 onto the existing worker/WM_ message design. `xcode-select --install` gives clang without Xcode.app.
- **Cons:** new language; programmatic AppKit layout is verbose; needs a bundle + ad-hoc signature.
- **Effort:** ~500 new/rewritten LOC. **4–7 days** with macOS experience, 8–12 without. **Risk: low.**
- **(a′) Swift + SwiftUI** is a viable alternative (~400 LOC, same effort) with identical properties; slightly more toolchain churn.

### (b) Keep C core, swap only the UI for a portable toolkit
- **GTK4 (Homebrew):** `brew install gtk4` pulls ~80 transitive dylibs. Either you require the user to have Homebrew (**a runtime dependency — violates the core constraint**) or you vendor them and rewrite ~80 install names with `install_name_tool`, then re-sign each under hardened runtime/library validation — fragile. GTK's macOS Quartz backend trails Linux significantly. **Reject.**
- **Qt 6:** the *best* portable widget set here and the lowest code volume (~300 LOC of QWidget, very close to the Win32 mental model). But `macdeployqt` yields a **60 MB** `.app`, and LGPLv3 imposes dynamic-linking + relink obligations. You'd add 60 MB and a licence to save ~100 lines. **Best of the toolkits, wrong trade.**
- **Tcl/Tk:** Apple's bundled Tcl/Tk is **8.5.9 (2009)** and deprecated; modern Tk needs a framework build or a `starpack` (~5 MB). Tk 9 has no real BiDi implementation — **Arabic layout would be broken**, disqualifying it against the 14-locale requirement. **Reject.**
- **FLTK 1.4:** ~2 MB static, Cocoa backend, HiDPI. But FLTK **explicitly does not implement complex text shaping or BiDi** — no Arabic joining, no Thai mark positioning, no accessibility. **Reject.**
- **Nuklear / ImGui + SDL2:** immediate-mode, ~3 MB, but you re-implement text editing, selection, clipboard, and a scrolling log, and get **zero** shaping/BiDi. ~700 LOC to a strictly worse result. **Reject.**
- **Plain Cocoa via Objective-C:** this *is* (a). **Yes.**
- **Key insight:** the constraint isn't *portable*, it's *self-contained*. AppKit is the only toolkit that ships with the OS; every alternative **adds** the runtime dependency you're trying to avoid.

### (c) Cocoa/Carbon via Objective-C++, Win32-shaped structure
ObjC++ (`.mm`) works fine and preserves the C core (use it if you prefer C++ in the core). But **Carbon is not viable**: HIToolbox is deprecated since 10.8, has no HiDPI/RTL story comparable to AppKit, and buys nothing. And mimicking the Win32 shape (`WndProc` shim + `DefWindowProc` forwarding + child-HWND hierarchy) actively fights AppKit. **ObjC++ yes as an implementation detail; the Win32 shape, no.** Effort ~600 LOC / 5–9 days.

### (d) Ship the Windows build under Wine / CrossOver — ❌ **NOT VIABLE**
- **Wine on Apple Silicon** works: Gcenx's `macOS_Wine_builds` (11.x, latest 11.17) state *"wine will function via Rosetta2"*, require macOS Catalina+, and **require `GStreamer.framework` installed for all users** ([release notes](https://newreleases.io/project/github/Gcenx/macOS_Wine_builds/release/11.3)) — already a hard violation of "single self-contained app". Rosetta 2 remains present through macOS 26 Tahoe but Apple has signalled its eventual wind-down.
- **CrossOver 26** (Feb 2026) integrates Wine 11, is optimised for macOS 26 Tahoe, needs CrossOver ≥ 25.1.1 on Tahoe, and costs **$74/yr** ([IT之家](https://www.ithome.com/0/920/944.htm)). Another third-party runtime, now paid.
- **Apple's Game Porting Toolkit 3** is a developer *porting* tool (Wine-derived, Metal/DirectX-oriented) whose licence does not permit redistributing the translation layer. Irrelevant to a GDI app.
- **The killer isn't the GUI — it's USB.** Wine's USB support targets a narrow set of device classes; `adb` needs WinUSB bulk endpoints, and no Wine-on-macOS USB backend is known to expose an Android device to a Windows `adb.exe`. Even if it did, the user is now running a 1–2 GB compatibility layer to click ten buttons.
- **Preserves C core: nominally yes, practically no.**
- **Effort to falsify: 30 minutes.** Boot a CrossOver trial, run the `.exe`, check whether `adb devices` lists anything. Do this before spending a day on anything else Wine-related.

### (e) Cross-platform rewrite / Tauri / Electron
- **Full rewrite (Rust/Go/Python):** throws away the C core to re-express 826 lines, then still faces the toolkit question in (b). **No benefit.**
- **Tauri 2:** the only wrapper that respects the constraint — on macOS it uses the **system `WKWebView`**, giving a **5–10 MB** `.app` (vs Electron's **150–250 MB** bundled Chromium). HTML/CSS gives RTL free (`dir="rtl"`, logical properties) and WebKit's CoreText-backed fallback handles Arabic/Thai/CJK. **The C core is preservable via `extern "C"` FFI.** Cost: a Rust + npm toolchain, and process spawning moves into Rust. **A credible second choice**, but ~6× the bundle of AppKit for 10 buttons and a log. Effort ~600 LOC / 7–12 days.
- **Electron:** 150–250 MB contradicts the premise. **Reject.**

### (f) Drop the GUI — CLI / shell port — ✅ **SHIP THIS FIRST**
The repo already contains `deploy/get_root_now.sh` (162 lines), `root_bridge.sh` (110), `run_root.sh` (11), `trigger_root_daemon.cmd` (13). `get_root_now.sh` is **strictly more capable than the GUI's one-click flow** (which the README itself calls the "simplified version"): it waits for `sys.boot_completed`, waits for uptime > 150 s, detects reboot via uptime regression, distinguishes `ERR:` from timeout from device-offline, does 2 triggers per boot, and issues `adb reboot` between rounds. **All of it runs on macOS today** after changing `ADB=` (currently a WSL `/mnt/c/...` path, lines 5–6) to `/opt/homebrew/bin/adb` and making `SERIAL=` dynamic. **Preserves C core: n/a — there is no host C code involved; the Android payloads are untouched.**

---

## 3. The `run_process` problem

### 3.1 What the Win32 version actually does (116–231)
1. Creates an output pipe (131) and, if `input` is non-NULL, an input pipe (134); clears `HANDLE_FLAG_INHERIT` on the parent's ends (132, 135).
2. `CreateProcessW` with `STARTF_USESTDHANDLES`, `hStdOutput = hStdError = wr`, `hStdInput = ir`, `CREATE_NO_WINDOW` (138–152).
3. Closes the child's ends, then **writes the whole `input` to the child's stdin before reading a single byte of output** (157–165).
4. Polls with `PeekNamedPipe` + `ReadFile`, `WaitForSingleObject(pi.hProcess, 200)`, and a `GetTickCount` deadline; `TerminateProcess` on timeout (167–211).
5. `GetExitCodeProcess` (213), closes handles.

### 3.2 The POSIX/macOS equivalent — and the traps
- **`fork()` + `exec` is the wrong answer on macOS.** The darwin `fork(2)` man page states: *"There are limits to what you can do in the child process. To be totally safe you should restrict yourself to only executing async-signal safe operations until such time as one of the exec functions is called. All APIs, including global data symbols, in any framework or library should be assumed to be unsafe after a `fork()` … If you need to use these frameworks in the child process, you must `exec`."* In an AppKit process the child runs `pthread_atfork` handlers registered by the Network/CF/ObjC runtimes; these have caused reproducible **98 %-CPU hangs and `EXC_BAD_ACCESS` crashes on macOS Tahoe 26.5/26.6** ([Formal.ai, Aug 2026](https://www.formal.ai/blog/dont-use-fork-macos/)). Apple DTS's recommendation is explicit: use `posix_spawn`.
- **`posix_spawn` with `POSIX_SPAWN_CLOEXEC_DEFAULT`** (Darwin-only flag) closes every fd not explicitly named by a file action. This permanently fixes a latent Windows bug: `CreateProcessW(..., bInheritHandle=TRUE, ...)` without `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` leaks *every* inheritable handle in the process into the child. It also guarantees you see EOF on the read pipe even if `adb` forks a daemon.
- **Timeout:** no `TerminateProcess` equivalent. `kill(pid, SIGTERM)` → 500 ms grace polling `waitpid(WNOHANG)` → `kill(pid, SIGKILL)` → **unconditional `waitpid`**. `SIGKILL` on the adb *client* leaves the adb *server* daemon running — on both platforms, which is desirable (the next call reuses it) but means a timed-out `push` may still be in flight server-side.
- **Zombie reaping:** every spawn must be paired with a `waitpid` on every path, including the spawn-failure and early-error paths. **Do not** `signal(SIGCHLD, SIG_IGN)`: on macOS that auto-reaps children *and* makes `waitpid` fail with `ECHILD`, silently turning every exit code into garbage.
- **No stdout/stderr deadlock:** `poll()` on two fds. Crucially, **also fix the Win32 version's latent bug** — it writes all of stdin before reading any output (157–165), which deadlocks if the child fills the 64 KB pipe before consuming stdin. Today's inputs are ≤ 2.9 KB (`HELPER_SCRIPT`) so it never bites; the macOS port should interleave reads and writes in one loop.
- **SIGPIPE:** `write()` to a dead child's stdin raises `SIGPIPE` and kills the process (Windows returns `FALSE` from `WriteFile`). Set `signal(SIGPIPE, SIG_IGN)` once; Darwin's `fcntl(fd, F_SETNOSIGPIPE, 1)` is the per-fd alternative.
- **Cancellation:** there is no cancel today — `g_busy` only disables buttons, and `do_oneclick` can block for 6 × ~13 min. Add `static _Atomic int g_cancel;` checked inside the poll loop and inside an interruptible `zl_sleep_ms()` (replacing `Sleep(1000/2000/5000)` at 386/419/495).
- **UTF-8:** `run_process` is already byte-transparent — the pipe carries raw UTF-8 on both platforms and needs no conversion. The `MultiByteToWideChar` pair (69–87) exists only to hand bytes to Win32 `-W` APIs. On macOS those 20 lines and every `wchar_t` disappear.
- **Never use `system()` or `popen()`:** `system()` spawns `/bin/sh -c` and mangles `SIGCHLD`; `popen()` cannot feed stdin and read stdout simultaneously — exactly what `do_helper` (413–414) needs.

### 3.3 C sketch — macOS `run_process` with the same contract

```c
/* mac_process.c — POSIX replacement for zero_light_desktop.c:116-231.
 * Contract preserved: run_process(argv, input, &out, timeout_ms) -> exit status,
 * *out = malloc'd stdout+stderr (caller frees). Two deliberate changes:
 *   - argv[] instead of a command-line string (no shell, no quoting bugs)
 *   - posix_spawn instead of CreateProcess, stdin written from the same poll loop
 */
#include <spawn.h>      /* posix_spawn, POSIX_SPAWN_CLOEXEC_DEFAULT */
#include <poll.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <stdatomic.h>

extern char **environ;
#define ZL_EXIT_TIMEOUT 124                 /* coreutils timeout(1) convention */

static _Atomic int g_cancel;                /* set by the UI's Cancel button */

static void zl_proc_init(void) {            /* Darwin: a dead child's stdin
                                               raises SIGPIPE and kills us  */
    static int done = 0;
    if (!done) { signal(SIGPIPE, SIG_IGN); done = 1; }
}

static int64_t zl_now_ms(void) {            /* GetTickCount, without the 49.7d wrap */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void zl_sleep_ms(int ms) {           /* Sleep(), but cancellable */
    for (int i = 0; i < ms / 25 && !atomic_load(&g_cancel); i++) {
        struct timespec ts = { 0, 25 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
}

static int zl_append(char **buf, size_t *cap, size_t *len, const char *s, size_t n) {
    if (*len + n + 1 > *cap) {
        size_t nc = *cap ? *cap : 65536;
        while (nc < *len + n + 1) nc *= 2;
        char *nb = realloc(*buf, nc);
        if (!nb) return -1;
        *buf = nb; *cap = nc;
    }
    memcpy(*buf + *len, s, n); *len += n; return 0;
}

int run_process(char *const argv[], const char *input, char **out, int timeout_ms)
{
    int op[2] = {-1,-1}, ip[2] = {-1,-1};
    pid_t pid = -1;
    char *buf = NULL; size_t cap = 0, len = 0;
    int status = 0, rc, result = -1;

    zl_proc_init();
    if (out) *out = NULL;
    if (!argv || !argv[0]) return -1;
    if (pipe(op) != 0) return -1;                          /* CreatePipe, 131 */
    if (input && pipe(ip) != 0) goto cleanup;

    /* ---- CreateProcessW + STARTUPINFOW + SetHandleInformation, 132-152 ---- */
    posix_spawn_file_actions_t fa;
    posix_spawnattr_t at;
    posix_spawn_file_actions_init(&fa);
    posix_spawnattr_init(&at);
    posix_spawn_file_actions_adddup2(&fa, op[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fa, op[1], STDERR_FILENO); /* hStdError=wr */
    posix_spawn_file_actions_addclose(&fa, op[0]);
    if (input) {
        posix_spawn_file_actions_adddup2(&fa, ip[0], STDIN_FILENO);
        posix_spawn_file_actions_addclose(&fa, ip[1]);
    } else {
        posix_spawn_file_actions_addopen(&fa, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    }
    /* Darwin: everything not named above is closed in the child -- the safe
     * equivalent of a handle-inheritance whitelist, and it guarantees EOF. */
    posix_spawnattr_setflags(&at, POSIX_SPAWN_CLOEXEC_DEFAULT);

    rc = posix_spawn(&pid, argv[0], &fa, &at, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);

    close(op[1]); op[1] = -1;                  /* CloseHandle(wr), 154 */
    if (ip[0] >= 0) { close(ip[0]); ip[0] = -1; }
    if (rc != 0) { errno = rc; goto cleanup; } /* posix_spawn RETURNS errno; no child */

    {
        struct pollfd pfd[2];
        int nfd = 0, iout, iin = -1, out_open = 1, in_open;
        size_t ioff = 0, ilen = input ? strlen(input) : 0;
        int64_t deadline = (timeout_ms > 0) ? zl_now_ms() + timeout_ms : INT64_MAX;

        iout = nfd; pfd[iout].fd = op[0]; pfd[iout].events = POLLIN; nfd++;
        in_open = (ilen > 0);
        if (in_open) {
            fcntl(ip[1], F_SETFL, O_NONBLOCK);  /* never block on a full stdin pipe */
            iin = nfd; pfd[iin].fd = ip[1]; pfd[iin].events = POLLOUT; nfd++;
        }

        for (;;) {
            if (waitpid(pid, &status, WNOHANG) == pid) { pid = -1; break; } /* reaped */
            int64_t left = deadline - zl_now_ms();
            if (left <= 0 || atomic_load(&g_cancel)) { result = ZL_EXIT_TIMEOUT; break; }

            int to = left > 200 ? 200 : (int)left;
            int pr = poll(pfd, (nfds_t)nfd, to);
            if (pr < 0) { if (errno == EINTR) continue; break; }

            if (out_open && (pfd[iout].revents & (POLLIN|POLLHUP|POLLERR))) {
                char tmp[4096];
                ssize_t n = read(op[0], tmp, sizeof tmp);
                if (n > 0) { if (zl_append(&buf,&cap,&len,tmp,(size_t)n)) break; }
                else if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN)) {
                    close(op[0]); op[0] = -1; pfd[iout].fd = -1; out_open = 0;
                }
            }
            if (in_open && (pfd[iin].revents & (POLLOUT|POLLERR|POLLHUP))) {
                ssize_t n = write(ip[1], input + ioff, ilen - ioff);
                if (n > 0) ioff += (size_t)n;
                else if (n < 0 && errno != EINTR && errno != EAGAIN) n = 0;
                if (ioff >= ilen || n == 0) {   /* EOF the child's stdin */
                    close(ip[1]); ip[1] = -1; pfd[iin].fd = -1; in_open = 0;
                }
            }
        }
        if (in_open) { close(ip[1]); ip[1] = -1; }

        if (pid > 0) {                          /* TerminateProcess, 190 */
            if (result == ZL_EXIT_TIMEOUT) {
                kill(pid, SIGTERM);
                for (int i = 0; i < 50 && pid > 0; i++) {   /* 500 ms grace */
                    if (waitpid(pid, &status, WNOHANG) == pid) pid = -1;
                    else zl_sleep_ms(10);
                }
                if (pid > 0) { kill(pid, SIGKILL); waitpid(pid, &status, 0); pid = -1; }
                result = ZL_EXIT_TIMEOUT;
            } else {
                waitpid(pid, &status, 0);       /* ALWAYS reap -- no zombies */
                pid = -1;
            }
        }
        if (op[0] >= 0) {                       /* bounded drain: a grandchild may
                                                   still hold the write end */
            struct pollfd d = { op[0], POLLIN, 0 };
            char tmp[4096];
            while (poll(&d, 1, 200) > 0 && (d.revents & (POLLIN|POLLHUP))) {
                ssize_t n = read(op[0], tmp, sizeof tmp);
                if (n <= 0 || zl_append(&buf,&cap,&len,tmp,(size_t)n)) break;
                d.revents = 0;
            }
        }
    }
    if (result != ZL_EXIT_TIMEOUT)              /* GetExitCodeProcess, 213 */
        result = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);

cleanup:
    for (int i = 0; i < 2; i++) { if (op[i] >= 0) close(op[i]); if (ip[i] >= 0) close(ip[i]); }
    if (buf && out) { buf[len] = '\0'; *out = buf; } else free(buf);
    return result;
}

/* --- call-site change: build argv, not a quoted command string ---------------
 * Replaces build_adb_cmd() (234-243), which wraps arguments in literal '"'
 * characters and would break on a serial containing a quote.               */
static int adb_argv(char *argv[], int max, const char *adb, const char *serial,
                    const char *sub0, const char *sub1)
{
    int n = 0;
    if (n >= max) return -1;
    argv[n++] = (char *)adb;                       /* MUST be an absolute path */
    if (serial && serial[0]) {
        if (n + 2 >= max) return -1;
        argv[n++] = (char *)"-s"; argv[n++] = (char *)serial;
    }
    if (n + 2 >= max) return -1;
    argv[n++] = (char *)sub0;                      /* "shell", "push", "version" */
    if (sub1) argv[n++] = (char *)sub1;            /* NULL for one-word subcommands */
    argv[n] = NULL;
    return 0;
}
/* do_helper() then becomes: argv = {adb,"-s",serial,"shell","cat > /data/.../x"};
 * run_process(argv, HELPER_SCRIPT, &out, 30000);
 * The device-side shell still interprets 'cat >', because adb passes the single
 * argv element through to /system/bin/sh on the glasses -- semantics preserved. */
```

**Critical macOS gotcha:** a `.app` launched from Finder/Dock inherits **launchd's PATH** (`/usr/bin:/bin:/usr/sbin:/sbin`), *not* the user's shell PATH. The current default `g_adb = L"adb"` (line 62) will fail with `ENOENT` on essentially every Mac. `argv[0]` must be an **absolute path** resolved at startup.

---

## 4. Distribution & signing on macOS

**Toolchain:** `xcode-select --install` provides `clang` without Xcode.app (~1.5 GB). Build the bundle by hand (`Info.plist` + `clang` + `codesign`), not via `xcodebuild`.

**Mandatory on Apple Silicon:** every arm64 Mach-O needs at least an **ad-hoc signature** (`codesign --force --sign - MyApp.app`). `clang` ad-hoc-signs binaries it links, but hand-assembling a bundle invalidates that — you must re-sign after every modification.

**The full signed path requires a paid account.** Notarization needs a *Developer ID Application* certificate, which needs **Apple Developer Program membership at $99/yr**. Pipeline: `codesign --options runtime --timestamp --sign "Developer ID Application: …"` → `xcrun notarytool submit` → `xcrun stapler staple`. Hardened runtime is required for notarization but does **not** block `posix_spawn` of an unsigned helper (App Sandbox would — another reason to stay off the Mac App Store).

**Without a paid account (the realistic case):**
1. User downloads a `.zip`/`.dmg` → macOS sets `com.apple.quarantine`.
2. First launch is blocked by Gatekeeper.
3. **Since macOS 15 Sequoia the old "right-click → Open" bypass was removed.** The user must go to **System Settings → Privacy & Security → Security → "Open Anyway"**, authenticate, and confirm ([Apple support 102445, quoted](https://forum.makemkv.com/forum/viewtopic.php?style=11&f=4&t=36212); [Ars Technica](https://arstechnica.com/gadgets/2024/08/macos-15-sequoia-makes-you-jump-through-more-hoops-to-disable-gatekeeper-app-checks/)). After that the app is permanently excepted.
4. The workaround that still works and belongs in the README: **`xattr -dr com.apple.quarantine /Applications/ZeroLightRokid.app`**. (`spctl --add` is a no-op on modern macOS; do not tell users to run `spctl --master-disable`.)

**Comparison to Windows:** unsigned Windows distribution is *meaningfully easier* — SmartScreen shows "Windows protected your PC → More info → Run anyway" (one click), triggered by Mark-of-the-Web. macOS unsigned requires a System Settings trip, plus a `.app` bundle/dmg packaging step the Windows build never needed.

**Notarization risk specific to this app:** `assets/ghostlock_rv101` is a Linux aarch64 LPE exploit. Notary runs malware scanning; an app embedding an exploit payload may well be rejected. **Assume notarization fails and design for the unsigned path**, or ship the payload as a separate first-run download. Keep the ELF payloads in `Contents/Resources/` (not `Contents/MacOS/`) and do not `--deep`-sign them.

**Locating `adb` on macOS.** The current probe is `%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe` (779–783). macOS equivalents, in probe order:
1. the user's typed path, persisted in preferences;
2. `/opt/homebrew/bin/adb` — Apple Silicon Homebrew (the `android-platform-tools` **cask**, currently **37.0.1**, universal arm64+Intel, symlinked to `$HOMEBREW_PREFIX/bin/adb`) ([Homebrew cask](https://formulae.brew.sh/cask/android-platform-tools));
3. `/usr/local/bin/adb` — Intel Homebrew / manual install;
4. `~/Library/Android/sdk/platform-tools/adb` — Android Studio default on macOS;
5. `~/Android/Sdk/platform-tools/adb`;
6. `$ANDROID_HOME` / `$ANDROID_SDK_ROOT` + `/platform-tools/adb`;
7. manual download: `platform-tools-latest-darwin.zip` ([release notes](https://developer.android.com/tools/releases/platform-tools));
8. fallback: `NSOpenPanel` "Locate adb…" (a UX win that costs 10 lines and removes the need to type paths at all).

**USB device access: confirmed, macOS needs nothing.** There is no driver requirement (that's Windows) and **no `udev`-rules requirement** (that's Linux, where `/etc/udev/rules.d/51-android.rules` or an `android-udev` package is needed to grant `plugdev` access). macOS enumerates the device through IOKit directly, as an unprivileged user, with no reboot. The only setup is on the glasses: enable USB debugging and accept the RSA "Allow USB debugging?" prompt — the host key lives in `~/.android/adbkey`, so a user who previously authorised on a *different* Mac must re-authorise. (`~/.android/adb_usb.ini` is legacy and unnecessary for modern adb.)

---

## 5. Unicode / i18n implications (14 locales, incl. Arabic RTL)

**Fonts — the macOS story is much better than the Windows one.** `[NSFont systemFontOfSize:]` is not a single file; it is the head of a CoreText **cascade list** with automatic per-run script fallback: PingFang SC/TC for CJK, **Geeza Pro / SF Arabic** for Arabic, **Thonburi / Sukhumvit Set** for Thai, SF Pro / Helvetica Neue for Cyrillic (Ukrainian included). Apple documents SF Pro as covering **150+ languages across Latin, Greek and Cyrillic** and ships SF Arabic, SF Hebrew, SF Armenian and SF Georgian as companion script extensions ([Apple Fonts](https://developer.apple.com/fonts/)). **You do not need to bundle a Unicode font on macOS** — the Windows plan to ship one becomes unnecessary.

**Shaping — handled by CoreText, zero code.** Arabic needs contextual joining (isolated/initial/medial/final forms) and lam-alef ligatures; Thai needs mark positioning *and dictionary-based word segmentation for line breaking* (Thai has no inter-word spaces). `NSTextView`/`NSTextField`/`NSAttributedString` get all of this free from CoreText. **This is the hard disqualifier for FLTK, ImGui, SDL2 and Tk**, all of which render glyph-by-glyph with no shaping.

**RTL — automatic, but only if you build the layout right.** Apple's HIG: *"System-provided UI frameworks support right-to-left (RTL) by default, allowing system-provided UI components to flip automatically in the RTL context. If you use system-provided elements and standard layouts, you might not need to make any changes to your app's automatically reversed interface."* The platform-considerations section says *"No additional considerations for … macOS …"* ([HIG: Right to left](https://developer.apple.com/design/human-interface-guidelines/right-to-left)).

Concretely, for this port:
1. **Localization set drives direction.** macOS derives per-app RTL from `CFBundleLocalizations` / `.lproj` folders plus the user's preferred-language order. Ship an `ar.lproj` and Arabic users get `NSApp.userInterfaceLayoutDirection == RightToLeft` automatically; `NSAlert` button order, sliders, table columns and disclosure triangles mirror without code.
2. **Use Auto Layout with `leadingAnchor`/`trailingAnchor`, never `leftAnchor`/`rightAnchor`.** If you port the current hard-coded pixel grid (`x = 12; x + 92; x + 584; …`, 638–688) literally, **nothing mirrors** and you get an LTR frame containing Arabic text. Use `NSStackView` + leading/trailing constraints; this one decision buys RTL plus window resizing plus Retina — none of which exist today.
3. **Pin the direction of technical views.** The adb-path field, the serial field, the root-command field and the log view contain Latin paths and shell commands and must stay LTR even in an Arabic UI. Set `NSViewSemanticContentAttributeForceLeftToRight` on those views, or set `NSParagraphStyle.baseWritingDirection = NSWritingDirectionLeftToRight` on the log's `NSTextStorage` default attributes. Check `NSView.userInterfaceLayoutDirection` for the effective value ([Apple docs](https://developer.apple.com/documentation/appkit/nsview/userinterfacelayoutdirection)).
4. **Mixed-direction log lines** ("progress: exploit-attempt-1", "serial: …") should use `NSWritingDirectionNatural` so each line is laid out by its own first strong character; wrap embedded Latin tokens in LTR isolates (U+2066…U+2069) when a line starts in Arabic but contains a path.
5. **Do not flip** the emoji/dingbat button glyphs (🚀 ✓ ①②③, 662–671) — HIG explicitly says logos and universal marks keep their orientation. SF Symbols supplies RTL variants automatically for directional symbols.
6. **Numerals:** don't reverse digits within a number, but do reverse the *order* of numerals used to indicate progress or counting direction.
7. **Log font:** use `[NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular]` (SF Mono) for shell output; CoreText still cascades for CJK/Arabic/Thai, though fixed-width alignment degrades for double-width and cursive scripts — acceptable for a log.

**Localization mechanics:** `.lproj/Localizable.strings` + `NSLocalizedString`, or `[[NSBundle mainBundle] localizedStringForKey:value:table:]` for the C core. **~40–60 strings** to extract: ~30 UI labels plus ~25 log/status messages currently inline as `wchar_t` literals (376, 387, 398, 403, 410, 420, 426, 432, 436, 439, 446–447, 482–504, 512–513, 532, 563–564, 581, 586, 594, 603, 620, 707, 725, 812–817). Mechanical, but a real 1–2 days across 14 locales before translator review. Set `CFBundleDevelopmentRegion` to the fallback (`en`).

---

## 6. Recommendation, decision table, phased plan

| Option | Preserves C core | New runtime for the user | Bundle | New/rewritten LOC | Days | RTL & fonts | Risk | Verdict |
|---|---|---|---|---|---|---|---|---|
| **(a) ObjC + AppKit, programmatic** | **Yes** (~250 LOC as-is) | **none** | **~1.5 MB** | ~500 | **4–7** | Native, free | **Low** | ✅ **Recommended** |
| (a′) Swift + SwiftUI | Yes (C shim) | none (Swift in OS) | ~1.5 MB | ~400 | 4–8 | Native, free | Low–Med | ✅ Viable alt |
| (b) GTK4 (Homebrew) | Yes | Homebrew + ~80 dylibs | 40+ MB vendored | ~450 | 6–10 | Good (Pango/CoreText) | Med–High | ❌ |
| (b) Qt 6 | Yes | Qt runtime | ~60 MB | ~300 | 5–8 | Very good | Med (LGPL, size) | ⚠️ best toolkit, wrong trade |
| (b) Tcl/Tk | Partly | Tk; system Tk 8.5 deprecated | ~5 MB | ~350 | 5–9 | **No BiDi** | High | ❌ |
| (b) FLTK 1.4 | Yes (C++) | none (static) | ~2 MB | ~400 | 6–10 | **No shaping/BiDi** | High | ❌ |
| (b) Nuklear/ImGui + SDL2 | Yes | SDL2 | ~3 MB | ~700 | 10–15 | **None** | High | ❌ |
| (c) ObjC++ Cocoa, Win32-shaped | Yes | none | ~1.5 MB | ~600 | 5–9 | Native | Low–Med (Carbon: no) | ⚠️ works, shape is dead weight |
| (d) Wine / CrossOver | nominally | Wine/CrossOver 1–2 GB, $74/yr | n/a | ~0 | 1–2 (spike) | n/a | **Very High — adb USB almost certainly invisible** | ❌ |
| (e) Tauri 2 (Rust + WKWebView) | Yes via FFI | none (WKWebView in OS) | 5–10 MB | ~600 | 7–12 | Excellent | Med (new toolchain) | ⚠️ credible 2nd |
| (e) Electron | Yes via FFI | none | 150–250 MB | ~500 | 5–8 | Excellent | Med | ❌ |
| (e) Full rewrite (Rust/Go) | **No** | — | — | ~1200 | 15–25 | toolkit-dependent | Med | ❌ |
| **(f) CLI / `.command` + Bash** | n/a | **none** (bash + adb) | ~50 KB | **0 (already exists)** | **0.5** | Terminal-native | **Very Low** | ✅ **Ship today** |

### Phased plan
- **Phase 0 — 0.5 day.** Copy `deploy/{get_root_now,root_bridge,run_root}.sh` + the two assets into a folder; change `ADB=` to a probe list and make `SERIAL=` auto-detected; add a `RootRokid.command` wrapper (double-click → `osascript` menu of the 8 operations → run); document `xattr -dr com.apple.quarantine`. **This is a genuinely shippable macOS "port" for one afternoon.**
- **Phase 1 — 2–3 days.** Extract `zero_light_core.c` + a 15-line `zero_light_core.h`; implement the POSIX `run_process` from §3.3; add a headless `zerolight-cli` built from the same core. **Validate against real hardware now, before any GUI exists** — this is where all the risk lives (spawn/pipe/timeout, adb discovery, PATH, USB).
- **Phase 2 — 3–5 days.** AppKit `.app`: 10 buttons, 3 text fields, info pane, log `NSTextView`, `NSAlert` confirmation, worker thread → `dispatch_async` UI updates, Cancel button. Auto Layout with leading/trailing from day one.
- **Phase 3 — 2 days.** `Info.plist`, `.icns`, `codesign --force --sign -`, `.dmg`, README with the quarantine instructions. Optionally start the $99 enrolment for a Developer ID + notarization attempt (expect the embedded exploit payload to be a problem).
- **Phase 4 — 2–4 days.** 14 locales + the RTL pass (§5).

**Verdict: a native macOS port is viable, at ~10–15 working days for a complete localized app, or ~4 days for a minimum viable one.** This is cheap for a Win32 port because the app scores zero on every usual porting hazard: no GDI drawing, no owner-drawn controls, no `.rc` dialogs or menus, no registry, no timers, no DPI code, no COM, no shell integration, no resource compiler. What it *does* have — a pipe-based child-process layer, a worker thread, and a text log — maps onto `posix_spawn` + `dispatch_async` + `NSTextView` almost line for line. **The thing that is not viable is Wine** (the compatibility layer cannot see the glasses over USB, and it costs the user 1–2 GB). **The thing that is not worth it is a portable GUI toolkit** — the constraint is *self-contained*, and AppKit is the only toolkit that ships with macOS.

---

## 7. Reality check: the GUI vs. the orchestration

**Value decomposition of the 826-line source:**

| Layer | Lines | Share |
|---|---|---|
| Domain orchestration (`adb_available`…`do_refresh`, 369–616) | ~248 | 30% |
| Device-side payloads (`HELPER_SCRIPT`, `TRIGGER_SCRIPT`, 275–348) | ~74 | 9% |
| adb plumbing (`run_process`…`adb_logged`, 116–270) | ~155 | 19% |
| Win32 UI + `wWinMain` (631–826) | ~196 | 24% |
| Settings/encoding/log glue (67–113, 350–365) | ~63 | 8% |
| Defines, headers, layout constants | ~90 | 10% |

**The orchestration is the tool; the GUI is a delivery mechanism.** And every part of the orchestration already exists in Bash, in a *more* complete form: `get_root_now.sh` (162 lines) implements boot-completion waiting, uptime-stability gating, reboot detection via uptime regression, `ERR:` vs timeout vs offline discrimination, 2 triggers per boot and `adb reboot` between rounds — the GUI's `do_oneclick` (481–506) is the simplified version. The `HELPER_SCRIPT` constant (275–333) is a byte-for-byte transcription of the heredoc in `root_bridge.sh` lines 37–99, and `TRIGGER_SCRIPT` (336–348) is a transcription of `trigger_root_daemon.cmd`. `run_root.sh` (11 lines) does the "run an arbitrary root command" feature. **The entire functional content of the GUI is ~250–300 lines of Bash that already runs on macOS after a one-line `ADB=` change.** The payload binaries are Android aarch64 ELF — completely host-independent, zero work.

**What the GUI uniquely adds:** (1) no need to edit a script to set the adb path and serial — real value, since `get_root_now.sh` currently hard-codes `SERIAL="<DEVICE_SERIAL>"` and a WSL `/mnt/c/...` adb path and is unusable as-is on a Mac; (2) at-a-glance status/info; (3) no Terminal window; (4) a double-clickable artifact matching the Windows product.

**Does a shell script + small wrapper deliver 90% at 10%?**
- **For the realistic audience** — someone rooting AR glasses, who by definition has `adb` and probably Homebrew — **yes: ~90–95% of the functional value for ~2–5% of the cost.** A 4-hour `.command` port is shippable and testable today.
- **For a non-technical audience, no: closer to 70%.** A `.command` file is quarantined exactly like an `.app` (same `xattr` dance), still opens Terminal, and shows a wall of text instead of a status line. The "double-click, press ① ② ③" experience genuinely requires AppKit; ~120 lines of AppleScript (`osacompile -o ZeroLight.app`) can add `choose file` + `display dialog` and follows the system language for RTL — but `do shell script` gives you no live streaming log and needs `with timeout of 7200 seconds` for a 6-round, ~80-minute one-click run. That makes it a launcher, not a live console.
- **So: ship the `.command` this week, then decide.** It de-risks everything (adb discovery, PATH, USB, the retry loop) before a line of Objective-C exists, and it is a legitimate deliverable on its own if the native port never gets funded.
