#include <stdio.h>
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "driver/temperature_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_remote_id.h"
#include "protocol_detect.h"
#include "nmea_parser.h"
#include "msp_parser.h"
#include "mavlink_parser.h"
#include "wifi_tx.h"
#include "ble_tx.h"
#include "esp_netif.h"
#include "web_config.h"
#include "nvs_storage.h"
#include "led_status.h"
#include "rid_patrol.h"
#include "cli.h"
#include "rid_kalman.h"
#include "rid_mavlink_tx.h"
#include "rid_auth.h"
#include "rid_ota.h"
#include "led_ws2812.h"
#include "rid_lighting.h"
#include "rid_dronecan.h"
#include "rid_mavlink_usb.h"
#include "buzzer.h"

#define TAG "ESP_RID"

#define C_BLU  "\x1b[34m"
#define C_GRN  "\x1b[32m"
#define C_AMB  "\x1b[33m"
#define C_RED  "\x1b[31m"
#define C_RST  "\x1b[0m"

static rid_config_t g_config;
static rid_state_t g_state;
static bool g_running = false;
static SemaphoreHandle_t g_lock = NULL;
static rid_kalman_3d_t g_kalman;

static void default_config(rid_config_t *cfg)
{
    memset(cfg, 0, sizeof(rid_config_t));

    cfg->protocol = RID_PROTOCOL_AUTO;
    cfg->uart_port = 1;
    cfg->baud_rate = 9600;
    cfg->tx_pin = 10;
    cfg->rx_pin = 11;

    /* UA Type 2 = Helicopter/Multirotor, ID Type 2 = CAA Registration --
     * the most common real-world configuration for this class of
     * aircraft, rather than the ODID spec's raw enum-order defaults. */
    cfg->ua_type = 2;
    cfg->id_type = 2;
    snprintf(cfg->uas_id, sizeof(cfg->uas_id), "%s", "ESP32-RID-001");
    snprintf(cfg->operator_id, sizeof(cfg->operator_id), "%s", "OP-UNKNOWN");
    cfg->self_id_text[0] = '\0';
    cfg->operator_lat = 0.0;
    cfg->operator_lon = 0.0;
    cfg->operator_alt = 0.0f;

    cfg->ua_type_2 = 0;
    cfg->id_type_2 = 0;
    cfg->uas_id_2[0] = '\0';

    /* WiFi NAN stays off by default -- it's still experimental/beta in
     * this firmware, unlike the other three which are all established
     * enough to broadcast out of the box. */
    cfg->tx_modes = RID_TRANSMIT_WIFI_BCN | RID_TRANSMIT_BLE4 | RID_TRANSMIT_BLE5 | RID_TRANSMIT_BLE5_EXT1M;
    cfg->wifi_channel = 6;
    cfg->wifi_power_dbm = 20.0f;
    /* 5Hz (within the 0-10Hz UI range -- 10Hz is the true ceiling, set
     * by rid_task()'s 100ms main loop tick). Unlike BLE's
     * continuously-repeating advertising engine, an injected WiFi
     * beacon (esp_wifi_80211_tx()) only goes out at the exact instant
     * this rate allows -- there's no RF-level auto-repeat in between --
     * so this rate directly controls how likely an Android WiFi scan's
     * channel dwell window is to overlap with one. A faster default
     * also shortens the BLE4 Legacy schedule's full-cycle wall-clock
     * time (see s_legacy_schedule below), so Basic ID and Location both
     * surface sooner without changing the schedule itself. */
    cfg->wifi_bcn_rate_hz = 5.0f;
    cfg->wifi_nan_rate_hz = 0.0f;
    cfg->ble4_rate_hz = 5.0f;
    /* +9dBm is the genuine achievable maximum on this chip's classic
     * Bluedroid BLE stack (esp_power_level_t tops out at
     * ESP_PWR_LVL_P9) -- there is no higher level to map to, so the
     * default reflects reality instead of a value that silently gets
     * clamped down by ble_tx_set_power(). */
    cfg->ble4_power_dbm = 9.0f;
    cfg->ble5_rate_hz = 5.0f;
    cfg->ble5_power_dbm = 9.0f;

    cfg->wifi_ssid[0] = '\0';
    snprintf(cfg->wifi_ssid, sizeof(cfg->wifi_ssid), "%s", "ESP-RID");
    cfg->wifi_password[0] = '\0';
    cfg->webserver_en = 1;

    cfg->mavlink_sysid = 0;
    cfg->bcast_powerup = 1;

    cfg->options = 0;
    cfg->lock_level = 0;

    cfg->led_r_gpio =
#ifdef CONFIG_RID_LED_R_GPIO
        CONFIG_RID_LED_R_GPIO;
#else
        -1;
#endif
    cfg->led_g_gpio =
#ifdef CONFIG_RID_LED_G_GPIO
        CONFIG_RID_LED_G_GPIO;
#else
        -1;
#endif
    cfg->led_b_gpio =
#ifdef CONFIG_RID_LED_B_GPIO
        CONFIG_RID_LED_B_GPIO;
#else
        -1;
#endif

    /* GPIO9 is not a strapping/USB/flash pin on ESP32-S3, so it's safe
     * as a default; still user-changeable via the web UI Hardware tab. */
    cfg->buzzer_gpio = 9;

    cfg->ws2812_gpio = -1;
    cfg->ws2812_brightness = 16;

    memset(cfg->lighting_pins, -1, sizeof(cfg->lighting_pins));
    memset(cfg->lighting_patterns, 0, sizeof(cfg->lighting_patterns));
    memset(cfg->lighting_phase_offsets, 0, sizeof(cfg->lighting_phase_offsets));

    cfg->dronecan_rx_gpio = -1;
    cfg->dronecan_tx_gpio = -1;
    cfg->dronecan_bitrate = 1000000;

    cfg->mavlink_usb_enable = false;

    cfg->ota_trigger_gpio = -1;

    cfg->auth_private_key[0] = '\0';

    cfg->start_delay_ms = 10000;

    for (int i = 0; i < ESP_RID_NUM_KEYS; i++)
        cfg->public_keys[i][0] = '\0';
}

