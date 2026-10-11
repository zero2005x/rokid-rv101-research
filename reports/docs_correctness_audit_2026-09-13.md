# 文件正確性稽核報告（全工作區，2026-09-13）

> English: [docs_correctness_audit_2026-09-13_EN.md](docs_correctness_audit_2026-09-13_EN.md)


- 範圍：`README.md`、`MANIFEST.md`、`README_ORGANIZED.md`、`docs/*.md`（15）、`reports/*.md`（97）
- 方法：4 個子代理分片深查 70 份歷史研究報告 ＋ 2 個子代理查核權威文件的路徑／指令／數字／結構 ＋
  本人對核心文件與實作做自動化一致性檢查。
- 原則：**日期化的當期紀錄本身不是錯誤**；只有「會誤導今天動手的人」或「文件內部自相矛盾／引用失效」才需修正。

---

## 1. 已修正（依嚴重度）

### 1.1 會讓人照錯做（最高）
| 文件 | 問題 | 處置 |
|------|------|------|
| `docs/deploy.md` §9.3、故障排除、`docs/device-debug-notes.md`、`deploy/root_bridge.sh`／`setup_direct.sh` 輸出 | 教人改 `USE_ENGINE`／`USE_BRIDGE` 再重建 APK | 改為**執行期自動探測**說明；腳本訊息同步 |
| 6 份報告（`CVE-2024-38401_Test_Report`、`CVE-2024-36971_analysis`、`CVE_PROBE_REPORT`、`RESEARCH_FINDINGS`、`Rokid_Attack_Vector_Summary`、`CVE_5_10_x_Scan_Report`、`PRIV_ESC_SESSION_10`、`OTA_Authentication_Analysis_Report`、`SESSION_16`、`NEW_CVE_ANALYSIS`、`ADB_Media_Exfiltration_Report`、`Rokid_Attack_Vector_Summary`） | 仍推薦 **Fastboot／Magisk／刷 boot.img** 為「最可靠 root 路徑」 | 全部加註「路線不可行；root 已由 GhostLock LPE 取得」 |
| `docs/protocol.md` | **完全未記載 `SET_RING`** | 補上；並更新 `ENABLE_WIFI_ADB` 欄位（`persistPort`/`autoAfterReboot`） |
| `docs/root-from-zero-guide.md` | 教人在 `get_root_now.sh` 改 `ADB=`/`SERIAL=`（已集中到 `deploy/env.sh`） | 改為 env 覆寫寫法 |
| `reports/GHOSTLOCK_HANDOFF_V2` | **「RV101 沒有 VMAP_STACK」為事實錯誤**（實機 `CONFIG_VMAP_STACK=y`），使 §2.2 機制不成立 | 標為錯誤並指出正確 config |
| `reports/VULN_VERIFICATION_INDEX` | 開頭稱「已確認檔案存在」，實測 **26/70 個名稱不在工作區** | 加複查表：16 個僅二進位缺（.c 仍在）、10 個原始碼也缺 |

### 1.2 對外揭露文件（Rokid 信件／英文報告）
| 文件 | 問題 | 處置 |
|------|------|------|
| `Rokid_Glasses_Security_Report_EN.md`、`Rokid_Glasses_資安漏洞報告.md` | 「SELinux Enforcing／Root Access: None」與現況不符；「Over 6 Months」與同節 19 個月矛盾 | 加狀態更正橫幅＋逐列更正 |
| 同上＋`email.md`／`Rokid_漏洞報告信件.md` | CVSS 標籤矛盾（8.6 標 Critical、7.5 vs 4.6）；90 天期限日期算錯一日（應 2026-05-20） | 標註待統一；日期已標 |
| `Rokid_Glasses_系統資料報告(.md/_Marp.md)` | 「驗證開機 enforcing」與同檔「orange/unlocked」矛盾；感測器 16 但表列 8 | 更正為 orange＋標註表為子集 |

