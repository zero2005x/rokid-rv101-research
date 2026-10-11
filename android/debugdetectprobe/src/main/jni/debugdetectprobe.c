/* debugdetectprobe.c — native detection paths (the half Java hooks cannot reach)
 *
 * 1. __system_property_get(): bypasses Java hooks on android.os.SystemProperties.
 * 2. Direct read of /proc/net/tcp{,6}: see whether adb-over-TCP (5555 = 0x15B3) is listening.
 */
#include <jni.h>
#include <stdio.h>
#include <string.h>
#include <sys/system_properties.h>

JNIEXPORT jstring JNICALL
Java_com_rokid_rc_debugdetectprobe_NativeProps_getProp(JNIEnv *env, jclass clazz, jstring jkey) {
    const char *key = (*env)->GetStringUTFChars(env, jkey, NULL);
    if (key == NULL) {
        return (*env)->NewStringUTF(env, "(oom)");
    }
    char value[PROP_VALUE_MAX];
    memset(value, 0, sizeof(value));
    int n = __system_property_get(key, value);
    (*env)->ReleaseStringUTFChars(env, jkey, key);
    if (n <= 0) {
        return (*env)->NewStringUTF(env, "(empty)");
    }
    return (*env)->NewStringUTF(env, value);
}

/* Scan /proc/net/tcp and tcp6, return "port1,port2,..." (hex local_port; adb defaults to 5555=0x15B3) */
JNIEXPORT jstring JNICALL
Java_com_rokid_rc_debugdetectprobe_NativeProps_listenPorts(JNIEnv *env, jclass clazz) {
    static const char *files[] = {"/proc/net/tcp", "/proc/net/tcp6"};
    char out[512];
    out[0] = '\0';
    size_t used = 0;

    for (int f = 0; f < 2; f++) {
        FILE *fp = fopen(files[f], "re");
        if (fp == NULL) {
            continue;
        }
        char line[512];
        int first = 1;
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (first) { /* header */ first = 0; continue; }
            unsigned int lport = 0, state = 0;
            /* sl local_address rem_address st ... */
            if (sscanf(line, " %*d: %*[0-9A-Fa-f]:%x %*[0-9A-Fa-f]:%*x %x", &lport, &state) != 2) {
                continue;
            }
            if (state != 0x0A) { /* TCP_LISTEN */
                continue;
            }
            if (used < sizeof(out) - 16) {
                used += (size_t) snprintf(out + used, sizeof(out) - used, "%s%s:%u",
                                          used ? "," : "", files[f], lport);
            }
        }
        fclose(fp);
    }
    if (out[0] == '\0') {
        return (*env)->NewStringUTF(env, "(no listener)");
    }
    return (*env)->NewStringUTF(env, out);
}
