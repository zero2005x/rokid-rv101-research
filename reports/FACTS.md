# FACTS.md — 單一權威事實表（RV101 / GhostLock 專案）

> **用途**：`reports/` 內 90+ 份文件常有「同一件事、不同說法」。本檔是**唯一的權威來源**：
> 其他報告若與本表衝突，**以本表為準**，並請在該報告註明「見 `reports/FACTS.md` §X」。
>
> **權威順序（衝突時依序採用）**：
> 1. **本機實測**（裝置 `getprop`／`/proc`／`sysfs`／run log 的原始輸出）
> 2. **現行原始碼**（`source_code/ghostlock_rv101/`、`android/`）
> 3. **官方外部來源**（CVE 記錄、kernel stable、Qualcomm 公告）
> 4. **當期報告的敘述**（僅在前三項都缺時採用，並標為「未複核」）
>
> 每條事實都附**證據**與**複核日期**。最後更新：**2026-09-13**。

---

## 1. 裝置與世代（identity）

| ID | 事實 | 證據 | 複核 |
|----|------|------|------|
| D1 | 機型 RG-glasses（RV101），序號 `<DEVICE_SERIAL>` | `adb devices` / `ro.serialno` | 2026-09-13 |
| D2 | **現行韌體**：`1.22.009-20260710-150201`（fingerprint `Rokid/glasses/glasses:12/SKQ1.240613.001/1.22.009-20260710-1`） | `getprop ro.build.version.incremental` | 2026-09-13 |
| D3 | **現行 kernel**：`5.10.209-perf-gd908d71a0404-ab1.22.009-20260710-150201` | `uname -r` | 2026-09-13 |
| D4 | Android **12**（SDK 32） | `ro.build.version.release` | 2026-09-13 |
| D5 | CPU：**4** 邏輯核（4×Cortex-A55） | `grep -c processor /proc/cpuinfo` = 4 | 2026-09-13 |
| D6 | GPU：**Adreno621v1** | `/sys/class/kgsl/kgsl-3d0/gpu_model` | 2026-09-13 |
| D7 | 舊世代（歷史報告用）：`1.12.009-20260109-150201` / kernel `gb80a8319d11b` | 2026-01 期報告 | — |
| D8 | Bootloader 已解鎖（`verifiedbootstate=orange`）；**boot.img 取不到** → 無 Magisk／無持久 root | `ROOT_PROCEDURE.md`、多次實測 | 2026-09-13 |

> ⚠ **引用規則**：任何報告提到 `1.12.009` 或 `gb80a8319d11b` 時，**必須標明世代**；
> 兩個世代的 ioctl／符號結論可能不同（見 §5、§6）。

---

## 2. Kernel config 與系統旋鈕（本機實測）

| ID | 項目 | 值 | 意義 |
|----|------|-----|------|
| K1 | `CONFIG_VMAP_STACK` | **`=y`** | 「RV101 沒有 VMAP_STACK」是**錯的**；任何依賴無 VMAP_STACK 的機制（如 order-2 stack pollution）不成立 |
| K2 | `CONFIG_SYSVIPC` | **`# CONFIG_SYSVIPC is not set`** | **`msgsnd()`/SysV IPC 不可用**；spray 載體需改用 pipe/SKB |
| K3 | `CONFIG_RCU_FAST_NO_HZ` | **`=y`** | 報告寫 `=n` 者為筆誤 |
| K4 | `CONFIG_IO_URING` | **`=y`** | 「CONFIG_IO_URING is not set」為筆誤；但 CVE-2024-0582 於 5.10 **不受影響**（見 §5 C3） |
| K5 | `CONFIG_SLAB_FREELIST_HARDENED` | `=y` | freelist 硬化開啟 |
| K6 | `CONFIG_CFI_CLANG` | `=y` | 控制流完整性開啟（影響 fake fops 等手法） |
| K7 | `kernel.perf_event_paranoid` | **3** | 未授權 perf 不可用 |
| K8 | `kernel.kptr_restrict` | **2** | `/proc` 不洩漏核心位址（但 `printk_formats` 洩漏路徑存在，見 §6 G3） |
| K9 | `persist.sys.usb.config` / `sys.usb.config` | **`adb,diag`** | 寫 `adb` 者為舊值 |

證據：`zcat /proc/config.gz | grep …`、`cat /proc/sys/kernel/*`、`getprop`（2026-09-13）。

---

## 3. 硬體身分（裁決 i2c 0x30 之爭）

| ID | 事實 | 證據 |
|----|------|------|
| H1 | **i2c `1-0030` = `aw2110x`（隱私指示燈 LED 驅動）** | `/sys/bus/i2c/devices/1-0030/name`；且 `/sys/class/leds/white/device` → `1-0030` |
| H2 | **i2c `1-0008` = `psoc-tp-right`（觸控／ proximity）** | `/sys/bus/i2c/devices/1-0008/name` |
| H3 | 隱私燈 sysfs：`/sys/class/leds/{white,green,blue,red}/brightness`（另有 `white/led_current`） | 實測（見 `reports/RV101_CAMERA_LED_FLASH_MECHANISM.md`） |
| H4 | 眼鏡輸入裝置：`/dev/input/event1` = `ROKID,PSOC-TP-R`（鏡腳觸控板）；`event0` = `qpnp_pon` | `getevent -pl` |

> ✗ **錯誤說法**（見 `reports/Rokid_漏洞報告信件.md` §…）：`0x30 = display controller`、`0x08 = PMIC`。

---

## 4. 提權與維運事實

| ID | 事實 | 證據 |
|----|------|------|
| R1 | root **已取得**：`uid=0(root) … context=u:r:kernel:s0`、SELinux **Permissive** | `nc -U /data/local/tmp/root_daemon.sock` → `id`（2026-09-13） |
| R2 | 取得方式：**GhostLock LPE（CVE-2026-43499）**，經 `deploy/root_bridge.sh`（bridge helper）或 `deploy/start_shizuku.sh`（Shizuku）；**每 boot 需以 adb 武裝一次** | `deploy/get_root_now.log`、實測 |
| R3 | 取得耗時：**84–112 秒**（本次實測 84 s；`get_root_now` 112 s；歷史中位數 101.5 s／n=15） | `reports/su_escalation_roi_assessment.md` §1 |
| R4 | 引擎選擇＝**執行期探測**（`AutoGhostlockEngine`）：Shizuku `pingBinder()` 優先，否則 bridge helper 心跳（`ghostlock_bridge/helper.alive`，容忍 120 s）；**`USE_ENGINE`／`USE_BRIDGE` 已不存在** | `AppGraph.kt`、`AutoGhostlockEngine.kt` |
| R5 | daemon 存活強化：root 側 keep-alive（20 s 一輪）**實測 9–18 秒救回**；daemon 與 keep-alive `oom_score_adj=-1000`；App 側 `RootDaemonKeeper` 30 s 一輪、自動重建冷卻 5 分鐘 | `deploy/install_root_keepalive.sh`、實測 |
| R6 | **跨工具共用租約** `/data/local/tmp/ghostlock_op.lease`（`owner=… pid=… ts=…`），App 看守／`deploy/lease.sh`／桌面工具三方共用；看守於清 stale socket **之前**檢查，重建前另有 TOCTOU 預檢 | `RootDaemonKeeper.kt`、`deploy/lease.sh` |
| R7 | ~~**adb over TCP 不支援**~~ → **❌ 已推翻（2026-09-20）**：眼鏡 adbd **確實綁 TCP**——`persist.adb.tcp.port=5555` 跨開機保留，listener 在 **IPv6 wildcard `[::]:5555`**（`/proc/net/tcp6`，uid 2000），`netstat` 顯示 `tcp6 [::]:5555 LISTEN`。PC 加入 SoftAP 後 `adb connect 192.168.43.1:5555` → `device` ＋ `adb shell` 實得 `uid=2000(shell) context=u:r:shell:s0`（見 `reports/wireless_adb_feasibility_2026-09-20.md`）。**原「不支援」結論的根因是偵測碼只 grep IPv4 的 `/proc/net/tcp`**（`:15B3`），而 listener 只在 `/proc/net/tcp6` | 2026-09-20 實測（PC↔SoftAP 端到端） |
| R8 | 腳本 `ADB`／`SERIAL` 唯一來源＝`deploy/env.sh`（18/18 支 `deploy/*.sh` 皆 source） | `deploy/env.sh` |
| R9 | **`adb forward` 在此環境只在 Windows loopback 生效**（adb.exe 是 Windows 執行檔，WSL2 NAT 到不了）→ `scripts/gate_*.py` 這類「host 端連 127.0.0.1:port」的腳本**無法直接從 WSL 跑**；改由裝置內部對 `127.0.0.1` 探測，或改用 Linux 原生 adb | 2026-09-13 實測 |

---

## 5. CVE 事實表

