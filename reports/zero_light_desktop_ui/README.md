# ZeroLightRokid Desktop — GUI 驗收截圖（2026-09-11/12）

Windows 原生（Win32 C）LPE 輔助工具 v2.0.0 的實機操作截圖。
主報告見 [`../zero_light_desktop_test_report.md`](../zero_light_desktop_test_report.md)。

## 環境
- 主機：Windows 11 build 26200，2880×1620，**200% 縮放（192 DPI）**
- 裝置：Rokid RG-glasses RV101（serial `<DEVICE_SERIAL>`，fw `1.22.009-20260710-150201`，Android 12）
- 截圖皆為 DPI-aware 擷取（2126×1524 實際像素）

## 畫面

| 檔案 | 內容 |
|---|---|
| `01_main_zh-TW.png` | 主畫面（繁體中文）：adb 路徑、序號、語言下拉、裝置資訊面板、4×4 按鈕、紀錄區、進度列、狀態列 |
| `02_arabic_rtl.png` | **阿拉伯文 RTL**：整個介面鏡像 —— 關閉鈕移至左側、標籤右對齊、按鈕由右至左；`adb` / `exploit` / `root` 等拉丁詞正確嵌入 |
| `03_ja_th_ru_ko.png` | 日文／泰文／俄文／韓文：字型後備鏈各命中 Yu Gothic UI / Leelawadee UI / Segoe UI / Malgun Gothic；泰文標記定位、俄文 « » 引號正確 |
| `04_zh-CN_fr_de_uk.png` | 簡中／法文／德文／烏克蘭文：簡中採大陸用語（刷新／应用／序号）、法文 « »、德文 „ "、烏克蘭文為真烏克蘭文 |
| `05_device_detected.png` | 裝置資訊面板實測：序號／型號／韌體／SPL／Android／SELinux／身分／root／Shizuku／開機時間／**韌體狀態：漏洞版（可提權）** |
| `06_rooted_after_oneclick.png` | 「🚀 一鍵取得 root」成功後：面板自動更新為 `root: Running`、`SELinux: Permissive`，狀態列 `root OK` |

## 驗收矩陣

| 項目 | 結果 |
|---|---|
| 14 語系渲染（zh-TW / zh-CN / en / ja / ko / fr / de / es / pt / ru / uk / th / vi / ar） | ✅ 全部正確 |
| 阿拉伯文 RTL 鏡像（`WS_EX_LAYOUTRTL`） | ✅ 全介面鏡像 |
| 各語系字型自動選擇 | ✅ 4 種字型鏈皆命中 |
| 連續 14 次介面重建（即時切換語言） | ✅ 不崩潰、紀錄無重複無遺失 |
| 200% DPI 版面（按鈕 500×68 實際像素） | ✅ 修正客戶區短少 41 px 後全部可見 |
| 確認對話框（靜音拍攝／隱私燈／重開機／未知韌體） | ✅ 皆正確彈出並可用 |
| 狀態列終態（不再殘留「執行中…」） | ✅ 修正後正確 |

## 需人工目視確認
1. 高 DPI 下的實際字級是否舒適（截圖為 200% 縮放環境）
2. 阿拉伯文母語者對 RTL 鏡像後按鈕順序的直覺性
3. 泰文與烏克蘭文的用詞是否符合當地習慣（本次僅驗證渲染與大陸/俄語區分正確）
