package com.rokid.rc.glasses.ring

import android.util.Log
import com.rokid.rc.glasses.GlassesPaths
import com.rokid.rc.glasses.ghost.RootDaemonClient
import com.rokid.rc.shared.ring.RingIdentity

/**
 * 路徑 B：眼鏡端以 **root 直讀 evdev**（`getevent`）取得手勢。
 *
 * 為什麼用 getevent 而不是自製 native binary：
 *   - App 是 untrusted_app，讀不到 dev/input 節點；但 root daemon 可以。
 *   - `getevent` 是裝置內建的 evdev 讀取器，免去隨 APK 附帶 aarch64 二進位與其建置鏈。
 *   - daemon 的 exec 是「指令結束才回傳」，因此做法是：**背景**啟動 getevent 將事件
 *     追加到 `/data/local/tmp/ring_events.log`，App 再以固定間隔 `tail -c +offset`
 *     取回增量（每次 exec 很短，事件延遲 ≈ 輪詢間隔）。
 *
 * 事件格式（`getevent -lt`）：
 *   `[   12345.678901] /dev/input/event1: EV_KEY       BTN_TOUCH            DOWN`
 */
class RingEvdevReader(
    private val root: RootDaemonClient = RootDaemonClient(),
    private val logPath: String = GlassesPaths.RING_EVENTS_LOG,
    private val pidPath: String = GlassesPaths.RING_READER_PID
) {

    private var offset = 0L
    private var device: String? = null

    /** 列出可用的輸入裝置：回傳 (路徑, 名稱)。 */
    suspend fun listDevices(): List<Pair<String, String>> {
        val out = root.exec("getevent -pl 2>/dev/null | grep -E 'add device|name:'", 20_000L)
        val list = mutableListOf<Pair<String, String>>()
        var pending: String? = null
        out.lineSequence().forEach { line ->
            val l = line.trim()
            when {
                l.startsWith("add device") -> pending = l.substringAfter(":").trim()
                l.startsWith("name:") && pending != null -> {
                    list += pending!! to l.substringAfter("name:").trim().trim('"')
                    pending = null
                }
            }
        }
        return list
    }

    /**
     * 檢查某個 evdev 路徑是否**真的存在**（裝置可能已解除配對／被移除）。
     *
     * ⚠ `listDevices()` 是走 root daemon（`getevent -pl`）；**沒有 root 時它一律回空清單**。
     * 若把「空清單」當成「裝置不存在」，就會在沒有 root 的情況下**誤刪使用者已設定的裝置**
     * （實測踩到：19:40 無 root 啟動時把 `/dev/input/event1` 誤判為不存在而清掉）。
     * 因此列舉不到（空清單）時一律回 true —— 無法確認就不動它。
     */
    suspend fun deviceExists(path: String): Boolean {
        val all = listDevices()
        if (all.isEmpty()) return true
        return all.any { it.first == path }
    }

    /** 依名稱子字串自動挑裝置（大小寫無關）；找不到回傳 null。 */
    suspend fun autoDetect(namePattern: String = RingIdentity.DEFAULT_NAME_PATTERN): Pair<String, String>? {
        val all = listDevices()
        return all.firstOrNull { it.second.contains(namePattern, true) }
            ?: all.firstOrNull { it.second.contains(RingIdentity.FALLBACK_NAME_PATTERN, true) }
    }

    /**
     * 啟動背景 getevent（root）。成功回傳 true。
     *
     * 注意：**同一行指令內不可同時 pkill 與啟動 getevent**——`pkill -f` 會比對到本行
     * 指令自身（後半段含 `getevent -lt` 字面）而自殺，導致後面全部沒跑（實測踩到）。
     * 因此：停止用獨立指令（`[g]etevent` 括號技巧 + PID 檔），啟動再另一次 exec。
     */
    /**
     * 啟動讀取。
     *
     * [skipExisting] 的語意（App 被系統回收後恢復時很重要）：
     *  - 先看 log 檔的**最後修改時間**：若在 [freshSec] 秒內（代表剛才真的有手勢），
     *    就從**上次讀到的位移**續讀 → 喚醒前一刻的手勢不會被丢掉；
     *  - 若檔案已閒置超過 [freshSec] 秒，就從檔尾開始，避免重放很舊的手勢。
     */
    suspend fun start(devicePath: String, skipExisting: Boolean = false, freshSec: Long = 10L): Boolean {
        stop()
        device = devicePath
        val size = root.exec("stat -c %s $logPath 2>/dev/null", 8_000L).trim().toLongOrNull() ?: 0L
        val mtime = root.exec("stat -c %Y $logPath 2>/dev/null", 8_000L).trim().toLongOrNull() ?: 0L
        val now = root.exec("date +%s", 8_000L).trim().toLongOrNull() ?: 0L
        val fresh = mtime > 0 && now > 0 && (now - mtime) <= freshSec
        offset = when {
            !skipExisting -> 0L
            fresh -> savedOffset().coerceAtMost(size)   // 續讀：可能補到剛剛漏掉的手勢
            else -> size                                // 太舊：跳過
        }
        Log.i(TAG, "start($devicePath) skipExisting=$skipExisting fresh=$fresh size=$size offset=$offset")
        val launch = "rm -f $logPath ; " +
            "setsid sh -c 'getevent -lt $devicePath >> $logPath 2>&1' >/dev/null 2>&1 & " +
            "echo \$! > $pidPath"
        root.exec(launch, 15_000L)
        kotlinx.coroutines.delay(500)
        val running = root.exec("pgrep -f '[g]etevent' | wc -l", 10_000L).trim().toIntOrNull() ?: 0
        val ok = running > 0
        Log.i(TAG, "start($devicePath) getevent 執行中=$ok")
        return ok
    }

    suspend fun stop() {
        root.exec("kill \$(cat $pidPath) 2>/dev/null ; pkill -f '[g]etevent' ; rm -f $pidPath", 10_000L)
        device = null
    }

    fun isRunning(): Boolean = device != null

    private var prefs: android.content.SharedPreferences? = null

    fun attach(context: android.content.Context) {
        prefs = context.applicationContext.getSharedPreferences(com.rokid.rc.glasses.ring.RingPrefs.NAME, android.content.Context.MODE_PRIVATE)
    }

    private fun savedOffset(): Long = prefs?.getLong(KEY_OFFSET, 0L) ?: 0L

    private fun saveOffset(v: Long) {
        prefs?.edit()?.putLong(KEY_OFFSET, v)?.apply()
    }

    /** 直接問 root daemon：背景 getevent 是否還活著（看門狗用）。 */
    suspend fun isAlive(): Boolean =
        (root.exec("pgrep -f '[g]etevent' | wc -l", 8_000L).trim().toIntOrNull() ?: 0) > 0

    /**
     * 取回自上次呼叫後的新事件行（最多 [maxBytes]）。
     * 讀不到時回傳空清單。
     */
    suspend fun pollNew(maxBytes: Int = 8192): List<String> {
        // 防護：log 檔若被重建（新一次 start）而位移還停留在舊值，tail 會永遠讀不到東西。
        // 每輪比對檔案大小，位移越界就歸零重讀。
        val size = root.exec("stat -c %s $logPath 2>/dev/null", 8_000L).trim().toLongOrNull() ?: 0L
        if (offset > size) {
            Log.w(TAG, "位移越界（offset=$offset > size=$size）→ 歸零重讀")
            offset = 0L
        }
        val out = root.exec("tail -c +${offset + 1} $logPath 2>/dev/null | head -c $maxBytes", 10_000L)
        if (out.isEmpty()) return emptyList()
        offset += out.toByteArray().size
        saveOffset(offset)
        return out.lineSequence().filter { it.isNotBlank() }.toList()
    }

    companion object {
        private const val TAG = "RingEvdevReader"
        private const val KEY_OFFSET = "reader_offset"

        /** 讀取器只把「會被當成點擊」的標籤交給手勢機；其餘回報原始標籤。 */
        val TAP_LIKE = setOf(
            "BTN_TOUCH", "BTN_LEFT", "BTN_MOUSE", "BTN_RIGHT", "BTN_MIDDLE",
            "BTN_0", "BTN_1", "BTN_TRIGGER",
            "KEY_ENTER", "KEY_KPENTER", "KEY_SELECT", "KEY_OK",
            "KEY_PLAYPAUSE", "KEY_HOMEPAGE", "KEY_BACK", "KEY_FORWARD",
            "KEY_VOLUMEUP", "KEY_VOLUMEDOWN"
        )
    }
}
