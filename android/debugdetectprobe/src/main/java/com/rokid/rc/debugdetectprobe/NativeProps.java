package com.rokid.rc.debugdetectprobe;

/** Native detection paths (see src/main/jni/debugdetectprobe.c). */
final class NativeProps {

    static {
        System.loadLibrary("debugdetectprobe");
    }

    private NativeProps() {
    }

    /** __system_property_get(): bypasses Java-layer SystemProperties hooks. */
    static native String getProp(String key);

    /** LISTEN port list from /proc/net/tcp{,6}. */
    static native String listenPorts();
}