### 1.3 內部自相矛盾（同一份文件前後打架）
| 文件 | 矛盾 | 處置 |
|------|------|------|
| `GHOSTLOCK_HANDOFF_V10` | 表格 W1/W2 標 ❌，附錄 E/AA 已驗證 ✅ | 表格更正＋待辦標「已完成勿重做」 |
| `GHOSTLOCK_V5_RECLAIM_MECHANISM` | 「payload 在位」vs V6 證明差 64 B | 加更正註 |
| `KGSL_RESEARCH_REPORT` | GPUMEM_ALLOC「✅ 成功」vs 實測 ENOTSUP | 標註停用並指向實測報告 |
| `CVE-2024-36971_攻堅分析報告` | `CONFIG_RCU_FAST_NO_HZ=n`（實為 y） | 更正 |
| `PASS_TABLE_EVIDENCE` | 「15 列」實為 10 列；`binder_uaf_v2~v5` 實際只有 v5 | 更正 |
| `ATTACK_SURFACE_RESEARCH` | 行數表過時（415→704 等） | 更正並註明量測日期 |
| `zero_light_desktop_test_report` | i18n 191/2674、149/44、8 核 | 以桌面工具實測更正：**228/3192、134/28/26、4 核** |
| `kernelsnitch_mm_leak_clean_boot_2026-09-13` | 樣本數、MemFree、keepalive 秒數、外部路徑 | 更正（路徑指向桌面工具專案、樣本 23） |
| `5.2_VERIFICATION_2026`、`CVE_5_10_x_Scan_Report` | 列數 16→15；報告年份 2025→2026 | 更正 |
| `KGSL_FINAL_REPORT` | 「ERESTARTSYS (524)」實為 ENOTSUPP | 更正 |

### 1.4 歷史定位標註（14＋27 份）
- 14 份自稱現況但已過時的報告（`FINAL_STATUS_20260202`、`NEXT_AGENT_PROMPT*`、`便紀錄`、`PRIVESC_REPORT` 等）→ 加「歷史文件（非現況）」橫幅。
- `ROOT_PROCEDURE.md`、`Rokid_Root_Guide_boot_img.md`、`CURRENT_STATUS_20260822.md`、兩份交接 prompt、`GHOSTLOCK_ROOT_操作說明.md`、`GHOSTLOCK_HANDOFF_V8.md`、`shizuku-feasibility-report.md`、`README_ORGANIZED.md`、影片製作 spec → 各自加「已被取代／外部專案／路徑已變更」註記。

---

## 2. 跨檔矛盾清單（尚未全面消解，供後續統一）

> 這些是**不同報告對同一事實給出不同答案**。本次僅在個別檔案加註，未逐條改寫；建議以「實測報告優先」原則統一。

