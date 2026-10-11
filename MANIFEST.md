# 工作區版面清單（MANIFEST）

> 目的：說明 `rokid_root/` 每個頂層項目的角色與「正確歸屬」，作為檔案擺放的判準。
> 最後盤點：**2026-09-13 23:xx**（複查：體積與檔案計數重新量測；新增 `dist/`、`binaries/`、`build-cache/`、`.zig-cache/` 四個頂層項目的角色與判定；補上 §2.1 的 APK 版本與證據目錄）。總計約 **4.7 GB**（頂層 23 項；其中約 4.3 GB 為工具鏈／研究輸入／建置產物，非專案內容）。

## 1. 分類原則

| 類別 | 歸屬 | 說明 |
|------|------|------|
| **專案內容**（應納入版控） | `android/` `docs/` `deploy/` `scripts/` `README.md` `MANIFEST.md` | 程式碼、文件、佈署腳本 |
| **研究產出**（可納版控，體積注意） | `reports/` `RV101_Disclosure/` `HITCON_ZeroDay_RV101/` `research/` `source_code/` | 報告、證據、外部參考、探測原始碼 |
| **研究輸入／大型資料**（不納版控） | `apks/` `analysis/` | 裝置 APK、封包與反編譯輸出 |
| **工具鏈／環境**（不納版控，可重建） | `tools/` `gradle_home/` `.venv/` `frida/` | JDK、jadx、Gradle 快取、Python venv、frida-server |
| **建置產物**（不納版控，可重建） | `android/*/build/` `android/.gradle/` | APK 與中間產物 |
| **備份／暫存**（目前**無**） | — | `rokid_root_20260904_backup.zip` 與 `wm_work/` 皆已移出（見 §5） |

## 2. 頂層項目現況

