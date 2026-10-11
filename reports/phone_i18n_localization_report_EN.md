# Phone-Side Multilingual (i18n) Full Localization and Language-Selection Button — Implementation and Verification Report

> 中文版：[phone_i18n_localization_report.md](phone_i18n_localization_report.md)

- Date: 2026-09-11
- Scope: `android/app-phone` (phone-side App) + `scripts/i18n/` (translation workflow)
- Requirement addressed: a built-in "language selection" button in the phone-side App; the interface and operation log no longer contain leftover Chinese

## 1. Conclusion (TL;DR)

| Item | Result |
|------|------|
| In-App language-selection button | ✅ `btnLanguage` (**from 2026-09-13 its location changed to the "Settings → Language" card**; the remote-control tab keeps only capture-related items); opens a single-choice dialog with 14 locales + "Follow system" |
| Switch takes effect immediately | ✅ Selection writes to SharedPreferences → `requireActivity().recreate()`, no App restart needed |
| Preserved across restart | ✅ `shared_prefs/app_locale.xml` → `LocaleHelper.apply()` (applied when the App starts) |
| Full localization | ✅ Inventoried 86 hard-coded Chinese strings (10 are developer messages / native language names, deliberately kept); the rest were organized into 68 string resources and completed across 14 locales (68 × 14 = 952 translations)<br>**2026-09-13 status**: the pipeline `scripts/i18n/phone_keys.tsv` has accumulated **193 keys** (+ 91 keys migrated earlier), all 14 locales at 100% coverage (284 entries each in `values-*/strings.xml`) |
| Build / Lint | ✅ `:app-phone:assembleDebug` and `:app-phone:lintDebug` both pass (0 error) |
| On-device validation | ✅ en / ja / ar (RTL) / th switching, persistence, broadcast regression, and operation-log localization all validated on the real device |

## 2. Language-selection button

- Location: **"Settings" tab → Language card** (from 2026-09-13; the remote-control tab no longer has an "Advanced" section).
- Label: `Language: <current language>` (`R.string.btn_language` is a format string carrying `%1$s`, and each locale brings its own separator:
  `Language: %1$s`, `Langue : %1$s`, `言語：%1$s`, `اللغة: %1$s`…).
- Dialog: `AlertDialog.setSingleChoiceItems`, with the list being "Follow system" + the 14 **native names**
  (`LocaleHelper.LANGUAGES`: 繁體中文／简体中文／English／日本語／한국어／Français／Deutsch／
  Español／Português／Русский／Українська／ไทย／Tiếng Việt／العربية), with the current language preselected.
- Selection behavior: `LocaleHelper.set(ctx, tag)` → writes `app_locale` → `AppCompatDelegate.setApplicationLocales`
  → `recreate()`; `tag = null` (follow system) removes that key.
- Debug broadcast (pre-existing, unchanged): `am broadcast -n com.rokid.rc.phone/.DebugReceiver -a com.rokid.rc.phone.DEBUG --es cmd SET_LOCALE --es lang <tag>`
  (without `--es lang` = restore follow-system).

## 3. Full localization: scope and method

### 3.1 Scope (86 strings → 68 keys)

86 **non-comment** Chinese strings were inventoried, distributed over 10 Kotlin files:

| Category | File | Content |
|------|------|------|
| Operation log (the largest group) | `ui/RemoteFragment.kt` | photo / video / root / ghostlock / LED / silent / WiFi / display settings / progress / language switching |
| Toast / dialog | `MainActivity.kt`, `MediaActivity.kt`, `ViewerActivity.kt`, `ui/AlbumsFragment.kt`, `ui/LocalFragment.kt` | Bluetooth off, not paired, connection failed, batch delete/download progress and results, local album delete |
| Status summary | `ui/RemoteFragment.kt` | `相簿 N · 媒體 N · root ✓ · ghostlock 開/關` |
| Media type label | `util/Format.kt` | video / photo (changed to `typeLabelRes()` returning a string-resource id) |
| WiFi error messages | `Remote.kt` | see §3.3 (changed to typed exceptions; the cause is no longer determined by string comparison) |

**Deliberately kept in Chinese** (not UI, or translation would be harmful):

| Location | Reason |
|------|------|
| Native names in `LocaleHelper.LANGUAGES` | The language menu must display each language **in that language itself** (`日本語`, `العربية`…); they must not be translated |
| `Log.i(...)` in `DebugReceiver.kt` and `TEST_LOG` test messages | logcat / test-tool messages used only by developers (`TEST_LOG` is for injecting test data) |

