/* nav_ui.h - round navigation display UI (LVGL 9, 412 x 412 px), shared by the simulator and the firmware.
 * The caller owns the route, road, and area buffers and keeps them alive while set. */
#ifndef NAV_UI_H
#define NAV_UI_H

#include <stddef.h>
#include "lvgl.h"
#include "nav_protocol.h"

/* Panel size in pixels (the layout scales; sizes with fonts: 412, 360). Set with -DNAV_UI_SIZE=... */
#ifndef NAV_UI_SIZE
#define NAV_UI_SIZE 412
#endif

/* Build the UI on `screen`. Call once after the display is created. */
void nav_ui_init(lv_obj_t *screen);

/* Route polyline (meters east/north of the origin). Resets route tracking. */
void nav_ui_set_route(int32_t origin_lat_e7, int32_t origin_lon_e7,
                      const nav_pt_t *pts, uint16_t n);

/* Slow stretches of the route (TRAFFIC characteristic): amber or red along the route line; indexes are into the route set by nav_ui_set_route, which clears them. */
void nav_ui_set_traffic(const nav_traffic_span_t *spans, int n);
/* The nearest slow stretch ahead (level 0 = none, otherwise NAV_TRAFFIC_SLOW or NAV_TRAFFIC_STOPPED): flashes a message when one appears or gets worse. */
void nav_ui_traffic_alert(int level, uint16_t dist_m, uint16_t length_m);

/* Tire pressures (TPMS characteristic): the tire page numbers and the pulsing red ring while a low flag is set; stale NAV_TPMS_STALE_S seconds after the last call. */
void nav_ui_set_tpms(const nav_tpms_t *t);
/* Whether the tire screen is in the swipe strip (the phone's monitoring switch). */
void nav_ui_set_tires_enabled(int enabled);

/* Optional background context drawn behind the route. */
void nav_ui_set_roads(const nav_seg_t *segs, uint16_t n);
void nav_ui_set_areas(const nav_area_t *areas, uint16_t n);
/* Park / water polygons (even-odd fill) in the route's frame. The array must stay valid. */
void nav_ui_set_polys(const nav_poly_t *polys, uint16_t n);

/* Feed a STATUS packet (about 1 Hz). Between packets the UI dead-reckons. */
void nav_ui_update(const nav_status_t *st);

/* Current weather + next four hours from the phone. Shown on the weather page. */
void nav_ui_set_weather(const nav_weather_t *wx);

/* Display units: imperial by default; the wire protocol is always metric. */
void nav_ui_set_metric(int metric);

/* Look: 0 standard (default), 1 vintage night, 2 tripper day, 3 tripper night, 4 tripper auto, 5 standard night, 6 standard day, 7 vintage day, 8 vintage auto. */
void nav_ui_set_style(int style);
int nav_ui_get_style(void);

/* 12-hour clock with AM/PM (0, the default) or 24-hour (1): the clock page and the weather page's hour labels. */
void nav_ui_set_clock24(int clock24);

/* Battery for the top of the map page: percent 0..100 and charging, or -1 for none; shown only while charging or at 30% and below. */
void nav_ui_set_battery(int percent, int charging);

/* Advance time-based state (dead reckoning, trip stats) by dt seconds; the frame timer calls it, the simulator uses it to fast-forward. */
void nav_ui_advance(float dt);

/* Reset trip statistics. A long press on the trip page does the same. */
void nav_ui_trip_reset(void);

/* Jump to a page: 0 map/navigation, 1 weather, 2 trip, 3 compass, 4 clock, 5 tires. */
void nav_ui_show_page(int page);
int nav_ui_active_page(void);
void nav_ui_next_page(void);
/* The code a phone must type to pair (6 digits) is shown over everything until this is called again with 0. */
void nav_ui_pairing_code(uint32_t code);
void nav_ui_toast(const char *text);                 /* a short message in the middle of the screen for about 1.5 seconds */                         /* go to the next screen in the swipe strip (the BOOT button); after the last, back to the first */
/* Screen ids: 0 map, 1 weather, 2 trip, 3 compass, 4 clock, 5 tires. They never change; only their order in the swipe strip does. */
int nav_ui_set_page_order(const uint8_t *ids, int n);   /* ids[i] = the screen shown i-th; must be a permutation of 0..5; returns 1 if applied */
void nav_ui_set_pages_off(uint8_t mask);             /* bit (1 << id) set = that screen is left out of the swipe strip; if nothing would be left, the map stays */
void nav_ui_get_page_order(uint8_t out[6]);          /* the page on screen now (same numbering as nav_ui_show_page) */

