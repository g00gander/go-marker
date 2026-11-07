#pragma once
/* The screen and touch of the Waveshare ESP32-S3-Touch-LCD-1.85B (ST77916 over QSPI, CST816S touch). */
#include "Display_ST77916.h"
#include "Touch_CST816S.h"

#define BOARD_COLOR_BITS       18     /* RGB666: 3 bytes a pixel, 262,144 colors, so gradients are smooth (the 1.46 is 16-bit) */
#define BOARD_LCD_INVERT_COLOR 1      /* this panel shows a photo negative unless its color inversion is switched on */
