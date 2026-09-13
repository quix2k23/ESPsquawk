package com.espsquawk.goldsquawk

import android.Manifest
import android.appwidget.AppWidgetManager
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.widget.SeekBar
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.espsquawk.goldsquawk.databinding.ActivityWidgetConfigureBinding

/**
 * Shown automatically by Android right after the widget is dropped on the home
 * screen (registered via `android:configure` in gold_widget_info.xml). Doubles as
 * the natural moment to ask for the POST_NOTIFICATIONS permission the bull/bear
 * ping needs on Android 13+.
 */
class GoldWidgetConfigureActivity : AppCompatActivity() {

    private lateinit var binding: ActivityWidgetConfigureBinding
    private var appWidgetId = AppWidgetManager.INVALID_APPWIDGET_ID

    private val requestNotificationPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { /* widget still works either way */ }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setResult(RESULT_CANCELED)

        binding = ActivityWidgetConfigureBinding.inflate(layoutInflater)
        setContentView(binding.root)

        appWidgetId = intent?.extras?.getInt(
            AppWidgetManager.EXTRA_APPWIDGET_ID, AppWidgetManager.INVALID_APPWIDGET_ID
        ) ?: AppWidgetManager.INVALID_APPWIDGET_ID

        if (appWidgetId == AppWidgetManager.INVALID_APPWIDGET_ID) {
            finish()
            return
        }

        val prefs = GoldPrefs(this)
        bindIntervalChoice(prefs.refreshIntervalMinutes)
        bindThresholdSeekBar(prefs.alertThresholdPercent)
        maybeRequestNotificationPermission()

        binding.addWidgetButton.setOnClickListener {
            prefs.refreshIntervalMinutes = selectedIntervalMinutes()
            prefs.alertThresholdPercent = selectedThresholdPercent()

            GoldWidgetScheduler.schedulePeriodic(this, prefs.refreshIntervalMinutes.toLong())
            GoldWidgetScheduler.refreshNow(this)

            setResult(RESULT_OK, Intent().putExtra(AppWidgetManager.EXTRA_APPWIDGET_ID, appWidgetId))
            finish()
        }
    }

    private fun bindIntervalChoice(currentMinutes: Int) {
        val checkedId = when (currentMinutes) {
            15 -> R.id.interval_15
            60 -> R.id.interval_60
            else -> R.id.interval_30
        }
        binding.intervalGroup.check(checkedId)
    }

    private fun selectedIntervalMinutes(): Int = when (binding.intervalGroup.checkedRadioButtonId) {
        R.id.interval_15 -> 15
        R.id.interval_60 -> 60
        else -> 30
    }

    private fun bindThresholdSeekBar(currentThreshold: Float) {
        val progress = ((currentThreshold / THRESHOLD_STEP) - 1).toInt().coerceIn(0, MAX_THRESHOLD_PROGRESS)
        binding.thresholdSeekbar.max = MAX_THRESHOLD_PROGRESS
        binding.thresholdSeekbar.progress = progress
        updateThresholdLabel(progress)
        binding.thresholdSeekbar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) =
                updateThresholdLabel(progress)
            override fun onStartTrackingTouch(seekBar: SeekBar?) = Unit
            override fun onStopTrackingTouch(seekBar: SeekBar?) = Unit
        })
    }

    private fun updateThresholdLabel(progress: Int) {
        binding.thresholdValue.text = getString(R.string.threshold_percent_format, thresholdPercentFor(progress))
    }

    private fun thresholdPercentFor(progress: Int): Float = (progress + 1) * THRESHOLD_STEP

    private fun selectedThresholdPercent(): Float = thresholdPercentFor(binding.thresholdSeekbar.progress)

    private fun maybeRequestNotificationPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS)
            != PackageManager.PERMISSION_GRANTED
        ) {
            requestNotificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
        }
    }

    private companion object {
        const val THRESHOLD_STEP = 0.05f
        const val MAX_THRESHOLD_PROGRESS = 19
    }
}
