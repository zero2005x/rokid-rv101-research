# Rokid RV101 Root 專案整理報告
> English: [README_ORGANIZED_EN.md](README_ORGANIZED_EN.md)
> ⚠ **本檔為歷史版面（2026-08）**：`assets/`、`binaries/`、`ghostlock_rv101/`、RVE01 OTA zip／payload.bin
> 等路徑**已不存在於目前工作區**；檔案計數（reports 225／logs 139／scripts 60）亦已過時。
> 現行版面請看 [`MANIFEST.md`](MANIFEST.md)；本檔僅保留當時的整理思路。


**整理日期**: 2026-08-22（最後更新）
**目標設備**: Rokid RV101（RG-glasses，代號 `neo`，NEO LUNA-V2）

> **2026-09 重大進展（App 產品化）**：專案已從「取得 root」延伸到「雙 Android App 系統」。
> 最新工作請看：`README.md`（總覽與文件索引）、`docs/deploy.md` §9（Shizuku 模式、WiFi 媒體通道、
> 多語系、圖示、無線 adb 結論）、`docs/architecture.md`（雙通道架構）、
> `reports/shizuku_integration_test_report.md` 與 `reports/wifi_transfer_test_report.md`（實機測試）。
> 已完成：Shizuku UserService shell 身份直跑 exploit（預設路徑）、WiFi 媒體傳輸（SoftAP + TCP 8555，
> 藍牙回退）、全透明眼鏡儀表板＋手機遠端顯示配置、14 語系、品牌 SVG/adaptive icon、
> 錄影隱私燈閃爍根治、操作紀錄兩行預覽＋長按完整視窗、
> **靜音拍攝（root bind-mount 靜音 Ogg 覆蓋相機/錄影提示音，手機可即時開關）**。

> 本文件是專案的主索引，取代舊版的 `20260814/` 描述（該目錄已不存在，檔案已重新整理到頂層目錄）。
> 若要了解「root 研究」的當前狀態，請閱讀 **`reports/GHOSTLOCK_HANDOFF_V13.md`**（Clean Root 達成）、
> **`reports/GHOSTLOCK_ROOT_操作說明.md`**（v9.60 操作）與 **`RV101_Disclosure/00_README.md`**（協調式揭露套件）。

---

## 0. 專案一句話摘要

這是一個針對 **Rokid RV101 AR 眼鏡** 的紅隊滲透測試 / Root 研究專案。目標是在
**Android 12 + Linux 5.10.209-perf（Qualcomm neo SoC）**、SELinux Enforcing、user build 的設備上，
從 `shell`（uid=2000）取得 **root** 權限。

**目前狀態：已取得 root（2026-08-22，v9.59 / v9.60）。** 主力攻擊鏈為
**CVE-2026-43499（GhostLock，rt_mutex futex PI stack-UAF）自製 exploit**，已達成
`uid=0 gid=0`、SELinux Permissive、完整 capabilities 的乾淨 root shell（證據：
`reports/ghostlock_runlogs/run_w2_v960_143215.log`、`run_w2_clean_v959_134613.log`）。
後續工作已由「取得 root」轉向「**協調式漏洞揭露**」準備（`RV101_Disclosure/`）。

---

## 1. 執行摘要

| 項目 | 狀態 |
|------|------|
| Root 權限 | ✅ **已取得**（2026-08-22，uid=0 gid=0、SELinux Permissive、完整 capabilities） |
| Bootloader | ✅ 已解鎖（orange state，`unlocked:yes`、`secure:no`） |
| Magisk | ✅ v30.6 已安裝；❌ 未激活（無 root 二進制） |
| 官方 OTA 韌體 | ✅ 已取得 1.22.009 並完整解包 |
| Magisk 修補 boot | ✅ `magisk_patched-122_009.img` 已就緒 |
| recovery / fastbootd 進入 | ❌ 1.22.009 已封鎖（`adb reboot recovery/bootloader` 皆無效） |
| KASLR 洩漏 | ✅ 已突破（`/sys/kernel/tracing/printk_formats`） |
| GhostLock 觸發 + walk | ✅ 已證明（poison 測試到達 rb_erase） |
| GhostLock 回收（reclaim） | ✅ **已克服**（仍 flaky ~50%/boot，用 boot 健康 gate + 自動重試 `boot_run_loop.sh` 掩蓋） |

---

## 2. 目錄結構

