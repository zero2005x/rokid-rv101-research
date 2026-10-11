package com.rokid.rc.usbdebughider;

import de.robv.android.xposed.IXposedHookLoadPackage;
import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedHelpers;
import de.robv.android.xposed.XposedBridge;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

/**
 * 模組入口（經典 Xposed API，{@code assets/xposed_init} 指到這裡）。
 *
 * <p>只在「第三方 App 行程」內安裝 hook：
 * <ul>
 *   <li>{@code android} / system_server 一律不碰（避免動到系統行為、也避免 bootloop）。</li>
 *   <li>uid &lt; 10000（root/system/shell）不遮，所以 adb、設定頁、系統檔案傳輸讀到的仍是真值。</li>
 *   <li>模組自己的行程不遮（UI 才能顯示裝置真實狀態）。</li>
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
                // handleLoadPackage 對「同一行程中的每個 package」都會呼叫一次；只裝一次。
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
     * native 層需要 app context 才載得起來，注入當下通常還沒有；
     * 這裡在 {@code Instrumentation.callApplicationOnCreate} 之後補一次。
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
