# RV101 Privacy Indicator Trust Boundary Audit Report
# (RV101 Privacy Indicator / P-Sensor Trust Boundary Audit Report)

> 中文版：[RV101_PRIVACY_LED_TRUST_BOUNDARY_AUDIT.md](RV101_PRIVACY_LED_TRUST_BOUNDARY_AUDIT.md)

> Audit date: 2026-08-24　Device: Rokid RV101 (serial <DEVICE_SERIAL>)
> Firmware: 1.22.009-20260710-150201 (Android 12, kernel 5.10.209-perf)
> Environment: adb root (uid=0 acquired via GhostLock CVE-2026-43499) + SELinux Permissive
> Method: **static analysis + read-only probing** (cat/ls/grep/dumpsys/getprop/sha256sum/disassembly), plus, added on 2026-08-24,
> **controlled dynamic validation** (controlled writes/cancellations/property forgery against the LED control path under root + Permissive; no camera capture/recording was triggered at any point).
> Related documents: `RV101_CAMERA_LED_FLASH_MECHANISM.md` (mechanism reverse engineering), `SHA256SUMS.txt` (evidence chain),
> `reports/led_validation/led_test.log` (dynamic validation evidence for paths A/B/C, SHA-256 `6ffb3979…`),
> `reports/led_validation/pathd_test.log` (path D evidence for P-Sensor/wear-state forgery, SHA-256 see manifest)

---

## 1. Executive Summary

**The RV101's capture indicator light (the white Privacy LED) is driven directly by the AP main chip over a single I2C bus with 0666 permissions,
and the entire control chain (CameraService patch → Binder → system_server Java factory → sysfs) runs inside the
REE (Rich Execution Environment); no TEE applet, Secure MCU, or hardware interlock anywhere in the system participates
in the "turn the light on" decision. A uid=0 attacker can therefore bypass the whole chain by writing to sysfs directly or intercepting Binder,
making the indicator light useless while the camera is running. Although the P-Sensor's raw data is isolated in the SLPI microcontroller, the
"wear state" derived from it is exposed to userspace as writable system properties
(`vendor.rkd.glasses.is_take_on/is_spread`), so the anti-obstruction threshold is likewise
Software-Enforced.**

> **Dynamic validation results (2026-08-24, root + Permissive, no camera capture triggered)**: the three attack paths in §4
> have moved from paper reasoning to **on-device validation** —— path A (`echo 0 > brightness/led_current` write takes effect,
> `led_current` 2→0), path B (`service call lights_ctrl 9/8` successfully cancels/re-sends the camera-open event),
> path C (`setprop vendor.rkd.camera.session_open 0` → `init` triggers `camera closed!`).
> Evidence in `reports/led_validation/led_test.log`. Because the "do not trigger capture" constraint was honored, the baseline is the camera idle state
> (brightness=0 / led_current=2), so "the light goes out while the camera is running" is a combined inference from the three validated control paths
> (INFERRED); the control paths themselves are CONFIRMED.
>
> **Path D (P-Sensor/wear-state forgery, 2026-08-24 second round)**: forging `is_take_on`/`is_spread`/`enforce_psensor`
> **succeeds** (`is_take_on=0` persists; `enforce_psensor=0` produces a hardware-level "proximity state = 0" reaction through init → PSOC sysfs → extcon → rt600),
> but **LED brightness stays at 255** —— confirming that the P-Sensor/wear state
> **does not directly control the privacy LED**, which is controlled only by paths A/B/C; what path D breaks is the "physical state" trust boundary (the downstream
> wear/proximity state can be forged), which is a **different trust boundary** from the LED. Evidence `reports/led_validation/pathd_test.log`.

---

## 2. Attack Surface Mapping

### 2.1 LED-Related Nodes