| 項目 | 體積 | 內容 | 判定 |
|------|------|------|------|
| `android/` | 122M | Gradle 多模組（shared/app-glasses/app-phone/branding/adbreachprobe/**usbdebug-hider**/**debugdetectprobe**）＋ `build/` 產物約 110M（2,310 檔） | ✅ 正確（`build/` 已列入 .gitignore） |
| `apks/` | 1.1G | 自裝置拉出的 17 個 vendor APK | ✅ 研究輸入（體積大，勿納版控） |
| `analysis/` | 807M | 封包（`cap1/2.pcap`）、`jadx_out/`、`strings/`、`phone/`、`device_data/` | ⚠️ 研究輸入，但缺 README；建議改名／移至 `research/device_analysis/` |
| `tools/` | 439M | `jdk/`（建置用）、`jadx/`（已解壓）、**`framework/`（APatch 用 Xposed 框架安裝包：NeoZygisk v2.4-289、Vector v2.2-3080）** | ✅ `jadx.zip` 已刪（見 §5）；framework zip 為第三方工具（可重新下載） |
| `.venv/` | 654M | Python 虛擬環境（腳本用） | ✅ 環境（勿納版控） |
| `gradle_home/` | 1.2G | Gradle 使用者快取（`GRADLE_USER_HOME`） | ✅ 環境（勿納版控） |
| `research/` | 71M | `ghostlock_refs/`（7 份公開 exploit 參考：7 個 zip ＋ 解壓目錄）、`kernel_configs/`（裝置 config）、`shizuku/`（Shizuku 源碼＋研究＋branding 之前置） | ✅ 外部參考的正確位置 |
| `frida/` | 51M | frida-server 二進位 + hook 腳本（OTA/intent） | ⚠️ 工具＋腳本混放；腳本可留、二進位屬工具 |
| `source_code/` | 36M | 262 個 `.c` 探測／exploit 原始碼（目錄共 286 檔）（adsprpc/binder/kgsl/ashmem 等） | ✅ 正確（與 `deploy/*.c` 的 App 用 daemon 分工明確） |
| `reports/` | 53M | 331 檔：研究報告、實機測試報告、`ghostlock_runlogs/`(148)、`media/`(32)、`ui_v11/`(7)、`zero_light_desktop_ui/`(7)、`ui_transport_2026-09-13/`(5)、`media_index_repair_2026-09-13/`(6)、`led_validation/`(2)、**`media_usbdebug_hider/`(6：USB 偵錯防偵測 A/B 證據)** | ✅ 研究產出的正確位置（**正式證據的 canonical 位置**） |
| `deploy/` | 2.4M | 40+ 檔：腳本（`.sh`/`.py`/`.cmd`）＋ `.c`（root_daemon／run_shell／ring_input_daemon／**mini_sqlite3**）＋ 已編譯 ELF；**USB 偵錯防偵測：`usbdebug_hider_ctl.sh`／`usbdebug_hider.conf`／`build_debugmask_so.sh`／`build_mini_sqlite3.sh`**；另有執行期 `*.log`（`.gitignore` 已排除，正式證據見 `reports/`） | ✅ 正確 |
| `scripts/` | 920K | 74 檔：開發期工具與 orchestrator（**canonical 位置**）＋ `i18n/`（手機端翻譯工作流：`phone_keys.tsv`／`out/`／正規化、檢查、套用、遷移共 4 支腳本） | ✅ 正確 |
| `RV101_Disclosure/` | 2.0M | 廠商協調式揭露套件（52 檔） | ✅ 正確 |
| `HITCON_ZeroDay_RV101/` | 72K | HITCON 投稿套件（14 檔，redacted 附件） | ✅ 正確（與上者為**不同交付對象**，見 §4） |
| `docs/` | 496K | 15 份 `.md`（＋1 份 PDF）設計／協定／佈署／研究文件 | ✅ 正確 |
| `README.md` / `README_ORGANIZED.md` | 26K | 專案總覽／歷史主索引 | ✅ 正確 |
| `dist/` | 39M | 12 檔 APK ＋ `README.md`：眼鏡 `1.0-safe.6`（目前）／`1.0-safe.5`／`1.0-safe.4`／`1.0-safe.3`／`1.0-safe.2`／`1.0-safe`／`1.0-original`、手機 `1.3`（目前）／`1.2`／`1.1`、**`usbdebug-hider-v1.1`（LSPosed 模組）／`debugdetectprobe-v1.0`（對照組探針）**，附版本對照與安裝指令 | ✅ 正確（**交付物**；`*.apk` 被 `.gitignore` 排除，需靠 `README.md` 重建） |
| `binaries/` | 4.6M | 13 檔：`device_rescue/` — **自裝置搶救、無法重建**的二進位（10 個無原始碼探針＋2 個 payload）＋ `README.md` | ✅ 正確（**不可重建，最需要保存**） |
| `build-cache/` | ~2.3G | 3,140+ 檔：**暫存／可重建** — zig 快取（`zig-global/`、`zig-local/`）、探針暫存（`gate_re/`、`ta_re/`、`ta_load/`、`tb/`）、截圖工作區（`shots/`）、第三方投遞測試素材（`ringtest/`）、一次性修檔腳本（`add_p21.py`／`add_p23.py`／`add_p24.py`／`add_p27.py`／`fix_p24.py`）；**`ota_122009/`、`ota_125012/`＝兩版 OTA 分析工作區**（各約 2.1G：OTA zip＋`payload.bin`／kernel Image／ELF；zip 與 payload **可自原廠 CDN 重新下載，勿視為唯一副本**；含絕對位址的 raw report 與 kernel.elf 只留此處、不外寄） | ⚠️ 依性質為暫存（**正式證據請歸 `reports/`**）；建議加入 `.gitignore` |
| `.zig-cache/` | 72M | zig 的 local cache（與 `build-cache/zig-local/` 重疊） | ⚠️ 冗餘快取；可刪（重建即可），或改用 `build-cache/zig-local` |

## 2.1 由腳本產生的檔案（可重建，勿手改）

| 產生檔 | 產生器 | 輸入 |
|--------|--------|------|
| `android/usbdebug-hider/…/EmbeddedSo.java` ＋ `BakedDefaults.java` | `android/usbdebug-hider/build.gradle.kts`（建置期自動產生） | `android/usbdebug-hider/src/main/jniLibs/arm64-v8a/libdebugmask.so`（`deploy/build_debugmask_so.sh`）＋ `-PmaskTargets=` |
| `android/app-glasses/…/ghost/SilentPayloads.kt` | `scripts/gen_silent_payload.py` | `reports/media/silent_camera.ogg`（ffmpeg 產生之 0.3s 靜音 Ogg Vorbis） |
| `android/app-phone/…/res/values*/strings.xml` 的 i18n 區塊 | `scripts/i18n/apply_phone_strings.py`（可重複執行） | `scripts/i18n/phone_keys.tsv`（zh-TW/en）＋ `scripts/i18n/out/*.xml`（其餘 12 語系） |

戒指控制相關新增檔案（canonical 位置）：

