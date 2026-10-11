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
 * Masking configuration.
 *
 * <p>There are two layers of configuration; the latter overrides the former:
 * <ol>
 *   <li><b>Build-time default</b>: {@code -PmaskTargets=...} is baked into {@link BakedDefaults}
 *       (target app allowlist).</li>
 *   <li><b>Runtime override</b>: {@value #CONF_PATH}, written by adb/root; the hook side checks every
 *       {@value #RELOAD_INTERVAL_MS} ms and reloads automatically (no reboot, no APK rebuild).</li>
 * </ol>
 *
 * <p>Config file format ({@code #} starts a comment):
 * <pre>
 * enabled=1                    # master switch
 * targets=*                    # target apps: * / space-separated package names / !pkg = exclude
 * mask_settings=1              # Settings.Global/Secure/System reads
 * mask_query=1                 # ContentResolver.query (direct cursor reads of settings)
 * mask_props=1                 # SystemProperties reads
 * setting=adb_enabled=0        # add/override a settings key to mask
 * prop=ro.boot.verifiedbootstate=green
 * del_setting=adb_wifi_enabled # remove a key
 * del_prop=ro.debuggable
 * </pre>
 */
final class MaskConfig {

    static final String CONF_PATH = "/data/local/tmp/usbdebug_hider.conf";
    static final long RELOAD_INTERVAL_MS = 2_000L;

    /** Default masked settings (Global) keys -> the fake value the app will read. */
    static final Map<String, String> DEFAULT_SETTINGS;

    /** Default masked system property keys -> the fake value the app will read. */
    static final Map<String, String> DEFAULT_PROPS;

    /**
     * Packages never masked even when uid >= 10000: the USB/MTP file transfer and media
     * backbone; masking them breaks MTP file transfer and media scanning.
     */
    static final Set<String> DEFAULT_EXCLUDE;

    static {
        Map<String, String> s = new LinkedHashMap<>();
        s.put("adb_enabled", "0");
        s.put("adb_wifi_enabled", "0");
        s.put("development_settings_enabled", "0");
        DEFAULT_SETTINGS = Collections.unmodifiableMap(s);

        Map<String, String> p = new LinkedHashMap<>();
        // USB gadget state: the app reads "MTP only, adbd not running"
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
        // debug/build flags
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

    // ── Active settings ─────────────────────────────────────────────────────
    volatile boolean enabled = true;
    volatile boolean maskSettings = true;
    volatile boolean maskQuery = true;
    volatile boolean maskProps = true;
    /** Whether the native (PLT/GOT) layer also masks properties; disabling it leaves only the Java layer. */
    volatile boolean maskPropsNative = true;
    volatile boolean allTargets = true;

    final Set<String> targets = new HashSet<>();
    final Set<String> excludes = new HashSet<>(DEFAULT_EXCLUDE);
    final Map<String, String> settings = new LinkedHashMap<>(DEFAULT_SETTINGS);
    final Map<String, String> props = new LinkedHashMap<>(DEFAULT_PROPS);

    String targetsRaw = "*";
    String source = "baked";
    /** Serialized props mask table (used by the native layer); empty string = the native layer masks nothing. */
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

    /** @return the fake value to return to the app for this settings key, null = do not mask. */
    String setting(String key) {
        return maskSettings ? settings.get(key) : null;
    }

    /** @return the fake value to return to the app for this system property, null = do not mask. */
    String prop(String key) {
        return maskProps ? props.get(key) : null;
    }

    // ── Loading ─────────────────────────────────────────────────────────────
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

    /** Serialize the props mask table as key=value\n... for the native layer. */
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
            // empty list -> treat as "all third-party apps" to stay conservative
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
            // on a broken config file keep the defaults; never let an exception escape from the hook side
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

    // ── Display ─────────────────────────────────────────────────────────────
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
