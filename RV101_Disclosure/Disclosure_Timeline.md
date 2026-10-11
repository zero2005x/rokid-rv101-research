# Disclosure Timeline — Rokid RV101 LPE (CVE-2026-43499)

> Unknown/future dates are marked "To be confirmed." Do not fabricate.

| Date (CST unless noted) | Event | Evidence |
|---|---|---|
| 2026-01-31 – 2026-02-03 | Initial research phase: enumerated known CVEs (BPF/KGSL/Binder/io_uring/Dirty Pipe, …); all software LPE paths failed. Bootloader observed unlocked; Magisk not active. | `reports/FINAL_STATUS_20260202.md` |
| 2026-02-19 | Earlier multi-issue vulnerability report drafted (broader scope; separate from this GhostLock report). Send status: **To be confirmed.** | `reports/email.md` |
| 2026-07-09 | CVE-2026-43499 (GhostLock) publicly disclosed upstream (AlmaLinux advisory). | public advisory |
| 2026-07-10 | Rokid OTA `1.22.009-20260710-150201` released. | OTA package `RVE01-ota-1.22.009-…zip` |
| 2026-08-12 | Device validation data collected (identity, security baseline, kernel runtime). | `assets/RV101_Validation/…20260812…` |
| 2026-08-14 | Firmware unpacked and reverse-analyzed; **KASLR leak established** via `/sys/kernel/tracing/printk_formats`. | `reports/REVERSE_ANALYSIS_1.22_009.md` |
| 2026-08-18 – 2026-08-20 | GhostLock exploit development (handoffs V2→V9): KASLR leak, mm leak, UAF trigger, pselect forgery, punch/walk to `rb_erase`. | `reports/GHOSTLOCK_HANDOFF_V*.md` |
| 2026-08-22 13:46 | **First clean root** (v9.59): `uid=0 gid=0`, SELinux Permissive. | `reports/ghostlock_runlogs/run_w2_clean_v959_134613.log` |
| 2026-08-22 14:25 | v9.60 clean demo run (root again). | `run_w2_demo_v960_142523.log` |
| 2026-08-22 14:32–14:39 | **Full before/after evidence capture** (v9.60): startup uid 2000 + `getenforce: Enforcing` → `uid=0(root) … context=u:r:kernel:s0`, `Permissive`, full caps, `/data/root_proof`. | `run_w2_v960_143215.log` |
| 2026-08-24 | Secondary finding validated: privacy-LED trust-boundary audit paths A/B/C (sysfs DAC override / `lights_ctrl` Binder txn#8·#9 / `vendor.rkd.camera.session_open` spoof → `init` "camera closed!") executed under root; no camera capture triggered. | `reports/led_validation/led_test.log` |
| To be confirmed | Initial vendor report sent (this package). | — |
| To be confirmed | Vendor acknowledgement / case reference. | — |
| To be confirmed | Technical exchanges / PoC transfer. | — |
| To be confirmed | Fix availability (backported kernel). | — |
| To be confirmed | Advisory / release note publication. | — |
| To be confirmed | Coordinated public disclosure. | — |

## Notes

- "First successful SELinux transition" and "first successful UID 0" are recorded on the
  same date because the exercised chain performs both in one run. If an earlier
  SELinux-only flip is found in the V2–V9 run logs, this table should be updated with
  that precise timestamp.
