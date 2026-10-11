# GhostLock RV101 — 工作狀態交接 v9（第八任 → 第九任，W2 實作回合）

> 更新於 2026-08-20。本輪（第八任，裝置已接回）完成三件大事：
> 1. **定位並修復 run51/v9.30 的 kernel panic 根因**（不是 8-byte 塗抹！）。
> 2. **落地 W2 FOPS 路線的完整程式碼**（fake_fops + configfs 任意讀 + task walk + cred 寫）。
> 3. 發現 **RV101 ashmem 用 strscpy（會零化字尾）**，OnePlus 的 zero_at 技巧在本機失效，改走「page-based configfs 讀」。

## 0. 最關鍵結論（先讀）

**run51 的 punch-1 死亡不是 8-byte 塗抹（嫌疑 A），而是 `waiter->task = data_addr(INIT_TASK)` 指向「真實 init_task」，PI walk 結尾的 `wake_q_add(head, init_task)` 腐蝕 init_task → panic。**

- 鐵證：v9.30 **selftest（寫噴灑頁、無害目標）也 100% panic at punch 1**；run50（v9.29，錯 phys，data_addr(INIT_TASK) 指向 +0x800000 垃圾）不炸。
- 修復（已驗證 selftest STRICT OK）：`route.c` 把 `PWS_TASK`（偽 waiter->task）從 `data_addr(INIT_TASK)` 改成 **`fake_task`**（噴灑頁內的假 task，OnePlus 同款）。
- 這代表 **W1（寫 selinux_state 關 SELinux）可能根本沒壞**——只是上一任的 panic 診斷錯了方向。但實測 W1 寫入後 `enforce` 仍為 1（STRICT OK 但 enforce 不翻），**selinux_state 的 enforcing offset 可能也不是 +0**（見 §2.3，待查）。

## 1. 已驗證的成果

| 項 | 狀態 |
|---|---|
| v9.30 panic 根因 + 修復（`waiter->task = fake_task`） | ✅ 已驗證（selftest STRICT OK、不再 panic） |
| 寫入原語（rb_erase 8-byte 寫） | ✅ 已驗證（STRICT 簽章） |
| 建置（WSL interop clang.exe） | ✅ `clang.exe --target=aarch64-linux-android31 ...` 可編譯，產物與裝置 v9.30 md5 一致 |
| adb（Windows adb.exe via interop） | ✅ `$ADB -s <DEVICE_SERIAL> shell ...`；push 需 `wslpath -w` 轉路徑 |

## 2. 反組譯定案的 offset（全部已寫入 `target_rv101.h`「W2 FOPS route (v9.31 prep)」）

### 2.1 task_struct
`tasks=0x4c8`、`pid=0x5c8`、`tgid=0x5cc`、`group_leader=0x608`、`real_cred=0x778`、`cred=0x780`、`comm=0x790`。

### 2.2 configfs_buffer（RV101 5.10，反組譯 configfs_read/write_bin_file）
`mutex=0x20`、`needs_read_fill=0x50`、`read_in_progress=0x54`、`write_in_progress=0x55`、`bin_buffer=0x58`、`bin_buffer_size=0x60`、`cb_max_size=0x64`、`item=0x68`、`page=0x10`、`count=0x00`。

### 2.3 關鍵符號（kallsyms/Image dump）
ashmem_fops=0x1e977a8、ashmem_misc=0x29bcbb0（**fops @ +0x10，不是 v3 交接的 +0x18**）、configfs_*_fops、全部 `.cfi_jt` 跳表位址（`CFI_*_OFF`）。**本核心是 Clang kCFI**：fake fops 表必須填 `.cfi_jt` 位址，且用 `.read`(0x10)/`.write`(0x18) 槽位（非 iter）。

### 2.4 三大位址錯誤（前兩任已修的 + 本輪新發現）
1. symbol offset +0x28000000（v9.29 已修）。
2. phys load base 0x80800000→0x80000000（v9.30 已修）。
3. **ashmem_misc.fops @ +0x10**（v3 交接寫 +0x18 是錯的，已修進 header）。

## 3. 目前卡點：configfs 任意讀尚未打通（兩層問題）

