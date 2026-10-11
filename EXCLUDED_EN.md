# Files Not Included in This Repo (EXCLUDED)

> 中文版：[EXCLUDED.md](EXCLUDED.md)

This file records the files **deliberately excluded** from version control, the
reason for each exclusion, and the SHA-256 at the moment of exclusion.
Purpose: to let anyone, at any later date, confirm "what is not here and why", and
to rebuild it themselves when needed.

> The full workspace is still kept at `~/rokid_root/` (17 GB, including the
> toolchain, APK targets, decompiler output, build caches and the video workspace).
> An unredacted backup of the original text files is at `~/rokid_root_unredacted_backup/`.

## Criteria for Retention / Exclusion

| Criterion | Disposition | Example |
|---|---|---|
| Runtime necessities (pushed to the device and executed by the tools) | **Retained** | `desktop/assets/ghostlock_rv101`, `desktop/assets/root_daemon`, `deploy/run_shell` |
| Device rescue, no source available to rebuild | **Retained** | `binaries/device_rescue/` (13 files) |
| Pre-existing native inputs referenced by the Gradle build | **Retained** | `android/*/jniLibs/*/lib*.so` (3 files) |
| Pure development-time compilation output (source available) | Excluded | the 15 ELF files under `source_code/`, `*.o` |
| Symbol dumps that can be regenerated from the kernel image | Excluded | `rv101_kallsyms*` |
| Coordinated-disclosure confidential appendix | Excluded | `RV101_Disclosure/confidential/` |

Total number of excluded files: **30**

## `RV101_Disclosure/confidential/` — Coordinated-Disclosure Confidential Appendix

Excluded under the "materials prohibited from submission" list in
`HITCON_ZeroDay_RV101/SUBMISSION_GATE.md` (raw run logs, kernel offsets, exploit
source). The original files are still in `~/rokid_root/`.

9 files in total.

| File | SHA-256 |
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

## Kernel Symbol Address Dump — Regenerable from the Kernel Image

A full UTF-16 kallsyms export (176,507 absolute addresses), containing
`commit_creds`, `prepare_kernel_cred`, `init_cred`, `selinux_state`, `init_task`. It
is also a verbatim prohibited item under SUBMISSION_GATE; it can be regenerated from
the kernel Image in `build-cache/ota_122009/`, so it is not included.

3 files in total.

| File | SHA-256 |
|---|---|
| `research/ghostlock_refs/rv101_kallsyms.err` | `5f68499f5b9f53c890a02cd54470712406efc6da34618047efd2a39bc273096c` |
| `research/ghostlock_refs/rv101_kallsyms.txt` | `5f68499f5b9f53c890a02cd54470712406efc6da34618047efd2a39bc273096c` |
| `research/ghostlock_refs/rv101_kallsyms.txt.kallsyms` | `6eafa7385137a1d2e1840bdd7023fb53df77bfcbeb8a171defa852d7a247d950` |

## Compilation Output of the `source_code/` Development Probes

Each one was confirmed to have a `.c` file of the same name (for example `test_bpf`
↔ `test_bpf.c`); these are development-time build artifacts, so only the compilation
output is excluded, and **all source code is retained**.

15 files in total.

| File | SHA-256 |
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

## Compilation Intermediates

`.o` object files, rebuildable from the `Makefile`.

3 files in total.

| File | SHA-256 |
|---|---|
| `source_code/ghostlock_rv101/src/main.o` | `ea2ce46e898edd15bf63e867081aecc1f697c86db649a8b1fefdac1f22fbe47f` |
| `source_code/ghostlock_rv101/src/route.o` | `69f7f578b659b4bcfb9552d31e72bd931ee70b9005e41f9e0dafcdc5c8027d77` |
| `source_code/ghostlock_rv101/src/util.o` | `2daa8254b9fcf5e86cbd7d9df0508b1ff85010495c7f5d7551db2a0c84e5206c` |

## Directory-Level Items Not Included

| Path | Size | Reason |
|---|---|---|
| `docs/video10/` | 7.5 GB | Video production workspace (MP4 outputs and `PRIVATE_DRAFT` intermediates) |
| `tools/` | 450 MB | Toolchain such as JDK/jadx; can be downloaded again |
| `apks/` | 1.1 GB | Third-party vendor APKs pulled from the device (Alipay, Rokid AssistServer, etc.) |
| `analysis/` | 807 MB | Packet captures, jadx decompiler output, string dumps |
| `build-cache/` | 4.4 GB | zig cache, OTA analysis workspace, screenshot workspace |
| `gradle_home/` `.venv/` `.zig-cache/` | 1.9 GB | Build and Python environments |
| `android/*/build/` | 116 MB | Gradle build artifacts |
| `android/local.properties` | 30 B | Machine-specific SDK path (replaced by `local.properties.example`) |
| `frida/frida-server` | 51 MB | Tool binary; can be downloaded again |
| `research/**/.git` (6 places) | — | Nested git repos, flattened into ordinary files |
| `**/*.res` `*.obj` | — | Compilation intermediates |

## Related Documents

- [`REDACTION_LOG.md`](REDACTION_LOG.md) — redaction log for personal data and leaked keys (before/after hashes)
- [`MANIFEST.md`](MANIFEST.md) — inventory of the original workspace layout and classification principles
