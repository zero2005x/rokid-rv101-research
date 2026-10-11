package com.rokid.rc.glasses

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log

/** Boot autostart: start CaptureService after BOOT_COMPLETED (includes the Bluetooth server and auto escalation). */
class BootReceiver : BroadcastReceiver() {

    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != Intent.ACTION_BOOT_COMPLETED) return
        Log.i(TAG, "BOOT_COMPLETED，啟動 CaptureService")
        try {
            context.startForegroundService(Intent(context, CaptureService::class.java))
        } catch (e: Exception) {
            Log.e(TAG, "startForegroundService failed", e)
        }
    }

    companion object {
        private const val TAG = "BootReceiver"
    }
}
