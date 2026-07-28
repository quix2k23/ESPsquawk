#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "soc/soc_caps.h"
#if defined(CONFIG_BT_BLUEDROID_ENABLED) && defined(SOC_BT_SUPPORTED)
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#endif
#include "ble_tx.h"
#include "opendroneid.h"

#define TAG "BLE_TX"

#define RID_SERVICE_UUID 0xFFFA

static bool g_initialized = false;
static bool g_legacy_adv_started = false;
static bool g_lr_adv_started = false;
static bool g_ext1m_adv_started = false;

/* Three concurrent BLE5 extended-advertising instances, all run
 * continuously and simultaneously once started in ble_tx_init():
 *  - instance 0 is flagged LEGACY_NONCONN (legacy ADV_NONCONN_IND
 *    equivalent), so BLE 4.2-only scanners with no extended-advertising
 *    support can still see it. Payload cycles one message type per
 *    broadcast (31-byte legacy payload limit) -- see s_legacy_schedule.
 *  - instance 1 uses real BLE5 extended-advertising properties on the
 *    Coded PHY for long-range / larger-payload broadcast: the full
 *    message pack (including Basic ID) every single broadcast, no
 *    cycling needed.
 *  - instance 2 broadcasts that exact same full pack again, but on
 *    ordinary 1M PHY instead of Coded PHY. Coded PHY reception is
 *    optional in the Bluetooth 5.0 spec, so plenty of phones that
 *    support basic BLE5 Extended Advertising still can't receive
 *    instance 1 at all -- this instance is a compatibility fallback
 *    that lets them see a complete pack too, not just whatever the
 *    cycled Legacy instance happens to be showing at that moment.
 * ble_tx_transmit_legacy()/ble_tx_transmit_lr() only refresh each
 * instance's payload afterward -- they do not stop/restart advertising
 * every cycle, which would otherwise interrupt continuous broadcast on
 * all three sets every time any one's data is refreshed. This requires
 * ESP_BLE_GAP_SET_EXT_ADV_PROP_LEGACY_NONCONN, confirmed present in
 * esp-idf's esp_gap_ble_api.h (maps to ADV_NONCONN_IND), and only needs
 * CONFIG_BT_BLE_50_EXTEND_ADV_EN -- BLE 4.2 mode is not used at all by
 * this file anymore. */
#define BLE_ADV_INSTANCE_LEGACY 0
#define BLE_ADV_INSTANCE_LR     1
#define BLE_ADV_INSTANCE_EXT_1M 2

#if defined(CONFIG_BT_BLUEDROID_ENABLED) && defined(SOC_BT_SUPPORTED) && defined(CONFIG_BT_BLE_50_EXTEND_ADV_EN)
static ODID_UAS_Data g_uas_data;

/* Legacy-compatible instance payload: one Service Data AD structure
 * holding a single complete ODID_MESSAGE_SIZE-byte message. No
 * Flags/UUID-list AD -- legacy advertising has a hard 31-byte payload
 * limit (Bluetooth Core Spec), and 4-byte header + 25-byte message = 29
 * already leaves only 2 bytes of margin; a 3-byte Flags AD would push a
 * single message over the limit entirely. */
static uint8_t g_legacy_adv[1 + 1 + 2 + ODID_MESSAGE_SIZE];

/* Long-range instance payload: the full multi-message pack. Sized to
 * match the pack_buf this file already builds elsewhere. */
static uint8_t g_lr_adv[4 + (ODID_PACK_MAX_MESSAGES * ODID_MESSAGE_SIZE + 8)];

/* Same full pack, broadcast again on ordinary 1M PHY -- see instance 2
 * in the comment above. */
static uint8_t g_ext1m_adv[4 + (ODID_PACK_MAX_MESSAGES * ODID_MESSAGE_SIZE + 8)];

static ODID_Horizontal_accuracy_t ble_horiz_acc(uint8_t fix_type, uint8_t satellites)
{
    if (fix_type >= 4 && satellites >= 15) return ODID_HOR_ACC_1_METER;
    if (fix_type >= 4 && satellites >= 10) return ODID_HOR_ACC_3_METER;
    if (fix_type >= 3) return ODID_HOR_ACC_10_METER;
    return ODID_HOR_ACC_30_METER;
}

