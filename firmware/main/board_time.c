#include "board_time.h"

#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "nvs.h"

#include "I2C_Driver.h"
#include "nav_ui.h"

static const char *TAG = "time";

#define RTC_ADDR      0x51
#define REG_CTRL1     0x00
#define REG_SECONDS   0x04

static uint8_t to_bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static int from_bcd(uint8_t b) { return (b >> 4) * 10 + (b & 15); }

/* days since 1970-01-01 for a civil date (Howard Hinnant's algorithm) */
static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static int32_t load_tz(void)
{
    nvs_handle_t h;
    int32_t tz = 0;
    if (nvs_open("cfg", NVS_READONLY, &h) == ESP_OK) { nvs_get_i32(h, "tz", &tz); nvs_close(h); }
    return tz;
}

static void save_tz(int32_t tz)
{
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "tz", tz);
    nvs_commit(h);
    nvs_close(h);
}

void board_time_init(void)
{
    nav_ui_set_clock_offset(load_tz());

    uint8_t c1;
    if (I2C_Read(RTC_ADDR, REG_CTRL1, &c1, 1) != ESP_OK) { ESP_LOGW(TAG, "clock chip not answering"); return; }
    if (c1 & 0x20) { c1 &= (uint8_t)~0x20; I2C_Write(RTC_ADDR, REG_CTRL1, &c1, 1); }     /* make sure it runs (STOP bit off) */

    uint8_t r[7];
    if (I2C_Read(RTC_ADDR, REG_SECONDS, r, 7) != ESP_OK) return;
    if (r[0] & 0x80) { ESP_LOGI(TAG, "clock chip lost power: time unknown until the phone connects"); return; }
    int year = 1970 + from_bcd(r[6]);
    if (year < 2024) { ESP_LOGI(TAG, "clock chip holds no valid date yet"); return; }
    int64_t days = days_from_civil(year, (unsigned)from_bcd(r[5] & 0x1F), (unsigned)from_bcd(r[3] & 0x3F));
    int64_t utc = days * 86400 + from_bcd(r[2] & 0x3F) * 3600 + from_bcd(r[1] & 0x7F) * 60 + from_bcd(r[0] & 0x7F);
    struct timeval tv = {.tv_sec = (time_t)utc, .tv_usec = 0};
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "restored time from the clock chip: %04d-%02d-%02d %02d:%02d:%02d UTC", year, from_bcd(r[5] & 0x1F),
             from_bcd(r[3] & 0x3F), from_bcd(r[2] & 0x3F), from_bcd(r[1] & 0x7F), from_bcd(r[0] & 0x7F));
}

void board_time_set(uint32_t unix_utc, int32_t tz_offset_s)
{
    struct timeval tv = {.tv_sec = (time_t)unix_utc, .tv_usec = 0};
    settimeofday(&tv, NULL);

    time_t t = (time_t)unix_utc;
    struct tm g;
    gmtime_r(&t, &g);
    uint8_t r[7] = {to_bcd(g.tm_sec), to_bcd(g.tm_min), to_bcd(g.tm_hour), to_bcd(g.tm_mday), (uint8_t)(g.tm_wday & 7),
                    to_bcd(g.tm_mon + 1), to_bcd(g.tm_year + 1900 - 1970)};
    esp_err_t e = I2C_Write(RTC_ADDR, REG_SECONDS, r, 7);       /* seconds register with bit 7 clear also clears the "lost power" flag */

    if (load_tz() != tz_offset_s) save_tz(tz_offset_s);
    nav_ui_set_clock_offset(tz_offset_s);

    time_t loc = t + tz_offset_s;
    struct tm l;
    gmtime_r(&loc, &l);
    ESP_LOGI(TAG, "set by phone: %04d-%02d-%02d %02d:%02d:%02d local (UTC%+d h), chip %s", l.tm_year + 1900, l.tm_mon + 1,
             l.tm_mday, l.tm_hour, l.tm_min, l.tm_sec, (int)(tz_offset_s / 3600), e == ESP_OK ? "updated" : "NOT updated");
}
