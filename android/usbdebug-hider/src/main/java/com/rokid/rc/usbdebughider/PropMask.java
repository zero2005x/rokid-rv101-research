package com.rokid.rc.usbdebughider;

import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedBridge;
import de.robv.android.xposed.XposedHelpers;

/**
 * Masking for {@code android.os.SystemProperties} (Java side).
 *
 * <p>For keys such as {@code sys.usb.*} / {@code init.svc.adbd} / {@code ro.debuggable}, it makes the
 * target app see "USB is MTP only, adbd is not running, not debuggable".
 *
 * <p>NOTE: this is an <b>in-process</b> illusion; the property area itself is untouched, so it
 * does not affect the values init/usbd actually use and does not break adb — which is exactly
 * why this is per-app rather than a global {@code resetprop}.
 *
 * <p>Both layers are used together:
 * <ul>
 *   <li><b>Java layer</b>: hook get/getInt/getLong/getBoolean on {@code android.os.SystemProperties}.</li>
 *   <li><b>native layer</b>: {@link NativeMask} applies PLT/GOT hooks to {@code __system_property_get} /
 *       {@code __system_property_read} / {@code __system_property_read_callback} to intercept
 *       direct reads that bypass the Java layer.</li>
 * </ul>
 */
final class PropMask {

    private PropMask() {
    }

    static boolean install(ClassLoader cl, MaskConfig cfg) {
        boolean ok;
        try {
            Class<?> sp = XposedHelpers.findClass("android.os.SystemProperties", cl);
            XposedBridge.hookAllMethods(sp, "get", HOOK);
            XposedBridge.hookAllMethods(sp, "getInt", HOOK);
            XposedBridge.hookAllMethods(sp, "getLong", HOOK);
            XposedBridge.hookAllMethods(sp, "getBoolean", HOOK);
            ok = true;
        } catch (Throwable t) {
            XposedBridge.log(MaskEntry.TAG + ": SystemProperties hook 失敗: " + t);
            ok = false;
        }
        // At injection time there is usually no app context yet and loading fails; retried later
        // from the hook and Application.onCreate
        if (cfg.maskProps && cfg.maskPropsNative) {
            NativeMask.install(cfg);
        }
        return ok;
    }

    private static final XC_MethodHook HOOK = new XC_MethodHook() {
        @Override
        protected void afterHookedMethod(MethodHookParam param) {
            MaskConfig cfg = MaskConfig.get();
            if (cfg.maskProps && cfg.maskPropsNative) {
                NativeMask.install(cfg); // idempotent: retry if not loaded, refresh the mask table if already loaded
                NativeMask.repatch();    // rescan .so files loaded later
            }
            Object[] a = param.args;
            if (a == null || a.length < 1 || !(a[0] instanceof String)) {
                return;
            }
            String fake = MaskConfig.get().prop((String) a[0]);
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
            if (rt == boolean.class || rt == Boolean.class) {
                return !("0".equals(fake) || "false".equalsIgnoreCase(fake));
            }
        } catch (Throwable ignored) {
            return null;
        }
        return null;
    }
}