static ODID_Vertical_accuracy_t ble_vert_acc(uint8_t fix_type, uint8_t satellites)
{
    if (fix_type >= 4 && satellites >= 15) return ODID_VER_ACC_3_METER;
    if (fix_type >= 4 && satellites >= 10) return ODID_VER_ACC_10_METER;
    if (fix_type >= 3) return ODID_VER_ACC_25_METER;
    return ODID_VER_ACC_45_METER;
}

static void populate_uas_data(ODID_UAS_Data *d, rid_gps_data_t *gps, rid_identity_t *identity)
{
    memset(d, 0, sizeof(*d));

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
    d->Location.SpeedVertical = gps->speed_vertical;
    d->Location.Direction = gps->heading;
    d->Location.HorizAccuracy = ble_horiz_acc(gps->fix_type, gps->satellites);
    d->Location.VertAccuracy = ble_vert_acc(gps->fix_type, gps->satellites);

    if (identity->self_id_text[0] != '\0') {
        d->SelfIDValid = 1;
        d->SelfID.DescType = ODID_DESC_TYPE_TEXT;
        strncpy((char *)d->SelfID.Desc, identity->self_id_text, ODID_STR_SIZE);
    }

    d->SystemValid = 1;
    d->System.OperatorLatitude = gps->operator_lat;
    d->System.OperatorLongitude = gps->operator_lon;
    d->System.OperatorLocationType = (ODID_operator_location_type_t)gps->operator_location_type;

    d->OperatorIDValid = 1;
    strncpy((char *)d->OperatorID.OperatorId, identity->operator_id, ODID_ID_SIZE);
}

/* ---------- Legacy-compatible instance: one complete message per call, cycling ---------- */

typedef enum {
    LEGACY_MSG_BASIC_ID = 0,
    LEGACY_MSG_LOCATION,
    LEGACY_MSG_SYSTEM,
    LEGACY_MSG_OPERATOR_ID,
    LEGACY_MSG_SELF_ID,
} legacy_msg_type_t;

/* Location is the dynamic message (position/speed/heading) and per ASTM
 * F3411 must refresh faster than the static fields, so it's scheduled
 * every other slot, keeping the same 50%-Location split as the
 * reference mav2odid.c scheduler (see droneidSchedule in mav2odid.c).
 * That reference divides its "static" half evenly across Basic ID +
 * up to 16 Auth pages + Self-ID + System + Operator ID -- this project
 * never populates Auth messages, so rather than waste that freed-up
 * capacity on a strict round-robin of the 4 remaining types, Basic ID
 * (the actual UAS ID -- the single most important thing for a scanner
 * to see quickly) gets 3 of the 6 static slots instead of 1, cutting
 * its worst-case wait from 8 broadcast cycles down to 4. */
static const legacy_msg_type_t s_legacy_schedule[] = {
    LEGACY_MSG_BASIC_ID,    LEGACY_MSG_LOCATION,
    LEGACY_MSG_SYSTEM,      LEGACY_MSG_LOCATION,
    LEGACY_MSG_BASIC_ID,    LEGACY_MSG_LOCATION,
    LEGACY_MSG_OPERATOR_ID, LEGACY_MSG_LOCATION,
    LEGACY_MSG_BASIC_ID,    LEGACY_MSG_LOCATION,
    LEGACY_MSG_SELF_ID,     LEGACY_MSG_LOCATION,
};
#define LEGACY_SCHEDULE_LEN (sizeof(s_legacy_schedule) / sizeof(s_legacy_schedule[0]))
static uint8_t s_legacy_sched_idx = 0;

/* Encodes exactly one message of the given type into a properly-typed
 * union member (mirroring the pattern mav2odid.c itself uses, and in
 * fact identical member-for-member to opendroneid.h's own
 * ODID_Message_encoded union), then copies out the raw
 * ODID_MESSAGE_SIZE-byte wire representation. Returns false if that
 * message type has no valid data right now. */