| # | 主題 | 不同說法（檔案） | 建議權威來源 |
|---|------|------------------|--------------|
| 1 | CVE-2024-36971 修補版本 | 5.10.219（`深度攻堅報告_v2`、`analysis`、`5.2_VERIFICATION`）vs **5.10.220+**（`CVE_5_10_x_Scan_Report`） | 以掃描報告＋線上 CVE 記錄為準 |
| 2 | CVE-2024-36971 可利用性 | 「~1–5%」／「需驗證」／「已驗證受影響」vs **「不可利用（無 routes/ICMP）」**（`PRIV_ESC_SESSION_10`、`5.2_VERIFICATION_LIVE_ADB_2026` §1.3） | 實測（LIVE_ADB）為準 |
| 3 | CVE-2024-0582 | 「影響 5.10–6.4／可能未修」vs「PBUF_RING 需 ≥5.19，本機不可達」 | 後者（實測 ioctl EINVAL） |
| 4 | `msgsnd` spray | 多份報告建議 vs `CONFIG_SYSVIPC` 未設 | 後者（實機 config） |
| 5 | GhostLock symbol offsets | V3（`0x2aca0140`）vs V7/V11（`0x02ca0140`、`0x02202058`） | V7/V11（較新） |
| 6 | `data_addr` 別名 | V7「不影響」vs V10「對應錯 PA，須改 `text_addr`」 | V10（實證） |
| 7 | panic 抓取方式 | V3「無 ramoops，只能 dmesg -w」vs V11「ramoops 已配置、console=ttyMSM0」 | V11 |
| 8 | miscdevice fops 偏移 | V3「+0x18」vs W2 blocker「+0x10（0x29bcbc0）」 | W2 blocker |
| 9 | reclaim uptime 門檻 | V7「>150s」vs W2 blocker「>~400s」 | W2 blocker |
| 10 | GateServiced 錯誤傳遞／能力 | 「所有命令回 300c」vs「timeout=error」；能力「幾乎全部」vs「4 個具名」 | 需一次實測釘死 |
| 11 | i2c-1 身分 | 信件「0x08 PMIC／0x30 display」vs LED 稽核「0x30 = AW2110x LED／0x08 = PSOC-TP-R」 | LED 稽核（sysfs 實證） |
| 12 | CVE-2025-21479/21480 歸屬 | 「cross-FD＝21479」vs CVE 記錄「micronode」；21480 Android 適用性未證 | CVE 記錄＋`5.2_VERIFICATION` §2.6 |
| 13 | CVE-2023-2163 狀態 | 「可能未修補、建議優先」vs「已修補（≥5.10.179）」 | 後者 |
| 14 | 裝置／韌體世代 | 1.12.009／kernel `gb80a8319d11b` vs **1.22.009／`gd908d71a0404`** | 後者（現行）；前者報告應標世代 |
| 15 | KASLR／kernel exploit 可行性 | 「無法繞過／kernel exploit 不可行」vs 現行可行（leak→root 86–155s） | 現況 |
| 16 | USB config 值 | `adb` vs `adb,diag` | 需實測 |
| 17 | GHOSTLOCK V5 vs V6 | 「可重現 2/2」「pad 64」「v9.19」vs 「一次都沒重現」「block+0」「v9.60」 | V6/V13 |

---

## 3. 引用失效與缺失成品（系統性）

| 類型 | 說明 |
|------|------|
| **編譯產物未納版控** | `binaries/`、`source_code/ghostlock_rv101/build/`、`binaries/device_probes/`、`cve_scanner`、`bpf_kaslr_exploit`、`io_uring_*`、`tipc_*`、`ashmem_*`、`msm_drm_probe` 等 **~51 個二進位**皆不在工作區（`.c` 多在 `source_code/`，可重建） |
| **未保留的原始碼** | `binder_uaf_v2~v4.c`、`cve_2024_36971_v2~v12.c`、`kgsl_bpf_spray_v3~v6.c`、`kgsl_exploit_v7/v8.c`、`ashmem_overflow.c`、`cve_scanner.c` 等 |
| **外部／主機產物** | `RVE01-ota-*.zip`、`payload.bin`、`magisk_patched-122_009.img`、`update_engine3.bin`、`msm_kgsl.ko`、`vendor_extracted/`、`RideFlux/`、`recovery_root/`、`assets/`（已移除）、`RV101_Edit_Project/`（桌面剪輯專案） |
| **雜湊無法複核** | 各報告的 binary md5/sha1（V4 `01bd9e97…`、V5 `3CCAAAB6…`、V11 sha1s、V13 sha1s）、`init.rokid.rc` SHA-256 |
| **已標註** | `VULN_VERIFICATION_INDEX` 加註 26 項缺失分類；`MANIFEST` 已更正計數與體積；`deploy` 腳本索引補齊 25 支 |

---

## 4. 無法驗證（需裝置或外部資源）

- 裝置端執行期數值（ioctl 回傳、port 8341、sysfs、PID、`/proc` 內容、GPU 計數器）——本次未接裝置複核。
- 外部 URL／CVE 記錄（NVD、CodeLinaro、GitHub、社群貼文）與其細節（修補日期、commit、CVE 範圍）。
- 已下線成品（見 §3）與其雜湊。
- 桌面工具報告的「x64 813 KB／helper 1957 bytes」等與現行檔案不符的數字（現行 exe 900,697 B）。

