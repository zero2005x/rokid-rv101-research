# Rokid Remote v1.1 — 現代化 UI 驗收紀錄（2026-09-08）

> English: [README_EN.md](README_EN.md)

## 版本
- app-phone v1.1 (versionCode 2)，debug build
- 已實裝紅米 21091116UG（serial <PHONE_SERIAL>）

## 畫面（截圖於 reports/ui_v11/）
| 檔案 | 內容 |
|---|---|
| tab_remote.png | 遙控分頁：連線卡(<DEVICE_ALIAS> 已連線/狀態列 root✓)/拍攝/隱私指示燈 chips/進階收合 |
| tab_albums.png | 相簿分頁：2 欄大封面卡（Camera 21 項） |
| media_grid.png | 3 欄照片牆 + 影片時長角標（00:04/00:05） |
| viewer_photo.png | 全螢幕照片檢視器（漸進載入後原圖） |
| viewer_video.png | 影片檢視器（下載完成播放） |
| tab_local.png | 本機分頁（Camera 7 項 · 39.4 MB） |

## 自動化驗證矩陣（MIUI 擋 input 注入，改用 DebugReceiver 廣播驅動）
| 項目 | 結果 |
|---|---|
| OPEN_TAB remote/albums/local 切換 + uiautomator 結構驗證 | ✅ 三頁皆正確 |
| MediaActivity 網格載入（22 筆、時長角標） | ✅ |
| Viewer 遠端照片：大縮圖→進度→原圖 5.1MB 完整下載 | ✅ 06:30 |
| Viewer 遠端影片：6.6MB 下載(約 110s)→播放，無 MediaPlayer error | ✅ |
| GET_DEVICE_INFO / SET_LED（白燈 sysfs 回讀 0→255） | ✅ |
| CAPTURE_PHOTO（眼鏡端新增 IMG_1788820415226.jpg） | ✅ |
| TEST_CHAIN 相簿(22)→清單→縮圖→原檔下載 Success | ✅ |
| 舊版逾時問題（8.5MB 影片 >120s）→ 動態 timeout | ✅ 已修 |
| .part 殘檔清理 | ✅ 0 個 |

## 需人工觸控驗證（adb 無法注入點擊）
1. 相簿卡長按 → BottomSheet（檢視/重新命名/刪除）
2. 媒體長按 → 多選模式（全選/批次下載/批次刪除/返回退出）
3. FAB 新增相簿、重新命名/刪除對話框
4. 檢視器：點一下切 chrome、雙指縮放、雙擊放大/還原、影片控制列
5. LED 顏色 chips、ghostlock/root 開關、錄影中狀態切換
6. 深/淺色模式切換（跟系統）

## 已知限制/偏離
- 未加 SwipeRefreshLayout（避免新增依賴於離線建置環境）；改為 toolbar 重新整理鍵＋onResume/切頁自動重刷
- 縮放、圖片載入皆自製（無新第三方依賴）
- 批次下載依序單工（RFCOMM 協定限制）
