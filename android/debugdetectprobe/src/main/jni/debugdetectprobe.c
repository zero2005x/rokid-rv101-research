/* debugdetectprobe.c — native 偵測途徑（Java hook 打不到的那一半）
 *
 * 1. __system_property_get()：繞過 android.os.SystemProperties 的 Java hook。
 * 2. /proc/net/tcp{,6} 直讀：看 adb-over-TCP（5555 = 0x15B3）是否監聽中。
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

/* 掃 /proc/net/tcp 與 tcp6，回傳 "port1,port2,..."（十六進位 local_port 中 adb 預設 5555=0x15B3） */
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
            if (first) { /* 表頭 */ first = 0; continue; }
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
