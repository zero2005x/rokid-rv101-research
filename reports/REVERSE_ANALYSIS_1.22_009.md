# 逆向分析報告 - RVE01 1.22.009 韌體包

> English: [REVERSE_ANALYSIS_1.22_009_EN.md](REVERSE_ANALYSIS_1.22_009_EN.md)

**日期**: 2026-08-14
**目標**: 對已下載的 `RVE01-ota-1.22.009-20260710-150201.zip` 進行逆向分析，找出可用的 root 攻擊面與潛在弱點。

---

## 1. 逆向範圍

| 項目 | 來源 | 結果 |
|------|------|------|
| Recovery ramdisk | recovery.img | 完整解包至本機 `recovery_root\fs\` |
| fastbootd 二進位 | recovery ramdisk `/system/bin/fastbootd` | 1288832 B，指令表完整 |
| recovery 二進位 | recovery ramdisk `/system/bin/recovery` | 1923816 B，sideload 簽章驗證確認 |
| XBL / UEFI | xbl.img / uefi.img / xbl_config.img | bootloader fastboot 閹割確認 |
| vendor.img | 7z ext4 | GateServiced、init rc 分析 |
| product.img | 7z ext4 | RokidOtaUpgrade.apk 逆向 |
| system.img | payload 提取 | update_engine、build.prop |
| vbmeta / vbmeta_system | payload 提取 | AVB flags 確認 |

---

## 2. 關鍵發現（弱點面）

### 2.1 Fastbootd（userspace fastboot）— ⭐ 最有希望的路徑
- **fastbootd 的限制僅針對 locked 設備**：`Flashing is not allowed on locked devices`、`Erase is not allowed on locked devices`、`Download is not allowed on locked devices`、`set_active command is not allowed on locked devices`。
- 本設備 **bootloader 已解鎖**（`ro.boot.flash.locked=0`，`verifiedbootstate=orange`，xbl_config 內 `Unlock = Yes`）。
- 因此若能在 **fastbootd 模式下** 執行 `fastboot flash boot_b`，不會觸發 locked 檢查。
- recovery ramdisk 內有 `service fastbootd /system/bin/fastbootd`，且 recovery 支援 `--fastboot` 參數（`Entering fastboot`）。
- **歷史阻塞點**：先前測試 `adb reboot fastboot` 與 `fastboot reboot fastboot` 均失敗。這些是走 bootloader fastboot 的途徑，bootloader 的 fastboot 被閹割（flash/boot/erase/oem unlock/flashing unlock 全部 `unknown command`）。
- **未驗證的突破點**：recovery 內建 fastbootd 從未被實際觸發過。需測試在 recovery 中手動執行 fastbootd 或寫 misc bootloader_message `command=boot-fastboot` + `--fastboot` 參數。

### 2.2 Bootloader fastboot — 完全閹割
- XBL（Qualcomm）層的 fastboot 僅保留 `getvar`、`download`、`reboot`、`reboot-recovery`，其餘寫入指令全部移除。
- xbl_config.img 含 `Unlock = Yes` 與 `EnableUfsIOC = 1`（UFS I/O 已啟用）。
- uefi.img（2.6MB）為核心 DXE，無 fastboot 命令表字串（fastboot 在另一個被移除的 DXE 中）。

### 2.3 OTA 客戶端（RokidOtaUpgrade.apk）
- 包名路徑：`product/app/RokidOtaUpgrade/RokidOtaUpgrade.apk`（3,615,458 B，`classes.dex` 6,992,392 B，5453 class）。
- **驗證鏈**：`MyUpdateEngine.applyPayload()` → `android.os.UpdateEngineService`（root 執行 `update_engine`）。
- **下載目標**：`/sdcard/update.zip`。
- **關鍵屬性**：
  - `ro.rokid.ota.check_url=https://ota.rokid.com`
  - `ro.rokid.ota.check_api=/v1/extended/ota/check`
  - `ro.product.ab_ota_partitions=system,system_ext,product,vbmeta_system`