### 3.1 strscpy 會零化字尾（關鍵新發現）
RV101 `ashmem set_name` 用 **strscpy**（`add x0,x19,#0xb; bl 0x7777b4=strscpy`），它會把「NUL 之後同一 8-byte word 內」的位元組**清零**。OnePlus 6.12 的 ashmem 用 strlcpy（保留字尾），所以 OnePlus 的 `try_set_ashmem_name_blob` zero_at 技巧在本機**失效**——實測 SET_NAME 後 name 全 0。

### 3.2 已改採「page-based configfs 讀」（理論上可行，尚未實測通過）
`configfs_read_file` 用 `page@0x10`（不是 bin_buffer@0x58）。page 在 SET_NAME blob 的 **開頭**（blob[5..12]），遠離 needs_read_fill（blob[69]），所以可以單次 SET_NAME 達成：
```c
memset(name, 0x41, 256);
memcpy(name+5, &target, 8);   // page = target
name[21] = 0;                  // NUL -> mutex(0x20)=0、needs_read_fill(0x50)=zalloc 0
ioctl(fd, ASHMEM_SET_NAME, name);
pread(fd, data, len, 0);       // configfs_read_file 讀 page=target
```
`fake_fops->read` 已改為 `configfs_read_file.cfi_jt`。**但尚未觀察到 probe 成功**（見 §3.3）。

### 3.3 當前障礙：mode-4 寫入（FOPS 安裝）在最近幾輪「STRICT MISS」且裝置 panic
- 早期一輪 mode-4 寫入曾 STRICT OK（diff@0x1600=(target-8)|1），證明寫入幾何正確。
- 之後加入「verify write」（`do_one_write(mode 4)` 的 STRICT 檢查）後，連續多輪 **child_pc@FOPS_OFF=0（MISS）**，且裝置 panic。
- 懷疑方向：(a) reclaim 波動、(b) do_one_write 的 retry loop 在 write#1 落地但 STRICT 漏檢時會留 stale fake_fops 指標、(c) 某個改動引入的回歸。**待下輪排查**。

## 4. 下輪待辦（按優先序）

1. **排障 mode-4 寫入**：先用 `--selftest` 確認 reclaim 穩定；單次 mode-4 寫入（不要 verify+install 雙寫、不要 retry loop），手動 cleanup 看 child_pc@FOPS_OFF 是否落地。
2. **驗證 page-based configfs 讀**：當 mode-4 落地後，probe `kernel_read64(fd, data_addr(INIT_TASK+TASK_TASKS_OFF))` 應回傳合法指標（非 0）。
3. **cred 寫**：找到 child_task 後用 `do_cred_write()`（rb_erase mode-2 寫 init_cred 到 cred/real_cred，單發不驗證、靠 child uid 驗證）。
4. （次要）查 selinux_state 的 enforcing 真實 offset（W1 可作備援路徑）。

## 5. 建置/執行速查

```bash
# 建置（WSL interop）
NDKBIN="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/ndk/android-ndk-r27d/toolchains/llvm/prebuilt/windows-x86_64/bin"
cd /mnt/c/Users/<USER>/Documents/codebase/rokid_root/source_code/ghostlock_rv101
"$NDKBIN/clang.exe" --target=aarch64-linux-android31 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
  -fno-stack-protector -fPIE -Isrc -include target_rv101.h -o build/ghostlock_rv101 \
  src/main.c src/util.c src/route.c -pie -static-libgcc -pthread

# push（wslpath -w 轉路徑）+ 跑
ADB="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe"
"$ADB" -s <DEVICE_SERIAL> push "$(wslpath -w .../build/ghostlock_rv101)" /data/local/tmp/ghostlock_rv101
"$ADB" -s <DEVICE_SERIAL> shell "chmod 755 /data/local/tmp/ghostlock_rv101"
"$ADB" -s <DEVICE_SERIAL> shell "GL_SKB_PAD=64 GL_DEBUG_DRAIN=1 GL_DUMMY_KILLS=180 GL_SETUP_ATTEMPTS=3 /data/local/tmp/ghostlock_rv101 --selftest"
```
反組譯：`python3 scripts/rv101_disasm.py <hex-offset> <len>`（capstone 已裝）。

---

## 附錄 A：第九任實作回合（round 1）新增發現

