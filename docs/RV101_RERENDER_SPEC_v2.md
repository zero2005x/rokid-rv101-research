# RV101 剪輯師重渲清單 v2（精確到檔案與行號）

> 對象：`RV101_Edit_Project（**外部剪輯專案，不在本工作區**）/production/` 下的 `design.py`、`story.json`。
> 成品輸出：重新跑 `design.py`（重產 `assets/scene_00..15.png`）→ `render.py`（重合成 MP4）。
> 對應旁白：`docs/RV101_NARRATION_v2.md`。

---

## 0. 總覽：哪些場景要動

| scene | 180s/60s | 動作 | 原因 |
|---|---|---|---|
| 0, 1, 4, 5, 6, 9, 10 | 180s | 不改 | 已合格 |
| 2 | 180s | 改 | bootloader 但書 |
| 3 | 180s | 改 | 「非新 CVE」定位 |
| 7 | 180s | 改（或刪） | RS-8 embargo |
| 8 | 180s | 改 | 「~99%」→「AI 協作」 |
| 11 | 180s | 改 | 揭露時間線＋定位 |
| 12 | 60s | 改 | bootloader 但書＋定位 |
| 13 | 60s | 改（或刪） | RS-8 embargo |
| 14 | 60s | 改 | 「~99%」→「AI 協作」 |
| 15 | 60s | 改 | 揭露時間線＋定位 |

---

## 1. `design.py` 改動（照行號，改完重跑）

### 1.1 署名（若 §9 改真名才動；否則跳過）
- line 44 `text(im,(105,64),'MAZUDA PAYRUNG',28,FG,True)` → 換名字
- line 52 `text(im,(72,1851),'mazuda payrung  ·  繁中 / EN',23,MUTED)` → 換名字
- line 130 `text(im,(88,1245),'mazuda payrung',63,FG,True)` → 換名字

### 1.2 `titles` 陣列（line 54–62）更新標題
- 索引 2 → `('本地提權的起點','AUTHORIZED ADB SHELL / ENV CONDITION')`
- 索引 3 → `('成果，驗證既有 CVE','GHOSTLOCK / CVE-2026-43499')`
- 索引 7 → `('拍攝指示燈機制','INDICATOR MECHANISM — NO FLASH')`
- 索引 8 → `('AI 協作開發','AI-ASSISTED RESEARCH')`
- 索引 11 → `('通報、修補與署名','DISCLOSURE TIMELINE & CREDIT')`
- 索引 12 → `('RV101 研究成果','EXISTING CVE VALIDATION')`
- 索引 13 → `('裝置、App 與指示燈','DEVICE / APP / INDICATOR')`
- 索引 14 → `('AI 協作與研究限制','AI COLLABORATION / LIMITS')`
- 索引 15 → `('通報、修補與署名','DISCLOSURE TIMELINE & CREDIT')`

### 1.3 `visual=='root'` 分支（line 66–73）
- line 67 `text(im,(90,455),'uid=0',132,CYAN,True)` → 加圖例標籤，改為：
  ```
  text(im,(90,455),'uid=0',132,CYAN,True)
  text(im,(94,627),'LEGEND 圖例（非原始輸出）',28,GOLD)
  ```
- line 71 的證據橫條：改用 `post_exploit_state_REDACTED.txt` 的完整行
  `uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0`，**放大到可讀**（現為 66px 高，太小）。
  建議把 `photo(...)` 換成直接 `block()` 印出下列文字：
  ```
  # id
  uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
  # getenforce
  Permissive
  ```

### 1.4 `visual=='baseline'` 分支（line 78–83）
- line 81 `pair(im,699,'Bootloader：已解鎖','Bootloader unlocked on the tested device')`
  → 改 `pair(im,699,'Bootloader：已解鎖（環境條件，非前置條件）','Unlocked — environmental condition, not a precondition')`

### 1.5 `visual=='led'` 分支（line 95–99）—— 關鍵
- **刪除** line 96 標題「實機燈號 / PHYSICAL INDICATOR」、line 97 的 LED 照片、
  line 98 的「獨立素材」、line 99 的「控制展示」。
- 改為純文字卡：
  ```
  pair(im,470,'無硬體閃光燈','No hardware flash on this device')
  pair(im,700,'白光 LED = 相機使用中指示燈','White LED = camera-in-use indicator')
  pair(im,930,'已循協調揭露通報原廠','Reported via coordinated disclosure')
  block(im,(88,1200),'細節待揭露期後公開。',30,GOLD,width=920)
  ```

