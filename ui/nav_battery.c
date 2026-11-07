#include "nav_battery.h"

/* Resting voltage (mV) -> percent for a typical lithium-polymer cell. */
static const struct { int mv; int pct; } CURVE[] = {
    {3270, 0}, {3610, 5}, {3690, 10}, {3710, 15}, {3730, 20}, {3750, 25}, {3770, 30}, {3790, 35}, {3800, 40}, {3820, 45}, {3840, 50},
    {3850, 55}, {3870, 60}, {3910, 65}, {3950, 70}, {3980, 75}, {4020, 80}, {4080, 85}, {4110, 90}, {4150, 95}, {4200, 100},
};
#define N ((int)(sizeof CURVE / sizeof CURVE[0]))
#define CELL_MOHM 150          /* internal resistance of the cell and its protection circuit, a typical figure for a small pack */

int nav_battery_percent(int mv, int ma)
{
    int ocv = mv - ma * CELL_MOHM / 1000;
    if (ocv <= CURVE[0].mv) return 0;
    if (ocv >= CURVE[N - 1].mv) return 100;
    for (int i = 1; i < N; i++) {
        if (ocv <= CURVE[i].mv) {
            int span = CURVE[i].mv - CURVE[i - 1].mv;
            return CURVE[i - 1].pct + (CURVE[i].pct - CURVE[i - 1].pct) * (ocv - CURVE[i - 1].mv) / span;
        }
    }
    return 100;
}
