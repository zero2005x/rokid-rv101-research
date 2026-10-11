# 通用無線 ADB 方案設計（RV101 眼鏡 × 開發者筆電）

> 目標：**開發者不需要插線**就能對眼鏡做 `adb connect` / `adb install` / `logcat` / 提權武裝。
> 使用者決策（2026-09-20）：①優先「PC 保有網際網路」②可接受**首次一次 USB/OTG** ③對象是
> **沒有 root、不跑 exploit 的外部開發者** ④眼鏡端 App 可自由修改。

---

## 0. 現況盤點（今天實測，決定方案空間）

| 事實 | 值 | 對方案的意義 |
|------|-----|--------------|
| adbd TCP listener | **已在**（`persist.adb.tcp.port=5555` → IPv6 `[::]:5555`） | 連線這一段**根本不用 root** |
| `cmd adb is-wifi-supported` | `false` | Android 11 官方「無線偵錯＋配對碼」**沒編進這台** |
| `settings get global adb_wifi_enabled` | `0` | 同上 |
| `/dev/socket/adbd` | 存在，`srw-rw---- system system` | 可當 userland relay 後端；**只有 root 能連**（shell/app 都被 DAC 擋） |
| `cmd wifi connect-network <ssid> wpa2 <pass>` | **存在** | 眼鏡可程式化加入開發者網段（STA）→ **PC 不必離開原 WiFi**（滿足優先 1） |
| STA ↔ SoftAP | 互斥 | 兩種拓撲只能擇一 |
| adb_keys | 6 把預授權、`ro.adb.secure=1` | 只要金鑰在裡面，連線不需授權彈窗（眼鏡也沒螢幕可彈） |

---

## 1. 三種拓撲（先選這個，其他都是細節）

| 拓撲 | PC 是否保有網際網路 | 需要 root？ | 適用 |
|------|------------------|-----------|------|
| **T1：眼鏡當 STA，與 PC 同網段**（**推薦，優先 1**） | ✅ 完全保有 | ❌ 只需 App | 開發者日常；眼鏡加入 `CHT6746`／手機熱點／辦公室 WiFi |
| **T2：眼鏡當 SoftAP，PC 加入** | ⚠️ 會短暫離開原網路 | ❌（屬性已設好） | 現場沒有共同網段時；可腳本化「連→測→還原」（本次已實作） |
| **T3：眼鏡當 SoftAP，手機／Windows 當閘道** | ✅ | 手機端需 root 做 DNAT；或用 Termux `socat` 免 root | PC 不能換網，且眼鏡進不了共同網段時 |

**T1 是唯一同時滿足「零線＋零 root＋PC 不斷網」的拓撲**，本方案以它為預設。

```
┌─────────────┐   同一個 WiFi 網段（例如 CHT6746）   ┌──────────────────────┐
│ 開發者 PC    │ ─────────────────────────────────── │ 眼鏡 RV101            │
│ adb connect  │        adb over TCP :5555            │  ├ GLASSES App        │
│ <glasses-ip> │                                      │  │   └ AdbWifiService │
└─────────────┘                                      │  │        ↓ AF_UNIX   │
                                                     │  └─ /dev/socket/adbd │
                                                     └──────────────────────┘
```

---

## 2. 元件設計

### 2.1 眼鏡端（改我們的 GLASSES App）

| 元件 | 職責 |
|------|------|
| `AdbWifiService`（前景服務） | 開 TCP listener（預設 5555）→ 轉 `/dev/socket/adbd`；多連線各一執行緒；`oom_score_adj` 保護 |
| `AdbWifiController` | 狀態機：`OFF → RELAY_UP → NETWORK_READY`；對外回報 `ip:port`、`boundOn`、`relayPid` |
| `WifiJoin`（STA） | 以 `cmd wifi connect-network`（root/shell）或 `WifiNetworkSuggestion`（App，需使用者同意一次）把眼鏡接上開發者網段 |
| `NsdAdvertiser`（mDNS） | 廣告 `_adb._tcp`（instance `adb-<serial>`）→ PC 端可**自動發現**，不必問 IP |
| 既有 `DebugReceiver` | 新增 `ADB_WIFI_START`／`ADB_WIFI_STOP`／`ADB_WIFI_STATUS`，讓手機 App 或開發者一鍵開關 |
| 既有 root 路徑（選配） | 若 App 有 root（研究版），改為 `setprop persist.adb.tcp.port 5555` ＋必要時授權金鑰 |

