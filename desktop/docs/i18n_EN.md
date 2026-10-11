# Multilingual Maintenance Guide

> 中文版：[i18n.md](i18n.md)

ZeroLightRokid Desktop supports **14 locales**, matching the locale list of the
phone-side app (`rokid_root/android/app-phone`); terminology also follows that
app's existing translations.

## 1. Data Flow

```
tools/i18n_master.json          ← single source of truth (14 locales × 241 strings)
        │
        │  tools/gen_i18n.py         validate + generate
        ▼
src/i18n_strings.h              ← auto-generated, do not edit by hand
        │
        │  #include
        ▼
src/i18n.h                      ← runtime: detection / lookup / font / RTL
        │
        ▼
src/zero_light_desktop.c        ← the UI fetches strings via T(STR_xxx) / Tf(STR_xxx, args)
```

**Never edit `src/i18n_strings.h` directly** — it is regenerated on every build.

## 2. Locale List and Order

The order matches the phone app's language selector (`LocaleHelper.LANGUAGES`):

| # | Code | Native name | Font | Win32 primary language ID |
|---|---|---|---|---|
| 0 | `zh-TW` | 繁體中文 | Microsoft JhengHei UI | 0x04 / sublanguage 0x01 |
| 1 | `zh-CN` | 简体中文 | Microsoft YaHei UI | 0x04 / sublanguage 0x02 |
| 2 | `en` | English | Segoe UI | 0x09 |
| 3 | `ja` | 日本語 | Yu Gothic UI | 0x11 |
| 4 | `ko` | 한국어 | Malgun Gothic | 0x12 |
| 5 | `fr` | Français | Segoe UI | 0x0C |
| 6 | `de` | Deutsch | Segoe UI | 0x07 |
| 7 | `es` | Español | Segoe UI | 0x0A |
| 8 | `pt` | Português | Segoe UI | 0x16 |
| 9 | `ru` | Русский | Segoe UI | 0x19 |
| 10 | `uk` | Українська | Segoe UI | 0x22 |
| 11 | `th` | ไทย | Leelawadee UI | 0x1E |
| 12 | `vi` | Tiếng Việt | Segoe UI | 0x2A |
| 13 | `ar` | العربية | Segoe UI | 0x01 |

`ar` is the only RTL locale; the UI automatically applies
`WS_EX_LAYOUTRTL | WS_EX_RTLREADING`.

## 3. String Format Conventions

| Form | Meaning |
|---|---|
| `%ls` | wide-string argument (`wchar_t *`) |
| `%d` / `%02d` / `%04d` | integer argument |
| `%%` | emit a literal percent sign |
| `\r\n` | line break inside a dialog |

- Always use **`%ls`**, never `%s` or `%hs`: MinGW's ANSI stdio and MSVC follow
  different conventions for `%s` inside a wide printf, whereas `%ls`
  unambiguously means a wide string in both.
- For the same key, the format specifiers must be exactly consistent across
  **all locales** (count, order, length modifiers). `gen_i18n.py` validates this,
  and the build fails on any inconsistency.
- A lone `%` (neither a format specifier nor `%%`) is rejected by the validator —
  at runtime it makes `vswprintf` read an argument that does not exist, which is a
  very dangerous bug.

## 4. Editing an Existing Translation

1. Edit the corresponding locale field of the corresponding key in
   `tools/i18n_master.json`.
2. Regenerate and validate:

```bash
python3 tools/gen_i18n.py
```

3. Rebuild.

## 5. Adding a Locale

1. Add an entry to the `locales` array in `tools/i18n_master.json`:

```json
{ "code": "it", "name": "Italiano", "english": "Italian",
  "font": "Segoe UI", "rtl": false, "win_primary": 16, "win_sublang": -1 }
```

   `win_primary` is the Win32 LANGID primary language ID (`LANGID & 0x3FF`); if
   the same language needs to be distinguished by region (as with Chinese), also
   supply `win_sublang` (`(LANGID >> 10) & 0x3F`).

2. Fill in all strings for that locale. You may fill in only `en` first, then run
   `python3 tools/i18n_apply.py` so that missing entries automatically fall back
   to English (ensuring the project always compiles), and replace them
   incrementally afterwards.

3. If a translator is to handle this, provide `tools/i18n_glossary.md` (the
   terminology baseline) and `tools/i18n_master.json`; the translation fragment
   format is:

```json
{ "it": { "BTN_APPLY": "Applica", "BTN_CANCEL": "Annulla" } }
```

4. Merge the fragment and validate:

```bash
python3 tools/i18n_apply.py it.json
python3 tools/gen_i18n.py
python3 tools/i18n_apply.py --status     # view completion per locale
```

5. If the new locale needs its own fallback font chain, add the corresponding
   `case` to `i18n_make_font()` in `src/i18n.h`.

## 6. Validation

### Generation stage (data correctness)

```bash
python3 tools/gen_i18n.py --check
```

It checks: every key × every locale is non-empty, format specifiers are
consistent across locales, there are no illegal control characters, locale codes
and LANGIDs are unique, and the **`en` baseline contains no non-ASCII** (to stop
data corruption such as "Chinese accidentally written into an English field" from
spreading to every locale that falls back to English).

### After the build (executable correctness)

```bash
python3 tools/verify_exe.py zero_light_desktop.exe
```

It takes bytes directly from the PE's `.text` / `.data` / `.rdata` and compares
each string against its UTF-16LE encoding, confirming that **all 3,374
translations really were compiled into the executable** (which catches
compile-time truncation, string merging and similar problems), and confirming
that Arabic contains no bidi control characters.

## 7. Known "Intentionally Identical" Strings

The following keys are intentionally identical to English in every locale (pure
commands / filenames / paths, where translation would actually be harmful):

- `CMD_DEFAULT` — `id; getenforce`
- `LOG_LASTDONE` — `--- last.done ---`
- `LOG_OTALOCK_SUSPEND` — `  pm suspend %ls`
- `LOG_START_ADB` — `[i] adb = %ls`

As a result, `i18n_apply.py --status` counts these as "to be translated", and the
completion rate will not show 100%. This is expected, not a gap.

## 8. Notes on Specific Locales

- **Arabic (`ar`)**: **do not** insert bidi control characters
  (U+200E/200F/202A–202E/2066–2069) into strings. Directionality is handled by
  `WS_EX_LAYOUTRTL`; embedded control characters actually corrupt the log output.
  `verify_exe.py` checks for this. In addition, because Latin characters (`root`,
  `pm suspend`, etc.) appear inside Arabic sentences, try to place the Latin words
  at the start or end of the sentence when translating; the layout is more stable
  that way.
- **French (`fr`)**: use a narrow no-break space (U+202F) before the colon, and
  `« »` for quotation marks. Exception: the `%02d:%02d:%02d` of `LOG_START_TIME`
  is a machine timestamp, so keep the half-width colons.
- **Thai (`th`)**: spaces are usually not inserted between words, only between
  clauses.
- **CJK**: `ja` uses `「」`, `zh-TW` uses full-width `（）：／`, and `ko` uses `( )`.
- Technical terms always keep their Latin spelling: `root`, `ghostlock`,
  `SELinux`, `Shizuku`, `exploit`, `adb`, `OTA`, `WiFi`, `Bluetooth`, `Android`,
  `APK`, `LED`, `AUTO`, `TCP`, `USB`, `uid=0`, `Permissive`, `Enforcing`, and all
  filenames / paths / commands.
