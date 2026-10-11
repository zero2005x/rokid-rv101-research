package com.rokid.rc.usbdebughider;

import android.os.SystemClock;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.util.Collections;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/**
 * 遮蔽設定。
 *
 * <p>來源有兩層，後者覆寫前者：
 * <ol>
 *   <li><b>建置期預設</b>：{@code -PmaskTargets=...} 燒進 {@link BakedDefaults}（目標 App 白名單）。</li>
 *   <li><b>執行期覆寫</b>：{@value #CONF_PATH}，由 adb/root 寫入，hook 端每 {@value #RELOAD_INTERVAL_MS} ms
 *       檢查一次並自動重載（不必重開機、不必重建 APK）。</li>
 * </ol>
 *
 * <p>設定檔格式（{@code #} 之後為註解）：
 * <pre>
 * enabled=1                    # 總開關
 * targets=*                    # 目標 App：* / 空白分隔的包名 / !包名 = 排除
 * mask_settings=1              # Settings.Global/Secure/System 讀取
 * mask_query=1                 # ContentResolver.query（cursor 直讀 settings）
 * mask_props=1                 # SystemProperties 讀取
 * setting=adb_enabled=0        # 追加／覆寫要遮蔽的 settings key
 * prop=ro.boot.verifiedbootstate=green
 * del_setting=adb_wifi_enabled # 移除某個 key
 * del_prop=ro.debuggable
 * </pre>
 */
final class MaskConfig {

    static final String CONF_PATH = "/data/local/tmp/usbdebug_hider.conf";
    static final long RELOAD_INTERVAL_MS = 2_000L;

    /** 預設遮蔽的 settings（Global）key → App 會讀到的假值。 */
    static final Map<String, String> DEFAULT_SETTINGS;

    /** 預設遮蔽的系統屬性 key → App 會讀到的假值。 */
    static final Map<String, String> DEFAULT_PROPS;

    /**
     * 即使 uid >= 10000 也不遮蔽的套件：USB/MTP 檔案傳輸與媒體骨幹，
     * 遮蔽它們會讓 MTP 檔案傳輸與媒體掃描行為異常。
     */
    static final Set<String> DEFAULT_EXCLUDE;

    static {
        Map<String, String> s = new LinkedHashMap<>();
        s.put("adb_enabled", "0");
        s.put("adb_wifi_enabled", "0");
        s.put("development_settings_enabled", "0");
        DEFAULT_SETTINGS = Collections.unmodifiableMap(s);

        Map<String, String> p = new LinkedHashMap<>();
        // USB gadget 狀態：App 讀到「純 MTP、adbd 沒在跑」
        p.put("sys.usb.config", "mtp");
        p.put("sys.usb.state", "mtp");
        p.put("persist.sys.usb.config", "mtp");
        p.put("vendor.usb.config", "mtp");
        p.put("persist.vendor.usb.config", "mtp");
        p.put("init.svc.adbd", "stopped");
        p.put("service.adb.root", "0");
        p.put("service.adb.tcp.port", "-1");
        p.put("persist.service.adb.enable", "0");
        p.put("sys.usb.ffs.ready", "0");
        p.put("sys.usb.ffs.adb.ready", "0");
        // 除錯/建置旗標
        p.put("ro.debuggable", "0");
        p.put("ro.adb.secure", "1");
        p.put("ro.build.type", "user");
        p.put("ro.build.tags", "release-keys");
        p.put("persist.sys.debuggable", "0");
        DEFAULT_PROPS = Collections.unmodifiableMap(p);

        Set<String> e = new HashSet<>();
        e.add("com.android.mtp");
        e.add("com.android.providers.media.module");
        e.add("com.google.android.providers.media.module");
        e.add("com.android.providers.downloads");
        e.add("com.android.providers.downloads.ui");
        e.add("android.process.media");
        DEFAULT_EXCLUDE = Collections.unmodifiableSet(e);
    }

    // ── 生效設定 ────────────────────────────────────────────────────────────
    volatile boolean enabled = true;
    volatile boolean maskSettings = true;
    volatile boolean maskQuery = true;
    volatile boolean maskProps = true;
    /** native（PLT/GOT）層是否也遮蔽屬性；關掉就只剩 Java 層。 */
    volatile boolean maskPropsNative = true;
    volatile boolean allTargets = true;

    final Set<String> targets = new HashSet<>();
    final Set<String> excludes = new HashSet<>(DEFAULT_EXCLUDE);
    final Map<String, String> settings = new LinkedHashMap<>(DEFAULT_SETTINGS);
    final Map<String, String> props = new LinkedHashMap<>(DEFAULT_PROPS);

    String targetsRaw = "*";
    String source = "baked";
    /** props 遮蔽表的序列化結果（給 native 層用）；空字串 = native 層不遮蔽。 */
    String propTableCache = "";

    private MaskConfig() {
    }

    boolean shouldMask(String pkg) {
        if (!enabled || pkg == null || pkg.isEmpty()) {
            return false;
        }
        if (excludes.contains(pkg)) {
            return false;
        }
        return allTargets || targets.contains(pkg);
    }

    /** @return 該 settings key 要回給 App 的假值，null = 不遮蔽。 */
    String setting(String key) {
        return maskSettings ? settings.get(key) : null;
    }

    /** @return 該系統屬性要回給 App 的假值，null = 不遮蔽。 */
    String prop(String key) {
        return maskProps ? props.get(key) : null;
    }

