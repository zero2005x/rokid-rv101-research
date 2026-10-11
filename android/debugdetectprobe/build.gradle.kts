plugins {
    id("com.android.application")
}

/*
 * debugdetectprobe — control-group detector (2026-09-30)
 *
 * Purpose: from the viewpoint of an "ordinary third-party app", actually read every USB-debugging-related detection
 *       path, to verify that :usbdebug-hider's masking really works (the same app, compared before and after being
 *       added to scope).
 *
 * Like adbreachprobe it is deliberately minimal: pure Java + one small JNI, no dependencies, no layout XML.
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
