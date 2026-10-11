# GhostLock RV101 — 工作狀態交接 v5（第五任 → 第六任）
> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。


> 更新於 2026-08-18 凌晨。本輪（第五任）自 v9.3 接手，歷經 v9.3→v9.19 共 17 版，
> 解開了全部回收機制謎團，**並於 run 32/33 首次觀測到 payload-walk 寫入（selftest
> 通過）**。v4 交接的「v9 回收設計」建立在三個錯誤前提上（見 §2），全部被本輪
> 的裝置實測推翻。
>
> 工作目錄：`C:\Users\<USER>\Documents\codebase\rokid_root`
> 源碼：`source_code\ghostlock_rv101\`（binary md5 見 §4）
> 目標裝置：`<DEVICE_SERIAL>`（RV101 / RG_glasses；Redmi `<PHONE_SERIAL>` 勿碰）

---

## 1. 里程碑（本輪唯一硬指標）

**vehicle forensic 已出現 diff —— payload 在位 + walk 寫入首次被觀測到。**

- run 32：`SELFTEST: write into sprayed page verified: 1 vehicle(s) carried kernel
  writes (attempt 1)`，vehicle[5] 的 diffs（offset 0x0E08+，leftmost=w0、owner=ftask|1），
  gate window −1（乾淨）。
- run 33：同樣 attempt 1 通過（vehicle[4]）。**可重現（2/2，均為 attempt 1）。**
- 判據環境變數：`GL_DUMMY_KILLS=0`（leftovers-trigger，見 §3.3）+ 其餘同下。

未完成：W1/W2（run 34 的 W1 在長開機後 24 次 attempt 全敗 —— 見 §5 的殘餘問題）。

---

## 2. 本輪推翻的 v4 前提（勿再踩）

1. **memfd pin 會洩漏**（probe3 實證）：本裝置 close(`/proc/pid/mem`) **不釋放 mm**。
   28 秒 + 4096 file callbacks 後 mm 仍 561/561。v9 的「pin + 有序 close = 回收序列」
   整條鏈是死的。
2. **mm_struct 的 free 是 lazy-RCU**（probe2 實證）：無 pin 的 exit-time mm free 也要
   ~10s 批量 flush（閾值 ~100-224 個 lazy callback，FIFO 執行）。v9 的「close 同步釋放」
   不存在。
3. **SKB payload 有 ±NET_SKB_PAD 錯位**：send() 的資料從 slab+64 開始（skb_reserve 的
   NET_SKB_PAD），v4 的 `SKB_DATA_DELTA=0` 假設資料在 block+0，payload 全部偏了 64 位元組。
   參考實作（oneplus/aresin/emerald/m3q）都用 `SKB_DATA_DELTA (-0xe80)`。已修
   （`GL_SKB_PAD`，預設 64；payload 與 forensic 提取同步 −64）。

## 2b. 本輪解開的機制全圖（裝置實測）

| 機制 | 實證 | 對策 |
|---|---|---|
| memfd pin 洩漏 | probe2-B / probe3：pinned mm 30s+ 不釋放 | 棄用 pin 回收（保留少量 pin 當「slab 永不空的守衛」） |
| mm free 為 lazy-RCU，批量 flush | probe2-A：exit-time mm free ~10.7s 才釋放 | 殺子進程序 = 回收序；flush 由閾值觸發 |
| flush 閾值 ~200-300 callbacks，每次 drain ~150-200 | probe5 / run19/22 | builders 288 + rest 448 造量；pre+leak 排在 drain 窗內 |
| 殺子進程的 task callback 喚醒 drain | run19：766 殺 → ~480 釋放；145 殺 → 0 | 量的來源 |
| 內核 stack 經 vmap-lazy 釋放，~32MB 閾值大 purge | run26：phase-3 才 +1456（閾值被 dummy 推過） | purge squad 700 在 phase-1 推過閾值，settle 內 purge 完 |
| purge 的 ~20-47 block 小 burst 與 drain 同 wake | run23/27/28 的 gate-window +16/+20/+47 | pre+leak 放 queue 尾 + 無 dummy 觸發（GL_DUMMY_KILLS=0） |
| 垃圾頁 walk 的 panic 模式 | runs 12-27 多次 | 成功後自然消失 |

---

## 3. 現行 v9.19 設計（prepare_good_kernel_page）

1. **克隆**：flood 765（early 204 + late 561）+ pre 16 + leak child（kernelsnitch 後
   **pause 存活**）+ post 17 + dummy 450 + purge 700。共 ~1950 線程（< ulimit 6608）。
2. **守衛 pin**：early flood 每 slab 1 pin（12）、post 1 pin、dummy/purge 每 slab 1 pin
   —— pin 洩漏恰好保證這些 slab 永不變空（不產生 discard、不埋 leak slab）。
3. **phase-1 kills**：builders（late flood 前 18 slab 的 288 sibling）→ early flood
   （192）→ late flood 剩餘（240）→ post（16）→ purge squad（659）。全部 frees 進 lazy
   queue；builders 的 slab 變 1-inuse 上 node partial（nr_partial ≥ 5 的來源）；
   purge squad 的 stack 把 vmap lazy 清單推過 ~32MB，purge 在 settle 內完成。
4. **settle**：固定 ≥6s soak + o2 與 mm-objs 連續 6 輪（3s）雙安靜（cap 20s）。
5. **phase-3**：dummy 觸發（`GL_DUMMY_KILLS`，0=純 leftovers）→ **pre+leak 最後殺**
   （leak slab 的 17 個 free 落在 queue 尾、最後一個 drain 窗內）→ slabinfo 的
   active_slabs **−1 dip**（leak slab discard 的唯一訊號）→ **dip 當下立刻 send**。
6. send vehicle[0] 搶 buddy LIFO 頭（leak slab）→ 其餘 767 → route/punch（不變）。

## 3b. 關鍵洞察（供下一任直接引用）

- **dip 判據**：early flood/post 的 slab 變空時 nr_partial < 5 → park（不 discard、
  不 drop 計數）；只有 leak slab（FIFO 最後 + nr_partial ~33）會 discard → active_slabs
  的 **−1 是 leak slab 的專屬訊號**。
- **leftovers-trigger**：settle 的 drain 停在 queue < 閾值（剩 ~0-250）；phase-3 的
  pre+leak 17 個 + leftovers ≥ 閾值時 flush 自己點火（無新 stack、無 purge 洪流）——
  run 32/33 的成功路徑。`GL_DUMMY_KILLS=0` 時成功率 ~30-50%/attempt（leftovers 大小的
  邊界運氣）。
- **±64 pad**：`GL_SKB_PAD`（預設 64）已同時作用於 payload 寫入與 forensic 提取。
  vehicle diffs 觀測位置為 recv buffer 的 0x0E08+（≈ slab 0xE08+64，與 walk 寫入
  對齊後待最後校準）。

---

## 4. 產物 / 檔案

| 位置 | 內容 |
|---|---|
| `source_code/ghostlock_rv101/` | v9.19 源碼（binary md5 `3CCAAAB61135B81844DEBA7CFC432AD4`；裝置已同步） |
| `reports/ghostlock_runlogs/exploit_run{13..35}*.log` | 本輪 23 份裝置日誌（run 32/33 為成功樣本） |
| `source_code/ghostlock_rv101/src/mmfree_probe{1,2,3,5}.c` | 裝置實測探針（pin 洩漏 / lazy free / 觸發器） |
| `reports/GHOSTLOCK_V5_RECLAIM_MECHANISM.md` | 機制全圖（本文件 §2b 的詳版） |

建置（照舊）：
```
$env:ANDROID_NDK_HOME='C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\android-ndk-r27d';
cd source_code\ghostlock_rv101; mingw32-make -B
```
push 後務必 Get-FileHash vs 裝置 md5sum 比對（PowerShell 管線吃失敗碼）。

跑法（成功參數）：
```
adb -s <DEVICE_SERIAL> shell "GL_DEBUG_DRAIN=1 GL_SETUP_ATTEMPTS=8 GL_DUMMY_KILLS=0 /data/local/tmp/ghostlock_rv101 --selftest"
```

---

## 5. 殘餘問題（下一任的待辦，按優先序）

1. **W1/W2 未達**：run 34 的 W1 在長開機（~30 min、mm cache 累積到 192 slab）後 24 次
   attempt 全敗；run 32/33 的成功都在**新開機的前幾個 attempt**。嫌疑：
   (a) 每 attempt 洩漏 ~80 pinned mm（~5 slab）累積後干擾 settle/drain；
   (b) leftovers 的大小邊界運氣（dip 時有時無）；
   (c) W1 的 mode-1 walk 是否與 mode-3 有別（payload 已修 ±64 後未在 W1 驗證過）。
   → 對策候選：每個 W1 attempt 前 `adb reboot`（新機狀態）；或把 pinned 守衛的 slab
   在 cleanup 釋放（現在 pin 洩漏是永久性的）。
2. **成功率工程化**：把 leftovers 大小做成可觀測（settle 結束時的 objs 變化率）並在
   phase-3 前補足到 ≥ 閾值（小 dummy 觸發 50-150，權衡 purge 小 burst）。
3. **±64 的最終校準**：vehicle diffs 的 0x0E08 位置暗示 pad 需再驗證（±0/±64/±128 掃
   描，`GL_SKB_PAD` 可調）；forensic 5 欄位提取（child_pc/scratch/usage）目前還未完全
   對齊（run 32 的 child_pc=0、usage=0x100，預期 (target-8)|1 與 0x101）。
4. selftest 通過後：W1（`--write1` → getenforce=Permissive）→ W2（cred=init_cred）。
   操作紀律照舊（每次 panic 2-5 分鐘自動重開、KASLR 自動重洩漏、dmesg 無用）。

---

## 6. 環境注意（增補 v4）

- 每 attempt 現在 ~12-20s（settle 6-20s）；GL_SETUP_ATTEMPTS 可調（1-12）。
- 裝置每次 panic 硬 reset；run 後常 OOM 重開（pinned mm 累積），計畫 reboot 前後都
  要驗 uptime ≥ 5 min 再跑。
- `GL_DUMMY_KILLS=0` 是唯一觀測到成功的觸發配置；dummy 450 全殺的配置（預設）會把
  vmap 閾值在 phase-3 推過、gate window 必見 +16~+47 purge burst（run 28/29 實證）。
