# Revised Production Plan v2 — "Rokid RV101 Root Research Record"

> 中文版：[VIDEO_PRODUCTION_PLAN_v2.md](VIDEO_PRODUCTION_PLAN_v2.md)

> Version: v2 (private review version takes priority)｜2026-09-06
> Basis: `docs/Disclosure_Timeline.md`, `docs/ghostlock-before-after-redacted.md`,
> `RV101_Disclosure/00_README.md`, `RV101_Disclosure/CVE_Relationship_Assessment.md`,
> `RV101_Disclosure/13_Redacted_Public_Materials/post_exploit_state_REDACTED.txt`,
> `reports/RV101_PRIVACY_LED_TRUST_BOUNDARY_AUDIT.md`, `reports/RV101_CAMERA_LED_FLASH_MECHANISM.md`.
> This plan supersedes v1 (the ChatGPT version); wherever anything conflicts with the documents above, this document prevails.

---

## 0. One-Page Conclusion

v1's **process** (question → plan → approval → start work) is fine, but its **content**
crosses the red lines you set yourself in four places, one of which is a
responsible-disclosure-level violation. The core changes in v2:

1. **Remove the on-device "indicator light off / suppressed" demonstration** — the
   privacy LED (RS-8) is **unpatched and still under embargo**.
2. **This version is positioned as a "private review version"** — the public version is
   deferred until 2026-11-23 (the disclosure date you set) and until the write-up has
   first been sent to the vendor for review.
3. **Use the correct frame for the root evidence** — use the redacted
   `uid=2000 → uid=0(root)` version and discard the `uid=10053` App screenshot.
4. **Reword "99% AI" and "patch re-test complete"** — the former becomes "AI
   collaboration", the latter becomes "static comparison confirms the backport".

---

## 1. v1 → v2 Item-by-Item Comparison (follow this table when re-editing)

| # | v1 content | Problem | v2 correction |
|---|---|---|---|
| 1 | Storyboard includes the "on-device indicator demonstration" and "lights-off and trust boundary" | RS-8 unpatched + embargo; "lights off while running" is actually INFERRED (no end-to-end demonstration with a triggered capture) | Delete every "lights-off / suppression / stealth mode" frame and narration line; keep only the mechanism fact "no hardware flash, the white light is a camera-in-use indicator" |
| 2 | The self-built App's "stealth mode (LED suppression)" appears on screen | Equivalent to disclosing an unpatched capability | The App shows only legitimate functions (Bluetooth remote shutter / album management); stealth mode never appears on screen |
| 3 | The default was public upload to YouTube | The disclosure date 2026-11-23 has not arrived and the write-up has not been sent to the vendor for review | This version = private review version; the public version is unlocked separately (see §3) |
| 4 | The "uid=0(root) close-up" + that uid=10053 screenshot | The App's uid was used as root evidence | Switch to the redacted `uid=0(root) gid=0(root) context=u:r:kernel:s0` |
| 5 | "The researcher estimates about 99% of the work was done by AI agents" | No documentary support; an interview/review will press on the person's own contribution | "AI-collaborative development: humans handle research design, validation and coordinated disclosure" |
| 6 | "The researcher has completed patch re-testing" | The 1.22.009 half of the disassembly is still `[待補]` | "Per the researcher's static comparison, 1.25.012 is confirmed to contain the upstream 3bfdc63 backport" |
| 7 | The ending made no mention of the disclosure status | Missing a responsible-disclosure statement | Add to the ending: "initial report 2026-08-25, vendor confirmed the fix 2026-09-06, disclosure date 2026-11-23" |
| 8 | Credit "mazuda payrung" | Inconsistent with the credit given to the vendor, "<RESEARCHER_NAME>（<RESEARCHER_NAME>）" | See §9, awaiting your decision |

---

## 2. Positioning and Publication Strategy (most important)

- **Purpose of this version**: professor review, security job interviews, private
  collaboration with the vendor, or a personal research record. **No public upload.**
- **"Core content" that can be safely presented** (the affected vulnerability is patched
  and the evidence is redacted):
  1. Runtime privilege-escalation evidence for GhostLock (CVE-2026-43499).
  2. The research environment and preconditions.
  3. The legitimate functions of the self-built App.
  4. The AI-collaborative development workflow.
  5. The reporting and patch status.
