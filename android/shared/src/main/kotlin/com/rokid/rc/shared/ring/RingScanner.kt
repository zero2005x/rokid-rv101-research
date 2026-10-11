package com.rokid.rc.shared.ring

import android.Manifest
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.os.Handler
import android.os.Looper
import android.util.Log
import androidx.core.content.ContextCompat

/**
 * BLE scanner (for ring discovery/identification).
 *
 * Purpose:
 *  1. Find the ring's **advertising name** (the phone settings page only shows the MAC, and BLE
 *     peripherals are often nameless there).
 *  2. Obtain RSSI / service UUID / connectability, as the basis for pairing and later GATT communication.
 *
 * Triggered by the `SCAN_RING` command of `DebugReceiver` or by the UI; results go both to logcat
 * (tag `RingScanner`) and to the callback, so `adb logcat -s RingScanner:*` shows them directly.
 */
class RingScanner(private val context: Context) {

    /** One scan result (only the strongest RSSI is kept per MAC). */
    data class Found(
        /** The BluetoothDevice returned by the scan (**keeps the address type**; rebuilding it
         *  from a string turns it public -> connection 133). */
        val device: android.bluetooth.BluetoothDevice? = null,
        val mac: String,
        val name: String?,
        val rssi: Int,
        val uuids: List<String>,
        val connectable: Boolean?,
        val txPower: Int?,
        val companyIds: List<Int> = emptyList(),
        val rawHex: String = ""
    ) {
        fun describe(): String = buildString {
            append(mac)
            append(" rssi=").append(rssi)
            append(" name=").append(name ?: "-")
            if (uuids.isNotEmpty()) append(" uuids=").append(uuids.joinToString(","))
            connectable?.let { append(" connectable=").append(it) }
            txPower?.let { append(" tx=").append(it) }
            if (companyIds.isNotEmpty()) {
                append(" company=").append(companyIds.joinToString(",") { "0x%04X".format(it) })
            }
            if (rawHex.isNotEmpty()) append(" raw=").append(rawHex)
        }
    }

    private val tag = "RingScanner"
    private val handler = Handler(Looper.getMainLooper())
    private val found = LinkedHashMap<String, Found>()

    /**
     * Scan for [durationMs] milliseconds and then call back with the results (strongest RSSI first).
     * Without the BLUETOOTH_SCAN permission, or when Bluetooth is off, it calls back an empty list
     * and logs the reason.
     */
    fun scan(
        durationMs: Long = 12_000L,
        nameFilter: String? = null,
        onDone: (List<Found>) -> Unit
    ) {
        val adapter = (context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter
        if (adapter == null || !adapter.isEnabled) {
            Log.w(tag, "掃描中止：藍牙未開啟")
            onDone(emptyList())
            return
        }
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_SCAN)
            != PackageManager.PERMISSION_GRANTED
        ) {
            Log.w(tag, "掃描中止：缺少 BLUETOOTH_SCAN 權限")
            onDone(emptyList())
            return
        }
        val scanner = adapter.bluetoothLeScanner
        if (scanner == null) {
            Log.w(tag, "掃描中止：bluetoothLeScanner 為 null")
            onDone(emptyList())
            return
        }

        found.clear()
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .setReportDelay(0L)
            .build()

        val callback = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) = collect(result)

            override fun onBatchScanResults(results: MutableList<ScanResult>) {
                results.forEach { collect(it) }
            }

            override fun onScanFailed(errorCode: Int) {
                Log.w(tag, "onScanFailed errorCode=$errorCode")
            }
        }

        Log.i(tag, "開始掃描 ${durationMs}ms（filter=${nameFilter ?: "-"}）")
        runCatching { scanner.startScan(null, settings, callback) }
            .onFailure { Log.w(tag, "startScan 失敗：${it.message}") }

        handler.postDelayed({
            runCatching { scanner.stopScan(callback) }
            val list = found.values.sortedByDescending { it.rssi }
            Log.i(tag, "掃描結束，共 ${list.size} 台：")
            list.forEach { Log.i(tag, "  裝置 ${it.describe()}") }
            val hit = nameFilter?.let { f -> list.filter { it.name?.contains(f, true) == true } }
            if (hit != null) {
                Log.i(tag, "符合「$nameFilter」者 ${hit.size} 台：")
                hit.forEach { Log.i(tag, "  ★ ${it.describe()}") }
            }
            onDone(list)
        }, durationMs)
    }

    /** Find only devices whose name contains ring/rokid (for quick identification). */
    fun findRingLike(durationMs: Long = 12_000L, onDone: (List<Found>) -> Unit) {
        scan(durationMs) { all ->
            onDone(
                all.filter {
                    val n = it.name ?: ""
                    n.contains("ring", true) || n.contains("rokid", true)
                }
            )
        }
    }

    @Synchronized
    private fun collect(r: ScanResult) {
        val dev = r.device ?: return
        val rec = r.scanRecord
        val mac = dev.address ?: return
        val name = rec?.deviceName ?: runCatching { dev.name }.getOrNull()
        val companyIds = rec?.manufacturerSpecificData?.let { arr ->
            (0 until arr.size()).map { arr.keyAt(it) }
        } ?: emptyList()
        val raw = rec?.bytes?.joinToString("") { "%02X".format(it) }?.take(120) ?: ""
        val item = Found(
            device = dev,
            mac = mac,
            name = name,
            rssi = r.rssi,
            uuids = rec?.serviceUuids?.map { it.uuid.toString() } ?: emptyList(),
            connectable = if (android.os.Build.VERSION.SDK_INT >= 26) r.isConnectable else null,
            txPower = rec?.txPowerLevel?.takeIf { it != Int.MIN_VALUE },
            companyIds = companyIds,
            rawHex = raw
        )
        val prev = found[mac]
        if (prev == null || item.rssi > prev.rssi || (prev.name == null && item.name != null)) {
            found[mac] = if (item.name == null && prev?.name != null) item.copy(name = prev.name) else item
        }
    }
}
