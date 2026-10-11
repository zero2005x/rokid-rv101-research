# GitHub Publish Log (PUBLISH LOG)

> 中文版：[GITHUB_PUBLISH_LOG.md](GITHUB_PUBLISH_LOG.md)

Publication date: **2026-10-11**
Target repo: <https://github.com/zero2005x/rokid-rv101-research> (**private**)

---

## 1. Publication Summary

| Item | Value |
|---|---|
| Repo | `zero2005x/rokid-rv101-research` |
| Visibility | **PRIVATE** (`gh api` verified `private=true`) |
| Branch | `main` |
| Initial commit | `b552795` (single commit, containing 2,068 files and 422,871 inserted lines) |
| Files included | **2,068** |
| Workspace size | approx. 159 MB (excluding the actual LFS objects) |
| Git LFS | Tracks `*.apk` `*.mp4` `*.png` `*.jpg` `*.jpeg` `*.webp` `*.ogg`; **91 objects, 204 MB** actually uploaded |
| Source | `~/rokid_root` (17 GB) + `~/ZeroLightRokidDesktop` (4.2 MB) → consolidated into the `desktop/` subdirectory |

## 2. Acceptance Results (All Based on the Freshly Cloned Content)

```
git clone https://github.com/zero2005x/rokid-rv101-research.git
```

| Check | Result |
|---|---|
| Local HEAD matches remote HEAD | ✅ `b552795…042ae` identical |
| File count after clone | ✅ 2,068 (matches local) |
| LFS objects retrievable | ✅ `desktop/assets/ghostlock_rv101` is a real ELF (`7f 45 4c 46`), not a pointer file |
| `dist/*.apk` are real APKs | ✅ `Android package (APK)`, 7,050,002 bytes |
| `desktop/SHA256SUMS.txt` self-verification | ✅ 3/3 `OK` |
| Personal-data / credential residue scan (15 patterns) | ✅ **PASS — no residue whatsoever** |
| Redaction actually applied | ✅ 126 files carry placeholders such as `<DEVICE_SERIAL>` |
| Excluded items truly absent from the remote | ✅ `confidential/`, `rv101_kallsyms.txt`, `docs/video10/`, `source_code/test_bpf` all absent |
| `binaries/device_rescue/dsp_poc_standalone` | ✅ **deliberately retained** (device rescue, no source; see the criteria table in `EXCLUDED.md`) |
| No large files accidentally committed outside LFS | ✅ No file >50 MB exists as an ordinary blob |

## 3. Content Composition

| Directory | File count | Description |
|---|---|---|
| `android/` | 214 | Gradle multi-module (including 3 pre-existing `jniLibs/*.so`) |
| `reports/` | 346 | Research reports, on-device test evidence (including 148 files under `ghostlock_runlogs/`) |
| `research/` | 943 | GhostLock public references, Shizuku upstream source, kernel config |
| `source_code/` | 268 | 250 probe/exploit source files |
| `scripts/` | 78 | i18n workflow, hard-coded-value audit, OTA tools |
| `deploy/` | 48 | ADB deployment scripts, root daemon, native tools |
| `RV101_Disclosure/` | 45 | Coordinated-disclosure package |
| `HITCON_ZeroDay_RV101/` | 14 | HITCON ZeroDay submission package |
| `desktop/` | 44 | Win32 GUI tool (source, assets, docs, i18n tools) |
| `dist/` | 13 | Installable APKs (7 glasses versions, 3 phone versions, hider, probe) |
| `binaries/` `docs/` `frida/` | 36 | Rescue binaries, design documents, hook scripts |
| Root directory | 5 | `README.md`, `MANIFEST.md`, `EXCLUDED.md`, `REDACTION_LOG.md`, `README_ORGANIZED.md` |

## 4. Pre-Publication Processing (Per the User-Approved Plan)

1. **Excluded**: `docs/video10/` (7.5 GB), `tools/`, `apks/`, `analysis/`, `build-cache/`,
   `.venv/`, `gradle_home/`, `.zig-cache/`, `android/*/build/`,
   `RV101_Disclosure/confidential/`, `rv101_kallsyms*`, and the 15 ELF files and `*.o`
   under `source_code/`.
   For the complete list and SHA-256, see [`EXCLUDED.md`](EXCLUDED.md).
2. **Redaction**: personal data (real name, mailbox, device serial, phone serial, device
   alias, Windows account/host name) and leaked credentials (Rokid OTA API
   key/secret/deviceTypeId, Google Maps API key, Alipay payment AppKey/AppId/signV2).
   For the per-file before/after hashes, see
   [`REDACTION_LOG.md`](REDACTION_LOG.md).
3. **Code adjustments**: `desktop/src/zero_light_desktop.c` now explicitly skips the OTA
   check when the credential is a placeholder; the 14-locale string `LOG_OTACHECK_NOCRED`
   was added (`desktop/src/i18n_strings.h` has been regenerated).
4. **Recompile verification**: `desktop/build_mingw.sh` passed on this machine
   (PE32+, Windows GUI subsystem, 937,347 bytes), and `check_logfmt.py` reports that all
   147 multilingual format-call arguments match.
5. **Flattening nested git repos**: the 6 `.git` entries under `research/shizuku/` were
   removed and replaced with ordinary files.
