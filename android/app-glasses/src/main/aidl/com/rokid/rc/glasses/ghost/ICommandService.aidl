// ICommandService.aidl — Shizuku UserService interface of the glasses-side app.
// Runs sh -c inside the Shizuku server process (started over adb = shell uid 2000),
// giving the app a "shell identity": run the exploit (runCommandStream) and write payloads (writeFile).
// destroy's transaction code 16777114 is a Shizuku server reserved code.
package com.rokid.rc.glasses.ghost;

import com.rokid.rc.glasses.ghost.ICommandCallback;

interface ICommandService {
    /** Run a command in streaming mode: the service starts a Thread, calls back line by line, and ends with onExit. */
    void runCommandStream(String cmd, ICommandCallback callback) = 1;

    /** Run a command synchronously, returning merged stdout/stderr (for small commands such as id). */
    String runCommand(String cmd) = 2;

    /** Write a file as shell; when executable=true, chmod 755. */
    boolean writeFile(String path, in byte[] data, boolean executable) = 3;

    /** Shizuku server reserved code: called by the server to destroy this UserService. */
    void destroy() = 16777114;
}
