# 回信 — 致 degang.xu@rokid.com（2026-09-16 版）

> English: [reply-to-rokid-2026-09-16_EN.md](reply-to-rokid-2026-09-16_EN.md)

> **本檔取代 `docs/reply-to-rokid-2026-09-06.md`**（舊版未涵蓋 9/16 決策）。
> 狀態：**草稿，尚未寄出**。
>
> ## 寄出前檢查
> - [x] **credit 名字已定案**：`Mazda Payrang`（只用化名）。
> - [x] **附件欄位已補**：`1.22.009` OTA 檔名與 SHA-256（取自 `RV101_Disclosure/SHA256SUMS.txt`；
>       MD5 未留存）、`1.25.012` kernel 版本字串標為 not available（映像未留存）。
>       §3.1 原本是空的（5 處 `[待補]`）——**已改為真實反組譯**：重新自原廠 CDN 下載 1.22.009
>       OTA（981,231,124 bytes，SHA-256 與留存清單一致）→ `payload.bin` → `boot.img` → kernel Image
>       → 還原符號 → 反組譯，換算為函式相對偏移。已移除內部的「寄出前檢查清單」章節。
>       附帶更正：原本「漏洞版無 `waiter->task` 載入」的說法**是錯的**（`+0x160` 有一處基底為 `x9` 的
>       `#0x30` 載入），§2／§4 的判別特徵已改為「看 `+0x040` 的 `x20` 來源」。
>       另已完成 **1.25.012 修補側獨立複核**（同套工具鏈）＋補上其 kernel 版本字串與 SHA-256：
>       兩版 `remove_waiter()` 同為 `0x28c`／163 條指令，**逐位元組只差 `+0x040` 一條指令**。附件已無 `not available`。
> - [x] **PDF 已重新產生**（`python3 scripts/gen_redacted_pdf.py`；3 頁、已驗證雜湊與檔名未被折行／裁切）。
>       註：排版與 9/6 的 Chrome 版不同；若偏好原風格，可在 Windows 重新列印。
> - [x] **簽名已定案**：`Mazda Payrang`（credit 與簽名一致，全篇只用化名）。
> - [ ] **附件**：寄出時附上 `docs/ghostlock-before-after-redacted.pdf`（3 頁、86 KB）。
> - [ ] 附件格式決策：目前寫「PDF 為主、`.md` 可另附」——若要只附一種，改掉對應句子。
> - [ ] 確認全篇無絕對 kernel 位址、無裝置序號、無武器化參數。
> - [ ] 收件：To `degang.xu@rokid.com`、**CC `security@rokid.com`**。
> - [ ] 主旨沿用原討論串（Re: …），不要另開新串。
> - [ ] 日期：預審草稿日暫填 **2026-11-02**、揭露日 **2026-11-23**（可改）。

---

**To:** degang.xu@rokid.com
**Cc:** security@rokid.com
**Subject:** Re: Security Vulnerability Report – Local Privilege Escalation to Root on Rokid Glasses RV101

Dear Mr. Xu,

Thank you for your reply of 6 September and for confirming receipt of both my original report
of 25 August and my follow-up of 4 September. My apologies for the delay in responding — I
wanted to complete the verification work below before replying.

**Areas of agreement**

I am glad our conclusions align:

- Firmware `1.25.012-20260901-150201` intentionally includes the remediation for
  CVE-2026-43499.
- Firmware `1.22.009-20260710-150201` and all earlier RV101 firmware versions should be
  considered affected.

This matches my own static analysis of the update package delivered to my test unit on
1 September: the relevant change corresponds to the upstream fix `3bfdc63936dd`
("rtmutex: Use waiter::task instead of current in remove_waiter()").

**Attachment**

Please find attached the redacted technical comparison, as requested. It contains:

1. Firmware/build identification for both versions, with package hashes and kernel
   version strings.
2. The control-flow difference in `remove_waiter()` between `1.22.009` and `1.25.012`.
   Both builds were analysed with the same toolchain; the two functions are the same size
   (`0x28c`) and differ in exactly one 4-byte instruction (`+0x040`). Absolute kernel
   addresses have been removed and replaced with function-relative offsets and symbolic
   call targets.
3. Redacted before/after privilege evidence (uid 2000 → uid 0; SELinux Enforcing →
   Permissive), without device-specific parameters.

It contains no device-specific exploit source code, no compiled exploit binaries, no
absolute kernel addresses, no heap-reclamation parameters, and no other weaponized
material. This remains the boundary of what I will share with you or publish. The
attachment is provided **for internal remediation-verification purposes only**; please do
not publish or redistribute it outside Rokid.

**Requests**

