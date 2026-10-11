# 回信草稿 — 致 degang.xu@rokid.com（2026-09-06）

> 寄出前檢查：
> - [ ] 附件換成 `docs/ghostlock-before-after-redacted.md`（先補完所有 `[待補]`）。
> - [ ] 確認全篇無絕對 kernel 位址、無序列號、無武器化細節。
> - [ ] 確認 credit 名字與拼音一致（真名 `<RESEARCHER_NAME>` / <RESEARCHER_NAME>）。
> - [ ] 勿把隱私 LED / RS 項目與 GhostLock 綁在一起結案。

---

**Subject:** Re: Security Vulnerability Report – Local Privilege Escalation to Root on Rokid Glasses RV101

Dear Mr. Xu,

Thank you for your reply and for confirming receipt of my reports of 25 August and 4
September. I appreciate your team's transparency, and the delay in acknowledgement is
fully understood.

I am glad our findings align:

- Firmware `1.25.012-20260901-150201` includes the CVE-2026-43499 remediation.
- Firmware `1.22.009-20260710-150201` and earlier should be considered affected.

This matches my static analysis: the remediation corresponds to upstream commit
`3bfdc63` ("rtmutex: Use waiter::task instead of current in remove_waiter()").

Please find attached the redacted technical comparison. It contains:

1. Firmware/build identification for both versions, with image hashes.
2. The key control-flow difference in `remove_waiter()` between `1.22.009` and
   `1.25.012`. Absolute kernel addresses have been removed and replaced with
   function-relative offsets and symbolic call targets.
3. Redacted before/after privilege evidence (uid 2000 → uid 0, SELinux Enforcing →
   Permissive), without device-specific parameters.

As agreed, it contains no device-specific exploit source code, compiled exploit
binaries, kernel addresses, offsets, heap-reclamation parameters, or other weaponized
material. I understand and respect that this remains the scope of what I will share
publicly or with you.

For the acknowledgement, please credit me as **"<RESEARCHER_NAME>"（<RESEARCHER_NAME>）**. Could you let
me know where the credit would appear and, if possible, an approximate timeline?

Two points I would like to confirm:

1. Will the fix be mentioned in any release notes, even without a broad public
   advisory? A release-note mention would help affected users understand the reason to
   update, given that the security patch level remains unchanged.

2. My original report also listed several Rokid-specific hardening items (RS-1..RS-9),
   including a root service listening on `0.0.0.0:8341` and a software-controllable
   camera privacy indicator. Could you confirm whether these are tracked separately from
   GhostLock, and what their current remediation status is? I want to ensure they are not
   considered closed together with CVE-2026-43499.

I will keep my coordinated-disclosure date on or after **23 November 2026** (90 days
after my initial report), and I will share the sanitized educational write-up with you
for review before publication.

Thank you again for handling this responsibly. I remain available to assist with any
technical verification through a private channel.

Sincerely,
<RESEARCHER_NAME>（<RESEARCHER_NAME>）
<RESEARCHER_EMAIL>
