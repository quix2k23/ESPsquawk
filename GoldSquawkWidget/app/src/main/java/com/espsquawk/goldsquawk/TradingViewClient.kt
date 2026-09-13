package com.espsquawk.goldsquawk

import com.espsquawk.goldsquawk.core.GoldQuote
import com.espsquawk.goldsquawk.core.GoldQuoteParser
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import java.io.IOException
import java.util.concurrent.TimeUnit

/**
 * Talks to TradingView's public scanner endpoint - the same unauthenticated JSON API
 * tradingview.com's own embeddable quote widgets call. There is no official public
 * TradingView API, so this endpoint isn't documented or guaranteed stable; if
 * TradingView changes it, [SYMBOL] and [GoldQuoteParser] are the two places to fix.
 *
 * To track a different instrument, swap [SYMBOL] for another TradingView ticker, e.g.
 * "OANDA:XAUUSD" (spot XAU/USD) or "COMEX:GC1!" (front-month gold futures).
 */
object TradingViewClient {

    const val SYMBOL = "TVC:GOLD"
    private const val SCAN_URL = "https://scanner.tradingview.com/cfd/scan"
    private val JSON_MEDIA_TYPE = "application/json; charset=utf-8".toMediaType()

    private val client = OkHttpClient.Builder()
        .connectTimeout(10, TimeUnit.SECONDS)
        .readTimeout(10, TimeUnit.SECONDS)
        .build()

    @Throws(IOException::class, GoldQuoteParser.GoldQuoteParseException::class)
    fun fetchGoldQuote(symbol: String = SYMBOL): GoldQuote {
        val request = Request.Builder()
            .url(SCAN_URL)
            .post(GoldQuoteParser.buildRequestBody(symbol).toRequestBody(JSON_MEDIA_TYPE))
            .header("User-Agent", "Mozilla/5.0 (compatible; GoldSquawkWidget/1.0)")
            .build()

        client.newCall(request).execute().use { response ->
            if (!response.isSuccessful) {
                throw IOException("TradingView returned HTTP ${response.code}")
            }
            val body = response.body?.string()
                ?: throw IOException("TradingView returned an empty response body")
            return GoldQuoteParser.parse(body, symbol, System.currentTimeMillis())
        }
    }
}
