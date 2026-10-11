package com.rokid.rc.glasses.ghost

import android.util.Log
import com.rokid.rc.glasses.AppGraph
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import java.io.File

/**
 * root daemon 存活看守（**P0**，App 側）。
 *
 * 與 payload 安裝的 **root 側 keep-alive 迴圈**互補：
 * | 情境 | 由誰處理 |
 * |------|----------|
 * | daemon 崩潰／被 lmkd 回收，但 **root 還在** | root 側 keep-alive（20 秒一輪，直接重建，不需 adb、不需重跑 exploit） |
 * | root 整體失效（重開機、keeper 也被清掉） | 本看守：清 stale socket → 檢查是否**還有人可以幫忙提權** → 自動重建或回報「要跑哪支腳本」 |
 *
 * 設計取捨：
 *  - **不盲目重跑 exploit**：只有在 (a) bridge helper 常駐 或 (b) Shizuku 可用 時才自動重建；
 *    兩者都沒有時只回報狀態（避免在沒有觸發管線的情況下白跑、甚至 panic 重開機）。
 *  - **速率限制**：兩次自動重建至少間隔 [REARM_COOLDOWN_MS]，避免短時間內反覆觸發。
 */
class RootDaemonKeeper(private val graph: AppGraph) {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var job: Job? = null

    /** 目前狀態代碼（結構化；手機端以自己語系顯示，見 shared `RootStates`）。 */
    @Volatile
    var state: String = com.rokid.rc.shared.protocol.RootStates.UNKNOWN
        private set

    /** 附加資訊（例如需要執行的腳本名稱、引擎名稱）。 */
    @Volatile
    var detail: String? = null
        private set

    @Volatile
    private var rearming = false
    private var lastRearmAtMs = 0L

    /**
     * 看守次數／自動重建次數（P2 度量用）。
     * **持久化於 prefs**：Rokid 常殺 App，只放記憶體會每次歸零。
     */
    private val prefs = graph.prefs

    @Volatile
    var checks: Int = graph.prefs.getInt("keeper_checks", 0)
        private set

    @Volatile
    var rearmAttempts: Int = graph.prefs.getInt("keeper_rearms_try", 0)
        private set

    @Volatile
    var rearmSuccesses: Int = graph.prefs.getInt("keeper_rearms_ok", 0)
        private set

    private fun persistCounters() {
        prefs.edit()
            .putInt("keeper_checks", checks)
            .putInt("keeper_rearms_try", rearmAttempts)
            .putInt("keeper_rearms_ok", rearmSuccesses)
            .apply()
    }

    fun start() {
        if (job?.isActive == true) return
        job = scope.launch {
            // 啟動後先等一輪，避免與 CaptureService 的 AUTO 提權撞在一起
            delay(15_000)
            while (isActive) {
                runCatching { check() }.onFailure { Log.w(TAG, "check failed: ${it.message}") }
                delay(INTERVAL_MS)
            }
        }
        Log.i(TAG, "keeper started (interval=${INTERVAL_MS}ms)")
    }

    fun stop() {
        job?.cancel()
        job = null
    }

    /**
     * 外部操作者持有租約時，回傳其 owner 名稱；否則 null。
     *
     * exploit 本體（ghostlock_rv101、.ghostlock_root.sh、gl_run.log）無法像
     * helper 與信箱那樣一分為二，所以三方用一個固定路徑的租約檔序列化。
     * 外部操作者在工作期間會持續續約（桌面工具 30 秒輪詢、deploy `lease.sh` 背景續約），
     * 因此這裡看到的 ts 是新鮮的就代表它確實在跑。
     *
     * 判斷維持 **deny-by-default** ——只有 `owner=app`（自己）不算外部；
     * 任何未知 owner 都讓開。**ts 新鮮但 owner 無法解析時保守視為外部**，
     * 與原本 `mine = txt.contains("owner=app")` 的行為一致。
     *
     * 回傳名稱（而非只是 Boolean）是為了讓 [detail] 顯示**實際**持有者：
     * 原先把字串寫死成 `"desktop"`，但 2026-09-13 起 deploy 腳本（`deploy/lease.sh`）
     * 也會持有租約，寫死會讓手機與日誌指出錯誤的對象。
     */
    private fun externalLeaseOwner(): String? {
        return try {
            val f = File(LEASE_PATH)
            if (!f.exists()) return null
            val txt = f.readText()
            val ts = Regex("ts=(\\d+)").find(txt)?.groupValues?.get(1)?.toLongOrNull()
                ?: return null
            val ageSec = System.currentTimeMillis() / 1000 - ts
            if (ageSec !in 0..LEASE_TTL_SEC) return null          // 空閒／過期
            val owner = Regex("owner=([A-Za-z][A-Za-z]*)").find(txt)?.groupValues?.get(1)
            if (owner == "app") return null                       // 自己持有
            owner ?: "unknown"                                    // 無法解析 → 保守視為外部
        } catch (e: Exception) {
            null
        }
    }

