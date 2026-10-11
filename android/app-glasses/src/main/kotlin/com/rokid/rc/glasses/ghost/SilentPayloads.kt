package com.rokid.rc.glasses.ghost

import com.rokid.rc.glasses.GlassesPaths

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
        "T2dnUwACAAAAAAAAAAADU1xuAAAAAOtP9IIBHgF2b3JiaXMAAAAAAUSsAAAAAAAAgLsAAAAAAAC4AU9nZ1MAAAAAAAAAAAAAA1Nc" +
        "bgEAAADGHKlMDz7/////////////////MgN2b3JiaXMMAAAATGF2ZjYxLjEuMTAwAQAAAB4AAABlbmNvZGVyPUxhdmM2MS4zLjEw" +
        "MCBsaWJ2b3JiaXMBBXZvcmJpcx9CQ1YBAAABABhjVClGmVLSSokZc5QxRplikkqJpYQWQkidcxRTqTnXnGusubUghBAaU1ApBZlS" +
        "jlJpGWOQKQWZUhBLSSV0EjonnWMQW0nB1phri0G2HIQNmlJMKcSUUopCCBlTjCnFlFJKQgcldA465hxTjkooQbicc6u1lpZji6l0" +
        "kkrnJGRMQkgphZJKB6VTTkJINZbWUikdc1JSakHoIIQQQrYghA2C0JBVAAABAMBAEBqyCgBQAAAQiqEYigKEhqwCADIAAASgKI7i" +
        "KI4jOZJjSRYQGrIKAAACABAAAMBwFEmRFMmxJEvSLEvTRFFVfdU2VVX2dV3XdV3XdSA0ZBUAAAEAQEinmaUaIMIMZBgIDVkFACAA" +
        "AABGKMIQA0JDVgEAAAEAAGIoOYgmtOZ8c46DZjloKsXmdHAi1eZJbirm5pxzzjknm3PGOOecc4pyZjFoJrTmnHMSg2YpaCa05pxz" +
        "nsTmQWuqtOacc8Y5p4NxRhjnnHOatOZBajbW5pxzFrSmOWouxeaccyLl5kltLtXmnHPOOeecc84555xzqhenc3BOOOecc6L25lpu" +
        "QhfnnHM+Gad7c0I455xzzjnnnHPOOeecc4LQkFUAABAAAEEYNoZxpyBIn6OBGEWIacikB92jwyRoDHIKqUejo5FS6iCUVMZJKZ0g" +
        "NGQVAAAIAAAhhBRSSCGFFFJIIYUUUoghhhhiyCmnnIIKKqmkoooyyiyzzDLLLLPMMuuws8467DDEEEMMrbQSS0211VhjrbnnnGsO" +
        "0lpprbXWSimllFJKKQgNWQUAgAAAEAgZZJBBRiGFFFKIIaaccsopqKACQkNWAQCAAAACAAAAPMlzREd0REd0REd0REd0RMdzPEeU" +
        "REmUREm0TMvUTE8VVdWVXVvWZd32bWEXdt33dd/3dePXhWFZlmVZlmVZlmVZlmVZlmVZgtCQVQAACAAAgBBCCCGFFFJIIaUYY8wx" +
        "56CTUEIgNGQVAAAIACAAAADAURzFcSRHciTJkixJkzRLszzN0zxN9ERRFE3TVEVXdEXdtEXZlE3XdE3ZdFVZtV1Ztm3Z1m1flm3f" +
        "933f933f933f933f93UdCA1ZBQBIAADoSI6kSIqkSI7jOJIkAaEhqwAAGQAAAQAoiqM4juNIkiRJlqRJnuVZomZqpmd6qqgCoSGr" +
        "AABAAAABAAAAAAAomuIppuIpouI5oiNKomVaoqZqriibsuu6ruu6ruu6ruu6ruu6ruu6ruu6ruu6ruu6ruu6ruu6ruu6LhAasgoA" +
        "kAAA0JEcyZEcSZEUSZEcyQFCQ1YBADIAAAIAcAzHkBTJsSxL0zzN0zxN9ERP9ExPFV3RBUJDVgEAgAAAAgAAAAAAMCTDUixHczRJ" +
        "lFRLtVRNtVRLFVVPVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVNU3TNE0gNGQlAAAEAMBijcHlICElJeXeEMIQk54x" +
        "JiG1XiEEkZLeMQYVg54yogxy3kLjEIMeCA1ZEQBEAQAAxiDHEHPIOUepkxI556h0lBrnHKWOUmcpxZhizSiV2FKsjXOOUketo5Ri" +
        "LC12lFKNqcYCAAACHAAAAiyEQkNWBABRAACEMUgppBRijDmnnEOMKeeYc4Yx5hxzjjnnoHRSKuecdE5KxBhzjjmnnHNSOieVc05K" +
        "J6EAAIAABwCAAAuh0JAVAUCcAIBBkjxP8jRRlDRPFEVTdF1RNF3X8jzV9ExTVT3RVFVTVW3ZVFVZljzPND3TVFXPNFXVVFVZNlVV" +
        "lkVV1W3TdXXbdFXdlm3b911bFnZRVW3dVF3bN1XX9l3Z9n1Z1nVj8jxV9UzTdT3TdGXVdW1bdV1d90xTlk3XlWXTdW3blWVdd2XZ" +
        "9zXTdF3TVWXZdF3ZdmVXt11Z9n3TdYXflWVfV2VZGHZd94Vb15XldF3dV2VXN1ZZ9n1b14Xh1nVhmTxPVT3TdF3PNF1XdV1fV13X" +
        "1jXTlGXTdW3ZVF1ZdmXZ911X1nXPNGXZdF3bNl1Xll1Z9n1XlnXddF1fV2VZ+FVX9nVZ15Xh1m3hN13X91VZ9oVXlnXh1nVhuXVd" +
        "GD5V9X1TdoXhdGXf14XfWW5dOJbRdX1hlW3hWGVZOX7hWJbd95VldF1fWG3ZGFZZFoZf+J3l9n3jeHVdGW7d58y67wzH76T7ytPV" +
        "bWOZfd1ZZl93juEYOr/w46mqr5uuKwynLAu/7evGs/u+soyu6/uqLAu/KtvCseu+8/y+sCyj7PrCasvCsNq2Mdy+biy/cBzLa+vK" +
        "Meu+UbZ1fF94CsPzdHVdeWZdx/Z1dONHOH7KAACAAQcAgAATykChISsCgDgBAI8kiaJkWaIoWZYoiqbouqJouq6kaaapaZ5pWppn" +
        "mqZpqrIpmq4saZppWp5mmpqnmaZomq5rmqasiqYpy6ZqyrJpmrLsurJtu65s26JpyrJpmrJsmqYsu7Kr267s6rqkWaapeZ5pap5n" +
        "mqZqyrJpmq6reZ5qep5oqp4oqqpqqqqtqqosW55nmproqaYniqpqqqatmqoqy6aq2rJpqrZsqqptu6rs+rJt67ppqrJtqqYtm6pq" +
        "267s6rIs27ovaZppap5nmprnmaZpmrJsmqorW56nmp4oqqrmiaZqqqosm6aqypbnmaoniqrqiZ5rmqoqy6Zq2qppmrZsqqotm6Yq" +
        "y65t+77ryrJuqqpsm6pq66ZqyrJsy77vyqruiqYpy6aq2rJpqrIt27Lvy7Ks+6JpyrJpqrJtqqouy7JtG7Ns+7pomrJtqqYtm6oq" +
        "27It+7os27rvyq5vq6qs67It+7ru+q5w67owvLJs+6qs+ror27pv6zLb9n1E05RlUzVt21RVWXZl2fZl2/Z90TRtW1VVWzZN1bZl" +
        "WfZ9WbZtYTRN2TZVVdZN1bRtWZZtYbZl4XZl2bdlW/Z115V1X9d949dl3ea6su3Lsq37qqv6tu77wnDrrvAKAAAYcAAACDChDBQa" +
        "shIAiAIAAIxhjDEIjVLOOQehUco55yBkzkEIIZXMOQghlJI5B6GUlDLnIJSSUgihlJRaCyGUlFJrBQAAFDgAAATYoCmxOEChISsB" +
        "gFQAAIPjWJbnmaJq2rJjSZ4niqqpqrbtSJbniaJpqqptW54niqapqq7r65rniaJpqqrr6rpomqapqq7ruroumqKpqqrrurKum6aq" +
        "qq4ru7Ls66aqqqrryq4s+8Kquq4ry7Jt68Kwqq7ryrJs27Zv3Lqu677v+8KRreu6LvzCMQxHAQDgCQ4AQAU2rI5wUjQWWGjISgAg" +
        "AwCAMAYhgxBCBiGEkFJKIaWUEgAAMOAAABBgQhkoNGRFABAnAAAYQymklFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSCmllFJK" +
        "KaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSqmklFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJK" +
        "KaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJK" +
        "KaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJK" +
        "KaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimVUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkop" +
        "pZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFIKAJCKcACQejChDBQashIASAUAAIxR" +
        "SinGnIMQMeYYY9BJKClizDnGHJSSUuUchBBSaS23yjkIIaTUUm2Zc1JaizHmGDPnpKQUW805h1JSi7HmmmvupLRWa64151paqzXX" +
        "nHPNubQWa64515xzyzHXnHPOOecYc84555xzzgUA4DQ4AIAe2LA6wknRWGChISsBgFQAAAIZpRhzzjnoEFKMOecchBAihRhzzjkI" +
        "IVSMOeccdBBCqBhzzDkIIYSQOecchBBCCCFzDjroIIQQQgcdhBBCCKGUzkEIIYQQSighhBBCCCGEEDoIIYQQQgghhBBCCCGEUkoI" +
        "IYQQQgmhlFAAAGCBAwBAgA2rI5wUjQUWGrISAAACAIAclqBSzoRBjkGPDUHKUTMNQkw50ZliTmozFVOQORCddBIZakHZXjILAACA" +
        "IAAgwAQQGCAo+EIIiDEAAEGIzBAJhVWwwKAMGhzmAcADRIREAJCYoEi7uIAuA1zQxV0HQghCEIJYHEABCTg44YYn3vCEG5ygU1Tq" +
        "IAAAAAAADADgAQDgoAAiIpqrsLjAyNDY4OjwCAAAAAAAFgD4AAA4PoCIiOYqLC4wMjQ2ODo8AgAAAAAAAAAAgICAAAAAAABAAAAA" +
        "gIBPZ2dTAASuMwAAAAAAAANTXG4CAAAACcPPOA8BAQEBAQEBAQEBAQEBAQEACg4ODg4ODg4ODg4ODg4="

    val SILENT_PATH = GlassesPaths.SILENT_OGG

    private const val DIRS = "/product/media/audio/ui /system/media/audio/ui /vendor/media/audio/ui"
    private const val NAMES =
        "camera_click.ogg camera_focus.ogg VideoRecord.ogg VideoStop.ogg rkd_record_start.ogg rkd_record_end.ogg"

    /** 一次性把靜音檔寫入裝置（root 執行；已存在則跳過）。 */
    fun writeOggCommand(): String = buildString {
        append("F=").append(SILENT_PATH).append("; ")
        append("if [ ! -s \"\$F\" ]; then printf '%s' '").append(SILENT_OGG_B64)
        append("' | base64 -d > \"\$F\" 2>/dev/null; chmod 644 \"\$F\"; fi; ")
        append("[ -s \"\$F\" ] && echo SILENT_FILE_OK || echo SILENT_FILE_ERR")
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
