#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "esp_efuse.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "wifi_tx.h"
#include "opendroneid.h"
#include "odid_wifi.h"
#include "esp_remote_id.h"

#define TAG "WIFI_TX"

static bool g_initialized = false;
/* Serializes access to the WiFi radio between the continuous transmit
 * loop (rid_task(), calling wifi_tx_transmit()/wifi_tx_transmit_nan()
 * repeatedly on its own schedule) and wifi_tx_reconfigure_ap() (called
 * from the HTTP server's task whenever config is saved). Without this,
 * esp_wifi_stop() could run concurrently with an in-flight
 * esp_wifi_80211_tx() call from the other task -- an unsynchronized
 * stop/transmit race on the same WiFi driver state, which is a
 * plausible cause of both an unreliable reconfigure and AP instability
 * observed after adding live reconfiguration. */
static SemaphoreHandle_t g_wifi_lock = NULL;
static uint8_t g_mac[6];
static uint8_t g_message_counter = 0;
static ODID_UAS_Data g_uas_data;


static void generate_random_mac(uint8_t mac[6])
{
    for (int i = 0; i < 6; i++) {
        mac[i] = (uint8_t)(esp_random() & 0xFF);
    }
    mac[0] |= 0x02;
    mac[0] &= 0xFE;
}

static void read_mac_from_efuse(uint8_t mac[6])
{
    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err != ESP_OK || (mac[0] == 0 && mac[1] == 0 && mac[2] == 0)) {
        ESP_LOGW(TAG, "eFuse MAC CRC error — using random MAC");
        generate_random_mac(mac);
    }
}

static ODID_Horizontal_accuracy_t horiz_acc_from_gps(uint8_t fix_type, uint8_t satellites)
{
    if (fix_type >= 4 && satellites >= 15) return ODID_HOR_ACC_1_METER;
    if (fix_type >= 4 && satellites >= 10) return ODID_HOR_ACC_3_METER;
    if (fix_type >= 4) return ODID_HOR_ACC_10_METER;
    if (fix_type >= 3) return ODID_HOR_ACC_10_METER;
    return ODID_HOR_ACC_30_METER;
}

static ODID_Vertical_accuracy_t vert_acc_from_gps(uint8_t fix_type, uint8_t satellites)
{
    if (fix_type >= 4 && satellites >= 15) return ODID_VER_ACC_3_METER;
    if (fix_type >= 4 && satellites >= 10) return ODID_VER_ACC_10_METER;
    if (fix_type >= 4) return ODID_VER_ACC_25_METER;
    if (fix_type >= 3) return ODID_VER_ACC_25_METER;
    return ODID_VER_ACC_45_METER;
}

/* Builds a wifi_config_t from the current config (SSID/password/channel)
 * and applies + starts it. Shared by wifi_tx_init() (first bring-up) and
 * wifi_tx_reconfigure_ap() (live reconfiguration after a web UI/CLI
 * change), so both paths stay in sync instead of drifting apart. */
