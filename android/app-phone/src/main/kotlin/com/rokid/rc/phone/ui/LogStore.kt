package com.rokid.rc.phone.ui

/**
 * Shared operation log buffer.
 *
 * After the redesign the "operation log" is shown only on the remote tab, but actions on the settings tab
 * (toggling WiFi/silent/root/ring …) must be written into the same log too, so the buffer is extracted for sharing
 * (a single source, so the two logs cannot drift apart).
 */
object LogStore {

    private const val MAX_CHARS = 20_000
    private val sb = StringBuilder()

    /** Content-change callback (registered by the remote tab; null while the tab is not in the foreground). */
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
