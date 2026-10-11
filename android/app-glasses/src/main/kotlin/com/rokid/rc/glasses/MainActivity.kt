package com.rokid.rc.glasses

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.view.KeyEvent
import android.view.View
import android.view.WindowManager
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.core.view.isVisible
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * Glasses-side dashboard (display only; interaction is done by the phone app):
 * five kinds of information in the bottom banner (connection / Root / camera / storage / LED),
 * tapping the banner (touchpad tap, or the DPAD center key/Enter) toggles expand/collapse;
 * the displayed content can be configured remotely from the phone (SET_DISPLAY_CONFIG).
 */
class MainActivity : AppCompatActivity() {

    private var expanded = true
    private var refreshJob: Job? = null
    private var serviceStarted = false

    private val graph: AppGraph get() = (application as App).graph

    private val onDisplayChanged = {
        syncPanelToConfig()
        refresh()
    }

    /** Sync the panel state on configuration change: all hidden → auto collapse; any visible row → auto expand. */
    private fun syncPanelToConfig() {
        val cfg = graph.displayConfig.config
        val anyVisible = cfg.conn || cfg.root || cfg.camera || cfg.storage || cfg.led
        if (anyVisible == expanded) return
        expanded = anyVisible
        applyPanelState()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        // keep the screen on: stop the glasses from sleeping during escalation/capture
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        /*
         * NOTE: we tried removing the system scrim at the bottom of the screen with "full-screen immersive" — **it did not work** (measured: still present).
         * That scrim is an extra layer (~12% white) the ROM overlays on **third-party apps**; system apps do not get it;
         * the app cannot remove it, so the workaround is to "move the dashboard text up out of those 48px" (see paddingBottom in
         * activity_main.xml and reports/...remote_diagnosis... §12).
         */
        window.statusBarColor = android.graphics.Color.TRANSPARENT
        window.navigationBarColor = android.graphics.Color.TRANSPARENT

        // tap the banner to toggle expand/collapse (a touchpad tap is an ordinary touch)
        findViewById<View>(R.id.panel).setOnClickListener { togglePanel() }

        requestPermissions()
        applyPanelState()
        refresh()
        currentInstance = this
    }

    override fun onDestroy() {
        super.onDestroy()
        if (currentInstance === this) currentInstance = null
    }

    override fun onResume() {
        super.onResume()
        // start the FGS only after the activity reaches the foreground, to avoid Background start not allowed
        if (!serviceStarted) {
            serviceStarted = true
            startForegroundService(Intent(this, CaptureService::class.java))
        }
        graph.displayConfig.addListener(onDisplayChanged)
        startPeriodicRefresh()
    }

    override fun onPause() {
        super.onPause()
        graph.displayConfig.removeListener(onDisplayChanged)
        refreshJob?.cancel()
        refreshJob = null
    }

    /** Touchpad keys (DPAD_CENTER / ENTER) also toggle expand/collapse. */
    override fun onKeyDown(keyCode: Int, event: KeyEvent?): Boolean {
        if (keyCode == KeyEvent.KEYCODE_DPAD_CENTER ||
            keyCode == KeyEvent.KEYCODE_ENTER ||
            keyCode == KeyEvent.KEYCODE_NUMPAD_ENTER
        ) {
            togglePanel()
            return true
        }
        return super.onKeyDown(keyCode, event)
    }

    private fun togglePanel() {
        expanded = !expanded
        applyPanelState()
    }

    /** For DebugReceiver (avoids input injection). */
    fun togglePanelPublic() = togglePanel()

    private fun applyPanelState() {
        findViewById<View>(R.id.collapsedBar).isVisible = !expanded
        findViewById<View>(R.id.expandedPanel).isVisible = expanded
    }

    private fun startPeriodicRefresh() {
        refreshJob?.cancel()
        refreshJob = lifecycleScope.launch {
            while (isActive) {
                refresh()
                delay(2000)
            }
        }
    }

    private fun refresh() {
        val cfg = graph.displayConfig.config
        val conn = graph.connection
        val camera = graph.camera

        // collapsed bar: connection dot
        findViewById<View>(R.id.dotConn).setBackgroundResource(
            if (conn != null) R.drawable.bg_dot_green else R.drawable.bg_dot_gray
        )

        setRow(R.id.rowConn, cfg.conn) {
            val bt = graph.btConnected
            val wifi = graph.wifiConnected
            findViewById<TextView>(R.id.tvConn).let { tv ->
                tv.text = when {
                    bt && wifi -> getString(R.string.dash_v_bt_wifi)
                    wifi -> getString(R.string.dash_v_wifi)
                    bt -> getString(R.string.dash_v_bt)
                    else -> getString(R.string.dash_v_none)
                }
                tv.setTextColor(colorOf(bt || wifi))
            }
        }

        setRow(R.id.rowRoot, cfg.root) {
            val g = graph.ghostlock
            findViewById<TextView>(R.id.tvRoot).let { tv ->
                tv.text = getString(
                    R.string.dash_v_root_fmt,
                    getString(if (g.rootAcquired) R.string.dash_v_root_yes else R.string.dash_v_root_no),
                    getString(if (g.isEnabled) R.string.dash_v_on else R.string.dash_v_off)
                )
                tv.setTextColor(colorOf(g.rootAcquired))
            }
        }

        setRow(R.id.rowCamera, cfg.camera) {
            findViewById<TextView>(R.id.tvCamera).let { tv ->
                tv.text = when {
                    camera == null -> getString(R.string.dash_v_cam_off)
                    camera.isRecording -> getString(R.string.dash_v_cam_rec)
                    else -> getString(R.string.dash_v_cam_ready)
                }
                tv.setTextColor(colorOf(camera != null))
            }
        }

        setRow(R.id.rowStorage, cfg.storage) {
            val media = graph.media
            findViewById<TextView>(R.id.tvStorage).let { tv ->
                val freeGb = media.storageFreeBytes() / (1024.0 * 1024.0 * 1024.0)
                tv.text = getString(R.string.dash_v_storage, String.format("%.1f GB", freeGb))
                tv.setTextColor(colorOf(true))
            }
        }

        setRow(R.id.rowLed, cfg.led) {
            findViewById<TextView>(R.id.tvLed).let { tv ->
                tv.text = getString(
                    R.string.dash_v_led_fmt,
                    getString(if (graph.ledOn) R.string.dash_v_on else R.string.dash_v_off)
                )
                tv.setTextColor(colorOf(!graph.ledOn)) // off = green (matches the privacy requirement)
            }
        }
    }

    private fun setRow(rowId: Int, visible: Boolean, update: () -> Unit) {
        findViewById<View>(rowId).isVisible = visible
        if (visible) update()
    }

    private fun colorOf(ok: Boolean): Int =
        ContextCompat.getColor(this, if (ok) R.color.dash_ok else R.color.dash_value)

    companion object {
        @Volatile
        var currentInstance: MainActivity? = null
            private set
    }

    private fun requestPermissions() {
        val needed = mutableListOf(Manifest.permission.CAMERA, Manifest.permission.RECORD_AUDIO)
        if (Build.VERSION.SDK_INT >= 31) needed += Manifest.permission.BLUETOOTH_CONNECT
        if (Build.VERSION.SDK_INT >= 33) needed += Manifest.permission.POST_NOTIFICATIONS
        val missing = needed.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (missing.isNotEmpty()) {
            ActivityCompat.requestPermissions(this, missing.toTypedArray(), 100)
        }
    }
}
