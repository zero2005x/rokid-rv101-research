# RV101 逐字旁白腳本 v2（華英雙語）

> English: [RV101_NARRATION_v2_EN.md](RV101_NARRATION_v2_EN.md)

> 對應 `VIDEO_PRODUCTION_PLAN_v2.md` §11。標記：`[原]`＝沿用 v1、`[改]`＝已改寫、`[新]`＝新增。
> 每場景約 15 秒。180s 用 id 0–11，60s 用 id 12–15。
> ⚠️ **署名**：以下用「mazuda payrung」為佔位，**尚未定稿**（見 v2 §9）。若改真名
> 「<RESEARCHER_NAME>（<RESEARCHER_NAME>）」，全片 16 格 header/footer 與結尾卡須一併同步。

---

## 一、180 秒版（id 0–11）

### Scene 0（0:00–0:15）ROOT / VERIFIED OUTPUT — [原]

ZH：這是 Rokid RV101 眼鏡的安全研究紀錄。原始影片中的終端，顯示程序取得 root 身分。本片整理實機成果、AI 協作，以及研究限制。

EN：This is a security research record of Rokid RV101 glasses. The original footage shows a process reporting root identity. This film covers the results, AI collaboration, and research limits.

---

### Scene 1（0:15–0:30）TEST ENVIRONMENT — [原]

ZH：研究紀錄中的裝置，執行 Android 12 與 Linux 5.10.209。實測韌體為 1.22.009。這些成果限於本次測試環境。

EN：The documented device runs Android twelve and Linux five point ten point two oh nine. The tested firmware is version one point twenty two point zero zero nine. These results apply to this test environment.

---

### Scene 2（0:30–0:45）STARTING POINT — [改]

ZH：起點是已授權的 ADB shell，UID 為兩千。Bootloader 已解鎖、fastboot 無法刷入 boot 映像——這是本次測試的環境條件，不是漏洞的前置條件。研究採用執行期間的本地提權。

EN：The starting point was an authorized ADB shell with user ID two thousand. The bootloader was unlocked and fastboot could not flash the boot image — these are test-environment conditions, not preconditions of the vulnerability. The research used runtime local privilege escalation.

---

### Scene 3（0:45–1:00）PRIVILEGE CHANGE / GhostLock — [改]

ZH：研究文件將這條提權路徑稱為 GhostLock，對應既有的 CVE-2026-43499。這裡保留原片中的 UID 零輸出，作為成果證據。本研究是驗證這個既有漏洞在 RV101 上的可利用性與修補狀態，並非發現新漏洞；公開版本隱去利用指令與記憶體位址。

EN：The research documents name this escalation path GhostLock, corresponding to the existing CVE-2026-43499. The original user ID zero output is retained here as evidence. This work verifies the exploitability and patch status of that existing vulnerability on the RV101 — it is not a newly discovered flaw — and the public version omits exploit commands and memory addresses.

> **[新] 關鍵定位句**：「本研究是驗證既有 CVE-2026-43499 的可利用性與修補狀態，並非發現新漏洞。」此句為揭露文件核心立場，必須保留。

---

### Scene 4（1:00–1:15）TWO DISTINCT STATES — [原]

ZH：取得 root，與 SELinux 轉為寬容模式，是兩個不同狀態。文件記錄兩者均已達成。寬容模式不等於移除 SELinux，也不代表突破所有硬體保護。

EN：Root identity and SELinux permissive mode are two distinct states. The documents report both outcomes. Permissive mode does not remove SELinux or establish that every hardware protection was defeated.

---

### Scene 5（1:15–1:30）CUSTOM APPLICATION — [原]

ZH：自製 App 整合了裝置連線、狀態查詢與操作介面。影片可見相機、相簿與燈號相關控制。介面回報仍需與實機證據交叉核對。

EN：A custom app brings device connection, status checks, and controls into one interface. The footage shows camera, album, and indicator controls. Interface status must still be checked against evidence from the device.

---

### Scene 6（1:30–1:45）DEVICE IN VIEW — [原]

ZH：實機素材同時記錄手機操作與眼鏡顯示介面。這讓研究成果有可觀看的操作脈絡。它不代表所有功能，都已完成獨立驗證。

EN：The device footage records phone interaction alongside the glasses display. This provides visible context for the research results. It does not mean every function has completed independent validation.

---

### Scene 7（1:45–2:00）INDICATOR（機制事實版）— [改]

ZH：裝置本身沒有硬體閃光燈。那顆白光 LED，是相機使用中的指示燈。相關的隱私與信任邊界研究，已循協調揭露通報原廠，細節待揭露期後再公開。

EN：The device has no hardware flash. The white LED is a camera-in-use indicator. Related privacy and trust-boundary research was reported through coordinated disclosure; details remain under embargo until the disclosure date.

> **[改] 說明**：v1 此處是「追查軟體控制鏈＋控制展示」（RS-8，未修補＋embargo）。v2 改成「機制事實＋已通報、細節待公開」，**不出現任何控制／熄燈／常亮照片**。公開版若想更保守，可整段刪除（見重渲清單方案 B）。

---

### Scene 8（2:00–2:15）AI COLLABORATION — [改]

ZH：研究由 AI 協作完成。人類負責研究設計、實機驗證，與協調揭露；AI agents 負責逆向分析、文件整理與腳本產出。文件呈現了迭代、交接與驗證的流程。

