# Rokid RV101 提權自舉替代方案 + OTA 阻擋比較報告

> 日期：2026-09-09 · 純研究（未對實機執行任何操作）
> 對象：RG-glasses / RV101，Android 12，kernel 5.10.209-perf，韌體 1.22.009（GhostLock 漏洞版）
> 前提：bootloader 已解鎖；現行提權鏈（host-adb helper → ghostlock_rv101 → root_daemon socket）已實證，但「reboot 後必須一次 host adb 重啟 helper」是最大痛點。
> 本報告依據工作區實測文件（`docs/device-debug-notes.md`、`deploy/*.sh`、`reports/OTA_Authentication_Analysis_Report.md`、`reports/ROOT_PROCEDURE.md` 等）＋外部社群/官方文件推論；凡未經實機驗證者均標示「推論」。

---

## A. 手機當 adb host（免 PC）

### A.1 方案與原理

目標：用現有的 Redmi 21091116UG（Android 13 / MIUI）取代 PC，做「reboot 後重啟 helper」的例行任務。

**核心結論：可行，且是風險最低的 PC 替代方案。** 關鍵鏈路：

1. **眼鏡是 USB gadget（device），手機是 USB host（OTG）**。眼鏡本來就對 PC 枚舉出 adb interface（`adb devices` 已見 `<DEVICE_SERIAL> device`），所以眼鏡端**不需要也不該進 host 模式**——它在 USB 拓撲裡永遠是 device。所謂「host 模式」指的是**手機**要進 OTG host 模式供電並驅動眼鏡的 adb interface。
2. **手機端 adb client**：非 root 的 Android 手機無法直接跑普通 `adb` 二進位做 USB host（沒有 libusb 對 USB Host API 的橋接）。標準解是 [nohajc/termux-adb](https://github.com/nohajc/termux-adb)（Termux 套件 + 一個 helper APK）：它用 Android USB Host API（Java `UsbManager`）接管目標裝置，再把原生 adb fork 接到 libusb 上，於是在 Termux 內就能 `adb devices / adb shell`，完全免 root、免 PC。社群實測（[XDA 串](https://xdaforums.com/t/app-non-root-termux-adb-debug-one-android-device-from-another-connected-by-usb-otg.4515059/)）即「Android 對 Android、USB-OTG 除錯」。備選：`pkg install android-tools` 提供的 adb 二進位**單獨不能**做 USB host（無 host 權限），必須搭配 termux-adb；另有 Algmali-ADB-Link 等，但多走 WiFi/TCP，不適用（眼鏡無網路）。
3. **紅米 OTG 已知坑（重要）**：
   - 21091116UG = Redmi Note 11 系列，USB-C 支援 OTG；但 **MIUI 的 OTG 會在閒置 ~10 分鐘後自動關閉**，且需在「設定 → 更多設定 → OTG」手動開啟。這是 MIUI 的實測已知行為，會中斷長連線；重啟 helper 是短任務，影響小，但寫 script 前要先開 OTG。
   - **線材**：C-to-C 必須是「數據線」而非「充電線」；兩端都可能是 DRP 埠，role 協商失敗時 adb 不枚舉——可換方向重插，或用 C-to-A OTG 轉接頭強制手機為 host。
   - **供電**：手機 host 會對眼鏡供 VBUS（5V），眼鏡從手機吃電 → 手機掉電快；OTG 供流通常 500mA 上下，僅跑 adb 足夠。
   - 眼鏡端「供電/辨識」推論：眼鏡作為 gadget 仍會被 host 供電（充電），並以既有 adb interface 被辨識；無公開文件，此為通用 Android USB-gadget 行為推論。

### A.2 授權問題：把手機 adb key 直接寫進 `adb_keys`

新 host（手機）連眼鏡時，正常會彈「允許 USB 除錯」指紋 UI。但我們**已有 root（root_daemon socket）**，可繞過 UI 直接授權。

**`/data/misc/adb/adb_keys` 格式與屬性**（[AOSP adb keystore 文件](https://github.com/GrapheneOS/platform_packages_modules_adb/blob/17/docs/dev/keystore.md) + 通用實務）：

- **格式**：純文字，每個已授權 host 一行，內容**就是該 host 的 `adbkey.pub` 那一整行**：`<base64 公鑰 blob> <email/comment>`。注意它是 adb 專用的 mincrypt 格式公鑰，**不是 OpenSSH `ssh-rsa` 格式**，所以直接 append `adbkey.pub` 內容即可，不要轉格式。
- **權限/屬主**：`system:shell`，模式 `0640`（部分 build `0644`）。
- **SELinux label**：`u:object_r:adb_keys_file:s0`。目前 exploit 成功後已是 Permissive，寫入/`restorecon` 都不會被擋；但檔案若由 root 新建會帶 `root` 屬主與錯 label，需修回來（見下）。
- **生效時機**：adbd 在收到 AUTH 請求時會重新載入 key 清單，所以**append 後通常當下生效，不必重啟 adbd**；若某 build 不即時生效，可 root 下 `stop adbd; start adbd`（或 `setprop ctl.restart adbd`）強制重讀。

**雞生蛋解法（一次性，之後免 PC）**：手機先跑一次 termux-adb 讓它自動產生 `~/.android/adbkey.pub`；把這行內容經**既有通道**送進眼鏡：

- 通道一（推薦、已實證）：手機 App → 藍牙 RFCOMM → 眼鏡 App → 既有的 `RootDaemonClient` → `root_daemon.sock` 以 root 執行 append。眼鏡 App 已實作 `RootDaemonClient`，加一個「注入 adb key」指令即可，零新基礎設施。
- 通道二：趁現在還有一次 PC-adb 機會，直接 `adb shell` 把 key append 進去（一次性）。

注入命令（root，經 socket 或 adb root）：

```sh
printf '%s\n' '<手機 adbkey.pub 那一整行>' >> /data/misc/adb/adb_keys
chown system:shell /data/misc/adb/adb_keys
chmod 640 /data/misc/adb/adb_keys
restorecon /data/misc/adb/adb_keys   # 修正 SELinux label（若檔案原已存在可略）
```

驗證（不需 PC）：手機 Termux `adb devices` 應顯示 `<DEVICE_SERIAL> device`，**無任何 UI 提示**。

### A.3 若 USB-OTG 不可行，其他手機↔眼鏡通道

- **藍牙 PAN / adb-over-TCP：不可行**。眼鏡本身無網路介面（實測 `UnknownHostException`），且此 build 的 `persist.adb.tcp.port` 已被實測**無效**（重啟後 adbd 不綁 TCP 5555，見 `device-debug-notes.md` Q5）；`service.adb.tcp.port` 重啟即失。就算 BT-PAN/NAP 給眼鏡一個 IP，adbd 仍不聽 TCP → 死路。
  - **2026-09-10 實測補充**：root 下設 `service.adb.tcp.port 5555` + 重啟 adbd → TCP 5555 仍拒絕連線（此 build 完全不支援 adb-over-TCP），且 **adbd 重啟會讓 USB 列舉丟失**（需拔插 USB 或重啟裝置恢復）。`ENABLE_WIFI_ADB` 指令已改為非破壞性（只設屬性＋偵測，不重啟 adbd）。免線 root 只剩 Magisk 一條路（§B）。
- **既有藍牙 RFCOMM App 通道：只能驅動 App，不能重啟 helper**。helper 是 `shell`(uid 2000) 常駐行程、由 adb 啟動；眼鏡 App 是 `untrusted_app`，無法 spawn shell-context 行程（App 直連 exploit 已實測 kernel panic）。所以 RFCOMM 通道「跑指令」可以（App 寫 `last.cmd`），但「把 helper 拉起來」不行。

**結論：USB-OTG + termux-adb 是唯一免 PC 又能重啟 helper 的通道；它同時是後續 `pm disable`、`dd` 等操作的載體。**

### A.4 結論（A 段）

- **能取代 PC**，例行任務簡化為：開 MIUI OTG → C-to-C 資料線接眼鏡 → Termux 內一條 `adb shell "setsid sh /data/local/tmp/ghostlock_bridge_helper.sh ..."`（helper 腳本、`ghostlock_rv101`、`root_daemon` 都在 `/data/local/tmp`，**跨 reboot 持久**，只有「行程」會死）。之後眼鏡 App 開機自啟（已實作 `BootReceiver`）→ 觸發 `ACQUIRE_ROOT` → root_daemon 復活。
- 若想更省事，可在手機 Termux 寫一個 3 行 wrapper script 自動完成「開 OTG → adb → 重啟 helper → 觸發 root」，把「每次 reboot 一次 adb」壓成一鍵。
- 限制：仍**非持久化**（每 reboot 仍要手機在場、手動或半自動一次）；想要「開機全自動、無任何 host」，必須走 B（Magisk）。

---

## B. Magisk 刷入（真正的持久化）

### B.1 dd 刷入步驟與風險（AVB / dm-verity）

現有資產：`magisk_patched-30600_9C56p.img`（Magisk 30.6 patch 好的 boot image；`ROOT_PROCEDURE.md` 記「Magisk App 30.6」）。boot 分區：**active slot = A = `/dev/block/mmcblk0p17`**（`boot_b` = `mmcblk0p18`，A/B 架構）。

**推薦用 root dd 而非 fastboot**（因為已有 root_daemon，`u:r:kernel:s0` + Permissive 可直接寫 block device）：

```sh
# 0) 先備份（詳 B.4）
dd if=/dev/block/mmcblk0p17 of=/data/local/tmp/boot_a_stock_1.22.009.img bs=4096
# 1) 刷入 patched boot（root，經 socket）
dd if=/data/local/tmp/magisk_patched-30600_9C56p.img of=/dev/block/mmcblk0p17 bs=4096
sync
# 2) 驗證大小一致（boot_a 約 96 MiB，見 extract_boot.sh count=24576*4096）
ls -l /data/local/tmp/magisk_patched-30600_9C56p.img /data/local/tmp/boot_a_stock_1.22.009.img
# 3) 重啟
reboot
```

等價的 fastboot 路徑（若不想用 dd）：`adb reboot bootloader` → `fastboot flash boot_a magisk_patched-30600_9C56p.img` → `fastboot reboot`（`Rokid_Root_Guide_boot_img.md` 的標準流程）。

**AVB / dm-verity 說明（關鍵判斷）**：

- **AVB（boot 簽章）**：bootloader **已解鎖**（`ro.boot.flash.locked=0`、`verifiedbootstate=orange`），表示 verified boot 已失效/放行 → **Magisk-patched boot.img 可直接開機，通常不需動 vbmeta**。`Rokid_Root_Guide_boot_img.md` 的 `fastboot flash vbmeta --disable-verity --disable-verification` 是「保險」；本裝置因已解鎖，可跳過，但若刷完卡開機可補這步。
- **dm-verity（system/vendor 唯讀）**：與 boot 無關，且 Magisk 是 **systemless**（ramdisk overlay + `/data/adb`），**不修改 `/system`、`/vendor`** → 不需關 dm-verity，SELinux 也維持 Enforcing。這與「install-recovery.sh 掛鉤被 dm-verity 擋」是兩回事：Magisk 不走 vendor 分區。
- 風險點集中在「**寫錯分區 / 半寫入 / 映像損壞**」：寫到 p18（非 active）沒用但不磚；寫壞 p17 且 bootloader 仍可進 fastboot 時可還原；只有「連 fastboot 都進不去」才真正磚。

### B.2 成功後能獲得什麼

- **`su`（magiskd）**：常駐 root，`adb shell su`、任何 app 申請 root。
- **`/data/adb/service.d/` 開機腳本**（root，late-boot 執行；`post-fs-data.d` 更早）：放一個「啟動 `root_daemon`（0777 socket）」的腳本，開機即常駐 → 眼鏡 App（`BootReceiver` 已自啟）直接連 socket，**完全免 adb、免 PC、免手機、免 exploit**。
- **更大改動空間**：有了 Magisk su，`ghostlock_rv101` 這條 flaky（~50% 重啟）的 LPE 鏈**整個可以退休**——root_daemon 直接由 `su` 啟動，root 確定性 100%、無 OOM/kernel-panic。既有的眼鏡 App 只認 `root_daemon.sock`，所以「service.d 啟動 root_daemon」是最小侵入、零 App 重寫的接法。
- **Shizuku root 啟動**：Shizuku 偵測到 `su` 時可用「以 root 啟動」（而非 adb）常駐，research/shizuku 那條線可免每次 adb 餵 `sh /sdcard/.../rish`。

### B.3 風險等級與 rollback 計畫

- **磚機風險：中**。只動 boot 分區，不動 bootloader/abl/xbl；bootloader 已解鎖 → fastboot 幾乎一定可用。真正磚的機率低，但「萬一」需 EDL。
- **EDL/Sahara 現狀（誠實評估）**：工作區 `scripts/sahara_reset.py` 只會對 COM10 送 Sahara **RESET**（把 9008 模式喚醒重開），**沒有 firehose programmer / rawprogram**，所以它**救不回已刷壞的分區**，只能「退出 EDL」。也就是說：**EDL 完整線刷目前不可用**（沒有原廠 firehose + 完整韌體）。→ 唯一可靠 rollback 是 **fastboot + 原廠 boot.img 備份**。
- **Rollback 步驟**：
  1. `fastboot flash boot_a boot_a_stock_1.22.009.img`（還原原廠 boot）。
  2. 若 A 槽進不去，切 B 槽 `fastboot set_active b`（若 B 仍原廠）或 `fastboot flash boot_b ...`。
  3. 最後手段才考慮 EDL（但目前缺 firehose，需先弄到 Rokid 完整線刷包，見 C.4）。
- 因為 bootloader 解鎖且原廠 boot.img 可 dd 備份，**可回滾性有保障**；主要紀律是「刷前必備份 + 驗證大小/MD5」。

### B.4 dd 備份現有 boot 的還原路徑

```sh
# 備份（root）
dd if=/dev/block/mmcblk0p17 of=/data/local/tmp/boot_a_stock_1.22.009.img bs=4096
# 拉到 PC/手機 各存一份（第二份在裝置外，防裝置磚後連 /data 都讀不到）
adb pull /data/local/tmp/boot_a_stock_1.22.009.img .
md5sum boot_a_stock_1.22.009.img
```

還原（任一 host：PC fastboot，或 termux-adb/fastboot）：

```sh
fastboot flash boot_a boot_a_stock_1.22.009.img
fastboot reboot
```

> 第二還原來源：已下載的 1.22.009 完整 OTA（`payload.bin`）可用 `payload-dumper-go` 抽出原廠 `boot.img` 作為交叉備份；但注意 1.25.012 的包抽出的 boot 是**已修補 GhostLock 版**，只能拿來「救磚」，不能拿來「回到漏洞版」。

---

## C. OTA 阻擋（避免升到 1.25.012 被修補）

### C.1 已知機制（工作區實測/逆向）

- OTA app = `com.rokid.glass.ota`（`all_pkgs.txt` 有列）。檢查 API = `POST https://ota.rokid.com/v1/extended/ota/check`，簽名 `MD5(key=&device_type_id=&device_id=&service=ota&version=1.0&time=&secret=)`，參數來自 `ro.boot.key/secret/devicetypeid/serialno`（已破解，見 `OTA_Authentication_Analysis_Report.md`）。
- 裝置端觸發 = broadcast `com.rokid.glass.ota.NORMAL_CMD_ACTION --es normal_cmd NORMAL_GET_UPDATE_INFO`；下載/版本資訊存 `ota_sp.xml`（`download_url`/`download_version`/`check_sum`）。
- 更新走 **A/B payload.bin + `/system/bin/update_engine`**（`SESSION_16_UPDATE_ENGINE_PAYLOAD_GATES.md` 已反組譯其 ApplyPayload gates；`hash_checks_mandatory` 由 `ro.debuggable`/`ro.build.type` 決定）。
- 最新版本實測 = `1.25.012-20260901-150201`，`isForceUpdate=false`（**非強制**），但其 kernel 已 backport GhostLock 修補（`remove_waiter` 用 `waiter->task`，見 device-debug-notes）→ **升級即失去 ghostlock_rv101**。

### C.2 推送路徑（手機代理？）

- 眼鏡本身**無網路**（`UnknownHostException`），所以「眼鏡自己檢查 + 下載」這條路在無網路時**先天就斷**。
- 真正風險 = **手機 Rokid App 代理**：官方 `com.rokid.rc.phone`（或 `phone_aiapp`）若在手機端檢查更新，再經藍牙/USB 把 ~1GB 包丟給眼鏡端 `com.rokid.glass.ota`/`update_engine`，就會繞過「眼鏡無網路」這道天然屏障。工作區 `app-phone`（自建的 com.rokid.rc.phone 源碼）**只有相簿/遙控/root 控制，沒有 OTA/sideload 功能**——但這是**自建 App**，不代表官方 Rokid App 沒有。官方文件（Rokid glass-docs FAQ）提到「手動刷機」字樣，顯示存在離線/線刷通道；**未能確認官方手機 App 是否有本地 sideload 更新**（推論：很可能有或未來會有，風險需以「有」來設防）。
- 觸發來源還可能含 `com.rokid.os.sprite.assistserver`（assist 服務，負責與手機/雲 relay，`all_pkgs.txt` 有列）——它是最可能的 OTA 推送中繼。

### C.3 阻擋手段（分級，依序）

> **實測更新（2026-09-09）**：本 Rokid build 的 `pm disable-user --user 0` 實測回
> `Failure [not support for 0]`（OEM 移除該能力）；**`pm suspend com.rokid.glass.ota`
> 實測成功**，且跨 3 次 kernel-panic 重啟後仍維持（`pm list packages -s` 列出、
> dumpsys `suspended=true`）。落地件 `deploy/lock_ota.sh` 已改為 suspend 優先 +
> disable-user 兜底。還原指令：`pm unsuspend com.rokid.glass.ota`。

**不做 root 也能做（但需一次 adb；配合 A 段手機 adb 即可隨時做）：**

1. **`adb shell pm disable-user --user 0 com.rokid.glass.ota`** —— **首選、且 reboot 後維持**（package disabled 狀態存 `/data/system/users/0/package-restrictions.xml`，持久）。重啟後 `com.rokid.glass.ota` 不再接收 broadcast、不再檢查/下載。還原：`pm enable com.rokid.glass.ota` 或 `cmd package install-existing com.rokid.glass.ota`。
2. 次要目標：`pm disable-user --user 0 com.rokid.os.sprite.assistserver`（中繼）。**風險**：可能影響其他手機連動功能，建議先只在有需要時再關；可先 `pm list packages -f | grep rokid` 精確盤點再動。
3. **不下載器**：`com.android.providers.downloads` 是系統通用下載器，**不要 disable**（會打壞相簿/其他 app）；OTA 的實際下載由 `com.rokid.glass.ota` 自己發起，關掉它即可，不必動 DownloadProvider。

**有 root 更徹底（需現有 root_daemon 或 Magisk su）：**

4. 上面 1+2 的 root 版（`pm disable-user` 本質相同）+ 補充：
   - `pm suspend com.rokid.glass.ota`（凍結）
   - 用 root 把 `com.rokid.glass.ota` 的 OTA 檢查 receiver 停用（`pm disable-user` 已涵蓋）。
5. **物理移除不可行**：`/system`、`/vendor` 受 dm-verity 保護唯讀（已實測），無法刪 APK。
6. **網路層：低優先**。眼鏡無網路，本就低風險；即便要加，可用 iptables 擋 `ota.rokid.com`/`ota-g.rokidcdn.com`（root 下），屬錦上添花。
7. **Magisk 層（B 方案之後）**：`service.d` 開機腳本直接跑 `pm disable-user`（每次開機防復活），或掛 DenyList 遮 `com.rokid.glass.ota` 的 root 偵測（DenyList 主要擋 app 看 root，對 OTA 本身助益有限，`pm disable` 才是正解）。

**1.22.009 被動升級機制檢查（`ro.build` props）**：依現有逆向，更新由 OTA app 主動 `NORMAL_GET_UPDATE_INFO` 觸發，`isForceUpdate=false`；**未見 silent/強制自升級機制**。但仍需防「官方手機 App 自動檢查 + 推送」。**實務建議：研究期間眼鏡與官方 Rokid App 藍牙斷連 / 不登入帳號，並執行上面 `pm disable-user com.rokid.glass.ota`，雙保險。**

### C.4 升級後不可逆提醒

- 1.25.012 的 `post-security-patch-level` 仍顯示 `2024-07-05`（與 1.22.009 相同），**SPL 會騙人**；真正差異在 kernel 已 backport GhostLock 修補。升級後 `ghostlock_rv101` 失效。
- A/B 架構有 anti-rollback（AVB rollback index）+ Rokid 官方**未見公開降級/線刷包**（工作區僅有「救醒 EDL 的 reset」，無 firehose 線刷）。→ **升級 1.25.012 應視為不可逆**。這是「必須阻擋 OTA」的最強理由。

---

## 比較矩陣

| 方案 | 免 adb 程度 | 跨 reboot | 磚機風險 | 實作難度 | 推薦順位 |
|---|---|---|---|---|---|
| **B. Magisk dd 刷入**（service.d 自啟 root_daemon） | ✅ 完全免（開機全自動） | ✅ 完全持久 | 中（只動 boot，可 fastboot 回滾；EDL 缺 firehose 不保險） | 中高（需備份+謹慎 dd/驗證） | **1（若接受刷機風險）** |
| **A. 手機 adb host**（termux-adb + adb_keys 注入） | ⚠️ 免 PC，但每 reboot 要手機一鍵 | ❌ 每 boot 需重啟 helper | 低（不碰分區） | 中（OTG 線材/MIUI OTG 坑 + 一次性 key 注入） | **2（低風險首選）** |
| 現況（PC adb helper） | ❌ 需 PC | ❌ 每 boot 需 PC | 低 | 低（現行已實證） | 3（維持兜底） |
| ~~install-recovery.sh 掛鉤~~ | — | — | — | — | ❌ dm-verity 擋死（已實測） |
| ~~symlink trick App 直連~~ | — | — | — | — | ❌ 需 shell 權限，App 直跑 kernel panic（已實測） |

**建議路徑**：先做 **A**（低風險、立刻移除 PC 依賴，且拿到隨時可用的手機 adb 載體）→ 若決心要「開機全自動、零 host」，在**確認已 dd 備份原廠 boot_a + 存到裝置外 + 驗證 MD5** 後再走 **B**。B 完成後順手把 C 的 `pm disable-user com.rokid.glass.ota` 寫進 `service.d`，把 OTA 也一併鎖死。

---

## 參考來源

- 工作區：`docs/device-debug-notes.md`、`docs/root-from-zero-guide.md`、`deploy/root_bridge.sh`、`deploy/check_ota.sh`、`deploy/ota_api.py`、`deploy/setup_direct.sh`、`reports/OTA_Authentication_Analysis_Report.md`、`reports/ROOT_PROCEDURE.md`、`reports/Rokid_Root_Guide_boot_img.md`、`reports/SESSION_16_UPDATE_ENGINE_PAYLOAD_GATES.md`、`scripts/sahara_reset.py`、`scripts/extract_boot.sh`、`all_pkgs.txt`
- [nohajc/termux-adb — Run adb in Termux without root](https://github.com/nohajc/termux-adb)
- [XDA: Termux-ADB — debug one Android device from another by USB-OTG](https://xdaforums.com/t/app-non-root-termux-adb-debug-one-android-device-from-another-connected-by-usb-otg.4515059/)
- [AOSP adb keystore（adb_keys 格式）](https://github.com/GrapheneOS/platform_packages_modules_adb/blob/17/docs/dev/keystore.md)
- [Magisk 開機腳本（post-fs-data.d / service.d）指引](https://xdaforums.com/t/guide-some-hints-for-writing-init-scripts-for-magisk.4689292/)
- [Rokid 官方 glass-docs / OTA FAQ](https://rokid.github.io/glass-docs/0-faq/)
