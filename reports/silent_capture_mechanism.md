# RV101 拍照／錄影提示音壓制（靜音拍攝）機制與驗證

> English: [silent_capture_mechanism_EN.md](silent_capture_mechanism_EN.md)

> 日期：2026-09-11 · 裝置：Rokid RV101（`<DEVICE_SERIAL>`，fw 1.22.009，Android 12，kernel 5.10.209-perf）
> 狀態：**已實作於眼鏡端與手機端 App，實機驗證通過**

## 0. 結論（一句話）

RV101 的拍照/錄影提示音由 **`libcameraservice.so` 播放 `/product/media/audio/ui/` 下的 OGG 檔**；
`/product` 唯讀（dm-verity）無法改檔，但 **root 可用 `mount --bind` 把一個合法「靜音 OGG」覆蓋這些檔案**
→ 播放端讀到靜音內容（無聲），`umount` 立即還原，**reboot 自動清除**（無持久副作用）。
已整合進 App：提權後自動套用，手機端可即時開關。

## 1. 聲音來源調查（實機）

### 1.1 播放端（誰在放音）

```bash
strings /system/lib64/libcameraservice.so | grep -iE 'camera_click|VideoRecord|audio/ui|playSound'
```

實測輸出（節錄）：

```
_ZN7android13CameraService9playSoundENS0_10sound_kindE     ← CameraService::playSound(sound_kind)
playSound(%d)
/product/media/audio/ui/camera_click.ogg                    ← 拍照快門（本機實際路徑）
/product/media/audio/ui/VideoStop.ogg
/system/media/audio/ui/VideoRecord.ogg
```

- **相機服務自己播音**（不是 App 播的）→ App 端 API 無法關閉，必須在檔案或系統層處理。
- 本機所有音效檔實際都位於 **`/product/media/audio/ui/`**（`/system/media/audio/ui/` 為空）。

### 1.2 目標檔案（6 個）

| 檔案 | 用途 |
|------|------|
| `camera_click.ogg` | 拍照快門 |
| `camera_focus.ogg` | 對焦 |
| `VideoRecord.ogg` | 錄影開始 |
| `VideoStop.ogg` | 錄影停止 |
| `rkd_record_start.ogg` | **Rokid 自訂**錄影開始提示 |
| `rkd_record_end.ogg` | **Rokid 自訂**錄影結束提示 |

> `rkd_*.ogg` 未被 `libcameraservice` 引用（由 Rokid App/服務播放）；一併覆蓋以求完整。
> 同目錄另有 50+ 個 `rkd_*.ogg` 系統 UI 音效（按鍵/通知等），**不在壓制範圍**。

### 1.3 系統屬性

`getprop | grep -iE 'camera.*sound|shutter|forced'` → **無輸出**（本機未設 `ro.camera.sound.forced`）。
即使在其他生產號上該屬性為 1（強制快門聲），**檔案層覆蓋仍然有效**，因為播放端讀的就是這些檔案。

## 2. 壓制方法

### 2.1 為什麼是 bind-mount

| 方案 | 可行性 |
|------|--------|
| 直接改 `/product/media/audio/ui/*.ogg` | ❌ 分區唯讀（dm-verity），`remount rw` 失敗 |
| 刪除 APK/停用服務 | ❌ 播音者是 `libcameraservice`（system 服務），停掉等於停相機 |
| 系統層靜音（音量歸零） | ❌ 會影響其他音訊（非針對性） |
| **bind-mount 靜音檔覆蓋** | ✅ root 可做、即時生效、可還原、reboot 自動清除 |

### 2.2 靜音檔

以 ffmpeg（imageio-ffmpeg 內建）產生 **合法 Ogg Vorbis 靜音**（mono 44100Hz 0.3s，3584 bytes），
避免用 0-byte/`/dev/null` 造成播放端 prepare 失敗：

```bash
FF=.venv/lib/python3.13/site-packages/imageio_ffmpeg/binaries/ffmpeg-linux-x86_64-v7.0.2
$FF -y -f lavfi -i anullsrc=r=44100:cl=mono -t 0.3 -c:a libvorbis -q:a 0 reports/media/silent_camera.ogg
```

此檔以 base64（4780 字元）內嵌進 App，由 `scripts/gen_silent_payload.py` 產生
`app-glasses/.../ghost/SilentPayloads.kt`（請勿手改該檔）。

### 2.3 實際命令

```sh
# 壓制（每個存在的目標檔；已掛載者跳過）
F=/data/local/tmp/silent_camera.ogg
printf '%s' '<base64>' | base64 -d > $F; chmod 644 $F
for d in /product/media/audio/ui /system/media/audio/ui /vendor/media/audio/ui; do
  for f in camera_click.ogg camera_focus.ogg VideoRecord.ogg VideoStop.ogg rkd_record_start.ogg rkd_record_end.ogg; do
    t=$d/$f; [ -f "$t" ] || continue
    grep -qF " $t " /proc/mounts 2>/dev/null && continue
    mount --bind $F $t
  done
done

# 還原
umount <每個被掛載的目標>
```