EN：The research was completed with AI collaboration. Humans handled research design, device validation, and coordinated disclosure; AI agents handled reverse engineering, documentation, and scripting. The documents record iteration, handoffs, and validation.

> **[改] 說明**：刪除「約 99%」。改為「分工描述」，不給具體比例。

---

### Scene 9（2:15–2:30）EVIDENCE & LIMITS — [原]

ZH：研究文件、原始影片與介面回報，各自扮演不同角色。本片沒有將文件內容，重製成假的終端實錄。也不主張已證明遠端攻擊，或一般 App 直接取得 root。

EN：Research documents, original footage, and interface status each play different roles. This film does not recreate document text as fake terminal footage. It does not establish a remote attack or direct root access from an ordinary app.

---

### Scene 10（2:30–2:45）TEMPORARY ROOT — [原]

ZH：成功率未達百分之百，裝置狀態會影響結果。重新開機後，需要再次提權。因此，這次成果是有前提與限制的執行期間 root。

EN：Success was not guaranteed, and device state affected the outcome. A reboot required another privilege escalation attempt. The demonstrated result is runtime root access, with specific preconditions and limitations.

---

### Scene 11（2:45–3:00）DISCLOSURE & CREDIT — [改]

ZH：協調揭露時間線：二〇二六年八月二十五日初始報告，九月一日推送修補韌體，九月六日廠商確認修補，揭露日為十一月二十三日。本研究是驗證既有 CVE-2026-43499，而非發現新漏洞。本研究於本人持有並獲授權的設備及帳號進行，僅供互通性與安全研究。署名，mazuda payrung。

EN：Coordinated disclosure timeline: initial report on August twenty-fifth, corrective firmware pushed September first, vendor confirmation September sixth, and a disclosure date of November twenty-third, twenty twenty-six. This work validates the existing CVE-2026-43499 rather than discovering a new one. It was conducted on owned and authorized equipment and accounts, for interoperability and security research. Research by mazuda payrung.

> **[改] 說明**：新增揭露時間線與「非新 CVE」定位。完整時間線細節放在畫面卡上（見 v2 §6），旁白只唸關鍵四點。

---

## 二、60 秒版（id 12–15）

### Scene 12（0:00–0:15）RV101 / RESEARCH HIGHLIGHTS — [改]

ZH：這是 Rokid RV101 的 root 研究成果，驗證的是既有 CVE-2026-43499。原始影片顯示 UID 零；起點是已授權的 ADB shell。裝置 Bootloader 已解鎖為環境條件，研究採用本地提權。

EN：This is a root research result on Rokid RV101 glasses, validating the existing CVE-2026-43499. The original footage shows user ID zero, starting from an authorized ADB shell. The bootloader was unlocked as a test-environment condition; the research used local privilege escalation.

---

### Scene 13（0:15–0:30）DEVICE / APP / INDICATOR — [改]

ZH：研究包含 root、SELinux 寬容模式與自製 App。裝置沒有硬體閃光燈，白光 LED 是相機使用中的指示燈。相關隱私信任邊界研究已通報原廠，細節待揭露期後公開。

EN：The research covers root, SELinux permissive mode, and a custom app. The device has no hardware flash; the white LED is a camera-in-use indicator. Related privacy trust-boundary research was reported to the vendor; details remain under embargo until disclosure.

> **[改] 說明**：刪除 v1 的「追查控制機制＋控制成果展示」。

---

### Scene 14（0:30–0:45）AI / LIMITS — [改]

ZH：研究由 AI 協作完成：人類負責設計、驗證與揭露，AI agents 負責逆向與文件。成功率未達百分之百，重開機需要再次提權。這是有明確限制的實機成果。

EN：The research was completed with AI collaboration: humans handled design, validation, and disclosure, while AI agents handled reverse engineering and documentation. Success was not guaranteed, and rebooting required another escalation attempt. These are device results with explicit limitations.

> **[改] 說明**：刪除「約 99%」。

---

### Scene 15（0:45–1:00）DISCLOSURE / CREDIT — [改]

ZH：Rokid 於九月一日推送修補韌體，九月六日確認修補；揭露日為十一月二十三日。本研究是驗證既有 CVE-2026-43499，而非發現新漏洞。本研究於本人持有並獲授權的設備及帳號進行，僅供互通性與安全研究。署名，mazuda payrung。

EN：Rokid pushed corrective firmware on September first and confirmed the fix on September sixth; the disclosure date is November twenty-third. This work validates the existing CVE-2026-43499 rather than discovering a new one. It was conducted on owned and authorized equipment and accounts, for interoperability and security research. Research by mazuda payrung.

---

## 三、必留關鍵句速查（改完旁白務必核對這些都在）

| 關鍵句 | 出現位置 |
|---|---|
| 「驗證既有 CVE-2026-43499 的可利用性與修補狀態，**非發現新漏洞**」 | 180s Scene 3、Scene 11；60s Scene 12、Scene 15 |
| 「初始報告 2026-08-25｜修補韌體 2026-09-01｜廠商確認 2026-09-06｜揭露日 2026-11-23」 | 180s Scene 11；60s Scene 15 |
| 「Bootloader 解鎖為環境條件，**非漏洞前置條件**」 | 180s Scene 2；60s Scene 12 |
| 「重開機需再次提權／成功率未達 100%」 | 180s Scene 10；60s Scene 14 |
| 「無硬體閃光燈；白光為 camera-in-use 指示燈」（不示範控制） | 180s Scene 7；60s Scene 13 |