static bool identity_is_sane(const rid_identity_t *id)
{
    if (id->uas_id[0] == '\0') return false;
    if (strstr((const char *)id->uas_id, "ESP32-RID-") == (const char *)id->uas_id) return false;
    if (strstr((const char *)id->operator_id, "OP-UNKNOWN") != NULL) return false;
    if (id->operator_id[0] == '\0') return false;
    return true;
}

static bool position_is_sane(const rid_gps_data_t *gps)
{
    if (gps->latitude < -90.0 || gps->latitude > 90.0) return false;
    if (gps->longitude < -180.0 || gps->longitude > 180.0) return false;
    return true;
}

void esp_rid_init(void)
{
    ESP_LOGI(TAG, "ESP DRONE REMOTEID v%s initializing", ESP_RID_VERSION);

    nvs_storage_init();
    default_config(&g_config);
    nvs_storage_load(&g_config);

    /* Validate tx_pin/rx_pin once, at boot, and persist any correction.
     * protocol_detect.c also validates these on every UART reinit, but
     * only corrects the value used for that specific call -- it never
     * rewrites NVS, so a genuinely bad stored value (confirmed to
     * happen: tx_pin/rx_pin were once found stuck at 165, not even a
     * valid GPIO number) keeps triggering the same warning and the same
     * runtime substitution forever. Fixing it here, once, and saving it
     * back means it only ever needs correcting a single time. */
    {
        bool pins_needed_fix = false;
        if (g_config.tx_pin > 48 || esp_rid_is_reserved_gpio(g_config.tx_pin)) {
            ESP_LOGW(TAG, "Stored tx_pin=%u is invalid/reserved, correcting to 10 and saving",
                     g_config.tx_pin);
            g_config.tx_pin = 10;
            pins_needed_fix = true;
        }
        if (g_config.rx_pin > 48 || esp_rid_is_reserved_gpio(g_config.rx_pin)) {
            ESP_LOGW(TAG, "Stored rx_pin=%u is invalid/reserved, correcting to 11 and saving",
                     g_config.rx_pin);
            g_config.rx_pin = 11;
            pins_needed_fix = true;
        }
        if (pins_needed_fix) {
            nvs_storage_save(&g_config);
        }
    }

    /* Check OTA mode (reads ota_trigger_gpio from config) */
    rid_ota_check_and_run(&g_config);

    g_lock = xSemaphoreCreateMutex();

    memset(&g_state, 0, sizeof(rid_state_t));

    /* Apply BLE TX power (default 9 dBm) */
    ble_tx_set_power(9);

    /* Startup delay */
    if (g_config.start_delay_ms > 0) {
        ESP_LOGI(TAG, "Startup delay %lu ms", (unsigned long)g_config.start_delay_ms);
        vTaskDelay(pdMS_TO_TICKS(g_config.start_delay_ms));
    }

    protocol_detect_init();
    nmea_parser_init();
    msp_parser_init();
    mavlink_parser_init();
    mavlink_parser_set_sysid_filter(g_config.mavlink_sysid);

    esp_netif_init();

    wifi_tx_init();
    ble_tx_init();

    /* MAVLink bidirectional link */
    if (g_config.options & (RID_OPT_MAVLINK_ARM_STATUS | RID_OPT_MAVLINK_OP_LOC_LOOP)) {
        rid_mavlink_tx_init();
        xTaskCreate(rid_mavlink_tx_task, "rid_mavlink_tx", 2048, NULL, 3, NULL);
    }

    /* Authentication */
    if (g_config.options & RID_OPT_AUTH_ED25519) {
        rid_auth_init(g_config.auth_private_key);
    }

    /* WS2812 addressable LED */
    led_ws2812_init(g_config.ws2812_gpio, g_config.ws2812_brightness);

    /* Buzzer (boot chime, satellite-lock / home-saved tone) */
    buzzer_init(g_config.buzzer_gpio);
    buzzer_play_boot_tone();

    /* External lighting outputs */
    for (int i = 0; i < RID_LIGHTING_MAX_OUTPUTS; i++) {
        if (g_config.lighting_pins[i] >= 0) {
            rid_lighting_init(g_config.lighting_pins, g_config.lighting_patterns,
                             g_config.lighting_phase_offsets);
            break;
        }
    }

    /* DroneCAN */
    if (g_config.dronecan_rx_gpio >= 0 && g_config.dronecan_tx_gpio >= 0) {
        rid_dronecan_init(g_config.dronecan_rx_gpio, g_config.dronecan_tx_gpio,
                         g_config.dronecan_bitrate);
    }

    /* USB Serial MAVLink */
    if (g_config.mavlink_usb_enable) {
        rid_mavlink_usb_init();
    }

    if (strcmp(g_config.uas_id, "ESP32-RID-001") == 0 ||
        strcmp(g_config.operator_id, "OP-UNKNOWN") == 0) {
        uint8_t mac[6];
        wifi_tx_get_mac(mac);
        snprintf(g_config.uas_id, sizeof(g_config.uas_id), "ESP32-RID-%02X%02X", mac[4], mac[5]);
        snprintf(g_config.operator_id, sizeof(g_config.operator_id), "ESP32-OP-%02X%02X", mac[4], mac[5]);
        nvs_storage_save(&g_config);
    }

    led_status_reconfigure(g_config.led_r_gpio, g_config.led_g_gpio, g_config.led_b_gpio);
    if (g_config.webserver_en) {
        web_config_init();
    } else {
        ESP_LOGW(TAG, "Web server disabled by config (webserver_en=0) -- use the serial CLI's "
                      "'webserver on' command or a factory reset to re-enable it");
    }

    cli_init();

    rid_kalman_init(&g_kalman);

    ESP_LOGI(TAG, "\xE2\x9C\x93 Remote ID initialized");
}