- **Unlock conditions for the public version** (all must be met before YouTube etc.):
  1. The disclosure date **2026-11-23** has been reached (or a date coordinated with the
     vendor).
  2. The sanitized write-up has **first been sent to the vendor for review**.
  3. The privacy LED (RS-8) segment is confirmed **patched and out of embargo**, or that
     segment is permanently removed.
  4. The redaction checklist (§7) passes in full.

> ⚠️ Key judgement: GhostLock is patched, so publicly saying "validating an existing CVE"
> carries low risk; but **the privacy LED issue is not patched, and demonstrating it
> publicly would amount to prematurely releasing an unpatched privacy bypass**. This is
> the one segment of v1 that must be "taken out right now".

---

## 3. Delivery Specifications (unchanged from v1)

| Item | Specification |
|---|---|
| Versions | Mandarin 180s, Mandarin 60s, English 180s, English 60s, one MP4 each |
| Aspect ratio / resolution | 9:16 vertical, 1080×1920 |
| Narration | Taiwan Mandarin male-voice synthesis + English synthesis, calm technical-documentary style |
| Subtitles | Traditional Chinese + English subtitles (burned in) + bilingual SRT |
| Music | Low-volume background music |
| Title | 《Rokid RV101 Root 研究紀錄｜驗證既有 CVE 與實機驗證》 |

---

## 4. Full Storyboard (180 seconds, private review version)

| Time | Visual | Narration points (verbatim direction) | Evidence / cautions |
|---|---|---|---|
| 0:00–0:12 | glasses hardware, power-on / putting-them-on footage + title card | "Rokid RV101. Today we document a local privilege-escalation study on a device we own, and its relationship to CVE-2026-43499." | The title itself sets the "validating an existing CVE" positioning |
| 0:12–0:38 | terminal environment: `getprop`, `uname`, `adb shell` | "Research environment: Android 12 (SKQ1.240613.001), kernel 5.10.209-perf, security patch 2024-07-05. The initial privilege is the ADB shell's uid 2000." | **List the unlocked bootloader as an "environmental condition", and add the sentence "this is an environmental condition, not a precondition of this vulnerability"**; do not imply that unlocking is required in order to exploit it |
| 0:38–1:10 | redacted before/after freeze-frames: `getenforce`→`Enforcing`, `id`→`uid=0(root) … u:r:kernel:s0`, `getenforce`→`Permissive`, `CapEff 000001ffffffffff`, `/data/root_proof` | "Initial state SELinux Enforcing, uid 2000. After triggering, uid 0 is obtained, with full effective capabilities and SELinux Permissive. This is a runtime privilege escalation — it does not claim persistence across reboots and does not claim an AVB/dm-verity bypass." | Use only `post_exploit_state_REDACTED.txt` on screen; **do not use the uid=10053 App screenshot** |
| 1:10–1:40 | self-built App: phone-side Bluetooth remote control, glasses-side CameraX photo/video capture, album management | "During the research we also built a remote-capture system: the phone controls the glasses over Bluetooth RFCOMM for photos, video recording and album management, used to collect validation material." | **Stealth mode / LED suppression must not appear on screen** |
| 1:40–2:05 | documented research process: timeline, disassembly comparison of `remove_waiter()`, redacted comparison | "The whole process was completed with AI collaboration: humans handled research design, on-device validation and coordinated disclosure; AI agents handled reverse analysis, documentation and script output." | Delete "99%"; present a "division of labour" rather than a "ratio" |
| 2:05–2:30 | reporting timeline card: 8/25 initial report → 9/1 OTA → 9/6 vendor confirmation | "The vulnerability is CVE-2026-43499 'GhostLock', an rt-mutex futex-PI use-after-free. Affected firmware 1.22.009 and earlier; Rokid pushed the 1.25.012 fix on 2026-09-01 and confirmed the backport on 9/6. This work is positioned as 'validating the exploitability and patch status of an existing CVE on the RV101', not as discovering a new vulnerability." | The positioning sentence **must** appear |
| 2:30–3:00 | limitations list card + research statement + ending card | "Limitations: a reboot requires re-escalation, the success rate is below 100%, and validation covered only one device and one firmware. The following is the research statement." | See the ending card in §6 |