I would be grateful for a response on the following points. They are numbered so they can
be answered individually.

1. **Case or ticket reference.** My earlier requests for a case identifier have not yet
   been answered. Could you provide one so this report can be tracked formally?

2. **Release notes / support documentation.** I understand and respect your assessment that
   a broad public advisory is not warranted. However, because the Android security patch
   level is **unchanged** between `1.22.009` (SPL `2024-07-05`) and `1.25.012`
   (SPL `2024-07-05`), users and administrators cannot determine patch status from the SPL
   alone. I would therefore ask whether Rokid can:

   a. mention the fix in the release notes or a support/KB article for `1.25.012`, with the
      CVE identifier and the affected/fixed version mapping; and
   b. confirm that I may **quote your confirmation** (that `1.25.012` fixes CVE-2026-43499
      and that earlier versions are affected) in my write-up.

3. **Distribution scope.** For the accuracy of my write-up, could you confirm:

   a. whether `1.25.012` is being distributed to **all** RV101 units, in all regions; and
   b. whether any **other Rokid products share the same kernel generation/branch** and
      should therefore be considered affected?

4. **Independent verification.** To avoid relying on version strings alone, I produced the
   instruction-level comparison in the attachment from the OTA package for `1.22.009` as
   published on Rokid's OTA CDN (its SHA-256 is recorded in §1.1 of the attachment). Could
   you confirm that packages published there are the authoritative reference for this kind
   of verification, and whether the same applies to `1.25.012`? I am happy to share the
   result of the patched-side verification with you privately.

**Second matter: Rokid-specific hardening items (RS-1 … RS-9)**

My original report also listed Rokid-specific items that are **independent of
CVE-2026-43499**. Your reply did not address them, so I would like to raise them formally
as a separate item and ask that they not be closed together with the GhostLock case:

5. Could these be **tracked under a separate case**, with a **named technical contact** on
   your side?

6. In my assessment, the two with the most direct user impact are (i) the camera-in-use
   privacy indicator being fully software-controllable, and (ii) the wearing/proximity state
   being forgeable. I am not including technical detail in plain e-mail; I will provide it
   over a secure channel.

7. **Secure channel.** Could you provide a PGP public key or a secure submission channel?
   Further material relating to RS-1 … RS-9 will be sent only over such a channel. The
   attachment to this message is already redacted and non-weaponized, which is why I am
   sending it in the present thread without waiting.

**Credit and recognition**

8. **Credit.** For any public acknowledgement, please credit me as
   **Mazda Payrang**, and please do not include the name used in my earlier messages in this
   thread in any public communication. Could you confirm where the credit will appear
   (release notes, security acknowledgements page, or other documentation) and an
   approximate timeline?

9. **Written acknowledgement.** I understand and respect that Rokid does not operate a
   bounty programme, and I am not requesting one. If Rokid is open to it, I would value a
   brief **signed letter of acknowledgement on company letterhead**, issued by a
   manager-level representative, confirming my report and validation of CVE-2026-43499 on
   the RV101. I would use it in a professional portfolio. I am happy to cover printing and
   postage costs; if a physical letter is impractical, a signed PDF would also serve the
   purpose.

10. Beyond the above, please let me know what form of recognition Rokid considers
    appropriate. Credit in your security documentation and the letter described in point 9
    are what I would value most.

**Publication plan and timing**

11. I am preparing a **sanitized educational write-up** covering the affected and fixed
    firmware versions, the high-level vulnerability mechanics, the patch-verification
    methodology, and redacted before/after privilege evidence. It will **not** contain
    device-specific exploit code, compiled binaries, absolute kernel addresses, offsets, or
    heap-reclamation parameters.

12. I will send you the draft for review **by 2 November 2026**. If I do not receive
    comments within **ten business days**, I will consider the draft cleared. Within the
    write-up, the technical facts — the CVE identifier, the affected/fixed version mapping,
    the technical description, and the redacted before/after evidence — are not negotiable;
    scheduling and wording are open to discussion.

13. My coordinated-disclosure date remains **on or after 23 November 2026**. I remain
    willing to adjust it if Rokid provides a remediation or communication schedule, but I
    should be clear that **the date is not contingent on the publication of an advisory**:
    if no advisory or release-note entry is issued, I will still publish on or after that
    date.

**Status on my side**

My test unit remains on the affected build `1.22.009-20260710-150201` so that the findings
stay reproducible, and I am continuing verification work on it. I have published nothing and
will publish nothing before the date above.

Thank you again for handling this responsibly. I remain available to assist with technical
verification through an appropriate private channel, and I am happy to answer any questions
about the attached material.

Sincerely,

Mazda Payrang
<RESEARCHER_EMAIL>
