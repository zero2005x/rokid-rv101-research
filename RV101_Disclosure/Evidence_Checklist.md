# Evidence Checklist — Rokid RV101 LPE Disclosure

Legend: ✅ collected · ⏳ requested/pending · ➖ not applicable

## 1. Device identity

| # | Item | Status | Evidence file |
|---|---|---|---|
| 1.1 | `ro.product.model` / `ro.product.device` | ✅ | `02_Device_Information/02_identity_build.txt` (`RG-glasses`, `glasses`) |
| 1.2 | `ro.build.fingerprint` | ✅ | same — `Rokid/glasses/glasses:12/SKQ1.240613.001/1.22.009-20260710-150201:user/release-keys` |
| 1.3 | `ro.build.version.release` (Android) | ✅ | same — `12` (SDK 32) |
| 1.4 | `ro.build.version.security_patch` | ✅ | `02_Device_Information/03_security_baseline.txt` — `2024-07-05` |
| 1.5 | `uname -a` / `/proc/version` | ✅ | `02_Device_Information/04_kernel_runtime.txt` — `5.10.209-perf-gd908d71a0404-ab1.22.009-20260710-150201` |
| 1.6 | Rokid model / firmware / build number | ✅ | model `RG-glasses`; firmware/build `1.22.009-20260710-150201` |
| 1.7 | Kernel config (live) | ✅ | `06_Kernel_Logs/kernel_config_1.22.009.txt` |
| 1.8 | Device serial | ✅ | `<DEVICE_SERIAL>` (`02_Device_Information/00_adb_devices.txt`) |

## 2. Initial (pre-exploit) privilege state

| # | Item | Status | Evidence file |
|---|---|---|---|
| 2.1 | `id` (uid/gid/groups/context) | ✅ | `03_Pre_Exploit_State/pre_exploit_uid_selinux.txt` — `uid=2000(shell) … context=u:r:shell:s0` |
| 2.2 | `getenforce` (before) | ✅ | `03_Pre_Exploit_State/pre_exploit_security_baseline.txt` — `Enforcing`; also `getenforce before: Enforcing` in run log |
| 2.3 | `CapInh/Prm/Eff/Bnd` before | ⏳ | capture separately as shell (recommended for completeness) |
| 2.4 | Bootloader / verified-boot state (environmental) | ✅ | `03_Pre_Exploit_State/pre_exploit_security_baseline.txt` — `orange`/`unlocked`/`flash.locked=0` |

## 3. Post-exploit privilege state

| # | Item | Status | Evidence file |
|---|---|---|---|
| 3.1 | `id` / `id -Z` | ✅ | `05_Post_Exploit_State/post_exploit_state_REDACTED.txt` + raw log `uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0` |
| 3.2 | `getenforce` (after) | ✅ | `Permissive` (same files) |
| 3.3 | `Uid/Gid/Cap*` `/proc/self/status` | ✅ | `CapPrm/CapEff/CapBnd = 000001ffffffffff` |
| 3.4 | `cat /proc/$$/attr/current` | ✅ | `u:r:kernel:s0` |

## 4. Practical (non-destructive) privilege demonstration

| # | Item | Status | Evidence file |
|---|---|---|---|
| 4.1 | Create/remove harmless proof file under `/data` | ✅ | `/data/root_proof` created (root:root, `-rw-rw-rw-`) |
| 4.2 | Access root-only directory (`/data/adb`, 0700 root) | ✅ | directory listing shows read access |
| 4.3 | Read root-only block metadata (`/dev/block/by-name`, 0600) | ✅ | partition dump succeeded |

## 5. Mount / partition status

| # | Item | Status | Evidence file |
|---|---|---|---|
| 5.1 | `mount` / `/proc/mounts` | ✅ | raw log — `system_ext`/`vendor`/`vendor_dlkm` on dm-* `ro` |
| 5.2 | `ls -l /dev/block/by-name` | ✅ | raw log (full by-name listing) |
| 5.3 | Confirm system/vendor NOT writable | ✅ | mounts remain `ro` |

## 6. Attack preconditions (to be answered by researcher — see Vendor_Questions.md)

| # | Question | Status |
|---|---|---|
| 6.1 | Physical access required? | ⏳ confirm |
| 6.2 | USB debugging / Developer Options required? | ⏳ confirm |
| 6.3 | Prior ADB RSA authorization (host key) required? | ⏳ confirm (see preloaded-key claim) |
| 6.4 | Attack begins from uid 2000 shell? | ✅ CONFIRMED (startup `uid=2000`) |
| 6.5 | Is an installed Android app sufficient? | ⏳ unknown (not tested) |
| 6.6 | Special Android permission required? | ⏳ unknown |
| 6.7 | Unlocked bootloader actually required? | ⏳ **environmental only — not believed required** (confirm) |
| 6.8 | Network access required? | ⏳ confirm (believed none) |
| 6.9 | User interaction during exploitation? | ⏳ confirm (believed none post-shell) |
| 6.10 | Survives reboot? | ⏳ **NOT demonstrated — do not claim** |

## 7. Root-cause / upstream CVE evidence

| # | Item | Status | Evidence file |
|---|---|---|---|
| 7.1 | CVE-2026-43499 assigned (upstream) | ✅ | public advisories (Red Hat RHSB-2026-010, CloudLinux, AlmaLinux, nebusec.ai) |
| 7.2 | RV101 kernel lacks the upstream fix | ✅ | disassembly of `remove_waiter()` (see `10_Upstream_CVE_Comparison/`, `research/ghostlock_refs/remove_waiter.disasm.txt`) |
| 7.3 | Fix commit / affected range | ✅ | mainline `3bfdc63936dd`; affected 2.6.39–7.1 (per project notes) |