```
rokid_root/
├── assets/               # 解包資產 (2804 files)
│   ├── TZAccessApp/          # TrustZone Access Service APK 原始碼
│   ├── RV101_Validation/     # 設備驗證資料收集 (2026-08-12)
│   ├── RokidOtaUpgrade_extracted/  # OTA 客戶端 APK 解包
│   ├── vendor_extracted/     # vendor.img 解包 (lib/lib64 HAL 等)
│   ├── vendor_output/        # vendor.img 原始檔
│   ├── output_1.19/          # 舊韌體 1.19 輸出 (boot/system/vendor 等 .img)
│   └── xlsx_tmp/             # Excel 漏洞盤點工作表
│
├── binaries/             # 韌體與二進位檔 (3103 files)
│   ├── RVE01-ota-1.22.009-20260710-150201.zip   # 官方 OTA 包 (981MB)
│   ├── payload.bin                               # OTA payload (939MB)
│   ├── Magisk-v30.6.apk                          # Magisk App
│   ├── CXRService.apk / seccamservice.apk        # 系統 APK
│   ├── GateServiced.bin / gate_test.bin          # TrustZone 服務二進位
│   ├── selinux_policy.bin                        # SELinux 策略
│   ├── device_probes/          # 實機驗證用 ARM64 ELF 編譯產物
│   └── extracted_1.22/         # payload 解包 (完整韌體)
│       ├── kernel                # 未壓縮 ARM64 Image (分析源)
│       ├── boot.img / vendor_boot.img / recovery.img
│       ├── magisk_patched-122_009.img   # Magisk 修補好的 boot
│       ├── msm_kgsl.ko           # KGSL GPU 驅動模組
│       ├── system.img / system_ext.img / vendor.img / product.img / odm.img
│       ├── modem.img / dtbo.img / uefi.img / xbl.img / xbl_config.img
│       ├── vbmeta.img / vbmeta_system.img / imagefv.img
│       ├── recovery_root/        # recovery ramdisk 解包
│       ├── ota_apk/              # RokidOtaUpgrade.apk 解包
│       └── dmesg_122009.txt
│
├── reports/              # 所有報告 (.md) + ghostlock 執行日誌 (225 files)
│   └── ghostlock_runlogs/       # run_w2_* / exploit 執行與診斷日誌 (139 logs)
│
├── research/             # GhostLock 逆向參考資料與 kernel config (488 files)
│   ├── ghostlock_refs/
│   │   ├── extracted/           # 參考 exploit 源碼 (OnePlus/Honor/POCO-aresin/
│   │   │                        #   emerald/m3q/selinuxdis/openvz_patch)
│   │   ├── rv101_kallsyms.txt   # 全量符號表 (176,575 symbols)
│   │   ├── rv101_disasm/        # 關鍵函數反組譯 (*.txt)
│   │   ├── remove_waiter.disasm.txt   # 未修補證據
│   │   └── slub_5.10.209.c
│   └── kernel_configs/         # kernel config 工作副本
│
├── scripts/              # 腳本與測試程式 (60 files)
│   ├── ghostlock_analysis/      # capstone 反組譯工具
│   └── *.py / *.sh / *.ps1      # 分析/利用/收集腳本
│
├── source_code/          # 原始碼 (262 個 .c 檔案)
│   └── ghostlock_rv101/         # ★ 主力 exploit（見 §6）
│
├── tools/                # 第三方工具說明與輔助工具 (1 file)
│   └── payload-dumper-go/       # OTA payload 解包工具說明
│
├── RV101_Disclosure/     # ★ 協調式漏洞揭露套件（2026-08-22 建立，見 §9）
│   ├── RV101_Vulnerability_Report.md       # 主報告（vendor 版）
│   ├── CVE_Relationship_Assessment.md      # 既有 CVE vs 獨立弱點評估
│   ├── Evidence_Checklist.md / Evidence_Index.md / SHA256SUMS.txt
│   ├── 11_Vendor_Communication/            # 初始通報 email + 6 份 follow-up 範本
│   └── confidential/                        # exploit source / offsets / 原始證據（限閱）
│
└── README_ORGANIZED.md   # 本文件
```

---

## 3. 設備資訊