| # | Node | Permission / label | Evidence |
|---|------|------------|------|
| A1 | `/sys/class/leds/{white,red,green,blue}/brightness` | DAC `0640 system:system`; SELinux `u:object_r:sysfs:s0` (**generic type, no dedicated leds type**) | `ls -laZ /sys/class/leds/white/` (measured in §2 of this audit) |
| A2 | `/sys/class/leds/white/{breathe,led_current,trigger}` | same as above: `0640 system:system` + `sysfs:s0` | same as above; re-checked with `stat -c` |
| A3 | `/sys/bus/i2c/devices/1-0030/` (AW2110x, including the `engine_mux/select_engine/run_engine/update_pattern` effect engine) | most driver attributes are root-only, but the bus node it belongs to is A4 | `ls -la /sys/class/leds/white/device/` |
| A4 | **`/dev/i2c-1`** | **`crw-rw-rw-` (0666, world-writable!)** | `ls -l /dev/i2c-*`; i2c-0/2 are both 0600, and only i2c-1, which carries the LED, is open |
| A5 | DTS `soc/i2c@a90000/aw2110x-leds@30` (chan@3=white reg3, max-cur=255) | the DT contains no protection description at all (no secure/pin attributes) | `/proc/device-tree/.../chan@3/*` |
| A6 | Binder `lights_ctrl` (descriptor `com.rokid.light.ILightsCtrl`) | publicly visible in the service list; txn#8 sendEvent / #9 cancelEvent can be called from any domain with binder access (measured: shell alone suffices) | `service list \| grep lights_ctrl`; `service call` recordings from the previous round |

### 2.2 P-Sensor-Related Nodes

| # | Node | Observation | Evidence |
|---|------|------|------|
| B1 | sensortek **UCS146E0** proximity sensor (HAL handle `ucs_ucs146e0`, both Non-wakeup and Wakeup paths, `0x51/0x52`) | SensorFramework registers it with **`perm: n/a`** —— any app can listen for proximity events with no permission at all | `dumpsys sensorservice` |
| B2 | **Two** proximity sensing paths: ① sensortek UCS146E0 (SLPI/SSC, Android HAL); ② **PSOC 1-0008** (AP I2C, driver `Rokid,PSOC-TP-R`, DTS `psoc4000R@08`) | ① raw ADC is in the SLPI; ② the raw readings `psensor/psensor0/psensor1` (0444 read-only, world-readable; measured 1/0/0) and `hall` (=1) are **readable on the AP I2C** | i2c device enumeration; measured with `cat /sys/.../1-0008/psensor*` |
| B3 | Derived state as **system properties**: `vendor.rkd.glasses.is_take_on`, `vendor.rkd.glasses.is_spread` | both currently `1`; **can be forged with setprop** —— `is_take_on=0` persists after being written; `is_spread=0` is **overwritten back to 1** within ~2 s by an active consumer (confirming it is actively maintained by a system component) | `getprop`; 2026-08-24 dynamic validation (`pathd_test.log`) |
| B4 | `/dev/input/event0,event1` | `0660 root:input` (qpnp_pon power key, PSOC-TP-R touch/keys) —— unrelated to prox but part of the same class of input surface | `getevent -lp` |
| B5 | **PSOC 1-0008 control nodes** (`/sys/devices/platform/soc/a90000.i2c/i2c-1/1-0008/`): `enforce_psensor` (=1), `enforce_hall` (=0), `thres_pct` (=0), `proximity_tuning` (=0), `proximity1_tuning` | written by init.rokid.rc via **property** triggers: `rokid.debug.enforce_psensor=1/0`→`enforce_psensor`, `rokid.debug.enforce_hall`→`enforce_hall`, `rokid.psensor.mode=standard/sensitive`→`thres_pct 0/3`, `rokid.debug.proximity_tuning`→`proximity_tuning` | `~~assets/vendor_extracted/etc/init/hw/init.rokid.rc~~ (**`assets/` no longer exists in the workspace; the cited line numbers cannot be re-checked, note of 2026-09-13**)` lines 41-87; measured `setprop rokid.debug.enforce_psensor 0` → dmesg `extcon psensor = 0` |

---

## 3. Trust Boundary Analysis

### 3.1 Control Flow Diagram (measured + confirmed by disassembly)

