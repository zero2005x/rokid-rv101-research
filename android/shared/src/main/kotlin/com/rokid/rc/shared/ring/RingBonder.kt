package com.rokid.rc.shared.ring

import android.annotation.SuppressLint
import android.bluetooth.BluetoothClass
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log

/**
 * Ring bonding.
 *
 * Rings are mostly BLE HID (HOGP 0x1812) peripherals; Android only attaches their input
 * device to the system after SMP bonding completes (key/mouse events arrive only after that).
 *
 * **The address type matters**: BLE peripherals often use a random address, and rebuilding a
 * `BluetoothDevice` from a MAC string treats it as public address type -> the connection gets GATT
 * status 133 and `createBond()` fails too. So always prefer the BluetoothDevice object returned by the scan.
 *
 * **2026-09-13 fix**: added [bondedHidDevices]. The only entry point used to be [scanAndBond] (scanning by
 * name substring), but **an already bonded ring stops advertising**, so that path never finds bonded devices
 * (AIVELA: three scans, nothing). The UI should instead list bonded HID devices for the user to pick.
 */
object RingBonder {

    private const val TAG = "RingBonder"

    /** BR/EDR HID (HID over Bluetooth classic). */
    private const val UUID_HID_BREDR = "00001124-0000-1000-8000-00805f9b34fb"

    /** HID over GATT (BLE). */
    private const val UUID_HOGP = "00001812-0000-1000-8000-00805f9b34fb"

    private const val UUID_HID_SERVICE = UUID_HOGP

