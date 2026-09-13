package com.espsquawk.goldsquawk

import android.content.Context
import androidx.work.BackoffPolicy
import androidx.work.Constraints
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.ExistingWorkPolicy
import androidx.work.NetworkType
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.PeriodicWorkRequestBuilder
import androidx.work.WorkManager
import androidx.work.WorkRequest
import java.util.concurrent.TimeUnit

/**
 * Drives [GoldPriceWorker] through WorkManager. A plain AppWidget's own
 * `updatePeriodMillis` is capped at a 30-minute minimum by Android and isn't precise,
 * so refreshing on a shorter, user-chosen cadence goes through WorkManager instead;
 * `gold_widget_info.xml`'s `updatePeriodMillis` is only a battery-friendly fallback.
 */
object GoldWidgetScheduler {

    const val PERIODIC_WORK_NAME = "gold_price_periodic_refresh"
    private const val ONE_TIME_WORK_NAME = "gold_price_manual_refresh"
    private const val MIN_PERIODIC_MINUTES = 15L

    fun schedulePeriodic(context: Context, intervalMinutes: Long) {
        val safeInterval = intervalMinutes.coerceAtLeast(MIN_PERIODIC_MINUTES)
        val request = PeriodicWorkRequestBuilder<GoldPriceWorker>(safeInterval, TimeUnit.MINUTES)
            .setConstraints(networkConstraints())
            .setBackoffCriteria(BackoffPolicy.LINEAR, WorkRequest.MIN_BACKOFF_MILLIS, TimeUnit.MILLISECONDS)
            .build()

        WorkManager.getInstance(context).enqueueUniquePeriodicWork(
            PERIODIC_WORK_NAME,
            ExistingPeriodicWorkPolicy.UPDATE,
            request
        )
    }

    fun refreshNow(context: Context) {
        val request = OneTimeWorkRequestBuilder<GoldPriceWorker>()
            .setConstraints(networkConstraints())
            .build()

        WorkManager.getInstance(context).enqueueUniqueWork(
            ONE_TIME_WORK_NAME,
            ExistingWorkPolicy.REPLACE,
            request
        )
    }

    fun cancel(context: Context) {
        WorkManager.getInstance(context).cancelUniqueWork(PERIODIC_WORK_NAME)
    }

    private fun networkConstraints(): Constraints =
        Constraints.Builder().setRequiredNetworkType(NetworkType.CONNECTED).build()
}