**關鍵設計決定**：relay 走 **App 自己的行程**，因此
- 綁 5555 不需要 root（>1024 的特權埠限制只影響 <1024）；
- 只要 SELinux 允許 `untrusted_app` 連 `adbd_socket`，**全程零 root**；
- 若 SELinux 不允許（今日實測中，見 §4），退路是「一次性 root 設定 `persist.adb.tcp.port`」，
  之後 adbd 自己聽 TCP，**開發者端仍全程零 root**。

### 2.2 PC 端小工具 `adbtool`（單一檔案、跨平台）

```
adbwifi discover            # mDNS 找 _adb._tcp（或掃 5555 網段）
adbwifi connect             # 自動 connect + 驗證 get-state=device
adbwifi status              # 列出所有 transport、是哪一台、走 USB 還是 WiFi
adbwifi keys install        # 把本機公鑰交給眼鏡（需眼鏡有 root 或 App 有權限）
adbwifi off                 # 關閉眼鏡 relay（走 App 通道）
```
- Windows 版可再包一層 `netsh portproxy`（WSL 直達 Windows 已驗證可行），
  讓 **WSL 內的 Linux adb** 也能用同一條無線路徑。

### 2.3 金鑰授權（「通用」的真正門檻）

`ro.adb.secure=1` → 每台新 PC 都要有一把金鑰在眼鏡的 `/data/misc/adb/adb_keys`。三種做法：

| 做法 | 需要 | 評價 |
|------|------|------|
| **A. 首次插線時由安裝腳本寫入一把「團隊共用開發金鑰」** | 一次 USB | ✅ 推薦：之後所有開發者用同一把金鑰，**永不需再授權** |
| B. App 顯示配對碼，PC 用 `adb pair` | 官方 wireless debugging（本機 `cmd adb` 無此子命令） | ❌ 本世代不可用 |
| C. 開發者各自插線一次授權 | 每人一次 USB | 可接受但違背「通用」 |

→ **建議採 A**：安裝腳本同時做「push App、寫 adb_keys、設 persist 屬性」三件事，一次搞定。

---

## 3. 首次安裝（唯一需要線的一次）

```bash
# 開發者第一次拿到眼鏡（PC 端）
bash tools/bootstrap_glasses.sh            # 一次 USB
#  1) 安裝 GLASSES App（內含 AdbWifiService）
#  2) 寫入團隊開發公鑰 → /data/misc/adb/adb_keys（需一次 root 或 App 協助）
#  3) setprop persist.adb.tcp.port 5555（需一次 root）
#  4) 拔線 → 之後全部走 WiFi
```

之後日常：
```bash
adbwifi discover && adbwifi connect && adb shell id     # 免線、免 root、PC 不斷網
```

---

## 4. 已定案：純 APK relay **不可行**（2026-09-20 實測）

用真實第三方 App（`com.rokid.rc.adbreachprobe`，uid 10064、`u:r:untrusted_app:s0`）實測：

```
PROBE_A_NATIVE_RAW errno=13 stage=2 (connect)     ← connect() 直接 EACCES
PROBE_A=ERR errno=13 Permission denied
PROBE_B=OK  （自己的 socket，對照組通過 → 測試儀器正常）
PROBE_C=ERR errno=111 Connection refused （0777 socket，DAC 放行、只是沒 listener）
SOCKSTAT /dev/socket/adbd mode=0660 uid=1000 gid=1000 access(W_OK)_errno=13
```

**判定：擋住的是 DAC，不是 SELinux。** `/dev/socket/adbd` 是 `0660 system:system`，
App（uid 10064、groups 9997/20064/50064）既非 owner 也不在 group 1000；
`access(W_OK)`（純 DAC、在 LSM hook 之前就回傳）同樣 EACCES，且全程 **沒有任何 `avc: denied`**。

| 結論 | 影響 |
|------|------|
| ❌ 純 APK（無 root）無法把 adbd 接上 TCP | 方案 3 取消；`AdbWifiService` 保留為**需 root**的研究版功能 |
| ✅ 但「開發者端全程免 root」仍成立 | 因為 adbd 自己聽 TCP 這件事由 `persist.adb.tcp.port`（一次性 root）或**既有預設值**解決 |
| ✅ 首次信任仍必須有線（或已在 `adb_keys` 的金鑰） | 與 §0「首次接觸物理上不可能純無線」一致 |

證據報告：[`reports/adbreachprobe_untrusted_app_adbd_socket_2026-09-20.md`](../reports/adbreachprobe_untrusted_app_adbd_socket_2026-09-20.md)。

---

## 4.1 開發模式實測（2026-09-20 端到端）

