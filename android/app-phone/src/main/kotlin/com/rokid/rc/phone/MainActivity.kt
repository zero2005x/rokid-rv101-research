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
 * Single Activity: hosts the bottom navigation (remote / albums / local),
 * and centrally manages the Bluetooth connection, permissions and "pick glasses".
 */
class MainActivity : AppCompatActivity() {

    // must start as an empty string: show()'s `if (tag == shownTag) return` would skip
    // show("remote") on the first call, leaving the home page blank (it only appears after switching tabs) — a known bug.
    private var shownTag = ""

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        intent.getStringExtra("tab")?.let { show(it) }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // the theme (light/dark, dark by default) and the per-app language must both be applied before setContentView
        ThemeHelper.apply(this)
        LocaleHelper.apply(this)
        setContentView(R.layout.activity_main)
        Remote.ensureInit(applicationContext)
        // clean up the *.part leftovers from interrupted transfers (a failed transfer leaves a full-size temp file every time)
        lifecycleScope.launch(kotlinx.coroutines.Dispatchers.IO) {
            com.rokid.rc.phone.util.LocalStore.cleanupStaleParts()
        }
        // connecting/disconnecting the transport and starting negotiation all update the screen (may happen on a background thread)
        Remote.onTransportChanged = { runOnUiThread { refreshAllFragments() } }
        com.rokid.rc.phone.ring.RingController.ensureInit(this)
        // background media keys (the ring's PLAYPAUSE/NEXT/PREVIOUS) → path A can trigger in the background too
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

    // ---------------- ring gesture interception (path A: phone side) ----------------
    // the ring's (BLE HID) events are delivered to the foreground window; this feeds them into RingController,
    // which recognizes single tap/double tap/long press/wheel and turns them into photo, video, silent and display toggle.
    private val handler = android.os.Handler(android.os.Looper.getMainLooper())
    @Volatile private var ringTickRunning = false
    private val ringTick = object : Runnable {
        override fun run() {
            if (!ringTickRunning) return
            com.rokid.rc.phone.ring.RingController.tick()
            handler.postDelayed(this, RING_TICK_MS)
        }
    }

    // use dispatchKeyEvent rather than onKeyDown: the latter is called only when "no View consumes" the event,
    // so an ENTER from the ring is eaten by a focused button (measured); only the dispatch layer intercepts it reliably.
    override fun dispatchKeyEvent(event: android.view.KeyEvent): Boolean {
        val handled = when (event.action) {
            android.view.KeyEvent.ACTION_DOWN -> com.rokid.rc.phone.ring.RingController.onKeyDown(event.keyCode)
            android.view.KeyEvent.ACTION_UP -> com.rokid.rc.phone.ring.RingController.onKeyUp(event.keyCode)
            else -> false
        }
        return handled || super.dispatchKeyEvent(event)
    }

    /**
     * A mouse-style ring's "click" arrives with `source=SOURCE_MOUSE/TOUCHPAD` through **dispatchTouchEvent**
     * (not dispatchGenericMotionEvent, which only carries hover/scroll) — so it must be intercepted here.
     * Only mouse/touchpad sources are handled, so **finger touches are unaffected** (an ordinary tap is not mistaken for a ring gesture).
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

            // note: a mouse "click" is not here (it goes through dispatchTouchEvent); this handles only wheel/hover
        }
        return super.dispatchGenericMotionEvent(e)
    }

    /** Enable App auto-connect to a paired glasses device whose name contains Glasses/Rokid. */
    // BLUETOOTH_CONNECT is declared in the Manifest and requested before connecting
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

    /** Show the "pick glasses" dialog (when not connected). */
    // BLUETOOTH_CONNECT is declared in the Manifest and requested before connecting
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

    /** Ask the fragments to refresh after a connection state change. */
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
         * ⚠ The WiFi media channel needs it (fixed 2026-09-13):
         * the Manifest had long declared NEARBY_WIFI_DEVICES, but it was **never requested at runtime** →
         * on Android 13 `WifiInfo.getSSID()` returns only `<unknown ssid>`,
         * so the "have we joined the glasses AP" test always failed → WiFi transfer was completely unusable (logcat showed
         * `specifier: network available` and then nothing further).
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
        /** Ring gesture tick interval (used for long-press / repeated-tap detection). */
        private const val RING_TICK_MS = 80L

        /** Open one glasses-side album with MediaActivity. */
        fun openRemoteAlbum(from: android.app.Activity, albumId: String, albumName: String) {
            from.startActivity(
                Intent(from, MediaActivity::class.java)
                    .putExtra("mode", "remote")
                    .putExtra("albumId", albumId)
                    .putExtra("albumName", albumName)
            )
        }

        /** Open a local album with MediaActivity. */
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
