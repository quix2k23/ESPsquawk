package com.espsquawk.goldsquawk

import android.Manifest
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.app.ActivityCompat
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import com.espsquawk.goldsquawk.core.GoldQuote
import com.espsquawk.goldsquawk.core.Trend
import java.util.Locale

object NotificationHelper {

    const val CHANNEL_ID = "gold_trend_alerts"
    private const val NOTIFICATION_ID = 1001

    fun createChannel(context: Context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
        val channel = NotificationChannel(
            CHANNEL_ID,
            context.getString(R.string.notif_channel_name),
            NotificationManager.IMPORTANCE_HIGH
        ).apply {
            description = context.getString(R.string.notif_channel_description)
            enableVibration(true)
        }
        context.getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
    }

    /** Pings the user that gold just flipped bullish/bearish. No-op for NEUTRAL or without permission. */
    fun pingTrendChange(context: Context, trend: Trend, quote: GoldQuote) {
        if (trend == Trend.NEUTRAL) return
        if (!hasNotificationPermission(context)) return

        val (iconRes, titleRes, colorRes) = when (trend) {
            Trend.BULL -> Triple(R.drawable.ic_trend_bull, R.string.notif_bull_title, R.color.bull_green)
            Trend.BEAR -> Triple(R.drawable.ic_trend_bear, R.string.notif_bear_title, R.color.bear_red)
            Trend.NEUTRAL -> return
        }

        val changeText = "${if (quote.changePercent >= 0) "+" else ""}${
            String.format(Locale.US, "%.2f", quote.changePercent)
        }%"
        val priceText = String.format(Locale.US, "%.2f", quote.price)
        val body = context.getString(R.string.notif_body, priceText, changeText)

        val notification = NotificationCompat.Builder(context, CHANNEL_ID)
            .setSmallIcon(iconRes)
            .setColor(ContextCompat.getColor(context, colorRes))
            .setContentTitle(context.getString(titleRes))
            .setContentText(body)
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setCategory(NotificationCompat.CATEGORY_STATUS)
            .setAutoCancel(true)
            .setDefaults(NotificationCompat.DEFAULT_SOUND or NotificationCompat.DEFAULT_VIBRATE)
            .build()

        NotificationManagerCompat.from(context).notify(NOTIFICATION_ID, notification)
    }

    private fun hasNotificationPermission(context: Context): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return true
        return ActivityCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) ==
            PackageManager.PERMISSION_GRANTED
    }
}
