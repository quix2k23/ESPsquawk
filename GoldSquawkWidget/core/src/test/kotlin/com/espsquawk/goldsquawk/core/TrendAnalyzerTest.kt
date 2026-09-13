package com.espsquawk.goldsquawk.core

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.assertThrows

class TrendAnalyzerTest {

    @Test
    fun `classifies a rise above threshold as bull`() {
        assertEquals(Trend.BULL, TrendAnalyzer.classify(changePercent = 0.5, thresholdPercent = 0.15))
    }

    @Test
    fun `classifies a drop beyond threshold as bear`() {
        assertEquals(Trend.BEAR, TrendAnalyzer.classify(changePercent = -0.5, thresholdPercent = 0.15))
    }

    @Test
    fun `classifies small moves inside the threshold as neutral`() {
        assertEquals(Trend.NEUTRAL, TrendAnalyzer.classify(changePercent = 0.05, thresholdPercent = 0.15))
        assertEquals(Trend.NEUTRAL, TrendAnalyzer.classify(changePercent = -0.05, thresholdPercent = 0.15))
    }

    @Test
    fun `treats the threshold boundary itself as a signal, not neutral`() {
        assertEquals(Trend.BULL, TrendAnalyzer.classify(changePercent = 0.15, thresholdPercent = 0.15))
        assertEquals(Trend.BEAR, TrendAnalyzer.classify(changePercent = -0.15, thresholdPercent = 0.15))
    }

    @Test
    fun `rejects a negative threshold`() {
        assertThrows<IllegalArgumentException> {
            TrendAnalyzer.classify(changePercent = 1.0, thresholdPercent = -0.1)
        }
    }

    @Test
    fun `flip detection ignores the first ever reading`() {
        assertFalse(TrendAnalyzer.didFlip(previous = null, current = Trend.BULL))
    }

    @Test
    fun `flip detection fires when direction reverses`() {
        assertTrue(TrendAnalyzer.didFlip(previous = Trend.BEAR, current = Trend.BULL))
        assertTrue(TrendAnalyzer.didFlip(previous = Trend.BULL, current = Trend.BEAR))
    }

    @Test
    fun `flip detection does not fire for the same trend or a fade to neutral`() {
        assertFalse(TrendAnalyzer.didFlip(previous = Trend.BULL, current = Trend.BULL))
        assertFalse(TrendAnalyzer.didFlip(previous = Trend.BULL, current = Trend.NEUTRAL))
    }
}
