package com.ghostlock.app;

import android.util.Log;

import java.io.BufferedReader;
import java.io.InputStreamReader;

/**
 * Shizuku UserService 实现。
 * 当 Shizuku 以 adb 方式启动时，本服务以 shell uid=2000 身份运行，
 * 拥有 /data/local/tmp/ 写权限。
 */
public class ShellService extends IShellService.Stub {

    private static final String TAG = "GhostlockShell";

    @Override
    public String exec(String command) {
        Log.i(TAG, "exec: " + command);
        try {
            java.lang.Process p = Runtime.getRuntime().exec(new String[]{"sh", "-c", command});
            BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream()));
            BufferedReader e = new BufferedReader(new InputStreamReader(p.getErrorStream()));
            StringBuilder sb = new StringBuilder();
            String line;
            while ((line = r.readLine()) != null) {
                sb.append(line).append("\n");
            }
            while ((line = e.readLine()) != null) {
                sb.append("[E] ").append(line).append("\n");
            }
            p.waitFor();
            sb.append("exit=").append(p.exitValue());
            return sb.toString();
        } catch (Exception ex) {
            Log.e(TAG, "exec failed", ex);
            return "exception: " + ex.getMessage();
        }
    }

    @Override
    public void destroy() {
        Log.i(TAG, "destroy requested, pid=" + android.os.Process.myPid() + " uid=" + android.os.Process.myUid());
    }
}
