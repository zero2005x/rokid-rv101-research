#!/usr/bin/env python3
"""gen_silent_payload.py — generate the glasses-side SilentPayloads.kt (embedded base64) from a
silent OGG.

Why generate it: the glasses side suppresses the photo/video shutter sound by using root to
bind-mount a "legitimate silent OGG" over `/product/media/audio/ui/*.ogg`; that silent file must
be embedded in the App (no external file needed, shared by the Shizuku and bridge paths). The
audio file is tiny (3584 B -> 4780 base64 characters).

Usage:
    python3 scripts/gen_silent_payload.py
Input:  reports/media/silent_camera.ogg
Output: android/app-glasses/src/main/kotlin/com/rokid/rc/glasses/ghost/SilentPayloads.kt
(Regenerate the silent file with imageio-ffmpeg's ffmpeg)
    FF=.venv/lib/python3.13/site-packages/imageio_ffmpeg/binaries/ffmpeg-linux-x86_64-v7.0.2
    $FF -y -f lavfi -i anullsrc=r=44100:cl=mono -t 0.3 -c:a libvorbis -q:a 0 reports/media/silent_camera.ogg
"""
import base64
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OGG = ROOT / "reports/media/silent_camera.ogg"
OUT = ROOT / "android/app-glasses/src/main/kotlin/com/rokid/rc/glasses/ghost/SilentPayloads.kt"

TEMPLATE = '''package com.rokid.rc.glasses.ghost

/**
 * 拍照/錄影提示音壓制（root）。
 *
 * 機制（2026-09-11 實機驗證，見 reports/silent_capture_mechanism.md）：
 *  - `libcameraservice.so` 播放 `/product/media/audio/ui/` 下的
 *    `camera_click.ogg`（拍照）、`VideoRecord.ogg` / `VideoStop.ogg`（錄影）；
 *    Rokid 另含 `camera_focus.ogg`、`rkd_record_start.ogg`、`rkd_record_end.ogg`。
 *  - `/product` 唯讀無法改檔，但 root 可 **bind-mount** 合法靜音 OGG 覆蓋這些檔案
 *    → 播放端讀到靜音內容（無聲）；`umount` 還原；**reboot 自動清除**（無持久副作用）。
 *  - 實測：壓制後 md5 == 靜音檔；拍照功能正常（無 MediaPlayer 錯誤）；
 *    umount 後 md5 復原；`/proc/mounts` 無殘留。
 *
 * 本檔由 `scripts/gen_silent_payload.py` 產生（請勿手改；改靜音檔後重跑）。
 */
object SilentPayloads {

    /** Ogg Vorbis 靜音檔（mono 44100Hz 0.3s，3584 bytes）base64。 */
    private val SILENT_OGG_B64: String =
@@B64@@

    val SILENT_PATH = GlassesPaths.SILENT_OGG

    private const val DIRS = "/product/media/audio/ui /system/media/audio/ui /vendor/media/audio/ui"
    private const val NAMES =
        "camera_click.ogg camera_focus.ogg VideoRecord.ogg VideoStop.ogg rkd_record_start.ogg rkd_record_end.ogg"

    /** 一次性把靜音檔寫入裝置（root 執行；已存在則跳過）。 */
    fun writeOggCommand(): String = buildString {
        append("F=").append(SILENT_PATH).append("; ")
        append("if [ ! -s \\"\\$F\\" ]; then printf '%s' '").append(SILENT_OGG_B64)
        append("' | base64 -d > \\"\\$F\\" 2>/dev/null; chmod 644 \\"\\$F\\"; fi; ")
        append("[ -s \\"\\$F\\" ] && echo SILENT_FILE_OK || echo SILENT_FILE_ERR")
    }

    /** 壓制：對每個存在的音效檔 bind-mount 靜音檔（已掛載者跳過）。 */
    fun applyCommand(): String = """
        F=$SILENT_PATH
        [ -s "${'$'}F" ] || { echo SILENT_FILE_MISSING; }
        n=0
        for d in $DIRS; do
          for f in $NAMES; do
            t="${'$'}d/${'$'}f"
            [ -f "${'$'}t" ] || continue
            grep -qF " ${'$'}t " /proc/mounts 2>/dev/null && continue
            mount --bind "${'$'}F" "${'$'}t" 2>/dev/null && n=$((n+1))
          done
        done
        echo "SILENT_MASKED=${'$'}n"
    """.trimIndent()

    /** 還原：umount 所有被壓制的音效檔（清除可能的重疊掛載）。 */
    fun restoreCommand(): String = """
        n=0
        for d in $DIRS; do
          for f in $NAMES; do
            t="${'$'}d/${'$'}f"
            i=0
            while grep -qF " ${'$'}t " /proc/mounts 2>/dev/null && [ ${'$'}i -lt 3 ]; do
              umount "${'$'}t" 2>/dev/null || break
              i=$((i+1)); n=$((n+1))
            done
          done
        done
        echo "SILENT_UNMOUNTED=${'$'}n"
    """.trimIndent()

    /** 目前被壓制的音效檔數量（驗證用）。 */
    fun countCommand(): String = "grep -c 'media/audio/ui' /proc/mounts 2>/dev/null || echo 0"
}
'''


def main() -> int:
    if not OGG.exists():
        print(f"[x] 找不到靜音檔：{OGG}", file=sys.stderr)
        return 1
    b64 = base64.b64encode(OGG.read_bytes()).decode()
    if len(b64) > 7900:  # root_daemon CMD_MAX = 8192
        print(f"[x] base64 過長（{len(b64)}），需縮短靜音檔", file=sys.stderr)
        return 1
    rows = [b64[i:i + 100] for i in range(0, len(b64), 100)]
    body = "\n".join(
        f'        "{r}"' + (" +" if i < len(rows) - 1 else "") for i, r in enumerate(rows)
    )
    # placeholder replacement (the template contains many % and {}, so %-format/format
    # cannot be used)
    OUT.write_text(TEMPLATE.replace("@@B64@@", body), encoding="utf-8")
    print(f"[+] 已產生 {OUT.relative_to(ROOT)}（ogg {OGG.stat().st_size} B → base64 {len(b64)} 字元）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
