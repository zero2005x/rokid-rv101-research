package com.rokid.rc.shared.ring

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log

/**
 * Ring watcher: periodically "scan → attempt a GATT connection → record the result".
 *
 * Use: if the ring is still bound to an old host it refuses new connections (GATT status 133). When the user
 * performs the reset described in the manual (long press / multiple clicks), this watcher can spot the moment
 * "the ring has been released and can be connected by a new host", and it can also act as background detection for
 * the pairing wizard.
 *
 * NOTE: on Android 12+ background scanning is restricted, so **the caller must keep the app in the foreground**.
 */
class RingWatcher(private val context: Context) {

    private val tag = "RingWatcher"
    private val handler = Handler(Looper.getMainLooper())
    @Volatile private var stopped = false

    /**
     * Run [rounds] rounds of watching; each round: scan [scanMs] → attempt a GATT connection to devices matching [namePattern].
     * [onRound] calls back (round number, result summary); [onConnectable] calls back on a successful connection (usable for auto-pairing).
     */
    fun watch(
        rounds: Int = 10,
        scanMs: Long = 6_000L,
        gapMs: Long = 4_000L,
        namePattern: String = RingIdentity.DEFAULT_NAME_PATTERN,
        onConnectable: ((RingScanner.Found) -> Unit)? = null,
        onRound: ((Int, String) -> Unit)? = null
    ) {
        stopped = false
        Log.i(tag, "開始監看 $rounds 輪（pattern=$namePattern，每輪掃描 ${scanMs}ms + 間隔 ${gapMs}ms）")

        fun round(i: Int) {
            if (stopped || i > rounds) {
                Log.i(tag, "監看結束（共 $rounds 輪）")
                onRound?.invoke(0, "watch finished")
                return
            }
            RingScanner(context).scan(scanMs, namePattern) { list ->
                if (stopped) return@scan
                // when a name is given, **never settle for second best** (avoid mis-pairing other BLE devices, such as a smart speaker)
                val target = if (namePattern.isBlank()) {
                    list.filter { it.connectable != false }.maxByOrNull { it.rssi }
                } else {
                    list.firstOrNull { it.name?.contains(namePattern, true) == true }
                }
                if (target?.device == null) {
                    val msg = "第 $i 輪：未見符合裝置（掃到 ${list.size} 台）"
                    Log.i(tag, msg)
                    onRound?.invoke(i, msg)
                    handler.postDelayed({ round(i + 1) }, gapMs)
                    return@scan
                }
                RingGatt.probe(context, target.device, 8_000L) { ok, svcs, note ->
                    val msg = "第 $i 輪：${target.name ?: "-"} ${target.mac} rssi=${target.rssi} " +
                        "→ 連線${if (ok) "成功（服務 ${svcs.size}）" else "失敗：$note"}"
                    Log.i(tag, msg)
                    onRound?.invoke(i, msg)
                    if (ok) {
                        Log.i(tag, "★ 戒指已可連線（可能已完成重置）→ 可進行配對")
                        onConnectable?.invoke(target)
                    }
                    handler.postDelayed({ round(i + 1) }, gapMs)
                }
            }
        }
        round(1)
    }

    fun stop() {
        stopped = true
        Log.i(tag, "監看已停止（請求）")
    }
}