### 3.2 Workflow (reproducible)

```bash
# 1) String table (key / zh-TW / en) = single source of truth
scripts/i18n/phone_keys.tsv
> ⚠ 2026-09-13 addendum: keys added later (ring/ghostlock/root/appearance, etc.) were translated by subagents in batches and then
> **merged into `scripts/i18n/out/*.xml`**; the `out/` directory keeps only the three merged files (`ja_ko_zhCN_vi.xml`,
> `fr_de_es_pt.xml`, `ru_uk_th_ar.xml`); the temporary partial files are deleted after merging.

# 2) Produce the translations for the other 12 locales (@@values-xx blocks; this directory preserves the translation sources)
scripts/i18n/out/{fr_de_es_pt,ja_ko_zhCN_vi,ru_uk_th_ar}.xml
# 3) Terminology normalization (align with each locale's existing conventional wording; idempotent) + quality checks
python3 scripts/i18n/normalize_locale_terms.py
python3 scripts/i18n/validate_translations.py
# 4) Merge into the 14 strings.xml files (repeatable; includes XML escaping of & ' < >)
python3 scripts/i18n/apply_phone_strings.py
# 5) Call-site replacement (one-off, already executed; each old string must match the specified number of times exactly before it acts)
python3 scripts/i18n/migrate_phone_call_sites.py
```

The translations were produced in parallel by 3 subagents (4 locales each), and after production were checked by script:
key coverage and ordering, `%1$s/%2$s/%3$s/%1$d/%2$d` placeholder-by-placeholder comparison, `\n` preservation,
`✓ ✗ ⚠ ●` prefix preservation, technical terms (WiFi/Bluetooth/root/ghostlock/LED/TCP) kept in Latin letters, no unescaped `& < >`.
After being merged into `values*/strings.xml`, those files are the **final source of truth** (the TSV / out files are the translation history).

### 3.3 Incidental refactor: typed WiFi failure causes

Originally `RemoteFragment` determined the failure cause by **comparing exception message strings** (`raw.contains("請手動加入")`),
and after localization the message changes with the locale and the check stops working; this was changed to `WifiSetupException(code)`:

| code | Display string |
|------|----------|
| `AP_TIMEOUT` | `wifi_ap_timeout` (glasses WiFi hotspot startup timed out…) |
| `AP_NOT_READY` | `wifi_ap_not_ready` (pre-existing) |
| `AP_JOIN_MANUAL` | `wifi_failed_manual` (pre-existing; carries SSID/password) |
| `TCP` | `wifi_tcp_failed` (TCP connection failed: …) |

The exception's own `message` was changed to an English debug string (for logcat only) and is no longer shown to the user directly.

## 4. Verification evidence (real device: phone `<PHONE_SERIAL>`)

| Verification | Method | Result |
|------|------|------|
| Button exists | `uiautomator dump` | `text="語言：跟隨系統"` |
| Dialog content | dump | Title + "Follow system" + 14 native names + Cancel (scrollable) |
| Switch to English | Tap list item | Interface immediately all English, `app_locale.xml → lang=en` |
| Preserved across restart | `am force-stop` → restart | Still `Albums 1 · Media 4 · root ✓ · ghostlock off` |
| Japanese | Tap list item | `アルバム 1 · メディア 5 · root ✓ · ghostlock オフ`, `言語：日本語` |
| Arabic (RTL) | Tap list item + `screencap` | Layout mirrored, `الألبومات ١ · الوسائط ٥ · ghostlock معطّل` (Arabic numerals) |
| Thai | Broadcast SET_LOCALE | `อัลบั้ม 1 · สื่อ 5 · root ✓ · ghostlock ปิด` |
| Operation-log localization | Dump after taking a photo / toggling the LED | en: `Taking photo… / ✓ Photo captured: IMG_…jpg`; ar: `مؤشر أخضر مضيء… / ✓ مؤشر أخضر: مضيء` |
| Broadcast regression | `SET_LOCALE --es lang th` | Writes `lang=th` and takes effect after restart |
| Restore default | `SET_LOCALE` (without lang) | `app_locale.xml` is an empty map, interface follows the system (zh-TW) |
| Lint | `:app-phone:lintDebug` | 0 error (fixed 14 `StringFormatMatches` along the way: `ap.port` is `Int` → `%2$d`) |

Screenshot: `reports/media/verify_ar_rtl.png` (Arabic RTL screen on the real device).

## 5. Source-table review and terminology normalization (second round, 2026-09-11)

After the first version shipped, `scripts/i18n/phone_keys.tsv` (the zh-TW/en source) was reviewed again and compared against
each locale's **existing** `strings.xml`; 3 classes of problems and 150 strings were corrected:

### 5.1 zh-TW/en source corrections (18 places)

| Category | Example |
|------|------|
| Traditional-Chinese half-width punctuation | `連線失敗: %1$s` → `連線失敗：%1$s`; `快門/錄影` → `快門／錄影` |
| Traditional-Chinese word order | `媒體回退藍牙` → `媒體改走藍牙`; LED log `白燈 亮…` → `白燈亮…` (`log_led_switch` removes a redundant space) |
| English feel / alignment with existing style | `Delete finished: %1$d succeeded` → `Deleted %1$d item(s)`; `Photo failed` → `Photo capture failed`;
`privacy LED` → `privacy indicator` (aligned with the existing `sec_led` = Privacy indicator); quotes changed to the `“%1$s”` used by existing files |

### 5.2 Terminology consistency across locales (132 strings, adding `scripts/i18n/normalize_locale_terms.py`)

The new strings originally all wrote `WiFi`/`Bluetooth`/`LED`, but each locale's existing files had long since chosen conventional terms, and mixing them within the same file would be inconsistent:

| Locale | WiFi | Bluetooth | Other |
|------|------|-----------|------|
| ja | `Wi-Fi` | Bluetooth | `グラス` → **`メガネ`** (aligned with `conn_card_title`) |
| ko | `Wi-Fi` | **`블루투스`** | — |
| de | **`WLAN`** | Bluetooth | — |
| fr / pt / ru / uk / vi / th | `Wi-Fi` | Bluetooth | th additionally fixed the spaces between Thai and loanwords |
| zh-CN | WiFi | **`蓝牙`** | — |
| ar | **`واي فاي`** | **`بلوتوث`** | — |
| zh-TW / en / es | WiFi | Bluetooth | — |

The 5 privacy-indicator keys (`log_led_enabling/disabling/on_ok/off_ok/fail`) and `led_name` were rewritten per locale
according to each locale's existing `sec_led`/`led_switch_label` wording (e.g. ja `プライバシーインジケーター`/`白ランプ`,
de `Datenschutz-Anzeige`/`Weiß-LED`, fr `voyant de confidentialité`, ar `مؤشر الخصوصية`).
The uk silent wording was aligned with the existing `silent_switch_label` (`Тиха зйомка`).

### 5.3 New quality-control scripts (repeatable)

```bash
python3 scripts/i18n/validate_translations.py       # TSV/XML structure, coverage, placeholders, escaping, banned terminology
python3 scripts/i18n/normalize_locale_terms.py      # terminology normalization (idempotent)
python3 scripts/i18n/apply_phone_strings.py         # merge into the 14 strings.xml files
```

`validate_translations.py` checks: 68-key coverage (**first-round figure**; currently 193 keys) and ordering, `%n$s/%n$d` placeholders matching the en column one by one,
leading `\n` and `✓ ✗ ⚠ ●` preservation, no unescaped `&`, and no banned wording in any locale (e.g. ja must not contain `グラス`).
After the review, `:app-phone:assembleDebug` + `lintDebug` still reported 0 error, and spot checks on the real device for
zh-TW (`白燈亮…／✓ 白燈：亮`), en (`Turning privacy indicator off…／✓ Indicator: off`),
ja (`白ランプ 点灯…／✓ 白ランプ：点灯`), de (`Weiß-LED an…／✓ Weiß-LED: an`) were all correct.

## 6. Known limitations

1. The logcat output of `DebugReceiver` and the `TEST_LOG` test messages are still in Chinese (developer tools, deliberately untranslated).
2. The 14-locale translations were produced by AI and verified by script for **structure** (placeholders/coverage/escaping);
   they have not been proofread by native speakers. If they are to be published to a store, having native speakers review `scripts/i18n/phone_keys.tsv` is recommended.
3. The language is a **per-app** setting (it does not affect the system or other Apps); `values/` is the Traditional-Chinese default,
   so a system language outside the 14 locales falls back to Traditional Chinese.
