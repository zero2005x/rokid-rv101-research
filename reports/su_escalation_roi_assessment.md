# 提權程式碼「值得再改寫嗎？」評估報告

- 日期：2026-09-13
- 問題：是否值得花時間改寫提權（root）程式碼，以提高**效率／成功率**？
- 結論：**不值得改寫提權鏈本身；值得花約 2–4 天做「維運層」強化。** 詳見 §5 建議與 §6 決策準則。
- ⚠ **§1–§7 為實作前的評估**（描述當時的痛點，例如 `USE_ENGINE` 編譯期開關）；**§8 是實作完成後的結果回填**，兩者對照閱讀。

---

## 1. 實測數據（本次 + 歷史）

### 1.1 本次實測（2026-09-13，整合鏈現況）

| 步驟 | 實測 |
|------|------|
| 起始狀態 | `root_daemon` 已死（socket 殘留，`nc -U` → Connection refused）、bridge helper 不存在 |
| 一鍵武裝 | `deploy/root_bridge.sh`（啟動 helper 常駐） |
| 觸發 | 手機 App `ACQUIRE_ROOT` → 引擎 SHIZUKU 探測失敗 → **自動回退 bridge helper** |
| 結果 | **84 秒取得 root**：`uid=0(root) … context=u:r:kernel:s0` |
| 後續 | 取 root 後續用 `isRoot()` 快速路徑（`跳過 LPE`），無需再跑 exploit |

> 歷史對照 `deploy/get_root_now.log`（2026-09-07）：boot 1、trigger 1、**112 秒**完成（含 helper 重建 122 秒）。

### 1.2 歷史 runlog 統計（子代理分析 148 份 log）

| 指標 | 數值 | 備註 |
|------|------|------|
| exploit 實際啟動次數 | **143** | 每份 log = 一次 `ghostlock_rv101` 執行 |
| 成功取得 root（`uid=0(root)` 驗證） | **15** | 另有 2 次已 `child is root!` 但未印 uid |
| 未取得 root | **126** | |
| 裝置消失／硬 reset（panic 徵兆） | **27** | 12 份直接 `device not found` + 15 份 punch 窗口死亡 |
| 取得 root 耗時（n=15） | **中位數 101.5 秒**（86.7–154.8） | 從 KASLR leak 到 exploit complete |
| 首次嘗試即成功 | 9/15 次執行 | 其餘 6 次第 2 次嘗試成功（run 內建重試） |
| root 存活最長證據 | **≥43 分鐘**（`run_w2_v960_143215.log`，未重開機、仍 Permissive） | |

**失敗分類（n=126）**：

| 類別 | 次數 | 能否由 App 層改寫改善 |
|------|------|----------------------|
| verifier gate miss／reclaim 失敗（核心競態） | **80** | ❌ 屬 exploit 研究（v9.x reclaim gate），非程式結構問題 |
| kernel panic／reboot | **21**（專案累計 27） | ⚠ 只能「減少無謂重試」，無法消除 |
| timeout | 0 | — |
| Shizuku 未執行／未授權 | 0（148 份 log 完全沒提到 Shizuku） | — |
| 行程被 OOM 殺 | 0 | — |
| 其他／不明 | 12 | — |

> ⚠ **15/143 = 10.5% 不是可用的成功率**：這 148 份 log 混合了 5 條工作線（W2/FOAT primitive 開發、selftest、版本掃描 v9.30→v9.60），分母各異且為人工挑選。**整合後的操作流程反而是首發成功**（§1.1 兩次實測皆一次成功）。

---

## 2. 時間都花在哪裡？

```
取得 root 總時間 ≈ 84–112 秒
├─ 引擎探測／回退（Shizuku → bridge）         < 5 秒     ← App 層，已足夠快
├─ helper 觸發 + exploit 核心競態（grooming）  ~80–100 秒  ← exploit 本質，改 App 不會變快
└─ root_daemon 啟動 + 驗證                    < 5 秒     ← App 層
```

**App 層的可優化空間 < 10%**，其餘是不可壓縮的核心 exploit 時間。也就是說：**寫程式碼改不出效率**。

---

## 3. 真正的痛點（實測到的，不是猜測）

