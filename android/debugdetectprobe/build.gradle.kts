plugins {
    id("com.android.application")
}

/*
 * debugdetectprobe — 對照組偵測器（2026-09-30）
 *
 * 目的：從「一般第三方 App」的視角，把 USB 偵錯相關的每一條偵測途徑實際讀一遍，
 *       用來驗證 :usbdebug-hider 的遮蔽是否真的生效（同一支 App，掛 scope 前後對照）。
 *
 * 與 adbreachprobe 同樣刻意保持最小：純 Java + 一支小 JNI、無依賴、無 layout XML。
 */
android {
    namespace = "com.rokid.rc.debugdetectprobe"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.rokid.rc.debugdetectprobe"
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

dependencies {
}
