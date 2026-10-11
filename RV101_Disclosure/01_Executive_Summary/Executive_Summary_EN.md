# Executive Summary — Rokid Glasses RV101 Local Privilege Escalation

**Classification:** Restricted (coordinated disclosure) · **Date:** 2026-08-22 (updated 2026-08-24) · **Status:** Draft, not yet submitted

## Finding

A local privilege-escalation chain was demonstrated on a personally owned Rokid Glasses
RV101 (firmware `1.22.009-20260710-150201`). Starting from an Android ADB shell running
as the low-privileged `shell` user (uid 2000) with SELinux Enforcing, the chain obtains a
**working uid 0 root shell** with **full effective Linux capabilities** and
**SELinux Permissive**.

The exercised vulnerability is **CVE-2026-43499 ("GhostLock")**, an existing upstream
Linux-kernel use-after-free in the futex PI / rt-mutex path. This report documents its
**exploitability on the RV101**; it does **not** claim a new CVE.

## Key facts

| Field | Value |
|---|---|
| Product | Rokid Glasses RV101 (`RG-glasses`, Android device `glasses`) |
| Serial | <DEVICE_SERIAL> |
| Firmware / build | 1.22.009-20260710-150201 |
| Android | 12 (SKQ1.240613.001, user/release-keys) |
| Kernel | 5.10.209-perf-gd908d71a0404-ab1.22.009-20260710-150201 |
| Security patch | 2024-07-05 |
| CVE | CVE-2026-43499 (upstream; fix `3bfdc63936dd` absent) |

## Before → after (verbatim from evidence)

```
BEFORE:  getenforce → Enforcing ; uid=2000 euid=2000 gid=2000
AFTER:   id         → uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
         id -Z      → u:r:kernel:s0
         getenforce → Permissive
         CapPrm/CapEff/CapBnd → 000001ffffffffff
```

Non-destructive proof: created root-owned `/data/root_proof`; read root-only paths
(`/data/adb`, `/dev/block/by-name`).

## Scope boundaries (not claimed)

- **Persistence:** not demonstrated.
- **AVB / dm-verity / boot-chain bypass:** not demonstrated — `system`, `system_ext`,
  `vendor` remained mounted read-only.
- **Remote exploitation / every-firmware applicability:** not established. The chain is
  local and was tested on one device/one firmware.

## Remediation request

1. Backport the upstream CVE-2026-43499 fix to the RV101 `5.10.209-perf` branch.
2. Review candidate independently-fixable conditions (e.g., tracefs pointer disclosure to
   the `shell` user, `dmesg_restrict=0`, exposed device nodes, root `GateServiced`).
3. Harden the camera-in-use privacy LED: hardware-triggered (camera ISP/CSID output-enable)
   or host-authenticated I2C control; dedicated SELinux type for the LED sysfs; restrict the
   `lights_ctrl` Binder surface and the `session_open` property control path (RS-8).

## Secondary finding (privacy-LED trust boundary)

With the root gained by the chain, the camera-in-use white privacy LED can be suppressed or
its state forged while the camera subsystem stays active. The control chain is entirely
software/REE: sysfs (`0640 system:system`, DAC-overridable), the `lights_ctrl` Binder
service, and the `vendor.rkd.camera.session_open` property that drives `init`'s
"camera opened/closed" action. Control paths A/B/C were dynamically validated 2026-08-24
(`reports/led_validation/led_test.log`; no camera capture triggered). This is a Rokid-specific
hardening issue (**RS-8**), not a new CVE. See
`reports/RV101_PRIVACY_LED_TRUST_BOUNDARY_AUDIT.md`.

## Disclosure status

Initial contact and full package prepared (`RV101_Disclosure/`); reproduction material
available privately. Standard 90-day coordinated-disclosure window (negotiable).