    /**
     * 逾時後清掉洩漏的 exploit 程序。
     *
     * exploit 失敗時會洩漏大量 ghostlock_rv101（實測 492 → 516，系統負載 347，
     * adb 指令開始逾時）。**必須用 ps 取 PID 再逐一 kill**：pkill -f 的 pattern
     * 會匹配到執行它的 shell 本身，造成自殺（退出碼 137）。
     *
     * 限制：App 只能殺自己的行程，這些是 shell 或 root 擁有，所以只有在
     * root 還可用時才清得掉；清不掉時交給桌面工具（adb shell 有權限）。
     */
    private suspend fun cleanupStrayExploits() {
        runCatching {
            val client = RootDaemonClient()
            if (!client.isRoot()) return@runCatching
            client.exec(
                "for p in \$(ps -A -o PID,ARGS 2>/dev/null | grep '[g]hostlock_rv10[1]' " +
                "| awk '{print \$1}'); do kill -9 \$p 2>/dev/null; done"
            )
            Log.i(TAG, "cleaned up stray exploit processes")
        }
    }

    /**
     * 取得租約（覆寫整個檔案）。
     *
     * 租約檔位於 `ghostlock_bridge/`（**777**），因此本 App 可以自行建立它 ——
     * 這是把租約從 `/data/local/tmp` 搬過來的原因（見 [LEASE_PATH] 的說明）。
     * 搬遷前，App 對 `/data/local/tmp` 沒有目錄寫入權，檔案不存在時本函式**必然失敗**。
     *
     * 失敗時仍然**必須留下痕跡**：原本 `runCatching {}` 把失敗吞掉，於是 App 會在
     * 「沒持有租約」的情況下照樣跑 exploit —— 正是租約要防止的互搶。
     * 搬遷後失敗只剩磁碟／權限異常等罕見情況，但既然要防的是互搶，
     * 寧可留一筆 W 級日誌也不要靜默。
     */
    private fun takeLease(): Boolean {
        val pid = android.os.Process.myPid()
        val ts = System.currentTimeMillis() / 1000
        return runCatching {
            File(LEASE_PATH).writeText("owner=app pid=$pid ts=$ts")
            true
        }.getOrElse {
            Log.w(TAG, "takeLease FAILED (${it.message}) — 租約檔需由 shell 端以 666 預先建立；" +
                "本次將在**未持租約**的情況下繼續")
            false
        }
    }

    /**
     * 釋放租約：**寫入「已過期」標記，而不是刪除檔案**。
     *
     * 歷史：原本用 `delete()`，但當時租約檔在 `/data/local/tmp`，App 沒有該目錄的
     * 寫入權 → `delete()` 永遠失敗（實測 `rm: Permission denied`），
     * 租約便以 `owner=app` + 新鮮 `ts` 殘留，把桌面工具與 deploy **擋住最多 900 秒**。
     *
     * 搬遷到 777 的 `ghostlock_bridge/` 之後刪除已經可行，但**仍維持寫標記**，因為：
     *  1. **一致性**：三方（app／deploy／desktop）用同一套約定，不必各自判斷刪除權。
     *  2. **沒有空窗**：刪除與重建之間，讀者會看到「檔案不存在」＝無租約＝可搶，
     *     那正是 TOCTOU 的溫床；寫標記是一次覆寫，沒有這種空窗。
     *
     * `ts=0` 在三方實作中都不通過有效性檢查（desktop 要求 `>1000000000`、
     * app/deploy 要求 age 在 `0..TTL`），因此等價於「已釋放」；
     * 同時保留檔案（666）讓後續 `takeLease()` 還能覆寫。
     */
    private fun dropLease() {
        runCatching { File(LEASE_PATH).writeText("owner=none pid=0 ts=0") }
            .onFailure { Log.w(TAG, "dropLease FAILED (${it.message})") }
    }