static bool encode_one_message(ODID_UAS_Data *d, legacy_msg_type_t type, uint8_t out[ODID_MESSAGE_SIZE])
{
    union {
        ODID_BasicID_encoded basicId;
        ODID_Location_encoded location;
        ODID_System_encoded system;
        ODID_OperatorID_encoded operatorId;
        ODID_SelfID_encoded selfId;
    } enc;
    int ret;

    switch (type) {
    case LEGACY_MSG_BASIC_ID:
        if (!d->BasicIDValid[0]) return false;
        ret = encodeBasicIDMessage(&enc.basicId, &d->BasicID[0]);
        break;
    case LEGACY_MSG_LOCATION:
        if (!d->LocationValid) return false;
        ret = encodeLocationMessage(&enc.location, &d->Location);
        break;
    case LEGACY_MSG_SYSTEM:
        if (!d->SystemValid) return false;
        ret = encodeSystemMessage(&enc.system, &d->System);
        break;
    case LEGACY_MSG_OPERATOR_ID:
        if (!d->OperatorIDValid) return false;
        ret = encodeOperatorIDMessage(&enc.operatorId, &d->OperatorID);
        break;
    case LEGACY_MSG_SELF_ID:
        if (!d->SelfIDValid) return false;
        ret = encodeSelfIDMessage(&enc.selfId, &d->SelfID);
        break;
    default:
        return false;
    }

    if (ret != ODID_SUCCESS) return false;
    memcpy(out, &enc, ODID_MESSAGE_SIZE);
    return true;
}

/* Builds one legacy-compatible advertising payload containing exactly
 * one complete, valid, correctly-sized ODID message -- never a
 * truncated fragment and never more than fits in a legacy advertisement.
 * Cycles through message types across calls so every field eventually
 * goes out, weighted toward Location. */
static bool build_legacy_adv(rid_gps_data_t *gps, rid_identity_t *identity, uint8_t *buf, uint16_t *len)
{
    populate_uas_data(&g_uas_data, gps, identity);

    uint8_t msg[ODID_MESSAGE_SIZE];
    bool got = false;

    for (unsigned tries = 0; tries < LEGACY_SCHEDULE_LEN; tries++) {
        legacy_msg_type_t type = s_legacy_schedule[s_legacy_sched_idx];
        s_legacy_sched_idx = (uint8_t)((s_legacy_sched_idx + 1) % LEGACY_SCHEDULE_LEN);
        if (encode_one_message(&g_uas_data, type, msg)) {
            got = true;
            break;
        }
    }

    /* populate_uas_data() always sets LocationValid=1, so in practice this
     * loop always finds something; this is just a defensive fallback. */
    if (!got) return false;

    uint16_t idx = 0;
    buf[idx++] = 1 + 2 + ODID_MESSAGE_SIZE;   /* AD length: type + uuid16 + message */
    buf[idx++] = 0x16;                        /* AD type: Service Data - 16-bit UUID */
    buf[idx++] = (uint8_t)(RID_SERVICE_UUID & 0xFF);
    buf[idx++] = (uint8_t)(RID_SERVICE_UUID >> 8);
    memcpy(buf + idx, msg, ODID_MESSAGE_SIZE);
    idx += ODID_MESSAGE_SIZE;

    *len = idx;
    return true;
}

/* ---------- Long-range instance: full multi-message pack ---------- */

/* Builds one long-range advertising payload containing the COMPLETE
 * message pack (Basic ID + Location + Self-ID + System + Operator ID),
 * using the capacity extended advertising actually provides. */
static bool build_full_pack_adv(rid_gps_data_t *gps, rid_identity_t *identity,
                                 uint8_t *buf, uint16_t *len, size_t buf_size)
{
    populate_uas_data(&g_uas_data, gps, identity);

    uint8_t pack_buf[ODID_PACK_MAX_MESSAGES * ODID_MESSAGE_SIZE + 8];
    int pack_len = odid_message_build_pack(&g_uas_data, pack_buf, sizeof(pack_buf));
    if (pack_len <= 0) {
        ESP_LOGW(TAG, "odid_message_build_pack failed (%d)", pack_len);
        return false;
    }
    /* AD length field is a single byte (max 255); 3 of those bytes are our
     * own type+uuid16 header, leaving 252 for the pack. This project never
     * populates Auth messages here, so realistic packs (BasicID x2 +
     * Location + SelfID + System + OperatorID = 6 x 25B = 150B) are well
     * under that -- this is a defensive guard, not an expected path. */
    if (pack_len > 252) {
        ESP_LOGE(TAG, "ODID pack too large for one AD structure (%d bytes)", pack_len);
        return false;
    }

    size_t needed = 1 + 1 + 2 + (size_t)pack_len; /* len + type + uuid16 + pack */
    if (needed > buf_size) {
        ESP_LOGE(TAG, "LR adv buffer too small: need %u, have %u", (unsigned)needed, (unsigned)buf_size);
        return false;
    }

    uint16_t idx = 0;
    buf[idx++] = (uint8_t)(1 + 2 + pack_len);
    buf[idx++] = 0x16;
    buf[idx++] = (uint8_t)(RID_SERVICE_UUID & 0xFF);
    buf[idx++] = (uint8_t)(RID_SERVICE_UUID >> 8);
    memcpy(buf + idx, pack_buf, (size_t)pack_len);
    idx += (uint16_t)pack_len;

    *len = idx;
    return true;
}

