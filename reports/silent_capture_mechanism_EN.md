# RV101 Photo/Video Camera-Tone Suppression (Silent Capture): Mechanism and Validation

> 中文版：[silent_capture_mechanism.md](silent_capture_mechanism.md)

> Date: 2026-09-11 · Device: Rokid RV101 (`<DEVICE_SERIAL>`, fw 1.22.009, Android 12, kernel 5.10.209-perf)
> Status: **implemented in both the glasses-side and phone-side apps; on-device validation passed**

## 0. Conclusion (in one sentence)

RV101's photo/video camera tone is **played by `libcameraservice.so` from the OGG files under `/product/media/audio/ui/`**;
`/product` is read-only (dm-verity) so the files cannot be changed, but **root can use `mount --bind` to overwrite them with a legitimate "silent OGG"**
→ the playback side reads silent content (no sound), `umount` restores them immediately, and **a reboot clears it automatically** (no persistent side effects).
Already integrated into the apps: applied automatically after privilege escalation, and toggled in real time from the phone side.

## 1. Sound-Source Investigation (on device)

### 1.1 Playback Side (who plays the sound)

```bash
strings /system/lib64/libcameraservice.so | grep -iE 'camera_click|VideoRecord|audio/ui|playSound'
```

Measured output (excerpt):

```
_ZN7android13CameraService9playSoundENS0_10sound_kindE     ← CameraService::playSound(sound_kind)
playSound(%d)
/product/media/audio/ui/camera_click.ogg                    ← photo shutter (actual path on this device)
/product/media/audio/ui/VideoStop.ogg
/system/media/audio/ui/VideoRecord.ogg
```

- **The camera service plays the sound itself** (it is not played by the app) → the app-side API cannot turn it off, so it must be handled at the file or system layer.
- On this device all sound-effect files actually live under **`/product/media/audio/ui/`** (`/system/media/audio/ui/` is empty).

### 1.2 Target Files (6)

| File | Purpose |
|------|------|
| `camera_click.ogg` | Photo shutter |
| `camera_focus.ogg` | Focus |
| `VideoRecord.ogg` | Video recording start |
| `VideoStop.ogg` | Video recording stop |
| `rkd_record_start.ogg` | **Rokid custom** video-start tone |
| `rkd_record_end.ogg` | **Rokid custom** video-end tone |

> `rkd_*.ogg` are not referenced by `libcameraservice` (they are played by the Rokid app/service); they are masked as well for completeness.
> The same directory holds 50+ other `rkd_*.ogg` system UI sounds (key presses, notifications, etc.), which are **not in scope for suppression**.

### 1.3 System Properties

`getprop | grep -iE 'camera.*sound|shutter|forced'` → **no output** (`ro.camera.sound.forced` is not set on this device).
Even if that property is 1 on other production units (forcing the shutter sound), **the file-layer overwrite still works**, because these are exactly the files the playback side reads.

## 2. Suppression Method

### 2.1 Why bind-mount

| Approach | Feasibility |
|------|--------|
| Modify `/product/media/audio/ui/*.ogg` directly | ❌ Partition is read-only (dm-verity); `remount rw` fails |
| Delete the APK / disable the service | ❌ The player is `libcameraservice` (a system service); stopping it means stopping the camera |
| System-level muting (volume to zero) | ❌ Affects other audio (not targeted) |
| **bind-mount a silent file over the target** | ✅ Doable with root, effective immediately, reversible, cleared automatically on reboot |

### 2.2 Silent File

Generate a **legitimate Ogg Vorbis silent clip** with ffmpeg (bundled with imageio-ffmpeg) — mono 44100 Hz, 0.3 s, 3584 bytes —
to avoid a 0-byte file or `/dev/null` making the playback side fail in prepare:

```bash
FF=.venv/lib/python3.13/site-packages/imageio_ffmpeg/binaries/ffmpeg-linux-x86_64-v7.0.2
$FF -y -f lavfi -i anullsrc=r=44100:cl=mono -t 0.3 -c:a libvorbis -q:a 0 reports/media/silent_camera.ogg
```

This file is embedded into the app as base64 (4780 characters); `scripts/gen_silent_payload.py` generates
`app-glasses/.../ghost/SilentPayloads.kt` (do not edit that file by hand).

### 2.3 Actual Commands

```sh
# Suppress (every target file that exists; skip any already mounted)
F=/data/local/tmp/silent_camera.ogg
printf '%s' '<base64>' | base64 -d > $F; chmod 644 $F
for d in /product/media/audio/ui /system/media/audio/ui /vendor/media/audio/ui; do
  for f in camera_click.ogg camera_focus.ogg VideoRecord.ogg VideoStop.ogg rkd_record_start.ogg rkd_record_end.ogg; do
    t=$d/$f; [ -f "$t" ] || continue
    grep -qF " $t " /proc/mounts 2>/dev/null && continue
    mount --bind $F $t
  done
done

# Restore
umount <each mounted target>
```

