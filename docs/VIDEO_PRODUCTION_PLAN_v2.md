# 修正後製作計畫 v2 —《Rokid RV101 Root 研究紀錄》

> English: [VIDEO_PRODUCTION_PLAN_v2_EN.md](VIDEO_PRODUCTION_PLAN_v2_EN.md)

> 版本：v2（私人審閱版優先）｜2026-09-06
> 依據：`docs/Disclosure_Timeline.md`、`docs/ghostlock-before-after-redacted.md`、
> `RV101_Disclosure/00_README.md`、`RV101_Disclosure/CVE_Relationship_Assessment.md`、
> `RV101_Disclosure/13_Redacted_Public_Materials/post_exploit_state_REDACTED.txt`、
> `reports/RV101_PRIVACY_LED_TRUST_BOUNDARY_AUDIT.md`、`reports/RV101_CAMERA_LED_FLASH_MECHANISM.md`。
> 本計畫取代 v1（ChatGPT 版），凡與上述文件衝突者，一律以本文件為準。

---

## 0. 一頁結論

v1 的**流程**（提問→計畫→核可→開工）沒問題，但**內容**有四處踩到你自己設的紅線，
其中一處是責任揭露等級的違規。v2 的核心改動：

1. **刪除「指示燈熄滅／壓制」實機展示** —— 隱私 LED（RS-8）**未修補、仍在 embargo**。
2. **本版定位為「私人審閱版」** —— 公開版押後到 2026-11-23（你自訂的揭露日）且 write-up
   先送廠商 review 之後。
3. **root 證據改用正確畫面** —— 用 `uid=2000 → uid=0(root)` 紅化版，捨棄 `uid=10053` App 截圖。
4. **「99% AI」「修補複測完成」改措辭** —— 前者改「AI 協作」，後者改「靜態比對確認 backport」。

---

## 1. v1 → v2 逐項對照（重剪時照這張表改）

| # | v1 內容 | 問題 | v2 修正 |
|---|---|---|---|
| 1 | 分鏡含「指示燈實機展示」「熄燈與信任邊界」 | RS-8 未修補＋embargo；「運作中熄燈」實為 INFERRED（未觸發 capture 的端到端演示） | 刪除一切「熄燈／壓制／隱蔽模式」畫面與旁白；僅保留「無硬體閃光燈，白光為 camera-in-use 指示燈」之機制事實 |
| 2 | 自製 App「隱蔽模式（壓制 LED）」入鏡 | 等同公開未修補能力 | App 只展示合法功能（藍牙遙控拍照／相簿管理），隱蔽模式完全不入鏡 |
| 3 | 公開上傳 YouTube 的預設 | 揭露日 2026-11-23 未到、write-up 未送廠商 review | 本版＝私人審閱版；公開版另行解鎖（見 §3） |
| 4 | 「uid=0(root) 特寫」+ 那張 uid=10053 截圖 | App 的 uid 當成 root 證據 | 改用紅化版 `uid=0(root) gid=0(root) context=u:r:kernel:s0` |
| 5 | 「研究者估計約 99% 工作由 AI agents 完成」 | 無文件背書；面試/審查會被追問本人貢獻 | 「AI 協作開發：人類負責研究設計、驗證與協調揭露」 |
| 6 | 「研究者已完成修補複測」 | 1.22.009 那半邊反組譯仍 `[待補]` | 「依研究者靜態比對，確認 1.25.012 含 upstream 3bfdc63 backport」 |
| 7 | 片尾未提揭露狀態 | 缺負責任揭露宣告 | 片尾加「初始報告 2026-08-25、廠商 2026-09-06 確認修補、揭露日 2026-11-23」 |
| 8 | 署名「mazuda payrung」 | 與給原廠的 credit「<RESEARCHER_NAME>（<RESEARCHER_NAME>）」不一致 | 見 §9 待你定奪 |

---

## 2. 定位與公開策略（最重要）

- **本版用途**：教授審查、資安求職面試、原廠私下協作，或自存研究紀錄。**不做公開上傳。**
- **可安全呈現的「核心內容」**（受影響漏洞已修補，證據已紅化）：
  1. GhostLock（CVE-2026-43499）的 runtime 提權證據。
  2. 研究環境與前提。
  3. 自製 App 的合法功能。
  4. AI 協作開發流程。
  5. 通報與修補狀態。
