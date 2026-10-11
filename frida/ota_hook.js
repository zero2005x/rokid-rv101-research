/**
 * ota_hook.js — 掛 com.rokid.glass.ota，dump OTA 版本檢查的「請求/回應」。
 *
 * 抓三層，互為備援：
 *   1. java.net.URL 建構       → 抓到 app 正在連的網址（含 ota.rokid.com check API）
 *   2. OkHttp Request.Builder  → 抓到請求 URL + headers（含裝置認證簽名）
 *      OkHttp ResponseBody.string() → 抓到回應 JSON（含最新 download_url / version）
 *   3. SharedPreferences.Editor→ 抓到 app 寫入 ota_sp.xml 的最終結果：
 *        download_url / download_version / check_sum / image_size / change_log
 *
 * 用法：
 *   真機（frida-server 已跑）：frida -U -f com.rokid.glass.ota -l ota_hook.js --no-pause
 *   附加已執行 app：           frida -U -n com.rokid.glass.ota -l ota_hook.js
 *
 * 輸出標記 [OTA-HOOK]，方便 logcat/終端過濾。
 */
'use strict';

var TAG = '[OTA-HOOK]';

function log(msg) { console.log(TAG + ' ' + msg); }
function safeStr(s) { try { return s.toString(); } catch (e) { return '<' + e + '>'; } }

// 只 dump 含關鍵字的 URL/內容，避免大量無關 log
function interesting(s) {
    if (s === null || s === undefined) return false;
    var t = String(s);
    return /ota|firmware|update|version|rom|upgrade|check|download|rokid/i.test(t);
}

Java.perform(function () {

    // =====================================================================
    // 1) java.net.URL 建構 — 抓所有 URL
    // =====================================================================
    try {
        var URL = Java.use('java.net.URL');
        URL.$init.overload('java.lang.String').implementation = function (spec) {
            var s = safeStr(spec);
            if (interesting(s)) log('URL: ' + s);
            return this.$init(spec);
        };
        log('hook: java.net.URL(String) OK');
    } catch (e) { log('hook URL 失敗: ' + e); }

    // =====================================================================
    // 2) OkHttp 請求 + 回應
    // =====================================================================
    try {
        var ReqBuilder = Java.use('okhttp3.Request$Builder');
        ReqBuilder.build.implementation = function () {
            var req = this.build();
            try {
                var u = req.url().toString();
                if (interesting(u)) {
                    log('REQ ' + req.method() + ' ' + u);
                    var hs = req.headers();
                    for (var i = 0; i < hs.size(); i++) {
                        log('  HDR ' + hs.name(i) + ': ' + hs.value(i));
                    }
                    var body = req.body();
                    if (body !== null) log('  BODY(' + body.contentType() + ') len=' + body.contentLength());
                }
            } catch (e) { /* ignore */ }
            return req;
        };
        log('hook: okhttp3.Request$Builder.build OK');
    } catch (e) { log('hook OkHttp Request 失敗: ' + e); }

    try {
        var RespBody = Java.use('okhttp3.ResponseBody');
        RespBody.string.implementation = function () {
            var s = this.string();
            if (interesting(s)) log('RESP-BODY: ' + s);
            return s;
        };
        log('hook: okhttp3.ResponseBody.string OK');
    } catch (e) { log('hook OkHttp ResponseBody 失敗: ' + e); }

    // =====================================================================
    // 3) SharedPreferences.Editor — 抓最終寫入的 download_url/version/checksum
    // =====================================================================
    var Editor = null;
    try {
        Editor = Java.use('android.content.SharedPreferences$Editor');

        var putString = Editor.putString.overload('java.lang.String', 'java.lang.String');
        putString.implementation = function (k, v) {
            var key = safeStr(k), val = safeStr(v);
            if (interesting(key) || interesting(val)) log('SP.putString(' + key + ') = ' + val);
            return this.putString(k, v);
        };

        var putLong = Editor.putLong.overload('java.lang.String', 'long');
        putLong.implementation = function (k, v) {
            var key = safeStr(k);
            if (interesting(key)) log('SP.putLong(' + key + ') = ' + v);
            return this.putLong(k, v);
        };

        var putInt = Editor.putInt.overload('java.lang.String', 'int');
        putInt.implementation = function (k, v) {
            var key = safeStr(k);
            if (interesting(key)) log('SP.putInt(' + key + ') = ' + v);
            return this.putInt(k, v);
        };

        var putBoolean = Editor.putBoolean.overload('java.lang.String', 'boolean');
        putBoolean.implementation = function (k, v) {
            var key = safeStr(k);
            if (interesting(key)) log('SP.putBoolean(' + key + ') = ' + v);
            return this.putBoolean(k, v);
        };

        log('hook: SharedPreferences$Editor OK');
    } catch (e) { log('hook SharedPreferences 失敗: ' + e); }

    log('all hooks installed');
});