| 測試 | 結果 |
|------|------|
| 眼鏡加入網段 **路徑 B**（`adb shell cmd wifi connect-network LouisaCoffee wpa2 …`） | ✅ 成功，取得 `wlan0=10.0.0.120/24`、5 GHz RSSI −51、Link 325 Mbps |
| 眼鏡加入網段 **路徑 A**（App 內 `WifiNetworkSuggestion`） | ❌ **失敗**——headless 眼鏡沒有可點擊的系統通知；此路徑在**有螢幕的裝置**才實用 |
| PC 端連線（Windows adb，LAN 同網段） | ✅ `adb connect 10.0.0.120:5555` → `device`；`uid=2000(shell) u:r:shell:s0` |
| `tools/adbwifi discover 10.0.0` | ✅ 掃到並寫入快取：`<DEVICE_SERIAL> → 10.0.0.120` |
| `tools/adbwifi connect <序號>`（多裝置日常用法） | ✅ 由快取解析 IP 並連上 |
| `tools/adbwifi verify <序號>` | ✅ 印出 serial/uid/listener/網址/sdcard 讀取 |
| 敏感指令來源守衛 | ✅ 第三方 App 廣播被拒（`拒絕 DEV_MODE_START：來源不可信`）；adb shell 放行 |
| 開發模式失敗回退 | ✅ 無憑證 → `ERROR 沒有目標網段`；加入失敗 → `已回退（加入失敗）`，狀態回 OFF |
| 開發模式關閉 | ✅ `state=OFF msg=已關閉（user-stop）` |

**結論**：以「**adb shell 路徑 B**」為自動化主線（免 root、可靠、無需螢幕互動），
路徑 A 保留給有螢幕／有使用者的裝置。這正好符合「一次插線 bootstrap 後全無線」的架構：
bootstrap 之後，所有 dev-mode 觸發都走 adb（有線或無線皆可）。

---

## 5. 最終方案：**一次插線 → 之後永久無線**（零 root）

**核心洞察**：adbd **已經**在聽 TCP:5555（本機 `persist.adb.tcp.port=5555`），
且開發機金鑰**已在**眼鏡 `adb_keys`（實測：Windows 端金鑰 `cb94291f…` 為 6 把之一）。
所以「通用無線 adb」缺的只是**把眼鏡放上網段**這一步——而 `cmd wifi connect-network` 正好可用。

```
① 一次 USB（唯一）        tools/bootstrap_glasses.sh
   ├─ 安裝眼鏡 App
   ├─ 寫入開發機公鑰 → /data/misc/adb/adb_keys      ← 之後不再需要授權
   └─ setprop persist.adb.tcp.port 5555（一次性 root；本機已是 5555）
                    ↓  拔線
② 眼鏡上網段（免 root）   adb shell cmd wifi connect-network <SSID> wpa2 <pass>
③ 開發者連線（免 root）   tools/adbwifi connect <眼鏡IP>  → adb install / logcat / shell
```

**為什麼②不用 root**：`cmd wifi` 只要 shell 權限；而 shell 權限來自**已經打通的 adb**
（USB 一次或無線之後）。App 也可以用 `WifiNetworkSuggestion` 在使用者同意一次之後自動加入。

**PC 保有網際網路**：拓撲 T1（眼鏡當 STA）下，PC 完全不需換網段——滿足你的優先 1。

---

## 6. 元件設計（最終版）

### 6.1 眼鏡端
| 元件 | 狀態 | 說明 |
|------|------|------|
| `AdbWifiService`（TCP→`/dev/socket/adbd` relay） | ✅ 已實作，**需 root** | 研究版用；無 root 時會明確回報 `adbd socket 不可連`（DAC） |
| `WifiJoin`（`cmd wifi connect-network`） | 📋 設計 | 讓眼鏡加入開發者網段（免 root，走 shell） |
| `NsdAdvertiser`（`_adb._tcp`） | 📋 設計 | PC 端自動發現 |
| `DebugReceiver`：`ADB_WIFI_START/STOP/STATUS` | ✅ 已實作 | 一鍵開關（relay 版） |
| 既有 `ENABLE_WIFI_ADB`（persist 屬性） | ✅ 已修正偵測 | ACK 回 `boundOn`（ipv4/ipv6） |

### 6.2 PC 端
| 元件 | 狀態 | 說明 |
|------|------|------|
| `tools/adbwifi`（discover／connect／status／shell／install／keys／off） | ✅ 已實作並測過 | 走 Windows adb；`discover` 會用 Windows 的真實 LAN 前綴做 /24 並行掃描 |
| `tools/bootstrap_glasses.sh` | ✅ 已實作 | 首次一次 USB：App＋金鑰＋屬性＋驗證 |

