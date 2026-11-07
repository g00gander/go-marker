/* Battery gauge of the Waveshare ESP32-S3-Touch-LCD-1.85B: a TI BQ27220 on the shared I2C bus (address 0x55).
 * Its standard commands are 16-bit little-endian registers: 0x06 temperature (0.1 K), 0x08 voltage (mV), 0x0C current (mA, signed),
 * 0x10 remaining capacity (mAh), 0x12 full-charge capacity (mAh), 0x2C state of charge (%). */
#include "board_battery.h"
#include "nav_battery.h"
#include "I2C_Driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#define GAUGE_ADDR 0x55
static const char *TAG = "battery";
static volatile board_battery_t latest;

static bool rd16(uint8_t reg, uint16_t *v)
{
    uint8_t b[2];
    if (I2C_Read(GAUGE_ADDR, reg, b, 2) != ESP_OK) return false;
    *v = (uint16_t)(b[0] | (b[1] << 8));
    return true;
}

/* ---- teaching the gauge the pack's real capacity -----------------------------------------------------------------------------
 * The gauge chip ships set up for a 3000 mAh pack. Ours is 500 mAh, so its own percentage and capacity figures are wrong. This writes the
 * design capacity and the full-charge capacity into the chip's data memory. The steps are the ones in TI's BQ27220 guide, the same as in
 * Waveshare's own library for this board (SensorLib, GaugeBQ27220.hpp): unseal, enter configuration-update mode, write each value
 * with its checksum, leave configuration-update mode (which restarts the gauge's algorithm), and seal again if it was sealed.
 * The chip keeps the values through power-offs, so after the first success this only reads one register at each boot. */
#define PACK_MAH          500
#define REG_CONTROL       0x00
#define REG_DESIGN_CAP    0x3C          /* standard command: design capacity, mAh (read only) */
#define REG_OP_STATUS     0x3A
#define ROM_FULL_CHARGE   0x929D        /* data-memory addresses of the two values written */
#define ROM_DESIGN        0x929F

static bool sub_cmd(uint16_t cmd)
{
    uint8_t b[2] = { (uint8_t)(cmd & 0xFF), (uint8_t)(cmd >> 8) };
    bool ok = I2C_Write(GAUGE_ADDR, REG_CONTROL, b, 2) == ESP_OK;
    vTaskDelay(pdMS_TO_TICKS(10));
    return ok;
}

static bool op_status(uint16_t *v) { return rd16(REG_OP_STATUS, v); }

/* One data-memory write, done the way Espressif's own BQ27220 driver does it (the one Waveshare's factory firmware runs): the address and the
 * value go in a single 4-byte write starting at 0x3E (address low, address high, value high, value low: the chip's data memory is big-endian),
 * then the checksum and the length (6) go in a single 2-byte write at 0x60. Writing the pieces one at a time was not accepted. */
static bool write_capacity(uint16_t rom_addr, uint16_t mah)
{
    uint8_t blk[4] = { (uint8_t)(rom_addr & 0xFF), (uint8_t)(rom_addr >> 8), (uint8_t)(mah >> 8), (uint8_t)(mah & 0xFF) };
    bool ok = I2C_Write(GAUGE_ADDR, 0x3E, blk, 4) == ESP_OK;
    vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t sum = (uint8_t)(0xFF - ((blk[0] + blk[1] + blk[2] + blk[3]) & 0xFF));
    uint8_t tail[2] = { sum, 6 };
    ok &= I2C_Write(GAUGE_ADDR, 0x60, tail, 2) == ESP_OK;
    vTaskDelay(pdMS_TO_TICKS(10));
    return ok;
}

