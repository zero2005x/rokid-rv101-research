// dump_intent.js — diagnostic: dump Intent.getStringExtra key/value to find the right broadcast key
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
