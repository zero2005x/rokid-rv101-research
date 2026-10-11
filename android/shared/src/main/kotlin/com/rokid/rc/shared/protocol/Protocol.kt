package com.rokid.rc.shared.protocol

/**
 * 通訊協定常數。與 docs/protocol.md 對應。
 */
object Protocol {
    const val PROTO_VERSION = 1
    // 傳輸為藍牙 RFCOMM（無 TCP port）；DEFAULT_PORT 已移除。
    const val APP_NAME_GLASSES = "RokidGlassesCapture"
    const val APP_NAME_PHONE = "RokidPhoneRemote"

    // WiFi 媒體通道（眼鏡 SoftAP + TCP server；控制指令維持藍牙，媒體可走 WiFi 並回退藍牙）
    const val WIFI_PORT = 8555
    const val AP_SSID = "RokidCam"
    const val AP_PASS = "rokid1234"
    const val AP_IP = "192.168.43.1"

    /**
     * 藍牙 SPP（RFCOMM）服務 UUID。
     *
     * **手機與眼鏡必須一致**：先前兩端各自寫死一份字串，只要有一邊改動就會靜默連不上，
     * 因此集中在協定常數（`java.util.UUID.fromString(Protocol.SPP_UUID)`）。
     */
    const val SPP_UUID = "00001101-0000-1000-8000-00805F9B34FB"
}

/**
 * 媒體型別（MIME 與副檔名）唯一來源。
 *
 * 先前 `"image/jpeg"`／`".mp4"`／`".part"` 等字面值散落在兩個 App 共 7 處，
 * 判斷邏輯（是否為影片、是否為下載暫存檔）也因此重複實作。
 */
object MediaTypes {
    const val MIME_JPEG = "image/jpeg"
    const val MIME_MP4 = "video/mp4"
    const val MIME_PNG = "image/png"

    const val EXT_JPG = ".jpg"
    const val EXT_PNG = ".png"
    const val EXT_MP4 = ".mp4"
    const val EXT_MOV = ".mov"
    /** 下載中的暫存副檔名（完成後才改名）。 */
    const val EXT_PART = ".part"

    /** 是否為影片（依副檔名）。 */
    fun isVideo(fileName: String): Boolean =
        fileName.endsWith(EXT_MP4, ignoreCase = true) || fileName.endsWith(EXT_MOV, ignoreCase = true)

    /** 是否為下載暫存檔。 */
    fun isPartial(fileName: String): Boolean = fileName.endsWith(EXT_PART, ignoreCase = true)

    /** 依副檔名推 MIME。 */
    fun mimeOf(fileName: String): String = when {
        fileName.endsWith(EXT_MP4, ignoreCase = true) -> MIME_MP4
        fileName.endsWith(EXT_PNG, ignoreCase = true) -> MIME_PNG
        else -> MIME_JPEG
    }
}

/** 訊息型別常數。 */
object MsgType {
    // 手機 → 眼鏡
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

    /** 開發模式：按需開啟無線 adb（加入網段＋開 listener＋逾時自動關）。 */
    const val DEV_MODE_START = "DEV_MODE_START"
    const val DEV_MODE_STOP = "DEV_MODE_STOP"
    const val DEV_MODE_STATUS = "DEV_MODE_STATUS"
    const val SET_SILENT = "SET_SILENT"
    const val SET_RING = "SET_RING"

    // 眼鏡 → 手機
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