void esp_rid_set_config(const rid_config_t *config)
{
    bool baud_changed = false, pins_changed = false, protocol_changed = false;
    bool led_changed = false, ws2812_changed = false, buzzer_changed = false;
    bool webserver_changed = false;
    uint32_t new_baud = 0;
    int8_t new_led_r = 0, new_led_g = 0, new_led_b = 0;
    int8_t new_ws2812_gpio = 0;
    uint8_t new_ws2812_brightness = 0;
    int8_t new_buzzer_gpio = 0;
    uint32_t new_sysid = 0;
    uint8_t new_webserver_en = 0;

    if (g_lock && xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        uint32_t old_baud = g_config.baud_rate;
        uint8_t old_tx_pin = g_config.tx_pin;
        uint8_t old_rx_pin = g_config.rx_pin;
        rid_protocol_t old_protocol = g_config.protocol;
        int8_t old_led_r = g_config.led_r_gpio;
        int8_t old_led_g = g_config.led_g_gpio;
        int8_t old_led_b = g_config.led_b_gpio;
        int8_t old_ws2812_gpio = g_config.ws2812_gpio;
        uint8_t old_ws2812_brightness = g_config.ws2812_brightness;
        int8_t old_buzzer_gpio = g_config.buzzer_gpio;
        uint8_t old_webserver_en = g_config.webserver_en;
        memcpy(&g_config, config, sizeof(rid_config_t));
        nvs_storage_save(&g_config);

        baud_changed = (g_config.baud_rate != old_baud && g_config.baud_rate > 0);
        pins_changed = (g_config.tx_pin != old_tx_pin || g_config.rx_pin != old_rx_pin);
        protocol_changed = (g_config.protocol != old_protocol);
        led_changed = (g_config.led_r_gpio != old_led_r ||
                       g_config.led_g_gpio != old_led_g ||
                       g_config.led_b_gpio != old_led_b);
        ws2812_changed = (g_config.ws2812_gpio != old_ws2812_gpio ||
                           g_config.ws2812_brightness != old_ws2812_brightness);
        buzzer_changed = (g_config.buzzer_gpio != old_buzzer_gpio);
        webserver_changed = (g_config.webserver_en != old_webserver_en);

        new_baud = g_config.baud_rate;
        new_led_r = g_config.led_r_gpio;
        new_led_g = g_config.led_g_gpio;
        new_led_b = g_config.led_b_gpio;
        new_ws2812_gpio = g_config.ws2812_gpio;
        new_ws2812_brightness = g_config.ws2812_brightness;
        new_buzzer_gpio = g_config.buzzer_gpio;
        new_sysid = g_config.mavlink_sysid;
        new_webserver_en = g_config.webserver_en;

        xSemaphoreGive(g_lock);
    } else {
        ESP_LOGW(TAG, "esp_rid_set_config: could not acquire lock, config NOT saved");
        return;
    }

    /* Everything below runs AFTER releasing g_lock. protocol_detect_reinit()
     * re-reads tx_pin/rx_pin from config itself (see protocol_detect.c)
     * via esp_rid_get_config(), which takes this SAME lock -- calling it
     * while still holding g_lock above caused a guaranteed self-deadlock
     * on every single reconfigure (confirmed: the lock-timeout warning
     * fired on every "Reconfiguring UART" line), silently falling back
     * to default pins instead of whatever was actually configured. */
    if (baud_changed || pins_changed || protocol_changed) {
        protocol_detect_reinit(new_baud);
    }
    mavlink_parser_set_sysid_filter(new_sysid);

    /* Only reconfigure LEDC/RMT when the relevant settings actually
     * changed. Calling these unconditionally on EVERY save (even ones
     * unrelated to LED settings) re-triggers LEDC's known GPIO
     * reservation-leak bug (see e.g. esp-idf issues #15621/#15666):
     * the driver never releases its internal "pin reserved" tracking
     * when a channel is reconfigured, so re-configuring the SAME pins
     * again logs a "GPIO X is not usable, maybe conflict with
     * others" warning -- cosmetic, but needless noise on every
     * unrelated config save. */
    if (led_changed) {
        led_status_reconfigure(new_led_r, new_led_g, new_led_b);
    }
    if (ws2812_changed) {
        led_ws2812_init(new_ws2812_gpio, new_ws2812_brightness);
    }
    if (buzzer_changed) {
        buzzer_reconfigure(new_buzzer_gpio);
    }
    if (webserver_changed) {
        if (new_webserver_en) {
            web_config_init();
        } else {
            ESP_LOGW(TAG, "Web server disabled -- use the serial CLI's 'webserver on' "
                          "command or a factory reset to re-enable it");
            web_config_stop();
        }
    }
}