---

## 5. 一致性複查（自動化，全綠）

| 檢查 | 結果 |
|------|------|
| i18n：`phone_keys.tsv` 193 key，14 語系各 284 條字串 | ✅ 零缺漏 |
| `protocol.md` 涵蓋 37/37 訊息型別、26/26 `Status` 欄位 | ✅ |
| 底部導覽 4 分頁、相簿/本機 3 欄、`USE_ENGINE` 無生效引用 | ✅ |
| 建置指令、模組名、JDK 路徑、APK 產物路徑 | ✅ 與實際相符 |
| `deploy/*.sh`、`scripts/*.sh` `bash -n`；i18n 驗證器 | ✅ 全通過 |
| `deploy` 腳本索引覆蓋 25 支 | ✅ |
| MANIFEST 引用檔案 23 個 | ✅ 全存在 |

---

## 6. 併行工作線（重要背景）

稽核期間發現**另一條工作線同時在改同一個工作區**（ZeroLightRokid Desktop 工具線）：

- 它新增了 `deploy/lease.sh`、`deploy/proc_util.sh`、`deploy/buddy_watch.sh`、`deploy/test_lease.sh`，並在
  `RootDaemonKeeper.kt` 加入**跨工具共用租約**（`/data/local/tmp/ghostlock_op.lease`）與 TOCTOU 預檢。
- 本報告的 §1 修正是**基於當前檔案內容**；若兩線同時編輯同一檔案，請以最新 mtime 為準重跑本稽核。
- 已把租約機制與新腳本補進 `docs/deploy.md`（P0 章節）與 `docs/architecture.md`。

---

## 7. 後續建議（依價值排序）

1. **統一 17 組跨檔矛盾**（§2）：建議在 `reports/` 增一份 `FACTS.md`（單一權威事實表：韌體世代、CVE 狀態與修補版本、可用 ioctl、symbol offsets、reclaim 門檻），各報告改為引用它。
2. **為每份報告加「世代戳記」**：`fw=1.22.009 / kernel=gd908d71a0404 / 日期`，讓「不同世代的不同結論」不再是矛盾。
3. **成品不入版控的規則寫進 MANIFEST**（§3 已加註）：報告若引用二進位，一律同時寫「如何重建」。
4. **對外文件（Rokid 信件／英文報告）另做一次事實凍結**：CVSS 標籤、SELinux/root 狀態、日期，於公開前逐條核對。


---

# Rev.2（2026-09-13 深夜）— 傳輸架構變更後的再稽核

## 觸發
當日稍晚修好「媒體一直走藍牙」（`reports/wifi_transfer_fix_2026-09-13.md` Rev.1–6）並改了
**傳輸選路、傳輸標示、眼鏡媒體索引**三塊行為 ⇒ 凡描述這些行為的文件都可能過期。

## 方法（可重跑）
1. 連結檢查：`README.md`／`MANIFEST.md`／`dist/README.md`／`docs/*.md` 的 markdown 連結是否存在（21 個連結，0 失效）。
2. 路徑檢查：抽出所有 `` `xxx.sh|py|kt|c|cmd|tsv|apk|json` `` 形式的引用，逐一比對**完整路徑或 basename**是否存在於工作區
   （排除裝置端路徑與外部專案引用）。
3. 程式碼交叉檢查：`Protocol.kt` 的 37 個 `MsgType` 常數是否全部出現在 `docs/protocol.md`。
4. 數字複查：i18n key 數、APK 版本、目錄計數與體積。
5. 指定段落人工複讀：`docs/deploy.md` §9.4、`docs/architecture.md` §1/§4/§7、`docs/protocol.md` §1、`dist/README.md`。

## 本輪修正（會誤導今天動手的人）