| 項目 | 值 |
|------|-----|
| 型號 | Rokid RV101（RG-glasses / NEO LUNA-V2） |
| 裝置序號 | `<DEVICE_SERIAL>` |
| 平台 | Qualcomm（`platform=neo`、`soc_id=579`，QCM4290 級 4 核） |
| GPU | Adreno 621v1（Chip ID 0x6020100） |
| Kernel | `5.10.209-perf-gd908d71a0404-ab1.22.009-20260710-150201`（1.22.009） |
| ｜ 舊版 | `5.10.209-perf-gb80a8319d11b`（1.19 以前） |
| Android | 12（SKQ1.240613.001，user build） |
| 安全補丁 | 2024-07-05 |
| Boot 分區 | boot_a=`/dev/block/mmcblk0p17`、boot_b=`p18`、recovery_a=`p19`、recovery_b=`p20`、misc=`p59` |
| 安全配置 | SELinux Enforcing、seccomp=off、kpti=off、KASLR on |
| ｜ 關鍵 sysctl | kptr_restrict=2、dmesg_restrict=0、perf_event_paranoid=3（perf 被擋）、unprivileged_bpf_disabled=0 |
| ｜ 其他 | bpf_jit_enable=1、bpf_jit_harden=0、`/proc/slabinfo` 可讀（shell 在 log 群組） |

---

## 4. 研究時間線

| 日期 | 里程碑 |
|------|--------|
| 2026-01-31 ~ 02-03 | 第一階段：大量已知 CVE 嘗試（BPF/KGSL/Binder/TIPC/io_uring/Dirty Pipe…），**全數失敗**。確認 bootloader 已解鎖、Magisk 未激活、boot.img 不可得（`FINAL_STATUS_20260202.md`）。發現 KGSL cross-FD GPU 記憶體訪問（資訊洩漏，非提權）。 |
| 2026-07-10 | 官方 OTA `1.22.009` 釋出。 |
| 2026-08-12 | 設備驗證資料收集（`assets/RV101_Validation/`）。 |
| 2026-08-14 | 取得並解包 OTA 韌體；逆向分析（`REVERSE_ANALYSIS_1.22_009.md`）。確認 recovery/fastbootd 被封鎖、CVE-2026-21385（KGSL align）不可利用。**KASLR 洩漏突破**（printk_formats）。 |
| 2026-08-18 ~ 20 | **GhostLock（CVE-2026-43499）exploit 開發**（交接 V2→V9）。完成 KASLR 洩漏、KernelSnitch mm 洩漏、GhostLock 觸發、pselect 偽造、punch、walk 到達 rb_erase（poison 證明）。 |
| 2026-08-20 | `GHOSTLOCK_HANDOFF_V9.md`：修復 run51 kernel panic 根因、落地 W2 FOPS 路線程式碼。 |
| 2026-08-21 | 最新原始碼更新（`source_code/ghostlock_rv101/src/*`）。 |
| 2026-08-22 | **Clean Root 達成**：v9.59 首次 `uid=0 gid=0`（`run_w2_clean_v959_134613.log`，boot 後 ~110s）；v9.60 重現並完成前後狀態取證（`run_w2_demo_v960_142523.log`、`run_w2_v960_143215.log`）。SELinux Enforcing→Permissive、完整 capabilities。 |
| 2026-08-22 | 建立協調式漏洞揭露套件 `RV101_Disclosure/`（主報告、初始通報 email、證據索引、SHA256 manifest、CVE 關係評估等）。 |
| 2026-08-23 | 全資料夾稽核修正（文件 hash/引用一致性）；**相機白光燈機制完整逆向**：CameraService patch → `lights_ctrl` Binder（EVENT_WARN_ID_CAMERA_OPEN）與 init.rc property 雙路徑，均實機閉環驗證（`RV101_CAMERA_LED_FLASH_MECHANISM.md`）。 |
| 2026-08-24 | **隱私指示燈信任邊界審計**（`RV101_PRIVACY_LED_TRUST_BOUNDARY_AUDIT.md`：REE-only、無 TEE/Secure MCU、無硬體互鎖）；同日將三條攻擊路徑（sysfs DAC override / `lights_ctrl` Binder / `session_open` 屬性偽造）由紙面推演轉為**受控動態驗證**（`reports/led_validation/led_test.log`，root + Permissive、未觸發相機 capture）；再驗證**路徑 D**（P-Sensor/穿戴態偽造）：狀態偽造成立（`is_take_on`/`enforce_psensor`→PSOC→extcon→rt600）但**不直接控燈**（LED 僅由 A/B/C 控制，`reports/led_validation/pathd_test.log`）。 |

