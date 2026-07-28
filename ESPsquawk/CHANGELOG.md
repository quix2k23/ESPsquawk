## [changlog]

# Changelog

All notable changes to the ESPsquawk Remote ID firmware from today's development session.

## [1.5.2]
### Fixed
- Added `autocomplete="new-password"` to the WiFi password and Security-panel password fields in both webUIs, quieting browser autofill warnings.
- Added a matching favicon `<link>` to `mobile.html` (config.html already had one).
- Added a `/favicon.ico` handler on the device's web server that returns `204 No Content`, eliminating the browser's automatic favicon 404 from the console.

## [1.5.1]
### Changed
- Moved "Restart Device" / "Factory Reset" buttons off the Status/Dashboard page and onto the System page in both webUIs (config.html's Dashboard had a duplicate pair alongside the ones already on System — removed the duplicate).
- Added a "Firmware" stat card to the Status/Dashboard page in both webUIs, showing the live firmware version.

## [1.5.0]
### Added
- Boot confirmation chime: a warm rising major-chord tune (C5→E5→G5→C6, last note held longer as a resolving landing note) played once at boot via `buzzer_play_boot_tone()`, independent of GPS state. Deliberately lower-pitched and slower than the existing GPS-lock chirp so the two are easy to tell apart by ear.
- Refactored `buzzer.c` so both tunes (boot chime, GPS-lock chirp) share one generic playback engine.
- Updated buzzer description text in both webUIs to mention the new boot chime.

## [1.4.1]
### Fixed
- **LED red/green channel isolation bug**: the TX-activity flash was hardcoded to `set_rgb(255, 255)`, forcing *both* red and green channels on during every transmission regardless of state — this let red bleed into the green "GPS lock" indicator (and would let green bleed into a red "no fix" indicator). Fixed so the flash now boosts only the current state's own color.
- Changed the `NO_GPS` LED color from amber (a red+green mix) to pure red, so it no longer has any green component while blinking.

## [1.4.0]
### Added
- Persistent "Reconnect to WiFi" notice shown after saving Access Point settings in both webUIs, triggered when a WiFi password was entered or the SSID differs from the default `ESP-RID` — warns that the browser's connection is about to change and gives reconnect instructions.
- `showNotice()` helper in mobile.html: a single-button (OK-only) variant of the existing confirm modal, styled neutrally rather than as a warning.
### Fixed
- A leftover top-relocated button from the "disable web server" warning could linger into a *later*, unrelated modal (showInfo/showError/showConfirm/showReport) in config.html. Fixed by moving the stray-button cleanup into the shared `setModalButtons()` function so every modal display path is covered.

## [1.3.0]
### Added
- UART Monitor activity dot: a small indicator next to the UART Monitor header in both webUIs that flashes green briefly whenever the firmware's monotonic byte counter increases between polls — distinguishes a genuinely stalled UART link from a GPS simply repeating the same NMEA sentence (which a plain "did the displayed text change" check couldn't tell apart).
- New `uart_mirror_get_total()` function and `"total"` field in `/api/uart_mirror`'s JSON response.

## [1.2.2]
### Changed
- Moved the red "I Understand, Disable" button to the absolute top of the "disable web server" warning box (above the icon/title/text), with Cancel remaining at the bottom.
- Web Server toggle label now dynamically reads "Web Server Enabled" / "Web Server Disabled" based on its checked state.
- Web Server toggle now uses a visually distinct filled/accent-colored style when enabled (the established `.chk` pill pattern in config.html; a new accent-colored row style in mobile.html), driven by CSS `:has()` so the state can never visually drift out of sync.

## [1.2.1]
### Changed
- Web Server toggle: the confirm-button in the "disable web server" warning now appears above Cancel (later superseded by the 1.2.2 "absolute top of box" placement).
- The first `/api/status` poll now fires immediately on page load instead of waiting for `setInterval`'s first 2-second tick, so the splash screen's firmware-version badge and connection status appear right away.

