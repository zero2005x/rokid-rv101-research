# RV101 Photo/Video "Flash" Activation Mechanism Research

> 中文版：[RV101_CAMERA_LED_FLASH_MECHANISM.md](RV101_CAMERA_LED_FLASH_MECHANISM.md)

> Date: 2026-08-23　Device: <DEVICE_SERIAL> (fw 1.22.009, Android 12, kernel 5.10.209-perf)
> Method: static analysis of the firmware images (system/product/vendor.img) + on-device sysfs/DTS/logcat/dmesg validation.
> All paths and line numbers are reproducible.

---

## 0. One-sentence conclusion

The RV101 **has no conventional camera flash** (the Camera HAL reports flash unit=false, the kernel flash subsystem is entirely off,
and the DTS has no strobe node). The so-called "flash" is **the white channel of the AW2110x RGBW indicator LED**, driven by
`init.rokid.rc` watching the system property `vendor.rkd.camera.session_open`, which **lights white at 255 when a camera session opens
and turns it off at 0 when it closes**; the master switch is `vendor.rkd.camera.led.enable` (default 1).
In essence it is a **camera-in-use fill light / privacy indicator**, not a strobe at the instant of capture.

---

## 1. The complete mechanism chain (four layers)

```
[App layer]   AssistServer(com.rokid.os.sprite.assistserver) / camera App
              │  open camera capture session
              ▼
[HAL layer]   libcameraservice.so (Rokid-modified, string vendor.rkd.camera.session_open @offset 557448)
              │  SystemProperties.set("vendor.rkd.camera.session_open","1"/"0")
              ▼
[Init layer]  /vendor/etc/init/hw/init.rokid.rc (lines 17–23)
              on property:vendor.rkd.camera.session_open=1
                  write /dev/kmsg "camera opened!"
                  write /sys/class/leds/white/brightness 255
              on property:vendor.rkd.camera.session_open=0
                  write /dev/kmsg "camera closed!"
                  write /sys/class/leds/white/brightness 0
              ▼
[Kernel layer] leds-aw2110x driver (I2C1 address 0x30, Awinic AW21104 RGBW)
            DTS: soc/i2c@a90000/aw2110x-leds@30, chan@0=blue reg0, chan@1=green reg1,
                 chan@2=red reg2, chan@3=white reg3 (led-cur=4, max-cur=255)
```

**Master switch**: `vendor.rkd.camera.led.enable` (measured getprop = `1`).

## 2. Evidence that "there is no flash hardware"

| # | Evidence | Source |
|---|------|------|
| 1 | `dumpsys media.camera`: `Has a flash unit: false`, `android.flash.info.available = 0` | real device 2026-08-23 |
| 2 | `# CONFIG_LEDS_QTI_FLASH is not set`, `# CONFIG_LEDS_QPNP_FLASH_V2 is not set`, `# CONFIG_V4L2_FLASH_LED_CLASS is not set` (`CONFIG_LEDS_CLASS_FLASH=y` but no actual driver) | `research/kernel_configs/device_config_1.22.009_live.txt` |
| 3 | A device-tree global search for `*flash*/*strobe*` only hits `mmc-hs400-enhanced-strobe` (SD card, unrelated); the only LED node is `aw2110x-leds@30` | `/sys/firmware/devicetree/base/` |
| 4 | No APK dex has `setTorchMode` or any torch reference; only AOSP Camera2 carries an inert legacy `FLASH_MODE` string | pulled APKs (record/assist/live/screenrecord/camera2/seccamservice) |
| 5 | All 9 references to `sys/class/leds/white` in vendor.img come from the lights HAL/init scripts (status-light usage), not the camera path | `grep -aoc` image scan |

## 3. LED control permission model (who can light it)

`/vendor/etc/ueventd.rc` lines 384–415:

```
/sys/class/leds/{red,green,blue,white}  brightness/delay_on/delay_off/breath/trigger/led_current
    → 0640  system:system
```