---

## 5. 漏洞與攻擊面總覽

### 5.1 當前焦點漏洞

| CVE / 技術 | 類型 | 狀態 |
|-----------|------|------|
| **CVE-2026-43499（GhostLock）** | rt_mutex futex PI **stack-UAF** | 🟢 **未修補，exploit 進行中（~90%）** |

- 本質：`remove_waiter()` 錯誤清除 `current->pi_blocked_on` 而非 `waiter->task->pi_blocked_on`，
  造成 waiter（A）的 `pi_blocked_on` 懸空指向已死的 kernel stack `rt_mutex_waiter`。
- 修復 commit：mainline `3bfdc63936dd`（7.1）；影響 2.6.39 ~ 7.1。RV101 kernel 經反組譯確認**未修補**。
- 利用鏈：KASLR 洩漏 → KernelSnitch mm 洩漏 → GhostLock 觸發 → pselect 偽造 → punch →
  rb_erase 8-byte 寫 → W1（selinux_state.enforcing=0）→ W2（cred=init_cred，FOPS 路線）。

### 5.2 已驗證但不可利用（本設備）

| CVE / 技術 | 類型 | 判定原因 |
|-----------|------|---------|
| CVE-2026-21385 | KGSL align sign-extension | userland 無法控制 align（ioctl 被封閉） |
| CVE-2026-KGSL-UAF | KGSL syncsource | SYNCSOURCE ioctl 返回 ENOTTY，不可達 |
| CVE-2024-36971 | 路由表 (dst) UAF | v1-v13，loopback DST 永不 obsolete |
| CVE-2024-0582 | io_uring PBUF_RING UAF | PBUF_RING 功能不存在（需 5.19+） |
| CVE-2024-38401 | KGSL UAF | 已修補 |
| CVE-2025-21479 / 21480 | KGSL UAF | Context 創建被阻止 |
| CVE-2024-43047 / 49848 | FastRPC UAF（ITW, NoviSpy） | 需經 HWBinder 取得 fd，shell 無權限 |
| CVE-2023-2163 | BPF verifier | 已修補 |
| CVE-2022-0847 | Dirty Pipe | 已修補 |
| CVE-2019-2215 | Binder UAF | 已修補 |
| CVE-2021-22555 | Netfilter x_tables | 需 CAP_NET_ADMIN |
| CVE-2024-56720 | BPF sockmap UAF | CONFIG_BPF_STREAM_PARSER 未啟用 |
| CVE-2024-1086 / 26581 | nf_tables UAF | CONFIG_NF_TABLES 未啟用 |
| CVE-2025-21655 | io_uring eventfd UAF | 僅影響 6.1+ |
| KGSL Cross-FD 訪問 | GPU 記憶體隔離繞過 | 資訊洩漏/記憶體損壞，但無法直達 kernel slab |

> ✅ **本表已於 2026-08-21 用 `adb.exe` 實機逐列複驗，15 列全部成立。**
> 證據：`reports/5.2_VERIFICATION_2026.md`（韌體二進位層）、
> `reports/5.2_VERIFICATION_LIVE_ADB_2026.md`（adb 實機層）。
> 實機 `/proc/config.gz` 已存檔：`research/kernel_configs/device_config_1.22.009_live.txt`。

### 5.3 攻擊面（已枚舉，2026-08-21 實機複驗）

| 攻擊面 | 實測狀態 |
|--------|---------|
| `/dev/kgsl-3d0` | 0666，O_RDWR ✅（shell 可開） |
| `/dev/i2c-1` | 0666，O_RDWR ✅、ioctl 可達；但充電 IC `mp2724`(0x3f) / 電量計 `cw221x`(0x64) 已被 kernel 驅動佔用 → `I2C_SLAVE` 回 EBUSY |
| Binder / hwbinder / vndbinder | 0666，O_RDWR ✅，`BINDER_VERSION`(proto=8) 可達；跨域受限於 SELinux |
| `/dev/adsprpc-smd(-secure)` | O_RDONLY ✅，O_RDWR ❌ EACCES（需 HWBinder 取得 fd） |
| BPF Socket Filter | `unprivileged_bpf_disabled=0`，`BPF_PROG_LOAD` + `SO_ATTACH_BPF` ✅ |
| SPI Flash / Serial / DIAG | ❌ 無 `/dev/spidev*`、`/dev/diag` 節點；`/dev/ttyS0-3` 0600 root 不可開；僅 `adb,diag` USB composite（host 端 DIAG） |
| `/mnt/vendor/qmcs/`（777 可寫）、`/data/local/tmp/` | 可寫 |
| GateServiced（root、TCP 0.0.0.0:8341、`u:r:tee:s0`） | 缺 drmrpc 組無法直連 `/dev/qseecom` |
| graphics_composer | 有 drmrpc 組 + qmcs 讀寫權限 |

