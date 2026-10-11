package com.rokid.rc.shared.protocol

import org.json.JSONObject

enum class MediaType { PHOTO, VIDEO }

/** root 取得模式：AUTO=開啟即自動跑 LPE；MANUAL=等手機下指令才跑。 */
enum class RootMode { AUTO, MANUAL }

data class Album(
    val id: String,
    val name: String,
    val createdAt: Long,
    val mediaCount: Int
) {
    fun toJson(): JSONObject = JSONObject()
        .put("id", id)
        .put("name", name)
        .put("createdAt", createdAt)
        .put("mediaCount", mediaCount)

    companion object {
        fun fromJson(o: JSONObject): Album = Album(
            id = o.getString("id"),
            name = o.getString("name"),
            createdAt = o.optLong("createdAt", 0L),
            mediaCount = o.optInt("mediaCount", 0)
        )
    }
}

data class MediaItem(
    val id: String,
    val albumId: String,
    val type: MediaType,
    val fileName: String,
    val mime: String,
    val sizeBytes: Long,
    val width: Int,
    val height: Int,
    val durationMs: Long,
    val createdAt: Long,
    val thumbReady: Boolean
) {
    fun toJson(): JSONObject = JSONObject()
        .put("id", id)
        .put("albumId", albumId)
        .put("type", type.name)
        .put("fileName", fileName)
        .put("mime", mime)
        .put("sizeBytes", sizeBytes)
        .put("width", width)
        .put("height", height)
        .put("durationMs", durationMs)
        .put("createdAt", createdAt)
        .put("thumbReady", thumbReady)

    companion object {
        fun fromJson(o: JSONObject): MediaItem = MediaItem(
            id = o.getString("id"),
            albumId = o.getString("albumId"),
            type = if (o.optString("type") == MediaType.VIDEO.name) MediaType.VIDEO else MediaType.PHOTO,
            fileName = o.getString("fileName"),
            mime = o.optString("mime", ""),
            sizeBytes = o.optLong("sizeBytes", 0L),
            width = o.optInt("width", 0),
            height = o.optInt("height", 0),
            durationMs = o.optLong("durationMs", 0L),
            createdAt = o.optLong("createdAt", 0L),
            thumbReady = o.optBoolean("thumbReady", false)
        )
    }
}

