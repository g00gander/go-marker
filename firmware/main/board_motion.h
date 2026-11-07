#pragma once
/* The motion sensor: a QMI8658 on the 1.85B (the 1.46 build reads none); readings go to the phone on the MOTION characteristic. */

void board_motion_init(void);          /* finds the sensor and starts a task that waits until streaming is switched on */
int board_motion_available(void);      /* 1 if the sensor answered */
void board_motion_enable(int on);      /* start or stop streaming */