void esp_rid_get_config(rid_config_t *config)
{
    /* Always start from safe, valid defaults, BEFORE attempting the
     * lock-protected copy. Previously, if xSemaphoreTake() timed out
     * (plausible under lock contention -- every CLI command, every web
     * save, and rid_task()'s own main loop all take this same lock,
     * the last of those every single cycle), this function did nothing
     * at all and returned silently, leaving the caller's struct as
     * whatever uninitialized stack garbage it started as. Callers then
     * often changed one or two fields and saved the WHOLE struct back
     * to NVS, permanently persisting that garbage for every other
     * field. This is confirmed as the actual mechanism behind a
     * recurring tx_pin/rx_pin corruption: the observed bad value (165)
     * is exactly FreeRTOS's own 0xA5 stack-fill byte. */
    default_config(config);
    if (g_lock && xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(config, &g_config, sizeof(rid_config_t));
        xSemaphoreGive(g_lock);
    } else {
        ESP_LOGW(TAG, "esp_rid_get_config: could not acquire lock, returning safe defaults");
    }
}

void esp_rid_get_state(rid_state_t *state)
{
    if (g_lock && xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(state, &g_state, sizeof(rid_state_t));
        xSemaphoreGive(g_lock);
    }
}

void esp_rid_factory_reset(void)
{
    nvs_storage_erase();
    if (g_lock && xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        default_config(&g_config);
        xSemaphoreGive(g_lock);
    }
    nvs_storage_save(&g_config);
}

/* Blocks GPIO pins that would break the board if reassigned to a plain
 * UART, rather than just being merely inadvisable:
 *  - 19, 20: hardwired to native USB D-/D+ on ESP32-S3. Reassigning
 *    these cuts the very USB/serial connection you're using to talk to
 *    the device -- this is exactly what caused this project's own
 *    "lost the COM port" incident, made worse by pin changes now
 *    applying live without a reboot.
 *  - 26-32: SPI flash/PSRAM on most ESP32-S3 modules. Reassigning these
 *    can corrupt flash access outright -- more severe than losing USB,
 *    since it can leave the device needing an external reflash to
 *    recover rather than just a BOOT-button recovery.
 *  - 0, 3, 45, 46: strapping pins read at boot to select boot mode.
 *    Driving these as a busy UART can interfere with the NEXT boot.
 *  - 43, 44: default UART0 console pins. Not hardwired the way 19/20
 *    are, but commonly relied on for flashing/debugging on many boards;
 *    blocked as a conservative default even though this project's own
 *    console currently runs over USB-Serial/JTAG instead. */
bool esp_rid_is_reserved_gpio(int pin)
{
    /* 0, 3, 45, 46: strapping pins sampled at boot/reset (boot mode,
     * flash voltage, etc.) -- driving these wrong can prevent booting.
     * 19, 20: native USB D-/D+. Repurposing either cuts the USB/COM
     *   port connection the instant a live pin change applies -- this
     *   is exactly what happened when tx_pin/rx_pin got set to one of
     *   these, requiring a BOOT-button bootloader-mode recovery.
     * 26-32: SPI flash on essentially all ESP32-S3 boards.
     * 33-37: PSRAM/flash in octal mode on many (not all) ESP32-S3
     *   module variants -- blocked unconditionally since a wrong guess
     *   here is far more costly than an overly conservative block.
     * 43, 44: default UART0 console pins. */
    static const int reserved[] = {
        0, 3, 19, 20, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 43, 44, 45, 46
    };
    for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++) {
        if (pin == reserved[i]) return true;
    }
    return false;
}

bool esp_rid_gpio_conflicts(const rid_config_t *cfg, int pin, const char *field_name,
                             const char **conflict_name)
{
    if (pin < 0) return false;

    struct { const char *name; int8_t value; } single_pins[] = {
        { "tx_pin",           (int8_t)cfg->tx_pin },
        { "rx_pin",           (int8_t)cfg->rx_pin },
        { "led_r_gpio",       cfg->led_r_gpio },
        { "led_g_gpio",       cfg->led_g_gpio },
        { "led_b_gpio",       cfg->led_b_gpio },
        { "buzzer_gpio",      cfg->buzzer_gpio },
        { "ws2812_gpio",      cfg->ws2812_gpio },
        { "dronecan_rx_gpio", cfg->dronecan_rx_gpio },
        { "dronecan_tx_gpio", cfg->dronecan_tx_gpio },
        { "ota_trigger_gpio", cfg->ota_trigger_gpio },
    };

    for (size_t i = 0; i < sizeof(single_pins) / sizeof(single_pins[0]); i++) {
        if (field_name && strcmp(single_pins[i].name, field_name) == 0) continue;
        if (single_pins[i].value == (int8_t)pin) {
            if (conflict_name) *conflict_name = single_pins[i].name;
            return true;
        }
    }

    if (!field_name || strcmp(field_name, "lighting_pins") != 0) {
        for (int i = 0; i < RID_LIGHTING_MAX_OUTPUTS; i++) {
            if (cfg->lighting_pins[i] == (int8_t)pin) {
                if (conflict_name) *conflict_name = "lighting_pins";
                return true;
            }
        }
    }

    return false;
}