| # | 檔案 | 錯誤 | 修正 |
|---|------|------|------|
| 1 | `README.md` §傳輸 | 「控制指令走藍牙 RFCOMM；照片／影片走 WiFi」 | 改為「控制與媒體都優先走 WiFi」＋互斥原因與實測數字 |
| 2 | `README.md` §目錄結構 | 缺 `dist/`、`binaries/`、`build-cache/`（頂層 23 項只列了 20） | 補上三個目錄與「正式證據→`reports/`、暫存→`build-cache/`」原則 |
| 3 | `docs/architecture.md` §1 | 「控制指令走藍牙 RFCOMM；媒體優先走 WiFi」＋架構圖標籤 | 改為 WiFi 優先；補上**通道互斥**警語 |
| 4 | `docs/architecture.md` §4 | `Remote.mediaClient()`／`wifiMode` 的舊描述 | 改為 `link()`／`ensureMediaLink()`／三態顯示／`onTransportChanged` |
| 5 | `docs/architecture.md` §7 | 「WiFi **媒體**通道」 | 改為「WiFi 通道（承載控制與媒體）」＋風險指向 FACTS P10 |
| 6 | `docs/deploy.md` §9.4 | 標題與內容仍是「手動啟用 WiFi 傳輸」、`~60KB/s` | 全節重寫：自動建立、持久化偏好、互斥警語、**實測 3.0–3.8s vs 324s** |
| 7 | `docs/protocol.md` §1 | 「控制指令**一律維持藍牙**」 | 改為兩條通道並列 ＋ 互斥說明 ＋ 選路規則 |
| 8 | `docs/architecture.md`／`deploy.md` | i18n「**193** key」 | 更正為 **203 key**（`validate_translations.py` 現行值） |
| 9 | `dist/README.md` | `app-glasses-v1.0-safe.apk` **重複兩列**、且兩列都寫「目前安裝於眼鏡的是這一版」 | 重寫表格：7 個 APK 各一列、目前版本為眼鏡 `1.0-safe.3`／手機 `1.3` |
| 10 | `MANIFEST.md` §2 | 缺 `dist/`／`binaries/`／`build-cache/`／`.zig-cache/`；體積與檔數過期（4.0GB→4.7GB、reports 293→331…） | 補四列判定 ＋ 全部計數重測；§2.1 補 `1.0-safe.3`／`v1.3` 與兩個新證據目錄 |
| 11 | `MANIFEST.md` §2.1 | 引用不存在的 `add_p2x.py` | 改列實際檔名 |

## 本輪查核後確認「不需修改」者
- `docs/protocol.md` 的訊息表：程式碼 37 個訊息常數**全部**都有記載（含 `ACK`／`HELLO_ACK`）。
- `docs/protocol.md` 的 `PROTO_VERSION=1` 與程式碼一致。
- `docs/sdk-setup.md` 的建置指令（`JAVA_HOME=tools/jdk/jdk-17.0.20.1+1`、`GRADLE_USER_HOME=gradle_home`）
  與本日實際建置方式一致。
- `docs/RV101_RERENDER_SPEC_v2.md`／`VIDEO_PRODUCTION_PLAN_v2.md` 引用的 `design.py`／`story.json` 等
  屬**外部剪輯專案**（文件已明示不在本工作區）；`docs/privacy-audit-report.md` 的 `assets/cei.json`／
  `endpoints.json` 實際位於 `analysis/phone/`；`.ghostlock_root.sh`／`last.cmd` 為**裝置端**路徑。以上皆非失效引用。
- `docs/device-debug-notes.md` 的「通訊改為藍牙」段落屬**歷史紀錄**（該檔開頭已標示歷史），依原則不改寫歷史。

## 執行說明（如實記錄）
本輪原先派出一顆子代理專責 `docs/` 稽核，但該子代理**執行失敗未產出報告**；
上表結果改由本人以「連結／路徑／程式碼交叉／數字」四類自動化檢查 ＋ 指定段落複讀完成。
另有一次**對抗式驗證代理**（針對當晚新增的 FACTS P27–P30）成功產出報告，抓到 3 個真缺口，
已修正並記錄於 `reports/wifi_transfer_fix_2026-09-13.md` Rev.6。
