# Facebook 發文 - Rokid Glasses 討論區

> English: [Facebook_Rokid_Root_Post_EN.md](Facebook_Rokid_Root_Post_EN.md)

> ⚠ **歷史文件（非現況）**：本文為當時的紀錄；專案其後已有重大變化（root 已取得、引擎改執行期探測、手機 UI 重做、i18n 193 key）。
> 現行狀態請看 `README.md`、`MANIFEST.md`、`docs/deploy.md`。


> 📍 發布位置：https://www.facebook.com/groups/1170966655169478
> 📅 日期：2026-01-31

---

## 複製以下內容貼到 Facebook：

---

🔧【技術分享】Rokid AR 眼鏡 Root 研究紀錄 - 尋求協助 🆘

大家好！我是剛入坑 Rokid 眼鏡的新手 👓

最近花了幾天時間研究如何對 Rokid 眼鏡進行 Root，想分享一下目前的進度和遇到的問題，也希望有經驗的前輩們能給點建議！

---

📱 我的設備資訊：
• 型號：Rokid RG-glasses (RV101)
• Android：12
• 韌體：1.12.009-20260109
• 內核：5.10.209

---

🎉 好消息！
經過測試確認 Bootloader 是【已解鎖】狀態！
這表示理論上可以直接用 Magisk 修補 boot.img 後刷入取得 Root。

進入 Fastboot 的方式：
1️⃣ 執行 adb reboot recovery
2️⃣ 在眼鏡螢幕選擇 "Enter fastboot"
3️⃣ 用 fastboot devices 確認連接

---

😢 遇到的問題：
核心問題是【無法提取 boot.img】！

已經嘗試過的方法：
❌ ADB shell dd → Permission denied
❌ fastboot fetch → 設備不支援
❌ fastboot oem dump → unknown command
❌ Recovery sideload → 驅動問題會卡住
❌ Magisk Direct Install → 需要先有 root（雞生蛋問題）

基本上所有「正規」提取 boot.img 的方式都試過了，全部失敗...

---

🎯 目前想到的可行方案：

1️⃣ 找到 Rokid 官方固件
→ 用 payload-dumper 解出 boot.img

2️⃣ 找到相容的 TWRP Recovery
→ 用 fastboot boot 臨時啟動，再 dd 提取

3️⃣ 硬體 EDL 模式（需要拆機）
→ 短接測試點進入 Qualcomm 下載模式

---

🙏 尋求協助：

如果你有以下任何資源，拜託私訊我或留言分享：

1. Rokid 官方固件（任何版本都可以！）
2. 適用於 SM6375/khaje 的 TWRP
3. 你成功 Root 的經驗

或者如果你知道其他方法也歡迎告訴我！

---

⚠️ 小提醒：
如果有人想嘗試 Recovery sideload，要小心！
Sideload 模式的 USB ID 跟正常 ADB 不同，Windows 可能會驅動錯誤導致卡住。
解決方法是去設備管理員解除安裝驅動後重新插拔 USB。

---

我會持續更新進度，也歡迎大家一起討論！
有任何問題都可以留言交流 💬

#Rokid #AR眼鏡 #Root #技術交流

---

## 附圖建議（可選）：

1. Fastboot 連接成功的截圖
2. fastboot getvar all 的輸出
3. Recovery 選單的照片（如果有拍）

---

## 簡短版（如果覺得太長）：

---

🔧 Rokid 眼鏡 Root 研究 - 求助！

各位好～

我在研究 Rokid 眼鏡的 Root，發現 Bootloader 已經解鎖 🎉
但卡在【無法提取 boot.img】這一步 😢

試過 ADB dd、fastboot fetch、sideload 全部失敗...

想請問有沒有人有：
1️⃣ Rokid 官方固件
2️⃣ 適用的 TWRP
3️⃣ Root 成功的經驗分享

有任何資訊都拜託留言或私訊！感謝 🙏

#Rokid #Root

---
