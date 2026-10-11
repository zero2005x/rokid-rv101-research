package com.rokid.rc.phone.ui

import android.os.Bundle
import android.util.Log
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import com.google.android.material.chip.Chip
import com.rokid.rc.phone.LocaleHelper
import com.rokid.rc.phone.MainActivity
import com.rokid.rc.phone.R
import com.rokid.rc.phone.Remote
import com.rokid.rc.phone.ThemeHelper
import com.rokid.rc.phone.WifiSetupException
import com.rokid.rc.phone.ring.RingController
import com.rokid.rc.shared.protocol.DisplayConfig
import com.rokid.rc.shared.protocol.RootMode
import com.rokid.rc.shared.ring.RingAction
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

/**
 * 設定分頁（新）：集中所有非拍攝當下必需的設定，依功能分組、樣式統一。
 *
 * 分組：連線（WiFi／裝置資訊）、拍攝與權限（靜音／ghostlock／root）、隱私指示燈顏色、
 * 眼鏡儀表板顯示內容、戒指控制（手機端／眼鏡端）、語言。
 */
class SettingsFragment : Fragment() {

    private var rootV: View? = null
    private var syncing = false
    private var refreshJob: Job? = null
    private var dispCfg = DisplayConfig()
    private var rootStateView: TextView? = null
    private var rootMetricsView: TextView? = null
    /** 最近一次狀態（供操作指引對話框使用）。 */
    private var lastStatus: com.rokid.rc.shared.protocol.Status? = null

    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?
    ): View {
        val v = inflater.inflate(R.layout.fragment_settings, container, false)
        rootV = v

        // ---------- 連線 ----------
        v.findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.swWifi)
            .setOnCheckedChangeListener { _, checked ->
                if (syncing) return@setOnCheckedChangeListener
                launchUi { toggleWifi(checked) }
            }
        v.findViewById<View>(R.id.btnDeviceInfo).setOnClickListener {
            launchUi {
                log(getString(R.string.log_querying))
                Remote.deviceInfo().fold(
                    onSuccess = { info ->
                        val s = StringBuilder()
                        s.append("SELinux: ").append(info.optString("selinux")).append('\n')
                        s.append("root: ").append(info.optBoolean("rootAcquired")).append('\n')
                        s.append("ghostlock: ").append(info.optBoolean("ghostlockEnabled")).append('\n')
                        s.append("mode: ").append(info.optString("rootMode"))
                        log(s.toString())
                    },
                    onFailure = { e -> log(getString(R.string.log_query_fail, e.message ?: "")) }
                )
            }
        }

        // ---------- 拍攝與權限 ----------
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
                            revertSwitch(R.id.swSilent, !checked)
                        }
                    )
                }
            }
        v.findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.swGhost)
            .setOnCheckedChangeListener { _, checked ->
                if (syncing) return@setOnCheckedChangeListener
                launchUi {
                    Remote.setGhostlock(checked).onFailure { e ->
                        log(getString(R.string.log_ghost_fail, e.message ?: ""))
                        revertSwitch(R.id.swGhost, !checked)
                    }
                }
            }
        v.findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.swRootAuto)
            .setOnCheckedChangeListener { _, checked ->
                if (syncing) return@setOnCheckedChangeListener
                launchUi {
                    Remote.setRootMode(if (checked) RootMode.AUTO else RootMode.MANUAL).onFailure { e ->
                        log(getString(R.string.log_root_mode_fail, e.message ?: ""))
                        revertSwitch(R.id.swRootAuto, !checked)
                    }
                }
            }
        v.findViewById<View>(R.id.btnAcquireRoot).setOnClickListener {
            launchUi {
                log(getString(R.string.log_root_acquiring))
                Remote.acquireRoot().fold(
                    onSuccess = { ok -> log(getString(if (ok) R.string.log_root_ready else R.string.log_root_failed)) },
                    onFailure = { e -> log(getString(R.string.log_root_fail, e.message ?: "")) }
                )
            }
        }

        // ---------- root 狀態（P0-b）：點一下顯示操作指引 ----------
        v.findViewById<TextView>(R.id.tvRootState).setOnClickListener { showRootHelpDialog() }
        rootStateView = v.findViewById(R.id.tvRootState)
        rootMetricsView = v.findViewById(R.id.tvRootMetrics)

        // ---------- 隱私指示燈顏色 ----------
        bindLedChip(v, R.id.chipLedW, "white")
        bindLedChip(v, R.id.chipLedG, "green")
        bindLedChip(v, R.id.chipLedB, "blue")
        bindLedChip(v, R.id.chipLedR, "red")

        // ---------- 眼鏡儀表板顯示內容 ----------
        bindDispChip(v, R.id.chipDispConn) { it.copy(conn = it.conn.not()) }
        bindDispChip(v, R.id.chipDispRoot) { it.copy(root = it.root.not()) }
        bindDispChip(v, R.id.chipDispCamera) { it.copy(camera = it.camera.not()) }
        bindDispChip(v, R.id.chipDispStorage) { it.copy(storage = it.storage.not()) }
        bindDispChip(v, R.id.chipDispLed) { it.copy(led = it.led.not()) }
        v.findViewById<View>(R.id.btnDispAll).setOnClickListener { applyDisp(DisplayConfig()) }
        v.findViewById<View>(R.id.btnDispNone).setOnClickListener {
            applyDisp(DisplayConfig(conn = false, root = false, camera = false, storage = false, led = false))
        }

        // ---------- 戒指控制 ----------
        val ring = RingController
        ring.ensureInit(requireContext())
        val tvRingStatus = v.findViewById<TextView>(R.id.tvRingStatus)
        v.findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.swRing)
            .apply { isChecked = ring.enabled }
            .setOnCheckedChangeListener { _, checked ->
                ring.setEnabled(requireContext(), checked)
                log(
                    getString(
                        R.string.ring_a_label
                    ) + "：" + getString(if (checked) R.string.ring_on else R.string.ring_off)
                )
            }
        ring.onStateChanged = { msg ->
            view?.findViewById<TextView>(R.id.tvRingStatus)?.text =
                getString(R.string.ring_phone_prefix, getString(R.string.ring_status_fmt, ring.count, msg))
        }
        tvRingStatus.text = getString(
            R.string.ring_phone_prefix,
            ring.summary.ifBlank { getString(R.string.ring_status_idle) }
        )

        v.findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.swRingGlasses)
            .setOnCheckedChangeListener { _, checked ->
                if (syncing) return@setOnCheckedChangeListener
                launchUi {
                    Remote.setRing(checked).fold(
                        onSuccess = { running ->
                            log(getString(R.string.ring_glasses_status, getString(if (running) R.string.ring_on else R.string.ring_off)))
                            refresh()
                        },
                        onFailure = { e ->
                            log(getString(R.string.ring_glasses_status, "✗ ${e.message}"))
                            revertSwitch(R.id.swRingGlasses, !checked)
                        }
                    )
                }
            }
        val btnLearn = v.findViewById<View>(R.id.btnRingLearn)
        btnLearn.setOnClickListener {
            toast(getString(R.string.ring_learn_hint))
            ring.startLearn { code, desc ->
                activity?.runOnUiThread {
                    tvRingStatus.text = getString(R.string.ring_learn_result, desc)
                    log(getString(R.string.ring_learn_result, desc))
                    showRingActionPicker(code, desc, tvRingStatus)
                }
            }
        }
        // 長按＝清除所有自訂鍵碼對應
        btnLearn.setOnLongClickListener {
            ring.clearKeyMappings()
            toast(getString(R.string.ring_map_cleared))
            log(getString(R.string.ring_map_cleared))
            true
        }
        v.findViewById<View>(R.id.btnRingPair).setOnClickListener { showRingPicker(tvRingStatus) }

        // ---------- 語言 ----------
        val btnLang = v.findViewById<com.google.android.material.button.MaterialButton>(R.id.btnLanguage)
        btnLang.text = getString(R.string.btn_language, currentLanguageLabel())
        btnLang.setOnClickListener { showLanguageDialog() }

        // ---------- 可折疊分組（狀態記憶於 prefs） ----------
        bindCollapsible(v, R.id.headerConn, R.id.chevronConn, R.id.contentConn, "conn", defaultOpen = true)
        bindCollapsible(v, R.id.headerCapture, R.id.chevronCapture, R.id.contentCapture, "capture", defaultOpen = true)
        bindCollapsible(v, R.id.headerLed, R.id.chevronLed, R.id.contentLed, "led", defaultOpen = false)
        bindCollapsible(v, R.id.headerDisplay, R.id.chevronDisplay, R.id.contentDisplay, "display", defaultOpen = false)
        bindCollapsible(v, R.id.headerRing, R.id.chevronRing, R.id.contentRing, "ring", defaultOpen = false)
        bindCollapsible(v, R.id.headerAppearance, R.id.chevronAppearance, R.id.contentAppearance, "appearance", defaultOpen = false)
        bindCollapsible(v, R.id.headerLang, R.id.chevronLang, R.id.contentLang, "lang", defaultOpen = false)

        // ---------- 外觀：淺色模式 ----------
        v.findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.swLight)
            .apply { isChecked = ThemeHelper.isLight(requireContext()) }
            .setOnCheckedChangeListener { _, checked ->
                if (checked == ThemeHelper.isLight(requireContext())) return@setOnCheckedChangeListener
                ThemeHelper.setLight(requireContext(), checked)
                log(getString(R.string.light_mode_label) + "：" + getString(if (checked) R.string.ring_on else R.string.ring_off))
                requireActivity().recreate()   // 立即套用新主題
            }

        refresh()
        return v
    }

    /**
     * 綁定可折疊分組：點標題列切換內容顯示，箭頭旋轉 180°，
     * 展開狀態存 prefs（下次進來保持一致）。
     */
    private fun bindCollapsible(
        root: View, headerId: Int, chevronId: Int, contentId: Int, key: String, defaultOpen: Boolean
    ) {
        val prefs = requireContext().getSharedPreferences("settings_groups", android.content.Context.MODE_PRIVATE)
        val header = root.findViewById<View>(headerId)
        val chevron = root.findViewById<View>(chevronId)
        val content = root.findViewById<View>(contentId)
        var open = prefs.getBoolean(key, defaultOpen)
        fun render() {
            content.visibility = if (open) View.VISIBLE else View.GONE
            chevron.animate().rotation(if (open) 180f else 0f).setDuration(150).start()
        }
        render()
        header.setOnClickListener {
            open = !open
            prefs.edit().putBoolean(key, open).apply()
            render()
        }
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    override fun onDestroyView() {
        if (RingController.onStateChanged != null) RingController.onStateChanged = null
        refreshJob?.cancel()
        rootV = null
        super.onDestroyView()
    }

    /** 讀回眼鏡狀態以同步開關與顯示。 */
    fun refresh() {
        val v = rootV ?: return
        refreshJob?.cancel()
        refreshJob = viewLifecycleOwner.lifecycleScope.launch {
            syncing = true
            // WiFi 傳輸開關
            val swWifi = v.findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.swWifi)
            swWifi.isChecked = Remote.autoWifi
            v.findViewById<TextView>(R.id.tvWifiHint).text = when {
                Remote.wifiMode -> getString(
                    R.string.transport_wifi, Remote.wifiLink.apInfo.ip, Remote.wifiLink.apInfo.port
                )
                Remote.wifiNegotiating -> getString(R.string.transport_wifi_connecting)
                // 已開啟自動模式但尚未連上：說明下一次媒體操作會自動切到 WiFi
                Remote.autoWifi -> getString(R.string.wifi_switch_hint)
                else -> getString(R.string.transport_bt)
            }
            syncing = false

            if (!Remote.isConnected) return@launch
            Remote.status().onSuccess { s ->
                lastStatus = s
                updateRootState(s)
                syncing = true
                v.findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.swSilent).isChecked = s.silentEnabled
                v.findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.swGhost).isChecked = s.ghostlockEnabled
                v.findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.swRootAuto).isChecked = s.rootMode == RootMode.AUTO.name
                v.findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.swRingGlasses).isChecked = s.ringEnabled
                syncing = false
                v.findViewById<TextView>(R.id.tvRingGlassesStatus).text = getString(
                    R.string.ring_glasses_status,
                    buildString {
                        append(if (s.ringRunning) getString(R.string.state_running) else getString(R.string.state_stopped))
                        s.ringDevice?.let { append(" · ").append(it) }
                        RingText.result(requireContext(), s.ringLastKey, s.ringLastArg)
                            ?.let { append(" · ").append(it) }
                    }
                )
            }
            Remote.fetchDisplayConfig().onSuccess { cfg ->
                dispCfg = cfg
                rootV?.let { setDispChips(it, cfg) }
            }
        }
    }

    /** 更新 root 狀態列與度量（P0-b／P2）。 */
    private fun updateRootState(s: com.rokid.rc.shared.protocol.Status) {
        val ctx = context ?: return
        rootStateView?.text = RootText.state(ctx, s.rootState)
        val lines = listOfNotNull(
            RootText.hint(ctx, s.rootState, s.rootHint),
            RootText.metrics(ctx, s),
            RootText.keeper(ctx, s)
        )
        rootMetricsView?.text = lines.joinToString("\n")
    }

    /**
     * root 操作指引（P0-b）：明確告訴使用者「現在缺什麼、要跑哪支腳本」。
     * 沒有 root 時，靜音拍攝／隱私燈抑制／WiFi 媒體傳輸／眼鏡端戒指都會失效。
     */
    private fun showRootHelpDialog() {
        val ctx = context ?: return
        val s = lastStatus ?: return
        val sb = StringBuilder()
        sb.append(RootText.state(ctx, s?.rootState)).append('\n')
        RootText.hint(ctx, s?.rootState, s?.rootHint)?.let { sb.append(it).append('\n') }
        RootText.metrics(ctx, s)?.let { sb.append(it).append('\n') }
        RootText.keeper(ctx, s)?.let { sb.append(it).append('\n') }
        sb.append('\n').append(getString(R.string.root_help_body))
        com.google.android.material.dialog.MaterialAlertDialogBuilder(ctx)
            .setTitle(R.string.sec_root_status)
            .setMessage(sb.toString().trim())
            .setPositiveButton(R.string.dialog_close, null)
            .show()
    }

    // ---------------- WiFi ----------------

    private suspend fun toggleWifi(enable: Boolean) {
        // 記住偏好：往後每次媒體傳輸（縮圖／下載）都會自動確保 WiFi 通道
        Remote.setAutoWifi(enable)
        if (!enable) {
            Remote.closeWifiMedia()
            log(getString(R.string.log_wifi_off))
            refresh()
            return
        }
        log(getString(R.string.log_wifi_enabling, Remote.wifiLink.apInfo.ssid))
        Remote.ensureWifiMedia().fold(
            onSuccess = { ap ->
                log(getString(R.string.log_wifi_enabled, ap.ip, ap.port))
                refresh()
            },
            onFailure = { e ->
                val ap = Remote.wifiLink.apInfo
                val msg = when ((e as? WifiSetupException)?.code) {
                    WifiSetupException.Code.AP_JOIN_MANUAL -> getString(R.string.wifi_failed_manual, ap.ssid, ap.pass)
                    WifiSetupException.Code.AP_NOT_READY -> getString(R.string.wifi_ap_not_ready)
                    WifiSetupException.Code.AP_TIMEOUT -> getString(R.string.wifi_ap_timeout)
                    WifiSetupException.Code.TCP -> getString(R.string.wifi_tcp_failed, e.message ?: "")
                    WifiSetupException.Code.NOT_CONNECTED -> getString(R.string.toast_not_connected)
                    else -> e.message ?: getString(R.string.err_unknown)
                }
                log(getString(R.string.log_wifi_failed, msg))
                toast(msg)
                refresh()
            }
        )
    }

    // ---------------- 指示燈顏色 ----------------

    private fun bindLedChip(v: View, id: Int, color: String) {
        val chip = v.findViewById<Chip>(id)
        chip.setOnCheckedChangeListener { _, checked ->
            if (syncing) return@setOnCheckedChangeListener
            launchUi {
                log("${ledName(color)} ${getString(if (checked) R.string.state_on else R.string.state_off)}…")
                Remote.setLed(checked, color).fold(
                    onSuccess = {
                        log("✓ ${ledName(color)}：${getString(if (checked) R.string.state_on else R.string.state_off)}")
                    },
                    onFailure = { e ->
                        log(getString(R.string.log_led_switch_fail, ledName(color), e.message ?: ""))
                        syncing = true
                        chip.isChecked = !checked
                        syncing = false
                    }
                )
            }
        }
    }

    private fun colorLabel(color: String): String = getString(
        when (color) {
            "white" -> R.string.chip_white
            "green" -> R.string.chip_green
            "blue" -> R.string.chip_blue
            else -> R.string.chip_red
        }
    )

    private fun ledName(color: String): String = getString(R.string.led_name, colorLabel(color))

    // ---------------- 顯示配置 ----------------

    private fun bindDispChip(v: View, id: Int, toggle: (DisplayConfig) -> DisplayConfig) {
        val chip = v.findViewById<Chip>(id)
        chip.setOnCheckedChangeListener { _, checked ->
            if (syncing) return@setOnCheckedChangeListener
            val expected = when (id) {
                R.id.chipDispConn -> dispCfg.conn
                R.id.chipDispRoot -> dispCfg.root
                R.id.chipDispCamera -> dispCfg.camera
                R.id.chipDispStorage -> dispCfg.storage
                else -> dispCfg.led
            }
            if (checked == expected) return@setOnCheckedChangeListener
            applyDisp(toggle(dispCfg))
        }
    }

    private fun applyDisp(new: DisplayConfig) {
        dispCfg = new
        rootV?.let { setDispChips(it, new) }
        launchUi {
            Remote.setDisplayConfig(new).fold(
                onSuccess = { log(getString(R.string.log_display_ok)) },
                onFailure = { e -> log(getString(R.string.log_display_fail, e.message ?: "")) }
            )
        }
    }

    private fun setDispChips(v: View, cfg: DisplayConfig) {
        syncing = true
        v.findViewById<Chip>(R.id.chipDispConn).isChecked = cfg.conn
        v.findViewById<Chip>(R.id.chipDispRoot).isChecked = cfg.root
        v.findViewById<Chip>(R.id.chipDispCamera).isChecked = cfg.camera
        v.findViewById<Chip>(R.id.chipDispStorage).isChecked = cfg.storage
        v.findViewById<Chip>(R.id.chipDispLed).isChecked = cfg.led
        syncing = false
    }

    // ---------------- 語言 ----------------

    private fun currentLanguageLabel(): String {
        val cur = LocaleHelper.current(requireContext())
        if (cur.isNullOrEmpty()) return getString(R.string.lang_system)
        return LocaleHelper.LANGUAGES.firstOrNull { it.first.equals(cur, ignoreCase = true) }?.second ?: cur
    }

    private fun showLanguageDialog() {
        val ctx = requireContext()
        val labels = mutableListOf(getString(R.string.lang_system))
        LocaleHelper.LANGUAGES.forEach { labels += it.second }
        androidx.appcompat.app.AlertDialog.Builder(ctx)
            .setTitle(R.string.lang_dialog_title)
            .setSingleChoiceItems(labels.toTypedArray(), LocaleHelper.currentIndex(ctx)) { dialog, which ->
                val tag = if (which == 0) null else LocaleHelper.LANGUAGES[which - 1].first
                LocaleHelper.set(ctx, tag)
                dialog.dismiss()
                log(getString(R.string.log_lang_switched, labels[which]))
                requireActivity().recreate()
            }
            .setNegativeButton(R.string.action_cancel, null)
            .show()
    }

    /** 學習到鍵碼後，選擇要綁定的動作（可學習的鍵碼對應）。 */
    /**
     * 「配對戒指」＝**列出已配對的 HID 裝置讓使用者選**（2026-09-13 改版）。
     *
     * 舊版用 `scanAndBond(pattern = "RGR")`，對已配對的戒指**永遠不會成功**：
     *   1. 已配對的 BLE 周邊不再對外廣播 → 掃描清單根本不會出現它；
     *   2. 名稱不含 "RGR" 者（例如 `AIVELA_RP13EJ37`）也永遠不符。
     * 見 `reports/aivela_ring_connection_failure_2026-09-13.md`。
     *
     * 只有要配「全新、尚未配對」的裝置時才做掃描，且掃描結果**不套名稱過濾**，
     * 一律由使用者從清單中挑選。
     */
    private fun showRingPicker(status: TextView) {
        val ctx = requireContext()
        val bonded = com.rokid.rc.shared.ring.RingBonder.bondedHidDevices(ctx)
        val labels = ArrayList<String>()
        val actions = ArrayList<() -> Unit>()

        bonded.forEach { d ->
            val name = com.rokid.rc.shared.ring.RingBonder.displayName(d)
            val mac = d.address ?: "?"
            val bondedMark = if (d.bondState == android.bluetooth.BluetoothDevice.BOND_BONDED) "✓" else "…"
            labels += "$bondedMark $name\n$mac"
            actions += { useRingDevice(d, status) }
        }
        if (bonded.isEmpty()) {
            labels += getString(R.string.ring_pick_empty)
            actions += { toast(getString(R.string.ring_pick_empty)) }
        }
        labels += getString(R.string.ring_pick_scan)
        actions += { scanAndPickRing(status) }

        androidx.appcompat.app.AlertDialog.Builder(ctx)
            .setTitle(R.string.ring_pick_title)
            .setItems(labels.toTypedArray()) { _, i -> actions[i].invoke() }
            .setNegativeButton(R.string.action_cancel, null)
            .show()
    }

    /** 使用者從清單選定裝置：記住它（唯一來源）＋必要時完成配對。 */
    private fun useRingDevice(device: android.bluetooth.BluetoothDevice, status: TextView) {
        val ctx = requireContext()
        val name = com.rokid.rc.shared.ring.RingBonder.displayName(device)
        val mac = device.address ?: "?"
        RingController.setApprovedDevice(ctx, name, mac)
        val msg = getString(R.string.ring_pick_selected, "$name（$mac）")
        status.text = msg
        log(msg)
        Log.i("SettingsFragment", "ring approved: $name $mac bondState=${device.bondState}")
        if (device.bondState != android.bluetooth.BluetoothDevice.BOND_BONDED) {
            com.rokid.rc.shared.ring.RingBonder.bond(ctx, device) { ok, note ->
                activity?.runOnUiThread {
                    val m = if (ok) getString(R.string.ring_pick_bonded, name)
                    else getString(R.string.ring_pick_fail, note)
                    status.text = m
                    log(m)
                }
            }
        }
    }

    /** 配「全新裝置」：掃描所有可連線裝置（不過濾名稱），由使用者挑選。 */
    private fun scanAndPickRing(status: TextView) {
        val ctx = requireContext()
        val scanning = getString(R.string.ring_pick_scanning)
        toast(scanning)
        status.text = scanning
        com.rokid.rc.shared.ring.RingScanner(ctx).scan(12_000L) { list ->
            activity?.runOnUiThread {
                val cand = list.filter { it.connectable != false && it.device != null }
                if (cand.isEmpty()) {
                    val m = getString(R.string.ring_pick_empty)
                    status.text = m
                    log(m)
                    return@runOnUiThread
                }
                val labels = cand.map { "${it.name ?: "-"}\n${it.mac}   rssi=${it.rssi}" }.toTypedArray()
                androidx.appcompat.app.AlertDialog.Builder(ctx)
                    .setTitle(R.string.ring_pick_title)
                    .setItems(labels) { _, i -> useRingDevice(cand[i].device!!, status) }
                    .setNegativeButton(R.string.action_cancel, null)
                    .show()
            }
        }
    }

    private fun showRingActionPicker(code: Int, desc: String, statusView: TextView) {
        val actions = listOf(
            RingAction.PHOTO to R.string.ring_action_photo,
            RingAction.VIDEO_TOGGLE to R.string.ring_action_video,
            RingAction.SILENT_TOGGLE to R.string.ring_action_silent,
            RingAction.DISPLAY_NEXT to R.string.ring_action_display_next,
            RingAction.DISPLAY_PREV to R.string.ring_action_display_prev,
            RingAction.NONE to R.string.ring_action_none
        )
        val labels = actions.map { getString(it.second) }.toTypedArray()
        androidx.appcompat.app.AlertDialog.Builder(requireContext())
            .setTitle(getString(R.string.ring_map_title) + "\n" + desc)
            .setItems(labels) { _, which ->
                val action = actions[which].first
                RingController.setKeyMapping(code, action)
                val msg = getString(R.string.ring_map_saved, desc, labels[which])
                statusView.text = msg
                log(msg)
            }
            .setNegativeButton(R.string.action_cancel, null)
            .show()
    }

    // ---------------- 共用小工具 ----------------

    private fun revertSwitch(id: Int, value: Boolean) {
        syncing = true
        rootV?.findViewById<com.google.android.material.materialswitch.MaterialSwitch>(id)?.isChecked = value
        syncing = false
    }

    private fun launchUi(block: suspend () -> Unit) {
        viewLifecycleOwner.lifecycleScope.launch { block() }
    }

    private fun log(msg: String) = LogStore.append(msg)

    private fun toast(msg: String) {
        (activity as? MainActivity)?.toast(msg)
    }
}