- OTA 下載的 update.zip 最終由 update_engine 驗證簽章（payload 簽章）與序號。**驗證鏈完整，無法直接利用**。

### 2.4 Recovery sideload — 簽章驗證完整
- recovery 二進位包含 `RSA_verify` / `ECDSA_verify` / `X509_get_signature_nid`（libmincrypt 全家）。
- 使用 `otacerts.zip`（內含 Rokid releasekey.x509.pem，public key，SHA-256 with RSA）。
- **額外檢查**：`Package is for serial ...` —— recovery 驗證 OTA 包內的序號，防止跨設備刷機。
- `bootreason is in the blocklist; skip OTA installation` —— 有 bootreason 黑名單機制。
- **結論**：sideload 無法繞過簽章（私鑰不在設備上）。

### 2.5 GateServiced — TrustZone 通訊（先前已全面研究）
- 以 root 執行（`init.gateserviced.rc`：user root, group root system, disabled，`sys.boot_completed=1` 後啟動）。
- AArch64 ELF（20304 B），依賴 `CreateDmabufHeapBufferAllocator`、`TZCom_getClientEnvObject`（TrustZone）。
- 含 `run_exec_cmd_ta`（load/unload TA）、DMA-Buf heap 分配、socket 監聽、`cmd_dispatch`。
- 先前研究已實測 DMA 溢出等失敗，無可用漏洞。

### 2.6 update_engine
- `/system/bin/update_engine`（2,083,864 B）以 **root** 執行（`user root group root system wakelock inet cache media_rw`）。
- `update_engine.rc`：`on property:ro.boot.slot_suffix=*` → `enable update_engine`。
- 由 OTA APK 透過 binder `applyPayload` 觸發，payload 簽章驗證完整。

### 2.7 其他系統服務（root 權限面）
- `bootconfigs`（/system/xbin/bootconfigs）：root，seclabel `u:r:init:s0`，post-fs 執行。
- `backup_keys`（/system/xbin/backupkeys）：root，seclabel `u:r:init:s0`，boot_completed 執行。
- `init.rokid.rc`：大量 `write /sys/.../uart_en` 等，UART console 控制（`ro.bootimage.build.type=userdebug` 時開啟）。

---

## 3. 已提取/下載資產（本機）

| 檔案 | 位置 |
|------|------|
| OTA zip | `binaries\RVE01-ota-1.22.009-20260710-150201.zip` |
| payload.bin + 提取物 | `binaries\extracted_1.22\`（boot/vendor_boot/vbmeta/recovery/vendor/system/product/system_ext/xbl/uefi 等） |
| Magisk 修補 boot | `binaries\extracted_1.22\magisk_patched-122_009.img` |
| Recovery ramdisk | `binaries\extracted_1.22\recovery_root\fs\` |
| vendor 解包 | `binaries\extracted_1.22\vendor\extract\` |
| OTA APK 解包 | `binaries\extracted_1.22\ota_apk\` |
| product 解包 | `binaries\extracted_1.22\product\extract\` |

---

## 4. 弱點評估總結

| 攻擊面 | 可利用性 | 說明 |
|--------|---------|------|
| **Fastbootd (recovery 內建)** | 🟡 **未驗證，最有希望** | unlocked 設備允許 flash，但從未成功進入 fastbootd |
| Bootloader fastboot | 🔴 閹割 | flash/boot/erase 全被移除 |
| Recovery sideload | 🔴 簽章完整 | Rokid releasekey 驗證 + 序號檢查 |
| OTA update_engine | 🔴 簽章完整 | payload 驗證由 root update_engine 執行 |
| GateServiced | 🔴 無可用漏洞 | TrustZone 通訊，先前研究失敗 |
| 已知 CVE exploit | 🔴 全部失敗 | 見 FINAL_STATUS_20260202.md |

### 主要攻擊路徑評估
1. **⭐ 進入 fastbootd**（最優先）：想辦法在 unlocked 設備上啟動 recovery 內建 fastbootd，然後 `fastboot flash boot_b magisk_patched-122_009.img`。
   - 方式 A：recovery 中手動啟動 fastbootd。
   - 方式 B：寫 misc bootloader_message（需 root 或漏洞）。
   - 方式 C：recovery 選單（若「Enter fastboot」進的是 bootloader 而非 fastbootd，需另找途徑）。
2. **Magisk patched boot 已就緒**：`magisk_patched-122_009.img`（sha1 `730c3f89...`）已 pull 本機，隨時可刷。

---

## 5. misc 分區 (bootloader_message) 可寫入途徑分析

### 5.1 misc 分區狀態
- 位置：`/dev/block/mmcblk0p59`（by-name `misc`，1024 扇區 = 512KB）。
- 權限：`brw------- 0600 root:root`，SELinux `u:object_r:misc_block_device:s0`。
- shell (uid=2000) **不可讀也不可寫**（實測 `dd` = Permission denied）。
- recovery_a=p19、recovery_b=p20、boot_a=p17、boot_b=p18、misc=p59（GPT 映射已確認）。

### 5.2 bootloader_message (BCB) 結構
標準 AOSP `bootloader_message` 結構（libbootloader_message.so / libboot_control_qti.so 使用）：
```
offset 0x000: command[32]        // "boot-fastboot" / "boot-recovery" / "reboot"
offset 0x020: status[32]
offset 0x040: recovery[768]      // recovery 指令
offset 0x340: reserved[224]
offset 0x420: (A/B 延伸區)
  slot_suffix[32], update_channel[128], recovery_api_version[32], compatible[32]
