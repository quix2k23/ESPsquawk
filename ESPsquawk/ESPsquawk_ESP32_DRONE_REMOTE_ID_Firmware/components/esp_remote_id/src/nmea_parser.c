#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "driver/uart.h"
#include "nmea_parser.h"
#include "uart_mirror.h"

#define TAG "NMEA"
#define NMEA_BUF_SIZE 256
#define UART_NMEA UART_NUM_1

static char g_nmea_buf[NMEA_BUF_SIZE];
static int g_buf_idx = 0;
static rid_gps_data_t g_last_gps;

void nmea_parser_init(void)
{
    memset(&g_last_gps, 0, sizeof(rid_gps_data_t));
    memset(g_nmea_buf, 0, NMEA_BUF_SIZE);
}

static double nmea_to_decimal(const char *nmea_str, char dir)
{
    if (!nmea_str || strlen(nmea_str) < 4) return 0.0;

    int dot_pos = -1;
    for (int i = 0; nmea_str[i]; i++) {
        if (nmea_str[i] == '.') { dot_pos = i; break; }
    }
    if (dot_pos < 4) return 0.0;

    int deg_len = dot_pos - 2;
    char deg_str[4] = {0};
    if (deg_len >= (int)sizeof(deg_str)) deg_len = sizeof(deg_str) - 1;
    memcpy(deg_str, nmea_str, deg_len);
    deg_str[deg_len] = '\0';
    double degrees = atoi(deg_str);

    double minutes = atof(nmea_str + deg_len);
    double decimal = degrees + minutes / 60.0;

    if (dir == 'S' || dir == 'W') decimal = -decimal;
    return decimal;
}

static void parse_gga(char *fields[])
{
    if (!fields[2] || !fields[3] || !fields[4] || !fields[5] || !fields[6]) return;

    int fix = atoi(fields[6]);
    if (fix < 1) return;

    /* Raw NMEA fix=1 means "standard GPS fix" -- the ONLY value the
     * vast majority of consumer/hobbyist GPS modules ever report
     * (2=DGPS, 4/5=RTK, hardware most modules don't have). Previously
     * mapped to internal fix_type=1, which failed every ">= 2" usable-
     * fix check elsewhere in this codebase (nmea_parser_get()'s own
     * return condition, and two checks in esp_remote_id.c) -- meaning
     * a normal, valid GPS fix never actually counted as usable. Both a
     * standard fix and an augmented one now satisfy those checks;
     * fix_type=3 is preserved for genuinely higher-confidence
     * DGPS/RTK fixes in case anything distinguishes it specifically. */
    g_last_gps.fix_type = (fix >= 2) ? 3 : 2;
    g_last_gps.latitude = nmea_to_decimal(fields[2], fields[3][0]);
    g_last_gps.longitude = nmea_to_decimal(fields[4], fields[5][0]);

    if (fields[7]) g_last_gps.satellites = atoi(fields[7]);
    if (fields[9]) {
        g_last_gps.altitude_msl = (float)atof(fields[9]);
        g_last_gps.altitude_baro = g_last_gps.altitude_msl;
    }
}

static void parse_rmc(char *fields[])
{
    if (!fields[3] || !fields[4] || !fields[5]) return;
    if (fields[2] && fields[2][0] != 'A') return;

    g_last_gps.latitude = nmea_to_decimal(fields[3], fields[4][0]);
    g_last_gps.longitude = nmea_to_decimal(fields[5], fields[6][0]);

    if (fields[7]) {
        g_last_gps.speed = (float)(atof(fields[7]) * 0.514444);
    }
}

static void parse_vtg(char *fields[])
{
    if (fields[1]) {
        g_last_gps.heading = (int16_t)atof(fields[1]);
    }
    if (fields[5]) {
        g_last_gps.speed = (float)(atof(fields[5]) * 0.514444);
    }
}

static void parse_nmea_line(const char *line)
{
    char work[128];
    strncpy(work, line, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';

    char *star = strchr(work, '*');
    if (star) *star = '\0';

    /* Manual comma scan instead of strtok(): strtok() treats consecutive
     * delimiters as one, silently SKIPPING empty fields rather than
     * producing an empty-string entry for them. NMEA sentences commonly
     * have empty fields (especially with no GPS fix, e.g.
     * "$GPGGA,164029.05,,,,,0,00,99.99,,,,,,*6B"), and this codebase's
     * parse_gga/parse_rmc/parse_vtg assume FIXED field positions (e.g.
     * fields[9] = altitude) per the NMEA spec -- if empty fields get
     * skipped instead of counted, every position after one shifts left,
     * misaligning the data. Also zero-initialize fields[] up front: any
     * index beyond however many commas were actually found previously
     * held uninitialized stack garbage rather than NULL, so the
     * existing "if (fields[N])" guards never actually caught a missing
     * field -- garbage is essentially never exactly zero. This produced
     * a real crash: atof()/strtod() called on a garbage pointer. */
    char *fields[16] = {0};
    int count = 0;
    char *p = work;
    fields[count++] = p;
    while (*p && count < 16) {
        if (*p == ',') {
            *p = '\0';
            fields[count++] = p + 1;
        }
        p++;
    }
    if (count < 2) return;

    if (strcmp(fields[0], "$GPGGA") == 0 || strcmp(fields[0], "$GNGGA") == 0) {
        parse_gga(fields);
    } else if (strcmp(fields[0], "$GPRMC") == 0 || strcmp(fields[0], "$GNRMC") == 0) {
        parse_rmc(fields);
    } else if (strcmp(fields[0], "$GPVTG") == 0 || strcmp(fields[0], "$GNVTG") == 0) {
        parse_vtg(fields);
    }
}

bool nmea_parser_get(rid_gps_data_t *gps)
{
    uint8_t buf[64];
    int len = uart_read_bytes(UART_NMEA, buf, sizeof(buf), 0);
    if (len > 0) uart_mirror_feed(buf, len);
    for (int i = 0; i < len; i++) {
        uint8_t c = buf[i];
        if (c == '\n' || g_buf_idx >= NMEA_BUF_SIZE - 1) {
            if (g_nmea_buf[0] == '$' && g_buf_idx > 5) {
                g_nmea_buf[g_buf_idx] = '\0';
                parse_nmea_line(g_nmea_buf);
            }
            g_buf_idx = 0;
            memset(g_nmea_buf, 0, NMEA_BUF_SIZE);
        } else {
            g_nmea_buf[g_buf_idx++] = c;
        }
    }

    if (g_last_gps.fix_type >= 2 && g_last_gps.latitude != 0.0) {
        memcpy(gps, &g_last_gps, sizeof(rid_gps_data_t));
        return true;
    }
    return false;
}
