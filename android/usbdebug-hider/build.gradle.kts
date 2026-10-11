import java.security.MessageDigest
import java.util.Base64

plugins {
    id("com.android.application")
}

// ─────────────────────────────────────────────────────────────────────────────
// 建置期可把「目標 App 白名單」直接燒進 APK（不必依賴裝置端設定檔）：
//   ./gradlew :usbdebug-hider:assembleDebug -PmaskTargets=com.example.a,com.example.b
// 預設 "*" = 所有第三方 App（uid >= 10000）；系統/root/shell 一律保持真值。
// ─────────────────────────────────────────────────────────────────────────────
val maskTargets: String =
    (project.findProperty("maskTargets") as String?)?.trim().takeUnless { it.isNullOrEmpty() } ?: "*"

val bakedDir = layout.buildDirectory.dir("generated/baked").get().asFile
bakedDir.mkdirs()
File(bakedDir, "BakedDefaults.java").writeText(
    """
    package com.rokid.rc.usbdebughider;

    /** 由 build.gradle.kts 產生（-PmaskTargets=...），請勿手改。 */
    final class BakedDefaults {
        static final String TARGETS = "${maskTargets.replace("\\", "\\\\").replace("\"", "\\\"")}";

        private BakedDefaults() {
        }
    }
    """.trimIndent() + "\n"
)

// ─────────────────────────────────────────────────────────────────────────────
// native 層（libdebugmask.so）以 Base64 內嵌進 dex。
//
// 為什麼不靠 APK 路徑：Android 11+ 的套件可見性讓目標 App 查不到我們的包名
// （PackageManager.NameNotFoundException），而 Vector 用 in-memory dex 載入模組，
// /proc/self/maps 也看不到 APK 路徑。內嵌位元組則完全不依賴路徑探索：
// 注入後解到「目標 App 自己的 cacheDir」再 System.load()。
// 來源：src/main/jniLibs/arm64-v8a/libdebugmask.so（deploy/build_debugmask_so.sh 產生）
// ─────────────────────────────────────────────────────────────────────────────
run {
    val soFile = file("src/main/jniLibs/arm64-v8a/libdebugmask.so")
    val outFile = File(bakedDir, "EmbeddedSo.java")
    if (soFile.isFile) {
        val bytes = soFile.readBytes()
        val b64 = Base64.getEncoder().encodeToString(bytes)
        val hash = MessageDigest.getInstance("SHA-256")
            .digest(bytes).joinToString("") { "%02x".format(it) }.substring(0, 16)
        outFile.writeText(
            """
            package com.rokid.rc.usbdebughider;

            /** 由 build.gradle.kts 產生：內嵌的 native 層（PLT/GOT hook）。請勿手改。 */
            final class EmbeddedSo {
                static final String B64 = "$b64";
                static final String HASH = "$hash";
                static final int SIZE = ${bytes.size};

                private EmbeddedSo() {
                }
            }
            """.trimIndent() + "\n"
        )
    } else {
        outFile.writeText(
            """
            package com.rokid.rc.usbdebughider;

            /** 建置時找不到 libdebugmask.so → native 層自動停用（只剩 Java 層）。 */
            final class EmbeddedSo {
                static final String B64 = "";
                static final String HASH = "none";
                static final int SIZE = 0;

                private EmbeddedSo() {
                }
            }
            """.trimIndent() + "\n"
        )
    }
}

android {
    namespace = "com.rokid.rc.usbdebughider"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.rokid.rc.usbdebughider"
        minSdk = 26
        targetSdk = 33
        versionCode = 2
        versionName = "1.1"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    // 產生的 BakedDefaults.java
    sourceSets["main"].java.srcDir(bakedDir)
}

dependencies {
    // 經典 Xposed API：只供編譯，執行期由 LSPosed/Vector 框架提供
    compileOnly("de.robv.android.xposed:api:82")
}
