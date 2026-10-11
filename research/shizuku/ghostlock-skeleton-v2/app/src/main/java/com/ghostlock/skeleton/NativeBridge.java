package com.ghostlock.skeleton;

/**
 * GhostLock exploit 本地代码桥接。
 * 
 * 调用链：
 *   UserService.exec("ghostlock <args>")  → shell 身份
 *   或
 *   NativeBridge.execute(args)             → app 身份 (Shizuku 已连接)
 */
public class NativeBridge {
    private static volatile boolean sLoaded;

    /** 同步加载 libghostlock.so，多次调用安全 */
    public static synchronized void ensureLoaded() {
        if (sLoaded) return;
        System.loadLibrary("ghostlock");
        sLoaded = true;
    }

    /**
     * 执行 GhostLock 本地载荷。
     * @param args 命令行参数数组，第一个元素通常是操作名
     *             ("load" | "info" | "trigger" | "spray" | "write" | "pwn")
     * @return native 输出的完整文本（stdout/stderr 合并），失败时返回错误描述
     */
    public static native String execute(String[] args);
}
