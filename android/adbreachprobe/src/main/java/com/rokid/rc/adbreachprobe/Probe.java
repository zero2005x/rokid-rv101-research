package com.rokid.rc.adbreachprobe;

import android.content.Context;
import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.Process;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructStat;
import android.util.Log;

import java.io.File;
import java.io.FileDescriptor;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * adbreachprobe — the probe itself (2026-09-20).
 *
 * Everything runs inside the app's own process:
 *   uid      = the app's own uid (u0_aNNN)
 *   SELinux  = untrusted_app (read from /proc/self/attr/current, not guessed)
 *
 * Question: can an ordinary third-party app connect() to /dev/socket/adbd (adbd's UNIX socket)?
 *
 * The main probe goes through native raw syscalls (NativeProbe / libadbreachprobe.so), because on this ROM
 * android.net.LocalSocket throws a framework exception before connect() and never reaches the kernel.
 * The Java LocalSocket version is kept as well and logged step by step, as a comparison for framework behavior.
 *
 * Only connection and read-only operations: no binding to system paths, no writing any file, no property
 * changes, no creating or starting /data/local/tmp/root_daemon.sock, no touching adbd.
 */
public final class Probe {

    public static final String TAG = "ADBREACH";

    /** The real question: adbd's UNIX socket. */
    public static final String TARGET_ADBD = "/dev/socket/adbd";
    /** Secondary comparison: a socket that is 0777 with SELinux type = shell_data_file. */
    public static final String TARGET_SECONDARY = "/data/local/tmp/root_daemon.sock";

    private static final int TIMEOUT_MS = 3000;

    private static final Pattern ERRNO_RE = Pattern.compile("errno=(\\d+)");

    private Probe() {
    }

    // ---------------------------------------------------------------- result

    private static final class Res {
        boolean connected;
        int errno = Integer.MIN_VALUE;
        int stage = -1;
        int nbytes = -1;
        String hex = "";
        String detail = "";

        String errnoText() {
            if (errno == Integer.MIN_VALUE) return "?";
            return errno + " " + safeStrerror(errno);
        }

        String token() {
            if (connected) return "OK";
            return "ERR:" + (errno == Integer.MIN_VALUE ? "?" : errno);
        }
    }

    // ------------------------------------------------------------- utilities

    private static int errnoOf(Throwable t) {
        Throwable c = t;
        int depth = 0;
        while (c != null && depth < 8) {
            if (c instanceof ErrnoException) {
                return ((ErrnoException) c).errno;
            }
            String m = c.getMessage();
            if (m != null) {
                Matcher mm = ERRNO_RE.matcher(m);
                if (mm.find()) {
                    try {
                        return Integer.parseInt(mm.group(1));
                    } catch (NumberFormatException ignored) {
                        // fall through
                    }
                }
            }
            if (c.getCause() == c) break;
            c = c.getCause();
            depth++;
        }
        return Integer.MIN_VALUE;
    }

    private static String safeStrerror(int e) {
        try {
            String s = Os.strerror(e);
            return s == null ? "?" : s;
        } catch (Throwable t) {
            return "?";
        }
    }

    private static String hex(byte[] b, int n) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < n; i++) {
            sb.append(String.format("%02x", b[i] & 0xff));
        }
        return sb.toString();
    }

    private static String messageOf(Throwable t) {
        StringBuilder sb = new StringBuilder();
        Throwable c = t;
        int depth = 0;
        while (c != null && depth < 6) {
            if (sb.length() > 0) sb.append("  <-  ");
            sb.append(c.getClass().getName()).append(": ").append(c.getMessage());
            if (c.getCause() == c) break;
            c = c.getCause();
            depth++;
        }
        return sb.toString();
    }

    private static String readProc(String path, String keyPrefix) {
        try {
            byte[] buf = new byte[4096];
            FileDescriptor fd = Os.open(path, OsConstants.O_RDONLY, 0);
            try {
                int n = Os.read(fd, buf, 0, buf.length);
                if (n <= 0) return "<empty>";
                String s = new String(buf, 0, n, StandardCharsets.UTF_8);
                if (keyPrefix == null) {
                    int z = s.indexOf('\0');
                    if (z >= 0) s = s.substring(0, z);
                    return s.trim();
                }
                for (String line : s.split("\n")) {
                    if (line.startsWith(keyPrefix)) return line.trim();
                }
                return "<" + keyPrefix + " not found>";
            } finally {
                try {
                    Os.close(fd);
                } catch (Throwable ignored) {
                }
            }
        } catch (Throwable t) {
            return "<unreadable: " + t + ">";
        }
    }

    private static void closeQuietly(LocalSocket s) {
        if (s == null) return;
        try {
            s.close();
        } catch (Throwable ignored) {
        }
    }

    private static void closeQuietly(LocalServerSocket s) {
        if (s == null) return;
        try {
            s.close();
        } catch (Throwable ignored) {
        }
    }

    // ------------------------------------------------- native-backed probes

    /**
     * Probe A (the main question, native raw syscall): connect() to /dev/socket/adbd.
     */
    private static Res probeANative(List<String> lines) {
        Res r = new Res();
        int[] a = NativeProbe.connectUnixRead(TARGET_ADBD, TIMEOUT_MS);
        r.errno = a[0];
        r.stage = a[1];
        r.nbytes = a[2];

        if (r.stage >= 3) {
            // connect succeeded
            r.connected = true;
            if (r.nbytes > 0) {
                byte[] buf = new byte[r.nbytes];
                for (int i = 0; i < r.nbytes; i++) buf[i] = (byte) a[4 + i];
                r.hex = hex(buf, r.nbytes);
                r.detail = "bytes=" + r.nbytes + " hex=" + r.hex;
            } else if (r.errno == 11 /* EAGAIN */) {
                r.detail = "bytes=0 (connect succeeded; read timed out after "
                        + TIMEOUT_MS + "ms, errno=11 EAGAIN)";
            } else if (r.errno == 0) {
                r.detail = "bytes=0 (EOF)";
            } else {
                r.detail = "bytes=" + r.nbytes + " read errno=" + r.errno
                        + " " + safeStrerror(r.errno);
            }
        } else {
            r.detail = "stage=" + stageName(r.stage) + " (native raw syscall)";
        }
        lines.add("PROBE_A_NATIVE_RAW errno=" + r.errno + " stage=" + r.stage
                + " (" + stageName(r.stage) + ") nbytes=" + r.nbytes);
        return r;
    }

    private static String stageName(int stage) {
        switch (stage) {
            case 1: return "socket";
            case 2: return "connect";
            case 3: return "read";
            case 4: return "bind";
            case 5: return "listen";
            case 6: return "accept";
            case 7: return "echo";
            default: return "init";
        }
    }

    /**
     * Probe A comparison (Java framework): log step by step to see exactly where android.net.LocalSocket fails.
     */
    private static void probeAJava(List<String> lines) {
        LocalSocket s = null;
        try {
            s = new LocalSocket(LocalSocket.SOCKET_STREAM);
            lines.add("PROBE_A_JAVA step=new LocalSocket OK");
            s.setSoTimeout(TIMEOUT_MS);
            lines.add("PROBE_A_JAVA step=setSoTimeout OK fd="
                    + (s.getFileDescriptor() == null ? "null" : "non-null"));
            s.connect(new LocalSocketAddress(TARGET_ADBD,
                    LocalSocketAddress.Namespace.FILESYSTEM), TIMEOUT_MS);
            lines.add("PROBE_A_JAVA step=connect OK (CONNECTED!)");
            byte[] buf = new byte[24];
            int n = s.getInputStream().read(buf, 0, 24);
            lines.add("PROBE_A_JAVA read n=" + n
                    + (n > 0 ? " hex=" + hex(buf, n) : ""));
        } catch (Throwable t) {
            lines.add("PROBE_A_JAVA FAILED errno=" + errnoOf(t) + " " + messageOf(t));
        } finally {
            closeQuietly(s);
        }
    }

    /**
     * Probe B (control group, native): a full bind() + listen() + connect() + accept() + echo
     * inside the app's own files dir.
     */
    private static Res probeBNative(Context ctx, List<String> lines) {
        Res r = new Res();
        String path = new File(ctx.getFilesDir(), "self.sock").getAbsolutePath();
        lines.add("PROBE_B_PATH=" + path);

        int[] b = NativeProbe.loopbackUnix(path, 2000);
        r.errno = b[0];
        r.stage = b[1];
        r.nbytes = b[2];
        int mode = b[3];
        r.connected = (r.errno == 0 && r.stage == 7);
        r.detail = "via=B-native filesystem-path bind_mode="
                + (mode < 0 ? "?" : String.format("%04o", mode))
                + " stage=" + stageName(r.stage);
        if (!r.connected) {
            r.detail += " echo=FAILED errno=" + r.errno + " " + safeStrerror(r.errno);
        } else {
            r.detail += " echo=ok";
        }
        lines.add("PROBE_B_NATIVE_RAW errno=" + r.errno + " stage=" + r.stage
                + " (" + stageName(r.stage) + ") bind_mode="
                + (mode < 0 ? "?" : String.format("%04o", mode))
                + " nbytes=" + r.nbytes);
        return r;
    }

    /** Probe B comparison (Java framework), logged step by step. */
    private static void probeBJava(Context ctx, List<String> lines) {
        String path = new File(ctx.getFilesDir(), "self.sock").getAbsolutePath();
        try {
            new File(path).delete();
        } catch (Throwable ignored) {
        }
        LocalSocket server = null;
        LocalSocket client = null;
        try {
            server = new LocalSocket(LocalSocket.SOCKET_STREAM);
            lines.add("PROBE_B_JAVA step=new LocalSocket OK");
            server.bind(new LocalSocketAddress(path, LocalSocketAddress.Namespace.FILESYSTEM));
            FileDescriptor sfd = server.getFileDescriptor();
            lines.add("PROBE_B_JAVA step=bind OK fd=" + (sfd == null ? "null" : "non-null"));
            if (sfd == null) throw new java.io.IOException("bind() left fd null");
            Os.listen(sfd, 4);
            lines.add("PROBE_B_JAVA step=Os.listen OK");
            client = new LocalSocket(LocalSocket.SOCKET_STREAM);
            client.connect(new LocalSocketAddress(path, LocalSocketAddress.Namespace.FILESYSTEM),
                    TIMEOUT_MS);
            lines.add("PROBE_B_JAVA step=connect OK");
            OutputStream os = client.getOutputStream();
            lines.add("PROBE_B_JAVA step=getOutputStream OK");
            os.write('P');
            os.flush();
            lines.add("PROBE_B_JAVA step=write OK");
        } catch (Throwable t) {
            lines.add("PROBE_B_JAVA FAILED errno=" + errnoOf(t) + " " + messageOf(t));
        } finally {
            closeQuietly(client);
            closeQuietly(server);
        }
    }

    /** Probe C (secondary, native): a socket that is 0777 but usually has no listener. */
    private static Res probeCNative(List<String> lines) {
        Res r = new Res();
        int[] c = NativeProbe.connectUnixRead(TARGET_SECONDARY, TIMEOUT_MS);
        r.errno = c[0];
        r.stage = c[1];
        r.nbytes = c[2];
        r.connected = (r.stage >= 3);
        r.detail = "stage=" + stageName(r.stage);
        lines.add("PROBE_C_NATIVE_RAW errno=" + r.errno + " stage=" + r.stage
                + " (" + stageName(r.stage) + ") nbytes=" + r.nbytes);
        return r;
    }

    // ----------------------------------------------------------- diagnostics

    private static void diag(List<String> lines) {
        lines.add("NATIVE_LIB available=" + NativeProbe.AVAILABLE
                + (NativeProbe.AVAILABLE ? "" : " err=" + NativeProbe.loadError()));

        for (String p : new String[]{TARGET_ADBD, TARGET_SECONDARY}) {
            try {
                StructStat st = Os.stat(p);
                lines.add(String.format("SOCKSTAT java path=%s mode=%04o uid=%d gid=%d",
                        p, st.st_mode & 07777, st.st_uid, st.st_gid));
            } catch (Throwable t) {
                lines.add("SOCKSTAT java path=" + p + " ERR " + messageOf(t)
                        + " errno=" + errnoOf(t));
            }
        }
        if (NativeProbe.AVAILABLE) {
            int[] st = NativeProbe.statPath(TARGET_ADBD);
            lines.add("SOCKSTAT native(DAC) path=" + TARGET_ADBD
                    + " stat_errno=" + st[0] + " (" + safeStrerror(st[0]) + ")"
                    + " mode=" + String.format("%04o", st[2])
                    + " uid=" + st[4] + " gid=" + st[5]
                    + " access(W_OK)_errno=" + st[1] + " (" + safeStrerror(st[1]) + ")"
                    + " W_OK=" + st[3]);
            int[] st2 = NativeProbe.statPath(TARGET_SECONDARY);
            lines.add("SOCKSTAT native(DAC) path=" + TARGET_SECONDARY
                    + " stat_errno=" + st2[0] + " (" + safeStrerror(st2[0]) + ")"
                    + " mode=" + String.format("%04o", st2[2])
                    + " uid=" + st2[4] + " gid=" + st2[5]
                    + " access(W_OK)_errno=" + st2[1] + " (" + safeStrerror(st2[1]) + ")"
                    + " W_OK=" + st2[3]);
        }

        lines.add("SELF " + readProc("/proc/self/status", "Uid:")
                + " | " + readProc("/proc/self/status", "Gid:")
                + " | " + readProc("/proc/self/status", "Groups:"));
        try {
            lines.add("SELF os uid=" + Os.getuid() + " euid=" + Os.geteuid()
                    + " gid=" + Os.getgid() + " egid=" + Os.getegid());
        } catch (Throwable t) {
            lines.add("SELF os ERR " + t);
        }
    }

    // ------------------------------------------------------------------ main

    public static synchronized List<String> runAll(Context ctx, String where) {
        List<String> lines = new ArrayList<>();
        lines.add("=== adbreachprobe run " + new java.util.Date() + " ===");

        lines.add("UID=" + Process.myUid()
                + " CONTEXT=" + readProc("/proc/self/attr/current", null)
                + " PID=" + Process.myPid()
                + " WHERE=" + where
                + " APPID=" + ctx.getApplicationInfo().uid);

        diag(lines);

        // ---- Probe A (the main question; the native raw syscall is the authoritative answer)
        Res a = NativeProbe.AVAILABLE ? probeANative(lines) : new Res();
        probeAJava(lines);
        if (!NativeProbe.AVAILABLE) {
            a.errno = Integer.MIN_VALUE;
            a.detail = "native lib unavailable";
        }
        lines.add(a.connected
                ? "PROBE_A=OK " + a.detail
                : "PROBE_A=ERR errno=" + a.errnoText() + " [" + a.detail + "]");

        // ---- Probe B (control group)
        Res b = probeBNative(ctx, lines);
        probeBJava(ctx, lines);
        lines.add(b.connected
                ? "PROBE_B=OK " + b.detail
                : "PROBE_B=ERR errno=" + b.errnoText() + " [" + b.detail + "]");

        // ---- Probe C (secondary)
        Res c = probeCNative(lines);
        lines.add(c.connected
                ? "PROBE_C=OK " + c.detail
                : "PROBE_C=ERR errno=" + c.errnoText() + " [" + c.detail + "]");

        lines.add("PROBE_SUMMARY A=" + a.token() + " B=" + b.token()
                + " C=" + (c.connected ? "OK"
                        : String.valueOf(c.errno == Integer.MIN_VALUE ? "?" : c.errno)));

        for (String l : lines) {
            Log.i(TAG, l);
        }
        writeFile(ctx, lines);
        return lines;
    }

    private static void writeFile(Context ctx, List<String> lines) {
        StringBuilder sb = new StringBuilder();
        for (String l : lines) {
            sb.append(l).append('\n');
        }
        try {
            File ext = ctx.getExternalFilesDir(null);
            if (ext != null) {
                if (!ext.exists()) ext.mkdirs();
                File f = new File(ext, "probe_result.txt");
                FileOutputStream fos = new FileOutputStream(f, false);
                try {
                    fos.write(sb.toString().getBytes(StandardCharsets.UTF_8));
                    fos.flush();
                } finally {
                    fos.close();
                }
                Log.i(TAG, "WROTE " + f.getAbsolutePath());
            } else {
                Log.i(TAG, "WROTE-EXT-FAIL getExternalFilesDir(null)==null");
            }
        } catch (Throwable t) {
            Log.i(TAG, "WROTE-EXT-FAIL " + t);
        }
        try {
            File f = new File(ctx.getFilesDir(), "probe_result.txt");
            FileOutputStream fos = new FileOutputStream(f, false);
            try {
                fos.write(sb.toString().getBytes(StandardCharsets.UTF_8));
                fos.flush();
            } finally {
                fos.close();
            }
            Log.i(TAG, "WROTE " + f.getAbsolutePath());
        } catch (Throwable t) {
            Log.i(TAG, "WROTE-INT-FAIL " + t);
        }
    }
}
