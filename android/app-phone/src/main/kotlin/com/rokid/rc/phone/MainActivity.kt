package com.rokid.rc.phone

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.rokid.rc.phone.ui.AlbumsFragment
import com.rokid.rc.phone.ui.LocalFragment
import com.rokid.rc.phone.ui.RemoteFragment
import com.rokid.rc.phone.ui.SettingsFragment
import kotlinx.coroutines.launch

/**
 * 單一 Activity：承載底部導覽（遙控 / 相簿 / 本機），
 * 並集中管理藍牙連線、權限與「選擇眼鏡」。
 */
class MainActivity : AppCompatActivity() {

    // 初始必須為空字串：show() 的 `if (tag == shownTag) return` 會在首次
    // show("remote") 時直接跳過，導致首頁空白（需切換 tab 才出現）——已知 bug。
    private var shownTag = ""

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        intent.getStringExtra("tab")?.let { show(it) }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // 主題（淺色／深色，預設深色）與 per-app 語言，都要在 setContentView 前套用
        ThemeHelper.apply(this)
        LocaleHelper.apply(this)
        setContentView(R.layout.activity_main)
        Remote.ensureInit(applicationContext)
        // 清掉中斷傳輸留下的 *.part 殘骸（失敗的傳輸每次會留下一個完整大小的暫存檔）
        lifecycleScope.launch(kotlinx.coroutines.Dispatchers.IO) {
            com.rokid.rc.phone.util.LocalStore.cleanupStaleParts()
        }
        // 傳輸通道連上／斷線／開始協商都要更新畫面（可能在背景執行緒發生）
        Remote.onTransportChanged = { runOnUiThread { refreshAllFragments() } }
        com.rokid.rc.phone.ring.RingController.ensureInit(this)
        // 背景媒體鍵（戒指的 PLAYPAUSE/NEXT/PREVIOUS）→ 路徑 A 在背景也能觸發
        com.rokid.rc.phone.ring.RingMediaSession.ensureStarted(this)
        requestBlePermissions()

