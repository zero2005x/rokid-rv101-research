// ICommandCallback.aidl — oneway 流式回呼：UserService（shell 身份）逐行回傳指令輸出。
// 參考 ghostlock-aak-apk 的 ICommandCallback 模式（自行重寫，見 research/shizuku/ghostlock_apk_analysis.md 授權注意事項）。
package com.rokid.rc.glasses.ghost;

oneway interface ICommandCallback {
    void onLine(String line);
    void onExit(int exitCode);
}
