# Rokid AR 眼鏡 Root 指南：刷入自定義 boot.img

> English: [Rokid_Root_Guide_boot_img_EN.md](Rokid_Root_Guide_boot_img_EN.md)

> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。

> ⚠ **歷史文件／非現行路徑（2026-09-13 註）**：本指南描述的是**刷入自訂 boot.img**（Magisk）路線，
> 該路線因取不到 boot.img 而阻塞。**現行 root 方式為 GhostLock LPE**（`deploy/root_bridge.sh`／`start_shizuku.sh`），
> 成功後 SELinux 為 **Permissive**（不是本文所述的 Enforcing）。


## 📋 前置條件檢查

### 設備狀態確認

```bash
# 1. 確認 Bootloader 已解鎖
adb shell getprop ro.boot.flash.locked
# 預期輸出: 0 (已解鎖)

# 2. 確認設備狀態
adb shell getprop ro.boot.verifiedbootstate
# 預期輸出: orange (解鎖狀態)

# 3. 確認當前使用的分區槽位
adb shell getprop ro.boot.slot_suffix
# 預期輸出: _a 或 _b

# 4. 確認 Android 版本
adb shell getprop ro.build.version.release
# 預期輸出: 12
```

### 設備資訊

| 項目         | 值                        |
| ------------ | ------------------------- |
| 型號         | Rokid RG-glasses (RV101)  |
| Android 版本 | 12 (API 32)               |
| 分區架構     | A/B 動態分區              |
| Bootloader   | **已解鎖** (orange state) |
| 內核版本     | 5.10.209-perf             |
| 安全補丁     | 2024-07-05                |

---

## ⚠️ 重要警告

1. **刷入自定義 boot.img 有可能導致設備無法開機**
2. **請確保已備份所有重要資料**
3. **需要可用的 Fastboot 環境**
4. **建議先備份原廠 boot.img 以便恢復**

---

## 📦 工具準備

### 必要工具

