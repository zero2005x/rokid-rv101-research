package com.rokid.rc.glasses

import android.app.Application
import rikka.shizuku.ShizukuProvider

class App : Application() {
    lateinit var graph: AppGraph
        private set

    override fun onCreate() {
        super.onCreate()
        // per-app 語言（SET_LOCALE 覆寫，預設跟隨系統）
        LocaleHelper.apply(this)
        // Shizuku：多進程支援（UserService 在獨立進程，需經 provider 接收 binder）。
        // UserService 進程內也會跑本 onCreate（makeApplication），此呼叫冪等且安全。
        runCatching { ShizukuProvider.enableMultiProcessSupport(true) }
        graph = AppGraph(this)

        // 路徑 B：戒指/觸控板控制改由 root 端常駐（getevent），但「讀取事件→觸發相機」的
        // 消費者活在 App 程序內；Rokid 會積極回收背景 App，因此這裡在**任何**程序啟動時
        // 恢復戒指控制（skipExisting 避免重放停用期間的舊手勢）。
        runCatching {
            if (graph.ringControl.enabled) graph.ringControl.ensureRunning(skipExisting = true)
        // P0：程序一啟動就開始 root daemon 看守（App 被系統殺掉後重啟也能自動復原）
        graph.rootKeeper.start()
        }
    }
}
