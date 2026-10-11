# Follow-up Template — Vendor Disputes the Issue

**Subject:** Re: [ticket#] Additional evidence — Rokid RV101 local privilege escalation

Dear [Name],

Thank you for your review. I want to make sure we are looking at the same evidence, so I
have summarized the reproducible facts below. All of the following are captured in the
provided logs and can be independently re-run on firmware 1.22.009-20260710-150201:

1. **Initial state:** ADB shell at `uid=2000(shell)` with SELinux `Enforcing`.
2. **Post-exploitation state:** `id` returns `uid=0(root) gid=0(root) groups=0(root)`
   with SELinux context `u:r:kernel:s0`; `getenforce` returns `Permissive`; the process
   capability mask is `000001ffffffffff` (full).
3. **Demonstration:** the process created `/data/root_proof` (root-owned) and read
   root-only filesystem paths, confirming real uid 0, not just a label change.

If you believe the demonstration is not valid or is attributable to a different root
cause, I would welcome your specific technical explanation or any tests you would like
me to run. I am providing the confidential reproduction materials precisely so your
engineers can confirm the result on their own lab unit.

I am committed to reaching an accurate, evidence-based conclusion and remain available
for a technical call or further detail.

Best regards,
[Researcher]
