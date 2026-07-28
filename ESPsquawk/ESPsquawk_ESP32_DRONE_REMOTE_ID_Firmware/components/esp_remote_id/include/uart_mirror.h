#ifndef UART_MIRROR_H
#define UART_MIRROR_H

#include <stdint.h>
#include <stddef.h>

/* Call once before first use; safe to call multiple times. */
void uart_mirror_init(void);

/* Appends raw bytes read from any protocol's UART read call. Non-printable
 * bytes (binary MSP/MAVLink framing, etc.) are shown as '.' rather than
 * passed through raw, so the buffer always stays safely displayable and
 * JSON-encodable. Cheap to call even if nothing is currently polling the
 * mirror -- just writes into a small in-memory ring buffer, nothing else. */
void uart_mirror_feed(const uint8_t *data, int len);

/* Copies the current rolling window into out (null-terminated, up to
 * max_len-1 bytes). Non-destructive -- repeated polls see overlapping
 * history rather than losing data between polls. Returns bytes copied. */
size_t uart_mirror_read(char *out, size_t max_len);

/* Monotonically increasing count of every byte ever fed in, independent
 * of the rolling display buffer above (which only keeps a fixed-size
 * window and can show the same repeating content when a GPS holds a
 * stalled fix). Lets a UI distinguish "the stream is still alive" from
 * "nothing new has actually arrived" even when the visible text looks
 * identical between polls -- see the UART Monitor's activity dot. */
uint32_t uart_mirror_get_total(void);

#endif
