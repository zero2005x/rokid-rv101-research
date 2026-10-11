# Rokid 雙 Android 遙控拍攝系統

自有 Rokid 眼鏡 + Android 手機的遙控拍攝／相簿管理系統，同時是 RV101 眼鏡的 root 安全研究專案。

> **本 repo 為 monorepo**：主體是 `rokid_root` 研究專案，Windows 桌面工具置於 [`desktop/`](desktop/README.md)。
>
> 發布說明（2026-10-11）：
> - [`EXCLUDED.md`](EXCLUDED.md) — 未納入版控的檔案與理由（含 SHA-256）
> - [`REDACTION_LOG.md`](REDACTION_LOG.md) — 個資與洩漏憑證的遮蔽紀錄（before/after 雜湊）
> - [`MANIFEST.md`](MANIFEST.md) — 原始工作區版面清單與分類原則
>
> 完整工作區（含工具鏈、APK 標的、反編譯輸出、建置快取與影片製作區，約 17 GB）仍保留在本機 `~/rokid_root/`。

- **眼鏡端 App**：CameraX 拍照／錄影、相簿索引、`ghostlock_rv101`（CVE-2026-43499）LPE 提權、
  隱私燈壓制（ghostlock）、**靜音拍攝（壓制快門/錄影提示音）**、**全透明資訊儀表板**、WiFi 媒體 TCP server、
  **戒指控制（root 直讀 evdev，背景可拍照/錄影）**、
  **提權維運層**：執行期引擎探測 `AutoGhostlockEngine`、root daemon 存活看守 `RootDaemonKeeper`＋root 側 keep-alive 迴圈、提權度量；取得 root 後跑 `deploy/install_root_keepalive.sh` 讓 daemon 撐完整個 session。
- **手機端 App**：品牌雙主題（深色深藍底／淺色白底，可切換）、四分頁（遙控／相簿／本機／設定；
  設定頁分組可折疊、相簿 3 欄；
  遙控頁一屏完成拍攝、設定集中管理）、相簿搜尋／排序、本機容量摘要與批次刪除、
  藍牙遙控（相機／LED／root／ghostlock）、相簿管理與下載、
  **WiFi 媒體通道（自動回退藍牙）**、眼鏡顯示內容遠端設定、**root 狀態與提權度量顯示（含「請執行哪支腳本」指引）**、14 語系介面、
  **App 內建語言選擇按鈕（即時切換，操作紀錄一併在地化）**、**戒指控制卡（手勢攔截＋手勢學習）**。
- **傳輸**：**控制指令與媒體都優先走 WiFi**（眼鏡 SoftAP `192.168.43.1:8555`，同一條 TCP 連線、
  同一個 `CommandRouter`）；媒體操作前會**自動**建立 WiFi 通道（請眼鏡開熱點→加入 AP→連 TCP），
  失敗才回退藍牙 RFCOMM。原因：眼鏡端 `BluetoothServer` 與 `WifiServer` 在 accept 時都會關閉前一條連線，
  兩條通道**互斥**——若一般指令改走藍牙，會把 WiFi socket 踢掉、媒體便默默退回藍牙（實測 10.6 秒影片
  WiFi 3.8 秒 vs 藍牙 324 秒逾時）。設定頁「WiFi 傳輸」為持久化偏好（預設開）。

> 僅供**自有/授權設備**的安全研究與滲透測試使用。

## 文件索引

