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
 * 眼鏡端儀表板（顯示為主，操作由手機 App）：
 * 底部橫幅五類資訊（連線 / Root / 相機 / 儲存 / 指示燈），
 * 點按橫幅（觸控板 tap 或 DPAD 中心鍵/Enter）切換展開/摺疊；
 * 顯示內容可由手機端遠端設定（SET_DISPLAY_CONFIG）。
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

    /** 配置變更時同步面板狀態：全隱藏→自動摺疊；有任何可見列→自動展開。 */
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
        // 螢幕常亮：避免眼鏡在提權/拍攝期間休眠
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        /*
         * 註：曾嘗試用「全螢幕沉浸」消除畫面底部那層系統 scrim —— **無效**（實測仍存在）。
         * 該 scrim 是 ROM 對**第三方 App** 額外疊上的一層（~12% 白），系統 App 不會有；
         * App 端無法移除，因此改用「把儀表板文字上移離開那 48px」的方式處理（見 activity_main.xml
         * 的 paddingBottom 與 reports/...remote_diagnosis... §12）。
         */
        window.statusBarColor = android.graphics.Color.TRANSPARENT
        window.navigationBarColor = android.graphics.Color.TRANSPARENT

        // 點按橫幅切換展開/摺疊（觸控板 tap 即一般 touch）
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
        // activity 進入前景後才啟動 FGS，避免 Background start not allowed
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

    /** 觸控板按鍵（DPAD_CENTER / ENTER）也可切換展開/摺疊。 */
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

    /** DebugReceiver 用（避免 input 注入）。 */
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

        // 摺疊條：連線圓點
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
                tv.setTextColor(colorOf(!graph.ledOn)) // 熄=綠（符合隱私需求）
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
