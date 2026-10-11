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
 * Launcher Activity: as soon as it is am start-ed it runs all probes (on a background thread),
 * sending the results to both logcat (tag=ADBREACH) and the external files dir.
 * It also starts ProbeService so the probe keeps running after the Activity goes away.
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

        // run on a background thread (connect/read are blocking)
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

        // keep the probe resident as a Service too (best effort; failure does not affect the results above)
        try {
            startService(new Intent(this, ProbeService.class));
        } catch (Throwable t) {
            android.util.Log.i(Probe.TAG, "START_SERVICE_FAIL " + t);
        }
    }
}