/* Brings up one extended-advertising instance: set params, seed it with
 * a well-formed placeholder payload (built from zeroed gps/identity, so
 * it's already broadcasting valid-format data before the first real GPS
 * fix arrives), then start it running continuously (duration=0,
 * max_events=0). Called once from ble_tx_init(); never stopped/restarted
 * afterward -- transmit calls only refresh the payload via
 * esp_ble_gap_config_ext_adv_data_raw(). */
static bool start_adv_instance(uint8_t instance, const esp_ble_gap_ext_adv_params_t *params,
                                uint8_t *buf, size_t buf_size, bool is_legacy, const char *label)
{
    esp_err_t err = esp_ble_gap_ext_adv_set_params(instance, params);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "instance %d set_params failed: %s", instance, esp_err_to_name(err));
        return false;
    }

    /* Confirmed missing entirely by comparing against Espressif's own
     * official multi-adv example (examples/bluetooth/bluedroid/ble_50/
     * multi-adv/main/multi_adv_demo.c): whenever own_addr_type is
     * BLE_ADDR_TYPE_RANDOM (as ours is), a random address must be
     * explicitly assigned to the advertising instance before it can be
     * enabled. Without this, the controller has no valid address to
     * advertise with and rejects the later Enable command with Invalid
     * Param (0x12) -- on BOTH instances identically, since neither ever
     * had one assigned. */
    esp_bd_addr_t rand_addr;
    esp_ble_gap_addr_create_static(rand_addr);
    err = esp_ble_gap_ext_adv_set_rand_addr(instance, rand_addr);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "instance %d set_rand_addr failed: %s", instance, esp_err_to_name(err));
    } else {
        /* This address is what a generic BLE scanner (nRF Connect, etc.)
         * will show for this advertiser -- it's a freshly-generated
         * random static address, NOT the device's WiFi MAC, and there
         * was previously no way to know it without scrolling through
         * every scanned device's raw advertising data by hand. */
        ESP_LOGI(TAG, "%s BLE advertising address: %02x:%02x:%02x:%02x:%02x:%02x",
                 label, rand_addr[0], rand_addr[1], rand_addr[2], rand_addr[3], rand_addr[4], rand_addr[5]);
    }

    rid_gps_data_t zero_gps;
    rid_identity_t zero_identity;
    memset(&zero_gps, 0, sizeof(zero_gps));
    memset(&zero_identity, 0, sizeof(zero_identity));

    uint16_t len = 0;
    bool built = is_legacy
        ? build_legacy_adv(&zero_gps, &zero_identity, buf, &len)
        : build_full_pack_adv(&zero_gps, &zero_identity, buf, &len, buf_size);

    if (built) {
        err = esp_ble_gap_config_ext_adv_data_raw(instance, len, buf);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "instance %d initial data config failed: %s", instance, esp_err_to_name(err));
        }
    }

    esp_ble_gap_ext_adv_t adv = { .instance = instance, .duration = 0, .max_events = 0 };
    err = esp_ble_gap_ext_adv_start(1, &adv);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "instance %d start failed: %s", instance, esp_err_to_name(err));
        return false;
    }

    return true;
}
#endif