    /** 單次健康檢查 + 必要時自動重建。 */
    private suspend fun check() {
        checks++
        persistCounters()
        val client = RootDaemonClient()
        if (client.isRoot()) {
            if (state != com.rokid.rc.shared.protocol.RootStates.OK) {
                Log.i(TAG, "root ok (was $state)")
            }
            state = com.rokid.rc.shared.protocol.RootStates.OK
            detail = null
            return
        }

        /*
         * 先看有沒有人在用同一套 exploit。順序很重要：這個檢查必須在
         * 「刪 stale socket」之前 —— 否則桌面工具剛把 daemon 拉起來、
         * socket 還沒出現在 /proc/net/unix 的空窗期，就會被我們刪掉。
         */
        val externalOwner = externalLeaseOwner()
        if (externalOwner != null) {
            state = com.rokid.rc.shared.protocol.RootStates.LOST_COOLDOWN
            // 顯示**實際**持有者（desktop／deploy／…），不要寫死
            detail = "external-lease($externalOwner)"
            Log.i(TAG, "external operator($externalOwner) holds lease; skip re-arm and leave socket alone")
            return
        }

        /*
         * root 不可用：嘗試清掉 stale socket。
         *
         * ⚠ App 對 `/data/local/tmp` **沒有目錄寫入權**（見 takeLease 的說明），
         * 所以 `delete()` 實際上**永遠失敗**。原本的程式碼不看回傳值就印
         * 「removed stale socket」，日誌與事實不符（實測：日誌說刪了、檔案還在）
         * —— 這種假日誌會讓後續除錯往錯的方向走，所以改為照實記錄。
         */
        val stale = File(com.rokid.rc.glasses.GlassesPaths.ROOT_DAEMON_SOCK)
        if (stale.exists()) {
            if (stale.delete()) {
                Log.i(TAG, "removed stale socket ${stale.absolutePath}")
            } else {
                Log.i(TAG, "stale socket present but not removable by app uid " +
                    "(${stale.absolutePath}); leaving it (harmless: no listener)")
            }
        }

        // 有沒有人可以幫忙提權？
        val helperResident = isBridgeHelperResident()
        val shizukuUp = isShizukuAvailable()
        if (helperResident) {
            detail = SCRIPT_BRIDGE
        } else if (shizukuUp) {
            detail = SCRIPT_SHIZUKU
        } else {
            state = com.rokid.rc.shared.protocol.RootStates.LOST_NEED_ARM
            detail = "$SCRIPT_BRIDGE|$SCRIPT_SHIZUKU"
            Log.w(TAG, "root lost and no trigger available (helper=$helperResident shizuku=$shizukuUp)")
            return
        }

        /*
         * 安全武裝政策（2026-09-13）：開機迴圈斷路器／每次開機重試上限＋指數退避／
         * 僅充電時自動武裝／開機後延遲。EXPLOIT 每次約 50% 機率 kernel panic → 2–5 分鐘後重開機，
         * 無限制重試會變成開機迴圈（reports/glasses_reboot_after_adb_unplug_2026-09-13.md）。
         * 這裡只約束**自動**武裝；使用者手動提權不受影響。
         */
        val appCtx = graph.app
        ArmPolicy.blockReason(appCtx, prefs)?.let { why ->
            state = com.rokid.rc.shared.protocol.RootStates.LOST_COOLDOWN
            detail = "safe-arm($why)"
            Log.i(TAG, "自動武裝暫停：$why（${ArmPolicy.describe(appCtx, prefs)}）")
            return
        }

        // 速率限制
        val now = android.os.SystemClock.elapsedRealtime()
        if (rearming || now - lastRearmAtMs < REARM_COOLDOWN_MS) {
            state = com.rokid.rc.shared.protocol.RootStates.LOST_COOLDOWN
            return
        }
        // 通過所有檢查 → 記錄這次自動武裝（供斷路器與退避使用）
        ArmPolicy.onAutoArmAttempt(prefs)

        rearming = true
        rearmAttempts++
        persistCounters()
        lastRearmAtMs = now
        state = com.rokid.rc.shared.protocol.RootStates.REARMING
        Log.i(TAG, "auto re-arm start (helper=$helperResident shizuku=$shizukuUp detail=$detail)")
        /*
         * 租約預檢：從上面讀到這裡之間可能已被外部操作者搶走（TOCTOU），
         * 所以在真正動手前再確認一次，避免兩邊同時改 .ghostlock_root.sh。
         */
        val preOwner = externalLeaseOwner()
        if (preOwner != null) {
            state = com.rokid.rc.shared.protocol.RootStates.LOST_COOLDOWN
            detail = "external-lease($preOwner,precheck)"
            rearming = false
            Log.i(TAG, "lease taken by external operator($preOwner) just before re-arm; abort")
            return
        }

        // 取租約。失敗（例如租約檔不存在且 App 無目錄寫入權）仍會繼續，
        // 但 takeLease() 已留下 W 級日誌 —— 不再靜默地「沒持租約就跑 exploit」。
        if (!takeLease()) {
            Log.w(TAG, "proceeding WITHOUT lease: external operators cannot see us; " +
                "run deploy/lease.sh (ensure_file) to pre-create a 666 lease file")
        }
        try {
            val started = now
            /*
             * 逾時是必要的：先前沒有逾時，keeper 卡在 acquireRoot() 裡長達 17 分鐘，
             * 期間 (a) 不會再檢查租約 (b) 狀態永遠停在 REARMING
             * (c) exploit 持續洩漏程序把裝置拖垮。
             */
            val result = withTimeoutOrNull(REARM_TIMEOUT_MS) { graph.ghostlock.acquireRoot() }
            val tookMs = android.os.SystemClock.elapsedRealtime() - started
            if (result == null) {
                Log.w(TAG, "auto re-arm timeout after ${tookMs}ms; cleaning up strays")
                cleanupStrayExploits()
                state = com.rokid.rc.shared.protocol.RootStates.LOST_NEED_ARM
                detail = "rearm-timeout"
            } else if (result.success || graph.ghostlock.rootAcquired) {
                rearmSuccesses++
                persistCounters()
                state = com.rokid.rc.shared.protocol.RootStates.REARMED
                Log.i(TAG, "auto re-arm ok in ${tookMs}ms")
                // 重建成功後把相依功能補回來
                runCatching { graph.silentMode.applyIfEnabled() }
                runCatching { graph.ringControl.ensureRunning() }
            } else {
                state = com.rokid.rc.shared.protocol.RootStates.LOST_NEED_ARM
                Log.w(TAG, "auto re-arm failed: stage=${result.stage} out=${result.output.take(120)} (${tookMs}ms)")
            }
        } finally {
            dropLease()
            rearming = false
        }
    }