static bool configure_gauge(void)
{
    uint16_t cap = 0;
    if (!rd16(REG_DESIGN_CAP, &cap)) { ESP_LOGW(TAG, "gauge setup: cannot read the design capacity"); return false; }
    if (cap == PACK_MAH) return true;                          /* already taught */
    ESP_LOGI(TAG, "gauge setup: design capacity is %u mAh, setting it to %d mAh", cap, PACK_MAH);

    uint16_t st = 0;
    if (!op_status(&st)) return false;
    int access = (st >> 1) & 3;                                /* 1 full access, 2 unsealed, 3 sealed */
    ESP_LOGI(TAG, "gauge setup: operation status 0x%04x (access level %d)", st, access);
    bool was_sealed = access == 3;
    if (was_sealed) { sub_cmd(0x0414); sub_cmd(0x3672); }      /* unseal */
    if (access != 1) { sub_cmd(0xFFFF); sub_cmd(0xFFFF); }     /* full access (the default key) */

    sub_cmd(0x0090);                                           /* enter configuration-update mode */
    bool in_cfg = false;
    for (int i = 0; i < 15 && !in_cfg; i++) { vTaskDelay(pdMS_TO_TICKS(100)); in_cfg = op_status(&st) && (st & 0x0400); }
    bool ok = false;
    ESP_LOGI(TAG, "gauge setup: configuration-update mode %s (status 0x%04x)", in_cfg ? "entered" : "NOT entered", st);
    if (in_cfg) {
        bool a = write_capacity(ROM_DESIGN, PACK_MAH);
        bool b = write_capacity(ROM_FULL_CHARGE, PACK_MAH);
        ok = a && b;
        /* read the design capacity back from data memory (address, then the two bytes at 0x40), to see whether the write was accepted */
        uint8_t addr[2] = { ROM_DESIGN & 0xFF, ROM_DESIGN >> 8 }, rb[2] = {0, 0};
        I2C_Write(GAUGE_ADDR, 0x3E, addr, 2); vTaskDelay(pdMS_TO_TICKS(10));
        I2C_Read(GAUGE_ADDR, 0x40, rb, 2);
        ESP_LOGI(TAG, "gauge setup: writes %s/%s, data memory now reads %02x %02x for the design capacity", a ? "ok" : "failed", b ? "ok" : "failed", rb[0], rb[1]);
    } else {
        ESP_LOGW(TAG, "gauge setup: the chip did not enter configuration-update mode (status 0x%04x)", st);
    }
    sub_cmd(0x0091);                                           /* leave it, restarting the gauge's algorithm with the new values */
    for (int i = 0; i < 30; i++) { vTaskDelay(pdMS_TO_TICKS(100)); if (op_status(&st) && !(st & 0x0400)) break; }
    if (was_sealed) sub_cmd(0x0030);                           /* seal again */

    vTaskDelay(pdMS_TO_TICKS(200));
    uint16_t after = 0, full = 0;
    rd16(REG_DESIGN_CAP, &after); rd16(0x12, &full);
    ESP_LOGI(TAG, "gauge setup: %s; design capacity now %u mAh, full-charge capacity %u mAh", ok && after == PACK_MAH ? "done" : "FAILED", after, full);
    return ok && after == PACK_MAH;
}

volatile uint32_t nav_batt_max_us;               /* the longest gauge read since the last report (microseconds), printed by lvgl_port.c */

static void battery_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(3000));                          /* let the rest of start-up settle first */
    for (int tries = 0; tries < 3 && !configure_gauge(); tries++) vTaskDelay(pdMS_TO_TICKS(2000));
    for (;;) {
        board_battery_t r = {0};
        uint16_t temp, volt, cur, rem, full, soc;
        const int64_t bt0 = esp_timer_get_time();
        const bool got = rd16(0x08, &volt) && rd16(0x0C, &cur) && rd16(0x2C, &soc) && rd16(0x06, &temp) && rd16(0x10, &rem) && rd16(0x12, &full);
        { const uint32_t us = (uint32_t)(esp_timer_get_time() - bt0); if (us > nav_batt_max_us) nav_batt_max_us = us; }
        if (got) {
            r.valid = true;
            r.voltage_mv = volt;
            r.current_ma = (int16_t)cur;
            /* the gauge's own percentage is worked out for a 3000 mAh pack; ours is 500 mAh, so use the voltage */
            static int smooth = -1;
            int raw = nav_battery_percent(r.voltage_mv, r.current_ma);
            smooth = smooth < 0 ? raw : (smooth * 7 + raw * 3 + 5) / 10;          /* steadies the reading a little */
            r.percent = smooth;
            (void)soc;
            r.temp_c10 = (int)temp - 2732;               /* tenths of a kelvin -> tenths of a degree C */
            r.remaining_mah = rem;
            r.full_mah = full;
            ESP_LOGI(TAG, "%d mV, %d%% (from voltage; gauge says %u%% of %d mAh), %d mA, %d.%d C", r.voltage_mv, r.percent, (unsigned)soc, r.full_mah, r.current_ma,
                     r.temp_c10 / 10, (r.temp_c10 < 0 ? -r.temp_c10 : r.temp_c10) % 10);
        } else {
            ESP_LOGW(TAG, "gauge did not answer");
        }
        latest = r;
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* The gauge task is off by default: this display runs without a battery (power comes from the bike), the gauge has nothing to measure, and its reads share the
 * I2C bus with the touch chip. A read that collided with a touch read could hold the screen for almost two seconds every minute or so. The pill on the
 * clock page stays hidden (board_battery_get() reports "no reading"). Build with -DNAV_BATTERY_GAUGE=1 to bring it back. */
#ifndef NAV_BATTERY_GAUGE
#define NAV_BATTERY_GAUGE 0
#endif
void board_battery_init(void)
{
#if NAV_BATTERY_GAUGE
    xTaskCreate(battery_task, "battery", 3072, NULL, 2, NULL);
#endif
}
board_battery_t board_battery_get(void) { return latest; }