static void apply_ap_config_and_start(void)
{
    rid_config_t rcfg;
    esp_rid_get_config(&rcfg);

    wifi_config_t ap_config = { .ap = {
        .channel = (rcfg.wifi_channel >= 1 && rcfg.wifi_channel <= 13) ? rcfg.wifi_channel : 6,
        .max_connection = 4,
        .beacon_interval = 100,
    } };

    const char *ssid = (rcfg.wifi_ssid[0] != '\0') ? rcfg.wifi_ssid : "ESP-RID";
    size_t ssid_len = strlen(ssid);
    if (ssid_len > sizeof(ap_config.ap.ssid)) ssid_len = sizeof(ap_config.ap.ssid);
    memcpy(ap_config.ap.ssid, ssid, ssid_len);
    ap_config.ap.ssid_len = (uint8_t)ssid_len;

    if (rcfg.wifi_password[0] != '\0' && strlen(rcfg.wifi_password) >= 8) {
        ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
        strncpy((char *)ap_config.ap.password, rcfg.wifi_password, sizeof(ap_config.ap.password) - 1);
    } else {
        /* WPA2-PSK requires an 8+ character password; fall back to open
         * rather than silently rejecting esp_wifi_set_config() below. */
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(err));
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        /* This function is shared with wifi_tx_reconfigure_ap(), which
         * calls it while WiFi is already running (deliberately, since we
         * no longer stop/restart the interface to reconfigure it) -- an
         * "already started" failure here is expected and harmless in
         * that case, not a real fault, hence WARN rather than ERROR. */
        ESP_LOGW(TAG, "esp_wifi_start: %s (harmless if AP was already running)", esp_err_to_name(err));
    }

    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW20);

    float power_dbm = (rcfg.wifi_power_dbm >= 2.0f && rcfg.wifi_power_dbm <= 20.0f) ? rcfg.wifi_power_dbm : 20.0f;
    esp_wifi_set_max_tx_power((int8_t)(power_dbm * 4.0f)); /* esp_wifi API units are 0.25 dBm */

    ESP_LOGI(TAG, "WiFi AP: SSID=\"%s\" channel=%d auth=%s power=%.1fdBm",
             ssid, ap_config.ap.channel,
             (ap_config.ap.authmode == WIFI_AUTH_OPEN) ? "open" : "WPA2-PSK",
             (double)power_dbm);
}

void wifi_tx_init(void)
{
    if (g_initialized) return;

    if (!g_wifi_lock) {
        g_wifi_lock = xSemaphoreCreateMutex();
    }

    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop: %s", esp_err_to_name(ret));
    }

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    read_mac_from_efuse(g_mac);
    esp_base_mac_addr_set(g_mac);

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    apply_ap_config_and_start();

    g_initialized = true;
    ESP_LOGI(TAG, "WiFi TX initialized, MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             g_mac[0], g_mac[1], g_mac[2],
             g_mac[3], g_mac[4], g_mac[5]);
}

/* Live-reconfigures the AP (SSID/password/channel/power) without a device
 * reboot: stop the running AP, rebuild and apply the wifi_config_t from
 * whatever's now in config, and restart it. Only the one-time driver
 * bring-up (esp_wifi_init, netif creation, event loop, MAC) is skipped --
 * that must not be repeated on an already-initialized driver. Safe to
 * call repeatedly; a no-op if wifi_tx_init() hasn't run yet. */
void wifi_tx_reconfigure_ap(void)
{
    if (!g_initialized) return;

    if (g_wifi_lock && xSemaphoreTake(g_wifi_lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "reconfigure: could not acquire WiFi lock, aborting to avoid a race");
        return;
    }

    /* Explicitly deauth any currently-connected stations and give the
     * disconnect time to fully process BEFORE applying new credentials.
     * This is a documented ESP-IDF race condition (see e.g. esp-idf
     * issue #9973 and multiple ESP32 forum reports): if a station
     * reconnects before the AP has finished recognizing its PREVIOUS
     * disconnection, the new station's internal "bss" structure can
     * collide with incompletely-cleaned-up state from the old one,
     * corrupting the heap inside ap_sta_add().
     *
     * Increased from 200ms to 500ms after a separate crash was traced
     * to ieee80211_ampdu_age_all()/ieee80211_ampdu_request() -- the
     * driver's own internal per-station frame-aggregation (block-ack)
     * session bookkeeping -- happening while a station was ACTIVELY
     * transferring data at the moment of reconfigure. That internal
     * state likely needs more time to fully unwind than a station that
     * was merely idle-connected; this is a closed-source driver
     * internal, so this wait is an evidence-based mitigation rather
     * than a fix at the actual source. */
    esp_wifi_deauth_sta(0); /* aid 0 = deauth all connected stations */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* Deliberately NOT calling esp_wifi_stop() here. Doing so tears down
     * the entire netif/lwIP state for this interface while it may still
     * be processing in-flight network traffic (DHCP, UDP, etc.) from
     * connected clients -- entirely outside this module's control, since
     * it's driven by lwIP's own interrupt-context processing. That
     * teardown-while-in-use race is what caused a StoreProhibited crash
     * deep inside lwIP's recv_udp(), not anything in this project's own
     * code. esp_wifi_set_config() on an AP that's already running
     * applies live: SSID/channel/etc. changes take effect on the next
     * beacon, and connected stations naturally drop and reconnect once
     * they notice -- without ever tearing down the network stack
     * underneath whatever's currently using it. */
    apply_ap_config_and_start();

    ESP_LOGI(TAG, "WiFi AP reconfigured live (no stop/start, no reboot)");

    if (g_wifi_lock) xSemaphoreGive(g_wifi_lock);
}

