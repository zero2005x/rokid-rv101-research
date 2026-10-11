/*
 * adbreachprobe native probe (2026-09-20)
 *
 * 目的：繞過 Android Java framework 的 socket 封裝，直接做 raw syscall：
 *     socket(AF_UNIX, SOCK_STREAM, 0) + connect() + read()
 * 這樣拿到的 errno 是 kernel 的真實答案（DAC + SELinux 都會在這裡反映），
 * 不受 android.net.LocalSocket 的 @hide / API 行為差異影響。
 *
 * 只做連線與讀取；不 bind 任何系統路徑、不寫任何檔案（B 控制組只在 App 自己的
 * files dir 內建立 socket 檔案）。
 */
#include <jni.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/stat.h>

#define MAXREAD 24
#define RES_LEN (4 + MAXREAD)

/* 結果格式（int[RES_LEN]）：
 *   [0] errno       失敗時的 errno（0 = 沒失敗）
 *   [1] stage       1=socket 2=connect 3=read 4=bind 5=listen 6=accept 7=echo
 *   [2] nbytes      read 到的 byte 數（<0 代表 read 失敗，errno 在 [0]）
 *   [3] extra       控制組用：accept 是否成功 / 額外狀態
 *   [4..27]         read 到的前 24 bytes（不足補 -1）
 */
static jintArray
make_result(JNIEnv *env, int err, int stage, int nbytes, int extra,
            const unsigned char *buf)
{
    jint tmp[RES_LEN];
    int i;
    tmp[0] = err;
    tmp[1] = stage;
    tmp[2] = nbytes;
    tmp[3] = extra;
    for (i = 0; i < MAXREAD; i++) {
        tmp[4 + i] = (buf != NULL && i < nbytes) ? (jint) buf[i] : -1;
    }
    jintArray arr = (*env)->NewIntArray(env, RES_LEN);
    if (arr != NULL) {
        (*env)->SetIntArrayRegion(env, arr, 0, RES_LEN, tmp);
    }
    return arr;
}

static int
fill_sockaddr(struct sockaddr_un *sa, const char *path)
{
    size_t len = strlen(path);
    memset(sa, 0, sizeof(*sa));
    sa->sun_family = AF_UNIX;
    if (len >= sizeof(sa->sun_path)) {
        return -1;
    }
    memcpy(sa->sun_path, path, len + 1);
    return 0;
}

/*
 * Probe A（native）：connect 到 path，成功則在 timeoutMs 內最多讀 MAXREAD bytes。
 */