### 6.3 金鑰授權（「通用」的門檻）
`ro.adb.secure=1` → 每台新 PC 都需要一把金鑰在 `adb_keys`。**建議採「團隊共用開發金鑰」**：
bootstrap 腳本把它寫進眼鏡，之後所有開發者共用同一把 → 永不需再授權。
官方配對碼路線不可用（本機 `cmd adb` 無 `pair`/`enable-wifi`）。

---

## 7. 風險與注意事項

| 風險 | 說明 | 對策 |
|------|------|------|
| 安全 | relay 一開＝同網段 anyone 可打 adb（仍需金鑰握手，但**共用金鑰＝共用信任**） | 預設只在「開發者網段」開；提供 `off` 一鍵關；可加 IP 白名單 |
| 眼鏡省電 | WiFi 休眠／App 被 LMK 殺 → relay 消失（本專案已有 LMK 前例 P15） | 前景服務＋`oom_score_adj=-1000`＋自動重啟＋狀態回報 |
| STA/SoftAP 互斥 | 開熱點會踢掉 STA | 服務內互斥管理，切換時更新對外 IP |
| 眼鏡無螢幕 | 無法顯示配對碼／授權彈窗 | 走共用金鑰（§2.3 A） |
| 版本破碎 | 不同 OTA 可能改 adbd 行為 | `adbwifi status` 每次驗證 listener＋`get-state`，失敗時明確指出原因 |

---

## 8. 實作狀態（2026-09-20）

| 項目 | 狀態 |
|------|------|
| 清除常駐曝露（`persist.adb.tcp.port`） | ✅ bootstrap 預設清空；本次已於提權後清除（見 §9） |
| `tools/adbwifi` 多裝置（快取／`--all`／`verify`） | ✅ 完成並實測 |
| `tools/bootstrap_glasses.sh` 多裝置＋安全預設 | ✅ 完成 |
| 眼鏡 `WifiJoin` A/B | ✅ A 完成（headless 不可用）／B 實測成功 |
| 眼鏡 `DevModeController`（逾時／回退／狀態回報） | ✅ 完成並實測 |
| 敏感指令來源守衛 | ✅ 完成並實測 |
| 手機端 `DEV_MODE_*` 協定與指令 | ✅ 完成（`Remote`＋`DebugReceiver`） |
| mDNS | ⛔ 依決策**不做**（改用序號→IP 快取） |
| 手機 App 的開發模式 UI 入口 | 📋 未做（目前以 DebugReceiver 指令驅動；UI 為後續工作） |
| 無 root 時「即時關閉 listener」 | ⚠ **做不到**（需 root 或重開機）；已明確回報 `listenerManaged` |

### 已知限制（誠實揭露）
1. **headless 限制**：路徑 A（`WifiNetworkSuggestion`）需要點擊系統通知 → 眼鏡無螢幕，
   故自動化一律走路徑 B（adb shell）。
2. **無 root 無法即時關 listener**：`persist.adb.tcp.port` 清了也要等 adbd 重生（重開機）。
   本機刻意**不重啟 adbd**（會掉 USB 列舉）。
3. **App 無法背景自動啟動**：眼鏡禁止第三方 App 背景執行 → 開發模式觸發需 App 在前景，
   或由 PC／手機透過 adb 觸發（bootstrap 後 adb 已可用）。

## 9. 提權清理紀錄
見 `deploy/get_root_now.log`；清除後以重開機驗證 listener 不再出現。

---

## 10. 最終機制與限制（2026-09-20 實測定案）

### 10.1 唯一可靠的「開發模式開關」＝原生 listener 切換
```sh
# ON  （root）
setprop persist.adb.tcp.port 5555 ; setprop ctl.restart adbd
# OFF （root）
setprop persist.adb.tcp.port ""   ; setprop ctl.restart adbd
```
**已實測**：ON → `persist=5555`、`listener6=1`、USB 保留；OFF → `persist=[]`、`listener6=0`、USB 保留。
`ctl.restart adbd` 確實會換 PID（1711→21088），**不需重開機**。

### 10.2 為何不用 App 內建 relay（兩條都試過，都不可行）
| 嘗試 | 結果 |
|------|------|
| Java `LocalSocket` → `/dev/socket/adbd` | 檔案為 `srw-rw---- system:system` → 非 system 被 **DAC 拒絕（EACCES）**；且 `LocalSocket` 在本 ROM 連 `socket()` 都沒建立 |
| 原生 `socket(AF_UNIX, SOCK_STREAM)` | **EPROTOTYPE(91)** |
| 原生 `socket(AF_UNIX, SOCK_SEQPACKET)` | connect 成功但**立即 EOF**（adbd 立刻關閉）→ 無法轉發 |
| 原生 `SOCK_DGRAM` / abstract namespace | EPROTOTYPE / ECONNREFUSED |