offset 0x1000: BootloaderControl（A/B 槽位元資料，BootCtrl magic 0x0BADB1DE + CRC）
```
- recovery 支援 `--fastboot` 參數：寫 `command=boot-fastboot` + 於 `recovery[]` 放 `--fastboot` 即可讓下次開機進 **userspace fastbootd**。
- libboot_control_qti.so 導出 `write_reboot_bootloader`、`write_bootloader_message_to`、`SetMiscBlockDeviceForTest`。

### 5.3 可寫入途徑評估

| 途徑 | 權限要求 | 可用性 |
|------|---------|--------|
| `dd` 直接寫 misc (p59) | root + misc_block_device | 🔴 shell 無權限 |
| update_engine (root) | 需官方簽章 payload 觸發 | 🔴 簽章驗證完整 |
| boot HAL `android.hardware.boot@1.2-service` | root 執行，binder 呼叫 | 🟡 需 root 觸發，無 shell 接口 |
| libboot_control `SetMiscBlockDeviceForTest` | root 內部測試接口 | 🔴 無外部觸發面 |
| recovery `--fastboot` | 需先進入 recovery | 🟡 **可行，但需進入 recovery** |
| misc_writer (vendor) | SELinux `vendor_misc_writer_exec` | 🔴 設備上不存在此工具 |

### 5.4 結論
- **從 Android shell 無法寫 misc**（無 root、SELinux enforcing、misc_block_device 僅 root）。
- **最短路徑仍為進入 recovery 後啟動 fastbootd**：recovery ramdisk 的 init 有 `service fastbootd /system/bin/fastbootd`，recovery 二進位支援 `--fastboot`。
- update_engine/boot HAL 寫 misc 均需已驗證的 OTA 流程，無法直接利用。
- **唯一未驗證的實際攻擊面**：在 recovery 環境中（adbd 以 root 執行），手動執行 `/system/bin/fastbootd` 或寫 misc 設 `command=boot-fastboot`。

---

## 6. 實測驗證：recovery / fastbootd 進入途徑（2026-08-14 本次會話）

### 6.1 實測結果

| 測試 | 結果 |
|------|------|
| `adb reboot recovery` | ❌ 直接回 Android（uptime 28s，bootreason=`reboot,shell`） |
| `adb reboot bootloader` | ❌ 直接回 Android（uptime 77s） |
| 正常系統 `/system/bin/fastbootd` | ❌ 不存在（僅在 recovery ramdisk） |
| 正常系統 `/system/bin/recovery` | ❌ 不存在 |
| misc 分區 (p59) 讀寫 | ❌ Permission denied |
| recovery 分區 | ✅ 存在（p19/p20，recovery.img 內容正確，含 fastbootd） |

### 6.2 結論：bootloader 已封鎖 recovery/fastbootd 軟體進入途徑

- `adb reboot recovery` 與 `adb reboot bootloader` 均被 bootloader（XBL）忽略，直接啟動正常 Android。
- kernel cmdline 無 `androidboot.recovery` 參數；`qcom-reboot-reason.ko` 讀到的 reason 為 `reboot,shell`（recovery/bootloader 參數未生效）。
- `/system/bin/reboot` 工具支援 `recovery`/`bootloader`/`fastboot` 參數，但重啟後 bootloader 忽略。
- 設備僅有電源鍵（qpnp_pon / PM8150）與觸控板，無音量鍵，無硬體按鍵組合進 recovery。
- 先前報告（舊韌體 1.14/1.19）記載 `adb reboot recovery` 可進 Recovery 選單，**1.22.009 已封鎖此途徑**。

### 6.3 影響

- **fastbootd 路徑不可達**：無法透過 `adb reboot recovery/fastboot` 或按鍵組合進入含 fastbootd 的 recovery。
- **misc 寫入途徑不可行**：需 root 才可寫 misc（而 root 正是我們要獲取的）。
- **唯一剩餘途徑**：
  1. 取得 root 後寫 misc / 啟動 fastbootd（雞生蛋問題）。
  2. EDL（Emergency Download）9008 模式（Qualcomm firehose，繞過 bootloader 驗證直接刷分區）——需硬體進入 EDL 或刷機線。
  3. 利用已解鎖 bootloader（orange state）+ 現有 Magisk patched boot，透過其他未封鎖的刷寫通道。

---

## 7. KASLR 洩漏突破（2026-08-14 kernel/userspace 0-day 會話）

### 7.1 printk_formats 洩漏 kernel 位址

**`/sys/kernel/tracing/printk_formats` 可讀且洩漏 202 個 kernel 虛擬位址。**

```bash
shell@neo:/ $ cat /sys/kernel/tracing/printk_formats | head
0xffffffd23e82ac0c : "Rescheduling interrupts"
0xffffffd23e8ca100 : "Function call interrupts"
...
```

- shell uid=2000 在 `readtracefs` (3012) 群組，可讀 `/sys/kernel/tracing/`。
- `printk_formats` 是 `/sys/kernel/tracing/` 下 `r--r--r--`，全權可讀。
- **Kernel base ≈ 0xffffffd23e820000**（由最低洩漏位址 0xffffffd23e82ac0c 推導，`_text` 在 `_stext+0`）。
- dmesg 確認 `KASLR enabled`，`kpti=off`（kernel 全域映射，非強隔離）。
- 先前所有研究將「KASLR bypass 被 kptr_restrict=2 阻擋」列為主要障礙，**現已解除**。

### 7.2 其他新確認的配置

| 配置 | 值 | 意義 |
|------|-----|------|
| `dmesg_restrict` | 0 | dmesg 完全可讀（oops/warning 位址洩漏可行） |
| `kptr_restrict` | 2 | kallsyms/iomem 遮蔽，但 printk_formats 未遮蔽 |
| `bpf_jit_enable` | 1 | BPF JIT 開啟 |
| `bpf_jit_harden` | 0 | **JIT 未強化** |
| `unprivileged_bpf_disabled` | 0 | 非特權 BPF 可用 |
| `/proc/slabinfo` | root:log 可讀 | shell 在 log 群組 → **heap 佈局洩漏可行** |
| kernel cmdline | `kpti=off`, `kasan=off`, `mem auto-init: heap alloc:on/free:on` | kernel 全域映射；SLUB auto-init 已啟用 |
| USB config | `adb,diag` | DIAG 介面暴露（主機端 QXDM 使用） |
| ftrace | `trace` 666 可寫、`kprobe_events`/`set_event` 不可寫 | 無法寫 kprobe，但可讀 trace buffer |

### 7.3 剩餘拼圖：寫入原語

KASLR 已解決，但仍缺 **kernel 寫入/記憶體破壞原語**。先前所有破壞嘗試（BPF verifier、KGSL、binder UAF、TIPC）均被 CFI/SCS/SLAB auto-init 阻擋。具備的新原語：

1. **KGSL cross-FD UAF**（CVE-2025-21479）：可保留 freed GPU buffer 映射，但受 SMMU/GPU VA 隔離限制，無法直達 kernel slab。
2. **BPF heap spray + 已知 KASLR**：可精確預測 JIT 代碼位置，但需 verifier 漏洞才能寫入。
3. **slabinfo 洩漏**：可精確 heap feng shui（配合任一 OOB 寫）。

---

## 8. 後續行動建議

1. **EDL 9008 模式**（最有希望）：Qualcomm 設備可透過特定方式進入 EDL（Emergency Download），用 firehose 直接刷入分區，繞過 bootloader 的 fastboot/recovery 封鎖。先前研究已有 `sahara_reset.py` 工具與 EDL 進入經驗，可嘗試硬體方式進入 EDL。
2. **重新檢查先前 EDL 測試紀錄**：`Rokid_RedTeam_Internal_Report.md` 提到「EDL 9008 entry method + recovery tool」，確認當時如何進入 EDL 及 firehose 刷寫能力。
3. **bootloader 更新的舊韌體利用**：先前舊韌體（1.14/1.19）可進 recovery，若能取得舊韌體 bootloader 行為或 downgrade 途徑，可重現。
4. **分析 uefi.img/xbl.img 的 EDL 觸發邏輯**：檢查 PMIC/SPMI 鍵盤組合或 USB 拔插觸發 EDL 的條件。
5. 若 EDL 也封鎖，則剩餘路徑為：已解鎖 bootloader + 找未封鎖的刷寫通道，或繼續尋找 kernel/userspace 0-day。

---

## 9. KGSL 深入分析：CVE-2026-21385 驗證（2026-08-14 本次會話）

### 9.1 KGSL ioctl dispatch 表（1.22.009 實際註冊，共 47 個）

從 `msm_kgsl.ko` `.rodata` 的 `kgsl_ioctl_funcs`（0x98f8, size 0x600）提取的完整已註冊 ioctl：

| ioctl | 功能 | 狀態 |
|-------|------|------|
| `0xc0180920` (GPUMEM_ALLOC) | 分配 GPU 記憶體 | 🔴 **ENOTSUP (95)** 功能禁用 |
| `0xc0080913` (DRAWCTXT_CREATE) | GPU context 建立 | 🔴 先前確認被擋 |
| `0xc0100958` (GPUOBJ_IMPORT) | import user 記憶體 | 🟡 可用，**align 被強制覆寫** |
| `0xc0300957` (GPUMEM_BIND_RANGES/SPARSE) | sparse/VBO bind | 🟡 可用，無 align 注入 |
| `0xc0200956` (GPUOBJ_SYNC) | gpuobj 同步 | 🟡 可用 |
| `0xc0300930` (SYNCSOURCE_CREATE) | syncsource | 🔴 **ENOTTY (25)** |
| `0xc0180932` (SYNCSOURCE_CREATE_FENCE) | fence 建立 | 🔴 **ENOTTY (25)** |
| `0xc0300938` (GPUOBJ_ALLOC) | gpuobj 分配 | 🔴 **未註冊**（EINVAL） |
| `0xc008093a` (GPUOBJ_SET_INFO) | 設定 gpuobj | 🔴 **未註冊** |

### 9.2 CVE-2026-21385（alignment sign-extension）驗證結論

**漏洞本質**（CodeLinaro commit `91a0db3aa438bffbd235df8301bf188dbab218b7`「kgsl: Prevent sign extension on alignments」）：
- `kgsl_memdesc_get_align` 回傳型別 `int` → `u32`。
- `kgsl_iommu_get_gpuaddr` 的 `align = max_t(uint64_t, 1 << kgsl_memdesc_get_align(memdesc), PAGE_SIZE)` → `align = kgsl_get_align(memdesc)`。

**設備狀態判定（反彙編證據）**：
- `kgsl_iommu_get_gpuaddr`（0xa6170）內部 `0xa61d0: ubfx x11,x8,#0x10,#0x10; lsl w11,w12,w11`（`1 << align` inline，**無 capping、無 `bl kgsl_get_align`**）→ **iommu 路徑為修補前狀態**。
- 無 global `kgsl_get_align` 符號。
- `kgsl_get_unmapped_area`（0x10e00）的 `0x10fb8: ubfx w9,w3,#0x10,#8; cmp w9,#0x14; ... csel`（SZ_2M/1M/64K capping）→ 此 capping 是**原本就有**的 `get_align()`，不代表已修補。

