package com.rokid.rc.phone.ui

import android.graphics.Color
import android.os.Bundle
import android.text.method.ScrollingMovementMethod
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.core.view.isVisible
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import com.rokid.rc.phone.MainActivity
import com.rokid.rc.phone.R
import com.rokid.rc.phone.Remote
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

/**
 * Remote tab (redesigned): holds only what is "needed at capture time", visible on one screen without scrolling.
 *
 * Contents: connection status card (the single source of status)/capture (photo, video)/quick switches (silent, privacy LED)/
 * an operation log pinned at the bottom and scrollable.
 * The remaining settings-type items (WiFi, ghostlock, root, display config, LED colour, ring, language, device info)
 * have moved to the "Settings" tab ([SettingsFragment]).
 */
class RemoteFragment : Fragment() {

    private var rootV: View? = null
    private var recording = false
    private var syncing = false
    private var refreshJob: Job? = null

    /** Full log window for long-press/zoom viewing. */
    private var dialogLogView: TextView? = null
    private var logDialog: androidx.appcompat.app.AlertDialog? = null

    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?
    ): View {
        val v = inflater.inflate(R.layout.fragment_remote, container, false)
        rootV = v
        current = this

        // connection: pick glasses / disconnect
        v.findViewById<View>(R.id.btnConnect).setOnClickListener {
            (activity as? MainActivity)?.pickDevice()
        }
        v.findViewById<View>(R.id.btnDisconnect).setOnClickListener {
            (activity as? MainActivity)?.disconnect()
            log(getString(R.string.log_disconnected))
        }

        // take a photo
        v.findViewById<View>(R.id.btnPhoto).setOnClickListener {
            if (!Remote.isConnected) {
                toast(getString(R.string.toast_not_connected))
                return@setOnClickListener
            }
            launchUi {
                log(getString(R.string.log_capturing))
                Remote.capturePhoto().fold(
                    onSuccess = { m -> log(getString(R.string.log_photo_ok, m.fileName)) },
                    onFailure = { e -> log(getString(R.string.log_photo_fail, e.message ?: "")) }
                )
            }
        }

        // record video (start/stop)
        v.findViewById<View>(R.id.btnVideo).setOnClickListener {
            if (!Remote.isConnected) {
                toast(getString(R.string.toast_not_connected))
                return@setOnClickListener
            }
            launchUi {
                val btn = v.findViewById<com.google.android.material.button.MaterialButton>(R.id.btnVideo)
                if (!recording) {
                    Remote.startVideo().fold(
                        onSuccess = {
                            recording = true
                            btn.setText(R.string.btn_video_stop)
                            btn.setIconResource(R.drawable.ic_stop)
                            log(getString(R.string.log_recording))
                        },
                        onFailure = { e -> log(getString(R.string.log_record_fail, e.message ?: "")) }
                    )
                } else {
                    Remote.stopVideo().fold(
                        onSuccess = { m ->
                            recording = false
                            btn.setText(R.string.btn_video_start)
                            btn.setIconResource(R.drawable.ic_videocam)
                            log(getString(R.string.log_video_saved, m.fileName))
                        },
                        onFailure = { e ->
                            recording = false
                            btn.setText(R.string.btn_video_start)
                            log(getString(R.string.log_stop_fail, e.message ?: ""))
                        }
                    )
                }
            }
        }

        // silent capture (quick switch)
        v.findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.swSilent)
            .setOnCheckedChangeListener { _, checked ->
                if (syncing) return@setOnCheckedChangeListener
                launchUi {
                    Remote.setSilent(checked).fold(
                        onSuccess = { applied ->
                            log(
                                when {
                                    checked && applied -> getString(R.string.silent_applied)
                                    checked -> getString(R.string.silent_pending)
                                    else -> getString(R.string.silent_disabled)
                                }
                            )
                        },
                        onFailure = { e ->
                            log(getString(R.string.log_silent_fail, e.message ?: ""))
                            syncing = true
                            rootV?.findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.swSilent)
                                ?.isChecked = !checked
                            syncing = false
                        }
                    )
                }
            }

        // privacy indicator LED (quick switch; the colour choice is on the settings tab)
        v.findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.swLed)
            .setOnCheckedChangeListener { _, checked ->
                if (syncing) return@setOnCheckedChangeListener
                launchUi {
                    log(getString(if (checked) R.string.log_led_enabling else R.string.log_led_disabling))
                    Remote.setLed(checked).fold(
                        onSuccess = { log(getString(if (checked) R.string.log_led_on_ok else R.string.log_led_off_ok)) },
                        onFailure = { e ->
                            log(getString(R.string.log_led_fail, e.message ?: ""))
                            syncing = true
                            rootV?.findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.swLed)
                                ?.isChecked = !checked
                            syncing = false
                        }
                    )
                }
            }

        // operation log: a permanent panel (scrollable) plus zoom/long-press to view the full log
        val tvLog = v.findViewById<TextView>(R.id.tvLog)
        tvLog.movementMethod = ScrollingMovementMethod.getInstance()
        // the callback may come from a background thread (e.g. the record of auto-establishing WiFi before media) → always post to the main thread
        LogStore.onChanged = { text ->
            view?.let { rv ->
                rv.post {
                    rv.findViewById<TextView>(R.id.tvLog)?.let { tv ->
                        tv.text = text
                        scrollLogToBottom(tv)
                    }
                }
            }
        }
        if (LogStore.text.isBlank()) LogStore.append(getString(R.string.log_app_started))
        tvLog.text = LogStore.text
        tvLog.setOnLongClickListener {
            showLogDialog()
            true
        }
        v.findViewById<View>(R.id.btnLogExpand).setOnClickListener { showLogDialog() }
        v.findViewById<View>(R.id.btnClearLog).setOnClickListener {
            LogStore.clear()
        }

        // connection events → update the UI (both connect and disconnect are received centrally by MainActivity through Remote.onTransportChanged;
        // do not set onDisconnected here, or it would override that central registration)
        // progress pushed proactively by the glasses (e.g. LPE stages); Remote.onPush is attached to both the Bluetooth and the WiFi channel
        Remote.onPush = { o ->
            if (com.rokid.rc.shared.protocol.Msg.type(o) == com.rokid.rc.shared.protocol.MsgType.PROGRESS) {
                val code = com.rokid.rc.shared.protocol.Msg.string(o, "code")
                val arg = com.rokid.rc.shared.protocol.Msg.string(o, "arg")
                val raw = com.rokid.rc.shared.protocol.Msg.string(o, "message")
                // log to logcat so that "a push arrives over either channel" can be verified automatically (previously it only went into the in-app log)
                android.util.Log.i("RemotePush", "progress code=$code arg=$arg")
                activity?.runOnUiThread {
                    // the glasses send only codes; compose the text in the phone's locale (fall back to the raw message only for unknown codes)
                    val text = GhostlockText.progress(requireContext(), code, arg) ?: raw
                    log(getString(R.string.log_progress, text))
                }
            }
        }

        refresh()
        return v
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    override fun onDestroyView() {
        if (LogStore.onChanged != null) LogStore.onChanged = null
        if (current === this) current = null
        refreshJob?.cancel()
        logDialog?.dismiss()
        logDialog = null
        dialogLogView = null
        rootV = null
        super.onDestroyView()
    }

    fun onConnectionChanged() = refresh()

    /** Called when switching to this tab / when the connection state changes: update the connection card and switch states. */
    @Suppress("MissingPermission")
    fun refresh() {
        val v = rootV ?: return
        refreshJob?.cancel()
        refreshJob = viewLifecycleOwner.lifecycleScope.launch {
            val dot = v.findViewById<View>(R.id.vConnDot)
            val connected = Remote.isConnected
            val name = Remote.device?.name

            dot.setBackgroundColor(if (connected) Color.GREEN else Color.GRAY)
            v.findViewById<TextView>(R.id.tvDevice).text = name ?: getString(R.string.conn_no_device)
            v.findViewById<TextView>(R.id.tvConnState).text = getString(
                if (connected) R.string.conn_connected else R.string.conn_disconnected
            )
            v.findViewById<View>(R.id.btnDisconnect).isVisible = connected

            if (!connected) {
                v.findViewById<View>(R.id.tvSummary).visibility = View.GONE
                return@launch
            }

            Remote.status().onSuccess { s ->
                syncing = true
                v.findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.swLed).isChecked = s.ledOn
                v.findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.swSilent).isChecked = s.silentEnabled
                syncing = false
                // single source of status: device summary (albums/media/root/ghostlock) plus transport
                val transport = when {
                    Remote.wifiMode -> getString(
                        R.string.transport_wifi, Remote.wifiLink.apInfo.ip, Remote.wifiLink.apInfo.port
                    )
                    // WiFi is being established (a few seconds): do not show Bluetooth, otherwise the transfer is fast but the label says the opposite
                    Remote.wifiNegotiating -> getString(R.string.transport_wifi_connecting)
                    else -> getString(R.string.transport_bt)
                }
                val sum = getString(
                    R.string.status_summary,
                    s.albumCount,
                    s.mediaCount,
                    if (s.rootAcquired) "✓" else "—",
                    getString(if (s.ghostlockEnabled) R.string.state_enabled else R.string.state_disabled)
                )
                v.findViewById<TextView>(R.id.tvSummary).text = "$transport · $sum"
                v.findViewById<View>(R.id.tvSummary).visibility = View.VISIBLE
            }
        }
    }

    private fun launchUi(block: suspend () -> Unit) {
        viewLifecycleOwner.lifecycleScope.launch { block() }
    }

    /** Zoomed view of the full log (scrollable, selectable for copying, clearable). */
    private fun showLogDialog() {
        if (logDialog?.isShowing == true) return
        val view = LayoutInflater.from(requireContext()).inflate(R.layout.dialog_log, null, false)
        val tv = view.findViewById<TextView>(R.id.tvDialogLog)
        tv.movementMethod = ScrollingMovementMethod.getInstance()
        tv.text = LogStore.text
        view.findViewById<View>(R.id.btnDialogClear).setOnClickListener {
            LogStore.clear()
            tv.text = ""
        }
        dialogLogView = tv
        logDialog = androidx.appcompat.app.AlertDialog.Builder(requireContext())
            .setView(view)
            .setPositiveButton(R.string.dialog_close, null)
            .setOnDismissListener {
                dialogLogView = null
                logDialog = null
            }
            .create()
            .also { it.show() }
    }

    private fun log(msg: String) {
        LogStore.append(msg)
        rootV?.findViewById<TextView>(R.id.tvLog)?.let { tv ->
            tv.text = LogStore.text
            tv.post { scrollLogToBottom(tv) }
        }
    }

    private fun scrollLogToBottom(tv: TextView) {
        val parent = tv.parent as? android.widget.ScrollView ?: return
        parent.post { parent.fullScroll(View.FOCUS_DOWN) }
    }

    private fun toast(msg: String) {
        (activity as? MainActivity)?.toast(msg)
    }

    /** For DebugReceiver testing: inject a log line from outside. */
    fun logPublic(msg: String) = log(msg)

    companion object {
        @Volatile
        var current: RemoteFragment? = null
            private set
    }
}