→ `/dev/socket/adbd` **不是可用的 relay 後端**；「TCP → adbd socket」此路不通。

### 10.3 WiFi 加入：只有路徑 B 在 headless 眼鏡可用
| 路徑 | 結果 |
|------|------|
| A：`WifiNetworkSuggestion`（App） | `addNetworkSuggestions status=0`（已註冊）但 `requestNetwork` 立即失敗——**headless 無螢幕可批准系統通知**。已補 `CHANGE_WIFI_STATE`＋`NEARBY_WIFI_DEVICES` 權限仍無效 |
| **B：`cmd wifi connect-network`（adb shell 或 root）** | ✅ **成功**：`wlan0=10.0.0.120/24`、5 GHz、RSSI −51 |

### 10.4 已實測的完整流程（PC 主導，最穩定）
```
① 插線一次：bootstrap（App＋開發公鑰＋清 persist）
② 提權一次（部署 root daemon；本機需每次開機武裝）
③ PC 讓眼鏡上網段：adb shell cmd wifi connect-network <SSID> wpa2 <密碼>
④ App/PC 開 listener：DEV_MODE_START（setprop + ctl.restart adbd）
⑤ PC：tools/adbwifi discover / connect <序號> / verify   ← 全程免線、免 root
⑥ 收工：DEV_MODE_STOP → listener 立即關閉（persist 清空、跨開機不再自動開）
```

### 10.5 兩個必須知道的限制（誠實揭露）
1. **`ctl.restart adbd` 會殺掉以 adb shell 啟動的 root daemon 與其 keepalive**
   （實測：restart 後 `nc -U` → Connection refused，socket 檔殘留但無 listener）。
   → 每次 ON/OFF 切換後若要再切換，需**重新武裝提權**一次（PC 執行 `deploy/get_root_now.sh`，約 1–3 分鐘）。
   → 這是本機 adbd 由 vendor 啟動的固有行為；App 的 keeper 因眼鏡「禁止第三方 App 背景執行」而無法自動補位。
2. **本機 root 不跨開機**（需每次開機重新武裝）。因此「開機後自動可無線」目前只有兩條路：
   `persist.adb.tcp.port` 常駐（＝放棄安全開關），或每次開機插線一次武裝。

---

## 11. 交接：目前裝置狀態與回復指令（2026-09-20 12:45）

| 項目 | 現值 | 說明 |
|------|------|------|
| 眼鏡 App | `1.0-safe.6`（versionCode 8） | 含開發模式；已安裝 |
| `persist.adb.tcp.port` | **`5555`** | 開發模式目前為 ON（最後一次測試留下） |
| adbd TCP listener | **1（`[::]:5555`）** | 但 WiFi 目前 **disabled** → 無網路曝露 |
| WiFi | disabled | 眼鏡不在任何網段 |
| root daemon | **不在**（被 `ctl.restart adbd` 殺掉） | 需重新武裝 |
| PC 端 transport | USB `device` ＋ `10.0.0.120:5555 offline`（可忽略） | |

**要回到「零曝露」狀態**（需先重新武裝 root）：
```bash
SERIAL=<DEVICE_SERIAL> bash deploy/get_root_now.sh      # 重新武裝（1–3 分鐘）
# 然後任一種：
#  a) App 驅動：am broadcast … --es cmd DEV_MODE_STOP
#  b) 部署腳本：printf 'setprop persist.adb.tcp.port ""\nsetprop ctl.restart adbd\n' | adb shell nc -U /data/local/tmp/root_daemon.sock
#  c) 直接重開機（persist 已為 5555 → 仍會開；故仍需 a 或 b 先清 persist）
```

**恢復「無線 adb 可用」狀態**：
```bash
SERIAL=<DEVICE_SERIAL> bash deploy/get_root_now.sh                 # 武裝 root
adb shell cmd wifi set-wifi-enabled enabled && adb shell cmd wifi connect-network <SSID> wpa2 <密碼>
am broadcast -n com.rokid.rc.glasses/.DebugReceiver -a com.rokid.rc.glasses.DEBUG --es cmd DEV_MODE_START
tools/adbwifi discover && tools/adbwifi connect <序號> && tools/adbwifi verify <序號>
```