---

## 6. 主力 exploit：GhostLock（CVE-2026-43499）

位置：`source_code/ghostlock_rv101/`

| 檔案 | 用途 |
|------|------|
| `target_rv101.h` | 所有反組譯推導的 offset/常數（唯一真值來源） |
| `src/main.c` | 流程：`--selftest` / `--write1` / 完整 W1→W2 骨架 |
| `src/route.c` | 觸發執行緒 + pselect 偽造 + consumer punch + punch_done 同步 |
| `src/util.c` | KASLR 洩漏、KernelSnitch、mm 噴灑、key-payload 回收、forensic |
| `src/common.h` | 共用定義 |
| `src/mmfree_probe*.c` / `perf_probe.c` | 輔助探針 |
| `Makefile` | 建置（NDK r27d / WSL clang interop） |
| `build/ghostlock_rv101` | 已編譯 ELF |

**已克服（2026-08-22）**：reclaim 曾是唯一阻塞點（freed mm slab 未被噴灑載體取得、
寫入被舊 slab 靜默吸收），現以「boot 健康 gate（`nr_threads<3000`）+ 自動重試
`scripts/boot_run_loop.sh`」達成穩定重現；gid 修復改用 user-space `setgid(0)`（kernel-side
repair 第 4 次 reclaim 會 panic，詳見 `GHOSTLOCK_HANDOFF_V13.md`）。reclaim 仍為
~50%/boot 的 flaky 變數，但已被自動化掩蓋，不再阻塞 root 取得。參考
`GHOSTLOCK_V5_RECLAIM_MECHANISM.md` 與 `GHOSTLOCK_HANDOFF_V13.md`。

---

## 7. 工具與程式

### 7.1 C 語言 exploit / probe（`source_code/`，262 個 .c，按主題分類）

- **KGSL**（~80 檔）：`kgsl_uaf_exploit.c`、`kgsl_cross_fd_exploit.c`、`kgsl_cmd_inject.c`、
  `kgsl_exploit_v2~v9.c`、`kgsl_*_spray.c`、`kgsl_privesc_v*.c` 等
- **BPF**（~30 檔）：`bpf_leak_v3.c`、`bpf_ringbuf_oob_v6.c`、`bpf_kaslr_exploit.c`、
  `bpf_verifier_bypass.c`、`cgroup_bpf_exploit.c` 等
- **CVE 專項**：`cve_2024_36971_v13.c`、`cve_2024_0582_mmap_uaf.c`、`cve-2021-22555_exploit.c`、
  `cve_2023_2163_probe.c`、`cve_2025_21479_*.c`、`dirtypipe_test.c` 等
- **io_uring / TIPC / Binder / Ashmem / Adsprpc / MSM DRM / GateServiced**：對應 probe 與 exploit
- **GhostLock**：`ghostlock_root.c`、`ghostlock_trigger.c`（以及 `ghostlock_rv101/` 主體）
- **slabprobe**：`source_code/slabprobe/`（fork/micro/slab 探針，附 Makefile）

#### 7.1.1 2026-08-21 改進並實機驗證的工具

以下 5 支工具於 2026-08-21 修正 bug 後用 NDK r27d 重新編譯（動態連結、`-Wall -Wextra` 0 警告），
並在實機上重跑驗證（編譯產物 `build2_*` 位於 `binaries/device_probes/`）：

