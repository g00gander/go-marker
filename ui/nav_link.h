/* nav_link.h - the display's side of the phone link, independent of transport (UDP in the simulator, BLE writes in the firmware).
 * Call from the LVGL thread. */
#ifndef NAV_LINK_H
#define NAV_LINK_H

#include <stddef.h>
#include <stdint.h>
#include "nav_protocol.h"

typedef enum {
    NAV_CH_STATUS  = 0,
    NAV_CH_ROUTE   = 1,
    NAV_CH_WEATHER = 2,
    NAV_CH_ROADS   = 3,
    NAV_CH_AREAS   = 4,
    NAV_CH_SETTINGS = 5,
    NAV_CH_TIME    = 6,
    NAV_CH_TELEMETRY = 7,
    NAV_CH_LAYOUT  = 8,
    NAV_CH_ROUNDABOUT = 9,
    NAV_CH_TRAFFIC = 10,
    NAV_CH_MOTION = 11,
    NAV_CH_TPMS = 12,
} nav_channel_t;

typedef struct {
    uint32_t status;         /* accepted STATUS packets */
    uint32_t weather;
    uint32_t route_ok;       /* complete routes with a good CRC */
    uint32_t rejected;       /* wrong size / version / bad or out-of-order route chunk */
    uint32_t last_route_points;
    uint32_t roads_ok;             /* complete road sets with a good CRC */
    uint32_t last_road_segments;
    uint32_t areas_ok;             /* complete polygon sets with a good CRC */
    uint32_t last_area_polys;
    uint32_t telemetry;            /* accepted TELEMETRY packets */
    int32_t  last_altitude_m;
    uint32_t last_speed_kmh_x10;   /* speed in the most recent STATUS (for diagnostics) */
    uint32_t last_flags;           /* NAV_FLAG_* bits of the most recent STATUS */
    uint32_t traffic_spans;        /* accepted TRAFFIC span lists */
    uint32_t traffic_alerts;       /* accepted TRAFFIC alerts */
    uint32_t tpms;                 /* accepted TPMS packets */
} nav_link_stats_t;

/* Optional allocator for the large road buffers (firmware: PSRAM). NULL = malloc. Set before nav_link_init(). */
extern void *(*nav_link_alloc)(size_t bytes);

void nav_link_init(void);
void nav_link_write(nav_channel_t ch, const uint8_t *data, size_t len);
const nav_link_stats_t *nav_link_stats(void);

/* Latest SETTINGS from the phone; *seq changes whenever a new one is accepted. NULL until one arrived. */
const nav_settings_t *nav_link_settings(uint32_t *seq);

/* Latest TIME from the phone; *seq changes whenever a new one is accepted. NULL until one arrived. */
const nav_time_t *nav_link_time(uint32_t *seq);

/* The latest ROUNDABOUT descriptor (also applied to the UI as it arrives). NULL until one arrived. */
const nav_roundabout_t *nav_link_roundabout(uint32_t *seq);

/* What the phone last asked of the motion stream: 1 = on, 0 = off. *seq changes with every request. */
int nav_link_motion_requested(uint32_t *seq);

/* Whether the phone last said tire monitoring is on (the tire screen is shown); *seq changes whenever a TPMS packet changes that. */
int nav_link_tpms_enabled(uint32_t *seq);

/* Latest LAYOUT (screen order) from the phone; *seq changes whenever a valid one is accepted. NULL until one arrived. */
const nav_layout_t *nav_link_layout(uint32_t *seq);

#endif
