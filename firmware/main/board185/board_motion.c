/* The QMI8658 motion sensor of the Waveshare ESP32-S3-Touch-LCD-1.85B (I2C address 0x6B), read and streamed to the phone.
 * The register recipe follows Waveshare's own driver (vendor_waveshare/QMI8658); the filter bits are written outright because their
 * driver clears them again by mistake. */
#include "board_motion.h"

#include <string.h>

#include "I2C_Driver.h"
#include "ble_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nav_protocol.h"

static const char *TAG = "motion";

#define QMI_ADDR       0x6B
#define REG_WHO_AM_I   0x00
#define REG_CTRL1      0x02
#define REG_CTRL2      0x03      /* accelerometer: range (bits 6:4) and data rate (bits 3:0) */
#define REG_CTRL3      0x04      /* gyroscope: the same */
#define REG_CTRL5      0x06      /* low-pass filters */
#define REG_CTRL6      0x07
#define REG_CTRL7      0x08      /* which sensors run */
#define REG_STATUS0    0x2E      /* bit 0: new accelerometer data, bit 1: new gyroscope data */
#define REG_AX_L       0x35      /* ax ay az gx gy gz, 16 bits each, low byte first */

#define ACC_RANGE_8G   0x2
#define GYR_RANGE_512  0x5
#define ODR_60HZ       0x7

static volatile int available;
static volatile int enabled;
static uint16_t seq;

static esp_err_t wr(uint8_t reg, uint8_t v) { return I2C_Write(QMI_ADDR, reg, &v, 1); }

static int sensor_start(void)
{
    uint8_t id = 0;
    if (I2C_Read(QMI_ADDR, REG_WHO_AM_I, &id, 1) != ESP_OK || id != 0x05) {
        ESP_LOGW(TAG, "motion sensor not found (id 0x%02x)", id);
        return 0;
    }
    esp_err_t e = ESP_OK;
    e |= wr(REG_CTRL7, 0x00);                                  /* off while it is set up */
    e |= wr(REG_CTRL1, 0x40);                                  /* block reads step through the registers by themselves */
    e |= wr(REG_CTRL2, (uint8_t)((ACC_RANGE_8G << 4) | ODR_60HZ));
    e |= wr(REG_CTRL3, (uint8_t)((GYR_RANGE_512 << 4) | ODR_60HZ));
    e |= wr(REG_CTRL5, 0x75);                                  /* accelerometer filter on (mode 2), gyroscope filter on (mode 3) */
    e |= wr(REG_CTRL6, 0x00);
    e |= wr(REG_CTRL7, 0x43);                                  /* accelerometer and gyroscope run */
    if (e != ESP_OK) { ESP_LOGW(TAG, "motion sensor setup failed"); return 0; }
    return 1;
}

static void sensor_stop(void) { wr(REG_CTRL7, 0x00); }

static void motion_task(void *arg)
{
    (void)arg;
    uint8_t pkt[NAV_MOTION_HEADER_BYTES + NAV_MOTION_MAX_SAMPLES * NAV_MOTION_SAMPLE_BYTES];
    int count = 0, was_enabled = 0, first_sample_logged = 0;
    int64_t first_us = 0, last_us = 0;
    uint32_t sent = 0, dropped = 0;
    for (;;) {
        if (!enabled) {
            if (was_enabled) { sensor_stop(); was_enabled = 0; count = 0; ESP_LOGI(TAG, "stopped (%lu packets sent, %lu dropped)", (unsigned long)sent, (unsigned long)dropped); }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (!was_enabled) {
            if (!sensor_start()) { enabled = 0; continue; }
            was_enabled = 1; count = 0; sent = dropped = 0;
            ESP_LOGI(TAG, "streaming");
            first_sample_logged = 0;
        }
        uint8_t st = 0;
        if (I2C_Read(QMI_ADDR, REG_STATUS0, &st, 1) == ESP_OK && (st & 0x03) == 0x03) {
            uint8_t raw[12];
            if (I2C_Read(QMI_ADDR, REG_AX_L, raw, sizeof raw) == ESP_OK) {
                const int64_t now = esp_timer_get_time();
                if (count == 0) first_us = now;
                last_us = now;
                memcpy(pkt + NAV_MOTION_HEADER_BYTES + count * NAV_MOTION_SAMPLE_BYTES, raw, sizeof raw);     /* the sensor's order and byte order are the wire's */
                if (!first_sample_logged) {      /* once per start: a check that the readings make sense (about 1000 milli-g in total when the display is still) */
                    first_sample_logged = 1;
                    const int16_t *v = (const int16_t *)raw;
                    ESP_LOGI(TAG, "first reading: accel %d %d %d milli-g, gyro %d %d %d tenths of a degree per second",
                             v[0] * 1000 / NAV_MOTION_ACC_LSB_PER_G, v[1] * 1000 / NAV_MOTION_ACC_LSB_PER_G, v[2] * 1000 / NAV_MOTION_ACC_LSB_PER_G,
                             v[3] * 10 / NAV_MOTION_GYR_LSB_PER_DPS, v[4] * 10 / NAV_MOTION_GYR_LSB_PER_DPS, v[5] * 10 / NAV_MOTION_GYR_LSB_PER_DPS);
                }
                if (++count == NAV_MOTION_MAX_SAMPLES) {
                    const uint32_t t0 = (uint32_t)(first_us / 1000);
                    const uint16_t dt = (uint16_t)((last_us - first_us) / (NAV_MOTION_MAX_SAMPLES - 1));
                    pkt[0] = NAV_MOTION_DATA; pkt[1] = (uint8_t)count;
                    pkt[2] = (uint8_t)seq; pkt[3] = (uint8_t)(seq >> 8); seq++;
                    pkt[4] = (uint8_t)t0; pkt[5] = (uint8_t)(t0 >> 8); pkt[6] = (uint8_t)(t0 >> 16); pkt[7] = (uint8_t)(t0 >> 24);
                    pkt[8] = (uint8_t)dt; pkt[9] = (uint8_t)(dt >> 8);
                    if (ble_server_notify_motion(pkt, NAV_MOTION_HEADER_BYTES + count * NAV_MOTION_SAMPLE_BYTES) == 0) sent++; else dropped++;
                    count = 0;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));       /* about 100 looks a second for a sensor that makes 60 samples */
    }
}

void board_motion_init(void)
{
    available = sensor_start();
    if (available) sensor_stop();           /* it runs only while the phone wants the stream */
    if (!available) return;
    xTaskCreate(motion_task, "motion", 2560, NULL, 4, NULL);
    ESP_LOGI(TAG, "motion sensor ready");
}

int board_motion_available(void) { return available; }
void board_motion_enable(int on) { if (available) enabled = on ? 1 : 0; }
