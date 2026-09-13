package com.espsquawk.goldsquawk

import android.content.Context
import android.util.Log
import androidx.work.CoroutineWorker
import androidx.work.ListenableWorker.Result
import androidx.work.WorkerParameters
import com.espsquawk.goldsquawk.core.TrendAnalyzer
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** Fetches a fresh quote, updates the widget, and pings the user on a bull/bear flip. */
class GoldPriceWorker(appContext: Context, params: WorkerParameters) : CoroutineWorker(appContext, params) {

    override suspend fun doWork(): Result = withContext(Dispatchers.IO) {
        val prefs = GoldPrefs(applicationContext)
        try {
            val quote = TradingViewClient.fetchGoldQuote()
            val newTrend = TrendAnalyzer.classify(quote.changePercent, prefs.alertThresholdPercent.toDouble())
            val previousTrend = prefs.lastTrend

            prefs.lastPrice = quote.price.toFloat()
            prefs.lastChangePercent = quote.changePercent.toFloat()
            prefs.lastUpdated = quote.fetchedAtEpochMillis
            prefs.lastTrend = newTrend

            if (TrendAnalyzer.didFlip(previousTrend, newTrend)) {
                NotificationHelper.pingTrendChange(applicationContext, newTrend, quote)
            }

            GoldWidgetProvider.updateAllWidgets(applicationContext)
            Result.success()
        } catch (e: Exception) {
            // Network/parse failures against an unofficial, undocumented endpoint are
            // expected occasionally - retry with backoff rather than crash the worker.
            Log.w(TAG, "Gold quote refresh failed, will retry", e)
            Result.retry()
        }
    }

    private companion object {
        const val TAG = "GoldPriceWorker"
    }
}
