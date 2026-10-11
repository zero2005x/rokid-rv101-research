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
        // 經典 Xposed API（de.robv.android.xposed:api:82），compileOnly 用
        maven { url = uri("https://api.xposed.info/") }
    }
}
rootProject.name = "RokidRemoteCapture"
include(":shared", ":app-glasses", ":app-phone")
// 獨立安全研究探針模組（2026-09-20）：一般 App 能否 connect /dev/socket/adbd
include(":adbreachprobe")
// USB 偵錯防偵測：自建 LSPosed 模組（只對指定 App 遮蔽 adb/開發人員選項狀態）
include(":usbdebug-hider")
// 對照用偵測器：把 App 端所有「USB 偵錯偵測途徑」實際讀一遍，用來驗證遮蔽是否生效
include(":debugdetectprobe")
