package com.rokid.rc.usbdebughider;

import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedBridge;
import de.robv.android.xposed.XposedHelpers;

/**
 * {@code android.os.SystemProperties} 的遮蔽（Java 端）。
 *
 * <p>對 {@code sys.usb.*} / {@code init.svc.adbd} / {@code ro.debuggable} 等 key，
 * 讓目標 App 看到「USB 只有 MTP、adbd 沒在跑、非 debuggable」。
 *
 * <p>注意：這是<b>行程內</b>的假象，屬性區（property area）本身沒被改動，
 * 因此不會影響 init/usbd 真正要用的值，也不會弄斷 adb —— 這正是選 per-app 而非
 * 全域 {@code resetprop} 的原因。
 *
 * <p>兩層並用：
 * <ul>
 *   <li><b>Java 層</b>：hook {@code android.os.SystemProperties} 的 get/getInt/getLong/getBoolean。</li>
 *   <li><b>native 層</b>：{@link NativeMask} 對 {@code __system_property_get} /
 *       {@code __system_property_read} / {@code __system_property_read_callback} 做 PLT/GOT hook，
 *       攔下繞過 Java 層的直讀。</li>
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
        // 注入當下多半還沒有 app context，載入會失敗；之後由 hook 內與 Application.onCreate 重試
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
                NativeMask.install(cfg); // 冪等：未載入則重試，已載入則必要時更新遮蔽表
                NativeMask.repatch();    // 補掃之後才載入的 .so
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
            if (rt == boolean.class || rt == Boolean.class) {
                return !("0".equals(fake) || "false".equalsIgnoreCase(fake));
            }
        } catch (Throwable ignored) {
            return null;
        }
        return null;
    }
}
