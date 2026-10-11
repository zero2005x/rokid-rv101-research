plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

/*
 * Escalation "safe arming" switch (2026-09-13).
 *
 * Background: each GhostLock attempt ends in a kernel panic roughly 50% of the time → the device reboots by itself
 * after 2-5 minutes (see reports/glasses_reboot_after_adb_unplug_2026-09-13.md). The original auto-escalation retried
 * without limit on every boot and every 5 minutes, effectively gambling on that probability over and over, which can
 * produce a boot loop.
 *
 * `-PsafeArming=true` (default) → enables four protections: boot-loop breaker, per-boot retry cap + exponential
 * backoff, auto-arm only while charging, delay after boot (uptime > 150 s).
 * `-PsafeArming=false` → keeps the original behavior exactly (for A/B comparison).
 *
 * The two APK variants are copied to dist/ and distinguished by file name and versionName.
 */
val safeArming: Boolean = (project.findProperty("safeArming") as String?)?.toBoolean() ?: true

android {
    namespace = "com.rokid.rc.glasses"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.rokid.rc.glasses"
        minSdk = 26
        targetSdk = 34
        // the two variants can overwrite each other when installed (same applicationId), but the version string differs so they can be told apart
        versionCode = if (safeArming) 8 else 1
        versionName = if (safeArming) "1.0-safe.6" else "1.0"

        buildConfigField("boolean", "SAFE_ARMING", "$safeArming")
    }

    buildFeatures {
        // Shizuku UserService's custom AIDL (ICommandService/ICommandCallback)
        aidl = true
        buildConfig = true
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
    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    implementation(project(":shared"))

    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.constraintlayout:constraintlayout:2.1.4")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.4")
    implementation("androidx.lifecycle:lifecycle-service:2.8.4")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.8.1")

    // Shizuku: the shell-identity execution channel (ShizukuGhostlockEngine / CommandService)
    // api=MIT, Shizuku itself=Apache-2.0; the provider is what the client needs to receive the binder.
    val shizukuVersion = "13.1.5"
    implementation("dev.rikka.shizuku:api:$shizukuVersion")
    implementation("dev.rikka.shizuku:provider:$shizukuVersion")

    val camerax = "1.3.4"
    implementation("androidx.camera:camera-core:$camerax")
    implementation("androidx.camera:camera-camera2:$camerax")
    implementation("androidx.camera:camera-lifecycle:$camerax")
    implementation("androidx.camera:camera-video:$camerax")
}