## [1.2.0]
### Added
- The "Web Server Enabled" toggle now actually works — previously it was inert (saved to NVS but never read to gate anything). `esp_rid_set_config()` now detects the change and calls `web_config_init()`/`web_config_stop()` accordingly.
- New serial CLI command `webserver [on|off]` — the guaranteed way back in if the web server is disabled from the webUI (since that would otherwise make the only interface that could re-enable it unreachable).
- Mandatory "I Understand, Disable" warning modal shown before the web server can be disabled, explaining the consequences and the two ways to recover (serial CLI or factory reset).
### Fixed
- `web_config_stop()` defers the actual `httpd_stop()` via a one-shot timer rather than calling it synchronously, avoiding a deadlock risk when triggered from within the server's own `/api/config` request handler.

## [1.1.0]
### Added
- Firmware version is now single-sourced from the `ESP_RID_VERSION` macro and read live via `/api/status`'s `fw_version` field — removed all hardcoded version-string duplicates from both webUIs (header chip, footer, Sensors tab, System tab all now show `—` until the device responds).
- Firmware version now displayed on the loading/splash screen the moment it starts, in bold black text sized for readability on small mobile screens.
- Split the single merged BLE 5.0 advertising function into two independently toggleable channels: BLE 5.0 LR (Coded PHY) and BLE 5.0 1M (ordinary 1M PHY), each with its own on/off switch in both webUIs.
- Raised the broadcast-rate cap from 5Hz to the true 10Hz ceiling (the main loop's 100ms tick is the genuine hardware limit), with new defaults of 5Hz across WiFi Beacon/BLE4/BLE5.
- Default ID Type changed to CAA Registration and default UA Type changed to Helicopter/Multirotor in both webUIs and firmware defaults.
- Repurposed the status LED's blue channel into an independent "Compliance/Armed" indicator — lit solid once identity is set, GPS has a fix, and the device is actively transmitting — decoupled from the red/green status color mix.
- Added inline notes beside the Red Pin / Green Pin labels describing their function, and relabeled the third pin "Blue LED — Compliance Pin".
- Browser connection tolerance: both `getCfg()` and the periodic status poll now retry up to 3 times before declaring the connection lost, instead of flashing "Disconnected" on a single dropped packet.
- Added a "Security" tab to mobile.html — it existed on the desktop UI but had never been ported over (password-protect the panel, session timeout).

## Earlier session work (pre-versioning)
- Added a buzzer feature: synthesized ascending chirp confirming satellite lock + home GPS position saved, with a configurable GPIO pin (default 9) and pin-conflict warnings.
- Added a "Home Position" dashboard box (green/locked or red/no-home) showing the device-captured home coordinates.
- Built a true separate mobile webUI (`mobile.html`) rather than a responsive-only desktop page, including Console/log viewer, Presets, Compliance checklist, and Sensors detail view, plus an NMEA/UART mini-monitor and a spatially-offset factory-reset confirmation to prevent accidental double-taps.
- Fixed compliance checklist requirements to actually differ per region (FAA/EASA/CAA/ENAC) instead of showing identical requirements everywhere.
- Added ID-format-aware validation and inline examples beneath the UAS ID fields on the Identity page, in both webUIs.
- Added a verbose transmission-logging toggle, and fixed two real bugs uncovered while diagnosing a "not broadcasting" report: an `options` field truncated from `uint16_t` to `uint8_t` (silently discarding the verbose-logging bit) and a blank-UAS-ID overwrite bug from stale page saves.
- Fixed BLE transmit power UI to match the true +9dBm hardware maximum (was allowing selection of unachievable higher values).
- Converted the WiFi power field to a dropdown of the 11 actually-achievable discrete power levels instead of continuous free-form input.
- Reweighted the BLE4 legacy advertising schedule and raised default broadcast rates for better visibility to modern Android scanning behavior; added a third BLE5 advertising instance on ordinary 1M PHY for phones without Coded PHY support.

---


Fixed
GPS data appeared as garbage unless Protocol was manually forced to NMEA. Boot-time UART init used a hardcoded 115200 baud rate instead of the configured value (commonly 9600), producing misaligned garbage from the very first byte until any config save happened to trigger a reinit with the correct baud.

Config values (UART pins, etc.) could silently revert to garbage under lock contention. esp_rid_get_config() could fail to acquire its lock and return without populating the caller's struct at all, leaving it as uninitialized stack memory; callers then saved that garbage back to NVS.

Config saves could silently fail to actually apply. esp_rid_set_config() held its lock while calling code that tried to re-acquire the same lock, guaranteeing a timeout and silently falling back to defaults on every UART reconfigure.

Crash when parsing NMEA sentences with empty fields (common without a GPS fix, e.g. $GPGGA,...,,,,,*6B). Field tokenization silently skipped empty fields and left unpopulated array slots uninitialized, leading to atof()/strtod() being called on garbage pointers.

CLI input over USB-Serial/JTAG fragmented into individual characters, spamming the prompt. Rewrote input handling to accumulate a line across repeated non-blocking reads instead of treating each read as all-or-nothing.

Boot-time PROTO_DETECT deadlock introduced while fixing the baud issue above — caught and resolved before release.

LEDC: GPIO not usable warning firing on every unrelated config save (LED settings were being reconfigured unconditionally).

False "Connection Lost" popup on page load; stray /docs/shared.css 404.

Accessibility: form labels not associated with their fields.

Added

Live UART data monitor panel (Flight Controller tab) — mirrors raw incoming GPS/FC data for wiring/protocol verification.

Reserved-GPIO safety check: UART pins can no longer be set to USB, flash/PSRAM, or boot-strapping pins (this previously caused the device to lose its own USB connection).
Boot-time validation that detects and permanently corrects invalid/corrupted UART pin values in NVS.

CLI uart_pins command.

### Security
- **`/ota` endpoint accepted unsigned firmware uploads over the network.** Any device on the AP could flash arbitrary firmware at lock level 0/1 with no authentication. Now requires a mandatory SHA-256 integrity header at every lock level, and a valid signature (verified against configured public keys) at `lock_level >= 1`.
- **Unterminated config strings could leak adjacent memory into BLE/WiFi broadcasts.** `strncpy()` calls in the config API didn't guarantee null-termination when input matched the buffer's max length. Fixed across 7 call sites.
- **`options` config bitmask silently dropped flags beyond the 8th bit.** Cast to `uint8_t` while 9 option flags are defined; corrected to `uint16_t`.
- **PSA crypto subsystem was used before being initialized.** Added the missing `psa_crypto_init()` call at startup.
- **Ed25519 key-type validation was disabled** (`if (0)` stub). Restored with a real bit-length check.
- **Out-of-bounds buffer write/read in BLE advertising could broadcast adjacent memory contents over the air.** Rewrote the advertising payload builders to validate buffer capacity before every write.

### Fixed — Boot, build, and BLE advertising
- `rid_ota_check_and_run()` signature mismatch between declaration and definition (build failure).
- Crash on boot: `esp_ble_tx_power_set()` called before the BLE controller/host were initialized.
- BLE initialization silently failed every boot due to requesting `ESP_BT_MODE_BTDM` on ESP32-S3, which has no Classic Bluetooth radio.
- Legacy BLE advertising sent truncated/corrupted multi-message packs instead of one valid message per cycle.
- BLE long-range advertising truncated payloads to a legacy-sized buffer instead of using real extended-advertising capacity.
- BLE TX power setting was ignored; a fixed power level was always applied regardless of configured value.
- **BLE dual-instance advertising rejected by the controller with `Invalid Param (0x12)` on both instances.** Root cause: no random address was ever assigned to either advertising set despite `own_addr_type` being set to `BLE_ADDR_TYPE_RANDOM`. Added the missing `esp_ble_gap_ext_adv_set_rand_addr()` call (confirmed against Espressif's own official multi-adv example), plus explicit distinct SIDs per instance.
- Partition table exceeded the configured flash size (needed 4MB, sdkconfig specified 2MB).
- `CMakeLists.txt`: missing source file reference, a duplicate source entry, and an invalid `-lbt` linker flag.
- Build regressions caught before release: a variable name collision in `wifi_tx.c`, and a missing header include in `mavlink_parser.c` (`ODID_ID_SIZE` undeclared after removing an unused transitive include).

### Fixed — WiFi AP, live reconfiguration, and stability
- WiFi AP ignored configured SSID, password, channel, and TX power — always used hardcoded defaults.
- Web UI silently saved new WiFi settings with no reboot and no live-reconfiguration path, so changes never actually took effect.
- **Live WiFi reconfiguration (no-reboot SSID/password/channel changes) caused repeated crashes across multiple root causes, now all fixed:**
  - `handle_post_config()` only read the request body in a single `httpd_req_recv()` call, which can return fewer bytes than requested — now loops until the full `content_len` is received.
  - `esp_wifi_stop()` tore down the entire network stack while it was still actively serving the very HTTP connection that triggered the reconfigure, crashing inside lwIP's UDP/TCP receive paths. Removed the stop/start cycle; `esp_wifi_set_config()` on an already-running AP applies live.
  - The WiFi reconfigure task deleted itself (`vTaskDelete`) immediately after touching the WiFi driver, which only queues work to the driver's own internal task asynchronously — could leave a dangling reference to the deleted task, corrupting an unrelated later operation. Replaced with a persistent task that sleeps on a notification and never exits.
  - `handle_post_config()`'s HTTP socket pool (`max_open_sockets`) was left at ESP-IDF's small default with `lru_purge_enable` on, risking the server force-closing the client's own in-flight connection under load. Increased to 12.
  - **Root cause, confirmed via core dump + GDB analysis:** the HTTP server's task stack (already once increased from ESP-IDF's default to 8192 for its mbedtls/SHA-256 workload) was still too small for the combination of two full `rid_config_t` locals plus the deep `httpd → lwIP → recv` call chain. Doubled again to 16384.
  - `rid_task`'s stack (4096) was also increased to 8192 as a contributing factor, confirmed via a genuine FreeRTOS stack-overflow detection during the same investigation.
- `esp_wifi_80211_tx()` was called with no minimum spacing between attempts, violating a documented driver requirement that the previous packet's "sent" event be processed before sending again. Added enforced spacing across all TX call sites.
- Promiscuous mode was enabled unnecessarily (confirmed via Espressif's own docs that `esp_wifi_start()` alone is sufficient to initialize the radio for raw-frame TX) — removed, reducing RX processing load.
- **Heap corruption (`CORRUPT HEAP`, confirmed via ESP-IDF's own poisoning detector) when a station reconnected immediately after a WiFi credential change.** Documented ESP-IDF race: a station reconnecting before the AP fully recognizes its previous disconnection corrupts internal per-station state. Fixed by explicitly deauthenticating connected stations and waiting for the disconnect to settle before applying new credentials.
- `wifi_tx_transmit()`'s WiFi Beacon frame hardcoded `"ESP-RID"` as the SSID embedded in the payload itself, independent of the actual configured/broadcast SSID. Now reads the real configured value.

### Added
- Operator/takeoff location capture: GPS position recorded once at the first solid (3D) fix after boot, correctly labeled per ASTM F3411 (`Takeoff` / `Live-GNSS` / `Fixed`).
- Concurrent dual BLE5 extended-advertising instances: one legacy-compatible (visible to BLE 4.2-only scanners), one true long-range (Coded PHY), running simultaneously.
- Shared signature-verification module (`rid_security.c`/`.h`).
- Client-side SHA-256 hashing for web UI firmware uploads.
- Haptic feedback and persistent visual toggle-state in the web UI.
- Core dump-to-flash support for future debugging (dedicated partition, Kconfig).

### Fixed — Web UI
- OTA upload never sent the integrity hash the backend requires, so uploads always failed.
- **False "Connection Lost" popup appeared on nearly every fresh page load.** The disconnect-detection timer compared against an epoch-zero baseline instead of page-load time, so the very first check could fire before the first status fetch had a chance to complete. Fixed the baseline, and added a longer 30s grace period before the first-ever successful connection (covering the firmware's own ~10s startup delay) versus a tighter 15s threshold for a genuine later disconnect.

### Changed
- Web UI: faster tap response (removed default mobile tap-delay), tactile press feedback on buttons.