**結論**：CVE-2026-21385 的修補**未完整套用**（iommu 路徑仍為舊版）。

### 9.3 可利用性驗證（實測）—— 不可利用

| 路徑 | 結果 |
|------|------|
| GPUOBJ_ALLOC align 注入 | 🔴 ioctl 未註冊（EINVAL），無法到達 align 檢查 |
| GPUMEM_ALLOC align 注入 | 🔴 ENOTSUP，功能禁用 |
| GPUOBJ_IMPORT align 注入 | 🟡 **可設 0-255**，但 `kgsl_ioctl_gpuobj_import` 內 `0x80ac-0x80d8` 依 size **強制覆寫 align**（size≥1MB→align=20、size≥64K→align=16），userland 值無效 |
| SPARSE_BIND (0x57) | 🟡 建立 sparse obj（kzalloc 0x50），無 align 注入 |
| SYNCSOURCE_CREATE/FENCE | 🔴 ENOTTY，CVE-2026-KGSL-UAF 不可達 |

**判定**：設備 KGSL 為「精簡版」，所有可用於注入 align 控制 GPU VA 配置的 ioctl 均被封閉。**CVE-2026-21385 在本設備不可利用**（即使 iommu 路徑未修補，userland 也無法控制 align 到達 `kgsl_iommu_get_gpuaddr`）。