- **公開版解鎖條件**（全部滿足才可上 YouTube 等）：
  1. 已達揭露日 **2026-11-23**（或經原廠協調的日期）。
  2. sanitized write-up 已**先送原廠 review**。
  3. 隱私 LED（RS-8）段已確認**修補並解除 embargo**，或該段永久移除。
  4. 遮蔽清單（§7）全數過關。

> ⚠️ 關鍵判斷：GhostLock 已修補，公開講「驗證既有 CVE」風險較低；但**隱私 LED 那條沒修，
> 公開示範等於提前釋出一個未修補的隱私繞過**。這是 v1 唯一必須「現在就拿掉」的段落。

---

## 3. 交付規格（維持 v1 不變）

| 項目 | 規格 |
|---|---|
| 版本 | 華語 180s、華語 60s、英語 180s、英語 60s，各一支 MP4 |
| 比例／解析度 | 9:16 直式，1080×1920 |
| 旁白 | 台灣華語男聲合成＋英語合成，冷靜技術紀錄風格 |
| 字幕 | 繁中＋英文字幕（內嵌）＋雙語 SRT |
| 音樂 | 低音量背景音樂 |
| 片名 | 《Rokid RV101 Root 研究紀錄｜驗證既有 CVE 與實機驗證》 |

---

## 4. 完整版分鏡（180 秒，私人審閱版）

| 時間 | 畫面 | 旁白要點（逐字方向） | 證據／注意 |
|---|---|---|---|
| 0:00–0:12 | 眼鏡實體、開機／戴上畫面 + 片名卡 | 「Rokid RV101。今天記錄一次在自有裝置上的本機權限提升研究，以及它與 CVE-2026-43499 的關係。」 | 片名即定位「驗證既有 CVE」 |
| 0:12–0:38 | 終端環境：`getprop`、`uname`、`adb shell` | 「研究環境：Android 12（SKQ1.240613.001）、kernel 5.10.209-perf、安全補綴 2024-07-05。初始權限是 ADB shell 的 uid 2000。」 | **Bootloader 已解鎖列為「環境條件」，補一句「此為環境條件，非本漏洞前置條件」**；勿暗示需要解鎖才能打 |
| 0:38–1:10 | 紅化版 before/after 定格：`getenforce`→`Enforcing`、`id`→`uid=0(root) … u:r:kernel:s0`、`getenforce`→`Permissive`、`CapEff 000001ffffffffff`、`/data/root_proof` | 「起始狀態 SELinux Enforcing、uid 2000。觸發後取得 uid 0、完整 effective capabilities、SELinux Permissive。這是一次運行時提權——不宣稱跨重開機持久、不宣稱 AVB／dm-verity 繞過。」 | 畫面只用 `post_exploit_state_REDACTED.txt`；**不用 uid=10053 App 截圖** |
| 1:10–1:40 | 自製 App：手機端藍牙遙控、眼鏡端 CameraX 拍照／錄影、相簿管理 | 「研究期間也做了一套遙控拍攝系統：手機經藍牙 RFCOMM 控制眼鏡拍照、錄影與相簿管理，用於收集驗證素材。」 | **隱蔽模式／壓制 LED 不入鏡** |
| 1:40–2:05 | 文件化研究流程：時間線、反組譯比對 `remove_waiter()`、紅化比較 | 「整個過程由 AI 協作完成：人類負責研究設計、實機驗證與協調揭露，AI agents 負責逆向分析、文件與腳本產出。」 | 「99%」刪除；呈現「分工」而非「比例」 |
| 2:05–2:30 | 通報時間線卡：8/25 初始報告 → 9/1 OTA → 9/6 廠商確認 | 「漏洞為 CVE-2026-43499『GhostLock』，rt-mutex futex-PI use-after-free。受影響韌體 1.22.009 及更早；Rokid 已於 2026-09-01 推送 1.25.012 修補，9/6 確認納入 backport。本研究定位是『驗證既有 CVE 於 RV101 的可利用性與修補狀態』，非發現新漏洞。」 | 定位句**必須**出現 |
| 2:30–3:00 | 限制清單卡 + 研究聲明 + 片尾卡 | 「限制：重開機需重新提權、成功率未達 100%、僅於一台裝置一個韌體驗證。以下為研究聲明。」 | 見 §6 片尾卡 |