static temperature_sensor_handle_t g_temp_sensor = NULL;
static bool g_temp_sensor_init_attempted = false;

float esp_rid_get_cpu_temp(void)
{
    if (!g_temp_sensor_init_attempted) {
        g_temp_sensor_init_attempted = true;
        /* 10-80 C range covers normal operating conditions; ESP32-S3's
         * internal sensor supports several ranges with different
         * accuracy/coverage tradeoffs -- this one is a reasonable
         * general-purpose default. */
        temperature_sensor_config_t temp_cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);
        if (temperature_sensor_install(&temp_cfg, &g_temp_sensor) == ESP_OK) {
            if (temperature_sensor_enable(g_temp_sensor) != ESP_OK) {
                g_temp_sensor = NULL;
            }
        } else {
            g_temp_sensor = NULL;
        }
    }
    if (!g_temp_sensor) {
        return NAN;
    }
    float celsius = NAN;
    temperature_sensor_get_celsius(g_temp_sensor, &celsius);
    return celsius;
}

static int64_t last_tx_wifi_bcn = 0;
static int64_t last_tx_wifi_nan = 0;
static int64_t last_tx_ble4 = 0;
static int64_t last_tx_ble5 = 0;
static int64_t last_tx_ble5_ext1m = 0;

static bool rate_allowed(int64_t *last_us, float rate_hz)
{
    if (rate_hz <= 0.0f) return false;
    int64_t now = esp_timer_get_time();
    int64_t interval = (int64_t)(1000000.0f / rate_hz);
    if (now - *last_us >= interval) {
        *last_us = now;
        return true;
    }
    return false;
}

static uint8_t g_nan_counter = 0;

/* Throttles the "why nothing was sent" log to roughly once per 3s so a
 * device sitting with no GPS/no protocol for a long time doesn't flood
 * the log at the main loop's 100ms cadence. Per-channel "sent" logs
 * below aren't throttled the same way since rate_allowed() already
 * caps those to each channel's configured Hz (1/s by default). */
static void verbose_tx_skip_log(const char *reason)
{
    static int64_t last_us = 0;
    int64_t now = esp_timer_get_time();
    if (now - last_us < 3000000) return;
    last_us = now;
    ESP_LOGI(TAG, "[TX] Skipped: %s", reason);
}

static bool update_transmissions(void)
{
    bool verbose = g_config.options & RID_OPT_VERBOSE_TX_LOG;

    if (!g_state.gps_valid && !g_config.bcast_powerup) {
        if (verbose) verbose_tx_skip_log("no valid GPS fix and 'Broadcast without GPS' is disabled");
        return false;
    }
    if (g_state.active_protocol == RID_PROTOCOL_UNKNOWN) {
        if (verbose) verbose_tx_skip_log("active protocol is UNKNOWN (no GPS/FC data detected yet)");
        return false;
    }

    /* Identity readiness gate */
    if (g_config.options & RID_OPT_IDENTITY_READY_GATE) {
        if (!g_state.identity_ready) {
            if (verbose) verbose_tx_skip_log("Identity Readiness Gate is enabled and identity/position isn't sane yet");
            return false;
        }
    }

    bool tx = false;

    if (g_config.tx_modes & RID_TRANSMIT_WIFI_BCN) {
        if (rate_allowed(&last_tx_wifi_bcn, g_config.wifi_bcn_rate_hz)) {
            wifi_tx_transmit(&g_state.gps, &g_state.identity);
            g_state.wifi_bcn_count++;
            g_state.transmissions_count++;
            tx = true;
            if (verbose) {
                ESP_LOGI(TAG, "[TX] WiFi Beacon #%lu sent -- uas=%s lat=%.6f lon=%.6f fix=%d rate=%.1fHz",
                         (unsigned long)g_state.wifi_bcn_count, g_state.identity.uas_id,
                         g_state.gps.latitude, g_state.gps.longitude, g_state.gps.fix_type,
                         (double)g_config.wifi_bcn_rate_hz);
            }
        }
    }

    if (g_config.tx_modes & RID_TRANSMIT_WIFI_NAN) {
        if (rate_allowed(&last_tx_wifi_nan, g_config.wifi_nan_rate_hz)) {
            wifi_tx_transmit_nan(&g_state.gps, &g_state.identity, g_nan_counter++);
            g_state.wifi_nan_count++;
            g_state.transmissions_count++;
            tx = true;
            if (verbose) {
                ESP_LOGI(TAG, "[TX] WiFi NAN #%lu sent -- uas=%s rate=%.1fHz",
                         (unsigned long)g_state.wifi_nan_count, g_state.identity.uas_id,
                         (double)g_config.wifi_nan_rate_hz);
            }
        }
    }

    if (g_config.tx_modes & RID_TRANSMIT_BLE4) {
        if (rate_allowed(&last_tx_ble4, g_config.ble4_rate_hz)) {
            ble_tx_transmit_legacy(&g_state.gps, &g_state.identity);
            g_state.ble4_count++;
            g_state.transmissions_count++;
            tx = true;
            if (verbose) {
                ESP_LOGI(TAG, "[TX] BLE 4.0 Legacy #%lu sent -- uas=%s rate=%.1fHz",
                         (unsigned long)g_state.ble4_count, g_state.identity.uas_id,
                         (double)g_config.ble4_rate_hz);
            }
        }
    }

    if (g_config.tx_modes & RID_TRANSMIT_BLE5) {
        if (rate_allowed(&last_tx_ble5, g_config.ble5_rate_hz)) {
            ble_tx_transmit_lr(&g_state.gps, &g_state.identity);
            g_state.ble5_count++;
            g_state.transmissions_count++;
            tx = true;
            if (verbose) {
                ESP_LOGI(TAG, "[TX] BLE 5.0 LR #%lu sent -- uas=%s rate=%.1fHz",
                         (unsigned long)g_state.ble5_count, g_state.identity.uas_id,
                         (double)g_config.ble5_rate_hz);
            }
        }
    }

    if (g_config.tx_modes & RID_TRANSMIT_BLE5_EXT1M) {
        if (rate_allowed(&last_tx_ble5_ext1m, g_config.ble5_rate_hz)) {
            ble_tx_transmit_ext1m(&g_state.gps, &g_state.identity);
            g_state.ble5_ext1m_count++;
            g_state.transmissions_count++;
            tx = true;
            if (verbose) {
                ESP_LOGI(TAG, "[TX] BLE 5.0 Ext (1M PHY) #%lu sent -- uas=%s rate=%.1fHz",
                         (unsigned long)g_state.ble5_ext1m_count, g_state.identity.uas_id,
                         (double)g_config.ble5_rate_hz);
            }
        }
    }

    return tx;
}

