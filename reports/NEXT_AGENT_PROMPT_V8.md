# DeepSeek Harness Prompt for GhostLock RV101 (第九任)

> English: [NEXT_AGENT_PROMPT_V8_EN.md](NEXT_AGENT_PROMPT_V8_EN.md)

> ⚠ **歷史文件（非現況）**：本文為當時的紀錄；專案其後已有重大變化（root 已取得、引擎改執行期探測、手機 UI 重做、i18n 193 key）。
> 現行狀態請看 `README.md`、`MANIFEST.md`、`docs/deploy.md`。


你是 GhostLock 專案的第九任接手者。目標：在 RV101 裝置上完成 root 提權鏈。

## 首要前提：裝置連線狀態

第九任接手時裝置**已重新連上 Windows 主機的 USB**（`adb devices` → `<DEVICE_SERIAL>  device`）。
注意：**WSL 內的 adb 看不到這台裝置**（WSL2 無 USB 直通）。二選一：
- 用 `usbipd`（Windows 側 `usbipd bind --busid <id>` → WSL 內 `usbipd attach`），讓 WSL adb 能直連；或
- 直接在 **PowerShell** 跑（所有既有腳本/`mingw32-make` 本來就是 Windows 工具，先前 run 皆如此）。
- 序號：`<DEVICE_SERIAL>`。

## 已定讞的結論（不要再重做）

1. **run51 死因 = 8-byte 塗抹（嫌疑 A），已由既有日誌定讞**：run50 打無害位址（selinux_state+0x800000）
   5/5 punch 全乾淨完成，run51 打真 selinux_state punch 1 即死；唯一變數是目標位址，故排除 flood。
2. **W1（寫 selinux_state 關 SELinux）是死路**：rb_erase 只能 8-byte 寫、寫入值是核心指標（高 bytes=0xff），
   塗抹 `checkreqprot/initialized/policycap` 必炸。**直接推 W2。**
3. 詳情與完整符號表見 `reports/GHOSTLOCK_HANDOFF_V8.md`。

## 你的任務：實作並實測 W2 FOPS 路線

W2 目標：把 child 的 `task->cred`（offset `0x780`）寫成 `data_addr(INIT_CRED)`（8-byte 指標，無塗抹）。
目前卡點：`perf_find_task()` 被 `perf_event_paranoid=3` 擋死，拿不到 child 的 task_struct 位址。
解法 = FOPS 任意讀寫（參考 `research/ghostlock_refs/extracted/oneplus/.../src/core/{fops.c,root.c}`）。

**實作步驟（`source_code/ghostlock_rv101/`）：**

1. 在 `util.c` 的 `prepare_skb_payload()` 新增 fake `file_operations` 表（例如 `FOPS_OFF=0x1600`），
   填入 **`.cfi_jt` 跳表位址**（**不是函數本體**，見 `target_rv101.h` 的 `CFI_*_OFF`）：
   - `read`(0x10) = `configfs_read_bin_file.cfi_jt`、`write`(0x18) = `configfs_write_bin_file.cfi_jt`
   - `llseek`=noop、`ioctl/compat/mmap/open/release/show_fdinfo` = ashmem 原 `.cfi_jt`
   - ⚠️ configfs 用 `.read/.write`（+0x10/+0x18），**不是** read_iter/write_iter。
2. 用 mode-2 寫入原語把 `fake_fops` 寫進 **`data_addr(ASHMEM_MISC)+0x10`**（**不是 +0x18**，v3 交接寫錯）。
3. 開 `/dev/ashmem`，實作 `configfs_read_once`/`configfs_write_once`（用 ashmem `SET_NAME` blob 控制
   configfs buffer 的 `bin_buffer`）→ 任意核心讀寫。
4. 從 `data_addr(INIT_TASK + TASK_TASKS_OFF)`（tasks @ `0x4c8`）走 task list，用 **`tgid` @ `0x5cc`**
   比對找 child（child tgid == `fork()` 回傳值，比 comm 更穩；`pid` @ `0x5c8` 作備援）。
5. `task->cred(0x780) ← data_addr(INIT_CRED)` → child `setuid(0)`+`setenforce 0` → root shell。

所有需要的 offset 已寫入 `target_rv101.h`「W2 FOPS route (v9.31 prep)」區段（含 `CFI_*_OFF`、`FOPS_*_OFF`、
`CFG_*_OFF`、`ASHMEM_MISC_FOPS_OFF=0x10`、`ASHMEM_NAME_PREFIX_LEN=11`、`TASK_TASKS_OFF=0x4c8`、
`TASK_PID_OFF=0x5c8`、`TASK_TGID_OFF=0x5cc`、`TASK_COMM_OFF=0x790`）。

**configfs 任意讀寫原語已逐行對照完成**，務必先讀 `reports/GHOSTLOCK_W2_FOPS_CONFIGFS_RV101.md`
（RV101 讀寫都用 `bin_buffer@0x58`；fake fops 填 `.cfi_jt` 且用 `.read/.write` 槽位；SET_NAME NUL 分次寫）。

## 環境紀律

- 裝置序號 `<DEVICE_SERIAL>`；每次執行前 uptime > 150s；panic 後 2-5 分鐘自動重開、slide 會變。
- 建置：`$env:ANDROID_NDK_HOME='C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\android-ndk-r27d'; cd source_code\ghostlock_rv101; mingw32-make -B`
- 建置後務必 `Get-FileHash` 比對裝置 `md5sum`（v3 交接警示：PowerShell 管線會吃掉 make 失敗碼）。
- 跑批：`& scripts/ghostlock_reboot_run.ps1 -Cycles 3 -Env "GL_SKB_PAD=64 GL_DEBUG_DRAIN=1 GL_DUMMY_KILLS=180" -ExploitArgs "--selftest" -Log runNN`
- 先 `--selftest` 確認 reclaim/寫入原語仍穩（STRICT 簽章），再上 W2。

先讀 `reports/GHOSTLOCK_HANDOFF_V8.md` 與 `source_code/ghostlock_rv101/target_rv101.h`，然後開始實作。
