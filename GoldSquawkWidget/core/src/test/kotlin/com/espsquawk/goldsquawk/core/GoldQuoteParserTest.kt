package com.espsquawk.goldsquawk.core

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.assertThrows

class GoldQuoteParserTest {

    private val sampleResponse = """{"data":[{"s":"TVC:GOLD","d":[3352.41,0.42]}]}"""

    @Test
    fun `parses a well-formed scanner response`() {
        val quote = GoldQuoteParser.parse(sampleResponse, symbol = "TVC:GOLD", nowEpochMillis = 1000L)

        assertEquals("TVC:GOLD", quote.symbol)
        assertEquals(3352.41, quote.price)
        assertEquals(0.42, quote.changePercent)
        assertEquals(1000L, quote.fetchedAtEpochMillis)
    }

    @Test
    fun `rejects an empty data array`() {
        assertThrows<GoldQuoteParser.GoldQuoteParseException> {
            GoldQuoteParser.parse("""{"data":[]}""", symbol = "TVC:GOLD", nowEpochMillis = 0L)
        }
    }

    @Test
    fun `rejects malformed json instead of an unchecked exception`() {
        assertThrows<GoldQuoteParser.GoldQuoteParseException> {
            GoldQuoteParser.parse("not json", symbol = "TVC:GOLD", nowEpochMillis = 0L)
        }
    }

    @Test
    fun `rejects a null value in a requested column`() {
        val withNull = """{"data":[{"s":"TVC:GOLD","d":[null,0.42]}]}"""
        assertThrows<GoldQuoteParser.GoldQuoteParseException> {
            GoldQuoteParser.parse(withNull, symbol = "TVC:GOLD", nowEpochMillis = 0L)
        }
    }

    @Test
    fun `request body encodes the requested symbol and both columns`() {
        val body = GoldQuoteParser.buildRequestBody("TVC:GOLD")
        assertTrue(body.contains("TVC:GOLD"))
        assertTrue(body.contains("\"close\""))
        assertTrue(body.contains("\"change\""))
    }
}
