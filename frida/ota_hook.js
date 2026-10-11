/**
 * ota_hook.js — hook com.rokid.glass.ota and dump the OTA version-check request/response.
 *
 * Three layers, each a fallback for the others:
 *   1. java.net.URL construction    -> the URL the app is connecting to (including the
 *                                      ota.rokid.com check API)
 *   2. OkHttp Request.Builder       -> request URL + headers (including the device auth
 *                                      signature)
 *      OkHttp ResponseBody.string() -> response JSON (including the latest download_url /
 *                                      version)
 *   3. SharedPreferences.Editor     -> what the app finally writes into ota_sp.xml:
 *        download_url / download_version / check_sum / image_size / change_log
 *
 * Usage:
 *   Real device (frida-server running): frida -U -f com.rokid.glass.ota -l ota_hook.js --no-pause
 *   Attach to a running app:            frida -U -n com.rokid.glass.ota -l ota_hook.js
 *
 * Output is tagged [OTA-HOOK] for easy filtering in logcat/terminal.
 */
'use strict';

var TAG = '[OTA-HOOK]';

function log(msg) { console.log(TAG + ' ' + msg); }
function safeStr(s) { try { return s.toString(); } catch (e) { return '<' + e + '>'; } }

// only dump URLs/content matching the keywords, to avoid a flood of unrelated log lines
function interesting(s) {
    if (s === null || s === undefined) return false;
    var t = String(s);
    return /ota|firmware|update|version|rom|upgrade|check|download|rokid/i.test(t);
}

Java.perform(function () {

    // =====================================================================
    // 1) java.net.URL construction — capture every URL
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
    // 2) OkHttp request + response
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
    // 3) SharedPreferences.Editor — capture the final download_url/version/checksum writes
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