void ble_tx_init(void)
{
    if (g_initialized) return;
#if defined(CONFIG_BT_BLUEDROID_ENABLED) && defined(SOC_BT_SUPPORTED)
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if (esp_bt_controller_init(&bt_cfg) != ESP_OK) return;
    if (esp_bt_controller_enable(ESP_BT_MODE_BLE) != ESP_OK) return;
    if (esp_bluedroid_init() != ESP_OK) return;
    if (esp_bluedroid_enable() != ESP_OK) return;

    g_initialized = true;
    ESP_LOGI(TAG, "BLE initialized");

#if defined(CONFIG_BT_BLE_50_EXTEND_ADV_EN)
    {
        /* Legacy-compatible instance: LEGACY_NONCONN (ADV_NONCONN_IND
         * equivalent) on the 1M PHY -- BLE 4.2-only scanners cannot
         * receive Coded PHY at all, so this must stay on 1M regardless
         * of the long-range instance's PHY choice below. */
        esp_ble_gap_ext_adv_params_t legacy_params = {
            .type = ESP_BLE_GAP_SET_EXT_ADV_PROP_LEGACY_NONCONN,
            .interval_min = 0x100,
            .interval_max = 0x100,
            .channel_map = ADV_CHNL_ALL,
            .own_addr_type = BLE_ADDR_TYPE_RANDOM,
            .primary_phy = ESP_BLE_GAP_PRI_PHY_1M,
            .secondary_phy = ESP_BLE_GAP_PHY_1M,
            .sid = 0,
            .scan_req_notif = false,
        };
        g_legacy_adv_started = start_adv_instance(BLE_ADV_INSTANCE_LEGACY, &legacy_params,
                                                   g_legacy_adv, sizeof(g_legacy_adv), true,
                                                   "Legacy (BLE 4.0)");
        if (g_legacy_adv_started) {
            ESP_LOGI(TAG, "Legacy-compatible BLE advertising instance started");
        }
    }
    {
        /* Long-range instance: real BLE5 extended-advertising properties
         * on the Coded PHY, running concurrently with the instance above. */
        esp_ble_gap_ext_adv_params_t lr_params = {
            .type = ESP_BLE_GAP_SET_EXT_ADV_PROP_NONCONN_NONSCANNABLE_UNDIRECTED,
            .interval_min = 0x100,
            .interval_max = 0x100,
            .channel_map = ADV_CHNL_ALL,
            .own_addr_type = BLE_ADDR_TYPE_RANDOM,
            .primary_phy = ESP_BLE_GAP_PRI_PHY_CODED,
            .secondary_phy = ESP_BLE_GAP_PHY_CODED,
            .sid = 1,
            .scan_req_notif = false,
        };
        g_lr_adv_started = start_adv_instance(BLE_ADV_INSTANCE_LR, &lr_params,
                                               g_lr_adv, sizeof(g_lr_adv), false,
                                               "Long-Range (BLE 5.0, Coded PHY)");
        if (g_lr_adv_started) {
            ESP_LOGI(TAG, "Long-range BLE advertising instance started");
        }
    }
    {
        /* Same full pack as the long-range instance above, but on
         * ordinary 1M PHY instead of Coded PHY -- see the instance-2
         * explanation in the comment block near the top of this file. */
        esp_ble_gap_ext_adv_params_t ext1m_params = {
            .type = ESP_BLE_GAP_SET_EXT_ADV_PROP_NONCONN_NONSCANNABLE_UNDIRECTED,
            .interval_min = 0x100,
            .interval_max = 0x100,
            .channel_map = ADV_CHNL_ALL,
            .own_addr_type = BLE_ADDR_TYPE_RANDOM,
            .primary_phy = ESP_BLE_GAP_PRI_PHY_1M,
            .secondary_phy = ESP_BLE_GAP_PHY_1M,
            .sid = 2,
            .scan_req_notif = false,
        };
        g_ext1m_adv_started = start_adv_instance(BLE_ADV_INSTANCE_EXT_1M, &ext1m_params,
                                                  g_ext1m_adv, sizeof(g_ext1m_adv), false,
                                                  "Extended (BLE 5.0, 1M PHY)");
        if (g_ext1m_adv_started) {
            ESP_LOGI(TAG, "Extended 1M-PHY BLE advertising instance started");
        }
    }
#endif
#else
    ESP_LOGW(TAG, "BLE not available on this target");
#endif
}

bool ble_tx_transmit_legacy(rid_gps_data_t *gps, rid_identity_t *identity)
{
    if (!g_initialized || !gps || !identity) return false;

#if defined(CONFIG_BT_BLUEDROID_ENABLED) && defined(SOC_BT_SUPPORTED) && defined(CONFIG_BT_BLE_50_EXTEND_ADV_EN)
    if (!g_legacy_adv_started) return false;

    uint16_t len;
    if (!build_legacy_adv(gps, identity, g_legacy_adv, &len)) {
        return false;
    }

    esp_err_t err = esp_ble_gap_config_ext_adv_data_raw(BLE_ADV_INSTANCE_LEGACY, len, g_legacy_adv);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "legacy adv data update failed: %s", esp_err_to_name(err));
        return false;
    }

    return true;
