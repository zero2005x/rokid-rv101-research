package com.rokid.rc.shared.protocol

import org.json.JSONArray
import org.json.JSONObject

/**
 * 訊息信封建構/解析工具。
 * 信封：{ "t": type, "id": requestId, ...fields }
 * 回應：{ "t": type, "replyTo": requestId, ...fields }
 */
object Msg {
    const val KEY_TYPE = "t"
    const val KEY_ID = "id"
    const val KEY_REPLY_TO = "replyTo"

    fun build(type: String, id: Long = 0L, vararg fields: Pair<String, Any?>): JSONObject {
        val o = JSONObject().put(KEY_TYPE, type).put(KEY_ID, id)
        putFields(o, *fields)
        return o
    }

    fun reply(replyTo: Long, type: String, vararg fields: Pair<String, Any?>): JSONObject {
        val o = JSONObject().put(KEY_TYPE, type).put(KEY_REPLY_TO, replyTo)
        putFields(o, *fields)
        return o
    }

    private fun putFields(o: JSONObject, vararg fields: Pair<String, Any?>) {
        for ((k, v) in fields) {
            when (v) {
                null -> o.put(k, JSONObject.NULL)
                is JSONObject, is JSONArray -> o.put(k, v)
                is Int -> o.put(k, v)
                is Long -> o.put(k, v)
                is Boolean -> o.put(k, v)
                is Double -> o.put(k, v)
                else -> o.put(k, v.toString())
            }
        }
    }

    fun type(o: JSONObject): String = o.optString(KEY_TYPE)
    fun id(o: JSONObject): Long = o.optLong(KEY_ID, 0L)
    fun replyTo(o: JSONObject): Long = o.optLong(KEY_REPLY_TO, 0L)

    fun string(o: JSONObject, k: String): String = o.optString(k)
    fun bool(o: JSONObject, k: String): Boolean = o.optBoolean(k)
    fun int(o: JSONObject, k: String): Int = o.optInt(k)
    fun long(o: JSONObject, k: String): Long = o.optLong(k)

    fun ack(replyTo: Long, ok: Boolean, error: String? = null, extra: JSONObject? = null): JSONObject {
        val fields = mutableListOf<Pair<String, Any?>>("ok" to ok)
        if (error != null) fields += "error" to error
        if (extra != null) fields += "data" to extra
        return reply(replyTo, MsgType.ACK, *fields.toTypedArray())
    }

    fun ok(o: JSONObject): Boolean = o.optBoolean("ok")
    fun error(o: JSONObject): String? = o.optString("error").takeIf { it.isNotEmpty() }
    fun data(o: JSONObject): JSONObject? = o.optJSONObject("data")
}
