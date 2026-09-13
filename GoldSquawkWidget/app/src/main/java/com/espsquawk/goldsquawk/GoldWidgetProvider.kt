package com.espsquawk.goldsquawk

import android.app.PendingIntent
import android.appwidget.AppWidgetManager
import android.appwidget.AppWidgetProvider
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.widget.RemoteViews
import androidx.core.content.ContextCompat
import com.espsquawk.goldsquawk.core.Trend
import java.text.NumberFormat
import java.text.SimpleDateFormat
import java.util.Locale

class GoldWidgetProvider : AppWidgetProvider() {

    override fun onUpdate(context: Context, appWidgetManager: AppWidgetManager, appWidgetIds: IntArray) {
        appWidgetIds.forEach { id -> updateWidget(context, appWidgetManager, id) }
        GoldWidgetScheduler.schedulePeriodic(context, GoldPrefs(context).refreshIntervalMinutes.toLong())
    }

    override fun onReceive(context: Context, intent: Intent) {
        super.onReceive(context, intent)
        if (intent.action == ACTION_REFRESH) {
            GoldWidgetScheduler.refreshNow(context)
        }
    }

    override fun onEnabled(context: Context) {
        GoldWidgetScheduler.schedulePeriodic(context, GoldPrefs(context).refreshIntervalMinutes.toLong())
    }

    override fun onDisabled(context: Context) {
        GoldWidgetScheduler.cancel(context)
    }

    companion object {
        const val ACTION_REFRESH = "com.espsquawk.goldsquawk.ACTION_REFRESH"

        fun updateAllWidgets(context: Context) {
            val manager = AppWidgetManager.getInstance(context)
            val ids = manager.getAppWidgetIds(ComponentName(context, GoldWidgetProvider::class.java))
            ids.forEach { id -> updateWidget(context, manager, id) }
        }

        fun updateWidget(context: Context, appWidgetManager: AppWidgetManager, appWidgetId: Int) {
            val prefs = GoldPrefs(context)
            val views = RemoteViews(context.packageName, R.layout.gold_widget)
            val hasData = prefs.lastUpdated > 0L

            views.setTextViewText(
                R.id.price_text,
                if (hasData) formatPrice(prefs.lastPrice) else context.getString(R.string.placeholder_price)
            )
            views.setTextViewText(
                R.id.updated_text,
                if (hasData) formatUpdated(context, prefs.lastUpdated) else context.getString(R.string.placeholder_updated)
            )
            views.setTextViewText(
                R.id.change_text,
                if (hasData) formatChange(prefs.lastChangePercent) else context.getString(R.string.placeholder_change)
            )

            val trend = if (hasData) prefs.lastTrend ?: Trend.NEUTRAL else null
            val (iconRes, colorRes, description) = when (trend) {
                Trend.BULL -> Triple(R.drawable.ic_trend_bull, R.color.bull_green, context.getString(R.string.trend_bull))
                Trend.BEAR -> Triple(R.drawable.ic_trend_bear, R.color.bear_red, context.getString(R.string.trend_bear))
                else -> Triple(R.drawable.ic_trend_neutral, R.color.neutral_gray, context.getString(R.string.trend_neutral))
            }
            views.setImageViewResource(R.id.trend_icon, iconRes)
            views.setTextColor(R.id.change_text, ContextCompat.getColor(context, colorRes))
            views.setContentDescription(R.id.trend_icon, description)

            val refreshIntent = Intent(context, GoldWidgetProvider::class.java).apply { action = ACTION_REFRESH }
            val pendingIntent = PendingIntent.getBroadcast(
                context,
                appWidgetId,
                refreshIntent,
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
            )
            views.setOnClickPendingIntent(R.id.widget_root, pendingIntent)

            appWidgetManager.updateAppWidget(appWidgetId, views)
        }

        private fun formatPrice(price: Float): String =
            NumberFormat.getCurrencyInstance(Locale.US).format(price.toDouble())

        private fun formatChange(changePercent: Float): String {
            val sign = if (changePercent >= 0) "+" else ""
            return "$sign${String.format(Locale.US, "%.2f", changePercent)}%"
        }

        private fun formatUpdated(context: Context, epochMillis: Long): String {
            val time = SimpleDateFormat("h:mm a", Locale.getDefault()).format(epochMillis)
            return context.getString(R.string.updated_at_format, time)
        }
    }
}
