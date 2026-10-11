package com.ghostlock.app;

interface IShellService {
    /**
     * 在 shell uid=2000 上下文中执行命令，返回输出。
     * 由 Shizuku UserService 在服务端实现。
     */
    String exec(String command);

    /**
     * 显式销毁服务，事务码 16777114 由 Shizuku 服务端保留。
     */
    void destroy() = 16777114;
}
