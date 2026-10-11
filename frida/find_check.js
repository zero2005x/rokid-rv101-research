// find_check.js — 列舉 com.rokid.glass.ota 中與「檢查更新」相關的類別與方法
'use strict';
Java.perform(function () {
    var classes = Java.enumerateLoadedClassesSync();
    var hits = [];
    classes.forEach(function (c) {
        if (/rokid.*ota|ota.*rokid/i.test(c)) {
            hits.push(c);
        }
    });
    console.log('== OTA 相關類別 (' + hits.length + ') ==');
    hits.forEach(function (c) {
        console.log('  ' + c);
        try {
            var k = Java.use(c);
            var ms = k.class.getDeclaredMethods();
            ms.forEach(function (m) {
                var n = m.getName();
                if (/check|update|ota|normalCmd|download|version/i.test(n)) {
                    console.log('      M ' + n + '(' + m.getParameterTypes().map(function(t){return t.getName();}).join(',') + ')');
                }
            });
        } catch (e) {}
    });
});
