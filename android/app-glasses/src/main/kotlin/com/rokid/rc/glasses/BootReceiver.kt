package com.rokid.rc.glasses

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log

/** 開機自啟：BOOT_COMPLETED 後啟動 CaptureService（含藍牙 server 與自動提權）。 */
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