| 文件 | 內容 |
|------|------|
| [`MANIFEST.md`](MANIFEST.md) | **工作區版面清單**：各目錄角色、分類原則、待清理項 |
| [`dist/README.md`](dist/README.md) | **可安裝 APK 清單**：版本對照（眼鏡 safe/safe.2/original、手機）、安裝與重建指令 |
| [`docs/deploy.md`](docs/deploy.md) | 建置、佈署、Shizuku 模式、WiFi 媒體通道、多語系、圖示、無線 adb 結論（§9） |
| [`docs/wireless-adb-solution-design.md`](docs/wireless-adb-solution-design.md) | **通用無線 ADB 方案設計**（一次插線→永久無線；拓撲 T1/T2/T3、bootstrap、PC 工具；含「純 APK relay 不可行」的 DAC 實證） |
| [`docs/architecture.md`](docs/architecture.md) | 系統架構（雙通道、觸發引擎與**執行期探測**、儀表板、UI/語系、**提權維運層**） |
| [`docs/protocol.md`](docs/protocol.md) | 通訊協定（訊息總表、frame 格式、binary 傳輸） |
| [`docs/device-debug-notes.md`](docs/device-debug-notes.md) | 實機除錯紀錄（歷史） |
| [`docs/root-from-zero-guide.md`](docs/root-from-zero-guide.md) | 新手教學：從零取得 root（bridge 路線） |
| [`docs/shizuku-feasibility-report.md`](docs/shizuku-feasibility-report.md) | Shizuku 整合可行性（已實機驗證） |
| [`reports/shizuku_integration_test_report.md`](reports/shizuku_integration_test_report.md) | Shizuku 整合實機測試報告 |
| [`reports/wifi_transfer_test_report.md`](reports/wifi_transfer_test_report.md) | WiFi 傳輸＋空白畫面修復實機測試報告 |
| [`reports/RV101_CAMERA_LED_FLASH_MECHANISM.md`](reports/RV101_CAMERA_LED_FLASH_MECHANISM.md) | 隱私燈機制研究＋錄影閃爍根治（§9） |
| [`reports/silent_capture_mechanism.md`](reports/silent_capture_mechanism.md) | 拍照/錄影提示音壓制（靜音拍攝）機制與驗證 |
| [`reports/phone_i18n_localization_report.md`](reports/phone_i18n_localization_report.md) | 手機端 14 語系全在地化＋App 內語言選擇按鈕（實機驗證） |
| [`reports/ring_control_report.md`](reports/ring_control_report.md) | 戒指控制：戒指辨識、路徑 A/B 實作、root 直讀 evdev、實機驗證 |
| [`reports/hardcode_audit.md`](reports/hardcode_audit.md) | 全專案硬編碼稽核（字串／路徑／版面／腳本）與修正紀錄 |
| [`reports/FACTS.md`](reports/FACTS.md) | **單一權威事實表**：裝置世代／kernel config／CVE 狀態／GhostLock 位移／產品結構（衝突時以此為準） |
| [`reports/index_resilience_verification_2026-09-16.md`](reports/index_resilience_verification_2026-09-16.md) | **媒體索引雙向自我修復／傳輸通道推播的補跑實機驗證**（A–E 全過；含免 root 的 adb tunnel 驗證法） |
| [`reports/wireless_adb_feasibility_2026-09-20.md`](reports/wireless_adb_feasibility_2026-09-20.md) | **無線 adb（adb-over-TCP）可行性實測：可行**——adbd 綁在 IPv6 `[::]:5555`，PC 連 SoftAP 後 `adb connect 192.168.43.1:5555` 取得成功；**推翻**舊「本 build 不支援」結論（偵測碼只 grep IPv4 的 bug） |
| [`reports/docs_correctness_audit_2026-09-13.md`](reports/docs_correctness_audit_2026-09-13.md) | **文件正確性稽核**：97 份 reports＋15 份 docs 的分片查核、17 組跨檔矛盾、缺失成品與修正紀錄 |
| [`reports/usbdebug_antidetect_module_2026-09-30.md`](reports/usbdebug_antidetect_module_2026-09-30.md) | **手機端 USB 偵錯防偵測（自建 LSPosed/Vector 模組）**：`android/usbdebug-hider/` Java 層＋native PLT/GOT 兩層遮蔽，讓指定 App 讀到「USB 偵錯／開發人員選項＝關」而 adb 仍可用；含 APatch 框架安裝（NeoZygisk v2.4＋Vector v2.2）、全 adb 自動化（Vector CLI／mini_sqlite3）、A/B 對照與熱重載驗證、已知限制 |
| [`reports/su_escalation_roi_assessment.md`](reports/su_escalation_roi_assessment.md) | 提權程式碼投資評估：實測 84–112 秒、歷史 143 次統計、失敗分類與 P0–P3 建議 |
| [`reports/zero_light_desktop_test_report.md`](reports/zero_light_desktop_test_report.md) | **Windows 桌面工具（ZeroLightRokid Desktop v2.0）實機測試報告**：完整 root 流程＋重開機矩陣＋GUI/14 語系驗收 |
| [`reports/zero_light_desktop_ui/README.md`](reports/zero_light_desktop_ui/README.md) | 桌面板 GUI 驗收截圖（含阿拉伯文 RTL、各語系字型） |
| [`android/branding/README.md`](android/branding/README.md) | 品牌圖示設計（SVG／adaptive icon） |
| [`RV101_Disclosure/00_README.md`](RV101_Disclosure/00_README.md) | 協調式漏洞揭露套件 |

