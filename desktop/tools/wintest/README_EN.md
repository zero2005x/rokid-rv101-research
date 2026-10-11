# wintest — Windows Test Helper for ZeroLightRokid Desktop

> 中文版：[README.md](README.md)

## Why a Standalone Console Program Instead of a PowerShell Script

Previously I used PowerShell to P/Invoke directly, and ran into four consecutive
**false signals** (each one led me to a wrong conclusion):

| # | Symptom | Cause |
|---|---|---|
| 1 | "Move the main window" moved the wrong window | `$p.MainWindowHandle` points at the second top-level window (the log window) once the process has one |
| 2 | `FindWindowW(cls, $null)` returned 0, looking like the window did not exist | PowerShell marshals `$null` as the empty string `""`, so FindWindow went looking for a window "whose title is the empty string" |
| 3 | `GetCapture()` was always NULL, so it was useless for judging clicks | MSDN: it only reports the capture window of the **calling thread** |
| 4 | `Add-Type` could not build the type at all | it compiles to an executable by default, so `public static IntPtr Main()` was treated as an entry-point candidate, and the warning was treated as an error |

After switching to a standalone console program, all Win32 calls live inside C#,
strings are C# `null`, and the output is plain stdout, so it can be run directly
from bash — completely avoiding the four classes of problem above.

## Compiling

```bash
CSC="/mnt/c/Windows/Microsoft.NET/Framework64/v4.0.30319/csc.exe"
"$CSC" /nologo /optimize+ /out:ZlTest.exe ZlTest.cs /r:System.Drawing.dll
```

Compiling once is enough; reuse it afterwards (about 11 KB).

## Commands

| Command | Description |
|---|---|
| `info` | window state (position, size, visible, minimized, whether the log window exists) |
| `logopen` | whether the log window has been created (YES / NO) |
| `place` | put the main window back in the center of the workspace (**it restores the window first**, since only then can a minimized window be measured at its correct size) |
| `front` | bring the main window to the foreground |
| `origin` | where the client-area origin is on the screen (used to convert screenshot coordinates into client-area coordinates) |
| `click X Y` | click at client-area coordinates in the main window |
| `esc` | send Esc |
| `children main\|log` | list child controls (class, visible, position, text) |
| `childclick ID` | send `BM_CLICK` to a child control of the log window (a real control, so there is no custom-drawn hit-testing problem) |
| `shot PATH` | capture the main window |

Exit codes: 0 success, 1 failure.

## Known Limitations

**`logtext` cannot read the Rich Edit content of the log window.**

Rich Edit text cannot be reliably retrieved from another process with `WM_GETTEXT`
(cross-process marshalling is only guaranteed for system messages below `WM_USER`,
and Rich Edit's text-retrieval path does not go through that). The Rich Edit in the
main window is readable only because the app provides a separate channel of its own.

**To verify the contents of the log window, use `shot` to take a screenshot, or ask
the user to confirm visually.**

## Pitfalls Hit Along the Way (for Reference in Later Changes)

- `Main` cannot be used as a method name (see #4 in the table above).
- `place` must call `ShowWindow(SW_RESTORE)` first: a minimized window measures as
  a virtual `314x50`, and a direct `SetWindowPos` will not restore it either.
- If the window is manually moved by the user during testing, half of the `shot`
  screenshot will be blank desktop — this is not a program problem; run `place`
  first and then take the screenshot.
