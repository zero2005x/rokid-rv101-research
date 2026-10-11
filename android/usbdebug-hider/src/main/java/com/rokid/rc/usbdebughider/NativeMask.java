package com.rokid.rc.usbdebughider;

import android.content.Context;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;

import android.app.AndroidAppHelper;
import de.robv.android.xposed.XposedBridge;

/**
 * native 層（PLT/GOT hook）的載入與控制。
 *
 * <p>目標 App 行程內沒有我們的 .so，而 Android 11+ 的套件可見性讓目標 App 查不到模組包名
 * （{@code PackageManager.NameNotFoundException}），Vector 又是用 in-memory dex 載入模組
 * （{@code /proc/self/maps} 也看不到 APK 路徑）—— 所以 .so 直接以 Base64 內嵌在 dex
 * （{@link EmbeddedSo}），執行期解到<b>目標 App 自己的 cacheDir</b> 再 {@code System.load()}。
 * 不需 root、不寫別人的目錄、不依賴任何路徑探索。
 *
 * <p>注入時機（handleLoadPackage）通常還沒有 Application context，因此載入會失敗；
 * 之後由 {@code Instrumentation.callApplicationOnCreate} 的 hook 與屬性 hook 重試。
 */
final class NativeMask {

    private static final String SO_NAME = "libdebugmask.so";

    private static volatile boolean sLoaded;
    private static volatile boolean sInstalled;
    private static volatile String sTable = "";
    private static volatile long sLastRepatch;
    /** 載入失敗重試上限，避免每次屬性讀取都重試（log 洗版）。 */
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
     * 請 native 層補掃「之後才載入的 .so」。
     *
     * <p>Java 端節流 200ms：偵測器通常在讀 settings 之後才載入自己的 native 檢查庫，
     * 這條路徑讓那個時間差被補上（native 端另有每秒一次的背景重掃）。
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
            // native 層出錯不影響 Java 層遮蔽
        }
    }

    /** 立即補掃（App 剛載入 native 庫時呼叫；不節流）。 */
    static void repatchNow() {
        if (!sInstalled) {
            return;
        }
        try {
            nativeRepatch();
        } catch (Throwable ignored) {
            // 不影響 Java 層遮蔽
        }
    }

    /** 冪等：已安裝就只在遮蔽表變動時更新。 */
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
            return false; // 還沒有 app context，之後重試
        }
        if (sAttempts >= 3) {
            return false; // 放棄（例如 ABI 不符）；改 conf 重載後仍會再試
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

    /** 把內嵌的 .so 位元組寫進目標 App 自己的 cacheDir（不寫別人的目錄、不需 root）。 */
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
