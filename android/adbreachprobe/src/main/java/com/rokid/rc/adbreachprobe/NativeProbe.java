package com.rokid.rc.adbreachprobe;

/**
 * JNI 綁定：raw socket syscall 探針（見 src/main/jni/adbreachprobe.c）。
 *
 * 為什麼需要：Android 的 android.net.LocalSocket 在這個 ROM 上於 connect() 前
 * 就丟出 framework 層例外（"socket not created" / UnsupportedOperationException），
 * 根本沒碰到 kernel。native 版本直接做 socket()/connect()/read()，
 * 拿到的 errno 才是 DAC + SELinux 的真實答案。
 */
public final class NativeProbe {

    public static final boolean AVAILABLE;
    private static final String LOAD_ERROR;

    static {
        boolean ok;
        String err = "";
        try {
            System.loadLibrary("adbreachprobe");
            ok = true;
        } catch (Throwable t) {
            ok = false;
            err = t.getClass().getName() + ": " + t.getMessage();
        }
        AVAILABLE = ok;
        LOAD_ERROR = err;
    }

    private NativeProbe() {
    }

    public static String loadError() {
        return LOAD_ERROR;
    }

    /**
     * connect() 到 path，成功則在 timeoutMs 內最多讀 24 bytes。
     * @return int[28]： [0]=errno [1]=stage(1=socket,2=connect,3=read)
     *                   [2]=nbytes [3]=extra [4..27]=讀到的 bytes
     */
    public static native int[] connectUnixRead(String path, int timeoutMs);

    /**
     * 控制組：在 path 建立自己的 socket、listen、connect、accept、echo 1 byte。
     * @return int[28]： [0]=errno [1]=stage(1..7) [2]=nbytes [3]=bind 後的 mode
     */
    public static native int[] loopbackUnix(String path, int timeoutMs);

    /**
     * raw stat()/access(W_OK)。
     * @return int[28]： [0]=access errno [1]=0 [2]=3 [3]=W_OK(0/1) [4..6]=mode,uid,gid
     */
    public static native int[] statPath(String path);
}
