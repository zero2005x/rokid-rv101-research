package com.rokid.rc.glasses.ghost

import java.io.File
import com.rokid.rc.glasses.GlassesPaths

/**
 * 相機隱私 LED 壓制/還原的 root payload 模板。
 *
 * 依據既有測試腳本結論（led_test.log Path A）：root 下直寫
 * `/sys/class/leds/white/brightness` / `led_current` 即可控制 LED。
 * 為避免相機開啟事件（lights_ctrl `EVENT_WARN_ID_CAMERA_OPEN`）在拍攝中重新點亮，
 * suppress 會 fork 一個背景迴圈，在 marker 檔案存在期間持續把 LED 壓在 0。
 */
object LedPayloads {

    private val LED_BRIGHT = GlassesPaths.ledBrightness("white")
    private const val LED_CURRENT = GlassesPaths.LED_WHITE_CURRENT

    private val LED_PATHS = mapOf(
        "white" to GlassesPaths.ledBrightness("white"),
        "green" to GlassesPaths.ledBrightness("green"),
        "blue" to GlassesPaths.ledBrightness("blue"),
        "red" to GlassesPaths.ledBrightness("red")
    )

    /** 開始壓制：立即壓 0 + 取消 lights_ctrl camera-open 事件（根治閃爍）+ 背景保持迴圈。 */
    fun suppress(marker: File, payloadLog: File): RootScript {
        val content = """
            #!/system/bin/sh
            M=${marker.absolutePath}
            service call lights_ctrl 9 i32 2 i32 2014 >/dev/null 2>&1
            echo 0 > $LED_BRIGHT 2>/dev/null
            echo 0 > $LED_CURRENT 2>/dev/null
            ( while [ -f "${'$'}M" ]; do
                echo 0 > $LED_BRIGHT 2>/dev/null
                echo 0 > $LED_CURRENT 2>/dev/null
                sleep 0.2
              done ) >/dev/null 2>&1 &
            echo "suppressed brightness=$(cat $LED_BRIGHT 2>&1)" > ${payloadLog.absolutePath}
            echo ${PayloadMarkers.DONE} >> ${payloadLog.absolutePath}
        """.trimIndent()
        return RootScript("led_suppress", content)
    }

    /** 還原：LED 回到相機開啟狀態（brightness=255, led_current=2，並重發 camera-open 事件）。 */
    fun restore(payloadLog: File): RootScript {
        val content = """
            #!/system/bin/sh
            echo 255 > $LED_BRIGHT 2>/dev/null
            echo 2 > $LED_CURRENT 2>/dev/null
            service call lights_ctrl 8 i32 2 i32 2014 >/dev/null 2>&1
            echo ${PayloadMarkers.DONE} > ${payloadLog.absolutePath}
        """.trimIndent()
        return RootScript("led_restore", content)
    }

    /** 驗證/取得 root：把 id 寫到 log 並標記 DONE。 */
    fun verifyRoot(doneLog: File): RootScript {
        val content = """
            #!/system/bin/sh
            id > ${doneLog.absolutePath}
            echo ${PayloadMarkers.DONE} >> ${doneLog.absolutePath}
        """.trimIndent()
        return RootScript("verify_root", content)
    }