| 檔案 | 角色 |
|------|------|
| `android/shared/…/ring/{RingScanner,RingBonder,RingGatt,RingWatcher,RingGestureDetector}.kt` | 兩端共用的 BLE 探索／配對／GATT 探測／監看／手勢辨識 |
| `android/app-phone/…/ring/RingController.kt` | 路徑 A：手機端手勢攔截與動作執行（＋UI 狀態） |
| `android/app-glasses/…/ring/{RingEvdevReader,RingControlManager}.kt` | 路徑 B：root 直讀 evdev 與本機拍攝 |
| `deploy/ring_input_daemon.c`＋`build_ring_input_daemon.sh` | 備援實作（native evdev 讀取器；**未採用**，本版用 `getevent`） |

硬編碼治理相關檔案：

| 檔案 | 角色 |
|------|------|
| `scripts/audit_hardcode.py` | 硬編碼稽核工具（字串／路徑／版面／腳本／**元件可達性／憑證字面值／native `chmod`**，可重跑） |
| `reports/FACTS.md` | **單一權威事實表**（裁決 17 組跨檔矛盾；各報告引用它而非各自表述） |
| `binaries/device_rescue/` | **從裝置搶救的不可重建二進位**（10 個無原始碼探針＋2 個 payload；見該目錄 `README.md`） |
| `reports/missing_binaries_inventory_2026-09-13.md` | **缺失／被引用二進位完整盤點**（320 名稱：可重建／僅變體／全失／僅存裝置） |
| `reports/gateserviced_ta_load_2026-09-13.txt` | GateServiced **TA 載入與成功 EXEC_CMD** 實測 transcript（對應 FACTS §5.2 S10–S13） |
| `build-cache/empty_file_probe.py` | 空檔→`3001` 實測探針（用後即清理；對應 FACTS §5.2 S16） |
| `reports/static_re_gate_codes_and_rescue_2026-09-13.md` | GateServiced 錯誤碼**指令級 RE**＋搶救二進位分類（對應 FACTS §5.2 S14/S15、§9.5） |
| `reports/gate_dma_alloc_heapname_probe_2026-09-13.txt` | **`dma_alloc` heap 名稱／fd 槽實機否證探針**（推翻「heap 名＝堆疊指標」與「未初始化 fd 洗碼」；對應 FACTS §5.2 S19/S20） |
| `reports/gate_300a_and_dma_unalloc_2026-09-13.md` | GateServiced **`0x300A` 硬編碼**＋**`dma_unalloc` 回傳空間**逐指令 RE（對應 FACTS §5.2 S18–S20；Rev.2 已撤回兩側支結論） |
| `reports/dtx_ta_command_space_2026-09-13.md` | **DTX／gate TA 命令空間完整靜態列舉**（id→handler、`op` 子命令、SFS 物件、風險分級；**未執行任何命令**；對應 FACTS §5.2 S21/S22） |
| `reports/ring_capture_security_audit_2026-09-13.md` | **戒指操控拍照／攝影的來源授權與硬編碼資安稽核**（配對確認機制評估、exported 除錯接收器、`:8555` 零認證、AP 密碼寫死、root daemon socket `0777`；對應 FACTS §7 P7–P14、§9-E） |
| `reports/wifi_transfer_fix_2026-09-13.md` | **WiFi 媒體通道修復（Rev.1–5）**：①`NEARBY_WIFI_DEVICES` 未在執行期要求＋②TCP socket 未綁定 AP 網路（P27）③通道互斥導致媒體靜默回退藍牙＋媒體前不自動建立 WiFi（P28）④UI 傳輸標示競態⑤眼鏡媒體索引無聲遺失（P29）⑥未收尾影片與 `.part` 殘骸（P30） |
| `reports/camera_ring_failure_remote_diagnosis_2026-09-13.md` | **拔線狀態下的遠端診斷**：`camera_not_ready`（服務被 app idle 停掉）／`ringEnabled=false`／`ringDevice` 指到戒指；含「裝置路徑失效靜默沿用」真 bug 的修正（對應 FACTS §7 P20） |
| `dist/README.md` | **可安裝 APK 清單**（safe 版／原始版／手機版、安裝與重建指令） |
| `reports/arm_safety_policy_2026-09-13.md` | **提權安全武裝四道保護**（開機迴圈斷路器／重試上限＋退避／僅充電時／開機後延遲）實作與實機驗證；含兩版 APK 區分方式（對應 FACTS §7 P19） |
| `dist/app-glasses-v1.0-safe.3.apk` | **眼鏡端目前版本**（`1.0-safe.3`，code 5）：四道保護 ＋ 索引**雙向**校正（逐筆容錯含 albums、磁碟自我修復、外部刪檔自動移除）＋ 原子寫入（對應 FACTS §7 P29） |
| `dist/app-glasses-v1.0-safe.2.apk` | 眼鏡端前版（`1.0-safe.2`，code 4）：單向索引修復（僅 `media` 陣列逐筆容錯） |
| `dist/app-glasses-v1.0-safe.apk` | 眼鏡端前版（`1.0-safe`，code 3）：四道保護全開，索引修正前的行為（A/B 對照用） |
| `dist/app-glasses-v1.0-original.apk` | **原始版**（versionName `1.0`）供 A/B 對照 |
| `dist/app-phone-v1.3.apk` | **手機端目前版本**（`1.3`，code 4）：在 1.2 之上修掉「WiFi 為當前通道時 `PROGRESS` 推播被丟棄」的回歸（`Remote.onPush` 集中註冊）、斷線即時通知 UI、`@Volatile`、刪死碼。**目前安裝於手機** |
| `dist/app-phone-v1.2.apk` | 手機端前版（`1.2`，code 3）：WiFi 優先傳輸＋自動建立 WiFi 通道＋傳輸標示即時更新＋`.part` 清理 |
| `dist/app-phone-v1.1.apk` | 手機端前版（`1.1`，code 2）保留對照 |
| `reports/glasses_reboot_after_adb_unplug_2026-09-13.md` | **眼鏡拔掉 adb 後重開機的原因分析**（主因＝exploit panic 延遲後果；排除戒指／掉電／adb reboot；BOOT_COMPLETED 被擋；可修正清單；對應 FACTS §7 P18） |
| `reports/rgr06_pairing_failure_2026-09-13.md` | **RGR06 配對失敗取證**（手機：戒指中止 SMP；眼鏡：修掉 `RingBonder` 崩潰後配對成功並成為 HID 裝置；對應 FACTS §7 P17） |
| `reports/ring_p0_p1_fixes_test_2026-09-13.md` | **戒指／相機 P0＋P1 修正實作與實機測試**（busy_recording、錄影看門狗、連拍節流、媒體鍵放行、已配對 HID 清單；對應 FACTS §7 P16） |
| `reports/aivela_ring_connection_failure_2026-09-13.md` | **AIVELA 戒指「連接失敗」log 取證與處置**（LMK→FGS 被拒→錄影卡死→服務被停；含已驗證的立即恢復步驟；對應 FACTS §7 P15） |
| `reports/ring_reachability_test_2026-09-13.txt` | **第三方 App 投遞 `DebugReceiver` 的端到端實證**（自製最小 App、uid 10346／10064；測畢已移除，素材留 `build-cache/ringtest/`） |
| `reports/ui_transport_2026-09-13/` | **傳輸方式標示修正的實機截圖證據**（4 張＋README：藍牙→WiFi 連線中…→WiFi 已連線，全程未手動切分頁；對應 FACTS §7 P28） |
| `reports/media_index_repair_2026-09-13/` | **眼鏡媒體索引 A/B 實驗樣本**（修復後索引、刪除前索引、刻意損壞的索引、受害者檔名、刪除清單＋README；對應 FACTS §7 P29/P30） |
| `reports/url_check_2026-09-13.txt` | 外部 URL 可達性抽查（含 OTA zip 可下載的發現，對應 FACTS §9.6） |
| `reports/gateserviced_probe_2026-09-13.txt` | GateServiced 協定／能力**實測 transcript**（對應 FACTS §5.2） |
| `reports/device_facts_ramoops_uptime_2026-09-13.txt` | ramoops／uptime 條件／系統基準取證（對應 FACTS §6.1） |
| `reports/kgsl_ioctl_retest_2026-09-13.txt` | KGSL／周邊 ioctl **本世代實測原始輸出**（6 支探針；對應 FACTS §5.1） |
| `deploy/kgsl_micronode_probe.c`／`kgsl_micronode_probe2.c` | **0x58 micronode 專用探針**（nr×size 矩陣＋語意驗證；實測見 FACTS §5.1.1） |
| `deploy/build_probe_zig.sh` | 以 **zig 交叉編譯**裝置探針（aarch64-linux-musl 靜態；免 Windows NDK） |
| `reports/kgsl_micronode_retest_2026-09-13.txt` | micronode 探針原始輸出（證據） |
| `deploy/kgsl_ioctl_probe.c`／`build_kgsl_probe.sh` | 自建 ioctl 探針（**本次未能編譯**：Windows 掛載唯讀＋無 Linux 交叉工具鏈；保留供有工具鏈時使用） |
| `reports/hardcode_audit.md` | 稽核結果與修正紀錄（含刻意保留項目與待決事項） |
| `reports/docs_correctness_audit_2026-09-13.md` | 全工作區**文件正確性稽核**（分片方法、跨檔矛盾、缺失成品、已修正清單） |
| `android/app-glasses/…/GlassesPaths.kt` | 眼鏡端裝置路徑**唯一**來源（`/data/local/tmp`、`/sys/class/leds`） |
| `deploy/env.sh` | 腳本共用的 `ADB`／`SERIAL`（唯一預設值來源，可用環境變數覆寫） |
| `deploy/lease.sh` | deploy 路徑的**共用租約** primitive（與 App 看守、桌面工具共用 `/data/local/tmp/ghostlock_op.lease`） |
| `deploy/proc_util.sh` | 安全殺程序工具（取代會自我匹配／誤殺 `*_desktop` 變體的 `pkill -f`） |
| `deploy/test_lease.sh` | 租約測試套件（9 項；**不需要 root、不觸發 exploit**，改租約程式碼後必跑） |
| `deploy/test_keeper_lease_guard.sh` | keeper 租約守衛端到端測試（15 項；製造「root 失效」觀察守衛日誌）|
| `deploy/root_restore_delayed.sh` | 延遲復原行程（以 root 身分先留下；測試後帶回 daemon，**不需重跑 exploit**）|
| `reports/deploy_lease_fix_2026-09-13.md` | deploy 租約修補記錄＋兩個新缺陷（同 owner 不互斥、`pkill` 漏殺 `_desktop`） |
| `reports/index_resilience_verification_2026-09-16.md` | **索引／通道補跑驗證報告**（A 壞 album 容錯、B 正向補登、C 反向移除、D TCP 下的控制與推播、E 斷線 UI 更新；全數通過；含「`adb pull` 對 `/sdcard/Android/data` 無效」「眼鏡 logcat tag 是 `GlassesDebugReceiver`」「截圖前必查前景」三個方法坑） |
| `scripts/ota_kernel_diff.py` | **比較兩版 kernel 的 `remove_waiter()`**：符號定位 → Capstone 反組譯 → **逐位元組比對** → 輸出函式相對偏移清單（呼叫目標符號化）。用於獨立複核 CVE-2026-43499 的 backport；實測兩版同為 `0x28c`／163 條指令、只差 `+0x040` 一條 |
| `scripts/gen_redacted_pdf.py` | **把 `docs/ghostlock-before-after-redacted.md` 產生成寄給 Rokid 的 PDF**（weasyprint）。改 .md 後必須重跑，避免 PDF 與 .md 不同步（曾發生 PDF 仍留 `[待補]`）。排版要點已寫在檔頭 |
| `scripts/check_index_state.sh` | **索引狀態檢查／修復**（`--fix` 把被驗證流程改動的專輯名稱修回 `Camera`；並驗證索引／磁碟／手機三方筆數一致）。用於驗證中斷後的善後，唯讀模式可安心先跑 |
| `scripts/verify_index_resilience.sh` | **眼鏡媒體索引雙向自我修復驗證腳本**（A 壞 album 容錯／B 磁碟有索引沒有→補登／C 索引有磁碟沒有→移除／D WiFi 下的 push／E 關閉通道通知）；只動自建測試檔並先備份索引，可重複執行，產出逐字稿到 `reports/` |
| `deploy/buddy_watch.sh` | `/proc/buddyinfo`、`/proc/meminfo` 取樣（KernelSnitch mm leak 診斷） |

