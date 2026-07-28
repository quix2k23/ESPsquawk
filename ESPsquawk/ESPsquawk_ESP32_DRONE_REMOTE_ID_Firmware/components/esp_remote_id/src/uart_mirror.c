#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "uart_mirror.h"

#define UART_MIRROR_BUF_SIZE 512

static char g_buf[UART_MIRROR_BUF_SIZE];
static size_t g_len = 0;
static SemaphoreHandle_t g_lock = NULL;
static volatile uint32_t g_total = 0;

void uart_mirror_init(void)
{
    if (!g_lock) {
        g_lock = xSemaphoreCreateMutex();
    }
}

void uart_mirror_feed(const uint8_t *data, int len)
{
    if (!data || len <= 0) return;
    if (!g_lock) uart_mirror_init();
    if (!g_lock) return;

    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(20)) != pdTRUE) {
        /* Never block a protocol parser's read path waiting on this --
         * missing a mirror update is harmless, blocking real GPS/FC
         * parsing is not. */
        return;
    }

    for (int i = 0; i < len; i++) {
        uint8_t c = data[i];
        /* Keep the buffer safely printable and JSON-encodable: pass
         * through normal ASCII/newlines, replace anything else (raw
         * MSP/MAVLink binary framing bytes) with a placeholder dot
         * rather than showing/transmitting raw binary junk. */
        char out_c = (c == '\n' || c == '\r' || (c >= 0x20 && c < 0x7F)) ? (char)c : '.';

        if (g_len >= UART_MIRROR_BUF_SIZE - 1) {
            /* Buffer full: drop the oldest half to make room, keeping
             * the most recent data rather than just refusing new input. */
            size_t shift = UART_MIRROR_BUF_SIZE / 2;
            memmove(g_buf, g_buf + shift, g_len - shift);
            g_len -= shift;
        }
        g_buf[g_len++] = out_c;
    }
    g_total += (uint32_t)len;

    xSemaphoreGive(g_lock);
}

uint32_t uart_mirror_get_total(void)
{
    return g_total;
}

size_t uart_mirror_read(char *out, size_t max_len)
{
    if (!out || max_len == 0) return 0;
    out[0] = '\0';
    if (!g_lock) uart_mirror_init();
    if (!g_lock) return 0;

    size_t copied = 0;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        copied = (g_len < max_len - 1) ? g_len : max_len - 1;
        memcpy(out, g_buf, copied);
        out[copied] = '\0';
        xSemaphoreGive(g_lock);
    }
    return copied;
}