    /**
     * Bonded devices that look like **HID** (keyboard/mouse/touchpad/combo).
     *
     * Decision order: `BluetoothClass` peripheral category -> service UUID (0x1124 / 0x1812) ->
     * name containing RING / RGR / AIVELA (covers BLE peripherals that report no class).
     */
    @SuppressLint("MissingPermission")
    fun bondedHidDevices(context: Context): List<BluetoothDevice> {
        val adapter = (context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter
        if (adapter == null || !adapter.isEnabled) return emptyList()
        val bonded = runCatching { adapter.bondedDevices }.getOrNull().orEmpty()
        return bonded.filter { d ->
            val cls = runCatching { d.bluetoothClass }.getOrNull()
            val byClass = cls != null && (
                cls.doesClassMatch(BluetoothClass.Device.Major.PERIPHERAL) ||
                    cls.deviceClass == BluetoothClass.Device.PERIPHERAL_KEYBOARD ||
                    cls.deviceClass == BluetoothClass.Device.PERIPHERAL_POINTING ||
                    cls.deviceClass == BluetoothClass.Device.PERIPHERAL_KEYBOARD_POINTING
                )
            val uuids = runCatching { d.uuids }.getOrNull().orEmpty().map { it.uuid.toString().lowercase() }
            val byUuid = uuids.any { it == UUID_HID_BREDR || it == UUID_HID_SERVICE }
            val n = runCatching { d.name }.getOrNull().orEmpty()
            val byName = n.contains("RING", true) || n.contains("RGR", true) ||
                n.contains("AIVELA", true) || n.contains("戒指", true)
            // `Major.PERIPHERAL` also covers AUDIO_VIDEO peripherals such as headsets/speakers
            // (measured: Xiaomi Buds show up), and those are not HID -> exclude them unless the UUID/name
            // clearly looks like HID/ring, so the list really only holds ring-capable devices.
            val isAudio = cls != null && cls.doesClassMatch(BluetoothClass.Device.Major.AUDIO_VIDEO)
            val hidEvidence = byUuid || byName
            (byClass || hidEvidence) && !(isAudio && !hidEvidence)
        }.sortedBy { runCatching { it.name }.getOrNull() ?: (it.address ?: "") }
    }

    /** Display name of the device (falls back to the MAC when unknown). */
    @SuppressLint("MissingPermission")
    fun displayName(device: BluetoothDevice): String =
        runCatching { device.name }.getOrNull()?.takeIf { it.isNotBlank() } ?: (device.address ?: "?")

    /** Bond by MAC string (only for public-address devices; prefer the device overload of [bond]). */
    fun bond(context: Context, mac: String, timeoutMs: Long = 30_000L, onDone: (Boolean, String) -> Unit) {
        val adapter = (context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter
        if (adapter == null || !adapter.isEnabled) {
            onDone(false, "藍牙未開啟")
            return
        }
        val device = try {
            adapter.getRemoteDevice(mac)
        } catch (e: IllegalArgumentException) {
            onDone(false, "MAC 格式錯誤：$mac")
            return
        }
        bond(context, device, timeoutMs, onDone)
    }

    /** Bond with the [BluetoothDevice] obtained from a scan (**prefer this**, it keeps the correct address type). */
    fun bond(
        context: Context,
        device: BluetoothDevice,
        timeoutMs: Long = 30_000L,
        onDone: (Boolean, String) -> Unit
    ) {
        val mac = device.address ?: "?"
        if (device.bondState == BluetoothDevice.BOND_BONDED) {
            onDone(true, "已配對（bonded）")
            return
        }

        // WARNING: the **application context** must be used: this function can be called from
        // `BroadcastReceiver.onReceive()` (e.g. `DebugReceiver`'s PAIR_RING), and a receiver's context is a
        // `ReceiverRestrictedContext` — on Android 12 (API < 33) it only allows the flagged
        // registerReceiver, and the two-argument form throws `ReceiverCallNotAllowedException`, crashing the app
        // (measured: on glasses API 32 pairing from DebugReceiver always crashes, see
        // reports/rgr06_pairing_failure_2026-09-13.md).
        val regCtx = context.applicationContext ?: context
        val filter = IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(c: Context, intent: Intent) {
                val d = if (Build.VERSION.SDK_INT >= 33) {
                    intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
                } else {
                    @Suppress("DEPRECATION")
                    intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
                }
                if (d?.address != mac) return
                val state = intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.ERROR)
                Log.i(TAG, "$mac bondState=$state")
                when (state) {
                    BluetoothDevice.BOND_BONDED -> {
                        runCatching { regCtx.unregisterReceiver(this) }
                        onDone(true, "配對成功")
                    }
                    BluetoothDevice.BOND_NONE -> {
                        runCatching { regCtx.unregisterReceiver(this) }
                        onDone(false, "配對失敗（BOND_NONE）")
                    }
                }
            }
        }
        if (Build.VERSION.SDK_INT >= 33) {
            regCtx.registerReceiver(receiver, filter, Context.RECEIVER_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            regCtx.registerReceiver(receiver, filter)
        }

        Log.i(TAG, "發起配對 $mac（${runCatching { device.name }.getOrNull() ?: "-"}）")
        val started = runCatching { device.createBond() }.getOrDefault(false)
        if (!started) {
            runCatching { regCtx.unregisterReceiver(receiver) }
            onDone(false, "createBond() 回傳 false")
            return
        }
        Handler(Looper.getMainLooper()).postDelayed({
            if (device.bondState != BluetoothDevice.BOND_BONDED) {
                runCatching { regCtx.unregisterReceiver(receiver) }
                onDone(false, "配對逾時 ${timeoutMs}ms（bondState=${device.bondState}）")
            }
        }, timeoutMs)
    }

    /**
     * Scan -> pick a device whose name matches [namePattern] (case-insensitive; null means
     * "connectable with the strongest RSSI") -> bond immediately with the **BluetoothDevice from that scan**.
     */
    fun scanAndBond(
        context: Context,
        namePattern: String? = RingIdentity.DEFAULT_NAME_PATTERN,
        scanMs: Long = 12_000L,
        onDone: (Boolean, String, String?) -> Unit
    ) {
        RingScanner(context).scan(scanMs) { list ->
            val candidates = list.filter { it.connectable != false && it.device != null }
            // When a name is given **never settle for less**: otherwise it bonds the wrong
            // connectable device (e.g. a smart speaker)
            val target = if (namePattern.isNullOrBlank()) {
                candidates.maxByOrNull { it.rssi }
            } else {
                candidates.firstOrNull { it.name?.contains(namePattern, ignoreCase = true) == true }
            }
            if (target?.device == null) {
                onDone(
                    false,
                    if (namePattern.isNullOrBlank()) "掃描不到可連線裝置（共 ${list.size} 台）"
                    else "掃描不到名稱含「$namePattern」的裝置（共 ${list.size} 台）",
                    null
                )
                return@scan
            }
            Log.i(TAG, "scanAndBond 目標 ${target.describe()}")
            bond(context, target.device) { ok, note ->
                onDone(ok, "$note（${target.name ?: "-"} ${target.mac} rssi=${target.rssi}）", target.mac)
            }
        }
    }

    /** Remove the bond (removeBond; it often fails for non-system apps, so just report the result). */
    fun unbond(context: Context, mac: String, onDone: (Boolean, String) -> Unit) {
        val adapter = (context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter
        val device = runCatching { adapter?.getRemoteDevice(mac) }.getOrNull()
        if (device == null) {
            onDone(false, "找不到裝置 $mac")
            return
        }
        val ok = runCatching {
            val m = device.javaClass.getMethod("removeBond")
            m.invoke(device) as? Boolean ?: false
        }.getOrElse { false }
        onDone(ok, if (ok) "已解除配對" else "removeBond 失敗（非系統 App 常見）")
    }
}
