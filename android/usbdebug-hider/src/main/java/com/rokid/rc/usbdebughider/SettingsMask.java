package com.rokid.rc.usbdebughider;

import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.Bundle;

import java.util.List;

import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedBridge;
import de.robv.android.xposed.XposedHelpers;

/**
 * Settings（{@code adb_enabled} / {@code adb_wifi_enabled} / {@code development_settings_enabled}）
 * 的遮蔽，覆蓋三條偵測途徑：
 *
 * <ol>
 *   <li>{@code Settings$NameValueCache.getStringForUser} —— {@code Settings.Global/Secure/System}
 *       所有 getter（getInt/getLong/getFloat/getString）真正的取值點，一次 hook 全部覆蓋。</li>
 *   <li>{@code Settings$Global|Secure|System} 的靜態 getter —— 少數 ROM 走不同路徑時的保險。</li>
 *   <li>{@code ContentResolver.query} —— 直接讀 {@code content://settings/global} cursor 的偵測器。</li>
 * </ol>
 *
 * <p>所有 hook 都在目標 App 的行程內，且每次取值才呼叫 {@link MaskConfig#get()}，
 * 因此改設定檔即時生效（不必重新注入）。
 */
final class SettingsMask {

    private SettingsMask() {
    }

    static boolean install(ClassLoader cl, MaskConfig cfg) {
        boolean ok = false;
        ok |= hookNameValueCache(cl);
        ok |= hookStaticGetters(cl);
        if (cfg.maskQuery) {
            ok |= hookQuery(cl);
        }
        return ok;
    }

    // ── (1) NameValueCache：所有 getter 的必經之路 ───────────────────────────
    private static boolean hookNameValueCache(ClassLoader cl) {
        try {
            Class<?> nvc = XposedHelpers.findClass("android.provider.Settings$NameValueCache", cl);
            XposedBridge.hookAllMethods(nvc, "getStringForUser", new XC_MethodHook() {
                @Override
                protected void afterHookedMethod(MethodHookParam param) {
                    Object[] a = param.args;
                    if (a == null || a.length < 2 || !(a[1] instanceof String)) {
                        return;
                    }
                    String fake = MaskConfig.get().setting((String) a[1]);
                    if (fake != null) {
                        param.setResult(fake);
                    }
                    // 偵測器常在讀完 settings 後才載入自己的 native 檢查庫 → 順手補掃
                    NativeMask.repatch();
                }
            });
            return true;
        } catch (Throwable t) {
            XposedBridge.log(MaskEntry.TAG + ": NameValueCache hook 失敗: " + t);
            return false;
        }
    }

    // ── (2) 靜態 getter（保險層） ───────────────────────────────────────────
    private static boolean hookStaticGetters(ClassLoader cl) {
        boolean ok = false;
        for (String name : new String[]{
                "android.provider.Settings$Global",
                "android.provider.Settings$Secure",
                "android.provider.Settings$System"}) {
            try {
                Class<?> c = XposedHelpers.findClass(name, cl);
                XposedBridge.hookAllMethods(c, "getString", GETTER);
                XposedBridge.hookAllMethods(c, "getInt", GETTER);
                XposedBridge.hookAllMethods(c, "getLong", GETTER);
                XposedBridge.hookAllMethods(c, "getFloat", GETTER);
                ok = true;
            } catch (Throwable t) {
                XposedBridge.log(MaskEntry.TAG + ": " + name + " hook 失敗: " + t);
            }
        }
        return ok;
    }

    private static final XC_MethodHook GETTER = new XC_MethodHook() {
        @Override
        protected void afterHookedMethod(MethodHookParam param) {
            Object[] a = param.args;
            if (a == null || a.length < 2 || !(a[1] instanceof String)) {
                return;
            }
            String fake = MaskConfig.get().setting((String) a[1]);
            if (fake == null) {
                return;
            }
            Object v = coerce(fake, returnType(param));
            if (v != null) {
                param.setResult(v);
            }
        }
    };

    /** {@code MethodHookParam.method} 是 {@link java.lang.reflect.Member}，要自己轉成 Method 才拿得到回傳型別。 */
    private static Class<?> returnType(XC_MethodHook.MethodHookParam param) {
        java.lang.reflect.Member m = param.method;
        return (m instanceof java.lang.reflect.Method) ? ((java.lang.reflect.Method) m).getReturnType() : null;
    }

    private static Object coerce(String fake, Class<?> rt) {
        try {
            if (rt == String.class) {
                return fake;
            }
            if (rt == int.class || rt == Integer.class) {
                return Integer.parseInt(fake);
            }
            if (rt == long.class || rt == Long.class) {
                return Long.parseLong(fake);
            }
            if (rt == float.class || rt == Float.class) {
                return Float.parseFloat(fake);
            }
            if (rt == boolean.class || rt == Boolean.class) {
                return !("0".equals(fake) || "false".equalsIgnoreCase(fake));
            }
        } catch (Throwable ignored) {
            return null;
        }
        return null;
    }