### 4.1 (optional, private version only) Privacy LED segment — always deleted from the public version

> If a professor/interviewer needs to see the depth of this research, a **controlled
> segment** may be inserted after 1:10; it may only explain the mechanism and must not
> demonstrate the attack:

| Time | Visual | Allowed to say | Forbidden |
|---|---|---|---|
| Within 0:45 | The white LED turning on/off normally (when the camera is switched) | "The RV101 has no hardware flash; that white LED is a camera-in-use indicator, driven by a software chain." | **Do not demonstrate lights-off / suppression**, do not mention the path A/B/C commands, do not mention sysfs/Binder/`session_open` details, and do not claim "lights off while running" is proven (it is INFERRED) |
| — | Annotation | "This mechanism is a privacy trust-boundary issue and has been reported to the vendor through coordinated disclosure; details will be made public after the disclosure period." | — |

---

## 5. Short Storyboard (60 seconds, private review version)

| Time | Content |
|---|---|
| 0:00–0:10 | root evidence freeze-frame (redacted uid 0 / Permissive) |
| 0:10–0:22 | research environment + preconditions (including bootloader = environmental condition) |
| 0:22–0:35 | legitimate functions of the self-built App (Bluetooth remote control / album) |
| 0:35–0:48 | AI collaboration + reporting and patching (CVE-2026-43499, 1.25.012 fix) |
| 0:48–0:52 | limitations (reboot requires re-escalation, success rate, single device) |
| 0:52–0:60 | ending card |

---

## 6. Ending Card Copy

### 6.1 Private review version (use this card now)

```
本研究於本人持有並獲授權之設備及帳號上進行，僅供安全研究與互通性測試。
漏洞：CVE-2026-43499「GhostLock」（Linux rt-mutex futex-PI use-after-free）→ 本地權限提升。
定位：於 Rokid RV101 驗證既有 CVE 之可利用性與修補狀態，非新 CVE、非獨立漏洞開發。
修補：Rokid 已於 2026-09-01 推送韌體 1.25.012-20260901-150201；
　　　依研究者靜態比對，確認含 upstream 3bfdc63 backport。
協調揭露：初始報告 2026-08-25｜廠商 2026-09-06 確認修補｜揭露日 2026-11-23（或經協調）。
署名：mazuda payrung｜AI 協作安全研究
```

### 6.2 Public version (add one line after unlocking)

```
本片僅呈現已修補之 CVE 內容；未修補項目依協調揭露原則不予公開。
```

---

## 7. Redaction Checklist (before re-editing and export, grep the whole text for these strings)

### 7.1 Absolutely forbidden (any version)

| Category | Specific strings / values |
|---|---|
| Device serial | `<DEVICE_SERIAL>` |
| Phone serial | `<PHONE_SERIAL>` |
| Real name (if a pen name is used) | `<RESEARCHER_NAME>`, `<RESEARCHER_NAME>` |
| Mailbox | `<RESEARCHER_EMAIL>` |
| Windows local paths | `C:\Users\<USER>\...`, `Documents\codebase\rokid_root` |
| kernel addresses | `0xffffffc0*`, KASLR base/slide, `printk_formats`, `init_cred`, `selinux_state` |
| Absolute vaddr anchors | `0xef670`, `0xef998`, `0x22b744`, `0x228fac`, `0x229308`, `0x88188` |
| exploit internals | `target_rv101.h`, FOPS install, SKB/reclaim, pselect/rb_erase punch, heap grooming, `ghostlock_rv101` (binary name) |
| Exact reproduction commands | `boot_run_loop.sh 4 w2 v960`, `GL_PURGE_CNT`, `GL_TAIL_KILLS`, `W2_ATTEMPTS`, `GL_DUMMY_KILLS` |
| Partition layout | the output of `ls -l /dev/block/by-name`, the complete enumeration of `/dev/block/mmcblk0p*` |
| Keys | OTA keys, ADB keys, any third-party secret in any log |

### 7.2 Allowed (the redacted version can be used directly)

