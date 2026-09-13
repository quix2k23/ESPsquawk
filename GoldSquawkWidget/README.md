# Gold Squawk Widget

An Android home-screen widget that shows a live gold (XAU) price pulled from
TradingView and pings you with a notification when it flips bullish or
bearish.

## What it does

- Adds a home-screen widget showing the current gold price, percent change,
  and a bull/bear/neutral indicator.
- Polls TradingView in the background (default every 30 minutes, configurable
  down to 15) via WorkManager.
- Sends a notification only when the trend actually **flips** direction (bull
  → bear or bear → bull) — not on every poll, so it won't spam you while gold
  just sits in one regime.
- Tap the widget to force an immediate refresh.
- Long-press → widget settings (shown automatically when you add it) lets you
  pick the refresh interval and how big a move (in %) counts as a real trend
  versus noise.

## How it decides bull vs. bear

`TrendAnalyzer` (in the `:core` module) classifies TradingView's reported
percent change against a configurable threshold (default ±0.15%):

- `change% >= +threshold` → **BULL**
- `change% <= -threshold` → **BEAR**
- otherwise → **NEUTRAL**

A notification only fires on a genuine flip between BULL and BEAR (see
`TrendAnalyzer.didFlip`), never on the first-ever reading and never for a
fade into NEUTRAL.

## Where the data comes from (read this before relying on it)

TradingView has **no official public API**. This widget calls
`https://scanner.tradingview.com/cfd/scan`, the same unauthenticated JSON
endpoint tradingview.com's own embeddable quote widgets use internally
(`GoldQuoteParser.buildRequestBody` / `.parse` in `:core`). It defaults to
the `TVC:GOLD` symbol (TradingView's spot gold ticker). Because it's an
undocumented endpoint:

- It could change shape or start rejecting requests without notice.
- If that happens, `GoldQuoteParser` (request/response shape) and
  `TradingViewClient` (the actual HTTP call, in the `app` module) are the two
  places to fix.
- To track a different instrument, change `TradingViewClient.SYMBOL` — e.g.
  `"OANDA:XAUUSD"` for spot XAU/USD, or `"COMEX:GC1!"` for front-month gold
  futures.

## Project layout

```
GoldSquawkWidget/
  core/   - Plain-Kotlin module, no Android dependency: Trend, TrendAnalyzer,
            GoldQuote, GoldQuoteParser. Fully unit tested (see below).
  app/    - The Android app: widget provider, configure screen, WorkManager
            worker, TradingView HTTP client, notifications.
```

The TradingView parsing/decision logic lives in `:core` deliberately, so it
can be tested on a plain JVM without needing an emulator, a device, or the
Android SDK at all. The `app` module is a thin layer on top: it does the
actual network call, renders the widget's `RemoteViews`, schedules
WorkManager, and posts the notification.

## Building and running

Open the `GoldSquawkWidget/` folder in a recent Android Studio (Koala or
newer) and let it sync — it will offer to update the Gradle/AGP/Kotlin
versions if it has newer ones than the ones pinned here (AGP 8.5.2, Kotlin
1.9.24, Gradle 8.7), which is fine to accept.

From the command line:

```bash
# Pure-Kotlin logic module - no Android SDK needed:
./gradlew :core:test

# Full app - needs the Android SDK (ANDROID_HOME / local.properties):
./gradlew :app:assembleDebug
./gradlew installDebug   # with a device/emulator attached
```

Then, on the device: long-press the home screen → Widgets → **Gold Squawk**
→ drag it out. You'll land on the settings screen (refresh interval, alert
sensitivity, notification permission) before the widget is placed.

### What's actually been verified, and what hasn't

This was built in a sandboxed environment with **no Android SDK and no
network access to tradingview.com or Google's Maven repo** (the Android
Gradle Plugin itself couldn't even be downloaded there). So:

- ✅ `:core`'s 13 unit tests (`TrendAnalyzerTest`, `GoldQuoteParserTest`) were
  actually run there and pass — the bull/bear decision logic and the
  TradingView response parsing (against a realistic fixture response) are
  genuinely verified.
- ⚠️ The `app` module (widget rendering, the live network call, WorkManager
  scheduling, notifications) has **not** been compiled or run on a device or
  emulator. It's written carefully against current, stable Android APIs, but
  please do a real build + on-device test pass before relying on it -
  especially confirming the TradingView scanner endpoint still returns the
  shape `GoldQuoteParser` expects.

## Permissions

- `INTERNET` / `ACCESS_NETWORK_STATE` - to fetch the quote.
- `POST_NOTIFICATIONS` - to ping you on a trend flip (Android 13+ prompts for
  this on the widget's configure screen).
- `RECEIVE_BOOT_COMPLETED` - to re-arm the periodic refresh after a reboot.

There's no launcher activity by design - this app exists to host the widget,
not to be opened on its own.