1. **ADB & Fastboot**
   - 下載: [Android SDK Platform-Tools](https://developer.android.com/studio/releases/platform-tools)
   - 確保版本 >= 34.0.0

2. **Magisk Manager** (用於修補 boot.img)
   - 下載: [Magisk GitHub Releases](https://github.com/topjohnwu/Magisk/releases)
   - 推薦版本: 26.x 或更新

3. **存儲空間**
   - 電腦上約 500MB 可用空間
   - 設備內部存儲約 200MB

4. **Qualcomm USB 驅動** (Windows 用戶)
   - 下載: Qualcomm HS-USB Driver
   - 用於 Fastboot/EDL 模式連接

---

## 🔌 進入 Fastboot 模式 (Rokid 特定)

### 方法 1：ADB 命令

```bash
adb reboot bootloader
# 等待約 5-10 秒
fastboot devices
```

### 方法 2：硬體按鍵組合

由於 Rokid 眼鏡可能沒有傳統按鍵，請嘗試：

```bash
# 方法 A: 完全關機後
# 1. 長按電源鍵 10 秒關機
# 2. 連接 USB
# 3. 同時按住「電源鍵」+「調低音量」（如有）
# 4. 保持 5 秒直到 Fastboot 畫面

# 方法 B: 使用 ADB
adb reboot bootloader

# 方法 C: 通過 Recovery
adb reboot recovery
# 在 Recovery 選擇 "Reboot to bootloader"
```

### Fastboot 連接故障排除

```bash
# 如果 fastboot devices 無輸出：

# 1. 檢查 Windows 設備管理器
#    - 查找 "Android Bootloader Interface"
#    - 如顯示未知設備，安裝 Qualcomm 驅動

# 2. 嘗試不同 USB 端口（直接連接主板 USB）

# 3. 使用管理員權限運行終端

# 4. 檢查 Fastboot 版本
fastboot --version
```

---

## 🔧 步驟 1：提取原廠 boot.img

### 方法 A：從設備直接提取 (推薦)

```bash
# 1. 確認當前活動槽位
adb shell getprop ro.boot.slot_suffix
# 假設輸出 _a

# 2. 找到 boot 分區路徑
adb shell ls -la /dev/block/by-name/ | grep boot
# 預期輸出類似:
# boot_a -> /dev/block/sda10
# boot_b -> /dev/block/sda11

# 3. 提取 boot_a 分區 (根據當前槽位調整)
adb shell "dd if=/dev/block/by-name/boot_a of=/sdcard/boot_a.img bs=4096"

# 4. 拉取到電腦
adb pull /sdcard/boot_a.img .

# 5. 驗證文件
# Windows:
certutil -hashfile boot_a.img MD5
# Linux/Mac:
md5sum boot_a.img

# 保存這個 MD5 值用於備份驗證！
```

### 方法 B：從 OTA 包提取

```bash
# 如果有 OTA 更新包 (update.zip)
# 使用 payload-dumper-go 提取

# 1. 下載 payload-dumper-go
# https://github.com/ssut/payload-dumper-go/releases

# 2. 解壓 OTA 包中的 payload.bin
unzip update.zip payload.bin

# 3. 提取 boot.img
payload-dumper-go -o . payload.bin
# 會生成 boot.img
```

---

## 🔧 步驟 2：提取 vendor_boot.img (Android 12 必需)

```bash
# Android 12 使用 GKI (Generic Kernel Image)
# 需要同時處理 vendor_boot 分區

# 1. 提取 vendor_boot
adb shell "dd if=/dev/block/by-name/vendor_boot_a of=/sdcard/vendor_boot_a.img bs=4096"
adb pull /sdcard/vendor_boot_a.img .

# 2. 同樣備份 MD5
certutil -hashfile vendor_boot_a.img MD5
```

---

## 🔧 步驟 3：使用 Magisk 修補 boot.img

### 方法 A：在設備上修補 (推薦)

```bash
# 1. 安裝 Magisk Manager APK
adb install Magisk-v26.4.apk

# 2. 將 boot.img 推送到設備
adb push boot_a.img /sdcard/Download/

# 3. 在設備上：
#    - 打開 Magisk Manager
#    - 點擊 "安裝" -> "選擇並修補一個文件"
#    - 選擇 /sdcard/Download/boot_a.img
#    - 等待修補完成
#    - 修補後的文件保存為: /sdcard/Download/magisk_patched_xxxxx.img

# 4. 拉取修補後的文件
adb pull /sdcard/Download/magisk_patched_*.img magisk_patched_boot.img
```

### 方法 B：在電腦上修補 (使用 magiskboot)

```bash
# 需要 Linux 環境或 WSL

# 1. 下載 Magisk 並解壓
unzip Magisk-v26.4.apk -d magisk_tmp

# 2. 找到 magiskboot (ARM64 版本)
# 路徑: magisk_tmp/lib/arm64-v8a/libmagiskboot.so
# 重命名為 magiskboot

# 3. 解包 boot.img
./magiskboot unpack boot_a.img

# 4. 會生成以下文件：
#    - kernel (內核)
#    - ramdisk.cpio (initramfs)
#    - dtb (設備樹，可能)

# 5. 修補 ramdisk
./magiskboot cpio ramdisk.cpio "mkdir 0750 overlay.d" \
    "add 0644 overlay.d/sbin/magiskinit magiskinit"

# 6. 重打包
./magiskboot repack boot_a.img magisk_patched_boot.img
```

---

## 🔧 步驟 4：處理 vbmeta (關鍵步驟！)

**⚠️ 這一步非常重要！** Android Verified Boot 會檢查 boot.img 的簽名。
如果不禁用 vbmeta 驗證，刷入修補後的 boot.img 會導致無法開機。

### 選項 A：刷入禁用驗證的 vbmeta (推薦)

```bash
# 進入 Fastboot 模式
adb reboot bootloader

# 等待設備進入 fastboot
fastboot devices
# 應顯示設備序列號

# 刷入 vbmeta 並禁用驗證
# --disable-verity: 禁用 dm-verity
# --disable-verification: 禁用簽名驗證
fastboot flash vbmeta vbmeta.img --disable-verity --disable-verification

# 如果沒有原廠 vbmeta.img，可以創建空白的：
# dd if=/dev/zero of=vbmeta_disabled.img bs=256 count=64
# fastboot flash vbmeta vbmeta_disabled.img --disable-verity --disable-verification
```

### 選項 B：使用 Fastboot 參數 (某些設備支持)

```bash
# 某些設備支持直接禁用
fastboot flashing unlock_critical  # 如果需要
fastboot oem disable-verity
fastboot oem disable-dm-verity
```

---

## 🔧 步驟 5：刷入修補後的 boot.img

```bash
# 確保仍在 Fastboot 模式
fastboot devices

# 刷入修補後的 boot.img 到當前槽位
# 如果當前是 _a 槽位：
fastboot flash boot_a magisk_patched_boot.img

# 或者使用通用命令 (自動選擇當前槽位)：
fastboot flash boot magisk_patched_boot.img

# 確認刷入成功，輸出應類似：
# Sending 'boot_a' (xxxxx KB)   OKAY [  1.xxx s]
# Writing 'boot_a'              OKAY [  0.xxx s]
```

### Android 12 特殊處理：vendor_boot

```bash
# 如果 Magisk 輸出需要修補 vendor_boot，同時刷入：
fastboot flash vendor_boot_a vendor_boot_patched.img

# 注意：大多數情況下只需刷入 boot.img
# 只有當 Magisk 明確提示需要修補 vendor_boot 時才需要
```

---

## 🔧 步驟 6：重啟並驗證

```bash
# 重啟到系統
fastboot reboot

# 等待系統啟動 (可能需要較長時間)
# 首次啟動可能顯示警告訊息，這是正常的

# 系統啟動後，驗證 Root 狀態
adb shell

# 在 shell 中執行：
su
# 如果成功，提示符會變成 #

# 驗證 UID
id
# 應顯示 uid=0(root) gid=0(root)

# 驗證 Magisk 版本
magisk -v

# 檢查 SELinux 狀態
getenforce
# 應顯示 Enforcing (Magisk 不會破壞 SELinux)
```

---

## 🆘 災難恢復

### 情況 A：無法開機但能進 Fastboot

```bash
# 進入 fastboot
# 長按電源鍵 + 音量下 (具體組合鍵依設備而定)

# 刷回原廠 boot.img
fastboot flash boot_a boot_a.img

# 刷回原廠 vbmeta
fastboot flash vbmeta vbmeta.img

# 重啟
fastboot reboot
```

### 情況 B：完全無法開機

```bash
# 1. 嘗試強制進入 Fastboot
#    - 完全斷電 (如果可能)
#    - 按住組合鍵開機

# 2. 如果有恢復分區
fastboot boot recovery.img

# 3. 最後手段：重刷完整韌體
#    需要原廠完整刷機包
```

### 情況 C：需要 EDL 模式恢復

```bash
# 某些 Qualcomm 設備支持 EDL (Emergency Download) 模式
# 這是最低層級的恢復方式

# 進入 EDL 模式：
adb reboot edl
# 或按住特定按鍵組合

# 使用 QFIL 或 edl 工具恢復
# 需要設備的原廠 firehose 程式和分區映像
```

---

## 📋 完整命令序列 (快速參考)

```bash
# ========== 備份階段 ==========
adb shell getprop ro.boot.slot_suffix          # 確認槽位
adb shell "dd if=/dev/block/by-name/boot_a of=/sdcard/boot_a.img bs=4096"
adb shell "dd if=/dev/block/by-name/vendor_boot_a of=/sdcard/vendor_boot_a.img bs=4096"
adb shell "dd if=/dev/block/by-name/vbmeta_a of=/sdcard/vbmeta_a.img bs=4096"
adb pull /sdcard/boot_a.img .
adb pull /sdcard/vendor_boot_a.img .
adb pull /sdcard/vbmeta_a.img .

# ========== 修補階段 ==========
adb install Magisk-v26.4.apk
adb push boot_a.img /sdcard/Download/
# 在設備上使用 Magisk 修補...
adb pull /sdcard/Download/magisk_patched*.img magisk_patched_boot.img

# ========== 刷入階段 ==========
adb reboot bootloader
fastboot devices
fastboot flash vbmeta vbmeta_a.img --disable-verity --disable-verification
fastboot flash boot_a magisk_patched_boot.img
fastboot reboot

# ========== 驗證階段 ==========
adb shell su -c id
adb shell su -c "getenforce"
adb shell su -c "magisk -v"
```

---

## 🔍 故障排除

### 問題：Fastboot 無法識別設備

```bash
# 檢查驅動
# Windows: 安裝 Google USB Driver 或 Qualcomm Driver
# 設備管理器中查看是否有未知設備

# 嘗試不同 USB 線/端口
# 確保 USB 調試已啟用
```

### 問題：刷入後卡在開機畫面

```bash
# 可能是 vbmeta 問題
# 重新進入 fastboot 並確保禁用驗證
fastboot flash vbmeta vbmeta_a.img --disable-verity --disable-verification
fastboot reboot
```

### 問題：SELinux 變成 Permissive

```bash
# 這通常表示 Magisk 未正確安裝
# 嘗試重新刷入或使用更新版本的 Magisk
```

### 問題：某些應用檢測到 Root

```bash
# 使用 Magisk 的 DenyList (原 MagiskHide)
# 在 Magisk Manager 中：
# 設置 -> 配置 DenyList -> 添加需要隱藏的應用
```

---

## 📝 附加說明

### 關於 A/B 分區系統

Rokid 使用 A/B 分區方案：

- `boot_a` / `boot_b`
- `system_a` / `system_b`
- `vendor_boot_a` / `vendor_boot_b`

**優點**：

- OTA 更新更安全
- 可以回退到上一個槽位

**注意**：

- 刷入 boot 時只會影響當前槽位
- 如果系統 OTA 更新，可能會覆蓋修改

### 關於 GKI (Generic Kernel Image)

Android 12 使用 GKI 架構：

- `boot.img` 包含通用內核
- `vendor_boot.img` 包含設備特定的內核模組

大多數情況下，Magisk 只需修補 `boot.img`。
只有在特定情況下才需要修補 `vendor_boot.img`。

---

## ✅ 成功標誌

Root 成功後，您應該能夠：

1. ✅ 執行 `adb shell su` 獲得 root shell
2. ✅ Magisk Manager 顯示已安裝
3. ✅ SELinux 仍為 Enforcing
4. ✅ 系統正常運行，無明顯異常
5. ✅ 能安裝需要 root 權限的應用

---

## 🔗 相關資源

- [Magisk 官方文檔](https://topjohnwu.github.io/Magisk/)
- [Android Verified Boot 說明](https://source.android.com/security/verifiedboot)
- [Fastboot 命令參考](https://source.android.com/setup/build/running)

---

**最後更新**: 2026-01-31
**適用設備**: Rokid RG-glasses (RV101) / Android 12