## 3. 驗證證據（2026-09-11 實機）

### 3.1 手動驗證（root shell）

| 步驟 | 命令 | 結果 |
|------|------|------|
| 原始 md5 | `md5sum /product/media/audio/ui/camera_click.ogg` | `a3fc812e1880bf842350914657591d35` |
| 靜音檔 md5 | `md5sum /data/local/tmp/silent_camera.ogg` | `d7d0ebd6264bf06cb14d49d3fa3ca1fb` |
| 壓制 6 檔 | `mount --bind …` ×6 | `masked: ×6` ✅ |
| 壓制後 md5 | 同上三檔 | 全部 = `d7d0ebd6…` ✅（檔案確被遮蔽） |
| 相機功能 | `CAPTURE_PHOTO` | `result=true`、無 `MediaPlayer` 錯誤 ✅ |
| 還原 | `umount` ×6 | md5 回復原值；`/proc/mounts` 殘留 = **0** ✅ |

### 3.2 App 端驗證（整合後）

| 測試 | 結果 |
|------|------|
| 眼鏡 App 啟動（rootMode=AUTO）自動套用 | `SilentMode: apply: SILENT_MASKED=6`、`silent capture: true` ✅ |
| 手機關閉靜音（`SET_SILENT false`） | `Success(false)` → 掛載 0、md5 回原值 ✅ |
| 手機開啟靜音（`SET_SILENT true`） | `Success(true)` → 掛載 6、md5 = 靜音檔 ✅ |
| 靜音狀態下拍照 | `CAPTURE_PHOTO result=true` ✅ |
| 靜音狀態下錄影 | `START_VIDEO result=true` / `STOP_VIDEO result=true` ✅ |
| 眼鏡端錯誤日誌 | 無 FATAL / MediaPlayer error / mount failed ✅ |

## 4. App 整合

| 層 | 實作 |
|----|------|
| 眼鏡端 | `ghost/SilentPayloads.kt`（產生器產生，內嵌靜音檔 + 壓制/還原/計數命令）、`ghost/SilentMode.kt`（狀態：預設**開啟**、SharedPreferences 持久化、經 `root_daemon` 套用/還原、`refreshApplied()` 實際掛載數驗證） |
| 眼鏡端接線 | `AppGraph.silentMode`；`CaptureService`（AUTO 提權成功後自動套用）；`CommandRouter`（`SET_SILENT` + `STATUS` 回報 `silentEnabled`/`silentApplied`）；`ACQUIRE_ROOT` 成功後亦套用 |
| 手機端 | `Remote.setSilent()`；**「設定」分頁 → 拍攝與權限**新增開關（2026-09-13 起；遙控分頁另有快速開關）（預設跟隨眼鏡狀態）；`DebugReceiver`：`SET_SILENT --ez enabled` |
| 協定 | `SET_SILENT {enabled}` → `ACK{enabled, applied}`；`STATUS` 新增 `silentEnabled`/`silentApplied` |
| 語系 | `silent_switch_label` 已補 14 語系 |

## 5. 限制與注意

1. **需要 root**：無 root 時開關會回報「尚未套用（需先取得 root）」。
2. **每 boot 需重新套用**：bind-mount 不跨 reboot（與 root 同生命週期）；App 於 AUTO 提權後自動處理。
3. **僅覆蓋相機/錄影提示音**：其他 `rkd_*.ogg` 系統音效不受影響（刻意）。
4. **第三方 App 自播音效**：若有 App 自行播放提示音（非 camera service），不在本機制範圍。
5. **`silent_camera.ogg`** 位於 `/data/local/tmp/`（reboot 後消失，App 會自動重建）。
6. 本機制屬**授權研究用途**（自有設備、驗證「提示音可被繞過」的隱私風險）；請勿用於未授權設備。

## 6. 重現步驟（完整）

```bash
source deploy/env.sh   # ADB/SERIAL 唯一來源（2026-09-13 起；原為寫死路徑）; S=<DEVICE_SERIAL>

# 1) 提權（Shizuku 路徑）
cd /home/kali/rokid_root && ADB="$ADB" SERIAL=$S bash deploy/start_shizuku.sh
ADB="$ADB" SERIAL=$S bash deploy/test_shizuku_e2e.sh

# 2) 安裝 App（提權後啟動即自動套用靜音）
ADB="$ADB" -s $S install -r android/app-glasses/build/outputs/apk/debug/app-glasses-debug.apk
ADB="$ADB" -s $S shell am start -n com.rokid.rc.glasses/.MainActivity

# 3) 驗證（掛載數 6、md5 = d7d0ebd6…）
ADB="$ADB" -s $S shell "echo \"grep -c 'media/audio/ui' /proc/mounts; md5sum /product/media/audio/ui/camera_click.ogg\" | nc -U /data/local/tmp/root_daemon.sock"

# 4) 手機端開關
$ADB -s $SERIAL_PHONE   # 手機序號請自行設定（勿寫死） shell "am broadcast -n com.rokid.rc.phone/.DebugReceiver -a com.rokid.rc.phone.DEBUG --es cmd SET_SILENT --ez enabled false"
```
