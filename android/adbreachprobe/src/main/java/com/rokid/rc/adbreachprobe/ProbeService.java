package com.rokid.rc.adbreachprobe;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;

import java.util.List;

/**
 * 前景 Service：在 App 自己的 process / uid / SELinux domain 內再跑一次探針，
 * 證明結果不依賴 Activity 生命週期。
 */
public class ProbeService extends Service {

    private static final String CH_ID = "adbreachprobe";
    private static final int NOTIF_ID = 0xADB;

    @Override
    public void onCreate() {
        super.onCreate();
        ensureChannel();
        try {
            startForeground(NOTIF_ID, buildNotification());
        } catch (Throwable t) {
            Log.i(Probe.TAG, "START_FOREGROUND_FAIL " + t);
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        new Thread(new Runnable() {
            @Override
            public void run() {
                List<String> lines = Probe.runAll(ProbeService.this, "ProbeService");
                Log.i(Probe.TAG, "SERVICE_RUN_DONE lines=" + lines.size());
            }
        }, "adbreach-probe-svc").start();
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void ensureChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return;
        NotificationManager nm = getSystemService(NotificationManager.class);
        if (nm == null) return;
        if (nm.getNotificationChannel(CH_ID) == null) {
            NotificationChannel ch = new NotificationChannel(
                    CH_ID, "adbreachprobe", NotificationManager.IMPORTANCE_LOW);
            nm.createNotificationChannel(ch);
        }
    }

    private Notification buildNotification() {
        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            b = new Notification.Builder(this, CH_ID);
        } else {
            b = new Notification.Builder(this);
        }
        return b.setContentTitle("adbreachprobe")
                .setContentText("probing /dev/socket/adbd")
                .setSmallIcon(android.R.drawable.stat_notify_more)
                .setOngoing(true)
                .build();
    }
}
