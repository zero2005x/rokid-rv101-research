package com.rokid.rc.shared.ring

import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log

/**
 * Ring GATT probing: connect to the given MAC, list services/characteristics, read the readable characteristic
 * values, then disconnect.
 *
 * Uses:
 *  1. BLE peripherals often **do not advertise a name**, so the only way to identify one is to connect and inspect its services.
 *  2. A HID device's **Report Map (0x2A4B)** can often be read while it is unbound → it can be parsed offline to see which
 *     keys/axes it sends (this project relies on that step to decide the gesture mapping).
 *  3. The basis for a later "ring GATT direct control".
 */
object RingGatt {

    private const val TAG = "RingGatt"

    // HID over GATT
    private const val UUID_HID_SERVICE = "00001812-0000-1000-8000-00805f9b34fb"
    private const val UUID_REPORT_MAP = "00002a4b-0000-1000-8000-00805f9b34fb"
    private const val UUID_HID_INFO = "00002a4a-0000-1000-8000-00805f9b34fb"
    private const val UUID_BATTERY_LEVEL = "00002a19-0000-1000-8000-00805f9b34fb"

    data class CharInfo(val uuid: String, val props: Int)
    data class SvcInfo(val uuid: String, val chars: List<CharInfo>) {
        val isHid: Boolean get() = uuid.equals(UUID_HID_SERVICE, ignoreCase = true)
    }

    private fun hex(b: ByteArray?): String = b?.joinToString("") { "%02X".format(it) } ?: "-"

