// ICommandCallback.aidl — oneway streaming callback: UserService (shell identity) returns command output line by line.
// Modelled on the ICommandCallback pattern of ghostlock-aak-apk (rewritten independently; see the authorization notes in research/shizuku/ghostlock_apk_analysis.md).
package com.rokid.rc.glasses.ghost;

oneway interface ICommandCallback {
    void onLine(String line);
    void onExit(int exitCode);
}
