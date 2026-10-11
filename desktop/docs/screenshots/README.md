# 實機測試截圖

2026-09-11 於 Windows 11（build 26200，2880x1620 @ 200% 縮放）+
實機 Rokid RV101 眼鏡（序號 <DEVICE_SERIAL>）執行 `zero_light_desktop.exe` 的截圖。

| 檔案 | 內容 |
|---|---|
| `01_main_zh-TW.png` | 主畫面（繁體中文），已偵測到眼鏡並顯示完整遙測 |
| `02_arabic_rtl.png` | 阿拉伯文：整個介面鏡像（關閉鈕移到左側、按鈕由右至左） |
| `03_ja_th_ru_ko.png` | 日文／泰文／俄文／韓文：字型後備鏈各自選中 Yu Gothic UI / Leelawadee UI / Segoe UI / Malgun Gothic |
| `04_zh-CN_fr_de_uk.png` | 簡中／法文／德文／烏克蘭文：簡中採大陸用語、法文 « »、德文 „ " |
| `05_device_detected.png` | 裝置資訊面板：序號／型號／韌體／SPL／Android／SELinux／身分／root／Shizuku／開機時間 |
| `06_rooted_after_oneclick.png` | 「🚀 一鍵取得 root」成功後：面板自動更新為 root: Running / SELinux: Permissive |

實測項目：14 語系全數渲染無誤且可連續切換（14 次介面重建不崩潰、紀錄無重複或遺失）、
重新整理／驗證 root／自訂 root 指令／exploit log／OTA 修補檢查／② 安裝 helper 皆正常，
② 寫入裝置的 helper 腳本為 1957 bytes / 58 行（stdin 修正前會是 0 bytes）。

## 執行測試抓到的缺陷（靜態審查與編譯都無法發現）

1. **APPCRASH**：裝置資訊面板格式字串 14 個 `%ls` 卻只給 11 個引數 → 按「重新整理」
   讀到裝置後崩潰於 `msvcrt.dll`（c0000005）。
2. **stdin 管線方向反了**：父行程保留寫端卻在寫入前關閉，再對讀端 `WriteFile`
   → `adb shell` 立刻 EOF，② 安裝 helper 會寫出空腳本、自訂 root 指令無作用。
3. 200% 縮放螢幕上客戶區比預期少 41 px → 狀態列被切掉。
4. 操作結束後狀態列停在「執行中…」。
5. `pkill -f` 會殺掉自己的 adb shell（回傳 137）。
6. 無 stdout 的指令被誤報為失敗。

## 環境備註

測試時裝置上有約 **1554 個先前 session 殘留的 `ghostlock_rv101` 行程**，
造成 8 核裝置 load average 達 70–102、adb 出現 `fork failed: Try again`。
清除後降至 load 1.44、可用記憶體 731 MB，root 與 Shizuku 均保持常駐。
正式使用前建議先確認裝置負載。

## 完整流程實測（2026-09-11/12）

### Phase 1 — 需要 root 的功能（在既有 root 狀態下測）
| 功能 | 結果 |
|---|---|
| 靜音拍攝 | 推送 3584 bytes + 建立 6 個 bind mount；`changed=6 mounted=6` |
| 還原拍攝音 | 6 → 0 個 mount；`changed=6 mounted=0` |
| 抑制隱私燈 | `led=off hold=1`，保持迴圈啟動 |
| 還原隱私燈 | `led=on hold=0`，保持迴圈停止 |
| 鎖定 OTA | `Package com.rokid.glass.ota new suspended state: true` |
| 解除 OTA 鎖定 | `new suspended state: false` / `new state: enabled`（測試後已重新鎖定） |

### Phase 2 — 重新開機
| 項目 | 重開機後 | 判定 |
|---|---|---|
| SELinux | **Enforcing**（原 Permissive） | ✅ root 消失（記憶體層級） |
| root_daemon socket | **0** | ✅ 符合設計 |
| shizuku_server | **0** | ✅ 每次開機需重跑 |
| 靜音拍攝 bind mount | **0** | ✅ 重開機即清除 |
| **OTA 鎖定** | **1（仍為 suspended）** | ✅ **跨重開機維持 —— 最重要的安全特性** |
| `/data/local/tmp` 檔案 | 全部保留 | ✅ 檔案層級持久 |
| App 狀態列 | `Status: connected, not rooted` | ✅ 正確反映 |

### Phase 3 — 從乾淨狀態取得 root
| 步驟 | 結果 |
|---|---|
| ① 推送 exploit | `ghostlock_rv101` 60272 bytes、`root_daemon` 9304 bytes |
| ② 安裝 helper | 腳本 1957 bytes、行程常駐、PID 檔寫入 |
| ③ 觸發提權 | **第一次嘗試即成功**（約 139 秒），`uid=0(root)`、Permissive |
| 🚀 一鍵取得 root | **兩次實測皆成功**（約 6–8 分鐘，含 uptime > 150 秒的穩定等待） |

一鍵流程日誌可見完整機制：等待 `uptime > 150`、`Boot 1/6, trigger 1/2`、
progress 由 `started` → `waiting-done` → `Root achieved! Verifying`。

### 本輪實測新發現並修正的缺陷
1. **對話框標題顯示字面 `v%ls`**：`T(STR_APP_TITLE)` 未用 `Tf()` 代入版本（3 處）。
   已新增靜態檢查：`T()` 取用的字串若含格式符即報錯。
2. **一鍵成功後裝置面板未更新**：面板仍顯示 `root: Not running`，與狀態列的
   `root ✓` 互相矛盾。已在成功後自動呼叫 `do_refresh()`。
