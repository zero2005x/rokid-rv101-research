# Reply — To degang.xu@rokid.com (2026-09-16 version)

> 中文版：[reply-to-rokid-2026-09-16.md](reply-to-rokid-2026-09-16.md)

> **This file supersedes `docs/reply-to-rokid-2026-09-06.md`** (the old version does not cover the 9/16
> decisions).
> Status: **draft, not yet sent**.
>
> ## Pre-send checklist
> - [x] **Credit name finalised**: `Mazda Payrang` (pseudonym only).
> - [x] **Attachment fields filled in**: `1.22.009` OTA filename and SHA-256 (from `RV101_Disclosure/SHA256SUMS.txt`;
>       MD5 not retained), `1.25.012` kernel version string marked as not available (image not retained).
>       §3.1 was originally empty (5 instances of `[TBD]`) — **now replaced with the real disassembly**:
>       re-downloaded the 1.22.009 OTA from the vendor CDN (981,231,124 bytes, SHA-256 matching the
>       retained list) → `payload.bin` → `boot.img` → kernel Image → restored symbols → disassembled,
>       converted to function-relative offsets. The internal "pre-send checklist" section has been removed.
>       Incidental correction: the earlier statement that "the vulnerable version has no `waiter->task`
>       load" **is wrong** (`+0x160` has a `#0x30` load whose base is `x9`); the distinguishing feature in
>       §2/§4 has been changed to "look at the source of the `x20` at `+0x040`".
>       The **independent re-check of the 1.25.012 patched side** (same toolchain) has also been completed,
>       with its kernel version string and SHA-256 added: both versions' `remove_waiter()` are `0x28c` /
>       163 instructions, **differing byte-for-byte in only the one instruction at `+0x040`**. The
>       attachment no longer has any `not available`.
> - [x] **PDF regenerated** (`python3 scripts/gen_redacted_pdf.py`; 3 pages, verified that the hashes and
>       filenames were not line-wrapped or clipped).
>       Note: the layout differs from the 9/6 Chrome version; if you prefer the original style, you can
>       reprint it on Windows.
> - [x] **Signature finalised**: `Mazda Payrang` (consistent between credit and signature; only the
>       pseudonym is used throughout).
> - [ ] **Attachment**: attach `docs/ghostlock-before-after-redacted.pdf` (3 pages, 86 KB) when sending.
> - [ ] Attachment-format decision: it currently says "PDF primarily, `.md` may be attached separately" — if
>       only one format is to be attached, change the corresponding sentence.
> - [ ] Confirm the whole document has no absolute kernel addresses, no device serial numbers and no weaponization parameters.
> - [ ] Recipients: To `degang.xu@rokid.com`, **CC `security@rokid.com`**.
> - [ ] Keep the subject line in the original thread (Re: …); do not start a new thread.
> - [ ] Dates: the pre-review draft date is provisionally **2026-11-02**, the disclosure date **2026-11-23** (changeable).

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
