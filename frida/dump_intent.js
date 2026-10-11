// dump_intent.js — 診斷：dump Intent.getStringExtra 的 key/value，找出正確 broadcast key
'use strict';
Java.perform(function () {
    try {
        var Intent = Java.use('android.content.Intent');
        Intent.getStringExtra.overload('java.lang.String').implementation = function (k) {
            var v = this.getStringExtra(k);
            console.log('[INTENT] getStringExtra("' + k + '") = "' + v + '"');
            return v;
        };
        console.log('[INTENT] hook getStringExtra OK');
    } catch (e) { console.log('[INTENT] hook 失敗: ' + e); }
});
