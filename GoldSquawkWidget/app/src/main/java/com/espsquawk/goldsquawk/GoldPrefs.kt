package com.espsquawk.goldsquawk

import android.content.Context
import com.espsquawk.goldsquawk.core.Trend

/** Small SharedPreferences wrapper holding the widget's settings and last known quote. */
class GoldPrefs(context: Context) {

    private val prefs = context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    var refreshIntervalMinutes: Int
        get() = prefs.getInt(KEY_INTERVAL_MINUTES, DEFAULT_INTERVAL_MINUTES)
        set(value) = prefs.edit().putInt(KEY_INTERVAL_MINUTES, value).apply()

    var alertThresholdPercent: Float
        get() = prefs.getFloat(KEY_THRESHOLD_PERCENT, DEFAULT_THRESHOLD_PERCENT)
        set(value) = prefs.edit().putFloat(KEY_THRESHOLD_PERCENT, value).apply()

    var lastPrice: Float
        get() = prefs.getFloat(KEY_LAST_PRICE, 0f)
        set(value) = prefs.edit().putFloat(KEY_LAST_PRICE, value).apply()

    var lastChangePercent: Float
        get() = prefs.getFloat(KEY_LAST_CHANGE_PERCENT, 0f)
        set(value) = prefs.edit().putFloat(KEY_LAST_CHANGE_PERCENT, value).apply()

    var lastUpdated: Long
        get() = prefs.getLong(KEY_LAST_UPDATED, 0L)
        set(value) = prefs.edit().putLong(KEY_LAST_UPDATED, value).apply()

    var lastTrend: Trend?
        get() = prefs.getString(KEY_LAST_TREND, null)?.let { runCatching { Trend.valueOf(it) }.getOrNull() }
        set(value) = prefs.edit().putString(KEY_LAST_TREND, value?.name).apply()

    companion object {
        private const val PREFS_NAME = "gold_squawk_prefs"
        private const val KEY_INTERVAL_MINUTES = "refresh_interval_minutes"
        private const val KEY_THRESHOLD_PERCENT = "alert_threshold_percent"
        private const val KEY_LAST_PRICE = "last_price"
        private const val KEY_LAST_CHANGE_PERCENT = "last_change_percent"
        private const val KEY_LAST_UPDATED = "last_updated"
        private const val KEY_LAST_TREND = "last_trend"

        const val DEFAULT_INTERVAL_MINUTES = 30
        const val DEFAULT_THRESHOLD_PERCENT = 0.15f
    }
}