### 9.4 附：KGSL 其他分析筆記

- `kgsl_ioctl_gpuobj_import`（0x7e98）：type 檢查（0x7ed0 `cmp w8,#2`）、offset mask `0x1ffffff009d00000`（0x7f48，CVE-2023-33107 修補）、flags mask `0x3_1ff_ff08`（0x7f9c）。
- `gpumem_alloc_entry`（0x9fc0）：align 檢查 `0xa00c ubfx w8,w19,#0x10,#8; cmp w8,#0x19`（align ≥25 時 clamp 到 25，非 reject）。
- PAC 啟用（`paciasp`/`autiasp` prologue/epilogue 全函數）。
- 先前報告的「6 個可用 ioctl (0x38-0x3a, 0x40, 0x56, 0x58)」**不正確**（基於舊 build 或誤判），1.22.009 實際僅 47 個註冊且記憶體分配面大幅封閉。

---

## 10. 更新後的路徑評估

| 路徑 | 狀態 | 說明 |
|------|------|------|
| recovery/fastbootd | 🔴 已封鎖 | §6 實測確認 |
| CVE-2026-21385 (KGSL align) | 🔴 不可利用 | §9 反彙編+實測確認 userland 無法控制 align |
| CVE-2026-KGSL-UAF (syncsource) | 🔴 不可達 | ENOTTY |
| **EDL 9008 firehose** | 🟡 **最有希望** | 繞過 bootloader 驗證直接刷 patched boot |
| 其他 kernel 0-day | 🟡 未窮盡 | 需新攻擊面掃描 |

**下一個實際行動**：回到 EDL 9008 模式（`scripts/sahara_reset.py` + firehose），或在已有 KASLR 洩漏基礎上尋找其他可達驅動的記憶體破壞原語。