提權維運層（P0–P3，2026-09-13）：

| 檔案 | 角色 |
|------|------|
| `android/app-glasses/…/ghost/RootDaemonKeeper.kt` | root daemon 看守（30 秒）：stale socket 清理、心跳探針、自動重建（5 分鐘冷卻）、狀態回報 |
| `android/app-glasses/…/ghost/AutoGhostlockEngine.kt` | 執行期引擎探測／委派（Shizuku ↔ bridge），失敗自動換線 |
| `android/shared/…/protocol/RootState.kt` | `RootStates` 狀態代碼＋`RootMetrics` 提權度量 |
| `android/app-phone/…/ui/RootText.kt` | root 狀態／提示／度量 → 手機語系文字 |
| `deploy/install_root_keepalive.sh` | 在任何已 root 的情況下補裝 keepalive＋OOM 保護（deploy 路徑用） |

## 2.2 外部交付物（不在本工作區）

| 交付物 | 位置 | 說明 |
|--------|------|------|
| **ZeroLightRokid Desktop**（Windows GUI 工具） | `C:\Users\<USER>\Documents\codebase\ZeroLightRokidDesktop`（Linux 開發副本 `~/ZeroLightRokidDesktop`） | Win32 C 單一 exe，把本專案的 root 流程包成按鈕；14 語系（比照 `app-phone`）。**研究產出保留在本工作區**：實機測試報告見 `reports/zero_light_desktop_test_report.md`、截圖見 `reports/zero_light_desktop_ui/`。工具本身獨立版控，不併入 `rokid_root/`（語言／建置系統與 Gradle 工程不同）。 |

