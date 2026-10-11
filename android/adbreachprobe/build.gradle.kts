/*
 * adbreachprobe — 一次性安全研究探針（2026-09-20）
 *
 * 問題：一般第三方 App（uid = u0_aNNN、SELinux domain = untrusted_app、無 root）
 *       能否 connect() 到 glasses 的 adbd UNIX socket /dev/socket/adbd？
 *
 * 本模組刻意保持最小：純 Java、無任何依賴、無 layout XML、無 buildConfig。
 * 不得影響 app-glasses / app-phone / shared。
 */
plugins {
    id("com.android.application")
}

android {
    namespace = "com.rokid.rc.adbreachprobe"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.rokid.rc.adbreachprobe"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "1.0-probe"
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
}

// 無依賴：不使用 androidx / kotlin，避免任何離線解析問題。
dependencies {
}