### 4.1（可選、僅私人版）隱私 LED 段 —— 公開版一律刪除

> 若教授／面試官需要看這項研究的深度，可插入 1:10 之後的**受控段**，只准講機制、不示範攻擊：

| 時間 | 畫面 | 允許講的 | 禁止 |
|---|---|---|---|
| 0:45 內 | 白光 LED 正常點亮／熄滅（相機開關時） | 「RV101 沒有硬體閃光燈；那顆白光 LED 是 camera-in-use 指示燈，由軟體鏈驅動。」 | **不示範熄燈／壓制**、不提路徑 A/B/C 指令、不提 sysfs/Binder/`session_open` 細節、不宣稱「運作中熄燈」為已證實（其為 INFERRED） |
| — | 標註 | 「此機制屬隱私信任邊界議題，已循協調揭露通報原廠，細節待揭露期後公開。」 | — |

---

## 5. 短版分鏡（60 秒，私人審閱版）

| 時間 | 內容 |
|---|---|
| 0:00–0:10 | root 證據定格（紅化版 uid 0 / Permissive） |
| 0:10–0:22 | 研究環境 + 前提（含 bootloader＝環境條件） |
| 0:22–0:35 | 自製 App 合法功能（藍牙遙控／相簿） |
| 0:35–0:48 | AI 協作 + 通報修補（CVE-2026-43499、1.25.012 修補） |
| 0:48–0:52 | 限制（重開機需重新提權、成功率、單裝置） |
| 0:52–0:60 | 片尾卡 |

---

## 6. 片尾卡文案

### 6.1 私人審閱版（現在就用這張）

```
本研究於本人持有並獲授權之設備及帳號上進行，僅供安全研究與互通性測試。
漏洞：CVE-2026-43499「GhostLock」（Linux rt-mutex futex-PI use-after-free）→ 本地權限提升。
定位：於 Rokid RV101 驗證既有 CVE 之可利用性與修補狀態，非新 CVE、非獨立漏洞開發。
修補：Rokid 已於 2026-09-01 推送韌體 1.25.012-20260901-150201；
　　　依研究者靜態比對，確認含 upstream 3bfdc63 backport。
協調揭露：初始報告 2026-08-25｜廠商 2026-09-06 確認修補｜揭露日 2026-11-23（或經協調）。
署名：mazuda payrung｜AI 協作安全研究
```

### 6.2 公開版（解鎖後追加一行）

```
本片僅呈現已修補之 CVE 內容；未修補項目依協調揭露原則不予公開。
```

---

## 7. 遮蔽清單（重剪與匯出前，用這些字串全文 grep 一遍）

### 7.1 絕對禁止出現（任何版本）

| 類別 | 具體字串／值 |
|---|---|
| 裝置序號 | `<DEVICE_SERIAL>` |
| 手機序號 | `<PHONE_SERIAL>` |
| 真名（若採筆名） | `<RESEARCHER_NAME>`、`<RESEARCHER_NAME>` |
| 信箱 | `<RESEARCHER_EMAIL>` |
| Windows 本機路徑 | `C:\Users\<USER>\...`、`Documents\codebase\rokid_root` |
| kernel 位址 | `0xffffffc0*`、KASLR base/slide、`printk_formats`、`init_cred`、`selinux_state` |
| 絕對 vaddr 錨點 | `0xef670`、`0xef998`、`0x22b744`、`0x228fac`、`0x229308`、`0x88188` |
| exploit 內部 | `target_rv101.h`、FOPS install、SKB/reclaim、pselect/rb_erase punch、heap grooming、`ghostlock_rv101`（二進位名） |
| 精確重現指令 | `boot_run_loop.sh 4 w2 v960`、`GL_PURGE_CNT`、`GL_TAIL_KILLS`、`W2_ATTEMPTS`、`GL_DUMMY_KILLS` |
| 分區佈局 | `ls -l /dev/block/by-name` 輸出、`/dev/block/mmcblk0p*` 完整列舉 |
| 金鑰 | OTA keys、ADB keys、任何 log 中的第三方 secret |

### 7.2 允許出現（紅化版可直接用）

