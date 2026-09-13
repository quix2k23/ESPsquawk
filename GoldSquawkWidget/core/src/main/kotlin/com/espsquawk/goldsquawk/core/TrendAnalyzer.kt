package com.espsquawk.goldsquawk.core

/**
 * Turns a raw percent change into a bull/bear/neutral call, and decides whether a
 * fresh reading is worth pinging the user about.
 */
object TrendAnalyzer {

    const val DEFAULT_THRESHOLD_PERCENT = 0.15

    /**
     * @param changePercent the session change, e.g. 0.42 for +0.42%.
     * @param thresholdPercent minimum absolute move required to call a direction
     *   instead of NEUTRAL. Filters out noise on a quiet trading session.
     */
    fun classify(changePercent: Double, thresholdPercent: Double = DEFAULT_THRESHOLD_PERCENT): Trend {
        require(thresholdPercent >= 0) { "thresholdPercent must be >= 0, was $thresholdPercent" }
        return when {
            changePercent >= thresholdPercent -> Trend.BULL
            changePercent <= -thresholdPercent -> Trend.BEAR
            else -> Trend.NEUTRAL
        }
    }

    /**
     * True only when [current] is a genuine reversal from [previous] - not the first
     * ever reading, not a repeat of the same call, and not a fade into NEUTRAL.
     * This is what gates the notification ping, so a widget sitting in bull territory
     * doesn't re-notify on every poll.
     */
    fun didFlip(previous: Trend?, current: Trend): Boolean =
        previous != null && previous != current && current != Trend.NEUTRAL
}
