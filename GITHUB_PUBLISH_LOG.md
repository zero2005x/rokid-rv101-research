# GitHub 發布紀錄（PUBLISH LOG）

> English: [GITHUB_PUBLISH_LOG_EN.md](GITHUB_PUBLISH_LOG_EN.md)

發布日期：**2026-10-11**
目標 repo：<https://github.com/zero2005x/rokid-rv101-research>（**private**）

---

## 一、發布摘要

| 項目 | 值 |
|---|---|
| Repo | `zero2005x/rokid-rv101-research` |
| 可見性 | **PRIVATE**（`gh api` 驗證 `private=true`） |
| 分支 | `main` |
| 初始 commit | `b552795`（單一 commit，含 2,068 檔、422,871 行新增） |
| 納入檔案 | **2,068** |
| 工作區大小 | 約 159 MB（未計 LFS 實際物件） |
| Git LFS | 追蹤 `*.apk` `*.mp4` `*.png` `*.jpg` `*.jpeg` `*.webp` `*.ogg`；實際上傳 **91 個物件、204 MB** |
| 來源 | `~/rokid_root`（17 GB）+ `~/ZeroLightRokidDesktop`（4.2 MB）→ 整合為 `desktop/` 子目錄 |

## 二、驗收結果（全部以「重新 clone 的內容」為準）

```
git clone https://github.com/zero2005x/rokid-rv101-research.git
```

| 檢查項 | 結果 |
|---|---|
| 本地 HEAD 與 remote HEAD 一致 | ✅ `b552795…042ae` 相同 |
| Clone 後檔案數 | ✅ 2,068（與本地一致） |
| LFS 物件可正常取得 | ✅ `desktop/assets/ghostlock_rv101` 為真 ELF（`7f 45 4c 46`），非指標檔 |
| `dist/*.apk` 為真 APK | ✅ `Android package (APK)`，7,050,002 bytes |
| `desktop/SHA256SUMS.txt` 自我驗證 | ✅ 3/3 `OK` |
| 個資／憑證殘留掃描（15 種樣式） | ✅ **PASS — 無任何殘留** |
| 遮蔽確實套用 | ✅ 126 個檔案帶有 `<DEVICE_SERIAL>` 等佔位符 |
| 排除項確實不在遠端 | ✅ `confidential/`、`rv101_kallsyms.txt`、`docs/video10/`、`source_code/test_bpf` 皆 absent |
| `binaries/device_rescue/dsp_poc_standalone` | ✅ **刻意保留**（裝置搶救、無原始碼；見 `EXCLUDED.md` 判準表） |
| 大檔未經 LFS 誤入版控 | ✅ 無任何 >50 MB 檔案以一般 blob 形式存在 |

## 三、內容組成

| 目錄 | 檔案數 | 說明 |
|---|---|---|
| `android/` | 214 | Gradle 多模組（含 3 個既存 `jniLibs/*.so`） |
| `reports/` | 346 | 研究報告、實機測試證據（含 `ghostlock_runlogs/` 148 檔） |
| `research/` | 943 | GhostLock 公開參考、Shizuku 上游源碼、kernel config |
| `source_code/` | 268 | 250 支探測／exploit 原始碼 |
| `scripts/` | 78 | i18n 工作流、硬編碼稽核、OTA 工具 |
| `deploy/` | 48 | ADB 佈署腳本、root daemon、native 工具 |
| `RV101_Disclosure/` | 45 | 協調式揭露套件 |
| `HITCON_ZeroDay_RV101/` | 14 | HITCON ZeroDay 投稿套件 |
| `desktop/` | 44 | Win32 GUI 工具（原始碼、assets、docs、i18n 工具） |
| `dist/` | 13 | 可安裝 APK（眼鏡 7 版、手機 3 版、hider、probe） |
| `binaries/` `docs/` `frida/` | 36 | 搶救二進位、設計文件、hook 腳本 |
| 根目錄 | 5 | `README.md`、`MANIFEST.md`、`EXCLUDED.md`、`REDACTION_LOG.md`、`README_ORGANIZED.md` |

## 四、發布前的處理（依使用者核可的計畫）

