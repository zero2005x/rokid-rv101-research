package com.rokid.rc.glasses

import android.app.Application
import rikka.shizuku.ShizukuProvider

class App : Application() {
    lateinit var graph: AppGraph
        private set

    override fun onCreate() {
        super.onCreate()
        // per-app language (SET_LOCALE override; follows the system by default)
        LocaleHelper.apply(this)
        // Shizuku: multi-process support (UserService lives in a separate process and must receive its binder through the provider).
        // This onCreate (makeApplication) also runs inside the UserService process; the call is idempotent and safe.
        runCatching { ShizukuProvider.enableMultiProcessSupport(true) }
        graph = AppGraph(this)

        // Path B: ring/touchpad control is now resident on the root side (getevent), but the consumer that turns
        // "read event -> trigger camera" lives inside the app process; Rokid aggressively reaps background apps, so
        // ring control is restored on **any** process start (skipExisting avoids replaying gestures from while it was off).
        runCatching {
            if (graph.ringControl.enabled) graph.ringControl.ensureRunning(skipExisting = true)
        // P0: start watching the root daemon as soon as the process starts (so it recovers automatically after the system kills the app)
        graph.rootKeeper.start()
        }
    }
}