static const char *proto_name(rid_protocol_t p)
{
    switch (p) {
        case RID_PROTOCOL_UNKNOWN: return "UNKNOWN";
        case RID_PROTOCOL_MAVLINK: return "MAVLink";
        case RID_PROTOCOL_MSP:     return "MSP";
        case RID_PROTOCOL_NMEA:   return "NMEA";
        case RID_PROTOCOL_NONE:   return "NONE";
        case RID_PROTOCOL_AUTO:   return "AUTO";
        default: return "?";
    }
}

static void print_status_box(void)
{
    char lat_str[16], lon_str[16];
    if (g_state.gps_valid)
        snprintf(lat_str, sizeof(lat_str), "%.4f", g_state.gps.latitude);
    else
        snprintf(lat_str, sizeof(lat_str), "--");
    if (g_state.gps_valid)
        snprintf(lon_str, sizeof(lon_str), "%.4f", g_state.gps.longitude);
    else
        snprintf(lon_str, sizeof(lon_str), "--");

    const char *gps_str = g_state.gps_valid ? "YES" : "NO";
    const char *proto = proto_name(g_state.active_protocol);
    const char *kal_str;
    if (g_config.options & RID_OPT_KALMAN_FILTER)
        kal_str = rid_kalman_valid_age(&g_kalman, esp_timer_get_time()) ? "ON" : "INIT";
    else
        kal_str = "OFF";

    const char *rdy_str = g_state.identity_ready ? "YES" : "NO";

    printf(C_GRN);
    printf("\n");
    printf("  \xE2\x94\x8C\xE2\x94\x80 ESP Drone Remote ID \xE2\x94\x80 status \xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x90\n");
    printf("  \xE2\x94\x82  Protocol  : %-33s \xE2\x94\x82\n", proto);
    printf("  \xE2\x94\x82  GPS Fix   : %-3s           (%d/%d)       \xE2\x94\x82\n",
           gps_str, g_state.gps.fix_type, g_state.gps.satellites);
    printf("  \xE2\x94\x82  Lat / Lon : %s / %-12s       \xE2\x94\x82\n", lat_str, lon_str);
    printf("  \xE2\x94\x82  TX count  : %-35lu \xE2\x94\x82\n",
           (unsigned long)g_state.transmissions_count);
    printf("  \xE2\x94\x82  Kalman    : %-33s \xE2\x94\x82\n", kal_str);
    printf("  \xE2\x94\x82  Identity  : %-33s \xE2\x94\x82\n", rdy_str);
    printf("  \xE2\x94\x94\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x98\n");
    printf(C_RST);
}

static void print_system_box(void)
{
    int64_t us = esp_timer_get_time();
    uint32_t sec = (uint32_t)(us / 1000000);
    uint32_t h = sec / 3600, m = (sec % 3600) / 60, s = sec % 60;
    uint32_t heap_free = esp_get_free_heap_size();
    uint32_t heap_total = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    printf(C_BLU);
    printf("\n");
    printf("  \xE2\x94\x8C\xE2\x94\x80 System Info \xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x90\n");
    printf("  \xE2\x94\x82  Heap free : %-5lu KB / %-5lu KB                        \xE2\x94\x82\n",
           (unsigned long)(heap_free / 1024), (unsigned long)(heap_total / 1024));
    printf("  \xE2\x94\x82  Uptime    : %02lu:%02lu:%02lu                                      \xE2\x94\x82\n",
           (unsigned long)h, (unsigned long)m, (unsigned long)s);
    printf("  \xE2\x94\x94\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x98\n");
    printf(C_RST);
}