- The `id` / `id -Z` / `getenforce` / `Uid/Gid/CapInh/CapPrm/CapEff/CapBnd` lines.
- The firmware/build fingerprint and the Android/kernel/SPL version strings.
- The statement that "system/vendor remain read-only".
- The non-destructive demonstration: creating `/data/root_proof`.
- The CVE-2026-43499 reference and the "affected / patched" relationship.
- **Relative** function offsets + symbol names (such as `+0x040 ldr x20,[x23,#0x30]` for `remove_waiter`) — **relative offsets are acceptable, absolute addresses are not**.

### 7.3 Automatic check before export (treat the following grep as a hard gate)

```bash
grep -RInE '<DEVICE_SERIAL>|<PHONE_SERIAL>|<RESEARCHER_HANDLE>|<USER>|0xffffffc0|0xef[0-9a-f]{3}|0x22b744|0x228fac|0x229308|0x88188|target_rv101|ghostlock_rv101|GL_PURGE_CNT|W2_ATTEMPTS' 剪輯專案/腳本/字幕/
```

Any hit → redact or delete it before exporting.

---

## 8. Asset Inventory (what can and cannot be used)

| Asset | Usable? | Purpose / restriction |
|---|---|---|
| glasses hardware, power-on, putting them on | ✅ | Opening |
| terminal before/after (redacted) | ✅ | main root-evidence frame |
| original `run_w2_v960_143215.log` | ❌ public | contains addresses and exploit internals; view privately only, do not cut into the film |
| App Bluetooth remote-control / album frames | ✅ | demonstration of legitimate functions |
| App "stealth mode / LED suppression" frames | ❌ | unpatched, delete |
| white LED turning on normally (camera on) | ⚠️ private version only | show normal behaviour only; do not demonstrate lights-off |
| white LED "off / suppressed" demo | ❌ | unpatched + embargo, delete |
| the two light-effect clips in `wm_work/` | ⚠️ private version only | if they contain "lights-off" or path details, remove them from the public version |
| reporting emails / timeline | ✅ (de-identified) | hide the real name, mailbox and serial numbers |

---

## 9. Two Things Awaiting Your Decision (decide before re-editing)

1. **Credit**: which name should the public video use?
   - Option A (recommended): consistent with the vendor credit → **"<RESEARCHER_NAME>（<RESEARCHER_NAME>）"**, the most credible and matching Rokid's future public acknowledgement.
   - Option B: keep the pen name **"mazuda payrung"**, but **the real name/mailbox must not appear anywhere in the film**, and a line must be added to the ending — "the name in this film differs from the credit given to the vendor" — to avoid questions from reviewers. → I personally do not recommend this; an interviewer can easily shoot it down.
   - Option C: pen name + real name in parentheses (usable only in the private review version; keep only one of them in the public version).
2. **Should the privacy LED segment be kept in the "private version"?** Keep = it may only be shown privately to a professor/interviewer/the vendor; do not keep = most conservative, zero risk. The public version never keeps it.

---

## 10. Pre-Upload / Pre-Delivery Checklist (tick each item)

- [ ] The credit has been settled (§9-1) and no PII of the other identity appears anywhere in the film.
- [ ] All privacy-LED "lights-off / suppression" frames and narration have been removed (or confirmed to be private-version only).
- [ ] The root evidence comes only from the redacted version; no uid=10053 App screenshot is passed off as it.
- [ ] "99% AI" has been changed to "AI collaboration".
- [ ] "patch re-test" has been changed to "static comparison confirms the backport".
- [ ] The "validating an existing CVE, not a new vulnerability" positioning sentence is in the film.
- [ ] The bootloader is noted as = environmental condition, not a precondition.
- [ ] The four limitations (re-escalation after reboot / success rate / single device and single firmware / no persistence and no AVB bypass) have been stated.
- [ ] The §7.3 grep returns zero hits across the board.
- [ ] If this is the public version: the disclosure date has arrived, the write-up has been sent to the vendor for review, and RS-8 is patched or that segment has been removed.

---

## 11. Per-Scene Re-Edit Checklist for the Actual v1 Files (added 2026-09-06 after reading the actual artifacts)

> The artifacts under `video_picture_ori/` and
> `RV101_Edit_Project（**external editing project, not in this workspace**）/production/{story.json,design.py}`
> have been read directly.
> v1 consists of 16 scene cards (`scene_00..15`): the 180s cut uses id 0–11, the 60s cut uses id 12–15.
> "Change" = re-render that scene card + re-composite the voice-over and subtitles;
> "Delete" = drop that scene and re-time the rest.

