package com.ghostlock.skeleton;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.io.StringWriter;

import android.util.Log;

public class UserService extends IUserService.Stub {

    private static final String TAG = "GhostLockService";

    @Override
    public void destroy() {
        Log.i(TAG, "destroy called, exiting");
        System.exit(0);
    }

    @Override
    public String exec(String command) {
        if (command == null || command.trim().isEmpty()) {
            return "empty command";
        }

        String trimmed = command.trim();

        // ghostlock 命令走 native 路径
        if (trimmed.startsWith("ghostlock ") || trimmed.equals("ghostlock")) {
            return execNative(trimmed);
        }

        // 其他命令走 shell
        return execShell(trimmed);
    }

    /**
     * 加载 libghostlock.so 并通过 JNI 调用 native 入口。
     * 注意：UserService 运行在 Shizuku server 进程，native 库搜索路径可能与 app 不同。
     * 整体 try-catch(Throwable) 防止 native crash 导致进程死掉，binder 断连。
     */
    private String execNative(String cmd) {
        try {
            NativeBridge.ensureLoaded();
        } catch (UnsatisfiedLinkError e) {
            return "✗ UserService 无法加载 libghostlock.so\n" +
                   "原因: " + e.getClass().getSimpleName() + "\n" +
                   (e.getMessage() != null ? e.getMessage() : "(无详细信息)") + "\n" +
                   "\nUserService 运行在 Shizuku 进程中，native 库搜索路径为:\n" +
                   System.getProperty("java.library.path", "(unknown)");
        }

        try {
            // 解析参数：跳过 "ghostlock" 前缀
            String[] parts = cmd.split("\\s+");
            String[] args;
            if (parts.length > 0 && "ghostlock".equals(parts[0])) {
                args = new String[parts.length - 1];
                System.arraycopy(parts, 1, args, 0, args.length);
            } else {
                args = parts;
            }

            if (args.length == 0) {
                args = new String[]{"info"};
            }

            return NativeBridge.execute(args);
        } catch (Throwable t) {
            // 捕获所有异常（包括 UnsatisfiedLinkError, SIGSEGV 转化的 Error 等）
            // 防止 UserService 进程崩溃导致 DeadObjectException
            StringWriter sw = new StringWriter();
            PrintWriter pw = new PrintWriter(sw);
            t.printStackTrace(pw);
            return "✗ NativeBridge.execute 异常\n" +
                   "类型: " + t.getClass().getName() + "\n" +
                   "消息: " + (t.getMessage() != null ? t.getMessage() : "(无)") + "\n" +
                   "\n堆栈:\n" + sw.toString();
        }
    }

    /**
     * 以 shell 身份执行系统命令。
     */
    private String execShell(String command) {
        try {
            java.lang.Process p = Runtime.getRuntime().exec(
                    new String[]{"sh", "-c", command});
            BufferedReader reader = new BufferedReader(
                    new InputStreamReader(p.getInputStream()));
            BufferedReader errReader = new BufferedReader(
                    new InputStreamReader(p.getErrorStream()));
            StringBuilder out = new StringBuilder();
            String line;
            while ((line = reader.readLine()) != null) {
                out.append(line).append("\n");
            }
            while ((line = errReader.readLine()) != null) {
                out.append("[stderr] ").append(line).append("\n");
            }
            p.waitFor();
            int exit = p.exitValue();
            if (exit != 0) {
                out.append("(exit: ").append(exit).append(")");
            }
            return out.toString().trim();
        } catch (Exception e) {
            return "shell error: " + e.getClass().getSimpleName() + "\n" +
                   (e.getMessage() != null ? e.getMessage() : "");
        }
    }
}
