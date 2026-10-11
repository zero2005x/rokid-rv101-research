# 眼鏡媒體索引修復 — A/B 實驗樣本（2026-09-13）

> English: [README_EN.md](README_EN.md)

## 對應報告
`reports/wifi_transfer_fix_2026-09-13.md` Rev.3 §13–§16、Rev.5 §17–§18（FACTS.md §7 P29、P30）。

## 背景
眼鏡端 `media/index.json` 是**單一 JSON 全量覆寫**，且有兩個脆弱點：

1. `load()` 的解析迴圈**共用一個 try/catch** —— 任何一筆 `MediaItem.fromJson` 丟例外，
   外層一 catch，**其後所有項目都不載入**；接著任何一次 `persist()`（拍照／錄影／產縮圖都會觸發）
   就把這個變短的清單**永久寫回**。
2. `persist()` 用 `File.writeText()`（先 truncate 再寫），程序若在中間被殺會留下空或半截檔。

## 檔案

| 檔案 | 內容 |
|------|------|
| `index_repaired_9items.json` | 修正版 App 自我修復後的索引（9 筆，磁碟＝索引一致） |
| `index_before_delete_9items.json` | 刪除 3 支未收尾影片**之前**的索引快照（9 筆，含 636 MB／664 MB／111 MB 三筆） |
| `index_ab_corrupt_entry4.json` | **A/B 實驗用**：把上表第 4 筆的必填欄位 `fileName` 移除，其餘 8 筆完好 |
| `ab_victim_file.txt` | 被破壞的那一筆檔名（`VID_1789152641290.mp4`） |
| `deleted_media_ids.txt` | 依使用者指示刪除的 3 個 mediaId |

## A/B 結果（同一份損壞索引、同一台裝置）

| APK | log | 手機看到的清單 |
|-----|-----|---------------|
| 舊版 `1.0-safe`（code 3） | `E MediaRepository: index load failed` ＋ stack 被外層 catch 吞掉 | **total=4** ← 第 4 筆之後 **5 筆全部消失** |
| 新版 `1.0-safe.2`（code 4） | `E media entry #4 skipped: No value for fileName`<br>`W index reconciled: 1 file(s) re-registered from disk` | **total=9** ← 一筆沒少 |

## 修正內容（`android/app-glasses/…/media/MediaRepository.kt`）
1. 解析改**逐筆 try/catch**（單一壞項目只跳過自己）。
2. 新增 **`reconcileWithDisk()`**：載入後掃描各相簿目錄，把「在磁碟上但不在索引裡」的檔案重新登記
   —— 索引不再是唯一真相，清單可自我修復。
3. `persist()` 改**原子寫入**（先寫 `index.json.tmp` 再 `renameTo()`）。

## 實機驗證
```
23:07:20 W MediaRepository: index reconciled: 6 file(s) re-registered from disk
23:18:22 E media entry #4 skipped: No value for fileName
23:18:22 W index reconciled: 1 file(s) re-registered from disk
23:30:02 （刪除 3 支後重啟）無任何 reconcile 訊息 ⇒ 磁碟與索引一致
```

> ⚠ 本目錄同時是「**證據**」而非暫存：`index_ab_corrupt_entry4.json` 是刻意損壞的标本，
> 請勿當成可用的索引檔複製回裝置。
