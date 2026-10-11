# Follow-up Template — Vendor States It Is Already CVE-2026-43499

**Subject:** Re: [ticket#] CVE-2026-43499 — RV101 affected-product confirmation request

Dear [Name],

Thank you — I agree that the primary root cause is the existing CVE-2026-43499, and I am
not requesting a new CVE for the kernel bug itself. Consistent with that, could you
please confirm the following for the RV101 specifically:

1. **Affected-product acknowledgement** — that the RV101 (firmware 1.22.009 and the
   underlying kernel branch) is affected by CVE-2026-43499.
2. **Fix details** — which firmware version(s) will contain the backport, and the
   expected release date.
3. **Advisory inclusion** — whether the RV101 will be listed in your security advisory
   or release notes for CVE-2026-43499.
4. **Researcher credit** — acknowledgement for reporting and validating exploitation of
   CVE-2026-43499 on the RV101.
5. **Separate review** — evaluation of the Rokid-specific conditions I flagged
   (e.g., tracefs pointer disclosure to the shell user, dmesg_restrict, exposed device
   nodes, the GateServiced root service), which may be independently fixable.

I am happy to provide any additional evidence for these items.

Best regards,
[Researcher]
