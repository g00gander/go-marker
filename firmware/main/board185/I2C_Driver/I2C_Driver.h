#pragma once
/* I2C for the Waveshare ESP32-S3-Touch-LCD-1.85B: one bus (SDA 11, SCL 10) shared by the touch chip (CST816S), the clock
 * chip (PCF85063A), the battery gauge (BQ27220), the motion sensor (QMI8658) and the audio codec (ES8311).
 * It uses ESP-IDF's new I2C master driver, because the touch component needs it and the old driver cannot be mixed with it.
 * The function names match the 1.46 board's driver, so board_time.c builds unchanged. */
#include <stdint.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

#define I2C_SCL_IO                  10
#define I2C_SDA_IO                  11
#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          400000
#define I2C_MASTER_TIMEOUT_MS       1000

extern i2c_master_bus_handle_t nav_i2c_bus;     /* for the touch driver */

void I2C_Init(void);
esp_err_t I2C_Write(uint8_t Driver_addr, uint8_t Reg_addr, const uint8_t *Reg_data, uint32_t Length);
esp_err_t I2C_Read(uint8_t Driver_addr, uint8_t Reg_addr, uint8_t *Reg_data, uint32_t Length);
