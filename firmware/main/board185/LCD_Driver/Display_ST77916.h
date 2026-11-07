#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

#define EXAMPLE_LCD_WIDTH       (360)
#define EXAMPLE_LCD_HEIGHT      (360)

extern esp_lcd_panel_handle_t panel_handle;

void LCD_Init(void);                    /* panel, backlight and touch; I2C_Init() must have run */
void Set_Backlight(uint8_t percent);    /* 0..100 */
void LCD_ColorBegin(void);              /* call just before esp_lcd_panel_draw_bitmap() ... */
int LCD_ColorWait(void);               /* ... and this after it: returns when the pixels have really left the buffer (the send is queued, not blocking) */