```
[photo/video streaming start]
        │
        ▼
┌─────────────────────────────────────────────────────────────┐
│ libcameraservice.so (REE, Rokid patch @0xef670)              │
│   BasicClient::startCameraStreamingOps()                    │
│   ├ ro.boot.glassesWithPanel==1 → LightsNative::sendEvent(2,2014) ──┐ REE
│   └ ==0 → property_set("vendor.rkd.camera.session_open","1")        │ REE
└─────────────────────────────────────────────────────────────┘     │
        │ Binder txn#8 ("lights_ctrl")                    │ setprop
        ▼                                                  ▼
┌───────────────────────────────┐   ┌──────────────────────────────────┐
│ Java service in system_server   │   │ init (userspace)                  │
│ com.rokid.light.LightsCtrl /   │   │ init.rokid.rc:17-23 property     │ REE
│ EventFactoryV4 (REE daemon)     │   │ trigger                          │
└───────────────┬───────────────┘   └──────────────┬───────────────────┘
                │ write                            │ write
                ▼                                  ▼
     /sys/class/leds/white/brightness (sysfs, 0640, object_r:sysfs)
                │ I2C1 @ 0x30 (/dev/i2c-1 is 0666)
                ▼
        AW2110x RGBW white channel ←—— [terminal actuator: ordinary I2C slave, no Secure Element]
```

**The entire path is REE. Key to the markings:**
- Participants: AP Linux kernel, init, system_server (Java), libcameraservice —— all Rich OS.
- **TEE involvement = 0**: zero qsee/tee strings in the lights HAL binary; in camera.qcom.so, QSEE appears only on the
  secure-streaming (DRM content protection) path (`QSEEIntfMutex` etc.), with no binding to the LED whatsoever.