## 3. Validation Evidence (on device, 2026-09-11)

### 3.1 Manual Validation (root shell)

| Step | Command | Result |
|------|------|------|
| Original md5 | `md5sum /product/media/audio/ui/camera_click.ogg` | `a3fc812e1880bf842350914657591d35` |
| Silent-file md5 | `md5sum /data/local/tmp/silent_camera.ogg` | `d7d0ebd6264bf06cb14d49d3fa3ca1fb` |
| Suppress 6 files | `mount --bind …` ×6 | `masked: ×6` ✅ |
| md5 after suppression | the same three files | all = `d7d0ebd6…` ✅ (the files really are masked) |
| Camera function | `CAPTURE_PHOTO` | `result=true`, no `MediaPlayer` error ✅ |
| Restore | `umount` ×6 | md5 returns to the original values; `/proc/mounts` leftovers = **0** ✅ |

### 3.2 App-Side Validation (after integration)

| Test | Result |
|------|------|
| Glasses app starts (rootMode=AUTO) and applies automatically | `SilentMode: apply: SILENT_MASKED=6`, `silent capture: true` ✅ |
| Phone turns silent mode off (`SET_SILENT false`) | `Success(false)` → 0 mounts, md5 back to the original values ✅ |
| Phone turns silent mode on (`SET_SILENT true`) | `Success(true)` → 6 mounts, md5 = silent file ✅ |
| Taking a photo while silent | `CAPTURE_PHOTO result=true` ✅ |
| Recording video while silent | `START_VIDEO result=true` / `STOP_VIDEO result=true` ✅ |
| Glasses-side error log | No FATAL / MediaPlayer error / mount failed ✅ |

## 4. App Integration

| Layer | Implementation |
|----|------|
| Glasses side | `ghost/SilentPayloads.kt` (generator-produced; embeds the silent file plus suppress/restore/count commands), `ghost/SilentMode.kt` (state: **on** by default, persisted in SharedPreferences, applied/restored via `root_daemon`, `refreshApplied()` verifies the actual mount count) |
| Glasses-side wiring | `AppGraph.silentMode`; `CaptureService` (applied automatically after a successful AUTO escalation); `CommandRouter` (`SET_SILENT`, and `STATUS` reports `silentEnabled`/`silentApplied`); also applied after a successful `ACQUIRE_ROOT` |
| Phone side | `Remote.setSilent()`; a new toggle under **Settings tab → Capture and permissions** (since 2026-09-13; the remote tab has a separate quick toggle) (follows the glasses state by default); `DebugReceiver`: `SET_SILENT --ez enabled` |
| Protocol | `SET_SILENT {enabled}` → `ACK{enabled, applied}`; `STATUS` gains `silentEnabled`/`silentApplied` |
| Locale | `silent_switch_label` now covers 14 locales |

## 5. Limitations and Caveats

1. **Requires root**: without root the toggle reports「尚未套用（需先取得 root）」.
2. **Must be re-applied on every boot**: bind-mounts do not survive a reboot (same lifetime as root); the app handles this automatically after AUTO escalation.
3. **Only camera/video tones are overwritten**: the other `rkd_*.ogg` system sounds are unaffected (deliberate).
4. **Third-party apps playing their own tones**: if an app plays a tone by itself (not the camera service), it is outside the scope of this mechanism.
5. **`silent_camera.ogg`** lives in `/data/local/tmp/` (it disappears after a reboot; the app recreates it automatically).
6. This mechanism is for **authorized research use** (own device; validating the privacy risk that "the camera tone can be bypassed"); do not use it on devices you are not authorized to test.

## 6. Reproduction Steps (complete)

```bash
source deploy/env.sh   # the single source for ADB/SERIAL (since 2026-09-13; previously a hard-coded path); S=<DEVICE_SERIAL>

# 1) Privilege escalation (Shizuku path)
cd /home/kali/rokid_root && ADB="$ADB" SERIAL=$S bash deploy/start_shizuku.sh
ADB="$ADB" SERIAL=$S bash deploy/test_shizuku_e2e.sh

# 2) Install the app (launching it after escalation applies silent mode automatically)
ADB="$ADB" -s $S install -r android/app-glasses/build/outputs/apk/debug/app-glasses-debug.apk
ADB="$ADB" -s $S shell am start -n com.rokid.rc.glasses/.MainActivity

# 3) Validate (mount count 6, md5 = d7d0ebd6…)
ADB="$ADB" -s $S shell "echo \"grep -c 'media/audio/ui' /proc/mounts; md5sum /product/media/audio/ui/camera_click.ogg\" | nc -U /data/local/tmp/root_daemon.sock"

# 4) Phone-side toggle
$ADB -s $SERIAL_PHONE   # set the phone serial yourself (do not hard-code) shell "am broadcast -n com.rokid.rc.phone/.DebugReceiver -a com.rokid.rc.phone.DEBUG --es cmd SET_SILENT --ez enabled false"
```
