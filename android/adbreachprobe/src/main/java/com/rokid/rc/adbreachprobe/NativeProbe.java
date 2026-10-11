package com.rokid.rc.adbreachprobe;

/**
 * JNI binding: raw socket syscall probe (see src/main/jni/adbreachprobe.c).
 *
 * Why it is needed: on this ROM Android's android.net.LocalSocket throws a framework-level exception before
 * connect() ("socket not created" / UnsupportedOperationException) and never reaches the kernel. The native
 * version does socket()/connect()/read() directly, so the errno it gets is the real DAC + SELinux answer.
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
     * connect() to path; on success read at most 24 bytes within timeoutMs.
     * @return int[28]: [0]=errno [1]=stage(1=socket,2=connect,3=read)
     *                  [2]=nbytes [3]=extra [4..27]=bytes read
     */
    public static native int[] connectUnixRead(String path, int timeoutMs);

    /**
     * Control group: create our own socket at path, listen, connect, accept, echo 1 byte.
     * @return int[28]: [0]=errno [1]=stage(1..7) [2]=nbytes [3]=mode after bind
     */
    public static native int[] loopbackUnix(String path, int timeoutMs);

    /**
     * raw stat()/access(W_OK)。
     * @return int[28]： [0]=access errno [1]=0 [2]=3 [3]=W_OK(0/1) [4..6]=mode,uid,gid
     */
    public static native int[] statPath(String path);
}