JNIEXPORT jintArray JNICALL
Java_com_rokid_rc_adbreachprobe_NativeProbe_connectUnixRead(JNIEnv *env, jclass clazz,
                                                            jstring jpath, jint timeoutMs)
{
    const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
    if (path == NULL) {
        return make_result(env, EINVAL, 0, -1, 0, NULL);
    }

    struct sockaddr_un sa;
    if (fill_sockaddr(&sa, path) != 0) {
        (*env)->ReleaseStringUTFChars(env, jpath, path);
        return make_result(env, ENAMETOOLONG, 0, -1, 0, NULL);
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        int e = errno;
        (*env)->ReleaseStringUTFChars(env, jpath, path);
        return make_result(env, e, 1, -1, 0, NULL);
    }

    if (connect(fd, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
        int e = errno;
        close(fd);
        (*env)->ReleaseStringUTFChars(env, jpath, path);
        return make_result(env, e, 2, -1, 0, NULL);
    }

    /* 連上了：設 3 秒接收逾時再讀 */
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    unsigned char buf[MAXREAD];
    memset(buf, 0, sizeof(buf));
    ssize_t n = read(fd, buf, MAXREAD);
    int e = (n < 0) ? errno : 0;
    close(fd);
    (*env)->ReleaseStringUTFChars(env, jpath, path);
    return make_result(env, e, 3, (int) n, 0, buf);
}

/*
 * Probe B（native 控制組）：在 path 建立自己的 UNIX socket、listen、
 * 由同一 process connect、accept、交換 1 byte。
 * 證明「本 process 有能力做完整的 AF_UNIX stream 連線」，所以 A 的失敗
 * 一定是目標 socket 的權限問題，而不是程式或 SELinux 全面禁止 AF_UNIX。
 */
JNIEXPORT jintArray JNICALL
Java_com_rokid_rc_adbreachprobe_NativeProbe_loopbackUnix(JNIEnv *env, jclass clazz,
                                                         jstring jpath, jint timeoutMs)
{
    const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
    if (path == NULL) {
        return make_result(env, EINVAL, 0, -1, 0, NULL);
    }

    struct sockaddr_un sa;
    if (fill_sockaddr(&sa, path) != 0) {
        (*env)->ReleaseStringUTFChars(env, jpath, path);
        return make_result(env, ENAMETOOLONG, 0, -1, 0, NULL);
    }

    unlink(path);

    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv < 0) {
        int e = errno;
        (*env)->ReleaseStringUTFChars(env, jpath, path);
        return make_result(env, e, 1, -1, 0, NULL);
    }

    if (bind(srv, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
        int e = errno;
        close(srv);
        (*env)->ReleaseStringUTFChars(env, jpath, path);
        return make_result(env, e, 4, -1, 0, NULL);
    }

    /* 記錄 socket 檔案的權限，當作 raw evidence */
    struct stat st;
    int mode = -1, st_uid = -1, st_gid = -1;
    if (stat(path, &st) == 0) {
        mode = (int) (st.st_mode & 07777);
        st_uid = (int) st.st_uid;
        st_gid = (int) st.st_gid;
    }

    if (listen(srv, 4) < 0) {
        int e = errno;
        close(srv);
        (*env)->ReleaseStringUTFChars(env, jpath, path);
        return make_result(env, e, 5, -1, mode, NULL);
    }

    int cli = socket(AF_UNIX, SOCK_STREAM, 0);
    if (cli < 0) {
        int e = errno;
        close(srv);
        (*env)->ReleaseStringUTFChars(env, jpath, path);
        return make_result(env, e, 1, -1, mode, NULL);
    }

    if (connect(cli, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
        int e = errno;
        close(cli);
        close(srv);
        (*env)->ReleaseStringUTFChars(env, jpath, path);
        return make_result(env, e, 2, -1, mode, NULL);
    }

    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    setsockopt(srv, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(cli, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int acc = accept(srv, NULL, NULL);
    if (acc < 0) {
        int e = errno;
        close(cli);
        close(srv);
        (*env)->ReleaseStringUTFChars(env, jpath, path);
        return make_result(env, e, 6, -1, mode, NULL);
    }

    unsigned char out = 'P';
    unsigned char in = 0;
    if (write(cli, &out, 1) != 1) {
        int e = errno;
        close(acc); close(cli); close(srv);
        (*env)->ReleaseStringUTFChars(env, jpath, path);
        return make_result(env, e, 7, -1, mode, NULL);
    }
    ssize_t n = read(acc, &in, 1);
    if (n != 1 || in != 'P') {
        int e = (n < 0) ? errno : 0;
        close(acc); close(cli); close(srv);
        (*env)->ReleaseStringUTFChars(env, jpath, path);
        return make_result(env, e, 7, (int) n, mode, NULL);
    }

    unsigned char echo[1];
    echo[0] = in;
    close(acc);
    close(cli);
    close(srv);
    (*env)->ReleaseStringUTFChars(env, jpath, path);
    return make_result(env, 0, 7, 1, mode, echo);
}

/*
 * 診斷：用 raw syscall 對 path 做 stat()/access()，證明 DAC 狀態。
 *
 * 注意：這是 DAC 專用。access(2) 完全不經過 SELinux，
 * 所以 access(W_OK) 的結果可以直接當成「DAC 是否允許寫入這個 socket inode」的證據。
 *
 * 結果格式（獨立於 make_result，避免截斷）：
 *   [0] stat errno          [1] access(W_OK) errno   [2] st_mode & 07777
 *   [3] W_OK (1/0)          [4] st_uid               [5] st_gid
 *   [6] st_mode & S_IFMT    [7..] = -1
 */
JNIEXPORT jintArray JNICALL
Java_com_rokid_rc_adbreachprobe_NativeProbe_statPath(JNIEnv *env, jclass clazz, jstring jpath)
{
    jint tmp[RES_LEN];
    int i;
    for (i = 0; i < RES_LEN; i++) tmp[i] = -1;

    const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
    if (path == NULL) {
        tmp[0] = EINVAL;
        jintArray arr0 = (*env)->NewIntArray(env, RES_LEN);
        if (arr0 != NULL) (*env)->SetIntArrayRegion(env, arr0, 0, RES_LEN, tmp);
        return arr0;
    }

    struct stat st;
    memset(&st, 0, sizeof(st));
    if (stat(path, &st) != 0) {
        tmp[0] = errno;
    } else {
        tmp[0] = 0;
        tmp[2] = (jint) (st.st_mode & 07777);
        tmp[4] = (jint) st.st_uid;
        tmp[5] = (jint) st.st_gid;
        tmp[6] = (jint) (st.st_mode & S_IFMT);
        if (access(path, W_OK) == 0) {
            tmp[1] = 0;
            tmp[3] = 1;
        } else {
            tmp[1] = errno;
            tmp[3] = 0;
        }
    }
    (*env)->ReleaseStringUTFChars(env, jpath, path);

    jintArray arr = (*env)->NewIntArray(env, RES_LEN);
    if (arr != NULL) {
        (*env)->SetIntArrayRegion(env, arr, 0, RES_LEN, tmp);
    }
    return arr;
}
