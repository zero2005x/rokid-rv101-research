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
 * 遙控分頁（重設計）：只放「拍攝當下必需」的東西，一屏可見不需要滾動。
 *
 * 內容：連線狀態卡（唯一狀態來源）／拍攝（拍照・錄影）／快速開關（靜音・隱私指示燈）／
 * 固定在下方且可捲動的操作紀錄。
 * 其餘設定類（WiFi、ghostlock、root、顯示配置、指示燈顏色、戒指、語言、裝置資訊）
 * 已移到「設定」分頁（[SettingsFragment]）。
 */
class RemoteFragment : Fragment() {

    private var rootV: View? = null
    private var recording = false
    private var syncing = false
    private var refreshJob: Job? = null

    /** 長按/放大檢視用的完整紀錄視窗。 */
    private var dialogLogView: TextView? = null
    private var logDialog: androidx.appcompat.app.AlertDialog? = null

    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?
    ): View {
        val v = inflater.inflate(R.layout.fragment_remote, container, false)
        rootV = v
        current = this

        // 連線：選擇眼鏡 / 斷線
        v.findViewById<View>(R.id.btnConnect).setOnClickListener {
            (activity as? MainActivity)?.pickDevice()
        }
        v.findViewById<View>(R.id.btnDisconnect).setOnClickListener {
            (activity as? MainActivity)?.disconnect()
            log(getString(R.string.log_disconnected))
        }

        // 拍照
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

        // 錄影（開始／停止）
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

        // 靜音拍攝（快速開關）
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

        // 隱私指示燈（快速開關；顏色選擇在設定分頁）
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

        // 操作紀錄：常駐面板（可捲動）＋放大/長按檢視完整紀錄
        val tvLog = v.findViewById<TextView>(R.id.tvLog)
        tvLog.movementMethod = ScrollingMovementMethod.getInstance()
        // 回呼可能來自背景執行緒（例如媒體前自動建立 WiFi 的紀錄）→ 一律 post 到主執行緒
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

        // 連線事件 → 更新 UI（連上／斷線皆由 MainActivity 統一接收 Remote.onTransportChanged；
        // 這裡不再自行指定 onDisconnected，否則會蓋掉那個集中註冊）
        // 眼鏡主動推送的進度（例如 LPE 階段）；Remote.onPush 會同時掛到藍牙與 WiFi 兩條通道
        Remote.onPush = { o ->
            if (com.rokid.rc.shared.protocol.Msg.type(o) == com.rokid.rc.shared.protocol.MsgType.PROGRESS) {
                val code = com.rokid.rc.shared.protocol.Msg.string(o, "code")
                val arg = com.rokid.rc.shared.protocol.Msg.string(o, "arg")
                val raw = com.rokid.rc.shared.protocol.Msg.string(o, "message")
                // 寫進 logcat 才能自動驗證「推播走哪條通道都送得到」（原本只進 App 內紀錄）
                android.util.Log.i("RemotePush", "progress code=$code arg=$arg")
                activity?.runOnUiThread {
                    // 眼鏡端只送代碼；以手機語系組字（未知代碼才退回原始 message）
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

    /** 切到本分頁／連線狀態改變時呼叫：更新連線卡與開關狀態。 */
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
                // 唯一狀態來源：裝置摘要（相簿／媒體／root／ghostlock）＋傳輸方式
                val transport = when {
                    Remote.wifiMode -> getString(
                        R.string.transport_wifi, Remote.wifiLink.apInfo.ip, Remote.wifiLink.apInfo.port
                    )
                    // 正在建立 WiFi（幾秒）：不要顯示成藍牙，否則傳輸很快但標示相反
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

    /** 放大檢視完整紀錄（可捲動、可選取複製、可清除）。 */
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

    /** DebugReceiver 測試用：外部灌入 log 行。 */
    fun logPublic(msg: String) = log(msg)

    companion object {
        @Volatile
        var current: RemoteFragment? = null
            private set
    }
}
