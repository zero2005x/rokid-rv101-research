# Rokid Capture / Remote 品牌圖示

> English: [README_EN.md](README_EN.md)

## 設計概念
AR 眼鏡 × 相機 × 隱私：左鏡片為**相機光圈**（拍攝能力），右鏡片為**隱私遮斷**（ghostlock 隱蔽模式）——涵蓋本專案兩大核心：遙控拍攝與隱私指示燈控制。

| 元素 | 色彩 | 語意 |
|------|------|------|
| 鏡框/鏡腿 | `#E8EDF5` 近白 | 產品本體 |
| 光圈中心 | `#4FC3F7` 青 | 相機／拍攝 |
| 遮斷斜線 | `#FFC24B` 琥珀 | 隱私／ghostlock |
| 背景（adaptive） | `#0B1220` 深藍 | 深色高對比（AR 顯示） |

## 檔案
- `logo.svg` — 全彩版（文件/網站用）
- `logo_monochrome.svg` — 單色版（壓印/themed icon）
- 兩 App 內對應資源：
  - `res/drawable/ic_launcher_foreground.xml`（adaptive 前景）
  - `res/drawable/ic_launcher_monochrome.xml`（Android 13+ themed icon）
  - `res/mipmap-anydpi-v26/ic_launcher.xml`、`ic_launcher_round.xml`
  - `res/values/colors_launcher.xml`（背景色 `#0B1220`）
  - 眼鏡 App 另含 `res/drawable/ic_notification.xml`（通知小圖示）

## 用法
兩 App 的 Manifest 已宣告 `android:icon` / `android:roundIcon`；眼鏡端前台服務通知
使用 `ic_notification`。替換任一資源後重建即可。
