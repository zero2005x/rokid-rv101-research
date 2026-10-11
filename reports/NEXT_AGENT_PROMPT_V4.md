# 移交 Prompt — GhostLock RV101 (CVE-2026-43499) 第五任智能體
> ⚠ **歷史文件（非現況）**：本文為當時的紀錄；專案其後已有重大變化（root 已取得、引擎改執行期探測、手機 UI 重做、i18n 193 key）。
> 現行狀態請看 `README.md`、`MANIFEST.md`、`docs/deploy.md`。


完整背景已寫入 `reports/GHOSTLOCK_HANDOFF_V4.md`（必讀）與
`reports/GHOSTLOCK_V3_PANIC_ROOTCAUSE_ANALYSIS.md`。以下為可直接執行的移交 prompt：

---

你接手 Rokid RV101 的 CVE-2026-43499（GhostLock）root exploit 專案（工作目錄
`C:\Users\<USER>\Documents\codebase\rokid_root`）。第四任的結論與進度：

1. **v3 的「fake page panic」已被證偽為「回收失敗的垃圾頁 walk」**——payload 從未被
   forensic 證實過在位；v9 的 o2 證據只是推論。真正的 payload-walk（寫入 5 處、
   forensic 可見）至今一次都沒被觀測到。
2. 已排除：dmesg 抓 PC（本裝置 panic 為硬 reset，無任何 oops 輸出）、spinlock debug
   假說（鎖 helpers 是 vanilla qspinlock，反組譯在 rv101_disasm/spinlock_helpers.txt）、
   punch 時序（已改 multi-punch，GL_POISON 實測到達 trylock）、clone/memfd 失敗
   （fail=0，slab 數學完好）。
3. 回收機制已修到「gate 偶發觸發」：run 10 的 tight probe `o2 2470→2471` 證明 leak slab
   確實被 slab_empty gate discard 回 buddy；但 payload 放置仍未達成（run 12 仍零寫入/
   panic，且 attempt 1 全序列 o2 +63 顯示 **RCU 噪音洪流**）。

**你唯一的任務：讓 vehicle forensic 出現 diff（= payload 在位 + walk 寫入），然後
selftest 穩定 → W1 → W2。**

按順序執行：

1. **adaptive settle（頭號嫌疑）**：`src/util.c` 的 `prepare_good_kernel_page()` 目前 kill 後
   固定 `usleep(150000)`。改成：輪詢 `debug_pagetype_o2()`（每 100ms），連續兩次讀值
   相同（RCU 安靜）才進入 close 序列（或直接固定 1-2s）。run 12 的 +63 證明 150ms
   沖不掉 765 個 child 的 kernel stack 回流，這些回流隨時可能落在
   leak-close→首 send 的 µs 視窗內搶走 leak slab。
2. 建置（`$env:ANDROID_NDK_HOME='...\android-ndk-r27d'; mingw32-make -B`）→ push →
   **Get-FileHash 與裝置 md5sum 比對**（PowerShell 管線會吃掉 make 失敗碼）→ 等 boot
   穩定（uptime ≥ 5 分鐘）再跑 `--selftest`。
3. selftest 判據（cleanup 已印）：有 vehicle diff 行、`child_pc=(target-8)|1`、
   `scratch=0x…500`、`fake_task_usage=0x101`。每次 panic 後裝置 2-5 分鐘重開、
   KASLR 自動重洩漏。
4. 若放置達成後 walk 仍 panic：用「安全分流」變體無 PC 二分（V4 交接 §3.3）：
   leftmost=0 → NOOWNER → w0->lock=0xdead… 逐段縮小 fault 點。
5. selftest 穩定後：W1（`--write1` → getenforce=Permissive）→ W2（fake fops →
   configfs 任意讀寫 → cred=init_cred；kallsyms offset 見 v3 交接 §5）。

操作紀律（照舊）：所有 adb 帶 `-s <DEVICE_SERIAL>`；Redmi `<PHONE_SERIAL>` 若在線別碰；
dmesg/pstore 無用不必再試；進度探針只有 forensic 與 controlled-fault。