    /**
     * bridge helper 是否常駐。
     *
     * ⚠ 不能用 `ps`：Android 的 App 看不到其他 process（實測 helper 明明在跑卻回 false，
     * 導致看守誤判「沒人可觸發」而不自動重建）。改讀 helper 每秒更新的心跳檔。
     */
    private fun isBridgeHelperResident(): Boolean = runCatching {
        val f = File(com.rokid.rc.glasses.GlassesPaths.BRIDGE_HELPER_ALIVE)
        if (!f.exists()) return@runCatching false
        val ts = f.readText().trim().toLongOrNull() ?: return@runCatching false
        val now = System.currentTimeMillis() / 1000
        // 心跳容忍 2 分鐘（helper 迴圈為 1 秒一輪；bridge 忙碌時也不會超過這個量級）
        now - ts in 0..HEARTBEAT_TOLERANCE_S
    }.getOrDefault(false)

    /** Shizuku server 是否可用（用官方 API，而非 `ps`）。 */
    private fun isShizukuAvailable(): Boolean =
        runCatching { rikka.shizuku.Shizuku.pingBinder() }.getOrDefault(false)

    companion object {
        private const val TAG = "RootDaemonKeeper"
        private const val INTERVAL_MS = 30_000L
        /** 兩次自動重建的最短間隔（避免反覆觸發 exploit）。 */
        private const val REARM_COOLDOWN_MS = 5 * 60_000L
        /** helper 心跳容忍秒數。 */
        private const val HEARTBEAT_TOLERANCE_S = 120L

        /**
         * 與 ZeroLightRokidDesktop／`deploy/lease.sh` 共用的租約檔。
         * **三處路徑必須完全一致**（`deploy/test_lease.sh` 有一項跨檔一致性檢查）。
         *
         * 為什麼在 `ghostlock_bridge/` 子目錄而不是 `/data/local/tmp` 本身：
         * `/data/local/tmp` 是 `drwxrwx--x shell shell`，本 App（uid 10053）屬 "other"，
         * 只有 `--x` **沒有目錄寫入權** —— 實測**既不能建立也不能刪除**其中的檔案，
         * 導致 `takeLease()` 在檔案不存在時必然失敗（App 沒持租約就跑 exploit）。
         * `ghostlock_bridge/` 是 **777**，實測本 App 在其中 `CREATE_OK` / `RM_OK`。
         */
        private const val LEASE_PATH = "/data/local/tmp/ghostlock_bridge/ghostlock_op.lease"
        private const val LEASE_TTL_SEC = 900L

        /** 單次自動重建的上限。沒有這個，keeper 會卡死（實測 17 分鐘）。 */
        private const val REARM_TIMEOUT_MS = 5 * 60 * 1000L
        const val SCRIPT_BRIDGE = "deploy/root_bridge.sh"
        const val SCRIPT_SHIZUKU = "deploy/start_shizuku.sh"
    }
}
