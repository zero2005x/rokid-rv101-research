# RV101 Editor Re-Render Checklist v2 (Precise to File and Line Number)

> 中文版：[RV101_RERENDER_SPEC_v2.md](RV101_RERENDER_SPEC_v2.md)

> Target: `design.py` and `story.json` under `RV101_Edit_Project（**external editing project, not in this workspace**）/production/`.
> Output: re-run `design.py` (regenerating `assets/scene_00..15.png`) → `render.py` (re-compositing the MP4).
> Corresponding narration: `docs/RV101_NARRATION_v2.md`.

---

## 0. Overview: Which Scenes Need Changes

| scene | 180s/60s | Action | Reason |
|---|---|---|---|
| 0, 1, 4, 5, 6, 9, 10 | 180s | No change | Already acceptable |
| 2 | 180s | Change | bootloader caveat |
| 3 | 180s | Change | the "not a new CVE" positioning |
| 7 | 180s | Change (or delete) | RS-8 embargo |
| 8 | 180s | Change | "~99%" → "AI collaboration" |
| 11 | 180s | Change | disclosure timeline + positioning |
| 12 | 60s | Change | bootloader caveat + positioning |
| 13 | 60s | Change (or delete) | RS-8 embargo |
| 14 | 60s | Change | "~99%" → "AI collaboration" |
| 15 | 60s | Change | disclosure timeline + positioning |

---

## 1. `design.py` Changes (follow the line numbers, re-run after editing)

### 1.1 Credit (touch only if §9 changes the real name; otherwise skip)
- line 44 `text(im,(105,64),'MAZUDA PAYRUNG',28,FG,True)` → change the name
- line 52 `text(im,(72,1851),'mazuda payrung  ·  繁中 / EN',23,MUTED)` → change the name
- line 130 `text(im,(88,1245),'mazuda payrung',63,FG,True)` → change the name

### 1.2 `titles` array (line 54–62): update the titles
- Index 2 → `('本地提權的起點','AUTHORIZED ADB SHELL / ENV CONDITION')`
- Index 3 → `('成果，驗證既有 CVE','GHOSTLOCK / CVE-2026-43499')`
- Index 7 → `('拍攝指示燈機制','INDICATOR MECHANISM — NO FLASH')`
- Index 8 → `('AI 協作開發','AI-ASSISTED RESEARCH')`
- Index 11 → `('通報、修補與署名','DISCLOSURE TIMELINE & CREDIT')`
- Index 12 → `('RV101 研究成果','EXISTING CVE VALIDATION')`
- Index 13 → `('裝置、App 與指示燈','DEVICE / APP / INDICATOR')`
- Index 14 → `('AI 協作與研究限制','AI COLLABORATION / LIMITS')`
- Index 15 → `('通報、修補與署名','DISCLOSURE TIMELINE & CREDIT')`

### 1.3 `visual=='root'` branch (line 66–73)
- line 67 `text(im,(90,455),'uid=0',132,CYAN,True)` → add a legend label, changing it to:
  ```
  text(im,(90,455),'uid=0',132,CYAN,True)
  text(im,(94,627),'LEGEND 圖例（非原始輸出）',28,GOLD)
  ```
- The evidence bar on line 71: switch to the complete line from `post_exploit_state_REDACTED.txt`,
  `uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0`, **enlarged until readable** (it is currently 66px tall, too small).
  It is suggested to replace `photo(...)` with a direct `block()` that prints the following text:
  ```
  # id
  uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
  # getenforce
  Permissive
  ```

### 1.4 `visual=='baseline'` branch (line 78–83)
- line 81 `pair(im,699,'Bootloader：已解鎖','Bootloader unlocked on the tested device')`
  → change to `pair(im,699,'Bootloader：已解鎖（環境條件，非前置條件）','Unlocked — environmental condition, not a precondition')`

### 1.5 `visual=='led'` branch (line 95–99) — critical
- **Delete** the line 96 title 「實機燈號 / PHYSICAL INDICATOR」, the LED photos on line 97,
  the 「獨立素材」 on line 98, and the 「控制展示」 on line 99.
- Replace with a plain-text card:
  ```
  pair(im,470,'無硬體閃光燈','No hardware flash on this device')
  pair(im,700,'白光 LED = 相機使用中指示燈','White LED = camera-in-use indicator')
  pair(im,930,'已循協調揭露通報原廠','Reported via coordinated disclosure')
  block(im,(88,1200),'細節待揭露期後公開。',30,GOLD,width=920)
  ```

### 1.6 `visual=='ai'` branch (line 100–109)
- line 101 `text(im,(86,455),'~99%',160,CYAN,True)` → change to `text(im,(86,455),'AI-ASSISTED',80,CYAN,True)`
- line 102 annotation `'研究者自述估計 / SELF-REPORTED ESTIMATE'` → `'人機分工 / HUMAN + AI DIVISION'`