## 3. 已確認的「擺放不邏輯」項目（冗餘副本）

以下為**位元組完全相同**的重複檔（根目錄 vs canonical 位置），屬歷史操作遺留：

| 根目錄檔案 | canonical 位置 | 狀態 |
|-----------|---------------|------|
| `led_test.sh` | `scripts/led_test.sh` | 完全相同（md5 一致） |
| `pathd_test.sh` | `scripts/pathd_test.sh` | 完全相同 |
| `run_led_orchestrator.sh` | `scripts/run_led_orchestrator.sh` | 完全相同 |
| `run_pathd_orchestrator.sh` | `scripts/run_pathd_orchestrator.sh` | 完全相同 |
| `led_test.log` | `reports/led_validation/led_test.log` | 完全相同（md5 一致） |
| `pathd_test.log` | `reports/led_validation/pathd_test.log` | 完全相同 |

其他位置待調整者：
- `all_pkgs.txt`（裝置套件清單）→ 建議 `analysis/device_data/all_pkgs.txt`。
- ~~`rokid_root_20260904_backup.zip`（24M）→ 移出專案~~ **已完成**（目前不在根目錄）。
- ~~`wm_work/`（35M）→ 建議 `reports/media/`~~ **已完成**（內容已併入 `reports/media/`）。

