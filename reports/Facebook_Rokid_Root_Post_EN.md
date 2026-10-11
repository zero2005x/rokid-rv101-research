# Facebook Post - Rokid Glasses Discussion Group

> 中文版：[Facebook_Rokid_Root_Post.md](Facebook_Rokid_Root_Post.md)

> ⚠ **Historical document (not the current state)**: this text records the situation at the time; the project has since changed significantly (root acquired, engine switched to runtime probing, phone UI redone, i18n 193 keys).
> For the current state see `README.md`, `MANIFEST.md`, `docs/deploy.md`.


> 📍 Posted at: https://www.facebook.com/groups/1170966655169478
> 📅 Date: 2026-01-31

---

## Copy the following and paste it into Facebook:

---

🔧 [Tech share] Rokid AR glasses root research log - looking for help 🆘

Hi everyone! I'm a newbie who just got into Rokid glasses 👓

I've spent the last few days researching how to root the Rokid glasses. I'd like to share my current progress and the problems I've run into, and I'm hoping the experienced folks here can give me some advice!

---

📱 My device info:
• Model: Rokid RG-glasses (RV101)
• Android: 12
• Firmware: 1.12.009-20260109
• Kernel: 5.10.209

---

🎉 Good news!
Testing confirmed the bootloader is 【unlocked】!
That means in theory I can just patch boot.img with Magisk and flash it to get root.

How to enter Fastboot:
1️⃣ Run adb reboot recovery
2️⃣ On the glasses screen choose "Enter fastboot"
3️⃣ Confirm the connection with fastboot devices

---

😢 The problem I hit:
The core issue is 【cannot extract boot.img】!

Methods I've already tried:
❌ ADB shell dd → Permission denied
❌ fastboot fetch → not supported by the device
❌ fastboot oem dump → unknown command
❌ Recovery sideload → gets stuck on a driver problem
❌ Magisk Direct Install → needs root first (chicken-and-egg problem)

I've basically tried every "legitimate" way to extract boot.img, and all of them failed...

---

🎯 Workable options I can think of right now:

1️⃣ Find official Rokid firmware
→ unpack boot.img with payload-dumper

2️⃣ Find a compatible TWRP Recovery
→ boot it temporarily with fastboot boot, then dd the image out

3️⃣ Hardware EDL mode (requires opening the device)
→ short the test points to enter Qualcomm download mode

---

🙏 Asking for help:

If you have any of the following resources, please DM me or share them in the comments:

1. Official Rokid firmware (any version is fine!)
2. TWRP for SM6375/khaje
3. Your own successful root experience

Or if you know any other method, I'd love to hear it too!

---

⚠️ A small reminder:
If anyone wants to try Recovery sideload, be careful!
Sideload mode's USB ID differs from normal ADB, so Windows may load the wrong driver and get stuck.
The fix is to uninstall the driver in Device Manager, then unplug and replug the USB.

---

I'll keep updating my progress, and everyone is welcome to join the discussion!
If you have any questions, feel free to leave a comment 💬

#Rokid #ARGlasses #Root #TechTalk

---

## Suggested images (optional):

1. Screenshot of a successful Fastboot connection
2. The output of fastboot getvar all
3. A photo of the Recovery menu (if you took one)

---

## Short version (if you think it's too long):

---

🔧 Rokid glasses root research - need help!

Hello everyone～

I'm researching how to root the Rokid glasses, and I found the bootloader is already unlocked 🎉
But I'm stuck at 【cannot extract boot.img】 😢

Tried ADB dd, fastboot fetch, sideload — all failed...

So I wanted to ask if anyone has:
1️⃣ Official Rokid firmware
2️⃣ A compatible TWRP
3️⃣ A successful root experience to share

If you have any information, please leave a comment or DM me! Thanks 🙏

#Rokid #Root

---