1. **`GL_SKB_PAD` 的正確值是 `0`，不是交接檔寫的 `64`。**
   - `--selftest`（mode 3）在 `GL_SKB_PAD=64` 時「0 diffs」（寫入沒落進 recv 車），改用 **`GL_SKB_PAD=0`** 後 `STRICT child_pc=OK`（寫入正常落地）。
   - 對照 `util.c` 的 v9.20 註解：「SKB data starts at block+0；pad=0 re-aligns」——**交接檔與 ps1 腳本裡的 `GL_SKB_PAD=64` 是錯的**。後續一律用 `GL_SKB_PAD=0`。

2. **fake_fops 安裝（mode-4 寫入 ashmem_misc.fops）會讓裝置在 punch 1 panic。**
   - 加 `install_fake_fops` 除錯：`route start` → `pselect route target=...mode=4` → `consumer punch 1/5` 後斷線（死機於 punch 1，即 rb_erase 寫 `fake_fops → ashmem_misc.fops` 當下）。
   - selftest（mode 3，寫噴灑頁）正常 → 問題專屬「寫 ashmem_misc.fops 這個全域 .data 指標」。
   - 最可能原因：Android 系統密集使用 `/dev/ashmem`，fops 被換成 fake_fops 後，**並行程序**開/讀/寫 ashmem 撞到 configfs 的 read/write（或 NULL 槽位）而崩潰。
   - 參考 OnePlus：裝完 fake_fops 後「立刻」做 configfs 操作並「立刻」還原原 fops（`configfs_write_once(fd, misc_fops, &original_fops, ...)`）。本實作目前**未還原**，且 configfs **寫**路徑（bin_buffer@0x58）因 strscpy 仍不通。

3. **下輪優先**：讓 fake_fops 安裝「安全化」——(a) 還原原 fops（注意 mode-4 還原會把 `ashmem_fops.owner` 塗成垃圾，需改用 configfs 寫或接受 owner 為 NULL 的內建驅動）、(b) 或找不碰 ashmem 全域 fops 的讀原語。

**目前程式碼狀態**：全部已同步到 Windows；`--selftest`（GL_SKB_PAD=0）通過；W2 主流程（裝 fops → configfs 讀 → task walk → mode-2 寫 cred）已落地但卡在 fake_fops 安裝 panic。

---

## 附錄 B：第九任 round 2 進展（裝置實測）

1. **fake_fops 安裝的 panic 來自「configfs 寫」而非「configfs 讀」。**
   - `fake_fops->write = configfs_write_bin_file` 會在安裝當下讓並行程序寫 ashmem 崩潰（punch 1 死機）。
   - 把 `write` 設成 **NULL**（read 保留 `configfs_read_file`）後，安裝不再穩定 panic（route 可跑完）。
   - 結論：**cred 寫用 rb_erase mode-2（`do_cred_write`），configfs 只做讀**，故 `write=NULL` 是正解。

2. **configfs 讀（page-based）的 buffer 欄位已驗證正確**：GET_NAME dump 顯示 `page@0x10 = target`、
   `needs_read_fill@0x50 = 0`、`mutex@0x20 = 0`（SET_NAME 單次 NUL 寫法正確）。但 `pread` 仍回 0。
   待查：`simple_read_from_buffer` 的 `available = buffer->count@0x00 = "dev/ashm"`（asmem_open 用
   `str x11,[asma]` 存前綴，`stur w10,[asma,#7]` 存 "mem/" 尾段 → `count` 是巨大正數），照理不該回 0。

3. **最大的不確定性是 reclaim 的 flaky**：`--selftest`(GL_SKB_PAD=0) 有時 STRICT OK、有時 0 diffs、
   有時 panic；mode-4 寫入連續 6 次 0 diffs。這是 v9.x heap feng shui 的老問題（交接檔 run45-48「zero diffs」）。
   下輪要：(a) 用 `GL_SETUP_ATTEMPTS=12` 多試、(b) 或回歸調 V9_PURGE_CNT/settle 時序。

**當前程式碼**：已同步到 Windows。`fake_fops->read=configfs_read_file`、`write=NULL`；`install_fake_fops`
保持頁面不清理（正解）；configfs 讀帶 GET_NAME 除錯。
