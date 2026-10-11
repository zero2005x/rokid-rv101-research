# HITCON ZeroDay Version Disclosure Timeline

> 中文版：[HITCON_Disclosure_Timeline.md](HITCON_Disclosure_Timeline.md)

> Timezone: Asia/Taipei (UTC+8). Unknown dates must not be inferred.

| Date | Event | Evidence / Notes |
|---|---|---|
| 2026-07-09 | Public upstream information for CVE-2026-43499 already exists | Public advisory; this case is therefore not an undisclosed root cause |
| 2026-07-10 | Date marker of the tested Rokid firmware `1.22.009-20260710-150201` | Build fingerprint / OTA metadata |
| 2026-08-12 23:07 | Collected device identity, security baseline, kernel runtime | Original `RV101_Disclosure/02_Device_Information/` |
| 2026-08-22 13:46 | First clean uid 0 / SELinux Permissive successful run | Private raw log v9.59 |
| 2026-08-22 14:25 | v9.60 clean demo succeeded again | Private raw log |
| 2026-08-22 14:32–14:39 | Complete BEFORE→AFTER and non-destructive evidence | Authoritative frozen raw log; the platform version is the derived A02 material |
| 2026-08-22 17:21 | Transport interrupted during a live ADB supplementary collection attempt | No new baseline produced; see internal record |
| 2026-08-22 | HITCON ZeroDay draft package created | This directory; not yet sent |
| To be confirmed | Send Rokid initial disclosure | Date, Message-ID / ticket to be filled in |
| To be confirmed | Send HITCON pre-submission inquiry | Date and reply to be filled in |
| To be confirmed | Formal HITCON platform submission | HZD code to be filled in |
| To be confirmed | HITCON review / organization notification | Status and date to be filled in |
| To be confirmed | Private PoC delivery | Record only the recipient, method, and hash; do not paste content here |
| To be confirmed | Fixed firmware release | Version and release date to be filled in |
| To be confirmed | Researcher retest | Result and evidence hash to be filled in |
| To be confirmed | Coordinated publication | Must be confirmed with Rokid / HITCON |
