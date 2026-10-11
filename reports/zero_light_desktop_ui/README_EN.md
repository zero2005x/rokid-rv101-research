# ZeroLightRokid Desktop — GUI Acceptance Screenshots (2026-09-11/12)

> 中文版：[README.md](README.md)

On-device operation screenshots of the Windows-native (Win32 C) LPE helper tool v2.0.0.
See [`../zero_light_desktop_test_report.md`](../zero_light_desktop_test_report.md) for the main report.

## Environment
- Host: Windows 11 build 26200, 2880×1620, **200% scaling (192 DPI)**
- Device: Rokid RG-glasses RV101 (serial `<DEVICE_SERIAL>`, fw `1.22.009-20260710-150201`, Android 12)
- All screenshots were captured DPI-aware (2126×1524 actual pixels)

## Screens

| File | Contents |
|---|---|
| `01_main_zh-TW.png` | Main screen (Traditional Chinese): adb path, serial, language dropdown, device information panel, 4×4 buttons, log area, progress bar, status bar |
| `02_arabic_rtl.png` | **Arabic RTL**: the whole interface is mirrored — the close button moves to the left, labels are right-aligned, buttons run right to left; Latin words such as `adb` / `exploit` / `root` are embedded correctly |
| `03_ja_th_ru_ko.png` | Japanese/Thai/Russian/Korean: the font fallback chain hits Yu Gothic UI / Leelawadee UI / Segoe UI / Malgun Gothic respectively; Thai mark positioning and Russian « » quotation marks are correct |
| `04_zh-CN_fr_de_uk.png` | Simplified Chinese/French/German/Ukrainian: Simplified Chinese uses mainland terminology (刷新／应用／序号), French « », German „ ", Ukrainian is genuine Ukrainian |
| `05_device_detected.png` | Device information panel measured on device: serial/model/firmware/SPL/Android/SELinux/identity/root/Shizuku/uptime/**firmware status: vulnerable build (escalatable)** |
| `06_rooted_after_oneclick.png` | After "🚀 一鍵取得 root" succeeds: the panel updates automatically to `root: Running` and `SELinux: Permissive`, and the status bar shows `root OK` |

## Acceptance Matrix

| Item | Result |
|---|---|
| Rendering across 14 locales (zh-TW / zh-CN / en / ja / ko / fr / de / es / pt / ru / uk / th / vi / ar) | ✅ all correct |
| Arabic RTL mirroring (`WS_EX_LAYOUTRTL`) | ✅ the whole interface is mirrored |
| Automatic font selection per locale | ✅ all 4 font chains hit |
| 14 consecutive interface rebuilds (switching language live) | ✅ no crash, no duplicated or lost log lines |
| 200% DPI layout (buttons 500×68 actual pixels) | ✅ all visible after fixing a 41 px client-area shortfall |
| Confirmation dialogs (silent capture / privacy LED / reboot / unknown firmware) | ✅ all pop up correctly and are usable |
| Status bar final state (no longer leaves "執行中…" behind) | ✅ correct after the fix |

## Requiring Manual Visual Confirmation
1. Whether the actual font size is comfortable at high DPI (the screenshots were taken in a 200% scaling environment)
2. How intuitive native Arabic speakers find the button order after RTL mirroring
3. Whether the Thai and Ukrainian wording matches local usage (this round only verified rendering, and that the mainland/Russian distinctions are correct)
