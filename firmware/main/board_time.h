/* board_time.h - the display's clock: system time (UTC) backed by the PCF85063, set by the phone. Without a battery the chip keeps time only while powered. */
#ifndef BOARD_TIME_H
#define BOARD_TIME_H
#include <stdint.h>

void board_time_init(void);                          /* after I2C_Init(): restore time and zone if known */
void board_time_set(uint32_t unix_utc, int32_t tz_offset_s);
#endif