    // ── (3) ContentResolver.query：cursor 直讀 route ────────────────────────
    private static boolean hookQuery(ClassLoader cl) {
        try {
            Class<?> cr = XposedHelpers.findClass("android.content.ContentResolver", cl);
            XposedBridge.hookAllMethods(cr, "query", new XC_MethodHook() {
                @Override
                protected void afterHookedMethod(MethodHookParam param) {
                    MaskConfig cfg = MaskConfig.get();
                    if (!cfg.maskQuery) {
                        return;
                    }
                    Object res = param.getResult();
                    if (!(res instanceof Cursor)) {
                        return;
                    }
                    Object[] a = param.args;
                    if (a == null || a.length < 1 || !(a[0] instanceof Uri)) {
                        return;
                    }
                    Uri uri = (Uri) a[0];
                    if (!isSettingsUri(uri)) {
                        return;
                    }
                    Cursor out = filter((Cursor) res, uri, cfg);
                    if (out != null && out != res) {
                        param.setResult(out);
                    }
                }
            });
            return true;
        } catch (Throwable t) {
            XposedBridge.log(MaskEntry.TAG + ": ContentResolver.query hook 失敗: " + t);
            return false;
        }
    }

    private static boolean isSettingsUri(Uri uri) {
        String auth = uri.getAuthority();
        return "settings".equals(auth) || "com.android.providers.settings".equals(auth);
    }

    /**
     * 把 settings cursor 內被遮蔽的 key 換成假值。
     *
     * <p>若沒有任何一列命中，直接回傳原 cursor（不包裝），把對呼叫端的行為影響降到最低。
     */
    private static Cursor filter(Cursor src, Uri uri, MaskConfig cfg) {
        try {
            String[] cols = src.getColumnNames();
            int nameIdx = indexOf(cols, "name");
            int valueIdx = indexOf(cols, "value");
            if (valueIdx < 0) {
                return src;
            }

            String uriName = null;
            if (nameIdx < 0) {
                // content://settings/global/adb_enabled —— key 在路徑上，cursor 只有 value 欄
                List<String> segs = uri.getPathSegments();
                if (!segs.isEmpty()) {
                    String last = segs.get(segs.size() - 1);
                    if (cfg.setting(last) != null) {
                        uriName = last;
                    }
                }
                if (uriName == null) {
                    return src;
                }
            }

            int pos = -1;
            try {
                pos = src.getPosition();
            } catch (Throwable ignored) {
                // 某些 cursor 不支援 getPosition
            }

            boolean hit = false;
            try {
                if (src.moveToFirst()) {
                    do {
                        String n = uriName;
                        if (nameIdx >= 0 && src.getType(nameIdx) != Cursor.FIELD_TYPE_NULL) {
                            n = src.getString(nameIdx);
                        }
                        if (n != null && cfg.setting(n) != null) {
                            hit = true;
                            break;
                        }
                    } while (src.moveToNext());
                }
            } finally {
                if (pos >= 0) {
                    try {
                        src.moveToPosition(pos);
                    } catch (Throwable ignored) {
                        // ignore
                    }
                }
            }
            if (!hit) {
                return src;
            }

            MatrixCursor out = new MatrixCursor(cols);
            try {
                if (src.moveToFirst()) {
                    do {
                        Object[] row = new Object[cols.length];
                        for (int i = 0; i < cols.length; i++) {
                            row[i] = readCell(src, i);
                        }
                        Object nObj = uriName != null ? uriName : row[nameIdx];
                        if (nObj instanceof String) {
                            String fake = cfg.setting((String) nObj);
                            if (fake != null) {
                                row[valueIdx] = fake;
                            }
                        }
                        out.addRow(row);
                    } while (src.moveToNext());
                }
            } finally {
                try {
                    Bundle extras = src.getExtras();
                    if (extras != null) {
                        out.setExtras(extras);
                    }
                } catch (Throwable ignored) {
                    // extras 非必要
                }
                try {
                    src.close();
                } catch (Throwable ignored) {
                    // ignore
                }
            }
            return out;
        } catch (Throwable t) {
            XposedBridge.log(MaskEntry.TAG + ": filter cursor 失敗: " + t);
            return src;
        }
    }

    private static Object readCell(Cursor c, int i) {
        switch (c.getType(i)) {
            case Cursor.FIELD_TYPE_INTEGER:
                return c.getLong(i);
            case Cursor.FIELD_TYPE_FLOAT:
                return c.getDouble(i);
            case Cursor.FIELD_TYPE_BLOB:
                return c.getBlob(i);
            case Cursor.FIELD_TYPE_NULL:
                return null;
            default:
                return c.getString(i);
        }
    }

    private static int indexOf(String[] cols, String name) {
        for (int i = 0; i < cols.length; i++) {
            if (name.equals(cols[i])) {
                return i;
            }
        }
        return -1;
    }
}