- `id` / `id -Z` / `getenforce` / `Uid/Gid/CapInh/CapPrm/CapEff/CapBnd` 各行。
- firmware/build fingerprint、Android／kernel／SPL 版本字串。
- 「system/vendor 維持唯讀」陳述。
- 非破壞示範：建立 `/data/root_proof`。
- CVE-2026-43499 引用與「受影響／修補」關係。
- 函式**相對**偏移＋符號名（如 `remove_waiter` 的 `+0x040 ldr x20,[x23,#0x30]`）——**相對偏移可，絕對位址不可**。

### 7.3 匯出前自動檢查（把下列 grep 當成硬性門檻）

```bash
grep -RInE '<DEVICE_SERIAL>|<PHONE_SERIAL>|<RESEARCHER_HANDLE>|<USER>|0xffffffc0|0xef[0-9a-f]{3}|0x22b744|0x228fac|0x229308|0x88188|target_rv101|ghostlock_rv101|GL_PURGE_CNT|W2_ATTEMPTS' 剪輯專案/腳本/字幕/
```

任何命中 → 遮蔽或刪除後才可匯出。

---

## 8. 素材對照（哪些能用、哪些不能用）

| 素材 | 能用？ | 用途／限制 |
|---|---|---|
| 眼鏡實體、開機、戴上 | ✅ | 開場 |
| 終端 before/after（紅化版） | ✅ | root 證據主畫面 |
| 原始 `run_w2_v960_143215.log` | ❌ 公開 | 內含位址與 exploit 內部，僅私下看，不剪進片 |
| App 藍牙遙控／相簿畫面 | ✅ | 合法功能展示 |
| App「隱蔽模式／壓制 LED」畫面 | ❌ | 未修補，刪除 |
| 白光 LED 正常點亮（相機開） | ⚠️ 僅私人版 | 只展示正常行為，不示範熄燈 |
| 白光 LED「熄滅／壓制」demo | ❌ | 未修補＋embargo，刪除 |
| `wm_work/` 兩支燈效片 | ⚠️ 僅私人版 | 若含「熄燈」或路徑細節，公開版移除 |
| 通報信件／時間線 | ✅（去識別） | 隱去真名、信箱、序號 |

---

## 9. 待你定奪的兩件事（先決定再重剪）

1. **署名**：公開影片用哪個名字？
   - 選 A（推薦）：與原廠 credit 一致 → **「<RESEARCHER_NAME>（<RESEARCHER_NAME>）」**，可信度最高、對得上 Rokid 未來的公開致謝。
   - 選 B：維持筆名 **「mazuda payrung」**，但**全片不得出現真名／信箱**，且需在片尾補一句「本片與原廠 credit 之姓名不同」以避免審查者質疑。→ 我個人不推薦，易被面試官打槍。
   - 選 C：筆名＋括號真名（僅私人審閱版可用，公開版只留其一）。
2. **隱私 LED 段要不要保留在「私人版」？** 保留＝只能給教授／面試官／原廠私下看；不保留＝最保守、零風險。公開版一律不保留。

---

## 10. 上傳／交付前檢查清單（逐項打勾）

- [ ] 署名已定調（§9-1），全片無另一身份的 PII。
- [ ] 隱私 LED「熄燈／壓制」畫面與旁白已全數移除（或確認僅私人版）。
- [ ] root 證據只來自紅化版，無 uid=10053 App 截圖冒充。
- [ ] 「99% AI」已改為「AI 協作」。
- [ ] 「修補複測」已改為「靜態比對確認 backport」。
- [ ] 「驗證既有 CVE、非新漏洞」定位句已入片。
- [ ] bootloader＝環境條件、非前置條件，已註明。
- [ ] 四項限制（重開機重提權／成功率／單裝置單韌體／無持久無 AVB 繞過）已陳述。
- [ ] §7.3 grep 全數零命中。
- [ ] 若是公開版：揭露日已到、write-up 已送廠商 review、RS-8 已修補或該段已移除。

---

## 11. 實際 v1 檔案逐場景重剪清單（2026-09-06 讀取實際成品後補）

> 已直接讀取 `video_picture_ori/` 下的成品與 `RV101_Edit_Project（**外部剪輯專案，不在本工作區**）/production/{story.json,design.py}`。
> v1 由 16 張場景卡（`scene_00..15`）組成：180s 用 id 0–11，60s 用 id 12–15。
> 「改」＝重渲該場景卡＋重合成配音字幕；「刪」＝抽掉該場景並重排時間。