    // ── 載入 ────────────────────────────────────────────────────────────────
    private static volatile MaskConfig sInstance;
    private static volatile long sNextCheck;

    static MaskConfig get() {
        long now = SystemClock.uptimeMillis();
        MaskConfig c = sInstance;
        if (c != null && now < sNextCheck) {
            return c;
        }
        synchronized (MaskConfig.class) {
            now = SystemClock.uptimeMillis();
            if (sInstance != null && now < sNextCheck) {
                return sInstance;
            }
            sInstance = load();
            sNextCheck = now + RELOAD_INTERVAL_MS;
            return sInstance;
        }
    }

    static MaskConfig load() {
        MaskConfig c = new MaskConfig();
        applyTargets(c, BakedDefaults.TARGETS);
        File f = new File(CONF_PATH);
        if (f.isFile() && f.canRead()) {
            c.source = CONF_PATH;
            parse(c, f);
        }
        c.propTableCache = buildPropTable(c);
        return c;
    }

    /** 把 props 遮蔽表序列化成 key=value\n... 給 native 層。 */
    private static String buildPropTable(MaskConfig c) {
        if (!c.maskProps || !c.maskPropsNative) {
            return "";
        }
        StringBuilder sb = new StringBuilder();
        for (Map.Entry<String, String> e : c.props.entrySet()) {
            sb.append(e.getKey()).append('=').append(e.getValue()).append('\n');
        }
        return sb.toString();
    }

    private static void applyTargets(MaskConfig c, String spec) {
        c.targetsRaw = spec == null ? "" : spec.trim();
        c.targets.clear();
        c.excludes.clear();
        c.excludes.addAll(DEFAULT_EXCLUDE);
        c.allTargets = false;
        for (String tok : c.targetsRaw.split("[,\\s]+")) {
            if (tok.isEmpty()) {
                continue;
            }
            if ("*".equals(tok) || "all".equalsIgnoreCase(tok)) {
                c.allTargets = true;
            } else if (tok.startsWith("!")) {
                if (tok.length() > 1) {
                    c.excludes.add(tok.substring(1));
                }
            } else {
                c.targets.add(tok);
            }
        }
        if (c.targets.isEmpty() && !c.allTargets) {
            // 空清單 → 保守起見視為「全部第三方 App」
            c.allTargets = true;
            if (c.targetsRaw.isEmpty()) {
                c.targetsRaw = "*";
            }
        }
    }

    private static void parse(MaskConfig c, File f) {
        try (BufferedReader r = new BufferedReader(new FileReader(f))) {
            String line;
            while ((line = r.readLine()) != null) {
                int hash = line.indexOf('#');
                if (hash >= 0) {
                    line = line.substring(0, hash);
                }
                line = line.trim();
                if (line.isEmpty()) {
                    continue;
                }
                int eq = line.indexOf('=');
                if (eq <= 0) {
                    continue;
                }
                String k = line.substring(0, eq).trim().toLowerCase(Locale.ROOT);
                String v = line.substring(eq + 1).trim();
                switch (k) {
                    case "enabled":
                        c.enabled = truthy(v);
                        break;
                    case "targets":
                    case "target":
                        applyTargets(c, v);
                        break;
                    case "mask_settings":
                        c.maskSettings = truthy(v);
                        break;
                    case "mask_query":
                        c.maskQuery = truthy(v);
                        break;
                    case "mask_props":
                        c.maskProps = truthy(v);
                        break;
                    case "mask_props_native":
                        c.maskPropsNative = truthy(v);
                        break;
                    case "setting":
                    case "settings":
                        putKv(c.settings, v);
                        break;
                    case "prop":
                    case "props":
                        putKv(c.props, v);
                        break;
                    case "del_setting":
                        c.settings.remove(v);
                        break;
                    case "del_prop":
                        c.props.remove(v);
                        break;
                    default:
                        break;
                }
            }
        } catch (Throwable ignored) {
            // 設定檔壞掉時沿用預設值，絕不讓 hook 端例外冒出去
        }
    }

    private static void putKv(Map<String, String> map, String kv) {
        int eq = kv.indexOf('=');
        if (eq <= 0) {
            return;
        }
        map.put(kv.substring(0, eq).trim(), kv.substring(eq + 1).trim());
    }

    private static boolean truthy(String v) {
        return "1".equals(v) || "true".equalsIgnoreCase(v) || "yes".equalsIgnoreCase(v)
                || "on".equalsIgnoreCase(v);
    }

    // ── 顯示用 ──────────────────────────────────────────────────────────────
    String describe() {
        StringBuilder sb = new StringBuilder();
        sb.append("enabled        = ").append(enabled).append('\n');
        sb.append("來源           = ").append(source).append('\n');
        sb.append("目標 targets   = ").append(targetsRaw).append('\n');
        sb.append("  全部第三方   = ").append(allTargets).append('\n');
        sb.append("  白名單       = ").append(targets).append('\n');
        sb.append("  排除         = ").append(excludes).append('\n');
        sb.append("mask_settings  = ").append(maskSettings).append('\n');
        sb.append("mask_query     = ").append(maskQuery).append('\n');
        sb.append("mask_props     = ").append(maskProps).append('\n');
        sb.append("  native(PLT)  = ").append(maskPropsNative)
                .append(" (").append(propTableCache.split("\n", -1).length - 1).append(" props)\n");
        sb.append("settings 遮蔽  = ").append(settings).append('\n');
        sb.append("props 遮蔽     = ").append(props).append('\n');
        return sb.toString();
    }
}