| 痛點 | 證據 | 影響 |
|------|------|------|
| **每次重開機都要 adb 重新武裝** | helper 與 Shizuku 都必須以 shell 身分常駐；App（app uid）無法自行啟動 | 沒有 PC 就沒有 root → 靜音拍攝／隱私燈／WiFi 媒體／戒指路徑 B 全失效 |
| **root 會在 session 中消失** | 本次 00:37 前 `nc -U` 為 Connection refused（socket 殘留、process 已死）；App 沒有 daemon 看門狗（ring control **有** 30 秒看門狗，root 沒有） | 功能靜默降級：`✗ 靜音設定失敗 silent mode failed (root required?)` |
| **引擎選擇是編譯期常數** | `AppGraph.kt:135  private val USE_ENGINE = Engine.SHIZUKU` | 換環境要改程式 + 重建 APK（執行期僅有 Shizuku→bridge 單向回退） |
| **沒有度量** | 無成功／失敗／耗時紀錄；`deploy/watch_root.log` 根本不存在 | 無法用數據判斷是否該投入 exploit 研究（本報告 §1.2 只能靠人工撈 log） |
| **失敗代價不對稱** | 27/143 次執行造成裝置重開機 | 每次 panic ≈ 重開機 1–2 分 + 重新武裝 + 全部執行期狀態遺失 |

---

## 4. 為什麼「改寫提權鏈」的期望收益低

1. **效率**：84–112 秒已接近下限（§2），App 層佔比 < 10%。
2. **成功率**：失敗主因是核心競態（80/126）。App 層重寫無法改變 kernel 的 reclaim 時序命中率——那是 v9.x 研究線（`GHOSTLOCK_V3_PANIC_ROOTCAUSE_ANALYSIS.md` 記載的 reclaim gate miss、fake page panic）在處理的問題。
3. **重構風險**：目前鏈路已有多項**正確但容易被誤刪**的設計（用 `/proc/net/unix` 判 listener 而非 `[ -S ]`、`setsid` 脫離、`isRoot()` 真的執行 `id` 驗證、Shizuku→bridge 回退）。重寫等於把這些踩過的坑再踩一次。
4. **跨重開機的持久 root 不可行**：需要 boot 映像／Magisk，而 `ROOT_PROCEDURE.md` 記載 boot.img 取不到（多條路徑皆失敗）→ 任何「改寫」都無法消除「每 boot 一次 adb」。

---

## 5. 建議（依 ROI 排序）

| 優先 | 工作 | 工作量 | 預期效益 |
|------|------|--------|----------|
| **P0** | **root daemon 存活強化**：看門狗（比照 ring control 的 30 秒迴圈）＋ 失效自動重建（若 helper/Shizuku 仍在）＋ stale socket 清理 ＋ `oom_score_adj` 保護 | 1–2 天 | 直接消滅「功能突然失效」與本次實測到的死亡案例 |
| **P0** | **失敗可視化**：手機端明確顯示「root 已失效／無法自動武裝，請跑 deploy/start_shizuku.sh 或 root_bridge.sh」（i18n 代碼 `gl_shizuku_not_running` 等**已存在**） | 0.5 天 | 不再靜默降級；操作者知道下一步 |
| **P1** | **執行期引擎探測**：Shizuku 可用？bridge helper 常駐？direct shell？有序選擇並回報原因，移除 `USE_ENGINE` 編譯期開關 | 0.5–1 天 | 免重建即可換環境；失敗原因明確 |
| **P2** | **提權度量**：記錄每次嘗試（引擎／開始時間／耗時／結果分類）成環狀緩衝並回報手機 | 0.5 天 | 這是「未來要不要碰 exploit」的唯一判準依據 |
| **P3** | **免插線重新武裝**：root 期間一次性 `setprop persist.adb.tcp.port 5555`，之後 PC 用無線 adb 即可重新武裝 | 0.5 天（需實測此 build 是否支援） | 免 USB；失敗則照舊（不可盲目重啟 adbd——已記載會掉 USB 列舉） |
| **不做** | 重寫 LPE／exploit 整合、payload 時序、reclaim 閘門 | 數週研究 | 失敗主因在核心競態，不是程式結構 |

**唯一值得的 exploit 側小改**：**panic 規避**。若連續 N 次 gate/寫入 miss 就提前中止（而不是繼續 punch 到硬 reset），可把 27 次裝置重開機的一部分換成「乾淨失敗」。這不是提高命中率，而是**降低失敗代價**，可併入 P0/P1 一起做。

