# 未納入本 repo 的檔案清單（EXCLUDED）

本檔記錄**刻意排除**於版本控制的檔案、排除理由，以及排除當下的 SHA-256。
目的：讓任何人日後都能確認「哪些東西不在這裡、為什麼」，並在需要時自行重建。

> 完整工作區仍保留在 `~/rokid_root/`（17 GB，含工具鏈、APK 標的、反編譯輸出、建置快取與影片工作區）。
> 未遮蔽的原始文字檔備份在 `~/rokid_root_unredacted_backup/`。

## 保留／排除的判準

| 判準 | 處置 | 例子 |
|---|---|---|
| 執行期必需品（工具會 push 到裝置上執行） | **保留** | `desktop/assets/ghostlock_rv101`、`desktop/assets/root_daemon`、`deploy/run_shell` |
| 裝置搶救、無原始碼可重建 | **保留** | `binaries/device_rescue/`（13 檔） |
| Gradle 建置引用的既存 native 輸入 | **保留** | `android/*/jniLibs/*/lib*.so`（3 檔） |
| 純開發期編譯產物（有原始碼） | 排除 | `source_code/` 的 15 個 ELF、`*.o` |
| 可由 kernel image 重新產生的符號傾印 | 排除 | `rv101_kallsyms*` |
| 協調式揭露機密附錄 | 排除 | `RV101_Disclosure/confidential/` |

排除檔案總數：**30**

## `RV101_Disclosure/confidential/` — 協調式揭露機密附錄

依 `HITCON_ZeroDay_RV101/SUBMISSION_GATE.md`「禁止送出的材料」清單排除（raw run logs、kernel offsets、exploit source）。原始檔仍在 `~/rokid_root/`。

共 9 檔。

| 檔案 | SHA-256 |
|---|---|
| `RV101_Disclosure/confidential/Confidential_Technical_Appendix.md` | `d1f3f29d4649f2919dba7de13b1ca6d2e559a5e97034d17e474780bb995f7380` |
| `RV101_Disclosure/confidential/detailed_reproduction/README.md` | `5d3873b5f460f7853d393c91ec535f19fcbb937c4f5bc1afd7ffe465e74e3d55` |
| `RV101_Disclosure/confidential/exploit_source/README.md` | `2f1b9ade351cf5f30abfcfb895f91c4a23e02ffd467e62c4838436daac4db979` |
| `RV101_Disclosure/confidential/post_exploit_state_05_REDACTED.txt.bak-20260926` | `dcc2f6eee67baa0d4f8d6d504e3fb584ff73e2bce21fcbeeceec6ace2ceaea16` |
| `RV101_Disclosure/confidential/post_exploit_state_REDACTED.txt.bak-20260926` | `dcc2f6eee67baa0d4f8d6d504e3fb584ff73e2bce21fcbeeceec6ace2ceaea16` |
| `RV101_Disclosure/confidential/raw_evidence/run_w2_clean_v959_134613.log` | `aa3b05468234559c2430016026fa6c467c231750d03f4a6aae6f7d932b4b931b` |
| `RV101_Disclosure/confidential/raw_evidence/run_w2_demo_v960_142523.log` | `67a945eb19af6ee0cb412c691cf1925b4cfb7e02e9073a7a206243ac9a981d26` |
| `RV101_Disclosure/confidential/raw_evidence/run_w2_v960_143215.log` | `1b1953ee07c5f45222900a0fe4773ec27e263aecfbe5aafe879d2f352c4ef89e` |
| `RV101_Disclosure/confidential/sensitive_offsets/README.md` | `8f3ebee1cf1ade33fe196c0f09318d4983bb963cd2133ba507e693a8025dc9b0` |

## 核心符號位址傾印 — 可由 kernel image 重新產生

UTF-16 完整 kallsyms 匯出（176,507 筆絕對位址），含 `commit_creds`、`prepare_kernel_cred`、`init_cred`、`selinux_state`、`init_task`。同時為 SUBMISSION_GATE 逐字禁止項；可由 `build-cache/ota_122009/` 的 kernel Image 重新產生，故不納入。

共 3 檔。

| 檔案 | SHA-256 |
|---|---|
| `research/ghostlock_refs/rv101_kallsyms.err` | `5f68499f5b9f53c890a02cd54470712406efc6da34618047efd2a39bc273096c` |
| `research/ghostlock_refs/rv101_kallsyms.txt` | `5f68499f5b9f53c890a02cd54470712406efc6da34618047efd2a39bc273096c` |
| `research/ghostlock_refs/rv101_kallsyms.txt.kallsyms` | `6eafa7385137a1d2e1840bdd7023fb53df77bfcbeb8a171defa852d7a247d950` |

