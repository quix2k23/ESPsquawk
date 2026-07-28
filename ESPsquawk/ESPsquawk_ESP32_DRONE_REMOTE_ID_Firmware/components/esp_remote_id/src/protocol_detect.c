#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "protocol_detect.h"
#include "esp_remote_id.h"
#include "uart_mirror.h"

#define TAG "PROTO_DETECT"
#define PROTO_BUF_SIZE 256
#define DETECT_TIMEOUT_MS 1000
#define DETECT_READ_MS 50

/* Reads tx_pin/rx_pin from the current config instead of hardcoding them,
 * so the web UI/CLI's pin settings actually take effect. Each pin is
 * validated independently (range + esp_rid_is_reserved_gpio()) and
 * falls back to the safe default individually if invalid -- not just
 * when BOTH happen to be exactly 0. A single stale/bad value (e.g. left
 * over from earlier pin experimentation, or a value that predates the
 * reserved-GPIO safeguard being added) would otherwise pass straight
 * through to uart_set_pin() and fail there instead of being caught
 * here, which is exactly what caused a real "tx_io_num error". */
static void get_effective_uart_pins(int *tx_pin, int *rx_pin, uint32_t *baud_out)
{
    rid_config_t cfg;
    esp_rid_get_config(&cfg);

    int tx = cfg.tx_pin;
    int rx = cfg.rx_pin;

    if (tx < 0 || tx > 48 || esp_rid_is_reserved_gpio(tx)) {
        ESP_LOGW(TAG, "stored tx_pin=%d is invalid/reserved, falling back to 10", tx);
        tx = 10;
    }
    if (rx < 0 || rx > 48 || esp_rid_is_reserved_gpio(rx)) {
        ESP_LOGW(TAG, "stored rx_pin=%d is invalid/reserved, falling back to 11", rx);
        rx = 11;
    }

    *tx_pin = tx;
    *rx_pin = rx;
    if (baud_out) {
        *baud_out = (cfg.baud_rate > 0) ? cfg.baud_rate : 9600;
    }
}

void protocol_detect_init(void)
{
    int tx_pin, rx_pin;
    uint32_t baud;
    get_effective_uart_pins(&tx_pin, &rx_pin, &baud);

    uart_config_t uart_cfg = {
        .baud_rate = (int)baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_param_config(UART_NUM_1, &uart_cfg);
    uart_set_pin(UART_NUM_1, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_1, PROTO_BUF_SIZE, 0, 0, NULL, 0);
    uart_mirror_init();
}

rid_protocol_t protocol_detect_auto(void)
{
    uint8_t buf[PROTO_BUF_SIZE];
    int len = uart_read_bytes(UART_NUM_1, buf, PROTO_BUF_SIZE, pdMS_TO_TICKS(DETECT_READ_MS));

    if (len <= 0) {
        return RID_PROTOCOL_UNKNOWN;
    }

    uart_mirror_feed(buf, len);

    if (len >= 3 && buf[0] == '$' && buf[1] == 'M' && buf[2] == '<') {
        return RID_PROTOCOL_MSP;
    }

    if (len >= 3 && buf[0] == '$' && (buf[1] == 'G' || buf[1] == 'N')) {
        return RID_PROTOCOL_NMEA;
    }

    for (int i = 0; i < len - 1; i++) {
        if (buf[i] == 0xFE || buf[i] == 0xFD) {
            uint8_t msg_len = buf[i + 1];
            if (msg_len > 0 && msg_len < 255 && (i + msg_len + 6) <= len) {
                return RID_PROTOCOL_MAVLINK;
            }
        }
    }

    return RID_PROTOCOL_NMEA;
}

void protocol_detect_reinit(uint32_t baud)
{
    ESP_LOGI(TAG, "Reconfiguring UART to %lu baud", (unsigned long)baud);
    uart_driver_delete(UART_NUM_1);
    uart_config_t uart_cfg = {
        .baud_rate = (int)baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(UART_NUM_1, &uart_cfg);
    int tx_pin, rx_pin;
    get_effective_uart_pins(&tx_pin, &rx_pin, NULL);
    uart_set_pin(UART_NUM_1, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_1, PROTO_BUF_SIZE, 0, 0, NULL, 0);
    ESP_LOGI(TAG, "UART reconfigured to %lu baud", (unsigned long)baud);
}