/* Compass page: altitude (m), climb rate (m/s, + = climbing), and whether valid. Heading comes from the STATUS GPS course, so it is known only while moving. */
void nav_ui_set_altitude(float altitude_m, float vspeed_mps, int valid);

/* Clock page: the time shown is UTC (system clock) plus this offset, so daylight saving follows the phone. */
void nav_ui_set_clock_offset(int32_t seconds_east_of_utc);

/* Theme: 1 = night (dark, default), 0 = day (light); follows the STATUS night flag, exposed for the simulator and backlight logic. */
void nav_ui_set_night(int night);

/* The exits of the next roundabout (from the phone). The turn arrow draws them when the maneuver is a roundabout. */
void nav_ui_set_roundabout(const nav_roundabout_t *r);
int nav_ui_is_night(void);

/* The logo splash: show while the phone is not connected, hide when it is. Call after nav_ui_init(). */
void nav_ui_splash_show(int show);
/* The logo grows from the centre with a little overshoot each time it is shown. With hold on, the first pop waits for nav_ui_splash_pop(); call hold before the first show. */
void nav_ui_splash_hold(int hold);
void nav_ui_splash_pop(void);

/* Origin (lat/lon in 1e-7 degrees) of the map's coordinate frame; 0 until there is one. Roads and areas are shifted into it. */
int nav_ui_frame_origin(int32_t *lat, int32_t *lon);

/* Map off-screen culling, a pure speed optimization (output identical either way); exposed for the simulator's --cull-check. */
extern int nav_ui_cull_enabled;

/* Distance from the position marker to the route, in screen pixels (updated every frame). */
extern float nav_ui_cross_px;
const lv_color32_t *nav_ui_debug_glyph(int man);   /* tests: the rendered maneuver glyph (ARROW_S x ARROW_S ARGB) */
void nav_ui_debug_position(float *x, float *y, float *heading_deg);   /* tests: current map position and heading */

/* Park/water fill method: 1 = fast horizontal spans (default), 0 = LVGL triangles (slow, for A/B tests). */
extern int nav_ui_area_scanline;

extern void *(*nav_ui_img_alloc)(size_t bytes);     /* optional allocator for the small pre-rendered images (PSRAM on the board) */

/* Slanted map lines: 1 = drawn in short pieces (fast, default), 0 = whole lines (slow, for A/B tests). */
extern int nav_ui_line_chunk;
extern float nav_pose_parts[3];       /* the parts of the last nav_ui_pose_step: translation, turning, zoom (px) */
int nav_ui_scrolling(void);           /* 1 while the pages are sliding */
float nav_ui_pose_step(void);         /* how far the picture moved since the last call, px (called once per drawn frame, for the frame log) */
extern uint32_t nav_gl_cache_stats[3];   /* kept arrows: shown from the cache, maneuver changes that had to be drawn, arrows drawn into the cache */
int nav_ui_debug_glyph_cache_check(void);   /* simulator test: the kept arrows against fresh renders, returns how many differ */
extern uint32_t nav_gl_job_ms;        /* longest wait from a new maneuver to its arrow being ready */
extern int nav_gl_cull;              /* tests: 0 turns off the glyph renderer's shortcut (same image, slower) */
extern uint32_t nav_gl_render_us;     /* slowest turn-glyph render since the last report */
extern float nav_zoom_stats[4];      /* zoom smoothness since the last reset: max rate (nep/s), max acceleration, sum of squared accelerations, ticks */
extern void (*nav_ui_zoom_hook)(float scale, float scale_tgt, float dt);   /* tests: sees the zoom every tick */
extern uint32_t nav_perf_us[13];
extern uint32_t nav_fl_stats[10];     /* label cache: images made, failures, last failed label, last made label */
extern int nav_ui_label_cache;
int nav_ui_label_count(void);         /* labels the cache looks after */       /* 1 (default) draws each label from a cached image, 0 lets LVGL draw the text every frame */      /* map drawing time by stage, see nav_ui.c */

#endif