### 11.1 Verified OK (no change needed)

- All four MP4s really exist and have the correct specifications: 1080×1920, 24fps,
  180s/60s (`exiftool` measured 3:00 / 1:00).
- PII grep over all text files (.srt/.ass/.md/.json/.py) returns **zero hits**; the source
  images' EXIF contains no GPS / serial number / author.
- The narration already uses conservative wording in several places: root and Permissive
  are kept separate, no claim of a remote attack or of privilege escalation from an
  ordinary App, no re-creation of a fake terminal, the patch is not independently
  verified, and the limitations are listed.

### 11.2 Must change (red lines)

| Scene | Current state (`design.py`/`story.json`) | Problem | How to change |
|---|---|---|---|
| **id 7 (180s) / id 13 (60s) "indicator research"** | `visual="led"`, with 「實機燈號 / PHYSICAL INDICATOR」 burned in + LED control photos (sourced from `下指令後常亮起綠光.jpg`, `放進盒子內也亮起綠光.jpg`, `展示閃爍白紅藍綠四色光.mp4`) | Demonstrating "software control of the indicator" = RS-8, **unpatched + embargo** | **Delete that scene**, or turn it into a pure mechanism-explanation card ("no hardware flash; the white light is a camera-in-use indicator"), with **no control / lights-off / steady-on photos at all** |
| **id 8 (180s) / id 14 (60s) "AI collaboration"** | `text(im,(86,455),'~99%',160,...)`, a huge "~99%" | The specific ratio has no documentary support | Change "~99%" to "AI-ASSISTED / AI collaboration" and delete "ninety-nine percent" from the narration, replacing it with "AI agents took part in the research and development collaboration" |

### 11.3 Suggested changes (accuracy)

| Scene | Current state | Problem | How to change |
|---|---|---|---|
| **id 0/3/12 "root"** | The huge "uid=0" is a **rendered graphic glyph** in `design.py` (not evidence); the real evidence is only the 66px-tall cropped bar `(96,820,888,66)` | The large "uid=0" is easily misread as evidence | Label the large graphic glyph as "LEGEND / legend"; for the real evidence use the complete, readable `uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0` from `post_exploit_state_REDACTED.txt` |
| **id 3 "GhostLock"** | Only says "the research documents name this escalation path GhostLock" | **The film lacks the "validating existing CVE-2026-43499, not a new vulnerability" positioning sentence** | Add a narration line + card: "This work validates the exploitability and patch status of the existing CVE-2026-43499 on the RV101; it is not the discovery of a new CVE" |
| **id 2 "starting point"** | "Bootloader unlocked" with no caveat | Easily misread as a precondition of the vulnerability | Add "the unlocked bootloader is a test-environment condition, not a precondition of this vulnerability" |
| **id 11/15 "ending card"** | Only "2026.09.01 patch firmware release" + credit | Missing the disclosure timeline | Add "initial report 2026-08-25｜vendor confirmed the fix 2026-09-06｜disclosure date 2026-11-23"; also add "not a new CVE" |
| **header/footer of all 16 scenes** | `design.py` burns "MAZUDA PAYRUNG" into **every** header cell (line 44) + footer (line 52) + ending card (line 130) | Once the credit decision (§9) changes, all 16 cards must be re-rendered | Settle the credit (§9) before re-rendering; if the real name is used, replace all 16 cells |

### 11.4 One thing you should check yourself before delivery

The root scene in `design.py` actually uses `frames/root-best.png` (taken from
1000072772.mp4 00:05.6) and `01-1000073132.jpg` (both are **render intermediates that
were not packaged with the delivery zip**). I cannot verify the content of these two
images. Please open the artifacts and confirm:

- [ ] The "root evidence" in the film shows **`uid=0(root) gid=0(root) ... u:r:kernel:s0`**,
      and **not** the App's **`uid=10053`** (ChatGPT's first screenshot was uid=10053, and
      that one cannot serve as root evidence).
- [ ] If it shows uid=10053, re-capture the evidence using
      `取得最高權限_root_0_selinux_pervassive.jpg` or
      `RV101_Disclosure/13_Redacted_Public_Materials/post_exploit_state_REDACTED.txt`.
