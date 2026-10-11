# GhostLock RV101 — 工作狀態交接 v7（第七任 → 第八任）
> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。


> 更新於 2026-08-19。本輪（第七任）自 v9.26 接手，歷經 v9.27→v9.30 共 4 版。
> **本輪成功定位並修復了困擾前幾任的「W1 寫入未落地」根因（兩大位址計算錯誤），但修正後的首次實測（run 51）在 punch 1 階段意外中斷。**
> 工作目錄：`C:\Users\<USER>\Documents\codebase\rokid_root`
> 源碼：`source_code\ghostlock_rv101\`（v9.30，binary md5 `478D10A064D21F70DD226C5E9E8BAD10`，裝置已同步）
> 目標裝置：`<DEVICE_SERIAL>`（RV101 / RG_glasses）

## 1. 總結論（一句話）

**前幾任的「W1 寫入未落地」是因為 `target_rv101.h` 的 symbol offset 與 `data_addr` 的物理基址算錯（共兩處致命 Bug）；本輪透過反組譯與 `kallsyms` 修正後（v9.30），寫入位址已精準命中 `selinux_state`，但隨即引發 run 51 在 punch 1 階段 kernel panic/重開機，需確認是否為 8-byte 寫入原語破壞了 `selinux_state` 結構體後續的 flag 欄位。**

---

## 2. 本輪核心成果：定位並修復兩大位址錯誤

### 2.1 錯誤一：Symbol Offset 多了 0x28000000（v9.29 修復）
*   **現象**：run49 (v9.27) 拿到 STRICT child_pc=OK，證明 `rb_erase` 執行了，但 `enforcing` 仍為 1。
*   **根因**：`target_rv101.h` 中 `SELINUX_STATE_OFF` 設為 `0x2aca0140`，但 `kallsyms` 顯示 `ffffffc00aca0140 B selinux_state`。正確 offset 應為 `0x02ca0140`（即 0x0aca0140 - 0x08000000）。所有 W1/W2 鏈上的 symbol（init_task, init_cred 等）全錯了 `+0x28000000`。
*   **修復**：v9.29 依 `kallsyms` 修正四個 offset。

### 2.2 錯誤二：實體記憶體基址多加 0x800000（v9.30 修復）
*   **現象**：run50 (v9.29) 修正 offset 後，enforce 仍為 1。
*   **根因**：`P0_KERNEL_PHYS_LOAD` 被設為 `0x80800000`。但經反組譯 `kaslr_early_init`、解析 ARM64 Image Header（發現 `text_offset = 0x0`，因 CONFIG_RELOCATABLE）、並讀取裝置 `/proc/zoneinfo`（`start_pfn=524288` → `memstart=0x80000000`）後，證實核心實體載入基址為 `0x80000000`。舊值讓寫入位址偏移了 8MB。
*   **修復**：v9.30 將 `P0_KERNEL_PHYS_LOAD` 改回 `0x80000000`。此時 W1 target 終於指向真實的 `selinux_state` direct-map 別名（`0xffffff8002ca0140`）。

### 2.3 其他靜態驗證
*   `selinux_init` 反組譯證實：`enforcing` 確實在 `selinux_state + 0`，且 `memset(state, 0, 0x88)`。GKI_struct_selinux_state (`0xaca3628`) 零引用，為死的 ABI 佔位。
*   `rb_erase` case-A 寫入幾何確認：必寫 `target+0`。

---

## 3. 當前阻塞點：run51 的致死之謎

在 v9.30（位址完全正確）下執行 run51（W1 模式，3 cycles），**3 個 cycle 全部在 punch 1 死亡（exit=255）**，日誌截斷於 `consumer punch 1/5`。

### 3.1 死因推演（兩大嫌疑）
1.  **嫌疑 A：8-byte 寫入塗抹導致結構體腐蝕（頭號嫌疑）**
    *   `rb_erase` 的寫入原語是 8-byte 指標寫入。
    *   `selinux_state` 的 `enforcing` 只佔 1 byte（offset +0）。
    *   寫入 8 bytes 會把指標的垃圾值覆蓋到 `state+1` 到 `state+7` 的欄位。
    *   根據 `selinux_init` 反組譯，`state+1` 是 `checkreqprot`，`state+2~+7` 可能是 `initialized`、`disabled`、`enabled` 等 bool 欄位。
    *   若 `disabled` 被寫成非零值，核心下一次存取 SELinux 狀態（如 avc 檢查）時，可能觸發 runtime disable 路徑或解引用錯誤指標，導致立即崩潰。
2.  **嫌疑 B：vmap purge flood 競態（舊問題殘留）**
    *   先前的 run 39/41/45 也曾因 gate flood 導致 leak slab 被重分配為 kernel stack，walk 寫入活 stack 致死。
    *   run51 的 3 次死亡可能是 1/3 機率的 flood-death 連續發生（機率約 4%）。

### 3.2 判讀基準
*   **Punch-1 死亡日誌特徵**：停在 `consumer punch 1/5 (+20000us) tid=T`，無 `returned` 行，adb 斷線。

---

## 4. 留給第八任的待辦（按優先序）

### 4.1 頭號任務：區分 run51 死因
*   **做法**：執行 W1 模式，但透過 `GL_TARGET_DELTA` 環境變數將寫入目標偏移到一個「無害」的 RAM 位置（例如之前錯誤的位址 `0xffffff80034a0140`）。
*   **判據**：
    *   若仍 3/3 死亡 → 代表是 flood-death（與目標位址無關）。
    *   若 0/3 死亡（乾淨完成但 enforce 未翻轉）→ 代表是 **8-byte 塗抹導致結構體腐蝕**。

### 4.2 第二任務：解決 8-byte 塗抹問題（若確認為嫌疑 A）
*   `rb_erase` 無法只寫 1 byte。
*   **對策 A**：放棄 W1（SELinux），直接推進 W2。W2 的目標是覆蓋 task 的 `cred` 指標為 `init_cred`。指標本身就是 8 bytes，不會有塗抹問題。
*   **對策 B**：修改 `write_value`（目前為 `page_base + SAFE_OFF`），使其 upper bytes 符合 `selinux_state` 後續欄位的合法值。但 `page_base` 是核心指標（`0xffffff80...`），bytes 4-7 必為 `0xff`，無法匹配全為 0 的 bool 欄位。

### 4.3 第三任務：W2 路徑修復
*   目前 `main.c` 的 W2 流程使用 `perf_find_task()` 來定位 child task_struct。
*   **問題**：裝置 `perf_event_paranoid=3`，perf 被完全阻擋（EACCES）。
*   **對策**：改用 V1 交接提到的 configfs/FOPS 路由，或透過讀取 `/proc` 檔案來定位 task_struct。

---

## 5. 環境與工具

| 項目 | 狀態/路徑 |
|---|---|
| **現行 Binary** | v9.30 (md5 `478D10A064D21F70DD226C5E9E8BAD10`) |
| **裝置狀態** | 需重開機確保乾淨狀態（uptime > 150s） |
| **反組譯工具** | `scripts/rv101_disasm.py <file_offset> <len>` (需 python3 + capstone) |
| **跑批腳本** | `& scripts/ghostlock_reboot_run.ps1 -Cycles 3 -Env "GL_SKB_PAD=64 GL_DEBUG_DRAIN=1 GL_DUMMY_KILLS=180" -ExploitArgs "--write1" -Log runNN` |
| **KASLR Leak** | 已驗證正常（VA slide >= 64GB，純虛擬，不影響 data_addr 別名） |

## 6. 歷史 run 矩陣摘要

| Run | 版本 | 配置/特徵 | 結果 |
|---|---|---|---|
| 48 | v9.27 | selftest | STRICT 簽章通過（payload 落地成功）。 |
| 49 | v9.27 | W1 (錯誤 offset) | STRICT child_pc=OK，但 enforce 仍為 1。發現 offset bug。 |
| 50 | v9.29 | W1 (修 offset, 錯 phys) | STRICT child_pc=OK，enforce 仍為 1。發現 phys bug。 |
| 51 | v9.30 | W1 (全修正) | 3/3 punch-1 死亡。疑似 8-byte 塗抹致崩或 flood。 |
