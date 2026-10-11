package com.rokid.rc.shared.ring

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log

/**
 * 戒指監看器：週期性「掃描 → 嘗試 GATT 連線 → 記錄結果」。
 *
 * 用途：戒指若仍綁在舊 host，會拒絕新連線（GATT status 133）。使用者依說明書做
 * 重置（長按/多擊）時，本監看器可即時看出「戒指已鬆綁、可被新 host 連上」的瞬間，
 * 也能當作配對精靈的背景偵測。
 *
 * 注意：Android 12+ 背景掃描會被限制，**呼叫端需讓 App 處於前景**。
 */
class RingWatcher(private val context: Context) {

    private val tag = "RingWatcher"
    private val handler = Handler(Looper.getMainLooper())
    @Volatile private var stopped = false

    /**
     * 進行 [rounds] 輪監看，每輪：掃描 [scanMs] → 對符合 [namePattern] 的裝置嘗試 GATT 連線。
     * [onRound] 回呼 (第幾輪, 結果摘要)；[onConnectable] 在連線成功時回呼（可用於自動配對）。
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
                // 指定名稱時**絕不退而求其次**（避免誤配對其他 BLE 裝置，例如智慧音箱）
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