        findViewById<com.google.android.material.bottomnavigation.BottomNavigationView>(R.id.bottomNav)
            .setOnItemSelectedListener { item ->
                when (item.itemId) {
                    R.id.nav_albums -> show("albums")
                    R.id.nav_local -> show("local")
                    R.id.nav_settings -> show("settings")
                    else -> show("remote")
                }
                true
            }
        intent.getStringExtra("tab")?.let { tab -> show(tab); return }
        show("remote")
    }

    private fun show(tag: String) {
        if (tag == shownTag) return
        val fm = supportFragmentManager
        val tx = fm.beginTransaction()
        var target = fm.findFragmentByTag(tag)
        if (target == null) {
            target = when (tag) {
                "albums" -> AlbumsFragment()
                "local" -> LocalFragment()
                "settings" -> com.rokid.rc.phone.ui.SettingsFragment()
                else -> RemoteFragment()
            }
            tx.add(R.id.navHost, target, tag)
        } else {
            tx.show(target)
        }
        listOf("remote", "albums", "local", "settings").filter { it != tag }.forEach { t ->
            fm.findFragmentByTag(t)?.let { if (it.isAdded) tx.hide(it) }
        }
        tx.commitAllowingStateLoss()
        shownTag = tag
        when (tag) {
            "albums" -> (target as? AlbumsFragment)?.refresh()
            "local" -> (target as? LocalFragment)?.refresh()
            "settings" -> (target as? com.rokid.rc.phone.ui.SettingsFragment)?.refresh()
            else -> (target as? RemoteFragment)?.refresh()
        }
    }

    override fun onResume() {
        super.onResume()
        if (!Remote.isConnected) autoConnectIfPossible()
        ringTickRunning = true
        handler.postDelayed(ringTick, RING_TICK_MS)
    }

    override fun onPause() {
        ringTickRunning = false
        handler.removeCallbacks(ringTick)
        super.onPause()
    }

    // ---------------- 戒指手勢攔截（路徑 A：手機端） ----------------
    // 戒指（BLE HID）的事件會送到前景視窗；這裡把它餵進 RingController，
    // 由它辨識單擊／雙擊／長按／滾輪並轉成拍照、錄影、靜音、顯示切換。
    private val handler = android.os.Handler(android.os.Looper.getMainLooper())
    @Volatile private var ringTickRunning = false
    private val ringTick = object : Runnable {
        override fun run() {
            if (!ringTickRunning) return
            com.rokid.rc.phone.ring.RingController.tick()
            handler.postDelayed(this, RING_TICK_MS)
        }
    }

    // 用 dispatchKeyEvent 而非 onKeyDown：後者只會在「沒有任何 View 消費」時才被呼叫，
    // 戒指若送 ENTER，會被畫面上聚焦的按鈕吃掉（實測），dispatch 層才能穩定攔到。
    override fun dispatchKeyEvent(event: android.view.KeyEvent): Boolean {
        val handled = when (event.action) {
            android.view.KeyEvent.ACTION_DOWN -> com.rokid.rc.phone.ring.RingController.onKeyDown(event.keyCode)
            android.view.KeyEvent.ACTION_UP -> com.rokid.rc.phone.ring.RingController.onKeyUp(event.keyCode)
            else -> false
        }
        return handled || super.dispatchKeyEvent(event)
    }

    /**
     * 滑鼠式戒指的「點擊」會以 `source=SOURCE_MOUSE/TOUCHPAD` 走 **dispatchTouchEvent**
     * （不是 dispatchGenericMotionEvent，後者只有 hover/scroll）——所以必須在這裡攔。
     * 只處理滑鼠/觸控板來源，**不影響手指觸控**（避免一般點擊也被當成戒指手勢）。
     */
    override fun dispatchTouchEvent(ev: android.view.MotionEvent): Boolean {
        val src = ev.source
        val isPointer = (src and android.view.InputDevice.SOURCE_MOUSE) == android.view.InputDevice.SOURCE_MOUSE ||
            (src and android.view.InputDevice.SOURCE_TOUCHPAD) == android.view.InputDevice.SOURCE_TOUCHPAD
        if (isPointer) {
            val button = if (ev.buttonState != 0) ev.buttonState else android.view.MotionEvent.BUTTON_PRIMARY
            if (com.rokid.rc.phone.ring.RingController.onMouseButton(ev.actionMasked, button)) return true
        }
        return super.dispatchTouchEvent(ev)
    }

    override fun dispatchGenericMotionEvent(event: android.view.MotionEvent?): Boolean {
        val e = event ?: return super.dispatchGenericMotionEvent(null)
        val ring = com.rokid.rc.phone.ring.RingController
        when (e.actionMasked) {
            android.view.MotionEvent.ACTION_SCROLL ->
                if (ring.onScroll(if (e.getAxisValue(android.view.MotionEvent.AXIS_VSCROLL) > 0f) 1 else -1)) return true

            // 註：滑鼠「點擊」不在這裡（走 dispatchTouchEvent），此處只處理滾輪/懸停
        }
        return super.dispatchGenericMotionEvent(e)
    }

    /** 開啟 App 自動連到已配對、名稱含 Glasses/Rokid 的眼鏡。 */
    // BLUETOOTH_CONNECT 已於 Manifest 宣告並於連線前請求
    @Suppress("MissingPermission")
    fun autoConnectIfPossible() {
        val adapter = BluetoothAdapter.getDefaultAdapter() ?: return
        if (!adapter.isEnabled) return
        val devices = try {
            adapter.bondedDevices.toList()
        } catch (e: Exception) {
            emptyList()
        }
        if (devices.isEmpty()) return
        val target = devices.firstOrNull {
            val n = it.name ?: ""
            n.contains("Glasses", true) || n.contains("Rokid", true)
        } ?: devices.first()
        lifecycleScope.launch { connect(target) }
    }

    /** 顯示「選擇眼鏡」對話框（未連線時）。 */
    // BLUETOOTH_CONNECT 已於 Manifest 宣告並於連線前請求
    @Suppress("MissingPermission")
    fun pickDevice() {
        val adapter = BluetoothAdapter.getDefaultAdapter()
        if (adapter == null || !adapter.isEnabled) {
            toast(getString(R.string.toast_bt_disabled))
            return
        }
        val devices = try {
            adapter.bondedDevices.toList()
        } catch (e: Exception) {
            emptyList()
        }
        if (devices.isEmpty()) {
            toast(getString(R.string.toast_no_paired_device))
            return
        }
        val names = devices.map { "${it.name ?: "unknown"} (${it.address})" }.toTypedArray()
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.conn_pick_title)
            .setItems(names) { _, which ->
                val d = devices[which]
                lifecycleScope.launch { connect(d) }
            }
            .show()
    }

    suspend fun connect(device: BluetoothDevice) {
        Remote.connect(device).fold(
            onSuccess = { refreshAllFragments() },
            onFailure = { e -> toast(getString(R.string.toast_connect_failed, e.message ?: "")) }
        )
    }

    fun disconnect() {
        Remote.disconnect()
        refreshAllFragments()
    }

    /** 連線狀態改變後叫各 Fragment 重新整理。 */
    fun refreshAllFragments() {
        val fm = supportFragmentManager
        (fm.findFragmentByTag("remote") as? RemoteFragment)?.onConnectionChanged()
        (fm.findFragmentByTag("albums") as? AlbumsFragment)?.onConnectionChanged()
        (fm.findFragmentByTag("local") as? LocalFragment)?.onConnectionChanged()
        (fm.findFragmentByTag("settings") as? SettingsFragment)?.refresh()
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == 100 && grantResults.isNotEmpty() &&
            grantResults.all { it == PackageManager.PERMISSION_GRANTED }
        ) {
            autoConnectIfPossible()
        }
    }

    private fun requestBlePermissions() {
        val needed = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= 31) {
            needed += Manifest.permission.BLUETOOTH_CONNECT
            needed += Manifest.permission.BLUETOOTH_SCAN
        }
        /*
         * ⚠ WiFi 媒體通道需要它（2026-09-13 修正）：
         * Manifest 早已宣告 NEARBY_WIFI_DEVICES，但**執行期從來沒要求**過 →
         * 在 Android 13 上 `WifiInfo.getSSID()` 只會回 `<unknown ssid>`，
         * 使「是否已加入眼鏡 AP」的判斷永遠失敗 → WiFi 傳輸完全不能用（logcat 只看到
         * `specifier: network available` 之後就沒有任何動靜）。
         */
        if (Build.VERSION.SDK_INT >= 33) {
            needed += Manifest.permission.NEARBY_WIFI_DEVICES
        }
        val missing = needed.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (missing.isNotEmpty()) {
            ActivityCompat.requestPermissions(this, missing.toTypedArray(), 100)
        }
    }

    fun toast(msg: String) {
        Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()
    }

    companion object {
        /** 戒指手勢 tick 間隔（長按／連續點擊判定用）。 */
        private const val RING_TICK_MS = 80L

        /** 以 MediaActivity 開啟眼鏡端某相簿。 */
        fun openRemoteAlbum(from: android.app.Activity, albumId: String, albumName: String) {
            from.startActivity(
                Intent(from, MediaActivity::class.java)
                    .putExtra("mode", "remote")
                    .putExtra("albumId", albumId)
                    .putExtra("albumName", albumName)
            )
        }

        /** 以 MediaActivity 開啟本機相簿。 */
        fun openLocalAlbum(from: android.app.Activity, albumId: String, albumName: String) {
            from.startActivity(
                Intent(from, MediaActivity::class.java)
                    .putExtra("mode", "local")
                    .putExtra("albumId", albumId)
                    .putExtra("albumName", albumName)
            )
        }
    }
}
