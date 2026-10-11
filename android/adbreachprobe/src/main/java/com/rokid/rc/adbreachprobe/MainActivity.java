package com.rokid.rc.adbreachprobe;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.util.TypedValue;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.List;

/**
 * 啟動器 Activity：一被 am start 就打全部探針（在背景 thread），
 * 並把結果同時送進 logcat(tag=ADBREACH) 與 external files dir。
 * 同時啟動 ProbeService，讓探針在 Activity 離開後仍持續存在。
 */
public class MainActivity extends Activity {

    private TextView out;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        int pad = (int) TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, 12, getResources().getDisplayMetrics());

        out = new TextView(this);
        out.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f);
        out.setTextColor(Color.GREEN);
        out.setBackgroundColor(Color.BLACK);
        out.setPadding(pad, pad, pad, pad);
        out.setText("adbreachprobe: running...");

        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(Color.BLACK);
        sv.addView(out);
        LinearLayout root = new LinearLayout(this);
        root.addView(sv);
        setContentView(root);

        // 背景 thread 執行（connect/read 為阻塞式）
        new Thread(new Runnable() {
            @Override
            public void run() {
                final List<String> lines = Probe.runAll(MainActivity.this, "MainActivity");
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        StringBuilder sb = new StringBuilder();
                        for (String l : lines) sb.append(l).append('\n');
                        out.setText(sb.toString());
                    }
                });
            }
        }, "adbreach-probe").start();

        // 讓探針也以 Service 形式常駐（best effort；失敗不影響上面的結果）
        try {
            startService(new Intent(this, ProbeService.class));
        } catch (Throwable t) {
            android.util.Log.i(Probe.TAG, "START_SERVICE_FAIL " + t);
        }
    }
}
