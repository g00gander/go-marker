#pragma once
/* The battery gauge: a BQ27220 on the 1.85B; the 1.46 build has none (the calls do nothing). */
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool valid;            /* false if the gauge did not answer */
    int voltage_mv;        /* cell voltage */
    int current_ma;        /* + charging, - discharging (as the gauge reports it) */
    int percent;           /* state of charge, 0..100 */
    int temp_c10;          /* cell temperature in tenths of a degree C */
    int remaining_mah, full_mah;
} board_battery_t;

void board_battery_init(void);                 /* starts a task that reads the gauge and logs it every 5 seconds */
board_battery_t board_battery_get(void);       /* the latest reading */