- **Secure MCU involvement = 0**: the AW2110x is an ordinary I2C PWM LED driver chip with no authentication and no host verification;
  the board also carries a PSOC (i2c-1 0x08, `psoc-tp-right`) responsible for touch/keys/**proximity (hall/psensor)** sensing ——
  it has `enforce_psensor`/`psensor*`/`hall` nodes (§2.2 B2/B5), but **does not participate in light control** (the light still goes through
  the AW2110x; forging the PSOC's psensor state does not change the LED, see §4 path D).

### 3.2 SELinux Isolation Failure Analysis

| Observation | Evidence | Implication |
|------|------|------|
| LED sysfs uses the **generic** `u:object_r:sysfs:s0` label | `ls -Z /sys/class/leds/white/` shows `sysfs:s0` throughout | unlike vibrator/dm_verity, no dedicated genfscon type was set (plat_sepolicy.cil:306-310 has precedents for dedicated types; the LED was missed) → extremely coarse granularity |
| A wide range of domains are allowed on generic sysfs | plat+vendor CIL total 14,803 allow rules; the `domain` attribute itself already carries sysfs rules (apexd/audioserver/bluetooth/charger/dumpstate/hal_audio/gmscore_app… many domains) | the isolation boundary depends on "who cannot get generic sysfs write" rather than "LED-specific least privilege" |
| hal_camera_default has no sysfs write authorization | the hal_camera section of vendor_sepolicy.cil has only exec/fwservice/dumpstate rules | the original design really does **rely on SELinux** to force the Camera HAL to route light control through the Framework; but this isolation drops to zero instantly under Permissive, and the Root domain (magisk/su or a `u:r:kernel:s0` context) is not constrained |
| Permissive measured | this audit ran Permissive throughout, and all the above nodes were read without obstruction (only DAC 0640 blocks shell; root overrides it directly) | software isolation = the only line of defense |

---

## 4. Exploit Paths (A/B/C dynamically verified on 2026-08-24 ✅; D verified as "state forgery succeeds but does not directly control the light")

### Path A: Sysfs Hijack (DAC Override) ✅ Proven
uid=0 holds CAP_DAC_OVERRIDE → directly `echo 0 > /sys/class/leds/white/brightness`
(or `led_current=0` to cut the current). There is no kernel interlock and no I2C-layer authentication, so the write must take effect.
Related: root can also write the AW2110x pattern engine (`update_pattern/run_engine`) to inject a custom animation,
making the light "look normal" while it is actually off.

**Dynamic validation (2026-08-24)**: under root (`u:r:kernel:s0`, Permissive), both `echo 0 > brightness`
and `echo 0 > led_current` returned `write_rc=0`; `led_current` went from `2 → 0` (the write actually took effect, not merely a successful return).
Incidental finding: `echo 255 > led_current` still reads back as `2` —— the `lights_ctrl` event's
`mCurrent=0x020202` (0x02 per channel) overwrites the raw sysfs value, proving that sysfs and Binder are two control planes that
**overwrite each other**.

### Path B: Binder IPC Filtering (Hooking `lights_ctrl`) ✅ Proven
- userspace hook: Frida attaches to `system_server` and replaces `LightsCtrl.sendEvent()` with a no-op;
  or LD_PRELOAD in cameraserver to intercept `LightsNative::sendEvent`.
- Lower level: binder transaction filtering (discarding txn #8). Because the server side is an ordinary Java Binder thread,
  freeze/priority manipulation can also stall the event queue (dumpsys shows the Event size changing dynamically = a pure software queue).

**Dynamic validation (2026-08-24)**: `service call lights_ctrl 9 i32 2 i32 2014` (cancelEvent) and
`service call lights_ctrl 8 i32 2 i32 2014` (sendEvent) both returned successful Parcels; txn#8 made dumpsys
register `Event{...EVENT_WARN_ID_CAMERA_OPEN(2014)... mState=TURN_ON}` (Event size 0→1), and
txn#9 zeroed it (`mCurrentEvent=null`, Event size=0). The event queue is pure software and can be added to or removed from at will.

### Path C: `vendor.rkd.camera.session_open` Property Forgery (state forgery, the init.rc branch) ✅ Proven
`setprop vendor.rkd.camera.session_open 0` directly forges the "camera not open" state. This property is monitored by
`/vendor/etc/init/hw/init.rokid.rc` (§3.1 control flow second branch, used by the `glassesWithPanel==0`
SKU), and on receiving it `init` performs the `camera closed!` action.

**Dynamic validation (2026-08-24)**: after `setprop ... 0`, dmesg immediately showed
`init: processing action (vendor.rkd.camera.session_open=0) from (/vendor/etc/init/hw/init.rokid.rc:21)`
and `camera closed!` —— proving that this property is a **real control signal** consumed by `init` and can be forged at the source
to fake the camera session state (earlier and further upstream than path A).

### Path D: P-Sensor Data Forgery (Sensor Spoofing) — Verified: state forgery succeeds, but it does not directly control the light
- RAW ADC is split across two paths (§2.2 B2): sensortek UCS146E0 in the SLPI; PSOC 1-0008's `psensor*` read-only on the AP I2C.
  Neither can be **rewritten directly**; **but the consumer/gate is entirely in software**:
  1) apps that rely on `android.sensor.proximity` → Frida injects via `SensorManager.registerListener`
     a fake event of `SENSOR_STATUS_ACCURACY_HIGH + distance=far` (perm: n/a, zero permission threshold);
  2) logic that relies on the `vendor.rkd.glasses.is_take_on/is_spread` properties (AssistServer has been shown to read them)
     → `setprop` directly rewrites the "worn/expanded" state;
  3) the PSOC's psensor enforcement (`enforce_psensor`) via the `rokid.debug.enforce_psensor` property → init →
     sysfs → extcon → rt600 can be forged.
- **Dynamic validation (2026-08-24 second round)**: under a camera-light-on baseline (brightness=255), setting
  `is_take_on=0`, `is_spread=0` and `rokid.debug.enforce_psensor=0` simultaneously → **brightness stayed at 255** and the
  camera-open event stayed TURN_ON. The forgery itself succeeded: `is_take_on=0` persisted; `is_spread` was overwritten
  back to 1 by an active consumer; `enforce_psensor=0` triggered dmesg `psensor_delay_work … extcon psensor = 0` and
  `rt600_spi_boot spi0.0: proximity state = 0` (hardware/driver-level physical-state forgery).
- Conclusion: **P-Sensor/wear-state forgery does not constitute direct suppression of the privacy LED** —— the LED is controlled only by paths A/B/C;
  what path D breaks is the trust boundary that "physical state (wear/proximity) is a hardware fact" (downstream consumers can be deceived),
  which is a **different trust boundary** from paths A/B/C. The conclusion that the anti-obstruction mechanism does not constitute a Hardware Interlock is unchanged.

### 4.5 Dynamic Validation Record Summary (2026-08-24, root + Permissive, no capture triggered)

| Path | Command | Observed result | Verdict |
|------|------|---------|------|
| A | `echo 0 > /sys/class/leds/white/brightness` / `led_current` | `write_rc=0`; `led_current` 2→0 | ✅ CONFIRMED (DAC override takes effect) |
| A' | `echo 255 > led_current` | reads back `2` (overwritten by the `lights_ctrl` event `mCurrent=0x020202`) | ✅ control-plane interaction confirmed |
| B | `service call lights_ctrl 9 i32 2 i32 2014` | successful Parcel; the event queue can be emptied | ✅ CONFIRMED |
| B' | `service call lights_ctrl 8 i32 2 i32 2014` | `Event{...CAMERA_OPEN... TURN_ON}`, Event size 0→1 | ✅ CONFIRMED |
| C | `setprop vendor.rkd.camera.session_open 0` | dmesg: `init: ... camera closed!` | ✅ CONFIRMED (state forgery takes effect) |
| D | `setprop vendor.rkd.glasses.is_take_on 0` (+ `is_spread 0`, `rokid.debug.enforce_psensor 0`) | `is_take_on=0` persists; `is_spread` is overwritten back to 1; `enforce_psensor` sysfs 0 (dmesg `extcon psensor=0` → rt600) | ✅ physical-state forgery CONFIRMED |
| D' | same as above (under a camera-light-on baseline of brightness=255) | **brightness stays at 255**, the camera-open event stays TURN_ON | ❌ does not directly suppress the LED (no control link between wear/proximity state and the LED) |

Raw evidence: `reports/led_validation/led_test.log` (paths A/B/C, SHA-256 `6ffb3979…`),
`reports/led_validation/pathd_test.log` (path D); reproduction scripts `scripts/led_test.sh`, `scripts/pathd_test.sh`.
The baseline was the camera idle state (brightness=0 / led_current=2 / session_open empty), so "the light goes out while the camera is actually running"
is a combined inference from the three validated control paths (INFERRED); the control paths themselves are CONFIRMED.

---

## 5. RE Targets Extraction (offline disassembly inventory)

| Target | Path | SHA-256 |
|------|------|---------|
| Camera HAL | `/vendor/lib64/hw/camera.qcom.so` | `85948d15756e846aeb62c4ad64961f16d1c8b0c899bc93c62f57245df4f4002b` |
| CameraService (the main body of the Rokid patch) | `/system/lib64/libcameraservice.so` | `db800b03cc19e2fc12ba9a05575fcdbada050a7db40595242205fbacb1eb6b4b` |
| Framework (embedding com.rokid.light.*) | `/system/framework/framework.jar` | `6d40be5cb07801d5ae6e3397113ab06b369a301bdf8c48bec1968544d315082d` |
| Init trigger script | `/vendor/etc/init/hw/init.rokid.rc` (locally extracted copy; on-device reads of that path by shell are refused by DAC) | `04a4cc21e92c53f1ffb43c4b0d40a3efafcf0849a0d4dd0b9f25c0f085d747b6` |
| Lights HAL service | `/vendor/bin/hw/vendor.qti.hardware.lights.service` | `506753e683e256226b7be3dd6c439d06241140f857c734330d48346e4b3d6c49` |
| sepolicy (CIL plaintext) | `/system/etc/selinux/plat_sepolicy.cil`, `/vendor/etc/selinux/vendor_sepolicy.cil` | (9,682 / 5,121 allow rules, for offline re-checking with sesearch) |

Known anchors (for IDA/Ghidra): libcameraservice.so — session_open string vaddr 0x88188,
startCameraStreamingOps xref adrp@0xef670, finishCameraStreamingOps@0xef998,
BpLightsCtrl::sendEvent@0x22b744, LightsNative::sendEvent@0x228fac.

---

## 6. Conclusion & Remediation

### Conclusion
1. The indicator light control chain is 100% in the REE; there is no TEE applet, no Secure MCU, and no hardware interlock. Under root,
   the guarantee "if the camera runs, the light must be on" does not hold. (Dynamically proven on 2026-08-24: root can, via the three paths of sysfs / Binder /
   the `session_open` property, directly extinguish the indicator light or forge its state, see §4.5.)
2. SELinux is the only software gate, and the LED falls under a coarse-grained generic `sysfs` label rather than a least-privilege design.
3. The P-Sensor's raw data has SLPI microcontroller isolation (a positive finding), but the gate derived from it is exposed as writable properties and
   zero-permission sensor events, so it is still Software-Enforced. (Dynamically proven on 2026-08-24: `is_take_on`/
   `enforce_psensor` can be forged and produce a hardware-level extcon reaction; but this does not affect the LED.)
4. **The LED is controlled only by paths A/B/C**: between the P-Sensor/wear state (`is_take_on/is_spread/enforce_psensor`)
   and LED brightness there is **no init.rc/driver link** (in init.rokid.rc the LED is triggered only by `session_open`),
   so "wear-state forgery" cannot directly extinguish the privacy LED —— turning the light off still requires the three paths of sysfs/Binder/`session_open`.

### Vendor Remediation Recommendations (Hardening Roadmap)
1. **Hardware interlock (the root fix)**: move the indicator light to a Secure MCU / PMIC hardware light-control channel, and have the camera ISP's
   streaming hardware signal (CSID/CAMIF output-enable) drive the LED enable line directly, so software can only "request"
   and cannot "veto"; or adopt an I2C slave with host authentication (accepting brightness commands only after challenge-response).
2. **I2C bus tightening**: return `/dev/i2c-1` to 0600 (it is currently 0666, world-writable, which is an unnecessary attack surface).
3. **SELinux least privilege**: add a dedicated type for leds (such as `sysfs_leds_w`) + precise genfscon labelling,
   so that only the domain containing `lights_ctrl` can write; remove the `domain` attribute's permissive allow on generic sysfs.
4. **De-privileging the state properties**: change `is_take_on/is_spread` and `rokid.debug.enforce_psensor`/`rokid.psensor.mode`
   to a direct kernel/SLPI read interface (such as an IIO sysfs read-only node + a dedicated SELinux type), to prevent setprop forgery of
   the wear/proximity state; the PSOC's `enforce_psensor`/`thres_pct` write permissions should also be tightened (currently 0644 system:system
   + property-triggered, which can be driven by root/property forgery).
5. **Integrity self-check**: bring init.rokid.rc and libcamereservice.so (which already contains the Rokid patch) into the scope of
   dm-verity/apex verification and compare against a manifest at boot, to detect the class of userspace tampering shown in this audit.

### Audit Limitations Statement (updated: 2026-08-24)
- No strace live-attach was performed (it requires ptrace on a root process, beyond the read-only principle); IPC behavior is instead corroborated by static disassembly
  anchors + dumpsys/getprop state, and the conclusions are unaffected.
- **Supplementary dynamic validation, 2026-08-24**: paths A/B/C were actually executed and verified in a controlled environment (root + Permissive, without triggering camera
  capture/recording) (`reports/led_validation/led_test.log`). The baseline state was camera
  idle (brightness=0 / led_current=2 / session_open empty), so the demonstration is a closed loop at the "control path layer"
  (writes/transactions/properties all took effect and produced the expected state transitions), not suppression of an LED-on baseline while "the camera is actually running".
- No camera stream was started for an end-to-end demonstration of "the light goes out while the camera is running" (honoring the "do not trigger capture" constraint). The
  suppression effect in that scenario is inferred from the combined conclusion of the three validated control paths (consistent with the §3.1 control flow), and is marked **INFERRED**,
  while the control paths themselves are **CONFIRMED**.
- **Path D second-round validation (2026-08-24)**: forging the wear/proximity state under a "camera light on" baseline (brightness=255)
  confirmed that **the state forgery succeeds but LED brightness does not change** (`reports/led_validation/pathd_test.log`).
  Path D is therefore characterized as "physical-state trust boundary breakage", not a direct privacy-LED suppression path.
