pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
        // classic Xposed API (de.robv.android.xposed:api:82), used compileOnly
        maven { url = uri("https://api.xposed.info/") }
    }
}
rootProject.name = "RokidRemoteCapture"
include(":shared", ":app-glasses", ":app-phone")
// standalone security research probe module (2026-09-20): can an ordinary app connect to /dev/socket/adbd
include(":adbreachprobe")
// USB debugging anti-detection: a self-built LSPosed module (masks adb/developer-options state for the listed apps only)
include(":usbdebug-hider")
// control detector: actually reads every "USB debugging detection path" on the app side, to verify that masking took effect
include(":debugdetectprobe")