- An ordinary app (untrusted_app) **cannot** write sysfs directly → it can only use init's property-trigger path.
- A `system` UID service (such as AssistServer, UID 1000) can write brightness directly (the fallback path).
- The QTI lights HAL (`vendor.qti.hardware.lights.service`) supports trigger/breath (breathing light),
  corresponding to notification-light functionality; unrelated to the camera.

## 4. Dynamic validation record (2026-08-23, closed loop complete ✅)

| Test | Result |
|------|------|
| `getprop vendor.rkd.camera.led.enable` | `1` (mechanism enabled by default) |
| Camera2 app start (API v1 connect→close after 68ms) | property **does not flip**, no kmsg → a plain open does not trigger |
| AssistServer opens `CameraDeviceClient 0` with API v2 (logcat 22:28:10) | likewise no kmsg → the trigger point is later, at the start of streaming |
| Sending a shutter event to the camera app (input tap) | **the whole device hung and rebooted automatically** (uptime reset to zero) |
| **`setprop vendor.rkd.camera.session_open 1/0` after root** | ✅ kmsg shows `camera opened!` / `camera closed!` (init T193 executes the write) |
| **`service call lights_ctrl 8 i32 2 i32 2014`** | ✅ dumpsys registers `Event{mType=EVENT_TYPE_WARN(2), mId=EVENT_WARN_ID_CAMERA_OPEN(2014), mLightId=8, mState=TURN_ON, mPriority=8000}` |
| **`service call lights_ctrl 9 i32 2 i32 2014`** | ✅ event cancelled, `mCurrentEvent=null` |

### 4.1 Suppression validation (2026-08-24, root + Permissive, without triggering capture)

Under root (`u:r:kernel:s0`, Permissive) a **controlled suppression closed-loop test** was run against the three control paths; raw evidence
`reports/led_validation/led_test.log` (SHA-256 `6ffb3979…`), reproduction script `scripts/led_test.sh`:

| Path | Command | Observation | Verdict |
|------|------|------|------|
| A — sysfs DAC override | `echo 0 > /sys/class/leds/white/brightness`; `echo 0 > led_current` | `write_rc=0`; `led_current` **2→0** | ✅ write takes effect (CAP_DAC_OVERRIDE bypasses `0640 system:system`) |
| A' — control-plane interaction | `echo 255 > led_current` | reads back **2** (overwritten by the `lights_ctrl` event `mCurrent=0x020202`) | ✅ the sysfs and Binder control planes overwrite each other |
| B — Binder cancel | `service call lights_ctrl 9 i32 2 i32 2014` | Parcel succeeds; `mCurrentEvent=null`, Event size=0 | ✅ the event can be cleared |
| B' — Binder re-send | `service call lights_ctrl 8 i32 2 i32 2014` | `Event{...CAMERA_OPEN(2014)... TURN_ON}`, size 0→1 | ✅ the event can be injected |
| C — property forgery | `setprop vendor.rkd.camera.session_open 0` | dmesg: `init: processing action (...session_open=0)...` + `camera closed!` | ✅ `init` consumes the forged state |