| 工具 | 主要修正 | 實機結果 |
|------|---------|---------|
| `kgsl_uaf_exploit.c` | 改用實際可用的 `KGSL_IOCTL_GPUOBJ_ALLOC`(0x45)+mmapaddr（原用 GPUMEM_ALLOC 回 ENOTSUP 跑不動）；移除 sigsetjmp/siglongjmp UB；修正 kernel 指標判斷（`(val>>40)==0xFFFFFF`） | UAF SUCCESS + cross-FD 覆寫 20 個 victim（GPU 記憶體層） |
| `bpf_leak_v3.c` | 修正 ringbuf mmap 大小；加 unprivileged_bpf_disabled/kptr_restrict/bpf_jit 探測 | verifier 拒絕 OOB（已修補） |
| `cve_2024_0582_mmap_uaf.c` | 加 `IORING_FEAT_PBUF_RING`(bit 11) 明確判斷 | bit 11 清空 + REGISTER_PBUF_RING EINVAL |
| `cve_2021_22555_test.c` | 移除 `system()`；直接解析 CapEff/CapBnd 判定 CAP_NET_ADMIN | CapEff=0 → EPERM |
| `adsprpc_secure_exploit.c` | 修正檔頭誇大描述；O_RDWR 權限面準確化 | O_RDWR EACCES（需 HWBinder） |

> 編譯範例：`aarch64-linux-android24-clang -O2 -pthread -o <out> <src>.c`
> （注意：`-static` 會觸發 Bionic TLS 對齊檢查而無法執行，須用動態連結。）
>
> 2026-08-22 盤點註記：`build2_cve_2024_0582_mmap_uaf` 編譯產物目前不在工作區；
> 原始碼 `source_code/cve_2024_0582_mmap_uaf.c` 仍完整保留，需要時可重新編譯。

### 7.2 腳本（`scripts/`，60 檔）

- GhostLock 分析：`ghostlock_analysis/disasm_func.py`、`batch_disasm.py`
- 反組譯 / 符號：`rv101_disasm.py`、`extract_offsets.py`、`unpack_bootimg.py`
- 設備重設 / OTA：`sahara_reset.py`、`sahara_reset_v2.py`、`rokid_ota_check.py`、`rokid_ota_auth.py`、`create_flash_zip.py`
- GateServiced 攻擊：`gate_*.py`、`gate_*.sh`、`GateService_exploit.py`、`frida_hook_gate.py`
- 提權枚舉：`full_privesc_enum.sh`、`final_privesc_attack.sh`、`comprehensive_vuln_scan.sh`
- 驗證收集：`Collect-RV101-Validation.ps1`、`run_v11.ps1`、`ghostlock_reboot_run.ps1`
- Root：`getroot.sh`、`magisk_activate.sh`

### 7.3 關鍵二進位（`binaries/`）

- `magisk_patched-122_009.img`（已就緒，隨時可刷）、`msm_kgsl.ko`、`kernel`（未壓縮 Image）
- `RVE01-ota-1.22.009-20260710-150201.zip` / `payload.bin`

---

## 8. 關鍵報告索引（`reports/`）

### 當前進行中（優先閱讀）

| 報告 | 說明 |
|------|------|
| `GHOSTLOCK_HANDOFF_V13.md` | **最新工作交接**（2026-08-22，第十三任：Clean Root uid=0 gid=0 達成） |
| `CURRENT_STATUS_20260822.md` | 當前狀態權威快照（root 已達成，重心轉向協調式揭露） |
| `GHOSTLOCK_ROOT_操作說明.md` | v9.60 root 取得操作手冊（boot 健康 gate、疑難排解、重建步驟） |
| `RV101_CAMERA_LED_FLASH_MECHANISM.md` | 拍照/錄影白光燈機制完整逆向（AW2110x + lights_ctrl Binder 雙路徑，2026-08-23） |
| `RV101_PRIVACY_LED_TRUST_BOUNDARY_AUDIT.md` | 隱私指示燈/P-Sensor 信任邊界審計（REE-only、無硬體互鎖；路徑 A/B/C 動態驗證 + 路徑 D 驗證「狀態偽造但非直接控燈」，2026-08-24） |
| `GHOSTLOCK_HANDOVER_PROMPT.md` | 完整接手指令 + 利用鏈全圖 |
| `GHOSTLOCK_V3_PANIC_ROOTCAUSE_ANALYSIS.md` | fake page panic 根因（回收失敗） |
| `GHOSTLOCK_V5_RECLAIM_MECHANISM.md` | 回收機制全圖 |
| `GHOSTLOCK_W2_CONFIGFS_READ_BLOCKER.md` / `GHOSTLOCK_W2_FOPS_CONFIGFS_RV101.md` | W2 FOPS/configfs 路線 |
| `REVERSE_ANALYSIS_1.22_009.md` | 1.22.009 韌體逆向（含 KASLR 突破、recovery 封鎖） |

