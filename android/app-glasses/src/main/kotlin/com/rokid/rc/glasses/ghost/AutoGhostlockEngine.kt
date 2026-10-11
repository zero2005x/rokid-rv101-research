package com.rokid.rc.glasses.ghost

import android.content.Context
import android.util.Log
import com.rokid.rc.shared.protocol.GhostlockCodes
import java.io.File

/**
 * 執行期引擎選擇（**P1**）：不再用編譯期常數 `USE_ENGINE`。
 *
 * 每次呼叫時依「**現在哪條觸發管線活著**」挑引擎並委派：
 * | 順序 | 條件 | 引擎 | 說明 |
 * |------|------|------|------|
 * | 1 | Shizuku server 在跑 | [ShizukuGhostlockEngine] | 以 shell 身分跑 exploit；內部失敗會回退 bridge |
 * | 2 | bridge helper 常駐 | [BridgeGhostlockEngine] | `deploy/root_bridge.sh` 常駐迴圈 |
 * | 3 | 皆無 | [ShizukuGhostlockEngine] | 由它回報「Shizuku 未執行」並嘗試回退，訊息最完整 |
 *
 * 若 root daemon 已經活著，任何引擎的 `acquireRoot()` 都會走快速路徑直接回報成功（跳過 LPE）。
 *
 * 這樣換環境（先跑 start_shizuku 或 root_bridge）**不需要重新建置 APK**，
 * 且失敗時回報的原因對應實際探測結果。
 */
class AutoGhostlockEngine(private val app: Context) : GhostlockEngine {

    private val shizuku = ShizukuGhostlockEngine(app, fallback = BridgeGhostlockEngine())
    private val bridge = BridgeGhostlockEngine()

    /** 最近一次實際使用的引擎名稱（`shizuku`／`bridge`），供 P2 度量與狀態顯示。 */
    @Volatile
    var lastEngineName: String = "none"
        private set

    /** 最近一次的選擇原因（結構化代碼，供手機顯示）。 */
    @Volatile
    var lastReasonCode: String? = null
        private set

    override val isEnabled: Boolean
        get() = current().isEnabled

    override val rootAcquired: Boolean
        get() = shizuku.rootAcquired || bridge.rootAcquired

    override var onProgress: ((com.rokid.rc.shared.protocol.ProgressEvent) -> Unit)?
        get() = shizuku.onProgress
        set(value) {
            shizuku.onProgress = value
            bridge.onProgress = value
        }

    override fun setEnabled(enabled: Boolean) {
        shizuku.setEnabled(enabled)
        bridge.setEnabled(enabled)
    }

    override fun doneLog(): File = File(com.rokid.rc.glasses.GlassesPaths.TMP, "root_done.log")

    override suspend fun exec(payload: RootScript): ExecResult {
        val engine = resolve()
        Log.i(TAG, "exec via $lastEngineName (${lastReasonCode})")
        return engine.exec(payload)
    }

    override suspend fun acquireRoot(): ExecResult {
        val engine = resolve()
        Log.i(TAG, "acquireRoot via $lastEngineName (${lastReasonCode})")
        engine.progress(GhostlockCodes.ENGINE_SELECTED, lastEngineName)
        val result = engine.acquireRoot()
        if (result.success || engine.rootAcquired) return result
        // 選中的引擎失敗時，若另一條管線其實可用，再試一次（避免探測時序造成的誤判）
        val other = if (engine === shizuku) bridge else shizuku
        val otherReady = if (other === bridge) isBridgeHelperResident() else isShizukuAvailable()
        if (!otherReady) return result
        Log.i(TAG, "$lastEngineName failed -> retry with ${if (other === bridge) "bridge" else "shizuku"}")
        lastEngineName = if (other === bridge) "bridge" else "shizuku"
        lastReasonCode = GhostlockCodes.ENGINE_RETRY_OTHER
        other.progress(GhostlockCodes.ENGINE_SELECTED, lastEngineName)
        val retry = other.acquireRoot()
        return if (retry.success || other.rootAcquired) retry else result
    }

    /** 依目前環境挑引擎（並記錄原因代碼）。 */
    private fun resolve(): GhostlockEngine {
        val shizukuUp = isShizukuAvailable()
        val helperUp = isBridgeHelperResident()
        return when {
            shizukuUp -> {
                lastEngineName = "shizuku"
                lastReasonCode = GhostlockCodes.ENGINE_SHIZUKU_RUNNING
                shizuku
            }
            helperUp -> {
                lastEngineName = "bridge"
                lastReasonCode = GhostlockCodes.ENGINE_BRIDGE_RESIDENT
                bridge
            }
            else -> {
                lastEngineName = "shizuku"
                lastReasonCode = GhostlockCodes.ENGINE_NONE_AVAILABLE
                shizuku
            }
        }
    }

    /**
     * Shizuku 是否可用：用官方 API（`Shizuku.pingBinder()`）。
     * ⚠ 不可用 `ps` 判斷——Android 的 App 看不到其他 process。
     */
    private fun isShizukuAvailable(): Boolean =
        runCatching { rikka.shizuku.Shizuku.pingBinder() }.getOrDefault(false)

    /** bridge helper 是否常駐：讀 helper 心跳檔（1 秒一輪）。 */
    private fun isBridgeHelperResident(): Boolean = runCatching {
        val f = File(com.rokid.rc.glasses.GlassesPaths.BRIDGE_HELPER_ALIVE)
        if (!f.exists()) return@runCatching false
        val ts = f.readText().trim().toLongOrNull() ?: return@runCatching false
        (System.currentTimeMillis() / 1000) - ts in 0..120L
    }.getOrDefault(false)

    private fun current(): GhostlockEngine = when (lastEngineName) {
        "bridge" -> bridge
        else -> shizuku
    }

    companion object {
        private const val TAG = "AutoGhostlockEngine"
    }
}
