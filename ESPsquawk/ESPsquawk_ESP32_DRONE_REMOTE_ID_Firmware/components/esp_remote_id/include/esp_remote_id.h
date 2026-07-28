#ifndef ESP_REMOTE_ID_H
#define ESP_REMOTE_ID_H

#include <stdint.h>
#include <stdbool.h>

#define ESP_RID_VERSION "1.5.2"
#define ESP_RID_MAX_STR_LEN 20
#define ESP_RID_MAX_KEY_LEN 256
#define ESP_RID_NUM_KEYS 5

#define RID_OPT_FORCE_ARM_OK          (1 << 0)
#define RID_OPT_DONT_SAVE_BASIC_ID   (1 << 1)
#define RID_OPT_PRINT_RID_MAVLINK    (1 << 2)
#define RID_OPT_DEMO_MODE            (1 << 3)
#define RID_OPT_KALMAN_FILTER        (1 << 4)
#define RID_OPT_AUTH_ED25519         (1 << 5)
#define RID_OPT_MAVLINK_ARM_STATUS   (1 << 6)
#define RID_OPT_MAVLINK_OP_LOC_LOOP  (1 << 7)
#define RID_OPT_IDENTITY_READY_GATE  (1 << 8)
#define RID_OPT_VERBOSE_TX_LOG       (1 << 9)

/* Max Authentication pages (ASTM F3411-22a) */
#ifndef ODID_AUTH_MAX_PAGES
#define ODID_AUTH_MAX_PAGES 16
#endif
#ifndef ODID_MESSAGE_SIZE
#define ODID_MESSAGE_SIZE 25
#endif
/* Values used by identity readiness gate */
#ifndef INV_ALT
#define INV_ALT  (-1000.0f)
#endif
#define INV_SPEED_H 255
#define INV_SPEED_V 63
#define INV_DIR 361
#ifndef MAX_TIMESTAMP
#define MAX_TIMESTAMP 0xFFFF
#endif

typedef enum {
    RID_PROTOCOL_UNKNOWN = 0,
    RID_PROTOCOL_MAVLINK,
    RID_PROTOCOL_MSP,
    RID_PROTOCOL_NMEA,
    RID_PROTOCOL_NONE,
    RID_PROTOCOL_AUTO = 255,
} rid_protocol_t;

typedef enum {
    RID_TRANSMIT_WIFI_BCN   = (1 << 0),
    RID_TRANSMIT_WIFI_NAN   = (1 << 1),
    RID_TRANSMIT_BLE4       = (1 << 2),
    RID_TRANSMIT_BLE5       = (1 << 3), /* Coded PHY, long-range instance only */
    RID_TRANSMIT_BLE5_EXT1M = (1 << 4), /* same full pack, ordinary 1M PHY instance */
} rid_transmit_mode_t;

typedef struct {
    double latitude;
    double longitude;
    float altitude_msl;
    float altitude_relative;
    float altitude_baro;
    float speed;
    float speed_vertical;
    int16_t heading;
    uint8_t fix_type;
    uint8_t satellites;
    bool armed;
    double operator_lat;
    double operator_lon;
    float operator_alt;
    uint8_t operator_location_type;
} rid_gps_data_t;

typedef struct {
    char uas_id[ESP_RID_MAX_STR_LEN + 1];
    char operator_id[ESP_RID_MAX_STR_LEN + 1];
    char self_id_text[ESP_RID_MAX_STR_LEN + 1];
    uint8_t id_type;
    uint8_t ua_type;
    char uas_id_2[ESP_RID_MAX_STR_LEN + 1];
    uint8_t id_type_2;
    uint8_t ua_type_2;
    /* Self-ID from MAVLink */
    bool has_self_id;
    uint8_t self_id_desc_type;
    /* Authentication from MAVLink relay */
    bool has_ext_auth;
    uint8_t ext_auth_last_page;
    uint16_t ext_auth_pages_received;
    uint8_t ext_auth_pages[ODID_AUTH_MAX_PAGES][ODID_MESSAGE_SIZE];
} rid_identity_t;

