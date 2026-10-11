# Initial Disclosure Email

**To:** security@rokid.com
**From:** [Researcher name / alias] <[contact email]>
**Subject:** Security Vulnerability Report – Local Privilege Escalation to Root on Rokid Glasses RV101
**Attachment:** None (reproduction evidence available privately on request)

---

Dear Rokid Security Team,

I am an independent security researcher. During authorized testing of a personally
owned Rokid Glasses RV101, I reproduced a **local privilege-escalation to root** and
would like to report it responsibly for your review and remediation.

**Affected device**

- Model: Rokid Glasses RV101 (`RG-glasses`)
- Serial: <DEVICE_SERIAL>
- Firmware/build: 1.22.009-20260710-150201
- Android: 12 (SKQ1.240613.001, user/release-keys)
- Kernel: 5.10.209-perf-gd908d71a0404-ab1.22.009-20260710-150201
- Android security patch level: 2024-07-05

**Technical summary**

Starting from an ADB shell running as the low-privileged `shell` user (uid 2000) with
SELinux Enforcing, a local exploit chain obtains a working root shell — uid 0, full
effective Linux capabilities, and SELinux set to Permissive.

The exercised vulnerability is **CVE-2026-43499** ("GhostLock"), the upstream Linux
kernel rt-mutex futex-PI use-after-free. The RV101 kernel appears to lack the upstream
fix, so I am reporting this as an **affected-product confirmation / exploit validation
of CVE-2026-43499** rather than a new CVE.

**Initial vs. resulting privilege**

- Before: uid=2000 (shell), context `u:r:shell:s0`, SELinux Enforcing
- After: uid=0 (root), context `u:r:kernel:s0`, SELinux Permissive, full capabilities

**Security impact**

Runtime root on the affected firmware. I did **not** demonstrate persistence, boot-chain
compromise, or a verified-boot (AVB/dm-verity) bypass; system/vendor partitions remained
read-only.

**Reproduction material**

I have a reproducible demonstration, including logs showing the before/after state and a
confidential technical appendix with reproduction detail. These are available privately
on request and will not be shared publicly.

**What I am requesting**

1. Acknowledgement of this report and a case/ticket reference.
2. Confirmation of whether the RV101 is affected and of affected firmware versions.
3. Coordination on a patch (expected: backport of the upstream CVE-2026-43499 fix).
4. A security advisory or release note, as appropriate.
5. Researcher acknowledgement for reporting and validating this issue on RV101.
6. Separate evaluation of any Rokid-specific weaknesses I observed during research that
   may be independently fixable (I will provide details under coordination).

**Coordinated disclosure**

I intend to follow a standard 90-day coordinated-disclosure window (negotiable) and will
not publish any detail before you release remediation. I am happy to provide the
confidential materials, answer questions, and assist with verification.

Thank you — please let me know the best secure channel for the confidential appendix
(e.g., PGP key if available).

Sincerely,
[Researcher name / alias]
[contact email]
[date — to be filled at send time]