### 5.2 表驗證（2026-08-21 新增）

| 報告 | 說明 |
|------|------|
| `5.2_VERIFICATION_LIVE_ADB_2026.md` | **§5.2 逐列 adb 實機複驗**（全部 probe 重跑，含編譯/上機步驟） |
| `5.2_VERIFICATION_2026.md` | §5.2 韌體二進位層驗證（kernel config / msm_kgsl.ko / dmesg 萃取） |

### 早期階段

| 報告 | 說明 |
|------|------|
| `FINAL_STATUS_20260202.md` | 第一階段最終狀態（所有軟體路徑失敗） |
| `HANDOVER_SESSION_20260131.md` | 完整 Session 交接 |
| `KGSL_FINAL_REPORT.md` | KGSL cross-FD 漏洞完整報告 |
| `ATTACK_SUMMARY.md` | GateServiced / 攻擊面總結 |
| `CVE_5_10_x_Scan_Report.md` | 5.10.x 內核漏洞掃描 |
| `VULN_VERIFICATION_INDEX.md` | 漏洞↔程式檔案對照索引 |
| `VULN_IMPLEMENTATION_AUDIT.md` | CVE 描述↔Repo 程式稽核 |
| `PASS_TABLE_EVIDENCE.md` | 可追溯證據表 |
| `ROOT_PROCEDURE.md` / `Rokid_Root_Guide_boot_img.md` | Root 流程指南 |
| `SESSION_14/15/16_*.md` | 各 session 研究總結 |
| `Medium_Rokid_Root_Article_EN.md` / `Facebook_Rokid_Root_Post.md` | 公開文章草稿 |
| `Rokid_Glasses_*.md` | 設備/資安報告（含 Marp 版本） |

---

## 9. 當前狀態與下一個步驟（依優先序）

Root 已達成（2026-08-22）。當前重點由「取得 root」轉為「**協調式漏洞揭露**」與可選的技術收尾：

1. **完成揭露前置證據**（詳見 `RV101_Disclosure/00_README.md`）：
   - 凍結裝置 session，重抓一份穩定的 `run_w2_v960_143215.log` 並重新 hash（原檔仍在被附加上，
     見 `RV101_Disclosure/Evidence_Index.md` 完整性註記）；
   - 補齊「攻擊前置條件」證據（bootloader 由誰解鎖、ADB 授權來源、app 情境可行性）；
   - 補 pre-exploit 的 `Cap*` 快照（對稱於 post-exploit）。
2. **發送初始通報**：`RV101_Disclosure/11_Vendor_Communication/Rokid_Initial_Disclosure_Email.md`
   → security@rokid.com（僅含最小必要資訊，不含武器化細節）。
3. **（可選）壓低 reclaim flake**（已不阻塞，僅為更優雅重現）：縮小 flood/dummy 量、換 reclaim
   載體（setxattr / sendmsg / BPF map / keyctl big_key）——參見 `GHOSTLOCK_HANDOFF_V13.md §6`。
4. **（可選）備援路徑**：EDL 9008 firehose 直接刷 `magisk_patched-122_009.img`
   （`scripts/sahara_reset.py`）——僅作為獨立備援，與本次 kernel 提權無關。
5. **交付物**：可重現 root 的單一 ELF `ghostlock_rv101`（`source_code/ghostlock_rv101/build/`）
   + 操作說明（`reports/GHOSTLOCK_ROOT_操作說明.md`）+ 完整揭露套件（`RV101_Disclosure/`）。

---

## 10. 備註

- 所有 `.md` 報告已集中於 `reports/`；原始碼集中於 `source_code/`；韌體二進位集中於 `binaries/`。
- GhostLock 參考 exploit 源碼（OnePlus/Honor/POCO 等）位於 `research/ghostlock_refs/extracted/`。
- 裝置每次 kernel panic 會自動重啟（2-5 分鐘）；KASLR slide 會變，需重洩漏。pstore 為空，
  唯一可靠的 walk 進度探針是 poison 二分法。
- 歷史交接文件（`GHOSTLOCK_HANDOFF_V2~V13`、`FINAL_STATUS_20260202.md`、`HANDOVER_SESSION_20260131.md`
  等）為各時點的「當時狀態」快照，**未回溯改寫**；以本文件、`reports/GHOSTLOCK_HANDOFF_V13.md`
  與 `reports/CURRENT_STATUS_20260822.md` 為最新狀態依據。
