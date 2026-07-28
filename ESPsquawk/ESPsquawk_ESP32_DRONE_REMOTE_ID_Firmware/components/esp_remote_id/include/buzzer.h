#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>

/* Configures the buzzer output on `gpio` (-1 disables it). Safe to call
 * again later (e.g. at boot) with the same GPIO. */
void buzzer_init(int8_t gpio);

/* Re-targets the buzzer to a different GPIO at runtime, e.g. after the
 * user changes it in the web UI. Cancels any tone currently playing. */
void buzzer_reconfigure(int8_t gpio);

/* Plays a short upbeat ascending chirp, non-blocking. Intended to
 * signal that satellite lock has been achieved and the home GPS
 * position has been captured for the session. No-op if no GPIO is
 * configured. */
void buzzer_play_lock_tone(void);

/* Plays a brief rising chime, non-blocking, to confirm the ESP32 has
 * booted successfully -- distinct in pitch and rhythm from the lock
 * tone above so the two are easy to tell apart by ear. Intended to be
 * called once per boot, independent of GPS state. No-op if no GPIO is
 * configured. */
void buzzer_play_boot_tone(void);

#endif
