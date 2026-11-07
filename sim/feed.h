/* feed.h - fake phone: drives a simulated ride and emits nav_status_t. */
#ifndef FEED_H
#define FEED_H

#include "nav_protocol.h"

#define FEED_ORIGIN_LAT_E7 377749000
#define FEED_ORIGIN_LON_E7 (-1224194000)

void feed_init(void);

/* Advance the simulated ride by dt seconds and fill `out`. */
void feed_step(double dt, nav_status_t *out);

/* Fake weather. `cond` < 0 uses the default (partly cloudy); otherwise forces it. */
void feed_weather(nav_weather_t *out, int cond, int night);

const nav_pt_t *feed_route(uint16_t *n);
const nav_seg_t *feed_roads(uint16_t *n);
const nav_area_t *feed_areas(uint16_t *n);

extern float feed_cruise_ms;   /* demo cruising speed on straights, m/s (default 14 = 31 mph) */
extern int feed_theme_night;   /* 1 = demo status carries the night flag (dark theme), 0 = day (light theme) */

#endif
