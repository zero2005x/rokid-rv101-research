package com.rokid.rc.debugdetectprobe;

/** native 偵測途徑（見 src/main/jni/debugdetectprobe.c）。 */
final class NativeProps {

    static {
        System.loadLibrary("debugdetectprobe");
    }

    private NativeProps() {
    }

    /** __system_property_get()：繞過 Java 層 SystemProperties hook。 */
    static native String getProp(String key);

    /** /proc/net/tcp{,6} 的 LISTEN 埠清單。 */
    static native String listenPorts();
}