static TaskHandle_t g_reconfig_task_handle = NULL;

static void reconfigure_task(void *arg)
{
    while (1) {
        /* Block indefinitely until wifi_tx_reconfigure_ap_deferred() wakes
         * us. This task is created once and never exits -- esp_wifi_*
         * calls (set_config/start) only queue work to the WiFi driver's
         * own internal task asynchronously, and deleting the calling task
         * immediately afterward (the previous one-shot design) could
         * race with the driver still holding an internal reference to
         * it, causing a later unrelated WiFi operation to dereference a
         * freed task control block. Never exiting removes that window
         * entirely instead of just narrowing it with more delay. */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Give the HTTP server time to fully close out the connection
         * this change arrived on before touching WiFi config -- calling
         * wifi_tx_reconfigure_ap() synchronously from inside the httpd
         * request handler itself was corrupting the httpd's
         * connection-tracking state. */
        vTaskDelay(pdMS_TO_TICKS(500));
        wifi_tx_reconfigure_ap();
    }
}

void wifi_tx_reconfigure_ap_deferred(void)
{
    if (!g_reconfig_task_handle) {
        xTaskCreate(reconfigure_task, "wifi_recfg", 4096, NULL, 5, &g_reconfig_task_handle);
    }
    if (g_reconfig_task_handle) {
        xTaskNotifyGive(g_reconfig_task_handle);
    }
}

void wifi_tx_get_mac(uint8_t mac[6])
{
    memcpy(mac, g_mac, 6);
}

static void populate_uas_data(ODID_UAS_Data *d, rid_gps_data_t *gps, rid_identity_t *identity)
{
    memset(d, 0, sizeof(ODID_UAS_Data));

    d->BasicIDValid[0] = 1;
    d->BasicID[0].IDType = (ODID_idtype_t)identity->id_type;
    d->BasicID[0].UAType = (ODID_uatype_t)identity->ua_type;
    strncpy((char *)d->BasicID[0].UASID, identity->uas_id, ODID_ID_SIZE);

    if (identity->uas_id_2[0] != '\0') {
        d->BasicIDValid[1] = 1;
        d->BasicID[1].IDType = (ODID_idtype_t)identity->id_type_2;
        d->BasicID[1].UAType = (ODID_uatype_t)identity->ua_type_2;
        strncpy((char *)d->BasicID[1].UASID, identity->uas_id_2, ODID_ID_SIZE);
    }

    d->LocationValid = 1;
    d->Location.Latitude = gps->latitude;
    d->Location.Longitude = gps->longitude;
    d->Location.AltitudeGeo = gps->altitude_msl;
    d->Location.Height = gps->altitude_relative;
    d->Location.SpeedHorizontal = gps->speed;
    d->Location.Direction = gps->heading;
    d->Location.SpeedVertical = gps->speed_vertical;
    d->Location.HorizAccuracy = horiz_acc_from_gps(gps->fix_type, gps->satellites);
    d->Location.VertAccuracy = vert_acc_from_gps(gps->fix_type, gps->satellites);

    d->SystemValid = 1;
    d->System.OperatorLatitude = gps->operator_lat;
    d->System.OperatorLongitude = gps->operator_lon;
    d->System.OperatorLocationType = (ODID_operator_location_type_t)gps->operator_location_type;
    d->System.AreaCount = 0;
    d->System.AreaRadius = 0;

    if (identity->self_id_text[0] != '\0') {
        d->SelfIDValid = 1;
        d->SelfID.DescType = ODID_DESC_TYPE_TEXT;
        strncpy((char *)d->SelfID.Desc, identity->self_id_text, ODID_STR_SIZE);
    }

    d->OperatorIDValid = 1;
    strncpy((char *)d->OperatorID.OperatorId, identity->operator_id, ODID_ID_SIZE);
}