## 目錄結構

```
rokid_root/
├── android/              # Gradle 多模組工程
│   ├── shared/           # 共用協定模組（FrameCodec / Protocol / 模型）
│   ├── app-glasses/      # 眼鏡端 App
│   ├── app-phone/        # 手機端 App
│   └── branding/         # 品牌 SVG 與圖示說明
├── desktop/              # ★ Windows 原生桌面工具（ZeroLightRokid Desktop v3.0）
│   ├── src/              # 純 Win32 API（C 語言），無 runtime 依賴
│   ├── assets/           # 執行期必需品（exploit／root_daemon／靜音提示音）
│   ├── tools/            # i18n 產生器與靜態檢查（14 語系）
│   └── docs/             # 迴圈保護、i18n、延伸路徑測試、macOS 可行性
├── deploy/               # ADB 佈署與測試腳本（root_bridge / start_shizuku / test_* / lock_ota ...）
├── docs/                 # 架構、協定、佈署、研究報告
├── reports/              # 漏洞研究、實機測試、除錯紀錄
├── research/             # GhostLock / Shizuku 逆向參考資料
├── scripts/              # exploit 開發期工具 ＋ i18n 工作流（scripts/i18n/）＋ 硬編碼稽核（audit_hardcode.py）
├── dist/                 # 可安裝 APK（眼鏡 safe/safe.2/original、手機）＋ dist/README.md 版本對照
├── binaries/             # 自裝置搶救的不可重建二進位（binaries/device_rescue/）
├── source_code/          # 250 支探測／exploit 原始碼（adsprpc/binder/kgsl/ashmem 等）
├── frida/                # Frida hook 腳本（OTA／intent 探索）
├── analysis/             # 封包、jadx 反編譯輸出（未納版控，見 EXCLUDED.md）
├── apks/ tools/          # APK 標的、JDK/NDK 等工具（未納版控，可重建）
├── build-cache/ .zig-cache/  # 建置／探針暫存；可重建（未納版控）
├── RV101_Disclosure/     # 協調式漏洞揭露套件（含凍結證據）
├── HITCON_ZeroDay_RV101/ # 研討會議程素材
├── EXCLUDED.md           # 未納入版控的檔案清單與理由（含 SHA-256）
└── REDACTION_LOG.md      # 個資／憑證遮蔽紀錄（before/after 雜湊）
```

> 檔案歸屬原則見 [`MANIFEST.md`](MANIFEST.md)：**正式證據 → `reports/`**、**暫存／可重建 → `build-cache/`**；
> `reports/media*/` 為截圖與量測原始檔的 canonical 位置。

## 快速開始

1. 建置：`docs/deploy.md` §1（本機需 JDK 17；已附 Temurin 路徑）
2. 佈署與提權：`docs/deploy.md` §9（每 boot 以 adb 武裝一次：`deploy/start_shizuku.sh` 或 `deploy/root_bridge.sh`）；
   取得 root 後建議跑 `deploy/install_root_keepalive.sh` 安裝 daemon 存活看守（見同檔 §提權維運 P0–P3）
3. WiFi 媒體傳輸：`docs/deploy.md` §9.4
4. 裝置端驗證腳本：`deploy/test_shizuku_e2e.sh`（提權）、`deploy/recover_and_verify.sh`（恢復後回歸）
