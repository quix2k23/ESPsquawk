package com.espsquawk.goldsquawk.core

import org.json.JSONArray
import org.json.JSONObject

/**
 * Builds requests for, and parses responses from, TradingView's scanner endpoint
 * (https://scanner.tradingview.com/cfd/scan) - the unauthenticated JSON API that
 * tradingview.com's own embeddable quote widgets call under the hood.
 *
 * There is no official public TradingView API. This is reverse-engineered from the
 * site's own network traffic, so the request/response shape could change without
 * notice - [COLUMNS] is the one place that couples the two together, so a schema
 * change only needs a fix here.
 */
object GoldQuoteParser {

    private val COLUMNS = listOf("close", "change")

    class GoldQuoteParseException(message: String, cause: Throwable? = null) : Exception(message, cause)

    fun buildRequestBody(symbol: String): String =
        JSONObject().apply {
            put("symbols", JSONObject().apply {
                put("tickers", JSONArray().put(symbol))
                put("query", JSONObject().put("types", JSONArray()))
            })
            put("columns", JSONArray(COLUMNS))
        }.toString()

    fun parse(rawJson: String, symbol: String, nowEpochMillis: Long): GoldQuote {
        try {
            val row = JSONObject(rawJson)
                .getJSONArray("data")
                .takeIf { it.length() > 0 }
                ?.getJSONObject(0)
                ?.getJSONArray("d")
                ?: throw GoldQuoteParseException("TradingView returned no data rows for symbol '$symbol'")

            return GoldQuote(
                symbol = symbol,
                price = row.doubleAt(COLUMNS.indexOf("close")),
                changePercent = row.doubleAt(COLUMNS.indexOf("change")),
                fetchedAtEpochMillis = nowEpochMillis
            )
        } catch (e: GoldQuoteParseException) {
            throw e
        } catch (e: Exception) {
            throw GoldQuoteParseException("Failed to parse TradingView response: ${e.message}", e)
        }
    }

    private fun JSONArray.doubleAt(index: Int): Double {
        if (isNull(index)) throw GoldQuoteParseException("Expected a numeric value at column index $index")
        return getDouble(index)
    }
}
