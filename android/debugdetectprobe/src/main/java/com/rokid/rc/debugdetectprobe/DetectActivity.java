package com.rokid.rc.debugdetectprobe;

import android.app.Activity;
import android.content.ContentResolver;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Debug;
import android.provider.Settings;
import android.util.Log;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * 對照組偵測器：以「一般第三方 App」身分，把 USB 偵錯/開發人員選項的偵測途徑全部讀一遍。
 *
 * <p>輸出格式固定為 {@code CHECK <向量> = <值>}，同時寫 logcat（tag {@code DebugDetectProbe}）
 * 與 {@code /sdcard/Android/data/com.rokid.rc.debugdetectprobe/files/detect_report.txt}，
 * 方便 adb 直接抓取並做遮蔽前後的 diff。
 */
public final class DetectActivity extends Activity {

    static final String TAG = "DebugDetectProbe";

    private static final String[] SETTING_KEYS = {
            "adb_enabled", "adb_wifi_enabled", "development_settings_enabled"};
    private static final String[] PROP_KEYS = {
            "sys.usb.config", "sys.usb.state", "persist.sys.usb.config", "init.svc.adbd",
            "service.adb.tcp.port", "service.adb.root", "ro.debuggable", "ro.adb.secure",
            "ro.build.type", "ro.build.tags", "sys.usb.ffs.ready"};

