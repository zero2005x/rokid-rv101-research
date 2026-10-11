# Kali Linux Android SDK 建置紀錄

本機（Kali / WSL2）已準備好可建置本專案的 Android SDK 與工具鏈，並已實際編譯通過。

## 安裝位置

| 元件 | 路徑 |
|------|------|
| JDK 17（Temurin 17.0.20.1） | `~/jdk` |
| Android SDK 根目錄 | `~/android-sdk` |
| cmdline-tools | `~/android-sdk/cmdline-tools/latest` |
| platform-tools（adb 等） | `~/android-sdk/platform-tools` |
| platform | `~/android-sdk/platforms/android-34` |
| build-tools | `~/android-sdk/build-tools/34.0.0` |
| Gradle 8.7 | `~/gradle/gradle-8.7` |

## 環境變數（已寫入 `~/.bashrc`）

```bash
export JAVA_HOME="$HOME/jdk"
export ANDROID_HOME="$HOME/android-sdk"
export ANDROID_SDK_ROOT="$HOME/android-sdk"
export PATH="$ANDROID_HOME/platform-tools:$ANDROID_HOME/cmdline-tools/latest/bin:$PATH"
```

## 建置

```bash
cd android
./gradlew :app-glasses:assembleDebug :app-phone:assembleDebug
```

（`gradlew` wrapper 已產生；亦可用 `~/gradle/gradle-8.7/bin/gradle`。）

> **2026-09 更新（實務要點）**
> - 另一條可用路徑：工作區自帶 JDK（`tools/jdk/jdk-17.0.20.1+1`）＋ `GRADLE_USER_HOME=<repo>/gradle_home`，
>   不依賴 `~/.bashrc`：
>   ```bash
>   cd android
>   JAVA_HOME=/home/kali/rokid_root/tools/jdk/jdk-17.0.20.1+1 \
>   GRADLE_USER_HOME=/home/kali/rokid_root/gradle_home ./gradlew :app-glasses:assembleDebug :app-phone:assembleDebug
>   ```
> - `android/gradle.properties` 已加 `kotlin.compiler.execution.strategy=in-process`：
>   當行程無法寫入 `~/.local/share/kotlin/daemon`（如受限沙箱）時，避免 Kotlin daemon 啟動失敗。
> - 建置需下載 Shizuku 依賴（`dev.rikka.shizuku:api|provider:13.1.5`，Maven Central）與 AndroidX/CameraX；
>   首次建置需要網路。
> - 完整建置（兩模組＋lint）在此環境約 1–5 分鐘；建議以背景作業執行避免逾時。

## 產出 APK

- `android/app-glasses/build/outputs/apk/debug/app-glasses-debug.apk`
- `android/app-phone/build/outputs/apk/debug/app-phone-debug.apk`

## 從零重現步驟

```bash
# 1. JDK 17
curl -L -o jdk17.tar.gz "https://api.adoptium.net/v3/binary/latest/17/ga/linux/x64/jdk/hotspot/normal/eclipse"
tar -xzf jdk17.tar.gz -C ~/jdk --strip-components=1

# 2. cmdline-tools
curl -L -o cmdtools.zip "https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip"
unzip -q cmdtools.zip -d ~/android-sdk/cmdline-tools-tmp
mkdir -p ~/android-sdk/cmdline-tools/latest
mv ~/android-sdk/cmdline-tools-tmp/cmdline-tools/* ~/android-sdk/cmdline-tools/latest/

# 3. SDK 元件
export JAVA_HOME=~/jdk ANDROID_HOME=~/android-sdk ANDROID_SDK_ROOT=~/android-sdk
yes | ~/android-sdk/cmdline-tools/latest/bin/sdkmanager --sdk_root=$ANDROID_HOME --licenses
~/android-sdk/cmdline-tools/latest/bin/sdkmanager --sdk_root=$ANDROID_HOME \
  "platform-tools" "platforms;android-34" "build-tools;34.0.0"

# 4. Gradle 8.7
curl -L -o gradle.zip "https://services.gradle.org/distributions/gradle-8.7-bin.zip"
unzip -q gradle.zip -d ~/gradle
```
