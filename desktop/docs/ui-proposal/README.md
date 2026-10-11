# ZeroLightRokid Desktop v3.0 — 自繪介面

> English: [README_EN.md](README_EN.md)

v3.0 把 UI 從「堆疊原生 Win32 控制項」改寫為**自繪的品牌介面**，
對應手機板的設計語彙。功能協調層（adb 流程）完全未動。

## 目錄

| 檔案 | 內容 |
|---|---|
| `01_dark.png` / `02_light.png` / `03_running.png` | 動工前的擬真 mockup（提案用） |
| `impl/` | **實機截圖**（2880×1620 @200% 與 1920×1080 @100%） |

## 架構

| 檔案 | 行數 | 職責 |
|---|---|---|
| `src/theme.h` | 406 | 深／淺調色盤、系統主題偵測、GDI+ 初始化、圓角／圓／線／幾何圖示 |
| `src/ui.h` | 232 | 元件庫：卡片、按鈕、開關、狀態面板、chip、進度條 |
| `src/ui_shell.h` | 761 | 元素定義、版面計算、繪製、命中測試、Rich Edit 紀錄、動作分派 |
| `src/zero_light_desktop.c` | 2732 | 其餘（adb 協調層、i18n、工作執行緒、視窗程序） |

只有 4 個真正的子控制項（adb 路徑／序號／root 指令／紀錄），其餘全部自繪。

## 對應原始痛點

| 痛點 | 解法 |
|---|---|
| 找不到按鈕 | 20 顆同權重 → 1 顆主要 + 6 顆次要 + 4 個開關，分四卡 |
| 看不到狀態 | 開關本身即狀態；30 秒背景輪詢即時更新 |
| 資訊難讀 | 一團文字 → 結構化：連線狀態＋序號、型號列、狀態 chips |
| 不知道下一步 | 主要卡動態引導文字（依 root 狀態改變） |
| 很醜 | 品牌色系、抗鋸齒圓角、自繪幾何圖示 |

## 診斷工具

`ZL_FORCE_DPI=96` 環境變數可強制指定 DPI，用於在固定縮放比例的機器上
驗證其他 DPI 的版面（例如在 200% 的螢幕上驗證 100% 版面）。

## 已知限制

- 展開紀錄時視窗會嘗試長高 240 邏輯像素；若已達工作區上限（例如
  200% 縮放的大螢幕）則維持原狀，此時紀錄佔滿剩餘空間並可捲動。
- `LOG_DEV_NOT_FOUND` / `ST_DEV_NOT_FOUND` 為 v3.0 新增的**安全機制**：
  指定序號的裝置不在線上時，工具拒絕對其他裝置做任何操作。

## 實機一鍵 root 回歸測試（2026-09-12）

在 2880×1620 @200% 縮放下，以 v3.0 介面在真實眼鏡（RG-glasses, 1.22.009）上執行完整一鍵流程。

**結果：通過。** 裝置端驗證：

| 檢查 | 結果 |
|---|---|
| SELinux | `Permissive` |
| `root_daemon` 程序 | 3 |
| socket | `/data/local/tmp/root_daemon.sock` 存在 |
| `last.done` | `uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0` ＋ `__GHOSTLOCK_PAYLOAD_DONE__` |
| 經 daemon 通道執行 `id` | `uid=0(root) gid=0(root) groups=0(root)` |

介面同步反映：主要卡的大按鈕自動變成 **✓ 眼鏡已 root** 狀態面板，
「重新取得 root」降為次要按鈕；裝置卡 chip 顯示 `Permissive`；
狀態列顯示「已連線，root ✓（常駐 daemon 在）」。

截圖：`impl/v3_regression_rooted.png`

### 過程中的失敗與判讀

前兩次嘗試失敗，日誌為 `failed-done-timeout`：SELinux 已變成 `Permissive`
（LPE 成功）但 payload 階段沒跑完，`last.done` 未被寫入。這是 exploit 已知的
**部分成功**模式，非介面改寫造成 —— 該路徑的裝置端腳本產生程式碼自 v2.0 起未變，
且 `assets/ghostlock_rv101`、`assets/root_daemon` 的雜湊仍與 `SHA256SUMS.txt` 相符。
第三次嘗試在 140 秒內成功。
