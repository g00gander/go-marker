#ifndef NAV_BATTERY_H
#define NAV_BATTERY_H
/* Battery percent (0..100) for one lithium-polymer cell, estimated from `mv`; `ma` (positive while charging) corrects for internal resistance. */
int nav_battery_percent(int mv, int ma);
#endif
