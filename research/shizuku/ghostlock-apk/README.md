# GhostLock APK

独立 APK：CVE-2026-43499 GhostLock 内核提权 + Shizuku shell 身份执行。

## 前置条件

- 荣耀 AAK-AN00 / MagicOS 10 / Linux 6.6.89 / 4KB 页
- 已安装 Shizuku 并以 adb 方式启动（`adb shell sh /sdcard/Android/data/moe.shizuku.privileged.api/start.sh`）
- USB 调试已授权

## 工作原理

1. **Shizuku UserService**：APK 启动后向 Shizuku 申请 UserService，当 Shizuku 以 adb 方式启动时，UserService 以 **shell uid=2000** 身份运行，拥有 `/data/local/tmp/` 写权限。
2. **释放 exploit 二进制**：APK 内置的 `ghostlock`（CVE-2026-43499 GhostLock 提权 ELF）通过 Shizuku 通道写入 `/data/local/tmp/a/ghostlock`。
3. **执行提权**：以 shell 身份执行 ghostlock，触发内核漏洞 → 获取 root → su 落地。

## 构建

```bash
# 1. 下载 Shizuku API
curl -L -o app/libs/shizuku-api.aar \
  https://github.com/RikkaApps/Shizuku-API/releases/download/v13.5.4/shizuku-api-13.5.4.aar

# 2. 放入 ghostlock 二进制
# cp /path/to/ghostlock app/src/main/assets/ghostlock

# 3. 构建
./gradlew assembleRelease
```

## CI

GitHub Actions 自动构建：push 到 main 分支或手动触发 workflow_dispatch。

## 免责声明

本工具仅供安全研究使用。使用者自行承担所有风险。