---

## 6. 決策準則（用 P2 的數據判斷）

| 觀察到的情況 | 行動 |
|--------------|------|
| 失敗以「helper/Shizuku 未常駐、daemon 死亡」為主（>70%） | 只做 P0–P2，**完全不碰 exploit** |
| 首發成功率 ≥ 60%、panic < 10% | 同上（現況證據偏向這一側） |
| 首發成功率 < 50% 且 panic > 20%、且 P0–P2 已完成 | 才值得投入 exploit 研究（reclaim gate／時序），且以「週」為單位編列，不是重構 |

---

## 7. 一句話回答

> **不要改寫提權程式碼。** 效率已到下限（84–112 秒，App 層佔 <10%），成功率瓶頸在 kernel 競態而非程式結構；真正在流失的是「每 boot 需 adb 武裝」「daemon 中途死亡」「失敗靜默」這三件維運問題——花 2–4 天做 P0–P2（其中 i18n 錯誤代碼與 ring 看門狗模式都已經現成）比重寫提權鏈的期望收益高一個數量級。

---

## 附錄：本報告的證據來源

| 來源 | 內容 |
|------|------|
| 本次實測（2026-09-13 00:37–00:39） | `root_bridge.sh` → `ACQUIRE_ROOT` → 84 秒取得 root；`nc -U` 驗證 `uid=0(root)` |
| `deploy/get_root_now.log` | 2026-09-07：boot 1 / trigger 1 / 112 秒 |
| `reports/ghostlock_runlogs/*.log`（148 份，子代理分析） | 143 次啟動、15 次驗證 root、27 次裝置消失、耗時中位數 101.5 秒、失敗分類 |
| `reports/GHOSTLOCK_V3_PANIC_ROOTCAUSE_ANALYSIS.md` | panic 為硬 reset、無 oops；reclaim gate miss 根因 |
| `reports/ROOT_PROCEDURE.md` | boot.img 取不到 → 持久 root 不可行 |
| `AppGraph.kt:135`、`RootDaemonClient.kt:21-30`、`LedPayloads.kt`（root_daemon 啟動） | 編譯期引擎選擇、健康檢查、`setsid` 與 listener 判斷等現有設計 |

---

## 8. 執行結果回填（2026-09-13 實作完成）

| 項目 | 狀態 | 實測證據 |
|------|------|----------|
| **P0** root daemon 存活強化 | ✅ 完成 | root 側 keepalive **9 秒**救回被殺的 daemon（App payload 路徑）、**18 秒**（deploy 路徑）；daemon 與 keepalive `oom_score_adj=-1000`；stale socket 自動清除 |
| **P0** App 側看守 | ✅ 完成 | 失效且 helper 心跳新鮮時**全自動重建**，實測 **147 秒**取回 root（無任何人工介入） |
| **P0-b** 失敗可視化 | ✅ 完成 | 真實重開機後手機顯示「root：已失效，需重新武裝／請執行：deploy/root_bridge.sh 或 deploy/start_shizuku.sh／看守 N 次」 |
| **P1** 執行期引擎探測 | ✅ 完成 | 移除 `USE_ENGINE`；改用心跳檔（helper）＋`Shizuku.pingBinder()`（Shizuku）；失敗自動換線重試 |
| **P2** 提權度量 | ✅ 完成 | `acquireRootMetered()` 記錄引擎／耗時／成功數並**存 prefs**（App 被殺不歸零），隨 STATUS 回報手機 |
| **P3** 免插線武裝 | ❌ **放棄（有證據）** | `persist.adb.tcp.port` 可設且跨重開機保留，但**本 build 的 adbd 不綁 TCP**（同 session 與開機後皆 0 listener）→ 平台不支援，非程式問題 |

### 執行期間遇到的真實失敗（正好印證評估）

- 第一次重試提權時遇到 **kernel panic 硬重置**（`[wait] 裝置離線，視為 reboot`，裝置自動重開機），
  下一個 boot 的第 2 次 trigger 才成功 → 對應評估 §1.2 的失敗分類（panic 21/126）。
- App 端原本用 `ps` 判斷 helper 是否常駐**永遠失敗**（Android 限制 App 看不到其他 process）→ 改讀心跳檔後才正確，
  這也解釋了為何「自動重建」必須以檔案為基礎做探測（已寫入 `docs/deploy.md`）。
