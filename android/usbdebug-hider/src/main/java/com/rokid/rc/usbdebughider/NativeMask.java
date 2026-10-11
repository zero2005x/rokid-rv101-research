package com.rokid.rc.usbdebughider;

import android.content.Context;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;

import android.app.AndroidAppHelper;
import de.robv.android.xposed.XposedBridge;

/**
 * Loading and control of the native layer (PLT/GOT hook).
 *
 * <p>The target app's process contains no .so of ours, and Android 11+ package visibility stops the target app from
 * resolving the module package name ({@code PackageManager.NameNotFoundException}), while Vector loads modules as
 * in-memory dex (the APK path is not visible in {@code /proc/self/maps} either) — so the .so is embedded directly in
 * the dex as Base64 ({@link EmbeddedSo}) and at runtime is unpacked into the <b>target app's own cacheDir</b> before
 * {@code System.load()}. No root, no writing into someone else's directory, no reliance on any path discovery.
 *
 * <p>The injection point (handleLoadPackage) usually has no Application context yet, so loading fails there;
 * the hook on {@code Instrumentation.callApplicationOnCreate} and the property hook retry later.
 */
final class NativeMask {

    private static final String SO_NAME = "libdebugmask.so";

    private static volatile boolean sLoaded;
    private static volatile boolean sInstalled;
    private static volatile String sTable = "";
    private static volatile long sLastRepatch;
    /** Retry cap for failed loads, so not every property read retries (log spam). */
    private static int sAttempts;

    private NativeMask() {
    }

    static boolean isInstalled() {
        return sInstalled;
    }

    static native boolean nativeInstall(String table);

    static native boolean nativeSetTable(String table);

    static native int nativePatchedSlots();

    static native int nativeRepatch();

    static native String nativeGetProp(String key);

    /**
     * Ask the native layer to re-scan ".so files loaded later".
     *
     * <p>The Java side throttles to 200 ms: the detector usually loads its own native check library after reading
     * settings, and this path covers that time gap (the native side also re-scans in the background once a second).
     */
    static void repatch() {
        if (!sInstalled) {
            return;
        }
        long now = android.os.SystemClock.uptimeMillis();
        if (now - sLastRepatch < 200) {
            return;
        }
        sLastRepatch = now;
        try {
            nativeRepatch();
        } catch (Throwable ignored) {
            // an error in the native layer does not affect Java-layer masking
        }
    }

    /** Immediate re-scan (called when the app has just loaded a native library; no throttling). */
    static void repatchNow() {
        if (!sInstalled) {
            return;
        }
        try {
            nativeRepatch();
        } catch (Throwable ignored) {
            // does not affect Java-layer masking
        }
    }

    /** Idempotent: once installed it only updates when the mask table changes. */
    static synchronized boolean install(MaskConfig cfg) {
        String table = cfg.propTableCache;
        if (sInstalled) {
            if (!table.equals(sTable)) {
                sTable = table;
                try {
                    nativeSetTable(table);
                } catch (Throwable t) {
                    XposedBridge.log(MaskEntry.TAG + ": native 更新表失敗: " + t);
                }
            }
            return true;
        }
        if (!sLoaded && !load()) {
            return false;
        }
        try {
            nativeInstall(table);
            sTable = table;
            sInstalled = true;
            XposedBridge.log(MaskEntry.TAG + ": native PLT hook 完成，改寫 "
                    + nativePatchedSlots() + " 個 GOT slot");
            return true;
        } catch (Throwable t) {
            XposedBridge.log(MaskEntry.TAG + ": native PLT hook 失敗: " + t);
            return false;
        }
    }

    private static boolean load() {
        Context ctx = AndroidAppHelper.currentApplication();
        if (ctx == null) {
            return false; // no app context yet, retry later
        }
        if (sAttempts >= 3) {
            return false; // give up (e.g. ABI mismatch); a conf reload will still try again
        }
        sAttempts++;
        if (EmbeddedSo.SIZE == 0) {
            XposedBridge.log(MaskEntry.TAG + ": 此 APK 未內嵌 libdebugmask.so，native 層停用");
            return false;
        }
        try {
            File out = new File(ctx.getCacheDir(), SO_NAME);
            if (!out.isFile() || out.length() != EmbeddedSo.SIZE) {
                write(out);
            }
            System.load(out.getAbsolutePath());
            sLoaded = true;
            XposedBridge.log(MaskEntry.TAG + ": native 層已載入 -> " + out.getAbsolutePath());
            return true;
        } catch (Throwable t) {
            XposedBridge.log(MaskEntry.TAG + ": 載入 " + SO_NAME + " 失敗: " + t);
            return false;
        }
    }

    /** Write the embedded .so bytes into the target app's own cacheDir (no writing into someone else's directory, no root needed). */
    private static void write(File out) throws Exception {
        byte[] bytes = android.util.Base64.decode(EmbeddedSo.B64, android.util.Base64.DEFAULT);
        if (bytes.length != EmbeddedSo.SIZE) {
            throw new IllegalStateException("內嵌 .so 長度不符: " + bytes.length + " != " + EmbeddedSo.SIZE);
        }
        try (OutputStream os = new FileOutputStream(out)) {
            os.write(bytes);
        }
        //noinspection ResultOfMethodCallIgnored
        out.setReadable(true, true);
        //noinspection ResultOfMethodCallIgnored
        out.setExecutable(true, true);
    }
}
