// ICommandService.aidl — 眼鏡端 App 的 Shizuku UserService 介面。
// 在 Shizuku server（adb 啟動 = shell uid 2000）的進程內執行 sh -c，
// 提供「shell 身份」給 App：跑 exploit（runCommandStream）與寫 payload（writeFile）。
// destroy 的 transaction code 16777114 為 Shizuku server 保留碼。
package com.rokid.rc.glasses.ghost;

import com.rokid.rc.glasses.ghost.ICommandCallback;

interface ICommandService {
    /** 流式執行指令：服務端另起 Thread 跑、逐行回呼、結束回 onExit。 */
    void runCommandStream(String cmd, ICommandCallback callback) = 1;

    /** 同步執行指令，回傳合併 stdout/stderr（小指令用，如 id）。 */
    String runCommand(String cmd) = 2;

    /** 以 shell 身份寫檔；executable=true 時 chmod 755。 */
    boolean writeFile(String path, in byte[] data, boolean executable) = 3;

    /** Shizuku server 保留碼：由 server 呼叫以銷毀本 UserService。 */
    void destroy() = 16777114;
}
