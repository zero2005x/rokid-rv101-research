// ota_hook_trigger.js — 掛 URL/Header/SharedPreferences 後，直接呼叫 CheckService 觸發版本檢查
'use strict';
var TAG = '[OTA-HOOK]';
function log(m) { console.log(TAG + ' ' + m); }
function interesting(s) { return s != null && /ota|firmware|update|version|rom|upgrade|check|download|rokid|sign|token|auth/i.test(String(s)); }

Java.perform(function () {
    // ---- URL ----
    try {
        var URL = Java.use('java.net.URL');
        URL.$init.overload('java.lang.String').implementation = function (spec) {
            if (interesting(String(spec))) log('URL: ' + spec);
            return this.$init(spec);
        };
    } catch (e) { log('URL hook 失敗: ' + e); }

    // ---- 請求 header（HttpURLConnection / URLConnection）----
    try {
        var Conn = Java.use('java.net.URLConnection');
        Conn.setRequestProperty.overload('java.lang.String', 'java.lang.String').implementation = function (k, v) {
            if (interesting(String(k)) || interesting(String(v))) log('HDR ' + k + ': ' + v);
            return this.setRequestProperty(k, v);
        };
        log('hook setRequestProperty OK');
    } catch (e) { log('setRequestProperty hook 失敗: ' + e); }

    // ---- 回應 status ----
    try {
        var HUC = Java.use('java.net.HttpURLConnection');
        HUC.getResponseCode.implementation = function () {
            var c = this.getResponseCode();
            log('RESP-CODE: ' + c);
            return c;
        };
        log('hook getResponseCode OK');
    } catch (e) { log('getResponseCode hook 失敗: ' + e); }

    // ---- SharedPreferences ----
    try {
        var Editor = Java.use('android.content.SharedPreferences$Editor');
        Editor.putString.overload('java.lang.String', 'java.lang.String').implementation = function (k, v) {
            var key = String(k), val = String(v);
            if (interesting(key) || interesting(val)) log('SP.putString(' + key + ') = ' + val);
            return this.putString(k, v);
        };
        Editor.putLong.overload('java.lang.String', 'long').implementation = function (k, v) {
            if (interesting(String(k))) log('SP.putLong(' + k + ') = ' + v);
            return this.putLong(k, v);
        };
    } catch (e) { log('SP hook 失敗: ' + e); }

    // ---- 觸發 ----
    try {
        Java.choose('com.rokid.glass.ota.component.CheckService', {
            onMatch: function (inst) {
                log('找到 CheckService，呼叫 checkNewOtaVersion()');
                try { inst.checkNewOtaVersion(); log('checkNewOtaVersion() 已呼叫'); }
                catch (e) { log('checkNewOtaVersion() 失敗: ' + e); }
            },
            onComplete: function () { log('CheckService choose 完成'); }
        });
    } catch (e) { log('觸發失敗: ' + e); }
});
