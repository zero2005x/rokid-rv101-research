package com.rokid.rc.shared.protocol

/**
 * Protocol constants. Mirrors docs/protocol.md.
 */
object Protocol {
    const val PROTO_VERSION = 1
    // Transport is Bluetooth RFCOMM (no TCP port); DEFAULT_PORT has been removed.
    const val APP_NAME_GLASSES = "RokidGlassesCapture"
    const val APP_NAME_PHONE = "RokidPhoneRemote"

    // WiFi media channel (glasses SoftAP + TCP server; control commands stay on Bluetooth,
    // media can use WiFi and fall back to Bluetooth)
    const val WIFI_PORT = 8555
    const val AP_SSID = "RokidCam"
    const val AP_PASS = "rokid1234"
    const val AP_IP = "192.168.43.1"

    /**
     * Bluetooth SPP (RFCOMM) service UUID.
     *
     * **The phone and the glasses must agree**: previously each side hard-coded its own string, so a change on
     * either side silently broke the connection; hence it is centralised in the protocol constants
     * (`java.util.UUID.fromString(Protocol.SPP_UUID)`).
     */
    const val SPP_UUID = "00001101-0000-1000-8000-00805F9B34FB"
}

/**
 * Single source of truth for media types (MIME and file extensions).
 *
 * Previously literals such as `"image/jpeg"` / `".mp4"` / `".part"` were scattered across 7 places in the two
 * apps, and the classification logic (is it a video, is it a partial download) was duplicated as a result.
 */
object MediaTypes {
    const val MIME_JPEG = "image/jpeg"
    const val MIME_MP4 = "video/mp4"
    const val MIME_PNG = "image/png"

    const val EXT_JPG = ".jpg"
    const val EXT_PNG = ".png"
    const val EXT_MP4 = ".mp4"
    const val EXT_MOV = ".mov"
    /** Extension of an in-progress download (renamed only once complete). */
    const val EXT_PART = ".part"

    /** Whether this is a video (by file extension). */
    fun isVideo(fileName: String): Boolean =
        fileName.endsWith(EXT_MP4, ignoreCase = true) || fileName.endsWith(EXT_MOV, ignoreCase = true)

    /** Whether this is a partial download. */
    fun isPartial(fileName: String): Boolean = fileName.endsWith(EXT_PART, ignoreCase = true)

    /** Infer the MIME type from the file extension. */
    fun mimeOf(fileName: String): String = when {
        fileName.endsWith(EXT_MP4, ignoreCase = true) -> MIME_MP4
        fileName.endsWith(EXT_PNG, ignoreCase = true) -> MIME_PNG
        else -> MIME_JPEG
    }
}

/** Message type constants. */
object MsgType {
    // phone -> glasses
    const val HELLO = "HELLO"
    const val GET_STATUS = "GET_STATUS"
    const val SET_GHOSTLOCK = "SET_GHOSTLOCK"
    const val CAPTURE_PHOTO = "CAPTURE_PHOTO"
    const val START_VIDEO = "START_VIDEO"
    const val STOP_VIDEO = "STOP_VIDEO"
    const val LIST_ALBUMS = "LIST_ALBUMS"
    const val CREATE_ALBUM = "CREATE_ALBUM"
    const val RENAME_ALBUM = "RENAME_ALBUM"
    const val DELETE_ALBUM = "DELETE_ALBUM"
    const val LIST_MEDIA = "LIST_MEDIA"
    const val GET_THUMBNAIL = "GET_THUMBNAIL"
    const val DOWNLOAD_MEDIA = "DOWNLOAD_MEDIA"
    const val DELETE_MEDIA = "DELETE_MEDIA"
    const val SET_ROOT_MODE = "SET_ROOT_MODE"
    const val ACQUIRE_ROOT = "ACQUIRE_ROOT"
    const val SET_LED = "SET_LED"
    const val GET_DEVICE_INFO = "GET_DEVICE_INFO"
    const val ENSURE_WIFI_AP = "ENSURE_WIFI_AP"
    const val SET_DISPLAY_CONFIG = "SET_DISPLAY_CONFIG"
    const val ENABLE_WIFI_ADB = "ENABLE_WIFI_ADB"

    /** Dev mode: enable wireless adb on demand (join the subnet + open a listener + auto-close on timeout). */
    const val DEV_MODE_START = "DEV_MODE_START"
    const val DEV_MODE_STOP = "DEV_MODE_STOP"
    const val DEV_MODE_STATUS = "DEV_MODE_STATUS"
    const val SET_SILENT = "SET_SILENT"
    const val SET_RING = "SET_RING"

    // glasses -> phone
    const val HELLO_ACK = "HELLO_ACK"
    const val ACK = "ACK"
    const val STATUS = "STATUS"
    const val PROGRESS = "PROGRESS"
    const val DEVICE_INFO = "DEVICE_INFO"
    const val PHOTO_TAKEN = "PHOTO_TAKEN"
    const val VIDEO_STARTED = "VIDEO_STARTED"
    const val VIDEO_STOPPED = "VIDEO_STOPPED"
    const val ALBUM_LIST = "ALBUM_LIST"
    const val MEDIA_LIST = "MEDIA_LIST"
    const val MEDIA_META = "MEDIA_META"
    const val TRANSFER_END = "TRANSFER_END"
    const val WIFI_AP_STATUS = "WIFI_AP_STATUS"
    const val ERROR = "ERROR"
}