bool wifi_tx_transmit(rid_gps_data_t *gps, rid_identity_t *identity)
{
    if (!g_initialized || !gps || !identity) return false;

    populate_uas_data(&g_uas_data, gps, identity);

    /* Embed the real configured SSID (same fallback as the actual AP
     * config in apply_ap_config_and_start()) instead of a fixed string,
     * so this beacon payload's claimed network name always matches what
     * the AP itself is really broadcasting. */
    rid_config_t rcfg;
    esp_rid_get_config(&rcfg);
    const char *ssid = (rcfg.wifi_ssid[0] != '\0') ? rcfg.wifi_ssid : "ESP-RID";
    uint8_t ssid_len = (uint8_t)strlen(ssid);
    if (ssid_len > 32) ssid_len = 32; /* 802.11 SSID field max; unreachable given wifi_ssid's own 20-char config limit, kept as a defensive bound */

    static uint8_t buffer[1024];
    uint8_t counter = g_message_counter++;
    int length = odid_wifi_build_message_pack_beacon_frame(
        &g_uas_data, (char *)g_mac,
        ssid, ssid_len, 100, counter,
        buffer, sizeof(buffer));

    if (length > 0) {
        if (g_wifi_lock && xSemaphoreTake(g_wifi_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
            /* AP is mid-reconfigure; skip this cycle rather than risk a
             * concurrent esp_wifi_80211_tx() during esp_wifi_stop(). */
            return false;
        }
        /* 4-attempt TX fallback: try STA/AP × no-seq/with-seq */
        static const wifi_interface_t ifaces[] = { WIFI_IF_AP, WIFI_IF_STA, WIFI_IF_AP, WIFI_IF_STA };
        static const bool seqs[] = { false, false, true, true };
        bool ok = false;
        for (int attempt = 0; attempt < 4; attempt++) {
            esp_err_t ret = esp_wifi_80211_tx(ifaces[attempt], buffer, length, seqs[attempt]);
            if (ret == ESP_OK) { ok = true; break; }
        }
        if (g_wifi_lock) xSemaphoreGive(g_wifi_lock);
        if (!ok) {
            ESP_LOGW(TAG, "TX failed after 4 attempts");
        }
        return ok;
    }

    return false;
}

bool wifi_tx_transmit_nan(rid_gps_data_t *gps, rid_identity_t *identity, uint8_t counter)
{
    if (!g_initialized || !gps || !identity) return false;

    populate_uas_data(&g_uas_data, gps, identity);

    static uint8_t buffer[1024];
    int length = odid_wifi_build_message_pack_nan_action_frame(
        &g_uas_data, (char *)g_mac,
        counter, buffer, sizeof(buffer));

    if (length > 0) {
        if (g_wifi_lock && xSemaphoreTake(g_wifi_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
            return false;
        }
        esp_err_t ret = esp_wifi_80211_tx(WIFI_IF_AP, buffer, length, true);
        if (g_wifi_lock) xSemaphoreGive(g_wifi_lock);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "NAN TX failed: %s", esp_err_to_name(ret));
            return false;
        }
        return true;
    }

    return false;
}
