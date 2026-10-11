package com.rokid.rc.shared.protocol

import org.json.JSONObject

/** Glasses WiFi hotspot (SoftAP) and media channel info; attached to DEVICE_INFO /
 *  WIFI_AP_STATUS. */
data class WifiApInfo(
    val up: Boolean,
    val ssid: String,
    val pass: String,
    val ip: String,
    val port: Int
) {
    fun toJson(): JSONObject = JSONObject()
        .put("up", up)
        .put("ssid", ssid)
        .put("pass", pass)
        .put("ip", ip)
        .put("port", port)

    companion object {
        fun fromJson(o: JSONObject): WifiApInfo = WifiApInfo(
            up = o.optBoolean("up", false),
            ssid = o.optString("ssid", Protocol.AP_SSID),
            pass = o.optString("pass", Protocol.AP_PASS),
            ip = o.optString("ip", Protocol.AP_IP),
            port = o.optInt("port", Protocol.WIFI_PORT)
        )
    }
}
