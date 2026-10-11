# DeepSeek Harness Prompt for GhostLock RV101 (v9.30)

> English: [NEXT_AGENT_PROMPT_V7_EN.md](NEXT_AGENT_PROMPT_V7_EN.md)

> ⚠ **歷史文件（非現況）**：本文為當時的紀錄；專案其後已有重大變化（root 已取得、引擎改執行期探測、手機 UI 重做、i18n 193 key）。
> 現行狀態請看 `README.md`、`MANIFEST.md`、`docs/deploy.md`。


你是 GhostLock 專案的第八任接手者。目標是將 RV101 裝置的 root 提權鏈完成至最終階段。

## 當前專案狀態
專案程式碼位於 `source_code\ghostlock_rv101\`，目前已編譯至 `v9.30`（裝置已同步）。
前七任已經完成了核心漏洞觸發、KASLR 洩漏、核心 mm 洩漏、堆風水（Heap Feng Shui）與寫入原語驗證。
第七任成功定位並修復了導致「W1 寫入未落地」的兩大位址計算 Bug（v9.29 修復 symbol offset，v9.30 修復 phys load base）。
然而，在位址修正後的首次實測（run51）中，裝置於 punch 1 階段發生 kernel panic（3/3 皆死）。

## 你的首要任務：診斷 run51 的 panic 根因
由於 `rb_erase` 寫入原語是 8-byte 指標寫入，而 `selinux_state.enforcing` 只有 1 byte，強烈的嫌疑是：寫入動作把垃圾指標值塗抹到了 `selinux_state` 後續的 bool 欄位（如 `disabled`），導致核心存取 SELinux 狀態時崩潰。

請依以下步驟執行：

1. **無害目標測試**：
   執行 W1 模式，但透過環境變數 `GL_TARGET_DELTA` 將寫入目標偏移到無害的 RAM 位置（例如 `+0x800000`，回到之前錯誤的位址）。
   指令範例：
   `adb -s <DEVICE_SERIAL> shell "GL_SKB_PAD=64 GL_DEBUG_DRAIN=1 GL_DUMMY_KILLS=180 GL_TARGET_DELTA=8388608 /data/local/tmp/ghostlock_rv101 --write1"`
   - 若 0/3 死亡且 enforce 仍為 1：確認為「8-byte 塗抹」問題。
   - 若仍 3/3 死亡：確認為舊的「vmap purge flood」競態問題。

2. **若確認為 8-byte 塗抹問題**：
   由於無法改變 `rb_erase` 的寫入寬度，請評估跳過 W1（SELinux），直接推進 W2。
   W2 的目標是覆寫 task 的 `cred` 指標為 `init_cred`，指標長度即為 8 bytes，完美契合寫入原語，不會有塗抹問題。

3. **修復 W2 路徑**：
   檢查 `main.c` 與 `util.c` 中的 W2 流程。目前使用 `perf_find_task()`，但裝置 `perf_event_paranoid=3` 阻擋了 perf。
   你需要實作替代方案（如 configfs/FOPS 路由）來定位 child task_struct 的位址。

## 環境紀律
- 裝置序號：`<DEVICE_SERIAL>`
- 每次執行前確保裝置 uptime > 150s。
- 每次 panic 後裝置會 2-5 分鐘自動重開，KASLR slide 會改變。
- 使用 `scripts/ghostlock_reboot_run.ps1` 進行 reboot-per-attempt 自動化測試。
- 建置指令：`$env:ANDROID_NDK_HOME='C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\android-ndk-r27d'; cd source_code\ghostlock_rv101; mingw32-make -B`

請先讀取 `reports/GHOSTLOCK_HANDOFF_V7.md` 與 `source_code/ghostlock_rv101/src/main.c`，然後開始執行無害目標測試。
