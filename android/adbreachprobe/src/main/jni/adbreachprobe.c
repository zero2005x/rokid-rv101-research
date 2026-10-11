/*
 * adbreachprobe native probe (2026-09-20)
 *
 * Purpose: bypass the Android Java framework's socket wrapper and issue raw syscalls directly:
 *     socket(AF_UNIX, SOCK_STREAM, 0) + connect() + read()
 * The errno obtained this way is the kernel's real answer (both DAC and SELinux show up here), unaffected by
 * android.net.LocalSocket's @hide / API behavior differences.
 *
 * Connection and reads only; no binding to any system path, no writing any file (the B control group only creates
 * a socket file inside the app's own files dir).
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

/* Result layout (int[RES_LEN]):
 *   [0] errno       errno on failure (0 = no failure)
 *   [1] stage       1=socket 2=connect 3=read 4=bind 5=listen 6=accept 7=echo
 *   [2] nbytes      number of bytes read (<0 means read failed, errno in [0])
 *   [3] extra       for the control group: whether accept succeeded / extra state
 *   [4..27]         first 24 bytes read (padded with -1)
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
 * Probe A (native): connect to path; on success read at most MAXREAD bytes within timeoutMs.
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

    /* connected: set a 3-second receive timeout before reading */
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
 * Probe B (native control group): create our own UNIX socket at path, listen, connect from the same process,
 * accept, and exchange 1 byte.
 * This proves "this process is capable of a complete AF_UNIX stream connection", so a failure of A must be a
 * permission problem of the target socket, not the program or SELinux banning AF_UNIX outright.
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

    /* record the socket file's permissions as raw evidence */
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
 * Diagnostics: stat()/access() on path through raw syscalls, to prove the DAC state.
 *
 * NOTE: this is DAC-only. access(2) does not go through SELinux at all,
 * so the access(W_OK) result can be taken directly as evidence of "whether DAC allows writing to this socket inode".
 *
 * Result layout (independent of make_result, to avoid truncation):
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