**Baseline (camera idle)**: `brightness=0`, `led_current=2`, `session_open` empty — the white privacy LED's
**default (idle) state is off**, and it is lit only when camera streaming starts, through the paths above (this report's §1).
All three paths can extinguish/forge it at the software layer, and path C is the most upstream (it directly forges the "camera not open" state).

### 4.2 Boundary confirmation: the P-Sensor / worn state does "not" control the LED (2026-08-24, second round)

To confirm whether the white privacy LED has a third "worn/proximity state" control path, the worn/proximity state was forged
on top of the camera-lit baseline (brightness=255) (`setprop vendor.rkd.glasses.is_take_on 0`, `is_spread 0`,
`rokid.debug.enforce_psensor 0`); raw evidence `reports/led_validation/pathd_test.log`:

| Observation | Result |
|------|------|
| `is_take_on=0` | forgery persists (not overwritten) |
| `is_spread=0` | overwritten back to 1 within ~2s by an active consumer (a system component actively maintains it) |
| `rokid.debug.enforce_psensor=0` | init → PSOC sysfs `enforce_psensor`=0; dmesg `psensor_delay_work … extcon psensor = 0`, `rt600_spi_boot spi0.0: proximity state = 0` (hardware-level physical-state forgery) |
| **LED brightness** | **stays 255**, the camera-open event stays TURN_ON |

**Conclusion**: the white LED control chain **only** has the two paths described in §1 (`lights_ctrl` Binder and the `session_open`
init.rc). Between the worn/proximity state (PSOC 1-0008 `enforce_psensor`/`psensor*`, `is_take_on`/`is_spread`)
and the LED there is **no init.rc/driver linkage** — forging the worn state cannot directly extinguish the privacy LED.

## 5. Exact call sites (libcameraservice.so disassembly, 2026-08-23)

The string `vendor.rkd.camera.session_open` @ .rodata file-offset 557448 (vaddr 0x88188),
and the whole binary has exactly two precise xrefs:

| Function | Address | Behavior |
|------|------|------|
| `CameraService::BasicClient::startCameraStreamingOps` | adrp@0xef670 | see the branch logic below |
| `CameraService::BasicClient::finishCameraStreamingOps` | adrp@0xef998 | symmetric: cancelEvent or setprop "0" |

```c
// startCameraStreamingOps (Rokid patch, pseudocode)
int panel = atoi(property_get("ro.boot.glassesWithPanel", "0"));
if (panel == 1)
    LightsNative::sendEvent(2, 2014);      // on this device ro.boot.glassesWithPanel=1 → this path is taken
else if (panel == 0)
    property_set("vendor.rkd.camera.session_open", "1");   // the init.rc path for other SKUs
```

`LightsNative::sendEvent/cancelEvent` are defined inside libcameraservice.so (T symbol 0x228fac /
0x229308), and the implementation is `defaultServiceManager()->getService(String16("lights_ctrl"))` followed by
vtable+0x58/+0x60 calls.

**Full BpLightsCtrl AIDL method table** (address order = transaction-code order):

| code | Method |
|------|------|
| 1–7 | setBrightness(int,float) / setFlashing(i,i,i) / pulse(i,i) / turnOn(i) / turnOnAll() / turnOff(i) / turnOffAll() |
| **8** | **sendEvent(int type, int id)** ← used by the camera |
| **9** | **cancelEvent(int type, int id)** |

**Server side**: the Rokid framework classes `com.rokid.light.*` inside system_server
(ILightsCtrl/LightsCtrl/LightsCtrlInternal/LedEventId/LedId/LedType,
see the system.img offset ~490532433 region; the factory class `com.rokid.server.light.EventFactoryV4`).

**Event semantics (decoded from a measured dumpsys lights_ctrl)**:

```
type=2   → EVENT_TYPE_WARN
id=2014  → EVENT_WARN_ID_CAMERA_OPEN (camera-open warning)
effect   → mLightId=8 TURN_ON, priority=8000, mCurrent=0x020202 (white)
end      → cancelEvent(2,2014) or automatic clearing when streaming stops
```

## 6. Final architecture diagram (two paths)

```
photo/video start (streaming start)
 └ CameraService::BasicClient::startCameraStreamingOps()   [Rokid patch]
    ├─ ro.boot.glassesWithPanel==1 (current state of this device)
    │    └ LightsNative::sendEvent(EVENT_TYPE_WARN=2, EVENT_WARN_ID_CAMERA_OPEN=2014)
    │         └ Binder "lights_ctrl" txn#8 (com.rokid.light.ILightsCtrl)
    │              └ system_server EventFactoryV4 → LightId 8 white TURN_ON (priority 8000)
    └─ ro.boot.glassesWithPanel==0 (other configurations)
         └ property "vendor.rkd.camera.session_open"="1"
              └ init.rokid.rc trigger → /sys/class/leds/white/brightness = 255

photo/video end → finishCameraStreamingOps() extinguishes symmetrically (cancelEvent / property "0"+write 0)
```

Both paths have been closed-loop validated on the real device (§4 table).

## 7. Known risk observations

- After forced camera operation (input tap on the shutter) the whole device rebooted once (around 2026-08-23 23:00).
  **Note**: at the time of the incident the environment was GhostLock root + Permissive (a uid=0 root holder was alive and
  the kernel had a history of unpatched UAF triggering), so **an interaction with the kernel UAF state cannot be ruled out**;
  until it is reproduced in a clean Enforcing environment, it must not be claimed as an independent DoS weakness of the camera path.

## 9. Root cause and permanent fix of video-recording flicker (2026-09-09 update)

**Symptom**: after acquiring root, when the phone-side App starts ghostlock video recording, the privacy LED flickers.

**Root cause (two layers)**:
1. **Initial single flash**: `CameraController.startVideo()` originally called `bindVideo()` first (streaming starts →
   libcameraservice sends `sendEvent(2,2014)` → LED at 255) and only **afterwards** `led.suppress()`
   → at the instant of binding it lights up first and is only pushed down by the hold-loop within 200ms.
2. **Pulsing flicker during recording**: if the camera session restarts/re-sends the event during recording (measured: the mTime of the
   streaming-start event is updated each time), `lights_ctrl` writes 255 back; it races with the 0.2s hold-loop → each re-sent event
   produces one visible pulse. Pure sysfs suppression is a "race", not an "off switch".

**Permanent fix (implemented and verified)**:
- When suppressing, cancel the event at the Binder layer: `service call lights_ctrl 9 i32 2 i32 2014`
  (`cancelEvent`) → `mCurrentEvent=null`, and system_server no longer has a forced-write source;
  sysfs writing 0 + the hold-loop are kept as a safety net.
- `startVideo()`'s `led.suppress()` was moved **before** `bindVideo()` (eliminating the initial flash).
- On restore, re-send the event: `service call lights_ctrl 8 i32 2 i32 2014` (while the camera is in use the system
  maintains the light itself; when the session ends the system cancels the event and turns it off itself).

**Measured (2026-09-09 23:08)**:
- ghostlock ON recording: sampling 20 seconds × 100ms gave **200/200 all zero**, and although `mCurrentEvent` is still re-sent by the camera service
  when streaming starts, there is no more flicker (the suppression path cuts it off at the event source).
- ghostlock OFF recording: brightness=255 (natural privacy indication) ✓.
- After stopping recording: the camera session is still bound (`Active Camera Clients State 2`) → the light stays at 255,
  which is the pre-existing privacy-indication semantics (lit while the camera is in use), not introduced by this fix.

## Appendix: reproduction command list (the original §8 of 2026-08-23, all verified)

```bash
source deploy/env.sh   # single source of ADB/SERIAL (since 2026-09-13); S="<DEVICE_SERIAL>"

# 0) Acquire root (after the boot health gate; inside the root shell it will setenforce 0)
./scripts/boot_run_loop.sh 4 w2 v960 "gid = 0 (" \
  "GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 W2_ATTEMPTS=1" /data/local/tmp/ghostlock_rv101

# 1) init.rc path verification (under Permissive, shell alone suffices)
"$ADB" -s $S shell 'setprop vendor.rkd.camera.session_open 1; dmesg | grep camera'
#    → "camera opened!"; set back to 0 → "camera closed!"

# 2) lights_ctrl Binder path verification (the path the camera actually takes)
"$ADB" -s $S shell 'service call lights_ctrl 8 i32 2 i32 2014; dumpsys lights_ctrl'   # light on
"$ADB" -s $S shell 'service call lights_ctrl 9 i32 2 i32 2014; dumpsys lights_ctrl'   # light off

# 3) sysfs DAC override (needs a real uid=0; shell is blocked by the 0640 system:system DAC)
"$ADB" -s $S shell 'echo 0 > /sys/class/leds/white/brightness; cat /sys/class/leds/white/brightness'

# 4) One-click three-path suppression closed loop (run as root; output written to /data/local/tmp/led_test.log)
#    → scripts/led_test.sh (used with scripts/run_led_orchestrator.sh to inject into the FOPS window)

# 5) Disassembly reproduction
#   string xref: python3 /tmp/opencode/find_xref.py libcameraservice.so 557448
#   → startCameraStreamingOps@0xef670 / finishCameraStreamingOps@0xef998
```