    /** 提權成功後啟動常駐 root daemon（並驗證 root）。 */
    /**
     * 啟動 root daemon，並**加上存活強化（P0）**：
     *  1. 寫 pid 檔（後續健康檢查／清理可精準鎖定）
     *  2. `oom_score_adj=-1000`：避免被 lmkd 在記憶體壓力下回收
     *  3. 安裝並啟動 **root 常駐 keep-alive 迴圈**：daemon 崩潰時自動重建（不需 adb／不需重跑 exploit）
     *
     * 注意（踩過的坑）：reboot 後 `/data/local/tmp` 會殘留 `root_daemon.sock`「檔案」但沒有 listener，
     * 只檢查 `[ -S ]` 會誤判 → 一律用 `/proc/net/unix`（只列有 listener 的 socket）判斷存活。
     */
    fun startRootDaemon(doneLog: File, daemonPath: String = GlassesPaths.ROOT_DAEMON_BIN): RootScript {
        // 注意：這是 Kotlin 原始字串，shell 的 $ 需以 ${'$'} 轉義
        val content = """
            #!/system/bin/sh
            echo 0 > /sys/fs/selinux/enforce 2>/dev/null

            KA=${GlassesPaths.ROOT_DAEMON_KEEPALIVE_SH}
            PID=${GlassesPaths.ROOT_DAEMON_PID}
            SOCK=${GlassesPaths.ROOT_DAEMON_SOCK}
            LOG=${GlassesPaths.ROOT_DAEMON_LOG}

            # ---- 1) keep-alive 迴圈（root 常駐；daemon 掛掉就重建）----
            cat > "${'$'}KA" <<'KEEPALIVE'
            #!/system/bin/sh
            # root daemon 存活看守（P0）。daemon 崩潰/被回收時自動重建，並持續維持低 OOM 分數。
            PID=${GlassesPaths.ROOT_DAEMON_PID}
            SOCK=${GlassesPaths.ROOT_DAEMON_SOCK}
            BIN=${GlassesPaths.ROOT_DAEMON_BIN}
            LOG=${GlassesPaths.ROOT_DAEMON_LOG}
            while true; do
              if ! grep -q 'root_daemon.sock' /proc/net/unix 2>/dev/null; then
                rm -f "${'$'}SOCK"
                setsid "${'$'}BIN" >>"${'$'}LOG" 2>&1 &
                echo ${'$'}! > "${'$'}PID"
                echo "${'$'}(date '+%F %T') keepalive: daemon restarted pid=${'$'}(cat ${'$'}PID 2>/dev/null)" >> "${'$'}LOG"
              fi
              # pid 檔可能殘留舊值 → 由 ps 反查真實 PID（keepalive 以 root 執行）
              ACTUAL=${'$'}(ps -A -o PID,ARGS 2>/dev/null | grep '[r]oot_daemon${'$'}' | awk '{print ${'$'}1}' | head -1)
              [ -n "${'$'}ACTUAL" ] && echo "${'$'}ACTUAL" > "${'$'}PID"
              for p in ${'$'}(cat "${'$'}PID" 2>/dev/null) ${'$'}${'$'}; do
                echo -1000 > /proc/${'$'}p/oom_score_adj 2>/dev/null
              done
              sleep 20
            done
            KEEPALIVE
            chmod 755 "${'$'}KA"

            # ---- 2) daemon 本體 ----
            if ! grep -q 'root_daemon.sock' /proc/net/unix 2>/dev/null; then
              rm -f "${'$'}SOCK"
              setsid ${daemonPath} >>"${'$'}LOG" 2>&1 &
              echo ${'$'}! > "${'$'}PID"
            fi
            for i in $(seq 1 20); do
              grep -q 'root_daemon.sock' /proc/net/unix 2>/dev/null && break
              sleep 0.2
            done

            # ---- 3) 啟動 keep-alive（已在跑就不重複）----
            if ! ps -A -o ARGS 2>/dev/null | grep -q '[r]oot_daemon_keepalive.sh'; then
              setsid sh "${'$'}KA" >/dev/null 2>&1 &
              echo ${'$'}! > ${GlassesPaths.ROOT_DAEMON_KEEPALIVE_PID}
            fi
            # daemon 與 keep-alive 都設為最不易被回收（pid 檔先校正）
            ACTUAL=${'$'}(ps -A -o PID,ARGS 2>/dev/null | grep '[r]oot_daemon${'$'}' | awk '{print ${'$'}1}' | head -1)
            [ -n "${'$'}ACTUAL" ] && echo "${'$'}ACTUAL" > "${'$'}PID"
            for p in $(cat "${'$'}PID" 2>/dev/null) $(cat ${GlassesPaths.ROOT_DAEMON_KEEPALIVE_PID} 2>/dev/null); do
              echo -1000 > /proc/${'$'}p/oom_score_adj 2>/dev/null
            done

            id > ${doneLog.absolutePath}
            echo ${PayloadMarkers.DONE} >> ${doneLog.absolutePath}
        """.trimIndent()
        return RootScript("start_root_daemon", content)
    }

    /** 指定顏色 LED 開/關。color ∈ white/green/blue/red/all。 */
    fun ledColor(color: String, on: Boolean, marker: File?, payloadLog: File): RootScript {
        val value = if (on) "255" else "0"
        val targets = if (color == "all") {
            LED_PATHS.values.toList()
        } else {
            listOf(LED_PATHS[color] ?: LED_PATHS["white"]!!)
        }
        val writes = targets.joinToString("\n") { "echo $value > $it 2>/dev/null" }
        // white（或 all）關燈時：取消 camera-open 事件（根治閃爍）+ hold loop 保險
        val hold = if (!on && marker != null && (color == "white" || color == "all")) {
            """
            service call lights_ctrl 9 i32 2 i32 2014 >/dev/null 2>&1
            M=${marker.absolutePath}
            ( while [ -f "${'$'}M" ]; do
                echo 0 > $LED_BRIGHT 2>/dev/null
                sleep 0.2
              done ) >/dev/null 2>&1 &
            """.trimIndent()
        } else {
            ""
        }
        val content = """
            #!/system/bin/sh
            $writes
            $hold
            echo ${PayloadMarkers.DONE} > ${payloadLog.absolutePath}
        """.trimIndent()
        return RootScript("led_${color}_${if (on) "on" else "off"}", content)
    }
}