static void rid_task(void *arg)
{
    g_state.active_protocol = RID_PROTOCOL_UNKNOWN;
    uint32_t log_cycle = 0;
    bool had_gps = false;
    rid_protocol_t auto_detected_proto = RID_PROTOCOL_UNKNOWN;

    while (g_running) {
        rid_gps_data_t gps_data;
        rid_protocol_t proto = RID_PROTOCOL_UNKNOWN;

        memset(&gps_data, 0, sizeof(rid_gps_data_t));

        if (g_lock) xSemaphoreTake(g_lock, portMAX_DELAY);
        rid_protocol_t cfg_proto = g_config.protocol;
        uint16_t cfg_opts = g_config.options;
        if (g_lock) xSemaphoreGive(g_lock);

        if (cfg_proto == RID_PROTOCOL_AUTO) {
            if (auto_detected_proto == RID_PROTOCOL_UNKNOWN) {
                /* Not yet locked onto a protocol (or lost it -- see
                 * re-detect logic below): sniff to find one. Once this
                 * succeeds, this read is skipped on future cycles so it
                 * stops competing with the real parser's own read for
                 * the same incoming bytes. */
                auto_detected_proto = protocol_detect_auto();
                if ((cfg_opts & RID_OPT_VERBOSE_TX_LOG) && auto_detected_proto != RID_PROTOCOL_UNKNOWN) {
                    ESP_LOGI(TAG, "[TX] Auto-detected protocol: %s", proto_name(auto_detected_proto));
                }
            }
            proto = auto_detected_proto;
        } else {
            proto = cfg_proto;
            auto_detected_proto = RID_PROTOCOL_UNKNOWN;
        }

        g_state.active_protocol = proto;

        bool have_data = false;

        switch (proto) {
        case RID_PROTOCOL_NMEA:
            have_data = nmea_parser_get(&gps_data);
            break;
        case RID_PROTOCOL_MSP:
            have_data = msp_parser_get(&gps_data);
            break;
        case RID_PROTOCOL_MAVLINK:
            have_data = mavlink_parser_get(&gps_data);
            break;
        default:
            break;
        }

        /* Try DroneCAN as secondary input */
        if (!have_data && rid_dronecan_is_active()) {
            have_data = rid_dronecan_get(&gps_data);
            if (have_data) g_state.active_protocol = RID_PROTOCOL_NONE;
        }

        had_gps = false;

        if (have_data && gps_data.latitude != 0.0) {
            bool force_tx = (cfg_opts & RID_OPT_FORCE_ARM_OK) && gps_data.armed;

            if (force_tx || gps_data.fix_type >= 2) {
                had_gps = true;
                if (g_lock) xSemaphoreTake(g_lock, portMAX_DELAY);
                memcpy(&g_state.gps, &gps_data, sizeof(rid_gps_data_t));
                g_state.gps_valid = true;
                g_state.last_update_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

                /* MAVLink arm status */
                if (proto == RID_PROTOCOL_MAVLINK) {
                    mavlink_parser_get_armed(&g_state.mavlink_armed);
                }
                g_state.gps.armed = g_state.mavlink_armed;

                rid_identity_t mav_id;
                bool have_mav_id = false;
                if (proto == RID_PROTOCOL_MAVLINK) {
                    have_mav_id = mavlink_parser_get_identity(&mav_id);
                }

                if (have_mav_id && mav_id.uas_id[0] != '\0') {
                    memcpy(&g_state.identity, &mav_id, sizeof(rid_identity_t));
                } else {
                    snprintf(g_state.identity.uas_id, sizeof(g_state.identity.uas_id), "%s", g_config.uas_id);
                    snprintf(g_state.identity.operator_id, sizeof(g_state.identity.operator_id), "%s", g_config.operator_id);
                    snprintf(g_state.identity.self_id_text, sizeof(g_state.identity.self_id_text), "%s", g_config.self_id_text);
                    g_state.identity.id_type = g_config.id_type;
                    g_state.identity.ua_type = g_config.ua_type;
                    snprintf(g_state.identity.uas_id_2, sizeof(g_state.identity.uas_id_2), "%s", g_config.uas_id_2);
                    g_state.identity.id_type_2 = g_config.id_type_2;
                    g_state.identity.ua_type_2 = g_config.ua_type_2;
                }

                /* Update operator location from MAVLink */
                double op_lat, op_lon;
                float op_alt;
                if (mavlink_parser_get_operator_location(&op_lat, &op_lon, &op_alt)) {
                    g_state.operator_lat = op_lat;
                    g_state.operator_lon = op_lon;
                    g_state.operator_alt = op_alt;
                    g_state.operator_position_updated_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
                    g_state.operator_location_type = 1;
                } else {
                    g_state.gps.operator_lat = g_config.operator_lat;
                    g_state.gps.operator_lon = g_config.operator_lon;
                    g_state.gps.operator_alt = g_config.operator_alt;
                }

                if (g_lock) xSemaphoreGive(g_lock);

                if (cfg_opts & RID_OPT_DONT_SAVE_BASIC_ID) {
                    g_state.identity.uas_id[0] = '\0';
                    g_state.identity.uas_id_2[0] = '\0';
                }

                /* Identity readiness gate */
                if ((cfg_opts & RID_OPT_IDENTITY_READY_GATE) &&
                    identity_is_sane(&g_state.identity) &&
                    position_is_sane(&g_state.gps)) {
                    g_state.identity_ready = true;
                } else if (!(cfg_opts & RID_OPT_IDENTITY_READY_GATE)) {
                    g_state.identity_ready = true;
                }
            }
        } else if (cfg_opts & RID_OPT_DEMO_MODE) {
            if (g_lock) xSemaphoreTake(g_lock, portMAX_DELAY);
            rid_patrol_tick(&g_state.gps);
            g_state.gps_valid = true;
            had_gps = true;
            g_state.last_update_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            g_state.active_protocol = RID_PROTOCOL_NONE;

            snprintf(g_state.identity.uas_id, sizeof(g_state.identity.uas_id), "%s", g_config.uas_id);
            snprintf(g_state.identity.operator_id, sizeof(g_state.identity.operator_id), "%s", g_config.operator_id);
            snprintf(g_state.identity.self_id_text, sizeof(g_state.identity.self_id_text), "%s", g_config.self_id_text);
            g_state.identity.id_type = g_config.id_type;
            g_state.identity.ua_type = g_config.ua_type;

            g_state.gps.operator_lat = g_config.operator_lat;
            g_state.gps.operator_lon = g_config.operator_lon;
            g_state.gps.operator_alt = g_config.operator_alt;

            if (g_lock) xSemaphoreGive(g_lock);

            g_state.identity_ready = true;
        }

        bool kalman_en = (cfg_opts & RID_OPT_KALMAN_FILTER) && !(cfg_opts & RID_OPT_DEMO_MODE);
        uint64_t now_us = kalman_en ? esp_timer_get_time() : 0;
        if (kalman_en) {

            if (had_gps && gps_data.latitude != 0.0 && gps_data.fix_type >= 2) {
                rid_kalman_update(&g_kalman, gps_data.latitude, gps_data.longitude,
                                 gps_data.altitude_msl, now_us);
            }

            rid_kalman_predict(&g_kalman, now_us);

            if (rid_kalman_valid_age(&g_kalman, now_us)) {
                double klat, klon;
                float kalt, kspeed, kclimb;
                int16_t kheading;
                rid_kalman_get(&g_kalman, &klat, &klon, &kalt, &kspeed, &kclimb, &kheading);
                g_state.gps.latitude = klat;
                g_state.gps.longitude = klon;
                g_state.gps.altitude_msl = kalt;
                g_state.gps.speed = kspeed;
                g_state.gps.speed_vertical = kclimb;
                g_state.gps.heading = kheading;
                g_state.gps_valid = true;
            } else if (!had_gps) {
                g_state.gps_valid = false;
            }
        }

        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (!kalman_en || !rid_kalman_valid_age(&g_kalman, now_us)) {
            if (g_state.gps_valid && (now_ms - g_state.last_update_ms > 10000)) {
                g_state.gps_valid = false;
            }
        }

        if (had_gps) {
            bool tx = update_transmissions();
            if (tx) led_status_tx_flash();

            /* Latches once per boot: the first time GPS reaches the same
             * fix quality this codebase already treats as "good enough to
             * transmit" (fix_type >= 2, see the force_tx check above),
             * the home position is captured and the buzzer plays its lock
             * tone. Deliberately not reset while flying (mirrors how
             * flight controllers play a GPS-lock tone once per power
             * cycle, not on every fix flicker). */
            if (!g_state.home_set && g_state.gps.fix_type >= 2 &&
                g_state.gps.latitude != 0.0 && g_state.gps.longitude != 0.0) {
                g_state.home_set = true;
                g_state.home_lat = g_state.gps.latitude;
                g_state.home_lon = g_state.gps.longitude;
                ESP_LOGI(TAG, "Satellite lock achieved, home position saved: %.6f, %.6f",
                         g_state.home_lat, g_state.home_lon);
                buzzer_play_lock_tone();
            }
        }

        if (g_config.lock_level >= 2) {
            led_status_set_state(RID_LED_LOCKED);
        } else if (cfg_opts & RID_OPT_DEMO_MODE) {
            led_status_set_state(RID_LED_DEMO);
        } else if (g_state.gps_valid) {
            led_status_set_state(RID_LED_GPS_OK);
        } else {
            led_status_set_state(RID_LED_NO_GPS);
        }
        led_status_tick();

        /* Compliance/armed indicator (repurposed blue LED pin): solid on
         * once identity is set, GPS has a valid fix, and the device has
         * actually transmitted at least one Remote ID message. */
        led_status_set_compliance(g_state.identity_ready && g_state.gps_valid &&
                                   g_state.transmissions_count > 0);

        /* WS2812 LED (if configured) */
        if (g_state.gps_valid) {
            led_ws2812_set_rgb(0, 255, 0);
        } else {
            led_ws2812_set_rgb(255, 200, 0);
        }

        /* External lighting outputs */
        rid_lighting_set_state(g_state.mavlink_armed, g_state.gps_valid);
        rid_lighting_tick();

        if (cfg_opts & RID_OPT_PRINT_RID_MAVLINK) {
            ESP_LOGI(TAG, "RID uas=%s lat=%.6f lon=%.6f alt=%.1f speed=%.1f hdg=%d fix=%d sat=%u ready=%d",
                g_state.identity.uas_id,
                g_state.gps.latitude, g_state.gps.longitude,
                (double)g_state.gps.altitude_msl, (double)g_state.gps.speed,
                g_state.gps.heading, g_state.gps.fix_type, g_state.gps.satellites,
                g_state.identity_ready);
        }

        log_cycle++;
        if (log_cycle % 100 == 0) {
            print_status_box();
        }
        if (log_cycle % 500 == 0) {
            print_system_box();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    vTaskDelete(NULL);
}

void esp_rid_start(void)
{
    if (g_running) return;
    g_running = true;
    xTaskCreate(rid_task, "rid_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "\xE2\x9C\x93 Remote ID started");
}

void esp_rid_stop(void)
{
    g_running = false;
    ESP_LOGI(TAG, "Remote ID stopped");
}