## 4. 兩個揭露目錄的關係（易混淆，非重複）

- `RV101_Disclosure/`：給 **Rokid 廠商**的協調式揭露（完整證據鏈、時間線、CVE 關係評估）。
- `HITCON_ZeroDay_RV101/`：給 **HITCON ZeroDay 平台**的投稿件（redacted 附件、風險註記、投稿信）。
兩者交付對象與紅線不同，**並存是正確的**；各自有 `00_README.md` 說明。

## 5. 清理執行紀錄（2026-09-11 已執行）

```bash
# ✅ 已執行：刪除根目錄冗餘副本（canonical 在 scripts/ 與 reports/led_validation/；md5 已驗證相同）
rm -f led_test.sh pathd_test.sh run_led_orchestrator.sh run_pathd_orchestrator.sh led_test.log pathd_test.log
# ✅ 已執行：刪除冗餘壓縮檔（tools/jadx/ 已解壓，內容等價）
rm -f tools/jadx.zip
# ✅ 已執行：歸位
mv all_pkgs.txt analysis/device_data/
mkdir -p reports/media && mv wm_work/* reports/media/ && rmdir wm_work
# ⏳ 待手動（沙箱無法寫出工作區外）：把壓縮備份移出專案
mv rokid_root_20260904_backup.zip ~/backups/     # ✅ 已完成（根目錄已無此檔）
```

> 結果：根目錄由 26 項降為 19 項；**2026-09-13 23:xx 複查為 23 項**——增加的是 `dist/`（APK 交付物）、
> `binaries/`（搶救二進位）、`build-cache/`、`.zig-cache/`（快取／暫存），皆為後續工作產生，非舊冗餘副本回歸；
> 根目錄**檔案**仍只有 4 個（`.gitignore`、`MANIFEST.md`、`README.md`、`README_ORGANIZED.md`），無散落檔。
> 文件中的引用皆指向 canonical 位置（`scripts/…`、`reports/led_validation/…`），未受影響。
