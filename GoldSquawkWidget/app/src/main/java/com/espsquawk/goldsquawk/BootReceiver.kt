package com.espsquawk.goldsquawk

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

/** WorkManager's own schedule doesn't survive a reboot on its own on every OEM skin; re-arm it explicitly. */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != Intent.ACTION_BOOT_COMPLETED) return
        GoldWidgetScheduler.schedulePeriodic(context, GoldPrefs(context).refreshIntervalMinutes.toLong())
    }
}