typedef struct {
    rid_protocol_t protocol;
    uint8_t uart_port;
    uint32_t baud_rate;
    uint8_t tx_pin;
    uint8_t rx_pin;

    uint8_t ua_type;
    uint8_t id_type;
    char uas_id[ESP_RID_MAX_STR_LEN + 1];
    char operator_id[ESP_RID_MAX_STR_LEN + 1];

    uint8_t ua_type_2;
    uint8_t id_type_2;
    char uas_id_2[ESP_RID_MAX_STR_LEN + 1];

    uint8_t tx_modes;
    uint8_t wifi_channel;
    float wifi_power_dbm;
    float wifi_bcn_rate_hz;
    float wifi_nan_rate_hz;
    float ble4_rate_hz;
    float ble4_power_dbm;
    float ble5_rate_hz;
    float ble5_power_dbm;

    char wifi_ssid[ESP_RID_MAX_STR_LEN + 1];
    char wifi_password[ESP_RID_MAX_STR_LEN + 1];
    uint8_t webserver_en;

    uint8_t mavlink_sysid;
    uint8_t bcast_powerup;

    double operator_lat;
    double operator_lon;
    float operator_alt;
    char self_id_text[ESP_RID_MAX_STR_LEN + 1];

    uint16_t options;
    int8_t lock_level;

    int8_t led_r_gpio;
    int8_t led_g_gpio;
    /* Historically the RGB status LED's blue channel; now an independent
     * "ready to fly" indicator (identity + GPS + active TX) -- see
     * led_status_set_compliance(). Field name kept as led_b_gpio for
     * NVS/API compatibility. */
    int8_t led_b_gpio;

    /* Piezo/magnetic buzzer driven via LEDC PWM tone (-1 = disabled) */
    int8_t buzzer_gpio;

    /* WS2812 addressable RGB LED */
    int8_t ws2812_gpio;
    uint8_t ws2812_brightness;

    /* External GPIO lighting outputs */
    int8_t lighting_pins[5];
    uint8_t lighting_patterns[5];
    int16_t lighting_phase_offsets[5];

    /* DroneCAN */
    int8_t dronecan_rx_gpio;
    int8_t dronecan_tx_gpio;
    uint32_t dronecan_bitrate;

    /* MAVLink USB transport */
    bool mavlink_usb_enable;

    /* OTA trigger GPIO (-1 = disabled) */
    int8_t ota_trigger_gpio;

    /* Authentication private key PEM */
    char auth_private_key[512];

    /* Startup delay before transmission (ms) */
    uint32_t start_delay_ms;

    char public_keys[ESP_RID_NUM_KEYS][ESP_RID_MAX_KEY_LEN + 1];
} rid_config_t;

typedef struct {
    rid_gps_data_t gps;
    rid_identity_t identity;
    rid_protocol_t active_protocol;
    uint32_t last_update_ms;
    uint32_t transmissions_count;
    uint32_t wifi_bcn_count;
    uint32_t wifi_nan_count;
    uint32_t ble4_count;
    uint32_t ble5_count;
    uint32_t ble5_ext1m_count;
    bool gps_valid;

    /* Identity readiness */
    bool identity_ready;

    /* MAVLink status */
    bool mavlink_armed;
    uint32_t mavlink_sysid;

    /* Operator location */
    double operator_lat;
    double operator_lon;
    float operator_alt;
    uint32_t operator_position_updated_ms;
    uint8_t operator_location_type;

    /* Auth status */
    bool auth_enabled;

    /* Home position -- captured once, the first time GPS reaches a
     * stable fix (fix_type >= 2) after power-up. Session-scoped: not
     * persisted to NVS, reset to unset on the next boot. */
    bool home_set;
    double home_lat;
    double home_lon;
} rid_state_t;

void esp_rid_init(void);
void esp_rid_set_config(const rid_config_t *config);
void esp_rid_get_config(rid_config_t *config);
void esp_rid_get_state(rid_state_t *state);
void esp_rid_start(void);
void esp_rid_stop(void);
void esp_rid_factory_reset(void);

/* Returns true if `pin` is reserved/dangerous to repurpose as a general
 * UART/GPIO pin on this specific board (native USB, SPI flash/PSRAM,
 * strapping pins, or the console UART) -- shared by the CLI and web UI
 * backend so both reject the same set of pins consistently. */
bool esp_rid_is_reserved_gpio(int pin);

/* Returns true if `pin` (>= 0) is already claimed by another configured
 * GPIO in `cfg`. `field_name` identifies the field being checked so it
 * can exclude itself (e.g. re-saving a pin's own unchanged value must
 * not report a conflict against itself). On a conflict, if
 * `conflict_name` is non-NULL it is set to the name of the field the
 * pin collides with. Used to warn (not block) about pin reuse across
 * independently-configurable features -- unlike esp_rid_is_reserved_gpio(),
 * which blocks pins that are unsafe to use at all. */
bool esp_rid_gpio_conflicts(const rid_config_t *cfg, int pin, const char *field_name,
                             const char **conflict_name);

/* Reads the ESP32-S3's internal die temperature sensor. Returns NAN if
 * the sensor failed to initialize (e.g. driver/hardware unavailable) --
 * callers should check with isnan() before using the result. */
float esp_rid_get_cpu_temp(void);

#endif