### 1.7 `visual=='end'` branch (line 122–131)
- The "2026.09.01 patch firmware release" block on lines 123–126 becomes a disclosure timeline:
  ```
  text(im,(86,465),'2026-11-23',88,CYAN,True)
  text(im,(90,593),'協調揭露日（或經協調）',40)
  block(im,(90,660),'初始報告 2026-08-25｜修補韌體 2026-09-01｜廠商確認 2026-09-06',30,GOLD,width=900)
  block(im,(90,748),'定位：驗證既有 CVE-2026-43499，非新漏洞。',34,width=890)
  ```
- Keep the research statement on lines 128–129; the credit on line 130 follows §9.

---

## 2. `story.json` Changes (which field of which id)

> In `story.json` each scene has `pairs` (three sentences, subtitled sentence by sentence) and the merged `zh`/`en` (used for the synthesized narration).
> Both places must be changed, and **the sentence split in `pairs` must match the merged sentences in `zh`/`en`**.

| id | Change `pairs` | Change `zh` / `en` |
|---|---|---|
| 2 | Add "environmental condition, not a precondition" to sentence 2 | Same as left |
| 3 | Add the CVE number to sentence 1; change sentence 3 to "validates an existing vulnerability, not a new one" | Same as left |
| 7 | Replace all three sentences with "no flash / white LED / reported" | Same as left |
| 8 | Change sentence 1 to "AI collaboration"; delete "99%" | Same as left |
| 11 | Replace sentence 1 with the disclosure timeline; add "not a new CVE" to sentence 2 | Same as left |
| 12 | Add "validates an existing CVE" to sentence 1; add "environmental condition" to sentence 3 | Same as left |
| 13 | Replace sentences 2 and 3 with "no flash / reported" | Same as left |
| 14 | Change sentence 1 to "AI collaboration"; delete "99%" | Same as left |
| 15 | Replace sentence 1 with the disclosure timeline; add "not a new CVE" to sentence 2 | Same as left |

> For the complete verbatim content, copy directly from `docs/RV101_NARRATION_v2.md`; it is not repeated here.

---

## 3. Timeline (Two Options)

### Option A (recommended, private review version) — delete no scenes, only change the content
- The 180s cut still has 12 scenes × 15s; the 60s cut still has 4 scenes × 15s. **The timeline is unchanged**; only the content needs re-rendering.
- Scenes 7/13 become "mechanism fact cards" (§1.5) and no longer demonstrate control.

### Option B (most conservative public version) — delete the indicator scenes
- Delete id 7 from the 180s cut → 11 scenes remain. Two ways to compensate:
  1. Extend id 4 (the two SELinux states) and id 5 (the self-built App) by 7.5s each (15→22.5s).
  2. Or add a "patch comparison" card (remove_waiter relative offset before/after) to fill 15s.
- Delete id 13 from the 60s cut → 3 scenes remain. Extend id 12 by 5s, id 14 by 5s, and id 15 by 5s (each 15→20s).
- After deleting scenes, the `titles` and `scenes` arrays and `timings.json` must all be re-ordered accordingly.

---

## 4. Asset Replacement Mapping

| Location | Used in v1 | Changed to in v2 |
|---|---|---|
| root scene evidence | `frames/root-best.png` (1000072772.mp4 00:05.6) + `01-1000073132.jpg` (neither packaged; cannot be re-verified) | the complete `uid=0(root)...` line from `RV101_Disclosure/13_Redacted_Public_Materials/post_exploit_state_REDACTED.txt` |
| led scene | `下指令後常亮起綠光.jpg`, `放進盒子內也亮起綠光.jpg`, `展示閃爍白紅藍綠四色光.mp4` | **all removed**; replaced by a plain-text mechanism card |
| Ending card | "2026.09.01 patch firmware release" | disclosure timeline + "not a new CVE" |

---

## 5. Post-Re-Render Verification (mandatory before delivery)

- [ ] Re-grep for PII (v2 §7.3); zero hits.
- [ ] Open the output and confirm the root evidence shows `uid=0(root) ... u:r:kernel:s0`, **not** `uid=10053`.
- [ ] Scenes 7/13 no longer contain any LED "control / off / steady-on" photos.
- [ ] "~99%" no longer appears in any frame or subtitle.
- [ ] The "not a new CVE" positioning line appears at least 2 times (180s) and 1 time (60s).
- [ ] The four dates in the ending card's disclosure timeline are correct (08-25 / 09-01 / 09-06 / 11-23).
- [ ] The credit matches the header/footer (after §9 is finalized).