    /** Connect and enumerate services (including reading readable characteristics); [onDone] returns (success?, service list, explanation). */
    fun probe(
        context: Context,
        mac: String,
        timeoutMs: Long = 15_000L,
        onDone: (Boolean, List<SvcInfo>, String) -> Unit
    ) {
        val adapter = (context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter
        if (adapter == null || !adapter.isEnabled) {
            onDone(false, emptyList(), "藍牙未開啟")
            return
        }
        val device: BluetoothDevice = try {
            adapter.getRemoteDevice(mac)
        } catch (e: IllegalArgumentException) {
            onDone(false, emptyList(), "MAC 格式錯誤：$mac")
            return
        }
        probe(context, device, timeoutMs, onDone)
    }

    /**
     * Probe with a [BluetoothDevice] obtained from a scan (**prefer this one**):
     * an object rebuilt from a MAC string is a public address type, and for random-address peripherals that yields status 133.
     */
    fun probe(
        context: Context,
        device: BluetoothDevice,
        timeoutMs: Long = 15_000L,
        onDone: (Boolean, List<SvcInfo>, String) -> Unit
    ) {
        Probe(context, device, device.address ?: "?", timeoutMs, onDone).start()
    }

    /** After a scan, probe the first [maxDevices] connectable devices in order. */
    fun scanAndProbe(context: Context, maxDevices: Int = 3, onDone: (List<Pair<String, List<SvcInfo>>>) -> Unit) {
        RingScanner(context).scan(durationMs = 10_000L) { list ->
            val targets = list.filter { it.connectable != false }.take(maxDevices)
            Log.i(TAG, "scanAndProbe 目標 ${targets.map { it.mac }}")
            val results = mutableListOf<Pair<String, List<SvcInfo>>>()
            var idx = 0
            fun next() {
                if (idx >= targets.size) {
                    onDone(results)
                    return
                }
                val t = targets[idx++]
                val dev = t.device
                if (dev == null) {
                    Log.w(TAG, "跳過 ${t.mac}（無 device 物件）")
                    next(); return
                }
                probe(context, dev) { ok, svcs, note ->
                    Log.i(TAG, "probe ${t.mac} ok=$ok $note")
                    if (ok) results += t.mac to svcs
                    next()
                }
            }
            next()
        }
    }

    /** State machine for a single probe (keeps several probes from interfering with each other). */
    private class Probe(
        private val context: Context,
        private val device: BluetoothDevice,
        private val mac: String,
        private val timeoutMs: Long,
        private val onDone: (Boolean, List<SvcInfo>, String) -> Unit
    ) {
        private val handler = Handler(Looper.getMainLooper())
        private var gatt: BluetoothGatt? = null
        private var finished = false
        private var readQueue: List<BluetoothGattCharacteristic> = emptyList()
        private var readIdx = 0
        private var svcs: List<SvcInfo> = emptyList()

        private val cb = object : BluetoothGattCallback() {
            override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
                Log.i(TAG, "$mac 連線狀態 status=$status state=$newState")
                when (newState) {
                    BluetoothProfile.STATE_CONNECTED -> {
                        val ok = runCatching { g.discoverServices() }.getOrDefault(false)
                        if (!ok) finish(false, "discoverServices 啟動失敗")
                    }
                    BluetoothProfile.STATE_DISCONNECTED -> finish(false, "連線中斷（status=$status）")
                }
            }

            override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
                svcs = g.services.orEmpty().map { s ->
                    SvcInfo(
                        uuid = s.uuid.toString(),
                        chars = s.characteristics.orEmpty().map { c -> CharInfo(c.uuid.toString(), c.properties) }
                    )
                }
                Log.i(TAG, "$mac 服務數=${svcs.size}")
                svcs.forEach { s ->
                    Log.i(TAG, "  service ${s.uuid}${if (s.isHid) "  <= HID (HOGP)" else ""}")
                    s.chars.forEach { c ->
                        Log.i(TAG, "    char ${c.uuid} props=0x${Integer.toHexString(c.props)}")
                    }
                }
                readQueue = g.services.orEmpty().flatMap { s ->
                    s.characteristics.orEmpty().filter {
                        (it.properties and BluetoothGattCharacteristic.PROPERTY_READ) != 0
                    }
                }
                readIdx = 0
                Log.i(TAG, "$mac 可讀特徵 ${readQueue.size} 個")
                readNext(g)
            }

            @Deprecated("Deprecated in Java")
            override fun onCharacteristicRead(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
                logChar(c, status)
                readNext(g)
            }

            override fun onCharacteristicRead(
                g: BluetoothGatt,
                c: BluetoothGattCharacteristic,
                value: ByteArray,
                status: Int
            ) {
                logChar(c, status, value)
                readNext(g)
            }

            private fun logChar(c: BluetoothGattCharacteristic, status: Int, value: ByteArray? = null) {
                @Suppress("DEPRECATION")
                val v = value ?: c.value
                val note = when (c.uuid.toString().lowercase()) {
                    UUID_REPORT_MAP -> "  <= HID Report Map"
                    UUID_HID_INFO -> "  <= HID Information"
                    UUID_BATTERY_LEVEL -> "  <= Battery Level"
                    else -> ""
                }
                Log.i(TAG, "  read ${c.uuid} status=$status len=${v?.size ?: 0} value=${hex(v)}$note")
            }
        }

        private fun readNext(g: BluetoothGatt) {
            if (readIdx >= readQueue.size) {
                finish(true, "完成（服務 ${svcs.size}，已讀特徵 ${readQueue.size}）")
                return
            }
            val c = readQueue[readIdx++]
            val ok = runCatching { g.readCharacteristic(c) }.getOrDefault(false)
            if (!ok) {
                Log.w(TAG, "  read 啟動失敗：${c.uuid}")
                readNext(g)
            }
        }

        private fun finish(ok: Boolean, note: String) {
            if (finished) return
            finished = true
            runCatching { gatt?.disconnect() }
            runCatching { gatt?.close() }
            onDone(ok, svcs, note)
        }

        fun start() {
            Log.i(TAG, "開始探測 $mac（${timeoutMs}ms）")
            gatt = runCatching {
                if (Build.VERSION.SDK_INT >= 23) {
                    device.connectGatt(context, false, cb, BluetoothDevice.TRANSPORT_LE)
                } else {
                    device.connectGatt(context, false, cb)
                }
            }.getOrElse {
                onDone(false, emptyList(), "connectGatt 失敗：${it.message}")
                return
            }
            handler.postDelayed({ finish(false, "逾時 ${timeoutMs}ms") }, timeoutMs)
        }
    }
}
