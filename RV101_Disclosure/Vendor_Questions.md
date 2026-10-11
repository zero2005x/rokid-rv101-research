# Vendor Questions — Rokid RV101 LPE

Questions to put to Rokid during coordination (and parallel questions the researcher
must resolve internally, marked [researcher]).

## A. Affected scope

1. Which RV101 firmware/build versions ship the vulnerable `5.10.209-perf` branch?
2. Is the vulnerability limited to this branch, or does it affect other Rokid products
   using the same Qualcomm `neo` / Linux 5.10 baseline?
3. When is the next scheduled OTA, and will it include the CVE-2026-43499 backport?

## B. Fix / remediation

4. Will Rokid backport mainline `3bfdc63936dd`, or adopt the equivalent Qualcomm
   Android-common backport?
5. What is the target fixed-firmware version and release date?
6. Will the fix be validated against this report's reproduction material?

## C. Independently fixable items (RS-1 … RS-7)

7. RS-1: Why does the `shell` user hold the `readtracefs` group (gid 3012), and can
   tracefs pointer exposure (`printk_formats`) be restricted?
8. RS-2/RS-3: Can `dmesg_restrict=1` and `unprivileged_bpf_disabled=1` be set?
9. RS-4: Why are `/dev/kgsl-3d0` and `/dev/i2c-1` world-accessible (0666)?
10. RS-5: Is `GateServiced` (root, TCP 0.0.0.0:8341) intended to be network-exposed?
11. RS-6: Is ADB intended to be persistently enabled, and are preloaded authorization
    keys present in production units?
12. RS-7: What is the OTA authentication design (hash, key storage)?

## D. Researcher-side items to resolve before finalizing CVSS [researcher]

13. Was the bootloader unlocked by the researcher, or shipped unlocked? (Determines
    whether "bootloader unlocked" is a product observation vs. a test artifact.)
14. How was the uid-2000 ADB shell obtained — researcher's own authorized key, or a
    preloaded/device key? (Defines the real preconditions.)
15. Does the chain work from an installed app (untrusted_app), or only from shell?
16. Does the chain require the `readtracefs` group, or is there an alternative KASLR
    leak (e.g., dmesg_restrict=0, kptr_restrict)?
17. Does the exploit survive reboot (persistence)? [Not demonstrated — do not claim.]

## E. Process

18. What secure channel / PGP key does Rokid provide for confidential material?
19. What is Rokid's coordinated-disclosure timeline preference?
20. Will Rokid provide a CVE/advisory reference for the RV101 backport?