data class Status(
    val ghostlockEnabled: Boolean,
    val cameraBusy: Boolean,
    val recording: Boolean,
    val albumCount: Int,
    val mediaCount: Int,
    val storageFreeBytes: Long,
    val rootMode: String,
    val rootAcquired: Boolean,
    val ledOn: Boolean,
    val silentEnabled: Boolean = false,
    val silentApplied: Boolean = false,
    // 路徑 B：眼鏡端戒指／觸控板控制狀態
    val ringEnabled: Boolean = true,
    val ringRunning: Boolean = false,
    val ringDevice: String? = null,
    /**
     * 戒指最近一次動作的**結構化結果**（不是已翻譯文字）。
     *
     * 眼鏡端只送代碼與參數（例如 `ringLastKey="photo_ok"`, `ringLastArg="IMG_123.jpg"`），
     * 由手機端用自己的語系組字 → 兩端語言可各自獨立（原本眼鏡送中文，手機設英文時會混語系）。
     * 代碼與手機字串的對應見 `app-phone` 的 `ui/RingText.kt`。
     */
    val ringLastKey: String? = null,
    val ringLastArg: String? = null,
    // ---- P0/P2：root 狀態與提權度量（結構化；手機端以自己語系顯示）----
    /** 見 `RootStates`：root_ok／root_rearming／root_rearmed／root_lost_need_arm／root_lost_cooldown。 */
    val rootState: String? = null,
    /** 附加資訊：建議執行的腳本（多個以 `|` 分隔）或引擎名稱。 */
    val rootHint: String? = null,
    /** 最近一次提權使用的引擎（shizuku／bridge／direct／none）。 */
    val suEngine: String? = null,
    val suLastOk: Boolean = false,
    val suLastMs: Long = 0L,
    val suAttempts: Int = 0,
    val suSuccesses: Int = 0,
    val suFirstTryOk: Int = 0,
    val suKeeperChecks: Int = 0,
    val suKeeperRearms: Int = 0
) {
    fun toJson(): JSONObject = JSONObject()
        .put("ghostlockEnabled", ghostlockEnabled)
        .put("cameraBusy", cameraBusy)
        .put("recording", recording)
        .put("albumCount", albumCount)
        .put("mediaCount", mediaCount)
        .put("storageFreeBytes", storageFreeBytes)
        .put("rootMode", rootMode)
        .put("rootAcquired", rootAcquired)
        .put("ledOn", ledOn)
        .put("silentEnabled", silentEnabled)
        .put("silentApplied", silentApplied)
        .put("ringEnabled", ringEnabled)
        .put("ringRunning", ringRunning)
        .put("ringDevice", ringDevice ?: JSONObject.NULL)
        .put("ringLastKey", ringLastKey ?: JSONObject.NULL)
        .put("ringLastArg", ringLastArg ?: JSONObject.NULL)
        .put("rootState", rootState ?: JSONObject.NULL)
        .put("rootHint", rootHint ?: JSONObject.NULL)
        .put("suEngine", suEngine ?: JSONObject.NULL)
        .put("suLastOk", suLastOk)
        .put("suLastMs", suLastMs)
        .put("suAttempts", suAttempts)
        .put("suSuccesses", suSuccesses)
        .put("suFirstTryOk", suFirstTryOk)
        .put("suKeeperChecks", suKeeperChecks)
        .put("suKeeperRearms", suKeeperRearms)

    companion object {
        fun fromJson(o: JSONObject): Status = Status(
            ghostlockEnabled = o.optBoolean("ghostlockEnabled"),
            cameraBusy = o.optBoolean("cameraBusy"),
            recording = o.optBoolean("recording"),
            albumCount = o.optInt("albumCount"),
            mediaCount = o.optInt("mediaCount"),
            storageFreeBytes = o.optLong("storageFreeBytes", 0L),
            rootMode = o.optString("rootMode", RootMode.MANUAL.name),
            rootAcquired = o.optBoolean("rootAcquired"),
            ledOn = o.optBoolean("ledOn"),
            silentEnabled = o.optBoolean("silentEnabled"),
            silentApplied = o.optBoolean("silentApplied"),
            ringEnabled = o.optBoolean("ringEnabled", true),
            ringRunning = o.optBoolean("ringRunning", false),
            ringDevice = o.optString("ringDevice").takeIf { it.isNotEmpty() && it != "null" },
            ringLastKey = o.optString("ringLastKey").takeIf { it.isNotEmpty() && it != "null" },
            ringLastArg = o.optString("ringLastArg").takeIf { it.isNotEmpty() && it != "null" },
            rootState = o.optString("rootState").takeIf { it.isNotEmpty() && it != "null" },
            rootHint = o.optString("rootHint").takeIf { it.isNotEmpty() && it != "null" },
            suEngine = o.optString("suEngine").takeIf { it.isNotEmpty() && it != "null" },
            suLastOk = o.optBoolean("suLastOk", false),
            suLastMs = o.optLong("suLastMs", 0L),
            suAttempts = o.optInt("suAttempts", 0),
            suSuccesses = o.optInt("suSuccesses", 0),
            suFirstTryOk = o.optInt("suFirstTryOk", 0),
            suKeeperChecks = o.optInt("suKeeperChecks", 0),
            suKeeperRearms = o.optInt("suKeeperRearms", 0)
        )
    }
}

/** 檔案傳輸開始宣告（MEDIA_META）與結束（TRANSFER_END）共用的後設資料。 */
data class TransferMeta(
    val mediaId: String,
    val transferId: String,
    val fileName: String,
    val mime: String,
    val size: Long
) {
    fun toJson(): JSONObject = JSONObject()
        .put("mediaId", mediaId)
        .put("transferId", transferId)
        .put("fileName", fileName)
        .put("mime", mime)
        .put("size", size)

    companion object {
        fun fromJson(o: JSONObject): TransferMeta = TransferMeta(
            mediaId = o.getString("mediaId"),
            transferId = o.optString("transferId", ""),
            fileName = o.optString("fileName", ""),
            mime = o.optString("mime", ""),
            size = o.optLong("size", 0L)
        )
    }
}
