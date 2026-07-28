#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "psa/crypto.h"
#include "esp_remote_id.h"
#include "rid_ota.h"

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define OTA_BUF_SIZE 4096
#define TAG "RID_OTA"

static httpd_handle_t g_ota_server = NULL;

/*
 * NOTE ON THREAT MODEL: this recovery server is only ever reachable after
 * someone has physically held the boot-trigger GPIO down at power-on, so it
 * is gated by physical possession of the device rather than by a
 * cryptographic signature (unlike the network-reachable OTA endpoint in
 * web_config.c, which *does* require a signature — see rid_security.h).
 * We still verify a SHA-256 of the upload so a corrupted/truncated transfer
 * is rejected instead of bricking the device.
 */

// OTA GET handler
static esp_err_t ota_get_handler(httpd_req_t *req)
{
    const char *response = "<html><body>"
                           "<h1>ESP32 Drone Remote ID - Recovery Mode</h1>"
                           "<p>Boot-trigger pin was held at startup. Normal operation is suspended "
                           "until firmware is (re)flashed or the device is reset.</p>"
                           "<form method='POST' action='/update' enctype='multipart/form-data'>"
                           "<input type='file' name='firmware'>"
                           "<input type='submit' value='Upload Firmware'>"
                           "</form>"
                           "<br>"
                           "<form method='POST' action='/factory_reset'>"
                           "<input type='submit' value='Factory Reset'>"
                           "</form>"
                           "<form method='POST' action='/rollback'>"
                           "<input type='submit' value='Rollback to Previous Firmware'>"
                           "</form>"
                           "</body></html>";
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

// OTA POST handler
static esp_err_t ota_update_handler(httpd_req_t *req)
{
    /* Optional integrity check: if the client sends X-Expected-SHA256, verify
     * the upload against it before committing to the boot partition. */
    char expected_hex[65] = {0};
    bool has_expected = false;
    size_t hdr_len = httpd_req_get_hdr_value_len(req, "X-Expected-SHA256");
    if (hdr_len > 0 && hdr_len <= 64) {
        httpd_req_get_hdr_value_str(req, "X-Expected-SHA256", expected_hex, sizeof(expected_hex));
        has_expected = true;
    }

    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);

    if (!update_partition) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition found");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    char *buf = malloc(OTA_BUF_SIZE);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        free(buf);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    psa_hash_operation_t sha_ctx = psa_hash_operation_init();
    bool hashing = has_expected && (psa_hash_setup(&sha_ctx, PSA_ALG_SHA_256) == PSA_SUCCESS);

    while (remaining > 0) {
        int recv = httpd_req_recv(req, buf, MIN(remaining, OTA_BUF_SIZE));
        if (recv <= 0) {
            if (recv == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            break;
        }

        if (hashing) {
            psa_hash_update(&sha_ctx, (const unsigned char *)buf, recv);
        }

        err = esp_ota_write(ota_handle, buf, recv);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(err));
            break;
        }
        remaining -= recv;
    }

    free(buf);

    if (err == ESP_OK && hashing) {
        uint8_t hash[32], expected_hash[32];
        size_t hash_len;
        bool hash_ok = (psa_hash_finish(&sha_ctx, hash, sizeof(hash), &hash_len) == PSA_SUCCESS);
        if (hash_ok) {
            for (int i = 0; i < 32 && hash_ok; i++) {
                unsigned int b;
                if (sscanf(expected_hex + i * 2, "%2x", &b) != 1) { hash_ok = false; break; }
                expected_hash[i] = (uint8_t)b;
            }
        }
        if (!hash_ok || memcmp(hash, expected_hash, 32) != 0) {
            ESP_LOGE(TAG, "OTA upload failed SHA-256 verification");
            err = ESP_FAIL;
        }
    } else if (hashing) {
        psa_hash_abort(&sha_ctx);
    }

    if (err == ESP_OK) {
        err = esp_ota_end(ota_handle);
        if (err == ESP_OK) {
            err = esp_ota_set_boot_partition(update_partition);
            if (err == ESP_OK) {
                httpd_resp_send(req, "Firmware updated successfully. Rebooting...", -1);
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
                return ESP_OK;
            }
        }
    }

    esp_ota_abort(ota_handle);
    ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA update failed");
    return ESP_FAIL;
}

// Factory reset handler
static esp_err_t factory_reset_handler(httpd_req_t *req)
{
    httpd_resp_send(req, "Factory reset triggered. Rebooting...", -1);
    esp_rid_factory_reset();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// Rollback handler
static esp_err_t rollback_handler(httpd_req_t *req)
{
    esp_err_t err = esp_ota_set_boot_partition(esp_ota_get_running_partition());
    if (err == ESP_OK) {
        httpd_resp_send(req, "Rollback successful. Rebooting...", -1);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Rollback failed");
    }
    return err;
}

// Start OTA server
esp_err_t start_ota_server(void)
{
    if (g_ota_server) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.stack_size = 8192;

    if (httpd_start(&g_ota_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start OTA server");
        return ESP_FAIL;
    }

    httpd_uri_t ota_get_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = ota_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(g_ota_server, &ota_get_uri);

    httpd_uri_t ota_update_uri = {
        .uri = "/update",
        .method = HTTP_POST,
        .handler = ota_update_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(g_ota_server, &ota_update_uri);

    httpd_uri_t factory_reset_uri = {
        .uri = "/factory_reset",
        .method = HTTP_POST,
        .handler = factory_reset_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(g_ota_server, &factory_reset_uri);

    httpd_uri_t rollback_uri = {
        .uri = "/rollback",
        .method = HTTP_POST,
        .handler = rollback_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(g_ota_server, &rollback_uri);

    ESP_LOGI(TAG, "OTA recovery server started on port %d", config.server_port);
    return ESP_OK;
}

// Stop OTA server
void stop_ota_server(void)
{
    if (g_ota_server) {
        httpd_stop(g_ota_server);
        g_ota_server = NULL;
    }
}

// Check GPIO and start OTA server if the trigger pin is held.
// Returns true if normal boot should continue, false if OTA recovery mode
// was entered. The recovery HTTP server runs asynchronously in its own
// task (httpd_start() spins that up internally), so this function does not
// need to block — but the caller MUST stop the rest of its normal init
// path when this returns false, or it will try to bind a second HTTP
// server to the same port.
bool rid_ota_check_and_run(rid_config_t *cfg)
{
    int trigger_gpio = (cfg && cfg->ota_trigger_gpio >= 0) ? cfg->ota_trigger_gpio : 0;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << trigger_gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // Check if trigger pin is pressed (pulled low)
    if (gpio_get_level(trigger_gpio) != 0) {
        return true; /* not held: proceed with normal boot */
    }

    ESP_LOGW(TAG, "Boot trigger (GPIO%d) held — entering OTA recovery mode", trigger_gpio);
    if (start_ota_server() != ESP_OK) {
        ESP_LOGE(TAG, "Recovery server failed to start; continuing normal boot");
        return true;
    }

    return false; /* recovery server is running; caller should stop here */
}
