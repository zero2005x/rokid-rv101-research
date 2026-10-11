/*
 * adbreachprobe — one-shot security research probe (2026-09-20)
 *
 * Question: can an ordinary third-party app (uid = u0_aNNN, SELinux domain = untrusted_app, no root)
 *       connect() to the glasses' adbd UNIX socket /dev/socket/adbd?
 *
 * This module is deliberately minimal: pure Java, no dependencies, no layout XML, no buildConfig.
 * It must not affect app-glasses / app-phone / shared.
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

// No dependencies: no androidx / kotlin, to avoid any offline resolution problem.
dependencies {
}