1. **排除**：`docs/video10/`（7.5 GB）、`tools/`、`apks/`、`analysis/`、`build-cache/`、
   `.venv/`、`gradle_home/`、`.zig-cache/`、`android/*/build/`、
   `RV101_Disclosure/confidential/`、`rv101_kallsyms*`、`source_code/` 的 15 個 ELF 與 `*.o`。
   完整清單與 SHA-256 見 [`EXCLUDED.md`](EXCLUDED.md)。
2. **遮蔽**：個資（真名、信箱、裝置序號、手機序號、裝置別名、Windows 帳號／主機名）
   與洩漏憑證（Rokid OTA API key/secret/deviceTypeId、Google Maps API key、
   Alipay 付款 AppKey/AppId/signV2）。逐檔 before/after 雜湊見
   [`REDACTION_LOG.md`](REDACTION_LOG.md)。
3. **程式碼調整**：`desktop/src/zero_light_desktop.c` 在憑證為佔位符時明確跳過 OTA 檢查；
   新增 14 語系字串 `LOG_OTACHECK_NOCRED`（`desktop/src/i18n_strings.h` 已重新產生）。
4. **重新編譯驗證**：`desktop/build_mingw.sh` 於本機通過
   （PE32+、Windows GUI 子系統、937,347 bytes），`check_logfmt.py` 回報 147 個
   多語系格式化呼叫引數全部相符。
5. **嵌套 git repo 攤平**：`research/shizuku/` 下 6 處 `.git` 已移除，改為一般檔案。
6. **LFS**：因使用者環境無 sudo，`git-lfs` 以使用者層級安裝於 `~/.local/bin/git-lfs`（v3.6.1）。

## 五、發布後待辦（尚未執行，需你決定）

| 項目 | 說明 |
|---|---|
| **GitHub Secret Scanning** | Repo 為 private，GitHub 不會對外告警；但已遮蔽的憑證本身仍有效，建議請 Rokid 輪替 OTA API key 與外洩的 Google／Alipay 憑證 |
| **備份位置** | 未遮蔽原檔在 `~/rokid_root_unredacted_backup/`（134 檔，2.3 MB）。請確認此目錄的保存方式；它**不在** repo 內 |
| **原始工作區** | `~/rokid_root`（17 GB）與 `~/ZeroLightRokidDesktop` 皆未被修改，仍可繼續使用 |
| **廠商聯絡人** | `degang.xu@rokid.com` 依「公務聯絡身分且影響往來可追溯性」保留，詳見 `REDACTION_LOG.md` 第八節。若要一併遮蔽請告知 |
| **周邊 MAC 位址** | D-Link／Gemtek／Xiaomi 等 OUI 之 MAC 保留未遮蔽，見 `REDACTION_LOG.md` 第七節 |
| **協調揭露時程** | 公開日 **2026-11-23**；本 repo 為 private，未改變任何對外承諾 |
| **遠端同步** | 本地新增 commit 後需 `git push`；remote 已設為 `origin`（HTTPS + `gh.exe` credential helper） |

## 六、如何繼續使用

```bash
cd ~/rokid-research        # 發布用 repo（乾淨、已遮蔽）
git push                   # 推送新 commit（憑證走 gh.exe）

cd ~/rokid_root            # 原始完整工作區（17 GB，未動）
```

`gh.exe` 位於 `/mnt/c/Program Files/GitHub CLI/gh.exe`，
WSL 端的 git credential helper 已設定於 `~/.gitconfig`，
沿用 Windows 端既有的 GitHub 登入（不需額外 token）。

## 附註：本檔中的 GitHub 帳號名稱

本檔出現的 `zero2005x` 是**本 repo 自身的 GitHub 帳號與 URL**
（`https://github.com/zero2005x/rokid-rv101-research`），屬於必要的發布識別資訊，
不是需要遮蔽的個資。`REDACTION_LOG.md` 中遮蔽的是該帳號被當作**個人信箱前綴**使用的
情形（`zero2005x@gmail.com`），兩者不同。此處刻意保留，否則讀者無法依本檔驗證發布結果。