### 11.1 已驗證 OK（不需改）

- 四支 MP4 皆真實存在且規格正確：1080×1920、24fps、180s/60s（`exiftool` 實測 3:00 / 1:00）。
- 全部文字檔（.srt/.ass/.md/.json/.py）PII grep **零命中**；來源圖 EXIF 無 GPS／序號／作者。
- 旁白已有多處保守措辭：root 與 Permissive 分開、不主張遠端攻擊／一般 App 提權、不重製假終端、修補未獨立驗證、限制已列。

### 11.2 必須改（紅線）

| 場景 | 現況（`design.py`/`story.json`） | 問題 | 改法 |
|---|---|---|---|
| **id 7（180s）／id 13（60s）「指示燈研究」** | `visual="led"`，燒入「實機燈號 / PHYSICAL INDICATOR」＋ LED 控制照片（來源 `下指令後常亮起綠光.jpg`、`放進盒子內也亮起綠光.jpg`、`展示閃爍白紅藍綠四色光.mp4`） | 演示「軟體控制指示燈」＝ RS-8，**未修補＋embargo** | **刪除該場景**，或改為純機制說明卡（「無硬體閃光燈、白光為 camera-in-use 指示燈」），**不出現任何控制／熄燈／常亮照片** |
| **id 8（180s）／id 14（60s）「AI 協作」** | `text(im,(86,455),'~99%',160,...)` 巨大「~99%」 | 具體比例無文件背書 | 把「~99%」改為「AI-ASSISTED／AI 協作」，旁白刪「百分之九十九」，改「AI agents 參與研究與開發協作」 |

### 11.3 建議改（準確性）

| 場景 | 現況 | 問題 | 改法 |
|---|---|---|---|
| **id 0/3/12「root」** | 巨大「uid=0」是 `design.py` **渲染的圖形字**（非證據）；真證據只是 66px 高的裁切橫條 `(96,820,888,66)` | 「uid=0」大字易被誤讀為證據 | 把大圖形字改標註「LEGEND／圖例」；真證據改用 `post_exploit_state_REDACTED.txt` 的 `uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0` 完整可讀呈現 |
| **id 3「GhostLock」** | 只說「研究文件將這條提權路徑稱為 GhostLock」 | **全片缺「驗證既有 CVE-2026-43499、非新漏洞」定位句** | 加一句旁白＋卡：「本研究於 RV101 驗證既有 CVE-2026-43499 之可利用性與修補狀態，非發現新 CVE」 |
| **id 2「起點」** | 「Bootloader 已解鎖」無但書 | 易被誤讀成漏洞前置條件 | 補「Bootloader 已解鎖為測試環境條件，非本漏洞前置條件」 |
| **id 11/15「結尾卡」** | 只有「2026.09.01 修補韌體發布」＋署名 | 缺揭露時間線 | 加「初始報告 2026-08-25｜廠商 2026-09-06 確認修補｜揭露日 2026-11-23」；並補「非新 CVE」 |
| **全部 16 場景 header/footer** | `design.py` 把「MAZUDA PAYRUNG」燒進**每一格** header（line 44）＋ footer（line 52）＋結尾卡（line 130） | 署名決策（§9）一變，16 張全要重渲 | 先定署名（§9）再重渲；若採真名則 16 格全換 |

### 11.4 交付前請你親自查一件事

`design.py` 的 root 場景實際用的是 `frames/root-best.png`（取自 1000072772.mp4 00:05.6）與 `01-1000073132.jpg`（兩者都是**渲染中間檔，未隨交付 zip 打包**）。我無法驗證這兩張圖的內容。請你打開成品確認：

- [ ] 影片裡那條「root 證據」顯示的是 **`uid=0(root) gid=0(root) ... u:r:kernel:s0`**，
      而**不是** App 的 **`uid=10053`**（ChatGPT 第一張截圖就是 uid=10053，那張不能當 root 證據）。
- [ ] 若顯示的是 uid=10053，改用 `取得最高權限_root_0_selinux_pervassive.jpg` 或
      `RV101_Disclosure/13_Redacted_Public_Materials/post_exploit_state_REDACTED.txt` 重新取證。