6. **LFS**: because the user's environment has no sudo, `git-lfs` was installed at the
   user level in `~/.local/bin/git-lfs` (v3.6.1).

## 5. Post-Publication To-Do (Not Yet Done, Requires Your Decision)

| Item | Description |
|---|---|
| **GitHub Secret Scanning** | The repo is private, so GitHub will not raise an external alert; but the redacted credentials themselves are still valid, so Rokid should be asked to rotate the OTA API key and the leaked Google/Alipay credentials |
| **Backup location** | The unredacted originals are in `~/rokid_root_unredacted_backup/` (134 files, 2.3 MB). Please confirm how this directory is to be preserved; it is **not** inside the repo |
| **Original workspace** | Neither `~/rokid_root` (17 GB) nor `~/ZeroLightRokidDesktop` was modified, and both remain usable |
| **Vendor contact** | `degang.xu@rokid.com` is retained on the grounds of "business-contact role and impact on correspondence traceability"; see section 8 of `REDACTION_LOG.md`. Tell me if you want it redacted as well |
| **Peripheral MAC addresses** | MACs with D-Link/Gemtek/Xiaomi and similar OUIs are left unredacted; see section 7 of `REDACTION_LOG.md` |
| **Coordinated-disclosure timeline** | Public disclosure date **2026-11-23**; this repo is private and no external commitment has changed |
| **Remote synchronization** | After adding a commit locally, `git push` is required; the remote is already set to `origin` (HTTPS + `gh.exe` credential helper) |

## 6. How to Keep Using It

```bash
cd ~/rokid-research        # publication repo (clean, redacted)
git push                   # push new commits (credentials go through gh.exe)

cd ~/rokid_root            # original complete workspace (17 GB, untouched)
```

`gh.exe` is located at `/mnt/c/Program Files/GitHub CLI/gh.exe`.
The git credential helper on the WSL side is already configured in `~/.gitconfig`,
reusing the existing GitHub login from the Windows side (no extra token needed).

## Note: the GitHub account name in this document

The `zero2005x` occurrences in this file are **this repository's own GitHub account and
URL** (`https://github.com/zero2005x/rokid-rv101-research`). They are necessary release
identification, not personal data to redact. `REDACTION_LOG.md` redacts the separate case
where that handle was used as the local part of a **personal e-mail address**
(`zero2005x@gmail.com`). They are intentionally kept here; removing them would make the
publish record unverifiable.

---

## 7. Phase two: English editions (2026-10-11)

Commit `c236451`. Goal: give every first-party non-English item in this repo a readable
English counterpart.

### Documents (186 `_EN.md` / `_EN.txt`, stored beside the Chinese originals)

| Scope | Count |
|---|---|
| `reports/` | 122 |
| `docs/` | 17 |
| `HITCON_ZeroDay_RV101/` | 11 |
| Root / index files | 6 |
| `RV101_Disclosure/` | 2 |
| `desktop/`, `desktop/tools/` | 3 |
| `deploy/` | 2 |
| `android/branding/` | 1 |

This covers 216,561 Chinese characters. Naming follows the existing `NAME_EN.md`
convention, and each pair is linked in both directions just below its title
(**358 links, 0 broken, 0 one-way**). Third-party upstream sources under `research/`
(Shizuku, public exploit references) are excluded — they are not this project's work.

### Source comments (245 files, rewritten in place, no separate copies)

142 Android files, 66 under `source_code/`, and 80 across
`deploy/`, `scripts/`, `frida/` and `desktop/`.

### Deliberately not translated (functional data; translating it would break things)

| Item | Volume | Reason |
|---|---|---|
| Program output strings (`echo`/`log`/`printf`) | ~2,084 CJK runs | `reports/` quotes this output verbatim as evidence; translating it would desynchronise the evidence from the program |
| `<string>` values in the 14-locale `strings.xml` files | all | these *are* the translation resources |
| `desktop/src/i18n_strings.h` | 690 runs | generated by `tools/gen_i18n.py`; its content is the translation data |
| Chinese characters inside real filenames | — | renaming would invalidate existing references and hashes |

### Verification

| Check | Result |
|---|---|
| Shell syntax | 44/44 `bash -n` pass |
| Python | 18/18 `py_compile` pass; additionally proven character-identical via `tokenize`+`AST` |
| XML well-formedness | 41/41 |
| C compilation | `gcc`/MinGW object files **byte-identical** before vs after |
| Kotlin/Java | no `kotlinc` available; evidenced instead by "comment-stripped source byte-identical to HEAD" |
| Cross-links | 358 links, 0 broken, 0 one-way |
| Redaction regression | zero residue across 15 PII/credential patterns |

### Hash maintenance

- `REDACTION_LOG.md` / `_EN.md`: the "after" column now reflects current content
  (112 values moved, caused by the added cross-links and comment translation; the
  "before" column and the substitution table remain the original record)
- `SHA256SUMS.txt` in `RV101_Disclosure/`, `HITCON_ZeroDay_RV101/` and `desktop/` were
  recomputed and verified (59+59+35+3+3 entries, **0 stale**; 41 entries cover
  intentionally excluded files and keep their original values)
- The `zero2005x` occurrences in `GITHUB_PUBLISH_LOG.md` are this repo's own URL, not
  personal data; a note in the file records this

