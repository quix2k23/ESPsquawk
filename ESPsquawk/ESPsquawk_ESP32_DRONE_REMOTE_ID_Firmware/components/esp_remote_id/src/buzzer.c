#include "buzzer.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stddef.h>

#define TAG "BUZZER"

/* LEDC_TIMER_0/CHANNEL_0-2 are already used by led_status.c for the RGB
 * status LED -- this uses its own timer/channel so the two features
 * never interfere with each other. */
#define LEDC_MODE   LEDC_LOW_SPEED_MODE
#define LEDC_TIMER  LEDC_TIMER_1
#define LEDC_CHAN   LEDC_CHANNEL_3
#define LEDC_RES    LEDC_TIMER_10_BIT
#define LEDC_DUTY_ON (1u << 9) /* 50% duty at 10-bit resolution */

typedef struct {
    uint16_t freq_hz;
    uint16_t duration_ms;
} buzzer_note_t;

/* Short upbeat ascending chirp -- signals satellite lock achieved and
 * the home GPS position captured for this session. */
static const buzzer_note_t lock_tune[] = {
    { 1319, 90 },  /* E6 */
    { 1568, 90 },  /* G6 */
    { 1760, 90 },  /* A6 */
    { 2093, 160 }, /* C7 */
};
#define LOCK_TUNE_LEN (sizeof(lock_tune) / sizeof(lock_tune[0]))

/* Warm rising major-chord chime, played once at boot to confirm the
 * ESP32 has started successfully -- deliberately lower-pitched, slower,
 * and more "resolving" than the lock chirp above so the two are easy to
 * tell apart by ear. Loosely modeled on the classic Windows 7 startup
 * sound's gentle ascending-then-settling chord shape (not a
 * transcription of it, just the same "soft arpeggio that resolves
 * upward" character). */
static const buzzer_note_t boot_tune[] = {
    { 523,  130 }, /* C5 */
    { 659,  130 }, /* E5 */
    { 784,  130 }, /* G5 */
    { 1047, 260 }, /* C6 -- held longer, the "resolved" landing note */
};
#define BOOT_TUNE_LEN (sizeof(boot_tune) / sizeof(boot_tune[0]))

static int8_t s_gpio = -1;
static bool s_hw_ready = false;
static esp_timer_handle_t s_note_timer = NULL;
static const buzzer_note_t *s_tune = NULL;
static size_t s_tune_len = 0;
static size_t s_note_idx = 0;
static bool s_playing = false;

static void silence(void)
{
    if (!s_hw_ready) return;
    ledc_set_duty(LEDC_MODE, LEDC_CHAN, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHAN);
}

static void play_note(size_t idx)
{
    if (!s_hw_ready) return;
    const buzzer_note_t *n = &s_tune[idx];
    ledc_set_freq(LEDC_MODE, LEDC_TIMER, n->freq_hz);
    ledc_set_duty(LEDC_MODE, LEDC_CHAN, LEDC_DUTY_ON);
    ledc_update_duty(LEDC_MODE, LEDC_CHAN);
    esp_timer_start_once(s_note_timer, (uint64_t)n->duration_ms * 1000);
}

static void note_timer_cb(void *arg)
{
    (void)arg;
    s_note_idx++;
    if (s_note_idx >= s_tune_len) {
        silence();
        s_playing = false;
        return;
    }
    play_note(s_note_idx);
}

static void play_tune(const buzzer_note_t *tune, size_t len)
{
    if (!s_hw_ready || s_gpio < 0) return;
    if (s_playing) {
        esp_timer_stop(s_note_timer);
    }
    s_playing = true;
    s_tune = tune;
    s_tune_len = len;
    s_note_idx = 0;
    play_note(0);
}

static void hw_teardown(void)
{
    if (s_hw_ready) {
        silence();
        ledc_stop(LEDC_MODE, LEDC_CHAN, 0);
    }
    s_hw_ready = false;
}

void buzzer_init(int8_t gpio)
{
    if (s_note_timer == NULL) {
        const esp_timer_create_args_t targs = {
            .callback = &note_timer_cb,
            .name = "buzzer_note",
        };
        esp_timer_create(&targs, &s_note_timer);
    }

    if (s_playing) {
        esp_timer_stop(s_note_timer);
        s_playing = false;
    }
    hw_teardown();

    s_gpio = gpio;
    s_note_idx = 0;

    if (s_gpio < 0) {
        ESP_LOGI(TAG, "Buzzer disabled (no GPIO configured)");
        return;
    }

    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_MODE,
        .duty_resolution = LEDC_RES,
        .timer_num       = LEDC_TIMER,
        .freq_hz         = 2000,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .gpio_num   = s_gpio,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHAN,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&ch);

    s_hw_ready = true;
    ESP_LOGI(TAG, "Buzzer PWM init GPIO=%d", s_gpio);
}

void buzzer_reconfigure(int8_t gpio)
{
    buzzer_init(gpio);
}

void buzzer_play_lock_tone(void)
{
    play_tune(lock_tune, LOCK_TUNE_LEN);
}

void buzzer_play_boot_tone(void)
{
    play_tune(boot_tune, BOOT_TUNE_LEN);
}
