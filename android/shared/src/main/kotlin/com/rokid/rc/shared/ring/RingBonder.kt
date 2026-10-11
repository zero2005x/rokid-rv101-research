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
 * 戒指配對（bonding）。
 *
 * 戒指多為 BLE HID（HOGP 0x1812）周邊，Android 需先完成 SMP 綁定才會把它的
 * input device 掛進系統（之後才收得到按鍵/滑鼠事件）。
 *
 * **位址型別很重要**：BLE 周邊常用 random address，若用字串 MAC 重建 `BluetoothDevice`
 * 會被當成 public address type → 連線得到 GATT status 133、`createBond()` 也會失敗。
 * 因此一律優先用「掃描結果帶回的 BluetoothDevice 物件」。
 *
 * **2026-09-13 修正**：新增 [bondedHidDevices]。原本唯一入口是 [scanAndBond]（用名稱子字串掃描），
 * 但**已配對的戒指不會再廣播**，所以那條路對已配對裝置永遠找不到（AIVELA 實測三次掃描皆無）。
 * UI 應改為「列出已配對的 HID 裝置讓使用者選」。
 */
object RingBonder {

    private const val TAG = "RingBonder"

    /** BR/EDR HID（HID over Bluetooth classic）。 */
    private const val UUID_HID_BREDR = "00001124-0000-1000-8000-00805f9b34fb"

    /** HID over GATT（BLE）。 */
    private const val UUID_HOGP = "00001812-0000-1000-8000-00805f9b34fb"

    private const val UUID_HID_SERVICE = UUID_HOGP

    /**
     * 已配對、且看起來是 **HID**（鍵盤／滑鼠／觸控板／複合）的裝置。
     *
     * 判定順序：`BluetoothClass` 的周邊類別 → 服務 UUID（0x1124 / 0x1812）→
     * 名稱含 RING / RGR / AIVELA（涵蓋未回報 class 的 BLE 周邊）。
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
            // `Major.PERIPHERAL` 也涵蓋耳機／喇叭等 AUDIO_VIDEO 周邊（實測 Xiaomi Buds 會入列），
            // 那些不是 HID → 除非 UUID／名稱明確像 HID/戒指，否則排除，讓清單真的只列可當戒指的裝置。
            val isAudio = cls != null && cls.doesClassMatch(BluetoothClass.Device.Major.AUDIO_VIDEO)
            val hidEvidence = byUuid || byName
            (byClass || hidEvidence) && !(isAudio && !hidEvidence)
        }.sortedBy { runCatching { it.name }.getOrNull() ?: (it.address ?: "") }
    }

    /** 裝置的顯示名稱（未知時回 MAC）。 */
    @SuppressLint("MissingPermission")
    fun displayName(device: BluetoothDevice): String =
        runCatching { device.name }.getOrNull()?.takeIf { it.isNotBlank() } ?: (device.address ?: "?")

    /** 以 MAC 字串配對（僅適用 public address 的裝置；優先用 [bond] 的 device 版本）。 */
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

    /** 以掃描取得的 [BluetoothDevice] 配對（**建議用這個**，保留正確 address type）。 */
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

        // ⚠ 必須用 **application context**：本函式可能被 `BroadcastReceiver.onReceive()` 呼叫
        // （例：`DebugReceiver` 的 PAIR_RING），而 receiver 的 context 是
        // `ReceiverRestrictedContext` —— 在 Android 12（API < 33）它只允許帶 flag 的
        // registerReceiver，兩參數版本會丟 `ReceiverCallNotAllowedException` 讓 App 直接崩潰
        // （實測：眼鏡 API 32 從 DebugReceiver 配對必崩，見 reports/rgr06_pairing_failure_2026-09-13.md）。
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
     * 掃描 → 挑選名稱符合 [namePattern]（不分大小寫；null 表示「可連線且 RSSI 最強者」）
     * → 立刻用**掃描當下的 BluetoothDevice** 配對。
     */
    fun scanAndBond(
        context: Context,
        namePattern: String? = RingIdentity.DEFAULT_NAME_PATTERN,
        scanMs: Long = 12_000L,
        onDone: (Boolean, String, String?) -> Unit
    ) {
        RingScanner(context).scan(scanMs) { list ->
            val candidates = list.filter { it.connectable != false && it.device != null }
            // 指定名稱時**絕不退而求其次**：否則會誤配對其他可連線裝置（例如智慧音箱）
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

    /** 解除配對（removeBond；非系統 App 常失敗，回報即可）。 */
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