## `source_code/` 開發期探針的編譯結果

每支皆確認存在同名 `.c`（例如 `test_bpf` ↔ `test_bpf.c`），屬開發期建置產物，因此只排除編譯結果，**原始碼全數保留**。

共 15 檔。

| 檔案 | SHA-256 |
|---|---|
| `source_code/kgsl_syncuaf_probe` | `2022107a340b9b2203baf5aae364f6ab25c01dfe657f0a81420c4cf1491386ce` |
| `source_code/test_adsp` | `a5e2c03a33fde0b0c57278bbf80752c3d7af270393f0d1e38a4b3a9ffc5c3af5` |
| `source_code/test_align` | `20fede4326955e8ab63d208566576ba9547c6c45e56748dd24936a7b16a83e1c` |
| `source_code/test_attach` | `c33737cc20791acf48d887061ac3b28c91b90425f5ae1952e67dee096c819d9e` |
| `source_code/test_bpf` | `a2f40ce7c887aa2bda93031368263e39de8e8581fcbb289337e7e2b2f8895706` |
| `source_code/test_bpf_prog` | `0a9c6a2f0573ab399628c7a70c650346d1d15ea2f5a187fdf34fe925fca2c7c3` |
| `source_code/test_bpf_subprog` | `311eff8350128a2d59b7f5aa9adc4977c3882c89dbc6732e14d7c3111fd665fb` |
| `source_code/test_bpf_valid` | `5a9e927bd4eaae35ad2aeadd2007a5991d028c6cdd4ce7ec301fa60b31468d97` |
| `source_code/test_cve41003` | `5387d596806681fddbb8037aa288ab80ef0384bf673885b6641a1e779a974473` |
| `source_code/test_gpumem` | `de62e9ab3ce5e62a2adf2d69018abe12a8f9ea227e05185cbf83e1d2c6ff6c0b` |
| `source_code/test_import` | `f32c23a81d0af92e78383a064cad92efac922d295bd6b96447ec467e12c055a3` |
| `source_code/test_jitfold` | `4819e65600940af772454c0abf6f8879a6074921b3b5373d041a7c7e77490db4` |
| `source_code/test_memuninit` | `c25b280cce6df54261c897e414be6414bf5809b643e491eed458b112daac9764` |
| `source_code/test_sync_errno` | `708089f144ad1265a41229cb5a4d226461d927906a59582514af3a221d297a90` |
| `source_code/test_tailcall` | `915cfafef83654644960fc332da8afcffcd66b5d31ab15df01d018c32057643c` |

## 編譯中間產物

`.o` 目的檔，可由 `Makefile` 重建。

共 3 檔。

| 檔案 | SHA-256 |
|---|---|
| `source_code/ghostlock_rv101/src/main.o` | `ea2ce46e898edd15bf63e867081aecc1f697c86db649a8b1fefdac1f22fbe47f` |
| `source_code/ghostlock_rv101/src/route.o` | `69f7f578b659b4bcfb9552d31e72bd931ee70b9005e41f9e0dafcdc5c8027d77` |
| `source_code/ghostlock_rv101/src/util.o` | `2daa8254b9fcf5e86cbd7d9df0508b1ff85010495c7f5d7551db2a0c84e5206c` |

## 目錄層級未納入項目

| 路徑 | 大小 | 理由 |
|---|---|---|
| `docs/video10/` | 7.5 GB | 影片製作工作區（MP4 成品與 `PRIVATE_DRAFT` 中間產物） |
| `tools/` | 450 MB | JDK／jadx 等工具鏈，可重新下載 |
| `apks/` | 1.1 GB | 自裝置拉出的第三方 vendor APK（Alipay、Rokid AssistServer 等） |
| `analysis/` | 807 MB | 封包、jadx 反編譯輸出、字串傾印 |
| `build-cache/` | 4.4 GB | zig 快取、OTA 分析工作區、截圖工作區 |
| `gradle_home/` `.venv/` `.zig-cache/` | 1.9 GB | 建置與 Python 環境 |
| `android/*/build/` | 116 MB | Gradle 建置產物 |
| `android/local.properties` | 30 B | 機器專屬 SDK 路徑（改附 `local.properties.example`） |
| `frida/frida-server` | 51 MB | 工具二進位，可重新下載 |
| `research/**/.git`（6 處） | — | 嵌套 git repo，已攤平為一般檔案 |
| `**/*.res` `*.obj` | — | 編譯中間產物 |

## 相關文件

- [`REDACTION_LOG.md`](REDACTION_LOG.md) — 個資與洩漏密鑰的遮蔽紀錄（before/after 雜湊）
- [`MANIFEST.md`](MANIFEST.md) — 原始工作區版面清單與分類原則