| ID | CVE | 內容（官方） | 修補／影響版本 | **本機（1.22.009 / 5.10.209）適用性** | 來源 |
|----|-----|--------------|----------------|--------------------------------------|------|
| C1 | **CVE-2026-43499**（本專案主線，GhostLock） | rt_mutex futex PI 的 stack-UAF：requeue-PI 代理路徑上 `remove_waiter()` 清錯對象（清到 requeuer 的 `pi_blocked_on`）；修補改讀 `waiter->task` | **上游**：2026-04-18 通報、04-20 修補、05-04 backport；回歸 CVE-2026-53166 於 06-02 修補；**2026-07-07 公開**（NebuSec「IonStack part II」，kernelCTF，97% 穩定／約 5 秒）。**Rokid**：`1.25.012-20260901-150201` 已修補（廠商 2026-09-06 確認；**2026-09-16 我方獨立複核**：兩版 `remove_waiter()` 同為 `0x28c`／163 條指令，機器碼**逐位元組只差 `+0x040` 一條**——`mrs x20, sp_el0`（漏洞版）→ `ldr x20, [x23, #0x30]`（`waiter->task`，修補版）；版本字串 `gd908d71a0404` → `g9a43350cc9ee`。⚠ 判別**不可**只搜尋 `#0x30`：兩版在 `+0x160` 各有一處基底為 `x9` 的 `ldr …, #0x30`（與 `waiter->task` 無關），僅看它會誤判為「已修補」） | ✅ **本機 1.22.009 仍可利用**（2026-09-13 實測取得 root，leak→root 84–155 s） | [NebuSec CVE 頁](https://nebusec.ai/buglist/CVE-2026-43499/)、[OSG-SEC-2026-07-08](https://osg-htc.org/security/vulns/OSG-SEC-2026-07-08/)、[Huawei Cloud 公告](https://support.huaweicloud.com/eu/bulletin-cce/cce_bulletin_0134.html#1)；專案方 `docs/Disclosure_Timeline.md` |
| C2 | CVE-2024-36971 | `__dst_negative_advice()` RCU 順序錯誤 → dst UAF | 上游 commit `92f1655a`（6.10-rc2）；**Debian 5.10 修補於 `5.10.221-1`** | ❌ **本機不可觸發**（LIVE 實測無可用 route／ICMP 面） | [Debian tracker](https://security-tracker.debian.org/tracker/CVE-2024-36971) |
| C3 | CVE-2024-0582 | io_uring `IORING_REGISTER_PBUF_RING` mmap 後釋放的 UAF | Debian：5.10 **not affected**（漏洞碼不存在，需 ≥5.19）；修正 6.6.8/6.7-rc4 | ❌ **不受影響**（PBUF_RING 不存在；實測 `REGISTER_PBUF_RING` EINVAL） | [Debian tracker](https://security-tracker.debian.org/tracker/CVE-2024-0582) |
| C4 | CVE-2023-2163 | BPF verifier pruning 不當 → 核心任意讀寫 | **修補於 5.10.179**（Debian bullseye `5.10.179-1`） | ❌ **已修補**（5.10.209 > 5.10.179） | [Debian tracker](https://security-tracker.debian.org/tracker/CVE-2023-2163) |
| C5 | CVE-2025-21479 | Qualcomm **GPU micronode** 未授權命令執行 → 記憶體損壞（CISA KEV，CVSS 8.6） | Qualcomm 2025-06 公告 | ⚠ **本世代實測：cross-FD `mmap` EINVAL、SYNCSOURCE ENOTTY**（§5.1）→ 現有 primitive 在本機不可用；micronode 路徑本身仍待專門探針 | [NVD](https://nvd.nist.gov/vuln/detail/cve-2025-21479)、[VulnDex 摘要](https://vulndex.at/cve/CVE-2025-21479) |
| C6 | CVE-2025-21480 | **Graphics Windows** 授權不當（非 Android KGSL） | Qualcomm 2025-06 公告 | ❌ **非 Android 元件**；`RG-glasses_…_matrix_v5.md` 的排除正確 | [VulnDex](https://vulndex.at/cve/CVE-2025-21480) |
| C7 | CVE-2024-29748 | 核心權限提升（**local EoP**，需 UI 互動；CVSS 7.8 High，CISA KEV） | 依官方公告 | ⚠ 報告寫「Remote Code Execution／Critical」為**錯誤標註** | CVE 記錄（2026-09-13 稽核查核） |
| C8 | CVE-2024-23706 / 23707 | 分別為 Android 14 健康資料權限繞過（local EoP）／**僅影響 Android 14** | — | ❌ 本機為 **Android 12**，23707 不適用；23706 描述需更正 | CVE 記錄（同上） |

---

## 5.1 KGSL／周邊 ioctl **本世代實測**（2026-09-13，1.22.009）

> **方法**：在目前韌體上執行既有探針二進位（`source_code/` 有原始碼；二進位為 2026-05 推入裝置，
> 但**執行的世代是現在的 1.22.009**）。每支以 `timeout` 包裹；**race／exploit 類探針一律不跑**。
> 原始輸出：`reports/kgsl_ioctl_retest_2026-09-13.txt`。**未**執行：`kgsl_syncuaf_probe`（create_fence×close race）。

| 探針 | 目標 ioctl | 實測結果 | 裁決 |
|------|-----------|----------|------|
| `test_gpumem` | `0xc0180920`（舊編碼「GPUMEM_ALLOC」）×8 種 flags | **EOPNOTSUPP(95)** 全部失敗 | 舊編碼不可用；GPUMEM_ALLOC 正確編碼為 **`0xc018092f`**（見 §6 爭議 5、§8 #… 相關列） |
| `test_sync_errno` | `SYNCSOURCE_CREATE 0xc0080930` | **ENOTTY(25)** | **SYNCSOURCE 家族在本世代停用** → 舊報告「SyncSource 完全可用／✅ 成功」僅適用 1.14 世代 |
| `cve_2025_21479_probe_v5` | `GPUMEM_ALLOC 0xc018092f` ＋ cross-FD `mmap` | child 配置到 `0x4000000000` 後退出；**parent mmap 失敗 EINVAL** | **cross-FD 讀寫 primitive 在本世代不可用**（C5 的 cross-FD 說法不成立） |
| `cve_2024_0582_uaf_check` | io_uring `PROVIDE_BUFFERS` / `REMOVE_BUFFERS` | `io_uring_setup` ✅、ring map ✅（features=0x7ff）；**PROVIDE/REMOVE 皆 EINVAL(-22)** | **C3 確認：本機不可觸達** |
| `ashmem_ioctl_test` | `ASHMEM_SET_NAME/GET_NAME/GET_SIZE` | SET/GET_NAME ✅；**GET_SIZE ENOTTY(25)** | 舊報告對 ashmem 名稱長度語意的假設**未證**；GET_SIZE 不可用 |
| `attack_surface_probe` | `/dev` 開權限、`I2C_SLAVE`、`BINDER_VERSION`、`BPF_PROG_LOAD` | kgsl/i2c/binder/adsprpc 皆可 open（R/W 依節點權限）；`BINDER_VERSION proto=8`；**`BPF_PROG_LOAD` 成功** | BPF 程式載入可用（與 C4「CVE-2023-2163 已修補」不衝突） |

### 5.1.1 「0x58 micronode」專用探針（v1 矩陣 ＋ v2 語意驗證）

> **工具**：`deploy/kgsl_micronode_probe.c`（v1，nr×size 矩陣）、`deploy/kgsl_micronode_probe2.c`（v2，語意驗證）；
> 由 `deploy/build_probe_zig.sh` 以 zig 交叉編譯（aarch64-linux-musl 靜態）。
> 原始輸出：`reports/kgsl_micronode_retest_2026-09-13.txt`。

| 受測 | 結果 | 判讀 |
|------|------|------|
| `nr=0x58` × size `0x18/0x20/0x28/0x30` | **全部 `ret=0`（OK）** | size **不被驗證** → 不能據此認定是哪一種結構 |
| `nr=0x58` arg=**NULL** | **EFAULT(14)** | 它確實是**已註冊**的 ioctl（會 copy_from_user） |
| `nr=0x58` gpuaddr=`0xdead0000` / `0x4000000000` / 全 `0xffff` | **全部 `ret=0`** | **不驗證內容** → **歷史報告「IOCTL 0x58 ✅ 返回數據」是假陽性**，不能當成 micronode 可達的證據 |
| `GPU_COMMAND` nr `0x3d` / `0x40` / `0x4a` | `0x3d`、`0x4a` = **EINVAL**；**`0x40` = OK** | **正確 nr 為 `0x40`（`0xc0580940`）**；專案內其他寫法皆非本機 ioctl |
| `GPU_COMMAND 0x40` 空批次 / 假 cmdlist（`0xdead0000`, len=1） | 兩者皆 **OK** | 同樣**不驗證 cmdlist**→「OK」不代表性質；舊報告「GPU command blocked／EOPNOTSUPP」不成立，但也不能視為可用 |
| `GPUMEM_ALLOC` nr `0x2f`（size=0x1000） | **EINVAL** | `0x2f` **不是**本機的配置 ioctl |
| `GPUMEM_ALLOC` nr `0x20`（size=0x1000，8 種 flags） | **EOPNOTSUPP(95)** | 本機配置路徑為 nr `0x20` 且**功能停用** |
| `SYNCSOURCE_CREATE 0x30` / `CREATE_FENCE 0x32` | **ENOTTY(25)** / **ENODEV(19)** | 家族停用（後者是「裝置不存在」而非「無此 ioctl」） |
| `GPUOBJ_SET_INFO 0x3a` / `GPUOBJ_SYNC 0x56` | OK | 號碼存在（內容仍不驗證） |
| `GPUOBJ_INFO 0x39` / `SPARSE_BIND 0x57` / `TIMESTAMP_EVENT 0x33` | ENODEV / EINVAL / EINVAL | 需正確參數才能定論 |

**結論（裁決歷史爭議）**

1. **「0x58 可用」與「0x58 EOPNOTSUPP」兩種說法都不成立**：此 ioctl 對任何內容都回成功，
   因此**任何以它為證據的結論都無效**；要判斷 micronode 是否可達，必須改用**會因參數而改變回傳值**
   的探針（例如帶合法 context/GPU 物件的完整鏈）。
2. **GPU_COMMAND 的正確號碼是 `0x40`**（`0x3D`／`0x4A` 為 EINVAL）→ 專案內三種寫法只有一種可用。
3. **本世代的 GPU 記憶體配置停用**（nr `0x20` → EOPNOTSUPP）：任何需要 GPUMEM_ALLOC 的舊鏈路
   在本機不可行；`0xc018092f` 的「✅」只可能來自更早世代或不同 ioctl。
4. **不能只用 `ret==0` 當證據**：本驅動多個 ioctl 不驗證內容（0x58、0x40 已證），
   引用舊報告時必須要求「參數變化會改變回傳值」的對照。

**可重跑指令**（`$ADB`／`$SERIAL` 來自 `deploy/env.sh`）：

```bash
# 既有探針（2026-05 推入裝置）
for b in test_gpumem test_sync_errno cve_2025_21479_probe_v5 cve_2024_0582_uaf_check attack_surface_probe ashmem_ioctl_test; do
  "$ADB" -s "$SERIAL" shell "cd /data/local/tmp && timeout 30 ./$b 2>&1 | head -40"
done
# 專用探針（本工作區以 zig 交叉編譯，見 deploy/build_probe_zig.sh）
PUSH=1 SERIAL=$SERIAL bash deploy/build_probe_zig.sh deploy/kgsl_micronode_probe.c  kgsl_micronode_probe
PUSH=1 SERIAL=$SERIAL bash deploy/build_probe_zig.sh deploy/kgsl_micronode_probe2.c kgsl_micronode_probe2
"$ADB" -s "$SERIAL" shell "cd /data/local/tmp && timeout 30 ./kgsl_micronode_probe 2>&1 | tail -30"
"$ADB" -s "$SERIAL" shell "cd /data/local/tmp && timeout 30 ./kgsl_micronode_probe2 2>&1"
```

## 5.2 GateServiced 協定與「能力」實測（2026-09-13）

> 方法：由裝置**內部**對 `127.0.0.1:8341` 送封包（`[cmd_id LE32][payload]`），hex 文字經 adb 傳輸；
> 送真實探測前先以 3 個對照組驗證儀器（banner／靜默保持／關閉）。
> 原始 transcript：`reports/gateserviced_probe_2026-09-13.txt`。
> DTX/gate TA **內部**命令空間（S21/S22）為**純靜態**分析：`reports/dtx_ta_command_space_2026-09-13.md`
> （未連 `:8341`、未下 adb、未執行任何命令或模擬）。
> ⚠ **未**執行任何 fuzz／overflow／race／exploit 類腳本；裝置事後健康（同 pid 1792、`boot_completed=1`）。

| ID | 事實 | 證據 |
|----|------|------|
| S1 | **「能力集 `000001ffffffffff`」是誤解**：那是 GateServiced **行程的 Linux capability 遮罩**（`/proc/1792/status` 的 `CapPrm/CapEff/CapBnd`），**不是**協定能力；shell（uid 2000）為 `CapEff=0`、`CapBnd=…00c0` → 隨行程而異 | `/proc/*/status` |
| S2 | **線上加構詞只有三支**：`0x00010003` LOAD_TA、`0x00010004` UNLOAD_TA、**其他任何 id → EXEC_CMD**（實測 `0x1`／`0xDEADBEEF`／`0x10001` 全部落到 `run_exec_cmd_ta`） | 送包＋logcat |
| S3 | RE 報告的「第 4 個能力」實為 `run_load_ta` **內部**的 TA info 查詢（`get_ta_version`／`dtxGetAppletInfo`，load cmd `0x1001`），**不是獨立的協定動詞**。⚠**補正（2026-09-13 靜態複查）**：它**仍可從線上到達**——`run_load_ta` 送出的正是 `cmd_id = 0x00010002`，而任何非 LOAD/UNLOAD 的 id 都落到 EXEC_CMD（S2），故 `EXEC_CMD 0x00010002` 會直接命中 TA 的 `dtx_get_appletinfo` handler 並回版本字串（實測見 S11／`gateserviced_ta_load_2026-09-13.txt` E2）。「不可達」僅指**它不是第四個線上動詞** | logcat＋`reports/dtx_ta_command_space_2026-09-13.md` §1 |
| S4 | **`300c` 只存在於 logcat，從不經 TCP 傳送**：錯誤時伺服器**送 0 bytes 並保持連線**（實測 mid-flight 為 `ESTABLISHED`） | TCP 空回應＋`netstat` |
| S5 | 有效指令 `UNLOAD_TA`（req `04000100`）→ 原始回應 `756e6c6f61642074612073756363656564` ＝ **`"unload ta succeed"`（17 B）**，logcat：`run_unload_ta succeeded.` → `Send: unload ta succeed, send_len:17` | 同上 |
| S6 | 未知／畸形 id → TCP **空**＋timeout；logcat 才是 `cmd_dispatch failed, ret:300c` | 同上 |
| S7 | → **裁決**：playbook 的「**回應＝成功、timeout＝錯誤**」在線上是**對的**；「所有命令都回 300c」是 **logcat 事實且過度概括**（UNLOAD_TA 成功並回真資料；LOAD_TA 失敗回 **300a** 不是 300c） | 上述 S4–S6 |
| S8 | ~~**LOAD_TA 從未成功**~~ → **已於同日稍後成功**（見 S10）。原觀察修正：`300a` **不是**獨立錯誤，而是 `run_load_ta` 尾端**硬編碼 catch-all**（`0x3180 mov w20,#0x300a` **覆寫** w20）→ 所有 LOAD_TA 失敗都塌成 `300a`。`qseeproxydaemon` 不存在是**紅鯡魚**（init 起不來，TA 仍可載入） | 靜態 RE（§5.2 S15） |
| S9 | **RE 錯誤表與實機不符**：表中「檔案不存在 → `0x3001`」，實機此路徑回 **`300a`** | 對照 §5.2 S8 |
| S10 | ✅ **成功 EXEC_CMD 路徑已演練（2026-09-13）**：以 root 列出 `/vendor/firmware_mnt/image/` 後載入**服務自己的 TA** `smcinvoke_gate_ta64.mbn`（399,300 B）→ 第一次即 **load ta succeed（15 B）**，logcat `ta version = 25.05.06.01`；`EXEC 0x00010002` 回 **25.05.06.01（11 B）**；`UNLOAD_TA` 後回到基線（`300c`） | `reports/gateserviced_ta_load_2026-09-13.txt` |
| S11 | **成功 EXEC_CMD 回傳 TA 原始輸出**，無框架／無狀態字（長度由 TA 決定，20480 B 緩衝截斷）；**`300e` = TA 回傳 0 bytes**（僅 logcat；線上與錯誤無法區分） | 同上（`EXEC 0x10001` → 0 B + `ret:300e`） |
| S12 | ⚠ **安全意涵**：`:8341` 是**未經驗證的本機 root TEE 載入器**——任何能連上該埠的本機行程都能指定 TA 路徑載入並執行其 applet 命令（本次已實證）。**⚠ 適用範圍限縮（2026-09-13 靜態複查）**：「任何本機 App」此一強度**尚未確認**——本次實證是在 **root context ＋ SELinux Permissive** 下完成，而 **TEE 端／smcinvoke 對「誰可載入並呼叫此 gate TA」的 ACL 無法從這兩個二進位判定**；`untrusted_app` 可達性應視為**未證實**。**破壞性命令空間已靜態完整列舉、刻意未探測**（見 S21/S22） | 本次實測＋TA 字串；限縮見 `reports/dtx_ta_command_space_2026-09-13.md` §6 |
| S13 | TA 清單（root）：`smcinvoke_gate_ta64.mbn`(399,300)、`smplap64.mbn`（**生物辨識**，排除）、`widevine.mbn`（DRM，排除）、`sp_license.mbn`、`evass/vpu20_4v.mbn`（elf32 DSP，非 TA）、`featenabler/loadalgota64/mldapta/soter64/hdcp*`（split `mdt+bNN`，無法單檔載入）；`keymaster64.mbn`／`gatekeeper.mbn` **不存在** | 同上附錄 |
| S14 | 二進位為 **full RELRO + BIND_NOW**（`GNU_RELRO` 覆蓋 `.got.plt`）→ **無 GOT overwrite 空間** | 靜態 RE |
| S16 | **空檔→`3001` 已實測確認（2026-09-13）**：以 root 建立 0-byte `/data/local/tmp/probe_empty.mbn` → logcat `Load app … failed: 12289`（=**0x3001**）；隨機 4 KB 非 TA → `failed: 12`；不存在路徑 → `get_file_size` 開檔失敗 → `Malloc failed` → **12292（0x3004）**。三類皆與靜態 RE 模型一致，線上皆為 **0 bytes + HELD-OPEN** | 本次探測（`build-cache/empty_file_probe.py`） |
| S18 | **`0x300A` 是原始碼層硬編碼常數（FACT，非編譯器產物）**：`0x3158–0x3190` 為單一基本塊，前驅集合恰為 `{0x30f8,0x3130}`；四個 `w20` 寫入者（`0x3001`/`0x3003`/`0x3004`/`0`）**只被 `load_app` 的 log 參數讀取**（`0x3120 mov w7,w20`），唯一的回傳讀取 `0x3254 mov w0,w20` 在 `0x3180` 之後 → 這些碼**作為回傳值已死、作為 log 仍活**。決定性證據：該 log 用**全新立即數** `0x3178 mov w6,#0x300a`，而可變 log 會編成 `mov w6,w20`（二進位在 5 處正是如此：`0x2f10`/`0x2fb0`/`0x30a4`/`0x2ca8`/`0x2cf4`）→ 原始碼傳的是字面值／巨集。`run_load_ta` 的回傳類別只有 **{0, 0x300A, 0x300B}**。**「為何」現已升級為有出處的判定（信心 ~95%）**：公開 Qualcomm `mink` 是 GateServiced 的同源兄弟，它做的是**相反**的事——`TaLoader.cpp:105-107` 以 `"Load app %s failed: %d (%s)"` 記錄並**回傳 `IAppLoader_loadFromBuffer` 的原始值**；GateServiced 在 `0x3124` 重現的卻是**短版** log（`"Load app %s failed: %d"` 位於 rodata `0x14f6`，**`(%s)` 變體不存在於 binary**），接著 `0x3180` 才把 `0x300a` 換進去 → 這是**本機對正確兄弟碼的在地分歧（defect），不是框架慣例**。且 `0x300a` **可證明是本 sample 的私有常數**：此路徑上所有公開碼空間皆有文件且**無一為 `0x30xx`**（`IQSEEComCompatAppLoader_ERROR_*` = 10..26、`app_loader_strerror()` 基底 10 → 10..21、`qcomtee_errno.h` = 0..10 與 −90..−103、`Object_ERROR_*` = 1..5）。「查無上游」亦已是**有支撐的否定**：Sourcegraph 公開碼 API 在**對照組通過**（`DmabufHeapCpuSyncStart` = 12 命中）下，對 `ISMCIGate*`／`dtxGetAppletInfo`／`TZCom_getClientEnvObject` 回 **0 命中**；公開 `mink` repo 768 檔 tarball grep **零個 `0x3xxx` 字面值**（被封鎖的來源—Wayback 離線、Bing/DDG 連對照組皆空—一律**丟棄不計**） | `reports/gate_300a_and_dma_unalloc_2026-09-13.md` |
| S19 | **`dma_unalloc` 回傳空間＝恰為 `{0, -1}`**（AOSP `libdmabufheap` 的 `CpuSyncEnd`＝`ioctl(DMA_BUF_IOCTL_SYNC)` 語意，非 `-errno`）；全 binary **僅一個呼叫點** `0x2c78`（無 PLT stub、不在 `.dynsym`）。**呼叫端 bug（真實觸發條件已修正）**：`0x2c7c mov w20,w0` 會**覆蓋自己的錯誤碼**，`0x2ccc cbz w20,#0x2d64` 只測 `dma_unalloc` 的回傳 → 當 **`dma_alloc` 已成功**、但其後的映射步驟失敗時（`0x2c34 cbz w0,#0x2d98`→`0x2e20 b #0x2c64`；`TZCom_getFdObject` 失敗 `0x2c38`），unalloc 對有效 fd 同步成功回 0 → **整筆被回報為成功且不留任何錯誤行**。⚠**已撤回**：原稱「fd 槽在 `dma_alloc` 失敗路徑上未初始化 → 可用殘留 fd 洗成成功」——該槽在 `0x2b00 mov w8,#-1`／`0x2b04 stur w8,[x29,#-0x1c]` **已先初始化為 -1**，`dma_alloc` 只在成功時寫入（`0x34f0 str w23,[x19]`），故配置失敗時 `ioctl(-1,…)` 不可能回 0。客戶端**兩種情況都看不到**（錯誤不上線；僅 logcat） | `reports/gate_300a_and_dma_unalloc_2026-09-13.md`、`reports/gate_dma_alloc_heapname_probe_2026-09-13.txt` |
| S20 | ✅ **已結案並推翻原假說（`dma_alloc` 無缺陷）**：原稱「`dma_alloc` 的 heap 名稱參數是堆疊指標 `0x5790` → 這就是它失敗的原因」**不成立**。`dma_alloc`（`0x346c`）是**自帶 heap 名稱的本地 wrapper**：`0x3488 mov x21,x1`（存起來）→ `0x3494 add x1,x1,#0xfd2`＝rodata **`"qcom,qseecom-ta"`** → `0x3498 mov x2,x21`（呼叫端的值成為 **`len`**）→ `0x34a8 bl DmabufHeapAlloc(allocator, heap, len, 0, 0)`。故有效簽章為 **`dma_alloc(allocator, len, &fd_out, &map_out)`**，公開 `BufferAllocatorWrapper.h` 的參數順序是**被滿足而非被違反**；`pthread_create`→`handle_client`→`cmd_dispatch`→`0x2b20` 那條鏈**為真但不搬運 heap 名稱**（`0x2b20` 的 `x20` ＝ `output_len`，`0x2a98 mov x20,x4`）。**實測佐證**：`EXEC_CMD 0x10002` 於 `output_len` 1024 與 20480 兩次皆 `run_shared_memory_example succeeded`，logcat **零** `dma_alloc failed`／`dma_unalloc failed` 行 | 同上（`capstone` 重反組譯＋實機探針） |
| S21 | **DTX／gate TA（`smcinvoke_gate_ta64.mbn`）命令空間已完整靜態列舉（未執行任何新命令）**。TA **未被 strip**：無 section header，但 `DT_SYMTAB`＋`DT_HASH`（nchain 796）提供 **754 個具名 FUNC**；`.text` 為 PT_LOAD `file 0x3000 → vaddr 0`，故 **vaddr = fileoff − 0x3000**；另有 **720 筆 wasm-native 註冊表**（24 B stride @`0x565b8`）與 **48 筆公開 DTX native API 表**（32 B stride @`0x4fa98`）。**線上 id 由 `GateServiced` 原樣轉送**，TA 端 `dispatch()` @`0x3a14` 以 `group = cmd>>16` 分派：`0x0000xxxx`＝靜默 no-op 成功；`0x00010001`＝no-op（`*out_len=0`）、`0x00010002`＝`dtx_get_appletinfo` 回 `"25.05.06.01"`（`0x10003/0x10004` 被客戶端當 LOAD/UNLOAD 吃掉）；`0x000A0001`＝`base_minimum_func_set`（JSON `op` 1 讀／2 寫或 append／3 刪 SFS，`fnm` 由呼叫者指定）；`0x000A0002`＝`base_dynamic_access`（op1 讀、op2 **覆寫固定物件** `F1EAAF6DB1723BF0BFB0E3160E62FEFF`）；`0x000B0001/2/3`＝`stx_load_decode`／`stx_exec_decode`／`stx_release`（在 TEE 內載入/執行/卸載 **wasm 模組**，bytecode 受硬編碼 `pub_key`@`0x4f2e8` 的 **SM2 簽章閘**保護，之後 LZ4+SM4）；`0x000C0001`＝`tee_token_entry`（op1 初始化根金鑰／op2 加密／op3 解密／op4 簽章／**op5 語意未知**）、`0x000C0002`＝`tee_token_sfs_func_set`（op1 讀／2 寫／3 刪）；其餘群組（2,3,6–9,≥0xD）與未知 `0xA/B/Cxxxx` → `"the cmd not support"` 回 **2**，但 `dispatch_antdtx` @`0x2068` **一律吞成 0**（`0x215c mov w0,wzr`）→ 線上與「合法空答案」**同為 0 bytes，不可區分**。**`dispatch()` 全程無 capability／呼叫者身分／命令簽章檢查**，僅檢查 `in/out/out_len` 非空與 `in_len ≥ 0`。**持久化後端**＝TEE 的 QSEE SFS（`qsee_sfs_open/read/write/rm/…` import stubs `0x240`–`0x2b0` → GOT），單一咽喉點 `sfs_persist` @`0x7814` 以 `sprintf("%s.%s.%s.%s","sec_storage/tee","0000","sync",name)` 組名；固定物件名為 **`dtx_yjym_key`**（16 B 根金鑰）與 **`F1EAAF6DB1723BF0BFB0E3160E62FEFF`**（啟用記錄）。⚠**自我更正**：`"token"`／`"tmpkey"` **不是** SFS 物件名——它們餵給 `dump_to_hex_data` @`0x762c`，而該函式在本版是 **4-byte `ret` no-op stub** | `reports/dtx_ta_command_space_2026-09-13.md` |
| S22 | ⛔ **DTX 命令風險分級（靜態；供後續任何人寫探針前查閱）**。**唯讀且靜態證明無副作用**（handler body 只碰呼叫者自己的 out buffer）：`0x00000000`–`0x0000FFFF`、`0x0001xxxx`（除 `0x10003/4`）、未知 `0xAxxxxx`（只回 4-byte `0xA001`）、所有被拒群組。**唯讀但會洩漏**：`0xA0001` op1、`0xA0002` op1、`0xC0002` op1（皆以呼叫者提供的名稱讀 SFS）。**MUTATING／不可逆**：`0xA0001` op2/op3（任意 SFS 覆寫／append／刪除，**可繞過** `"token already exist, will not write token"` 守衛）、`0xA0002` op2（覆寫啟用記錄）、`0xC0001` op1/2（產生隨機根金鑰並寫入）、`0xC0002` op2/3、`0xB0001/2/3`（TEE 內 wasm VM：**最高風險**，但受 SM2 簽章閘保護，故**不是**可任意利用的腳本原語）、wasm-only 的 `dtx_rsa_key_generate_path`／`dtx_delete_rsa_keypair`／`dtx_sm2_keypair_gen_path`／`dtx_sfs_*`。**UNKNOWN，禁跑**：`0xC0001` op5（`flag` 變體，分支已定位 `0x8874`、語意未追完）。**破壞半徑為何不可逆**：`dtx_yjym_key` 為**每次初始化即重新隨機**（`dtx_gen_random` @`0x9c1c`）→ 一旦被覆寫／刪除，**所有先前以舊金鑰封存的 token 永久無法解開**，且該物件位於 **TEE SFS 而非 `/data`** → **Android factory reset 不預期能還原**（此點為 API 層推論，未實測）。**已否決的探針**：報告 §5 建議的 1–5 號「安全」探針**未執行**——其中 #2／#4 等同已於 S10／S11／S20 實測過的 `0x10002`（無需重做），其餘（id `0`、`0x10001`、`0x0000FFFF`）只會再確認已知的「no-op → 0 bytes」，**資訊增量為零** | 同上 §3/§4/§5 |
| S17 | **`fakepayload`／`min_payload` 出處已定位**：裝置端 mtime **2026-08-14 17:28:13／17:28:38**、owner `shell`、mode **0666**（shell 寫入）；同日會話為 `REVERSE_ANALYSIS_1.22_009.md`（日期 2026-08-14）的 **update_engine／`applyPayload`／payload 簽章鏈**探索，並產出 `binaries/extracted_1.22/` 的 payload 提取物 → 兩檔是**當日 payload gate 實驗的暫存測試輸入**（`min_payload`＝手工 CrAU 標頭殘檔、`fakepayload`＝全零頁），**工作區與裝置腳本皆無引用**，無後續用途 | `stat`／`ls -la`＋同日報告 |
| S15 | **錯誤碼語意（靜態 RE，逐條 MOVZ 定位）**：`3001`＝檔案**大小為 0**（空檔；**缺檔不會走到這**）、`3002`＝LOAD_TA payload ≥128 B、`3003`＝`read_file` 回負值、**`3004`＝`calloc` 回 NULL**、`3005`＝`CreateDmabufHeapBufferAllocator` 回 NULL、`300B`＝`appPath` NULL、`300C`＝未載入旗標、`300D`＝`appObj` NULL、`300E`＝TA 回應長度 0、**`300A`＝硬編碼 catch-all**；**不存在 `300F`**。**12292 根因**：`get_file_size()` 失敗回 **-1**（`0x28DC mov w21,#-1`）→ 空檔檢查不成立 → `sxtw` 後 `calloc(2^64-1,1)`=NULL → `0x3004`；尾端再被硬編碼 `0x300a` 覆寫。另：**`load_app` 不是函式**（已 inline 進 `run_load_ta`，只是 log tag） | `reports/static_re_gate_codes_and_rescue_2026-09-13.md` |

---

## 6. GhostLock 內部事實（以現行原始碼為準）

| ID | 事實 | 證據 |
|----|------|------|
| G1 | exploit 版本 **v9.60**（banner：`ghostlock_rv101 v9.60 — CVE-2026-43499 (GhostLock) / Rokid RV101 1.22.009`） | `source_code/ghostlock_rv101/src/main.c:184` |
| G2 | `GL_SKB_PAD` **預設 0**（範圍 0–256）；「預設 64」與 ±64 校準理論已被推翻 | `src/util.c:665`、V6 §1.2 |
| G3 | 符號位移（**v9.29 起權威**）：`INIT_TASK=0x021eb840`、`INIT_CRED=0x02202058`、`SELINUX_STATE=0x02ca0140`、`ROOT_TASK_GROUP=0x02a71040`；**v9.29 之前的位移全部多 0x28000000** | `target_rv101.h:43-52`（註解明載前值錯誤） |
| G4 | KASLR：`KIMAGE_TEXT_BASE=0xffffffc008000000`，slide 由 `/sys/kernel/tracing/printk_formats` 洩漏（`PF_STR_RESCHED_OFF=0x1c2ac0c`） | `target_rv101.h:22-27,58` |
| G5 | `ashmem_misc` 位移 `0x029bcbb0`；**`miscdevice.fops` 偏移 `+0x10`**（非 +0x18） | `target_rv101.h:117-118` |
| G6 | `TASK_CRED_OFF=0x780`、`TASK_REAL_CRED_OFF=0x778`；cred **uid 在 `+0x04`**；v9.59 明載「**不要**加 kernel 側 gid 修復」 | `target_rv101.h:71-72`、`main.c:101` |
| G7 | 現行 FOPS 安裝走 **mode-5 / write#1**（`fake_fops → ashmem_misc.fops + 0x10`）；W1/W2 均已驗證成功（附錄 E／AA） | `main.c:297-364`、V10 附錄 |
| G8 | 需 **shell(uid 2000)** 身分執行；`untrusted_app` 直跑會 kernel panic（`perf_event_open` 等權限） | `docs/architecture.md` §5 |
| G9 | 失敗分類（143 次歷史）：verifier gate／reclaim **80**、panic **21**、timeout 0、OOM 0 | `reports/su_escalation_roi_assessment.md` §1.2 |

### 6.1 panic 取證與「時機條件」（2026-09-13 實測）

> 原始輸出：`reports/device_facts_ramoops_uptime_2026-09-13.txt`

| ID | 事實 | 證據 |
|----|------|------|
| G10 | **ramoops 後端確實已註冊**：`pstore: Registered ramoops as persistent store backend`、`ramoops: using 0x200000@0xf8e00000, ecc: 0`；`/sys/fs/pstore` 以 `pstore` 類型掛載（rw） | dmesg ＋ `mount` |
| G11 | kernel cmdline 含 `console=ttyMSM0,115200n8`、`pstore.compress=none`；`androidboot.selinux=enforcing`（**開機預設**）、`androidboot.verifiedbootstate=orange` | `/proc/cmdline` |
| G12 | **但 `/sys/fs/pstore` 目前是空的**（只有 `.`/`..`）——即使本日稍早發生過一次硬重置（`[wait] 裝置離線，視為 reboot`），仍**沒有留下 `console-ramoops-0`** | `ls -la /sys/fs/pstore/` |
| G13 | → **裁決**：「有沒有 ramoops」= **有**（V11 正確、V3 的「pstore 恆空、無 ramoops」錯誤）；但**這個失敗模式（硬重置）實務上抓不到現場**，因此「靠 ramoops 做 panic 事後分析」在本機**不可行** |
| G14 | **exploit 完全不讀 `/proc/uptime`**（`source_code/ghostlock_rv101/` 全樹 grep 無任何 uptime 引用） | 原始碼 |
| G15 | 真正的時機門檻是 `settle_wait(tag, min_rounds, quiet_needed, cap_rounds)`：**12 輪 soak ＋連續 6 輪 quiet**（每輪 500 ms，上限 40 輪）＝至少 ~6 秒穩定；並在 clone 失敗時診斷 `RLIMIT_NPROC`／`threads-max`／`pid_max`／`/proc` 條目數 | `src/util.c:951-975,1074-1085` |
| G16 | → **裁決**：「uptime > 150 s」與「> ~400 s」都只是**經驗代理指標**（開機後系統繁忙 → 難以 quiet）；**不是程式常數**。實證：2026-09-13 兩次成功取得 root 分別在 **uptime 159 s**（`deploy/get_root_now.log`，boot 1 / trigger 1）與 **~161 s**（`reports/kernelsnitch_mm_leak_clean_boot_2026-09-13.md`）→ **>400 s 之說被推翻** |
| G17 | 系統基準（本日 14:08）：`uptime=916 s`、`loadavg 2.10 15.54 18.41 2/1342`、`threads-max=13217`、`pid_max=32768`、`/proc/*/task` 約 2136 筆 | 同上證據檔 |

---

## 7. 產品事實（App 與協定）

| ID | 事實 | 證據 |
|----|------|------|
| P1 | 手機 App：**4 分頁**（遙控／相簿／本機／設定）；設定頁 **7 個可折疊分組**；相簿與本機 **3 欄** | `menu_bottom_nav.xml`、`fragment_settings.xml`、`AlbumsFragment.kt` |
| P2 | 主題：**預設深色**品牌（`#0B1220`＋`#22D3EE`／`#FFB300`），可切淺色（`#F4F6F9`＋`#0E7490`／`#B45309`） | `ThemeHelper.kt`、`values*/colors.xml` |
| P3 | i18n：**14 語系**、管線 **193 key**（`scripts/i18n/phone_keys.tsv`）；各 `values-*/strings.xml` **284** 條（193 管線＋91 早期遷移） | 驗證器 + 實測 |
| P4 | 眼鏡→手機文字為**結構化代碼**：戒指 `RingOutcome(ringLastKey/Arg)`、ghostlock `ProgressEvent(code,arg)`；手機以 `RingText`／`GhostlockText` 組字 | `shared/protocol/Progress.kt`、兩端 mapper |
| P5 | 協定訊息型別 **37** 個；`Status` 欄位 **26** 個（含 `rootState`／`su*` 度量） | `Protocol.kt`、`Models.kt`、`docs/protocol.md` |
| P6 | 傳輸：控制＝藍牙 RFCOMM（SPP UUID 集中於 `Protocol.SPP_UUID`）；媒體＝WiFi TCP 8555（SoftAP `RokidCam`／`192.168.43.1`），可回退藍牙 | `docs/protocol.md` |
| P7 | ⚠ **戒指控制的兩條路徑都沒有「來源裝置」驗證**（2026-09-13 稽核）。**路徑 A（手機）**：`MainActivity.dispatchKeyEvent`／`dispatchTouchEvent`／`dispatchGenericMotionEvent`／`RingMediaSession.onMediaButtonEvent` **從未讀取 `deviceId`**，`RingController.onKeyDown(code)`／`onMouseButton(action,button)` 簽章裡也沒有裝置參數 → **任何** HID 來源（藍牙／USB 鍵盤滑鼠、指紋 `uinput-fpc`、**別的廠牌戒指**、`input` 注入的合成事件）都能觸發拍照／錄影／靜音／顯示切換。**路徑 B（眼鏡）**：`RingEvdevReader.autoDetect()` 以**裝置名稱子字串**（預設 `RGR`，退化 `RING`）取第一台命中，且 `ring_ctrl.device` 有值就**跳過偵測** → 名稱即身分 | 靜態閱讀＋實機（見 P8/P9） |
| P8 | **眼鏡目前的「戒指控制」實際上是眼鏡自己的觸控板，不是戒指**（2026-09-13 實測）：眼鏡 `ring_ctrl` prefs ＝ `pattern=RGR`、`device=/dev/input/event1`、`enabled=true`；而眼鏡輸入裝置只有 `event0=qpnp_pon`、**`event1=ROKID,PSOC-TP-R`**；眼鏡已配對藍牙清單只有一支手機與一台平板，**沒有任何戒指**（`RGR06` 亦未配對）。手機端則在線掛著**另一家的** `AIVELA_RP13EJ37` input device（`dumpsys input` device 22），其事件同樣被 `RingController` 吃掉；手機 prefs ＝ `enabled=true`、`gesture_count=171`、`map_4097→PHOTO`（滑鼠主鍵／觸控板輕點→拍照） | `dumpsys input`、`dumpsys bluetooth_manager`、`getevent -i`、root 讀 prefs |
| P9 | ⚠ **兩端 `DebugReceiver` 皆為 `exported="true"` 且無 `android:permission`**（手機 manifest `:43-49`、眼鏡 `:67-73`），`CaptureService` 亦同（眼鏡 `:53-56`）。✅ **已端到端實證（2026-09-13 15:45）**：自製最小 App `com.ringaudit.reachtest` 以**一般 App uid（手機 10346／眼鏡 10064，皆非 shell 2000）**投遞，兩端 `DebugReceiver` 均**實際執行**指令（原始輸出 `reports/ring_reachability_test_2026-09-13.txt`；測試 App 已移除，素材留存 `build-cache/ringtest/`）。可觸發：手機 **`CAPTURE_PHOTO`／`START_VIDEO`／`STOP_VIDEO`／`ACQUIRE_ROOT`／`WIFI_ADB`／`WIFI_CONNECT <任意 IP>`／`PAIR_RING`／`WATCH_RING(autoBond)`／`DELETE_MEDIA`／`SET_RING`**、眼鏡 **`RING_START`／`RING_STOP`／`RING_DIAG`／`PAIR_RING`**。手機端 `onReceive` **不檢查 `intent.action`**（只看有無 `cmd` extra） | 稽核報告 §5；`ring_reachability_test_2026-09-13.txt` |
| P10 | ⚠ **WiFi 媒體／控制通道零認證，且 SoftAP 密碼寫死於 APK**：`WifiServer` ＝ `ServerSocket(8555)` 綁所有介面、`accept()` 任何來源（只印 `remoteSocketAddress`），`CommandRouter` 無權杖（`HELLO` 只回 `HELLO_ACK`）；`Protocol.kt:15` **`AP_PASS = "rokid1234"`**（`AP_SSID="RokidCam"`）隨 APK 散布。實機此刻 `netstat` 顯示 `[::]:8555 LISTEN`，但 `wlan0=NO-CARRIER/DOWN`、無 `wlan2` → **外部不可達**；一旦開 SoftAP 或眼鏡連上一般 WiFi（文件記錄過連上開放網路 `iTaiwan`）即同網段可達 | `WifiServer.kt:30-45`、`Protocol.kt:14-15`、實機 `netstat`／`ip addr` |
| P15 | **戒指「連線失敗」的真正成因鏈（2026-09-13 實測取證）**：AIVELA 戒指**早已配對＋HID 連線**（`mInputDevices BB:C3:…:2`、`/dev/input/event7`）且**確實能驅動拍照**（4 張檔案 + `RingController` log）。使用者看到的失敗是四段疊加：①**LMK 殺掉眼鏡 App**（`lowmemorykiller: Kill 'com.rokid.rc.glasses' … 102868kB`，觸發點是連 5 筆 `CAPTURE_PHOTO`）→ ②服務重建但 `Service.startForeground() not allowed due to bg restriction`（**沒進前景**）→ ③戒指按鍵（`KEY_PLAYPAUSE`／`MEDIA_NEXT` 被綁成 `VIDEO_TOGGLE`）觸發 `START_VIDEO`，而 `bindVideo()` 會設 **`imageCapture = null`** → 之後**所有拍照必回 `camera not ready`**、且該次錄影**等不到 keyframe 從未產檔** → ④`ActivityManager: Stopping service due to app idle` 停掉服務，`onDestroy` 關閉 RFCOMM/WiFi server → 手機顯示「未連線」。**「配對戒指」對 AIVELA 則永遠失敗**（已配對不廣播＋只比對名稱含 `RGR`）。**已驗證的立即處置**：`am start -n com.rokid.rc.glasses/.MainActivity`（帶到前景）→ `isForeground=true`／`camera ready`／`CONNECT Success`／`CAPTURE_PHOTO result=true`。⚠**日常勿用 `am start-foreground-service` 從背景拉服務**，必被 bg restriction 拒絕、隨後被 app idle 停掉 | `reports/aivela_ring_connection_failure_2026-09-13.md` |
| P16 | **相機錯誤碼已細分＋錄影／連拍保護已實作並實測（2026-09-13）**。新增 `shared/protocol/CameraErrors`：`camera_not_ready`／**`busy_recording`**／**`busy`**／**`record_start_timeout`**（手機 `ui/RingText.errorDetail()` 對應在地化字串，未知碼原樣顯示）。`CameraController`：①`takePhoto` 在 `imageCapture == null` 時依 `isRecording` 回 `busy_recording`（不再一律 `camera not ready`）；②`startVideo` 以 **5 秒** `withTimeoutOrNull` 等 `VideoRecordEvent.Start`，逾時→`stop()`＋強制 `bindPhoto()`＋刪殘檔＋回 `record_start_timeout`；③`takePhoto` 加 `Mutex.tryLock`（重疊即回 `busy`，不排隊）＋ **800 ms** 最小間隔。手機端：媒體鍵（PLAY_PAUSE／NEXT／PREVIOUS…共 9 鍵）自 `INTERESTING_KEYS` 移除（放行給系統），並以一次性遷移移除既有「媒體鍵→VIDEO_TOGGLE」對應；「配對戒指」改為 `RingBonder.bondedHidDevices()` 清單（class／UUID／名稱三重判定，並排除無 HID 證據的 AUDIO_VIDEO 類別），可選裝置存入 `ring_ctrl.approved_mac`；**畫面已實拍驗證**（AIVELA 入列；Xiaomi Buds 亦入列係因其確實為 HID 裝置，`mInputDevices` 同時列出兩者）。**實測**：5 連拍全成功且間隔 958–1079 ms、無 LMK；錄影中拍照回 `busy_recording`；錄影產出 17.6 MB/1280×720/8207 ms 正常檔；WiFi 與藍牙併發時重疊者回 `busy` | `reports/ring_p0_p1_fixes_test_2026-09-13.md` |
| P17 | **RGR06（`D1:03:CB:ED:C7:97`）配對：手機失敗、眼鏡成功（2026-09-13 取證）**。①**手機（Android 13/MIUI）**：每次都是「LE ACL 連線成功 → 約 300 ms 後 SMP 中止」——`smp_reason:SMP_FAIL`、`sec_level:0x0`、`L2CA_RemoveFixedChnl … CID 0x0006 not connected`，之後清金鑰放棄；同一分鐘同一支手機對**其他兩台** BLE 裝置 GATT 連線正常（`status=0 state=2`）→ **不是手機 BLE 故障、也不是本 App 的問題**，是該戒指拒絕此主機。②**我們程式有真 bug**：`RingBonder.bond()` 用 `ReceiverRestrictedContext`（來自 `BroadcastReceiver.onReceive`）呼叫**兩參數** `registerReceiver`，在 **Android 12（API<33）丟 `ReceiverCallNotAllowedException` 使 App FATAL**（手機 API 33 走三參數分支故不崩）。**已修**：改用 `context.applicationContext` 註冊／反註冊，並補上眼鏡端 `DebugReceiver` 的 try/catch。③**修正後眼鏡端配對立即成功**（`bondState 12`、LTK/IRK/CSRK 三種 BLE 金鑰寫入），RGR06 成為眼鏡 HID 裝置（`mInputDevices : 2`；`event2` Keyboard／`event3` Consumer Control／`event4` 觸控面 `BTN_TOUCH`+`ABS_X/Y`），眼鏡端戒指控制已切至 `/dev/input/event4`。⚠ 換裝置必須 `RING_STOP`→`RING_START`（執行中只更新 prefs 不換裝置） | `reports/rgr06_pairing_failure_2026-09-13.md` |
| P18 | **眼鏡「拔掉 adb 後過沒多久就重開機」＝ GhostLock 提權失敗的 kernel panic（延遲後果）**。專案自身文件明載「**每次開機約 50%**；失敗 = kernel panic → **2–5 分鐘後自動重開機**」（`ZeroLightRokidDesktop/docs/research-update.md`），本 workspace 統計亦為 **21/143 panic**（G9）；核心命令列有 **`kernel.panic_on_rcu_stall=1`** → UAF 留下的腐敗 rt_mutex PI 鏈讓任務卡死即直接 panic。實機：前一次關機為 **PS_HOLD（MSM controlled shutdown，屬乾淨軟體重置，非 watchdog／掉電）**；boot history `reboot,1789291362 / reboot,shell,1789278832 / reboot,1789271315 / reboot,1789271070` —— 17:22 那筆**無 `shell` 標記故非 `adb reboot`**，且 11:44／11:48／13:53 三次**均早於** RGR06 綁定（17:12:58）⇒ **排除戒指為成因**。⚠ 另一關鍵事實：這台眼鏡**禁止第三方 App 收 BOOT_COMPLETED**（`BroadcastQueue: Background execution Third-Party APP not allowed`），故**重開機後 App 根本不會自動起來**（實測 `pid=none`、root 亦失）——「自動恢復」是無效的，而 `root_mode=AUTO` ＋ keeper 每 5 分鐘無限重試會反覆重跑 exploit（可能形成開機迴圈）。**可修**：開機迴圈斷路器、每次開機重試上限＋指數退避、僅充電時自動武裝、開機後等 `uptime>150s`、明確告知需重新武裝 | `reports/glasses_reboot_after_adb_unplug_2026-09-13.md` |
| P19 | **提權「安全武裝」四道保護已實作並逐項實測（2026-09-13）**。新增 `ghost/ArmPolicy.kt`：①**開機迴圈斷路器**（開機識別＝`currentTimeMillis()-elapsedRealtime()`；新開機時若「武裝後 ≤10 分鐘就重開」記一次可疑，連續 2 次即 `suspended` 停用自動武裝）；②**每次開機重試上限 2 次 ＋ 指數退避 5/15/60 分**；③**僅充電時自動武裝**（`BatteryManager` sticky broadcast，免權限）；④**開機後延遲 `uptime>150s`**。`CaptureService`(AUTO) 與 `RootDaemonKeeper`(自動重建) **兩個自動路徑都受約束**；**手動提權不受影響**。以 build flag `-PsafeArming` 區分兩版：**`1.0-safe`(versionCode 3)** 與 **`1.0`(versionCode 1)**，產物在 `dist/`。實測（全程未真的跑 exploit）：斷路器 1/2→2/2 跳脫、`AUTO 提權暫停：breaker-open` 且 logcat 中 exploit 出現 **0 次**、`max-arms-this-boot`、`backoff-5m`、`ARM_POLICY_RESET` 全部通過。⚠ `boot-settle` 與 `on-battery` 未直接驗證（需重開機／拔線情境）。裝置現留 `1.0-safe` ＋ `root_mode=MANUAL` | `reports/arm_safety_policy_2026-09-13.md` |
| P22 | **眼鏡觸控板 `ROKID,PSOC-TP-R`（`/dev/input/event1`）確實會送鍵**：`KEY_ENTER`＝輕觸、**`KEY_DASHBOARD`＝開啟 Rokid 助手**、`KEY_PROG1`＝滑動、`KEY_BACK`＝返回。眼鏡端 `RingEvdevReader.TAP_LIKE` 只含 `KEY_ENTER` 等 → 輕觸會拍照，`KEY_DASHBOARD` 被忽略（正確）。⚠ **更正**：先前用 `getevent … | head` 得到「0 事件」是**測試方法錯誤**（pipe＋SIGTERM 使 stdio 緩衝遺失），觸控板其實正常；`ring_events.log` 為 0 bytes 則是 20:11 硬重置時**頁快取未落盤**，同樣不能證明沒有手勢。⚠ 20:11:55 的重開與今天前三次（17:49／18:14／18:44）特徵一致（`PS_HOLD` 乾淨重置、pstore 空），前三次都與助手無關 → **最可能是 LPE 的延遲 panic，助手只是觸發點**；但要判定仍需 root 讀 dropbox／bootstat，或即時重現抓 log | `reports/camera_ring_failure_remote_diagnosis_2026-09-13.md` §9 |
| P23 | **20:31–20:36 即時重現實驗：沒有重現崩潰**。把眼鏡 kernel log（`dmesg -w`）＋logcat 串流到 **PC 端檔案**（崩潰後仍保留），期間使用者多次操作觸控板開助手：uptime 持續遞增、kernel log **無 panic／oops／RCU stall／hung task**⇒ **「開助手」不是穩定可重現的當機原因**。捕捉到兩個現象：①助手確實會感知我們佔用相機（`AssistServer_Log: CameraController -> CameraManager Camera Un Available: 0`）但未報錯；②相機工作佇列延遲警告 **49 次**（`CAM-UTIL … workq schedule delay`，警告級）。同時確認該期間眼鏡觸控板路徑**是停的**（`getevent 執行中=false`，缺 root）→ 可**排除**觸控板／我們的程式為 20:11 該次成因；當時的檔案是手機端觸發的。⚠ 結論不變：最可能仍是 **LPE 延遲 panic**，但決定性證據（panic trace）仍需 root 讀 dropbox 或崩潰當下的即時串流 | `reports/camera_ring_failure_remote_diagnosis_2026-09-13.md` §10 |
| P26 | **40 分鐘即時串流（20:33–21:13）期間眼鏡沒有再重開機**：uptime 連續 3504s、boot history 最新仍是 20:11:55 那次、dmesg 無 panic／oops／RCU stall（3 個命中全是開機字串）。⇒ **沒有 root 的這 58 分鐘完全穩定**（期間反覆操作觸控板、開助手、拍照錄影、多次重裝 APK），而今天四次重開都發生在有 root 武裝時 → 「重開機 ↔ 提權」相關性更強（仍非決定性）。⚠ 限制：`dmesg -w` 串流在 20:33 就斷線（adb 被後續指令／忙碌打斷且腳本不重連），要「等 panic 抓現場」需加自動重連 | `reports/camera_ring_failure_remote_diagnosis_2026-09-13.md` §13 |
| P25 | **AR 畫面「灰底」真正根因＝Android 預設焦點高亮（default focus highlight）**。`panel` 是 `clickable`，而眼鏡主力輸入是**觸控板（D-pad 類）而非觸控螢幕** → 面板**取得焦點** → Android 在其上疊一層**圓角、約 12% 白**的高亮。三個決定性實驗：①把 `windowBackground` 暫改純綠，灰底區量到 `(31,255,31)`（＝綠被疊 12% 白）⇒ 是**畫在我們視窗之上**；②把 `paddingBottom` 由 8dp→44dp，灰帶**跟著變成 y=544..640**（= 完全等於 `panel` bounds）⇒ 排除「系統固定 48px bar」；③切到啟動器（無聚焦 View）同區為**純黑**。**修正：`android:defaultFocusHighlightEnabled="false"`**，驗證後該區由 `(31,31,31)` 變 **`(0,0,0)`**（只剩文字）。⚠ 同時**回退 `windowIsTranslucent`**：實測會讓背後啟動器整個透出來與儀表板疊在一起（「混在一起」）；**AR 上「透明」的正確實作是不透明黑**（黑不發光＝看穿，且擋掉背後 App）。全螢幕沉浸對該高亮無效 | `reports/camera_ring_failure_remote_diagnosis_2026-09-13.md` §12 |
| P27 | **WiFi 媒體通道原本完全不能用＝兩個獨立的真 bug（皆已修並實測通過）**。①`NEARBY_WIFI_DEVICES` **在 Manifest 有宣告、執行期從未要求**（`granted=false`）→ Android 13 的 `WifiInfo.getSSID()` 只回 `<unknown ssid>` → `WifiLink.isOnAp()` 永遠 false → 加入 AP 後仍判定失敗；**修正**：API 33+ 一併在執行期要求，且 `isOnAp()` 改為**優先看「該 Network 的本機 IPv4 是否落在 眼鏡 AP 子網（192.168.43.x）」**（免權限），SSID 比對退為備援。②TCP socket **未綁定到 `WifiNetworkSpecifier` 加入的網路**（該網路不是 App 預設網路）→ 實測錯誤 `failed to connect to /192.168.43.1 from /10.225.98.247`（走行動網路）；**修正**：`Network.bindSocket(socket)` 後才 connect。**實測**：`wifi connection accepted from /192.168.43.205`、TCP ESTABLISHED、`LIST_ALBUMS/LIST_MEDIA/DOWNLOAD_MEDIA` 全通、縮圖下載成功 | `reports/wifi_transfer_fix_2026-09-13.md` |
| P24 | **AR 畫面「灰底」：我們的面板沒有畫底色，但主題把整個視窗設成不透明黑（矛盾，已修）**。`activity_main.xml` 的註解寫「全透明（無底色、無面板背景）」且三個容器都真的沒有 `android:background`；但 `themes.xml` 原本 `windowBackground=@android:color/black` → **整個 480×640 視窗被不透明黑蓋住**。已改為 `transparent` ＋ `windowIsTranslucent=true` ＋ `backgroundDimEnabled=false`（`dumpsys window` 由 `fmt=OPAQUE` 變 `fmt=TRANSPARENT`）。⚠ **此段的「透明」處置已被 P25 推翻**：`windowIsTranslucent` 會讓背後啟動器透出來與儀表板疊在一起，已回退為**不透明黑**（AR 上「透明」的正確實作），灰底真因見 P25。另把 FGS 通知改到新通道 `capture_quiet`（`IMPORTANCE_MIN`＋靜音；通道 importance 建立後無法調降故必須換 id）。⚠ 使用者截圖中的灰底是 **`#1F1F1F` 圓角卡**，而該色在整個 App **不存在**、裝置 screencap 也量不到 → **不是本 App 的 layout 畫的**，較可能是系統替視窗加的外框或鏡像工具 UI；已請使用者複驗 | `reports/camera_ring_failure_remote_diagnosis_2026-09-13.md` §11 |
| P21 | **2026-09-13 19:55 以單次受控流程取得 root（無 reboot）**：避開 `get_root_now.sh` 的外層 `adb reboot` 迴圈，改為 `root_bridge.sh`（常駐 helper）＋把 `trigger_root_daemon.cmd` 寫進 `ghostlock_bridge/last.cmd` ＋每 10 秒監看（含 uptime 倒退偵測）。**約 130 秒成功**，`uid=0 … u:r:kernel:s0`、`getenforce=Permissive`、`root_daemon`＋keepalive 常駐。完成後實測：拍照 `ok IMG_1789300777925.jpg`、錄影 `ok VID_1789300796405.mp4`、眼鏡觸控板 `RING_STATUS running=true device=/dev/input/event1`、`CaptureService isForeground=true`。⚠ 三個坑：①**手機 `SET_RING` 會把眼鏡 `pattern` 覆寫回 `RGR`**（`pattern==null` 時 `orDefault(null)` 回 `RGR`）→ 固定來源請用 `RING_START --es device /dev/input/event1`；②`am start` 在 Activity 存活時只 resume，`onCreate` 不重跑故不會再叫服務，且螢幕睡著時上不到前景 → 需 `KEYCODE_WAKEUP`＋`force-stop`＋`am start`；③眼鏡忙於拍照時 `DUMP_STATUS` 會 request timeout | `reports/camera_ring_failure_remote_diagnosis_2026-09-13.md` §8 |
| P20 | **「戒指裝置路徑失效後靜默沿用」是真 bug（已修）**；另查出**相機反覆消失的機制＝螢幕睡著時 Activity 上不到前景 → Android 12 以背景啟動為由拒絕 FGS → 服務起不來 → `graph.camera=null`**，處置為 `KEYCODE_WAKEUP` ＋ **`am force-stop`** ＋ `am start`（單純 `am start` 只會 resume，`onCreate` 不重跑故不會再叫服務）。⚠ 我新增的 `deviceExists()` 初版有缺陷：沒 root 時 `listDevices()` 回空清單會被誤判成「裝置不存在」而清掉設定，已修為空清單即回 true。`RingControlManager.start()` 原本 `devicePath?.takeIf{isNotBlank()} ?: autoDetect()` —— prefs 一旦寫入 `ring_ctrl.device` 就永遠沿用且**不檢查該路徑是否還存在**；2026-09-13 為測 RGR06 設成 `/dev/input/event4` 後，使用者改按眼鏡觸控板時程式仍只讀 event4，**完全靜默**（回 true、狀態顯示執行中）。已修：新增 `RingEvdevReader.deviceExists()`，路徑失效即回退 `autoDetect(pattern)`。另：`camera_not_ready` 改為可行動訊息「相機未就緒（請在眼鏡上開啟 App）」（14 語系）；手機 `DebugReceiver` 新增 `DUMP_STATUS` 且拍照／錄影失敗會記錄真正原因 | `reports/camera_ring_failure_remote_diagnosis_2026-09-13.md` |
| P12 | 🔴 **root daemon 的 socket 是 `0777`，且執行期 SELinux 為 Permissive → 任何 App 可直接取得 root**（2026-09-13 實測）：`getenforce` ＝ **Permissive**；`/data/local/tmp` ＝ `drwxrwx--x`（other 可穿越）；`/data/local/tmp/root_daemon.sock` ＝ **`srwxrwxrwx root root`**。`deploy/root_daemon.c:132` 即 `chmod(..., 0777)`。這是**本專案自己引入**的介面，也是戒指路徑 B 的信任基礎（`RingEvdevReader` 所有 `exec` 都經它）。※「untrusted_app 實際可連」為權限位元推論，**未**以非 root uid 實連（§9-E） | `getenforce`／`ls -ldZ`；稽核報告 §2 R10 |
| P13 | **`scripts/rokid_ota_check.py` 寫死 OTA API 憑證**：`KEY`／`SECRET`／`DEVICE_TYPE_ID`（其中兩項實機確認來自 bootloader 屬性 `ro.boot.secret`、`ro.boot.devicetypeid`，為**燒錄的 provisioning 值**）。應改為執行時讀 `getprop` 或環境變數 | 稽核報告 §2 R11；`getprop` 實測 |
| P14 | **硬編碼稽核工具已補 3 條規則**（`scripts/audit_hardcode.py` 第 6／7／8 節）：exported 且無 permission 的元件、憑證型字面值、world-accessible `chmod`。補完立刻抓到 P12／P13。另：`"RGR"` 已集中為 `shared/ring/RingIdentity.kt`（11 處引用改寫，Kotlin 兩 App 編譯通過）、`ring_input_daemon.c` 的 `chmod` 已改 `0600` | 稽核報告 §4.1 |
| P11 | **戒指「唯一識別依據」`"RGR"` 被寫死 11 處**（`RingBonder.kt:109`、`RingWatcher.kt:31`、`RingEvdevReader.kt:48,51`、`DebugReceiver.kt:145,162`（手機）、`SettingsFragment.kt:204`、`CommandRouter.kt:92`、`RingControlManager.kt:85`、`DebugReceiver.kt:53,102`（眼鏡）、`ring_input_daemon.c:13`）；`RingBonder.scanAndBond()` 會**自動配對第一個廣告名稱含 `RGR` 的可連線裝置，無使用者確認**（BLE 名稱由對方自行宣告）。`scripts/audit_hardcode.py` **未**檢查此類「安全參數型字面值」，也未檢查 `exported`／`permission`／憑證字面值／native `chmod` | 稽核報告 §2 R3/R6/R8 |

| P28 | ✅ **媒體「一直在藍牙、10 秒影片極慢」的真因＝傳輸通道互斥＋沒有自動建立 WiFi（2026-09-13 已修並實測）**。①眼鏡端 `BluetoothServer`／`WifiServer` 在 `accept()` 時**都**執行 `graph.connection?.close()` ⇒ 兩條通道**互斥、後連上者勝**；②手機端 `Remote.request()`（**含 `GET_STATUS`／`LIST_ALBUMS`／`LIST_MEDIA` 等所有一般指令**）硬走藍牙，且 `ensureConnected()` 一見藍牙斷線就重撥 ⇒ **WiFi 一連上→眼鏡關掉藍牙→下一次分頁刷新就重連藍牙→眼鏡再關掉 WiFi socket→媒體靜默回退藍牙**（數秒內翻轉一次）；③`ensureWifiMedia()` 全 repo 只有 2 個呼叫點（設定頁開關、`DebugReceiver`）⇒ **任何媒體操作都不會自動建立 WiFi**，而開關預設關閉。**修正**：新增 `Remote.link()`（WiFi 優先、藍牙回退）並讓 `request()` 改走它（⚠ 首版 `request()` 其實仍 inline 判斷、`link()` 成死碼，**經對抗式驗證抓到後已修**，並刪除同樣零呼叫的 `mediaClient()`）；`ensureConnected()` 在 WiFi 已連線時不再重撥藍牙；`requestFile()` 前呼叫新的 `ensureMediaLink()`（自動開熱點→加入 AP→連 TCP，`Mutex` 去重、失敗 60 秒冷卻、自動模式熱點等待上限 30 秒，失敗才回退藍牙）；`isConnected` 改為 `client.connected \|\| tcp.connected`；設定頁開關改為持久化偏好 `auto_wifi_media`（**預設開啟**）；`WifiLink` 的 specifier callback 改為**刻意保留註冊**（反註冊會被系統拆掉連線）。⚠ **驗證後追加修正（Rev.6）**：①原本只註冊 `client.onPush`，而眼鏡 `AppGraph.push()` 走**當前連線** ⇒ **WiFi 為當前通道時 `PROGRESS`（LPE 進度）等推播全被丟棄**（把控制改走 WiFi 後**新引入**的回歸）→ 新增 `Remote.onPush` 集中屬性，setter 內同時掛 `client` 與 `tcp`；②`LinkClient.disconnect()` 原本不通知 ⇒ 按「斷線」／關 WiFi 通道時畫面停在舊傳輸方式 → 改為由連線轉斷線時呼叫 `onDisconnected`；③`lastAutoWifiFailAt` 加 `@Volatile`。**附帶條件（如實記錄）**：兩通道互斥是 accept 端 check-then-act（**非原子**，理論上可同時活著，未實測到）；`WifiServer.stop()` 不關 `graph.connection`（與 `BluetoothServer.stop()` 不對稱）；`ensureWifiMedia()` 的手動路徑（設定頁／DebugReceiver）**不取 Mutex、不受冷卻限制**；「失敗才回退藍牙」精確說是「沒拿到 TCP 就回藍牙」；「三態顯示」**只適用遙控分頁**，設定頁為四態（多「自動模式待命」提示）。**2026-09-16 補跑實機驗證（`reports/index_resilience_verification_2026-09-16.md`）**：①控制指令確實走 TCP（刻意不送 `CONNECT`，`LIST_ALBUMS`/`LIST_MEDIA` 由眼鏡的 TCP handler 收到）；②**TCP 通道上的推播已可用**（眼鏡 `PUSH_TEST … wifi=true bt=false` → 手機 `RemotePush: progress code=gl_attempt arg=verify-tcp`），藍牙對照組亦通；③關閉 WiFi 通道時 UI 立即由「WiFi 192.168.43.1:8555」變「藍牙」（前後截圖）。驗證時 root 不存在（裝置自行關機），SoftAP 不可用，故 TCP 通道以 **adb forward+reverse tunnel** 建立——眼鏡端走同一組 `WifiServer`/`ConnectionHandler`/`CommandRouter`，僅底層傳輸不同。**實測（同一支 10.6 秒／16,736,320 bytes 影片，`TEST_TRANSFER` 指令自報）**：**WiFi 3,786 ms／4,420.6 KB/s**，對照**藍牙 323,956 ms（5 分 24 秒）／51.7 KB/s，且因 `transferTimeout` 誤設 60 KB/s 而逾時失敗 ⇒ 相差約 85 倍**（已把估算速率下修 45 KB/s）；自動建立 WiFi 全程免手動（`媒體通道：建立 WiFi 中…`→`WiFi 192.168.43.1:8555`；冷啟動 4–11 秒、同 session 後續傳輸 **2,990 ms／5,597 KB/s**，且當時**藍牙是斷的**（`connected=false`）⇒ 一般指令確實也走 WiFi）。**UI 標示同步（同日 23:03 修）**：原本只有 `onDisconnected` 會刷新畫面，而眼鏡在 WiFi accept 時會關閉藍牙 → 刷新恰好落在「WiFi 還沒連上」的瞬間 → 標示卡在「傳輸：藍牙」（但傳輸其實已在 WiFi，所以「很快」）。已新增 `LinkClient.onConnected`、`Remote.onTransportChanged` 單一通知點、協商中顯示「WiFi 連線中…」，並把設定分頁納入刷新；三張截圖驗證：藍牙 → WiFi 連線中… → WiFi 192.168.43.1:8555 | `reports/wifi_transfer_fix_2026-09-13.md` §6–§10 |
---
| P29 | 🔴 **眼鏡端媒體索引會「無聲遺失」整批項目（2026-09-13 實際發生，已修並自我修復）**。實測 23:04 `media/index.json` 只剩 1 筆（相簿 7 筆、**檔案全在磁碟上**）；手機 `LIST_MEDIA_INFO` 22:51 `total=7` → 23:05 `total=1`。根因是索引為**單一 JSON 全量覆寫**且兩處脆弱：①`load()` 的解析迴圈**共用一個 try/catch** —— 任一筆 `MediaItem.fromJson` 丟例外，外層一吞，**其後全部不載入**（本例 albums 有載到、media 全空，故相簿 id 與 createdAt 都保留）②`persist()` 用 `File.writeText()`（先 truncate 再寫），程序若在中間被殺（LMK／force-stop／`adb install -r`）會留下空或半截檔。**修正**：解析改**逐筆 try/catch**（⚠ 首版**只保護了 `media` 陣列**，`albums` 迴圈仍共用外層 try —— `Album.fromJson` 用 `getString("id"/"name")` 同樣會丟例外，**一筆壞 album 仍會讓 album 與整個 media 全滅**；經對抗式驗證抓到後已補上 `album entry #i skipped`）；新增 **`reconcileWithDisk()`**（載入後掃描各相簿目錄，把「在磁碟但不在索引」的檔案重新登記，索引不再是最唯一真相）；`persist()` 改**原子寫入**（`.tmp` + `renameTo`）。**反向校正（Rev.6 追加）**：`reconcileWithDisk()` 原本**只增不減** ⇒ 被 App 以外方式刪掉的檔案會變成**永久幽靈項目**（清單仍顯示、每次 persist 又寫回、`DOWNLOAD_MEDIA` 回 `media not found`），且 `getThumbnail()` **不檢查來源檔就回舊快取縮圖**；已改為「索引有、磁碟沒有 → 移除該筆並刪縮圖（log `-$dropped`）」，且縮圖先確認來源檔存在。**實機驗證**：安裝後重啟眼鏡 App → `W MediaRepository: index reconciled: 6 file(s) re-registered from disk`，手機清單回到 8 筆。⚠ **另有 3 個檔案在 22:58–23:05 之間從磁碟消失**（`VID_1789308941304.mp4` 等，**與索引問題無關**）：可排除 App 刪除路徑（`deleteMedia()` 會同步 persist 索引，不會變成只剩 1 筆）與眼鏡端自動清理（無掃描刪未知檔的邏輯）；logcat 已輪替無 `DELETE_MEDIA` 可查 ⇒ **使用者已確認 3 個檔案是自己刪的**（§15）。⚠ **後續對帳發現索引問題更早、更嚴重**：22:45 磁碟有 **13** 個 App 拍攝檔、索引只有 **7** 筆⇒ **6 筆「幽靈檔案」在 23:04 之前就已存在**（無法瀏覽也無法下載），且正好是 `reconcileWithDisk()` 救回的那 6 筆。**A/B 實驗（決定性）**：把索引第 4 筆的必填 `fileName` 移除、其餘 8 筆完好 → **舊版 `1.0-safe`(code 3)**：`index load failed`（stack 被外層 catch 吞掉）→ 清單 **total=4**（第 4 筆之後 5 筆全消失）；**新版 `1.0-safe.2`(code 4)**：`media entry #4 skipped` ＋ `index reconciled: 1 file(s) re-registered` → 清單 **total=9**（一筆沒少）。⇒ 機制確認：一筆解析失敗 → 其後全部不載入 → 任何一次後續 `persist()`（拍照／錄影／產縮圖都會觸發）把變短的清單**永久寫回** | `reports/wifi_transfer_fix_2026-09-13.md` §13–§16 |
| P30 | ⚠ **3 支「無法播放」的影片佔 1.4 GB（MP4 未收尾）＋ 手機端 63 MB 傳輸殘骸（皆已處置）**。①`VID_1789290250579.mp4`(636.9 MB)、`VID_1789304656692.mp4`(664.7 MB)、`VID_1789152641290.mp4`(111.9 MB) 三者在索引中 `durationMs=0`，且**尾端掃不到 `moov` atom**（同批對照組 `VID_1788822710658.mp4` 有 moov、duration=15139）⇒ 錄影在收尾前被中斷（裝置重開機／App 被殺），**檔案不完整、無法播放**，建議刪除。②手機端傳輸協定是「先寫 `<transferId>.part`、完成才 rename」，任何失敗（含藍牙逾時那次）都留下**完整大小**的殘骸——實測累積 **5 個檔、63 MB**。已清除，並新增 `LocalStore.cleanupStaleParts()`（App 啟動時刪 >24 小時的 `.part`；**以目前最大檔 664 MB 計 timeout 為 4.115 小時**（公式 `size/45KB/s + 45s` 本身無上限；`.part` 每寫一個 chunk 就更新 mtime ⇒ 進行中的傳輸不會被誤刪，要誤刪需「連續 24 小時零寫入」，反推需 size > 3.62 GiB）。**③（23:29 已執行）**使用者指示刪除該 3 支未收尾影片，**走 App 路徑**（`DELETE_MEDIA` → `deleteMedia()`，同時刪檔／刪縮圖／persist 索引；若用 `rm` 會留下懸空索引）：`med-eba0d12e`／`med-0e50b012`／`med-a918b5ef` 皆 Success，**釋放 1,348.0 MB**，對照組 `VID_1788822710658.mp4`(`med-86d1521c`) 保留。刪後磁碟 6 檔＝索引 6 筆＝手機清單 6 筆，重啟 App 無 reconcile 訊息（一致性成立），儲存空間可用 11 GB。⚠ 該 3 筆刪除後索引與磁碟一致；**Rev.6 追加反向校正**後，日後若再有人從 App 外刪檔，清單會在下次啟動自動移除該筆。**2026-09-16 補跑實機驗證**：①把 `albums[0].name` 移除 → `album entry #0 skipped` **且 media 完全不受影響（total=14）**；②磁碟放測試檔 → `+1 re-registered`、手機 total 14→15；③外部刪除該檔 → `-1 removed (file missing on disk)`、手機回到 14；④每次改動前備份、結束還原，最終索引與磁碟一致（14＝14） | `reports/wifi_transfer_fix_2026-09-13.md` §17–§18 |

## 8. 17 組矛盾的裁決（對照稽核報告 §2）

| # | 爭議 | **裁決** | 依據 |
|---|------|----------|------|
| 1 | CVE-2024-36971 修補版本（5.10.219 vs 220+） | **Debian 5.10 修補於 `5.10.221-1`**（上游 `92f1655a`） | §5 C2 |
| 2 | CVE-2024-36971 可利用性 | **本機不可觸發**（無 route／ICMP 面） | §5 C2 |
| 3 | CVE-2024-0582 影響範圍 | **5.10 不受影響**；**本機實測 `PROVIDE_BUFFERS`=EINVAL**（§5.1） | §5 C3、§5.1 |
| 4 | `msgsnd` spray | **不可用**（`CONFIG_SYSVIPC is not set`） | §2 K2 |
| 5 | GhostLock symbol offsets | **v9.29 後值**（`INIT_CRED=0x02202058`、`SELINUX_STATE=0x02ca0140`） | §6 G3 |
| 6 | `data_addr` 別名 | **V10 正確**：須 `data_addr`→`text_addr` | §6 G4、V10 附錄 E |
| 7 | panic 現場取得方式 | **ramoops 已配置（2 MB @0xf8e00000）＋ `console=ttyMSM0`**；watchdog 重開機不寫 ramoops | V11 L190-206（未於本日複核裝置） |
| 8 | `miscdevice.fops` 偏移 | **`+0x10`**（`ashmem_misc=0x29bcbb0`） | §6 G5 |
| 9 | reclaim uptime 門檻 | **>~400 s**（150 s 只是下限，非充分） | W2 blocker UPDATE 2（未於本日複核） |
| 10 | GateServiced 錯誤傳遞／能力 | **已實測釘死（§5.2）**：線上「回應＝成功、timeout＝錯誤」正確；`300c` 僅 logcat；能力集 `000001ffffffffff` 是**行程 capability 遮罩**而非協定能力；線上只有三支構詞 | §5.2 S1–S9 |
| 11 | i2c `0x30`／`0x08` 身分 | **`0x30=aw2110x`（LED）、`0x08=psoc-tp-right`（觸控）** | §3 H1/H2 |
| 12 | CVE-2025-21479/21480 歸屬 | **21479=GPU micronode（Android 相關）；21480=Windows Graphics（非 Android）** | §5 C5/C6 |
| 13 | CVE-2023-2163 狀態 | **已修補（5.10.179）** | §5 C4 |
| 14 | 裝置／韌體世代 | **現行 1.22.009／`gd908d71a0404`**；舊報告須標世代 | §1 D2/D3/D7 |
| 15 | KASLR／kernel exploit 可行性 | **可行**：KASLR 由 `printk_formats` 洩漏，leak→root 84–155 s | §6 G4、§4 R3 |
| 16 | USB config 值 | **`adb,diag`** | §2 K9 |
| 17 | HANDOFF V5 vs V6（重現性／pad／版本） | **V6 為準**：pad 預設 0、payload 落點 block+0、版本 v9.60 | §6 G1/G2 |

---

## 9. 未解／待證（2026-09-13 大幅收斂）

> §9 原本 6 項，本輪全部收斂：**KGSL ioctl → §5.1／§5.1.1**、**GateServiced → §5.2**、
> **CVE-2026-43499 對外狀態 → §5 C1**、**ramoops／uptime → §6.1**、**缺失二進位 → §9.5**（並搶救 10 個不可重建者）。
> 隨後衍生 5 項（第 6–10 項）：**4 項已解**（`0x300A`、空檔 `3001`、`dma_unalloc` 空間、payload 檔出處），
> **第 10 項改為「已列舉、不執行」**。**目前僅剩 3 個真正無法由現有資源回答的問題**（見本節末）。

**仍真正未解（需 root 或外部資源）**

1. ~~**GateServiced 的成功 EXEC_CMD 路徑**~~ → ✅ **已演練並結案（§5.2 S10–S13）**：載入 gate TA 後回 load ta succeed，EXEC 回 TA 原始輸出（25.05.06.01）。
2. ~~**load_app 12292／run_load_ta 12298 的根因**~~ → ✅ **已解（§5.2 S15）**：get_file_size() 回 -1 → calloc(2^64-1) NULL → 0x3004，尾端硬編碼覆寫成 0x300a；load_app 只是 inline 後的 log tag。
3. ~~**外部 URL 細節**~~ → **已抽查 56 個並結案（§9.6）**：唯一失效者已修（projectzero），其餘為佔位符或限流。
4. ~~**桌面工具數字**~~ → **已用其自身工具實測並記錄（§9.6）**：228×14=3192、134/28/26/22、exe 901,721 B；另註明 `i18n_master.json` 非權威來源。
5. ~~**搶救回來的二進位語意**~~ → ✅ **已完成靜態分析**（`reports/static_re_gate_codes_and_rescue_2026-09-13.md`）：10 個為 NDK aarch64 探針（各帶原始檔名：io_uring／KGSL／netfilter／DSP×5／ashmem／eBPF KASLR leak）；**`fakepayload` 是 4096 bytes 全零頁、`min_payload` 是 24 B `CrAU` OTA `payload.bin` 標頭殘檔**（皆非執行檔）。

**新未解（本輪衍生）**

6. ~~`0x300A` 為何被硬編碼~~ → ✅ **已判定為原始碼層常數且有出處（§5.2 S18）**：「為何」＝**對公開兄弟碼 `mink` 的在地分歧**（`mink` 原樣回傳 loader 錯誤值，GateServiced 硬換成 `0x300a`），而 `0x300a` 本身在**所有公開碼空間中皆不存在**（信心 ~95%）。
7. ~~「真正空檔（0 B）→ `3001`」~~ → ✅ **已實測確認（§5.2 S16）**：logcat `failed: 12289`。
8. ~~`dma_unalloc` 回傳值空間~~ → ✅ **已解並已實測修正（§5.2 S19／S20）**：`{0,-1}`；洗碼 bug 為真但觸發條件是「`dma_alloc` **成功後**才失敗」；原「未初始化 fd 槽」與「heap 名稱是堆疊指標」兩說**已由實機探針推翻**。
   仍待：`get_file_size()` 回 `-1` 是否刻意（原始碼不可得；不影響任何線上行為）。
9. ~~`min_payload` 由哪支腳本寫出／`fakepayload` 用途不可考~~ → ✅ **已定位（§5.2 S17）**：2026-08-14 17:28 由 shell 寫入，屬當日 update_engine／payload gate 實驗的暫存輸入（`min_payload`＝CrAU 標頭殘檔、`fakepayload`＝全零頁）；無任何引用。
10. ~~**DTX/gate TA 的破壞性命令空間**（刻意未探測，需另立授權）~~ → ✅ **已靜態完整列舉並分級、仍未執行任何新命令（§5.2 S21／S22）**：命令 id、handler、子命令 `op`、SFS 物件名、持久化後端與破壞半徑皆已還原（TA 未 strip，754 具名函式）。**本輪明確決定不發出任何探針**——理由：報告 §5 所列「安全」探針（group 0/1）**只會重測已知事實**（`0x10002` → `25.05.06.01`），資訊增量為零；而一旦跨進 `0xA/0xB/0xC`，`op=1`（讀）與 `op=2`（不可逆覆寫）在 V8 編碼器裡僅差一個位元組，誤送即可能**永久毀損 TEE 內的啟用記錄／根金鑰**（且 factory reset 不預期可還原）。故**維持不執行**，直到有明確授權與可丟棄的測試機。
    **真正仍未知（不可由現有二進位判定）**：`0xC0001` op5 語意、TEE 端／smcinvoke 對 gate TA 的 **ACL**（`untrusted_app` 可達性未證實）、本機 TEE SFS 是否隨 factory reset 還原、TA metadata 尾端 `p=8:…` 權限描述元的解碼。

**§9 收束後的真正未解（3 項，皆非可再攻的技術缺口）**

| # | 未解項 | 為何無法用現有資源回答 | 若要解，需要什麼 |
|---|--------|------------------------|------------------|
| A | `get_file_size()` 失敗回 `-1` 是否**刻意** | 供應商原始碼不可得；二進位只能證明行為，不能證明意圖 | 取得 `GateServiced` 原始碼／廠商說明 |
| B | TEE 端／smcinvoke 對 gate TA 的 **ACL**（非 root、`untrusted_app` 能否載入並呼叫） | 屬 TEE 與 SELinux 政策，不在這兩個二進位內；本次實證為 root ＋ Permissive | 乾淨未 root 機上以 `untrusted_app` 實測（目前無此機） |
| C | 本機 **TEE SFS 是否隨 Android factory reset 還原**；以及 `0xC0001` op5 語意 | 前者需破壞性實驗（會毀損 SFS）；後者需在 TEE 內動態追蹤 | 可丟棄測試機＋明確授權；或廠商文件 |
| D | ~~**第三方 App 對兩端 `DebugReceiver` 的端到端投遞實證**（P9）~~ | ✅ **已結案（2026-09-13 15:45）** | 以自製最小 App（uid 10346／10064）實測投遞成功並被執行；見 `reports/ring_reachability_test_2026-09-13.txt` |
| E | **`root_daemon.sock` 對 untrusted_app 的實際可達性**（P12） | 目前為**權限位元 ＋ `getenforce`** 的靜態判定（`0777`＋Permissive）；**未**以非 root uid 實際 `connect()` 測試（會產生一次真實 root shell 連線） | 以一個非 root uid 的行程對該 socket 送 `id` 並觀察回傳（需授權；有副作用） |

> 其餘所有先前「未解」項目均已結案；上表 A 為**意圖**問題（不影響任何已證行為），B／C／E 為**授權／資源**問題；D 已結案。

## 9.5 缺失二進位清單（2026-09-13 完整盤點）

> 完整表格（320 個名稱、逐項分類）見 **`reports/missing_binaries_inventory_2026-09-13.md`**。

| 類別 | 名稱數 | 可重建（原始碼精確存在） | 僅有變體原始碼 | 完全無原始碼 | 裝置上仍有 |
|------|-------:|------------------------:|---------------:|-------------:|-----------:|
| probe | 125 | 92 | 14 | 19 | 70 |
| exploit | 119 | 77 | 26 | 16 | 19 |
| daemon | 13 | 4 | 0 | 9 | 7 |
| firmware image | 45 | 0 | 0 | 45 | 7 |
| kernel module | 2 | 0 | 0 | 2 | 0 |
| other | 16 | 0 | 0 | 16 | 1 |
| **總計** | **320** | **173 (54%)** | **40** | **107** | **104** |

**結論**

1. **repo 本質上是 source-only**：只有 18 個名稱在工作區有任何副本 → 編譯產物從未納版控（與 `reports/docs_correctness_audit_2026-09-13.md` 一致）。
2. **真正損失 79 個**（無原始碼、無 repo 二進位、裝置上也沒有）：`cve_scanner`、`ashmem_overflow`、`binder_uaf_v2/v3/v4`、`cve40094_test`、`cve_22555_check/test`、`kgsl_exploit_v7`、`kgsl_parasitic_inject`、`shizuku_server`、`hal_proxy_daemon`、`binaries/extracted_1.22/kernel`、`msm_kgsl.ko` 及所有 boot/vendor 映像。
3. **27 個只存在裝置上**（`/data/local/tmp`）：會因 factory reset／OTA／清空 `/data/local/tmp` 而消失。
   其中 **10 個連原始碼都沒有** → **已於 2026-09-13 搶救回 `binaries/device_rescue/`**（另含 2 個來歷不明的 payload，共 4.6 MB；清單與說明見該目錄 `README.md`）。其餘 17 個可用 `source_code/` 重建。
4. **三個缺失目錄**：`binaries/`（含 47 MB 的 kernel Image，**只能從 OTA/韌體 dump 重新取得**）、`source_code/ghostlock_rv101/build/`（**可用 `make` 重建**）、`binaries/device_probes/`（5 個 `build2_*` 僅存於裝置）。
5. **可重建路徑**：`source_code/ghostlock_rv101/Makefile` + `src/{main,util,route}.c` + `target_rv101.h`；探針類另有本次新增的 `deploy/build_probe_zig.sh`（zig 交叉編譯）。

## 9.6 外部連結與桌面工具數字（2026-09-13 實測）

**外部 URL**（來源：`reports/*.md`＋`docs/*.md` 共 178 個不重複 URL；抽查 56 個）

| 結果 | 數量 | 說明 |
|------|-----:|------|
| HTTP 200 | 48 | 正常（GitHub PoC、NVD、source.android.com、Rokid 官網／CDN、t.me、Qualcomm 公告…） |
| 000（不可達） | 4 | 全是 PoC 模板佔位符（`http://ATTACKER_IP/steal`、`http://10.10.10.10`），**非缺陷** |
| 429 | 1 | `git.codelinaro.org` 限流（連結本身有效） |
| 404 | 1 | `googleprojectzero.github.io`（已改為官方 bughunters deep-dive 連結） |
| 401 | 1 | `ota.rokid.com/v1/extended/ota/check` 需驗證（**正常**：該 API 需簽章） |

> 原始清單：`reports/url_check_2026-09-13.txt`

**★ 重大發現：缺失的 kernel Image 有復原來源**

`https://ota-g.rokidcdn.com/YodaOS-Sprite/OS/Rokid/1.14.006-20260204-150203/RVE01-ota-1.14.006-20260204-150203.zip`
→ **HTTP 200、`content-type: application/zip`、`content-length: 936,991,431`（≈937 MB）仍可下載**。
這推翻 `reports/ROOT_PROCEDURE.md` 的「官方 OTA 韌體未找到下載來源」（至少 CDN 直連可行），
也表示 `reports/missing_binaries_inventory_2026-09-13.md` 指為「唯一不可恢復」的
`binaries/extracted_1.22/kernel`（47 MB raw ARM64 Image）**可從 OTA zip 重新解出**
（注意：此 zip 為 **1.14.006** 世代；要 1.22.009 的 Image 需以相同 CDN 模式找對應版本）。

**桌面工具（ZeroLightRokid Desktop）數字：以工具自身實測為準**

> ⚠ 本節「指令」欄的相對路徑（`tools/gen_i18n.py`、`tools/verify_exe.py`、`tools/i18n_master.json`、
> `zero_light_desktop.exe`）皆屬**外部桌面工具專案**（`ZeroLightRokidDesktop/`，見 §2.2），
> **不是本工作區的 `tools/`**（本工作區 `tools/` 只有 JDK 與 jadx）。

| 項目 | 報告舊值 | **實測（2026-09-13）** | 指令 |
|------|----------|------------------------|------|
| i18n 覆蓋 | 191 字串／2674 筆 | **14 語系 × 228 字串 = 3192 筆** | `python3 tools/gen_i18n.py --check` |
| 編入執行檔 | 2674 筆 | **3192 筆全部編入**；阿拉伯文無 bidi 控制字元（RTL 由 `WS_EX_LAYOUTRTL`） | `python3 tools/verify_exe.py zero_light_desktop.exe` |
| 格式呼叫 | 149／44 | **134 多語系、28 字面、26 `T()`、22 寬格式**，引數個數全數相符 | `python3 tools/check_logfmt.py` |
| exe 大小 | 813 KB（x64） | **901,721 B** | `ls -la` |
| ⚠ 注意 | — | `tools/i18n_master.json` **只有 3 語系 × 163**（405 筆）→ **不是** 228×14 的來源，勿當權威引用 | `json` 統計 |

---

## 10. 引用方式（給各報告）

在報告開頭或相關段落加一行，例如：

```markdown
> 本節事實以 `reports/FACTS.md` §5 C4 為準（CVE-2023-2163 已修補於 5.10.179）。
```

已加註引用本表的報告（2026-09-13）：`VULN_VERIFICATION_INDEX.md`、`CVE-5_10_x_Scan_Report.md`、
`CVE-2024-36971_analysis.md`、`CVE-2024-36971_攻堅分析報告.md`、`CVE-2024-36971_deep_analysis.md`、
`CVE_2025_21479_EXPLOIT_SUMMARY.md`、`PARASITIC_INJECT_ATTACK.md`、`PARASITIC_INJECT_STATUS.md`、
`KGSL_FINAL_REPORT.md`、`KGSL_RESEARCH_REPORT.md`、`GHOSTLOCK_HANDOFF_V2.md`、`GHOSTLOCK_HANDOFF_V3.md`、
`GHOSTLOCK_HANDOFF_V5.md`、`GHOSTLOCK_HANDOFF_V7.md`、`GHOSTLOCK_HANDOFF_V10.md`、`GHOSTLOCK_HANDOFF_V11.md`、
`GHOSTLOCK_W2_CONFIGFS_READ_BLOCKER.md`、`GHOSTLOCK_W2_FOPS_CONFIGFS_RV101.md`、
`GHOSTLOCK_V5_RECLAIM_MECHANISM.md`、`Rokid_Glasses_Security_Report_EN.md`、`Rokid_Glasses_資安漏洞報告.md`、
`Rokid_Glasses_系統資料報告.md`、`Rokid_Glasses_系統資料報告_Marp.md`、`Rokid_漏洞報告信件.md`、`email.md`、
`Rokid_Attack_Vector_Summary.md`、`PRIV_ESC_SESSION_10.md`、`OTA_Authentication_Analysis_Report.md`、
`Rokid_Root_Guide_boot_img.md`、`ROOT_PROCEDURE.md`、`VULN_IMPLEMENTATION_AUDIT.md`、
`GateServiced_Phase1_RE_Report.md`、`GateServiced_Phase2_Exploitation_Playbook.md`、
`GateServiced_Phase2_Exploitation_Playbook_v1.md`、`KGSL_RESEARCH_SESSION_11.md`、`Rokid_Glasses_System_Report_EN.md`。

---

## 11. 變更紀錄

| 日期 | 變更 |
|------|------|
| 2026-09-13 | 新增 **§7 P20 遠端診斷**（拔線狀態下經手機藍牙取得 `camera_not_ready`/`ringEnabled=false`/`ringDevice=event4`）；修掉「戒指裝置路徑失效仍靜默沿用」的真 bug；`camera_not_ready` 改可行動訊息 |
| 2026-09-13 | **提權安全武裝四道保護已實作＋實測（§7 P19）**，並產出可區分的兩版 APK（`1.0-safe`／`1.0`，`dist/`）；報告 `reports/arm_safety_policy_2026-09-13.md` |
| 2026-09-13 | 新增 **§7 P18 眼鏡拔線後重開機分析**（主因＝exploit panic 的延遲後果；排除戒指／掉電／adb reboot；並揭露 BOOT_COMPLETED 被系統擋掉使自動恢復無效）；報告 `reports/glasses_reboot_after_adb_unplug_2026-09-13.md` |
| 2026-09-13 | 新增 **§7 P17 RGR06 配對取證**：手機端為戒指中止 SMP（非 App 問題）、眼鏡端發現並修掉 `RingBonder` 的 `ReceiverRestrictedContext` 崩潰後**配對成功**；報告 `reports/rgr06_pairing_failure_2026-09-13.md` |
| 2026-09-13 | **戒指／相機 P0＋P1 修正已實作、建置、安裝並實機測試**（新增 §7 P16；i18n key 193→203、strings 284→294；報告 `reports/ring_p0_p1_fixes_test_2026-09-13.md`） |
| 2026-09-13 | 新增 **§7 P15 AIVELA 戒指連線失敗取證**（LMK→FGS 被拒→錄影卡死→服務被停；已驗證以 `am start` 前景化恢復）；報告 `reports/aivela_ring_connection_failure_2026-09-13.md` |
| 2026-09-13 | 戒指稽核**追加**：第三方 App 投遞**已端到端實證**（P9、§9-D 結案）；新增 **P12 root daemon socket `0777`＋Permissive → 任何 App 可取 root**、**P13 OTA 憑證寫死**、**P14 稽核工具已補規則**；零風險清理已套用（`RingIdentity.kt` 集中 `"RGR"`、`chmod 0600`、`audit_hardcode.py` 第 6–8 節），Kotlin 兩 App **編譯通過** |
| 2026-09-13 | 新增 **§7 P7–P11 戒指操控資安稽核**：兩條路徑皆無來源裝置驗證、眼鏡目前實際監聽**自己的觸控板**、兩端 `DebugReceiver` exported 且無 permission、`:8555` 零認證＋AP 密碼寫死、`"RGR"` 寫死 11 處；§9 新增第 **D** 項未解（第三方 App 投遞的端到端實證）。報告：`reports/ring_capture_security_audit_2026-09-13.md`（**修復尚未套用**） |
| 2026-09-13 | 新增 **§5.2 S16–S22**：空檔→`3001` 實測、`0x300A` 硬編碼判定（含公開兄弟碼 `mink` 分歧，~95%）、`fakepayload`／`min_payload` 出處、`dma_unalloc` `{0,-1}` 與呼叫端洗碼；**S19/S20 兩則子結論經實機探針推翻並撤回**（`reports/gate_dma_alloc_heapname_probe_2026-09-13.txt`）；新增 **DTX/gate TA 命令空間完整靜態列舉與風險分級（S21/S22）**，S12 的「任何本機 App」適用範圍限縮為未證實 |
| 2026-09-13 | 新增 **§5.2 GateServiced 協定實測**、**§6.1 panic 取證與時機條件**、**§9.5 缺失二進位盤點**；C1 補上 CVE 對外時間線；§9 由 6 項收斂為 5 項真未解 |
| 2026-09-13 | 新增 **§5.1 KGSL／周邊 ioctl 本世代實測**（6 支探針、原始輸出 `reports/kgsl_ioctl_retest_2026-09-13.txt`）；C5 與 §9-2 隨之更新 |
| 2026-09-13 | 初版：建立 11 節事實表，裁決稽核報告 §2 的 17 組矛盾；裝置面事實以本機實測（`getprop`／`/proc/config.gz`／`sysfs`／`uname`）為準，CVE 面以 Debian tracker／NVD／Qualcomm 公告為準 |
