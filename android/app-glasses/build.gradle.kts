plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

/*
 * 提權「安全武裝」開關（2026-09-13）。
 *
 * 背景：GhostLock 每次嘗試約有 50% 機率以 kernel panic 收場 → 裝置在 2–5 分鐘後自動重開機
 * （見 reports/glasses_reboot_after_adb_unplug_2026-09-13.md）。原本的自動提權會在每次開機、
 * 每 5 分鐘無限重試，等於反覆去賭那個機率，可能形成開機迴圈。
 *
 * `-PsafeArming=true`（預設）→ 啟用四道保護：開機迴圈斷路器、每次開機重試上限＋指數退避、
 * 僅充電時自動武裝、開機後延遲（uptime > 150 s）。
 * `-PsafeArming=false` → 完全維持原行為（供 A/B 對照）。
 *
 * 兩個版本的 APK 會被複製到 dist/ 並以檔名與 versionName 區分。
 */
val safeArming: Boolean = (project.findProperty("safeArming") as String?)?.toBoolean() ?: true

android {
    namespace = "com.rokid.rc.glasses"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.rokid.rc.glasses"
        minSdk = 26
        targetSdk = 34
        // 兩個版本可互相覆蓋安裝（同 applicationId），但版本字串不同以便辨識
        versionCode = if (safeArming) 8 else 1
        versionName = if (safeArming) "1.0-safe.6" else "1.0"

        buildConfigField("boolean", "SAFE_ARMING", "$safeArming")
    }

    buildFeatures {
        // Shizuku UserService 的自訂 AIDL（ICommandService/ICommandCallback）
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

    // Shizuku：shell 身份執行通道（ShizukuGhostlockEngine / CommandService）
    // api=MIT、Shizuku 主體=Apache-2.0；provider 為 client 端接收 binder 所需。
    val shizukuVersion = "13.1.5"
    implementation("dev.rikka.shizuku:api:$shizukuVersion")
    implementation("dev.rikka.shizuku:provider:$shizukuVersion")

    val camerax = "1.3.4"
    implementation("androidx.camera:camera-core:$camerax")
    implementation("androidx.camera:camera-camera2:$camerax")
    implementation("androidx.camera:camera-lifecycle:$camerax")
    implementation("androidx.camera:camera-video:$camerax")
}
