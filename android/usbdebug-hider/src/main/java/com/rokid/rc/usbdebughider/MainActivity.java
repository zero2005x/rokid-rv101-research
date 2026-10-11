package com.rokid.rc.usbdebughider;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.provider.Settings;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * Module status page.
 *
 * <p>It deliberately does <b>not mask itself</b> (the module process always reads real values), so this
 * page shows the device's real state — comparing it with what a masked app reads is the most direct debug evidence.
 *
 * <p>The config can only be written by adb/root at {@value MaskConfig#CONF_PATH} (the app cannot write
 * /data/local/tmp), so this page only offers a view and usage instructions.
 */
public final class MainActivity extends Activity {

    private TextView mBody;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(16), dp(16), dp(16), dp(16));

        TextView title = new TextView(this);
        title.setText("DebugMask — USB 偵錯防偵測");
        title.setTextSize(18f);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        root.addView(title);

        Button refresh = new Button(this);
        refresh.setText("重新讀取");
        refresh.setOnClickListener(v -> render());
        root.addView(refresh, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        ScrollView scroll = new ScrollView(this);
        mBody = new TextView(this);
        mBody.setTextSize(12f);
        mBody.setTypeface(Typeface.MONOSPACE);
        mBody.setTextColor(Color.DKGRAY);
        mBody.setTextIsSelectable(true);
        scroll.addView(mBody, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        root.addView(scroll, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        TextView hint = new TextView(this);
        hint.setTextSize(11f);
        hint.setGravity(Gravity.START);
        hint.setText("設定：adb shell \"su -c 'cat > " + MaskConfig.CONF_PATH + "'\"\n"
                + "（本頁永遠顯示真值，因為模組不遮蔽自己）");
        root.addView(hint);

        setContentView(root);
        render();
    }

    @Override
    protected void onResume() {
        super.onResume();
        render();
    }

    private void render() {
        StringBuilder sb = new StringBuilder();

        sb.append("── 裝置真實狀態（不經遮蔽）──\n");
        sb.append("adb_enabled                  = ").append(global("adb_enabled")).append('\n');
        sb.append("adb_wifi_enabled             = ").append(global("adb_wifi_enabled")).append('\n');
        sb.append("development_settings_enabled = ").append(global("development_settings_enabled")).append('\n');
        sb.append("sys.usb.config               = ").append(prop("sys.usb.config")).append('\n');
        sb.append("sys.usb.state                = ").append(prop("sys.usb.state")).append('\n');
        sb.append("init.svc.adbd                = ").append(prop("init.svc.adbd")).append('\n');
        sb.append("ro.debuggable                = ").append(prop("ro.debuggable")).append('\n');
        sb.append("ro.build.type                = ").append(prop("ro.build.type")).append('\n');
        sb.append("uid                          = ").append(android.os.Process.myUid()).append('\n');

        sb.append("\n── 生效中的遮蔽設定 ──\n");
        sb.append(MaskConfig.get().describe());

        sb.append("\n── 用法 ──\n");
        sb.append("1. 在 LSPosed/Vector 啟用本模組並勾選目標 App（scope）\n");
        sb.append("2. 目標 App 重啟後即生效；改設定檔 2 秒內自動重載\n");
        sb.append("3. 白名單：targets=com.a,com.b；全部：targets=*\n");
        sb.append("4. 排除：targets=* !com.android.mtp\n");

        mBody.setText(sb.toString());
    }

    private String global(String key) {
        try {
            return String.valueOf(Settings.Global.getInt(getContentResolver(), key, -1));
        } catch (Throwable t) {
            return "ERR:" + t.getClass().getSimpleName();
        }
    }

    private String prop(String key) {
        // android.os.SystemProperties is a hidden API absent from the public SDK -> reflection is the only way
        try {
            Class<?> sp = Class.forName("android.os.SystemProperties");
            java.lang.reflect.Method get = sp.getMethod("get", String.class, String.class);
            return String.valueOf(get.invoke(null, key, "(null)"));
        } catch (Throwable t) {
            return "ERR:" + t.getClass().getSimpleName();
        }
    }

    private int dp(int v) {
        return Math.round(getResources().getDisplayMetrics().density * v);
    }
}
