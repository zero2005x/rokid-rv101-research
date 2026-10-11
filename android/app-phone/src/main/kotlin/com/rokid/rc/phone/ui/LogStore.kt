package com.rokid.rc.phone.ui

/**
 * 共用操作紀錄緩衝。
 *
 * 重設計後「操作紀錄」只顯示在遙控分頁，但設定分頁的動作（切換 WiFi／靜音／root／戒指…）
 * 也要寫進同一份紀錄，因此把緩衝抽出來共用（單一來源，避免兩份日誌不同步）。
 */
object LogStore {

    private const val MAX_CHARS = 20_000
    private val sb = StringBuilder()

    /** 內容變更回呼（由遙控分頁註冊；分頁不在前景時為 null）。 */
    @Volatile
    var onChanged: ((String) -> Unit)? = null

    val text: String
        get() = synchronized(this) { sb.toString() }

    fun append(msg: String) {
        val t = synchronized(this) {
            sb.append(msg).append('\n')
            if (sb.length > MAX_CHARS) sb.delete(0, sb.length - MAX_CHARS)
            sb.toString()
        }
        onChanged?.invoke(t)
    }

    fun clear() {
        synchronized(this) { sb.setLength(0) }
        onChanged?.invoke("")
    }
}