### 1.6 `visual=='ai'` 分支（line 100–109）
- line 101 `text(im,(86,455),'~99%',160,CYAN,True)` → 改 `text(im,(86,455),'AI-ASSISTED',80,CYAN,True)`
- line 102 標註 `'研究者自述估計 / SELF-REPORTED ESTIMATE'` → `'人機分工 / HUMAN + AI DIVISION'`

### 1.7 `visual=='end'` 分支（line 122–131）
- line 123–126 的「2026.09.01 修補韌體發布」區塊，改為揭露時間線：
  ```
  text(im,(86,465),'2026-11-23',88,CYAN,True)
  text(im,(90,593),'協調揭露日（或經協調）',40)
  block(im,(90,660),'初始報告 2026-08-25｜修補韌體 2026-09-01｜廠商確認 2026-09-06',30,GOLD,width=900)
  block(im,(90,748),'定位：驗證既有 CVE-2026-43499，非新漏洞。',34,width=890)
  ```
- line 128–129 保留研究聲明，line 130 署名依 §9。

---

## 2. `story.json` 改動（哪個 id 的哪個欄位）

> `story.json` 每個場景有 `pairs`（三句，逐句上字幕）與合併後的 `zh`/`en`（合成旁白用）。
> 兩處都要改，**`pairs` 拆句要與 `zh`/`en` 合併句一致**。

| id | 改 `pairs` | 改 `zh` / `en` |
|---|---|---|
| 2 | 第 2 句加「環境條件，非前置條件」 | 同左 |
| 3 | 第 1 句加 CVE 編號；第 3 句改「驗證既有漏洞、非新漏洞」 | 同左 |
| 7 | 三句全換為「無閃光燈／白光指示燈／已通報」 | 同左 |
| 8 | 第 1 句改「AI 協作」；刪「99%」 | 同左 |
| 11 | 第 1 句換揭露時間線；第 2 句加「非新 CVE」 | 同左 |
| 12 | 第 1 句加「驗證既有 CVE」；第 3 句加「環境條件」 | 同左 |
| 13 | 第 2、3 句換為「無閃光燈／已通報」 | 同左 |
| 14 | 第 1 句改「AI 協作」；刪「99%」 | 同左 |
| 15 | 第 1 句換揭露時間線；第 2 句加「非新 CVE」 | 同左 |

> 完整逐字內容直接抄 `docs/RV101_NARRATION_v2.md`，此處不重複。

---

## 3. 時間軸（兩個方案）

### 方案 A（建議，私人審閱版）—— 不刪場景，只改內容
- 180s 仍 12 場景 × 15s；60s 仍 4 場景 × 15s。**時間軸不變**，只需重渲內容。
- Scene 7/13 改成「機制事實卡」（§1.5），不示範控制。

### 方案 B（公開版最保守）—— 刪掉指示燈場景
- 180s 刪 id 7 → 剩 11 場景。兩種補法：
  1. 把 id 4（SELinux 兩狀態）與 id 5（自製 App）各延長 7.5s（15→22.5s）。
  2. 或新增一張「修補比對」（remove_waiter 相對偏移 before/after）卡補 15s。
- 60s 刪 id 13 → 剩 3 場景。把 id 12 延長 5s、id 14 延長 5s、id 15 延長 5s（各 15→20s）。
- 刪場景後 `titles`、`scenes` 陣列與 `timings.json` 都要同步重排。

---

## 4. 素材替換對照

| 位置 | v1 用的 | v2 改為 |
|---|---|---|
| root 場景證據 | `frames/root-best.png`（1000072772.mp4 00:05.6）+ `01-1000073132.jpg`（皆未打包，無法複驗） | `RV101_Disclosure/13_Redacted_Public_Materials/post_exploit_state_REDACTED.txt` 的 `uid=0(root)...` 完整行 |
| led 場景 | `下指令後常亮起綠光.jpg`、`放進盒子內也亮起綠光.jpg`、`展示閃爍白紅藍綠四色光.mp4` | **全部移除**；改純文字機制卡 |
| 結尾卡 | 「2026.09.01 修補韌體發布」 | 揭露時間線＋「非新 CVE」 |

---

## 5. 重渲後驗證（交付前必做）

- [ ] 重新 grep PII（v2 §7.3），零命中。
- [ ] 打開成品確認 root 證據顯示 `uid=0(root) ... u:r:kernel:s0`，**不是** `uid=10053`。
- [ ] Scene 7/13 已無任何 LED「控制／熄燈／常亮」照片。
- [ ] 「~99%」已不在任何畫面與字幕。
- [ ] 「非新 CVE」定位句至少出現 2 次（180s）、1 次（60s）。
- [ ] 結尾卡揭露時間線四日期正確（08-25 / 09-01 / 09-06 / 11-23）。
- [ ] 署名與 header/footer 一致（§9 定稿後）。
