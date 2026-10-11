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
 * Masking for Settings ({@code adb_enabled} / {@code adb_wifi_enabled} / {@code development_settings_enabled}),
 * covering three detection paths:
 *
 * <ol>
 *   <li>{@code Settings$NameValueCache.getStringForUser} — the real read point for every
 *       {@code Settings.Global/Secure/System} getter (getInt/getLong/getFloat/getString), so one hook covers all.</li>
 *   <li>Static getters on {@code Settings$Global|Secure|System} — a safeguard for the few ROMs that take another path.</li>
 *   <li>{@code ContentResolver.query} — detectors that read the {@code content://settings/global} cursor directly.</li>
 * </ol>
 *
 * <p>Every hook runs inside the target app's process and calls {@link MaskConfig#get()} on each
 * read, so editing the config file takes effect immediately (no re-injection needed).
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

    // ── (1) NameValueCache: the path every getter must take ──────────────────
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
                    // Detectors often load their native check library only after reading settings -> rescan too
                    NativeMask.repatch();
                }
            });
            return true;
        } catch (Throwable t) {
            XposedBridge.log(MaskEntry.TAG + ": NameValueCache hook 失敗: " + t);
            return false;
        }
    }

    // ── (2) Static getters (fallback layer) ─────────────────────────────────
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

    /** {@code MethodHookParam.method} is a {@link java.lang.reflect.Member}; cast it to Method to get the return type. */
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

    // ── (3) ContentResolver.query: direct cursor read route ─────────────────
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
     * Replace masked keys in a settings cursor with their fake values.
     *
     * <p>If no row matches, return the original cursor unwrapped to minimise the behavioural impact on callers.
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
                // content://settings/global/adb_enabled — the key is in the path and the cursor only has a value column
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
                // some cursors do not support getPosition
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
                    // extras are optional
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
