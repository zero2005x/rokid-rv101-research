# Kali Linux Android SDK Build Notes

> 中文版：[sdk-setup.md](sdk-setup.md)

This machine (Kali / WSL2) is now set up with an Android SDK and toolchain capable of building this project, and a build has actually been compiled successfully.

## Installation locations

| Component | Path |
|------|------|
| JDK 17 (Temurin 17.0.20.1) | `~/jdk` |
| Android SDK root | `~/android-sdk` |
| cmdline-tools | `~/android-sdk/cmdline-tools/latest` |
| platform-tools (adb, etc.) | `~/android-sdk/platform-tools` |
| platform | `~/android-sdk/platforms/android-34` |
| build-tools | `~/android-sdk/build-tools/34.0.0` |
| Gradle 8.7 | `~/gradle/gradle-8.7` |

## Environment variables (already written to `~/.bashrc`)

```bash
export JAVA_HOME="$HOME/jdk"
export ANDROID_HOME="$HOME/android-sdk"
export ANDROID_SDK_ROOT="$HOME/android-sdk"
export PATH="$ANDROID_HOME/platform-tools:$ANDROID_HOME/cmdline-tools/latest/bin:$PATH"
```

## Building

```bash
cd android
./gradlew :app-glasses:assembleDebug :app-phone:assembleDebug
```

(The `gradlew` wrapper has already been generated; `~/gradle/gradle-8.7/bin/gradle` can also be used.)

> **2026-09 update (practical points)**
> - Another usable path: the workspace's bundled JDK (`tools/jdk/jdk-17.0.20.1+1`) plus `GRADLE_USER_HOME=<repo>/gradle_home`, without depending on `~/.bashrc`:
>   ```bash
>   cd android
>   JAVA_HOME=/home/kali/rokid_root/tools/jdk/jdk-17.0.20.1+1 \
>   GRADLE_USER_HOME=/home/kali/rokid_root/gradle_home ./gradlew :app-glasses:assembleDebug :app-phone:assembleDebug
>   ```
> - `android/gradle.properties` now includes `kotlin.compiler.execution.strategy=in-process`:
>   this avoids a Kotlin daemon startup failure when the process cannot write to
>   `~/.local/share/kotlin/daemon` (for example in a restricted sandbox).
> - The build needs to download the Shizuku dependencies (`dev.rikka.shizuku:api|provider:13.1.5`,
>   Maven Central) and AndroidX/CameraX; the first build requires network access.
> - A full build (both modules + lint) takes about 1–5 minutes in this environment; running it as a
>   background job is recommended to avoid timeouts.

## Output APKs

- `android/app-glasses/build/outputs/apk/debug/app-glasses-debug.apk`
- `android/app-phone/build/outputs/apk/debug/app-phone-debug.apk`

## Reproduction steps from scratch

```bash
# 1. JDK 17
curl -L -o jdk17.tar.gz "https://api.adoptium.net/v3/binary/latest/17/ga/linux/x64/jdk/hotspot/normal/eclipse"
tar -xzf jdk17.tar.gz -C ~/jdk --strip-components=1

# 2. cmdline-tools
curl -L -o cmdtools.zip "https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip"
unzip -q cmdtools.zip -d ~/android-sdk/cmdline-tools-tmp
mkdir -p ~/android-sdk/cmdline-tools/latest
mv ~/android-sdk/cmdline-tools-tmp/cmdline-tools/* ~/android-sdk/cmdline-tools/latest/

# 3. SDK components
export JAVA_HOME=~/jdk ANDROID_HOME=~/android-sdk ANDROID_SDK_ROOT=~/android-sdk
yes | ~/android-sdk/cmdline-tools/latest/bin/sdkmanager --sdk_root=$ANDROID_HOME --licenses
~/android-sdk/cmdline-tools/latest/bin/sdkmanager --sdk_root=$ANDROID_HOME \
  "platform-tools" "platforms;android-34" "build-tools;34.0.0"

# 4. Gradle 8.7
curl -L -o gradle.zip "https://services.gradle.org/distributions/gradle-8.7-bin.zip"
unzip -q gradle.zip -d ~/gradle
```
