# Rokid AR Glasses Root 流程

> English: [ROOT_PROCEDURE_EN.md](ROOT_PROCEDURE_EN.md)

> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。

> ## ⚠ 2026-09-13 現況指引（本檔為 2026-02 的歷史紀錄）
> 
> - **root 早已取得**（2026-08-22 起，`uid=0(root) context=u:r:kernel:s0`、SELinux Permissive），
>   目前一鍵流程為 `deploy/root_bridge.sh`（bridge helper）或 `deploy/start_shizuku.sh`（Shizuku）＋ `deploy/get_root_now.sh`。
> - 本檔所指的 boot.img 阻塞，**只影響「跨重開機的持久 root」**（需 Magisk 修補 boot），不影響執行期提權。
> - 提權每 boot 需以 adb 武裝一次；取得後建議跑 `deploy/install_root_keepalive.sh` 讓 daemon 撐完整個 session。
> - 最新架構與維運見 `docs/deploy.md`（§9 與 §提權維運 P0–P3）、`reports/su_escalation_roi_assessment.md`。


> **🔴 當前狀態 (2026-02-02): 尚未成功取得 root 權限**
> **阻塞原因: 無法取得 boot.img 文件**

## 當前狀態

| 項目               | 狀態                                                     |
| ------------------ | -------------------------------------------------------- |
| **Root 權限**      | ❌ **未取得**                                            |
| **設備**           | RG-glasses (Serial: <DEVICE_SERIAL>)                    |
| **Android**        | 12                                                       |
| **Kernel**         | 5.10.209-perf                                            |
| **Bootloader**     | ✅ **已解鎖** (flash.locked=0, verifiedbootstate=orange) |
| **Magisk App**     | ✅ 30.6 已安裝，❌ boot image **未修補**                 |
| **SELinux**        | Enforcing                                                |
| **當前 Boot Slot** | A (boot_a → /dev/block/mmcblk0p17)                       |

---

## 🔴 阻塞點: 無法取得 boot.img

| 嘗試方法                       | 結果 | 說明                          |
| ------------------------------ | ---- | ----------------------------- |
| `dd /dev/block/mmcblk0p17`     | ❌   | Permission denied (需要 root) |
| `fastboot fetch boot_a`        | ❌   | 設備不支持此命令              |
| `fastboot oem dump-partitions` | ❌   | unknown command               |
| Recovery Sideload              | ❌   | 驅動問題導致卡住              |
| Custom Recovery (TWRP)         | ❌   | 找不到相容版本                |
| 官方 OTA 韌體                  | ❌   | 未找到下載來源                |

---

## Root 步驟 (需要先取得 boot.img)

### 步驟 1: 提取 Boot Image ❌ 阻塞中

```bash
# 方法 A: 直接讀取 (需要 root - 目前無法使用)
adb shell "dd if=/dev/block/mmcblk0p17 of=/data/local/tmp/boot.img" 2>&1

# 方法 B: 從 OTA 包提取 (需要找到韌體)
payload-dumper-go -o extracted/ firmware.zip

# 方法 C: 透過 TWRP 提取 (需要找到相容 TWRP)
fastboot boot twrp.img
adb shell dd if=/dev/block/mmcblk0p17 of=/sdcard/boot.img
adb pull /sdcard/boot.img
```

### 步驟 2: 使用 Magisk App 修補

1. 將 boot.img 傳到設備
2. 打開 Magisk App
3. 選擇 "安裝" → "選擇並修補一個文件"
4. 選擇 boot.img
5. 等待修補完成，生成 magisk_patched-xxx.img

### 步驟 3: 刷入修補後的 Boot Image

```bash
# 進入 fastboot 模式 (正確方式)
adb reboot recovery
# 在眼鏡螢幕上選擇 "Enter fastboot"

# 確認連接
fastboot devices

# 刷入修補後的 boot image
fastboot flash boot_a magisk_patched-xxx.img

# 重啟
fastboot reboot
```

### 步驟 4: 驗證 Root

```bash
adb shell su -c id
# 應該顯示 uid=0(root)
```

---

## 可行的替代方案

### 方案 A: 尋找 Rokid 官方韌體 (最佳)

1. 聯繫 Rokid 客服獲取韌體
2. 在論壇/社群尋找分享的韌體
3. 使用 `payload-dumper-go` 解包 payload.bin

### 方案 B: 尋找相容 TWRP

1. 尋找 SM6375/khaje 平台的 TWRP
2. 使用 `fastboot boot twrp.img` 臨時啟動
3. 在 TWRP 中 dd 提取 boot 分區

### 方案 C: EDL 模式 (需要拆機)

1. 短接設備上的測試點進入 EDL 模式
2. 使用 QFIL/Qualcomm 工具讀取分區
3. 需要找到正確的 firehose 文件

---

## 已確認失敗的軟體攻擊路徑

所有核心漏洞利用均已失敗，詳見 [FINAL_STATUS_20260202.md](./FINAL_STATUS_20260202.md)

| 漏洞                       | 狀態               |
| -------------------------- | ------------------ |
| CVE-2024-36971 (Route UAF) | ❌ v1-v13 全部失敗 |
| CVE-2024-0582 (io_uring)   | ❌ 功能不存在      |
| BPF 資訊洩漏               | ❌ 全部失敗        |
| KGSL 利用                  | ❌ v1-v9 全部失敗  |
| Dirty Pipe                 | ❌ 已修補          |
