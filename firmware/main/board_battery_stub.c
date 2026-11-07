#include "board_battery.h"
void board_battery_init(void) {}
board_battery_t board_battery_get(void) { board_battery_t b = {0}; return b; }
#include <stdint.h>
volatile uint32_t nav_batt_max_us;      /* (no gauge on this board: always 0; the frame log in lvgl_port.c prints it) */