#else
    return false;
#endif
}

bool ble_tx_transmit_lr(rid_gps_data_t *gps, rid_identity_t *identity)
{
    if (!g_initialized || !gps || !identity) return false;

#if defined(CONFIG_BT_BLUEDROID_ENABLED) && defined(SOC_BT_SUPPORTED) && defined(CONFIG_BT_BLE_50_EXTEND_ADV_EN)
    /* Refreshes only the Coded-PHY long-range instance. */
    if (g_lr_adv_started) {
        uint16_t len;
        if (build_full_pack_adv(gps, identity, g_lr_adv, &len, sizeof(g_lr_adv))) {
            esp_err_t err = esp_ble_gap_config_ext_adv_data_raw(BLE_ADV_INSTANCE_LR, len, g_lr_adv);
            if (err == ESP_OK) {
                return true;
            } else {
                ESP_LOGW(TAG, "LR adv data update failed: %s", esp_err_to_name(err));
            }
        }
    }

    return false;
#else
    return false;
#endif
}

bool ble_tx_transmit_ext1m(rid_gps_data_t *gps, rid_identity_t *identity)
{
    if (!g_initialized || !gps || !identity) return false;

#if defined(CONFIG_BT_BLUEDROID_ENABLED) && defined(SOC_BT_SUPPORTED) && defined(CONFIG_BT_BLE_50_EXTEND_ADV_EN)
    /* Refreshes only the ordinary 1M-PHY extended-advertising instance --
     * a compatibility fallback for phones that support BLE5 Extended
     * Advertising but not the optional Coded PHY. */
    if (g_ext1m_adv_started) {
        uint16_t len;
        if (build_full_pack_adv(gps, identity, g_ext1m_adv, &len, sizeof(g_ext1m_adv))) {
            esp_err_t err = esp_ble_gap_config_ext_adv_data_raw(BLE_ADV_INSTANCE_EXT_1M, len, g_ext1m_adv);
            if (err == ESP_OK) {
                return true;
            } else {
                ESP_LOGW(TAG, "Ext-1M adv data update failed: %s", esp_err_to_name(err));
            }
        }
    }

    return false;
#else
    return false;
#endif
}

void ble_tx_set_power(int8_t dbm)
{
#if defined(CONFIG_BT_BLUEDROID_ENABLED) && defined(SOC_BT_SUPPORTED)
    if (!g_initialized) {
        ESP_LOGW(TAG, "BLE not initialized, skipping TX power set");
        return;
    }

    /*
     * esp_ble_tx_power_set() takes a discrete esp_power_level_t, not a raw
     * dBm value, so we map the requested dBm to the nearest of the levels
     * present on this chip's classic Bluedroid stack (-12..+9 dBm in 3 dB
     * steps). Confirmed via esp_bt.h (components/bt/include/esp32/include)
     * for this exact target/SDK version: ESP_PWR_LVL_P9 (+9 dBm) is the
     * actual ceiling here -- there is no P12/P15/P18 to extend this table
     * with on ESP32/ESP32-S3's classic controller, which is why
     * ble4_power_dbm/ble5_power_dbm are capped at 9 in web_config.c and
     * both UIs rather than allowing a value this table can't reach. Also
     * note: this sets one global ADV power level, not per-instance -- all
     * three advertising instances share it.
     */
    esp_power_level_t level;
    if (dbm <= -10)      level = ESP_PWR_LVL_N12;
    else if (dbm <= -7)  level = ESP_PWR_LVL_N9;
    else if (dbm <= -4)  level = ESP_PWR_LVL_N6;
    else if (dbm <= -1)  level = ESP_PWR_LVL_N3;
    else if (dbm <= 1)   level = ESP_PWR_LVL_N0;
    else if (dbm <= 4)   level = ESP_PWR_LVL_P3;
    else if (dbm <= 7)   level = ESP_PWR_LVL_P6;
    else                 level = ESP_PWR_LVL_P9;

    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, level);
    ESP_LOGI(TAG, "BLE TX power requested %d dBm", dbm);
#endif
}
