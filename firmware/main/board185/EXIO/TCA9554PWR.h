#pragma once
/* The 1.85B has no IO expander (its LCD and touch reset lines are wired straight to the ESP32), so this only keeps app_main.c identical on both boards. */
#include "esp_err.h"
static inline esp_err_t EXIO_Init(void) { return ESP_OK; }
