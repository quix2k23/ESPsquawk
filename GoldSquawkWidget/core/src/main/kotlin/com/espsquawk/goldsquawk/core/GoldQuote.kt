package com.espsquawk.goldsquawk.core

data class GoldQuote(
    val symbol: String,
    val price: Double,
    val changePercent: Double,
    val fetchedAtEpochMillis: Long
)