    private final List<String> mReport = new ArrayList<>();
    private TextView mBody;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);

        Button run = new Button(this);
        run.setText("重新偵測");
        run.setOnClickListener(v -> run());
        root.addView(run, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        ScrollView scroll = new ScrollView(this);
        mBody = new TextView(this);
        mBody.setTextSize(11f);
        mBody.setTypeface(Typeface.MONOSPACE);
        mBody.setTextColor(Color.DKGRAY);
        mBody.setTextIsSelectable(true);
        scroll.addView(mBody);
        root.addView(scroll, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        setContentView(root);
        run();
    }

    private void run() {
        mReport.clear();
        line("probe.package", getPackageName());
        line("probe.uid", String.valueOf(android.os.Process.myUid()));

        checkSettingsGetters();
        checkCursorQuery();
        checkPropertiesJava();
        checkPropertiesNative();
        checkMisc();

        dump();
    }

    // ── 途徑 1：Settings getter（絕大多數偵測器走這條） ─────────────────────
    private void checkSettingsGetters() {
        ContentResolver cr = getContentResolver();
        for (String k : SETTING_KEYS) {
            line("settings.Global.getInt:" + k, String.valueOf(Settings.Global.getInt(cr, k, -1)));
            line("settings.Global.getString:" + k, String.valueOf(Settings.Global.getString(cr, k)));
        }
    }

    // ── 途徑 2：直接 query settings provider 的 cursor ─────────────────────
    private void checkCursorQuery() {
        ContentResolver cr = getContentResolver();
        for (String k : SETTING_KEYS) {
            line("query.single:" + k, querySingle(cr, Settings.Global.CONTENT_URI, k));
            line("query.path:" + k,
                    queryPath(cr, Uri.parse("content://settings/global/" + k)));
        }
        line("query.fulltable", queryFullTable(cr));
        line("query.list_global", queryList(cr, "content://settings/global"));
    }

    private String querySingle(ContentResolver cr, Uri uri, String key) {
        Cursor c = null;
        try {
            c = cr.query(uri, null, "name=?", new String[]{key}, null);
            if (c == null) {
                return "(null cursor)";
            }
            if (!c.moveToFirst()) {
                return "(no row)";
            }
            int idx = c.getColumnIndex("value");
            return idx < 0 ? "(no value column)" : String.valueOf(c.getString(idx));
        } catch (Throwable t) {
            return "ERR:" + t;
        } finally {
            close(c);
        }
    }

    private String queryPath(ContentResolver cr, Uri uri) {
        Cursor c = null;
        try {
            c = cr.query(uri, null, null, null, null);
            if (c == null) {
                return "(null cursor)";
            }
            if (!c.moveToFirst()) {
                return "(no row)";
            }
            int idx = c.getColumnIndex("value");
            return idx < 0 ? "(no value column)" : String.valueOf(c.getString(idx));
        } catch (Throwable t) {
            return "ERR:" + t;
        } finally {
            close(c);
        }
    }

    private String queryFullTable(ContentResolver cr) {
        Cursor c = null;
        try {
            c = cr.query(Settings.Global.CONTENT_URI,
                    new String[]{"name", "value"}, null, null, null);
            if (c == null) {
                return "(null cursor)";
            }
            StringBuilder sb = new StringBuilder();
            int nameIdx = c.getColumnIndex("name");
            int valueIdx = c.getColumnIndex("value");
            while (c.moveToNext()) {
                String n = c.getString(nameIdx);
                if (Arrays.asList(SETTING_KEYS).contains(n)) {
                    sb.append(n).append('=').append(c.getString(valueIdx)).append(' ');
                }
            }
            return sb.length() == 0 ? "(keys not found)" : sb.toString().trim();
        } catch (Throwable t) {
            return "ERR:" + t;
        } finally {
            close(c);
        }
    }

    private String queryList(ContentResolver cr, String uri) {
        Cursor c = null;
        try {
            c = cr.query(Uri.parse(uri), null, null, null, null);
            return c == null ? "(null cursor)" : "rows=" + c.getCount();
        } catch (Throwable t) {
            return "ERR:" + t;
        } finally {
            close(c);
        }
    }

    // ── 途徑 3：Java SystemProperties（隱藏 API，只能反射 —— 偵測器實務上也這樣寫） ──
    private static java.lang.reflect.Method sSpGet;
    private static java.lang.reflect.Method sSpGetInt;
    private static java.lang.reflect.Method sSpGetBool;

    static {
        try {
            Class<?> sp = Class.forName("android.os.SystemProperties");
            sSpGet = sp.getMethod("get", String.class, String.class);
            sSpGetInt = sp.getMethod("getInt", String.class, int.class);
            sSpGetBool = sp.getMethod("getBoolean", String.class, boolean.class);
        } catch (Throwable ignored) {
            // 反射被 hidden-API 限制擋住時留 null，報告中會顯示原因
        }
    }

    private void checkPropertiesJava() {
        for (String k : PROP_KEYS) {
            line("prop.java:" + k, propJava(k));
        }
        line("prop.java.getInt:ro.debuggable", propJavaInt("ro.debuggable"));
        line("prop.java.getBoolean:ro.debuggable", propJavaBool("ro.debuggable"));
    }

    private static String propJava(String key) {
        if (sSpGet == null) {
            return "BLOCKED:hidden-api(no method)";
        }
        try {
            return String.valueOf(sSpGet.invoke(null, key, "(null)"));
        } catch (Throwable t) {
            return "BLOCKED:" + rootCause(t);
        }
    }

    private static String propJavaInt(String key) {
        if (sSpGetInt == null) {
            return "BLOCKED:hidden-api(no method)";
        }
        try {
            return String.valueOf(sSpGetInt.invoke(null, key, -1));
        } catch (Throwable t) {
            return "BLOCKED:" + rootCause(t);
        }
    }

    private static String propJavaBool(String key) {
        if (sSpGetBool == null) {
            return "BLOCKED:hidden-api(no method)";
        }
        try {
            return String.valueOf(sSpGetBool.invoke(null, key, false));
        } catch (Throwable t) {
            return "BLOCKED:" + rootCause(t);
        }
    }

    private static String rootCause(Throwable t) {
        Throwable c = t;
        while (c.getCause() != null) {
            c = c.getCause();
        }
        return c.getClass().getSimpleName();
    }

    // ── 途徑 4：native __system_property_get / /proc/net/tcp ───────────────
    private void checkPropertiesNative() {
        for (String k : PROP_KEYS) {
            line("prop.native:" + k, String.valueOf(NativeProps.getProp(k)));
        }
        line("net.listen_ports", String.valueOf(NativeProps.listenPorts()));
    }

    private void checkMisc() {
        line("build.TYPE", String.valueOf(Build.TYPE));
        line("build.TAGS", String.valueOf(Build.TAGS));
        line("build.FINGERPRINT", String.valueOf(Build.FINGERPRINT));
        line("debug.isDebuggerConnected", String.valueOf(Debug.isDebuggerConnected()));
        line("app.flags.debuggable",
                String.valueOf((getApplicationInfo().flags & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0));
        line("selinux.enforce", readFile("/sys/fs/selinux/enforce"));
    }

    private String readFile(String path) {
        try {
            byte[] buf = new byte[64];
            try (java.io.FileInputStream in = new java.io.FileInputStream(path)) {
                int n = in.read(buf);
                return n <= 0 ? "(empty)" : new String(buf, 0, n, StandardCharsets.UTF_8).trim();
            }
        } catch (Throwable t) {
            return "ERR:" + t.getClass().getSimpleName();
        }
    }

    // ── 輸出 ───────────────────────────────────────────────────────────────
    private void line(String name, String value) {
        String s = "CHECK " + name + " = " + value;
        mReport.add(s);
        Log.i(TAG, s);
    }

    private void dump() {
        StringBuilder sb = new StringBuilder();
        for (String s : mReport) {
            sb.append(s).append('\n');
        }
        String text = sb.toString();
        try {
            File dir = getExternalFilesDir(null);
            if (dir != null) {
                File out = new File(dir, "detect_report.txt");
                try (FileOutputStream fos = new FileOutputStream(out)) {
                    fos.write(text.getBytes(StandardCharsets.UTF_8));
                }
                Log.i(TAG, "CHECK report_file = " + out.getAbsolutePath());
            }
        } catch (Throwable t) {
            Log.i(TAG, "CHECK report_file = ERR:" + t);
        }
        mBody.setText(text);
    }

    private static void close(Cursor c) {
        try {
            if (c != null) {
                c.close();
            }
        } catch (Throwable ignored) {
            // ignore
        }
    }
}
