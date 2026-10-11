# device_rescue/ — 從裝置搶救回來的二進位（2026-09-13）

## 為什麼會有這個目錄

`reports/missing_binaries_inventory_2026-09-13.md` 的盤點顯示：320 個被報告引用的二進位中，
**27 個只存在於眼鏡的 `/data/local/tmp`**，且其中 **10 個連原始碼都沒有**——
一旦 OTA、factory reset 或清空 `/data/local/tmp` 就會**永久消失**。

因此本次把「無原始碼」的 10 個 ＋ 2 個來歷不明的 payload 拉回工作區保存（共 4.6 MB）。

## 內容與來源

| 檔案 | 大小 | 原始碼在 repo？ | 說明 |
|------|-----:|------------------|------|
| `ashmem_blob_test` | 7,400 | ✗ | ashmem blob 測試（GhostLock 路徑相關） |
| `dsp_audit_blind` | 1,226,928 | ✗ | adsprpc/DSP 稽核（盲測） |
| `dsp_hidl_base_diag` | 10,776 | ✗ | DSP HIDL 診斷（base） |
| `dsp_hidl_session_diag` | 7,872 | ✗ | DSP HIDL 診斷（session） |
| `dsp_poc_standalone` | 1,288,720 | ✗ | DSP PoC（獨立版） |
| `dsp_raw_v2` | 8,160 | ✗ | DSP 原始通道測試 v2 |
| `kaslr_leak` | 8,336 | ✗ | KASLR 洩漏測試（早期版本；現行洩漏走 `printk_formats`） |
| `test_0582` | 2,145,128 | ✗ | CVE-2024-0582 測試（io_uring；本機不可觸達，見 FACTS §5 C3） |
| `test_21479` | 16,608 | ✗ | CVE-2025-21479（KGSL micronode）測試 |
| `test_36971` | 10,016 | ✗ | CVE-2024-36971 測試（本機不可觸發，見 FACTS §5 C2） |
| `fakepayload` | 4,096 | ✗ | **非執行檔：4096 bytes 全零頁**（sha256 `ad7facb2…92ca7` ＝ 4096 零位元組的標準雜湊）；未出現在任何報告，用途不可考 |
| `min_payload` | 24 | ✗ | **非執行檔：24 B `CrAU`（Android OTA `payload.bin`）標頭殘檔**（版本欄位為小端＝手工打造）；指向 update_engine/OTA 面，非核心 CVE |

- **來源**：`<DEVICE_SERIAL>:/data/local/tmp/<name>`（adb pull，2026-09-13）
- **對應清單**：`reports/missing_binaries_inventory_2026-09-13.md` §4.1／§4.3
- **未納入**：其餘 17 個「僅存裝置但原始碼仍在 `source_code/`」的二進位（可用 `deploy/build_probe_zig.sh`
  或 NDK 重建，不需佔版控體積）。

## 兩個非執行檔的**出處**（2026-09-13 追查）

| 檔案 | mtime（裝置端） | owner/mode | 出處判讀 |
|------|-----------------|-----------|----------|
| `fakepayload` | 2026-08-14 **17:28:13** | shell / 0666 | 全零頁＝「假 payload」測試輸入 |
| `min_payload` | 2026-08-14 **17:28:38** | shell / 0666 | 24 B `CrAU` 標頭殘檔＝「最小 payload」測試輸入 |

同一天（2026-08-14）的會話即 `reports/REVERSE_ANALYSIS_1.22_009.md`：**update_engine、`applyPayload`、payload 簽章鏈**與
`binaries/extracted_1.22/` 的 payload 提取。兩檔是**當日 payload gate 實驗留下的暫存輸入**，
工作區與裝置腳本皆無引用 → 保留僅為 provenance 證據，**無研究價值**（`fakepayload` 內容為零資訊）。

## 注意事項

1. 這些是**裝置端編譯產物**，與報告世代可能不符（多為 2026-05～08）；引用時請標註來源與日期。
2. **12 個中有 10 個是 aarch64 執行檔**（其餘 2 個見上表：全零頁與 OTA 標頭殘檔）。分類、sha256、`DT_NEEDED` 與 CVE／子系統對應見 `reports/static_re_gate_codes_and_rescue_2026-09-13.md`。
3. 依 `MANIFEST.md` 的分類原則，編譯產物一般不納版控；**本目錄是「不可重建者」的例外**，
   請勿在此放可從原始碼重建的檔案。
