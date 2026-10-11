package com.rokid.rc.shared.protocol

import org.json.JSONObject

enum class MediaType { PHOTO, VIDEO }

/** Root acquisition mode: AUTO=run the LPE automatically on start; MANUAL=run only on a phone command. */
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
    // Path B: ring/touchpad control state on the glasses side
    val ringEnabled: Boolean = true,
    val ringRunning: Boolean = false,
    val ringDevice: String? = null,
    /**
     * The **structured result** of the ring's most recent action (not pre-translated text).
     *
     * The glasses side sends only a code and arguments (e.g. `ringLastKey="photo_ok"`,
     * `ringLastArg="IMG_123.jpg"`), and the phone side renders the text in its own locale -> the two
     * sides can differ in language (previously the glasses sent Chinese, so an English phone mixed locales).
     * The code-to-phone-string mapping lives in `app-phone`'s `ui/RingText.kt`.
     */
    val ringLastKey: String? = null,
    val ringLastArg: String? = null,
    // ---- P0/P2: root state and escalation metrics (structured; the phone displays them in its own locale) ----
    /** See `RootStates`: root_ok / root_rearming / root_rearmed / root_lost_need_arm / root_lost_cooldown. */
    val rootState: String? = null,
    /** Extra information: the script suggested for execution (several separated by `|`) or the engine name. */
    val rootHint: String? = null,
    /** The engine used by the most recent escalation (shizuku / bridge / direct / none). */
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

/** Metadata shared by the file-transfer start announcement (MEDIA_META) and end (TRANSFER_END). */
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
