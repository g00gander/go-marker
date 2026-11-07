#pragma once
#include <stdint.h>
#include <stdbool.h>

#define CONFIG_ESP_LCD_TOUCH_MAX_POINTS (1)

void Touch_Init(void);                  /* I2C_Init() must have run */
/* Same call as the 1.46 board's touch driver: fills x[] and y[] and the number of points, returns true while touched. */
bool Touch_Get_xy(uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *count, uint8_t max);
