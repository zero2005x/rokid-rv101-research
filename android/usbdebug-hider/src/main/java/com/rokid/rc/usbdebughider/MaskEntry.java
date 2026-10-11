package com.rokid.rc.usbdebughider;

import de.robv.android.xposed.IXposedHookLoadPackage;
import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedHelpers;
import de.robv.android.xposed.XposedBridge;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

/**
 * Module entry point (classic Xposed API; {@code assets/xposed_init} points here).
 *
 * <p>Hooks are installed only inside third-party app processes:
 * <ul>
 *   <li>{@code android} / system_server is never touched (avoids altering system behaviour
 *       and avoids a boot loop).</li>
 *   <li>uid &lt; 10000 (root/system/shell) is not masked, so adb, the settings UI and system
 *       file transfer still read the real values.</li>
 *   <li>The module's own process is not masked (so its UI can show the device's real state).</li>
 * </ul>
 */
public final class MaskEntry implements IXposedHookLoadPackage {

    static final String TAG = "UsbDebugMask";
    private static final String SELF_PKG = "com.rokid.rc.usbdebughider";
    private static final int FIRST_APP_UID = 10000;

    private static volatile boolean sInstalled;

    @Override
    public void handleLoadPackage(XC_LoadPackage.LoadPackageParam lp) throws Throwable {
        try {
            if (lp == null || lp.packageName == null) {
                return;
            }
            if (SELF_PKG.equals(lp.packageName) || "android".equals(lp.packageName)) {
                return;
            }

            int uid = android.os.Process.myUid();
            if (uid < FIRST_APP_UID) {
                return;
            }

            MaskConfig cfg = MaskConfig.get();
            if (!cfg.shouldMask(lp.packageName)) {
                return;
            }
            if (sInstalled) {
                // handleLoadPackage is called once per package in the same process; install only once.
                return;
            }

            boolean s = SettingsMask.install(lp.classLoader, cfg);
            boolean p = PropMask.install(lp.classLoader, cfg);
            hookAppCreate(lp.classLoader);
            sInstalled = true;
            XposedBridge.log(TAG + ": 已遮蔽 " + lp.packageName + " (uid=" + uid
                    + ", process=" + lp.processName + ", settings=" + s + ", props=" + p
                    + ", source=" + cfg.source + ")");
        } catch (Throwable t) {
            XposedBridge.log(TAG + ": handleLoadPackage 失敗: " + t);
            XposedBridge.log(t);
        }
    }

    /**
     * The native layer needs an app context to load and there usually is none at injection time;
     * this retries it after {@code Instrumentation.callApplicationOnCreate}.
     */
    private static void hookAppCreate(ClassLoader cl) {
        try {
            XposedHelpers.findAndHookMethod("android.app.Instrumentation", cl,
                    "callApplicationOnCreate", android.app.Application.class, new XC_MethodHook() {
                        @Override
                        protected void afterHookedMethod(MethodHookParam param) {
                            MaskConfig cfg = MaskConfig.get();
                            if (cfg.maskProps && cfg.maskPropsNative && !NativeMask.isInstalled()) {
                                NativeMask.install(cfg);
                            }
                        }
                    });
        } catch (Throwable t) {
            XposedBridge.log(TAG + ": Application.onCreate hook 失敗: " + t);
        }
    }
}
