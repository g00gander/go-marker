#include "nav_link.h"

#include <stdlib.h>
#include <string.h>

#include "nav_protocol.h"
#include "nav_route_rx.h"
#include "nav_roads_rx.h"
#include "nav_areas_rx.h"
#include <math.h>
#include "nav_ui.h"

/* Two point buffers: one is on screen (owned by nav_ui) while the next route fills the other. */
static nav_route_rx_t rx;
static nav_pt_t shown[2][NAV_RX_MAX_POINTS];
static int shown_idx;
static nav_link_stats_t stats;
static nav_settings_t settings;
static uint32_t settings_seq;
static nav_time_t phone_time;
static uint32_t time_seq;
static nav_roundabout_t phone_roundabout;
static uint32_t roundabout_seq;
static nav_layout_t phone_layout;
static uint32_t layout_seq;
static int tpms_enabled = -1;       /* -1 = no TPMS packet yet, so the first one always counts as a change */
static uint32_t tpms_seq;
static int motion_req;
static uint32_t motion_seq;

/* Background roads: double-buffered, drawn in the current route's frame, so shifted whenever the origins differ. */
/* Large (~30 KB): allocated in init through nav_link_alloc (PSRAM on the board); as statics they stopped the Bluetooth stack starting. */
void *(*nav_link_alloc)(size_t bytes) = NULL;
static nav_roads_rx_t *rrx;
static nav_seg_t (*roads_shown)[NAV_MAX_ROADS];
static int roads_idx, have_roads;
static uint16_t roads_n;
static int32_t roads_lat, roads_lon;      /* origin of the frame the shown roads are in */

static nav_areas_rx_t *arx;
static nav_poly_t (*polys_shown)[NAV_MAX_POLYS];
static int polys_idx, have_polys;
static uint16_t polys_n;
static int32_t polys_lat, polys_lon;

static int16_t clamp16(float v) { return (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : lroundf(v))); }

/* The frame everything is drawn in is nav_ui's (the route's origin, or the first fix when not navigating). */
static int frame_now(int32_t *lat, int32_t *lon) { return nav_ui_frame_origin(lat, lon); }

static void frame_shift(int32_t from_lat, int32_t from_lon, int32_t route_lat, int32_t route_lon, float *dx, float *dy)
{
    const float m_per_deg = 6371008.8f * 3.14159265f / 180.0f;
    *dy = (float)(from_lat - route_lat) * 1e-7f * m_per_deg;
    *dx = (float)(from_lon - route_lon) * 1e-7f * m_per_deg * cosf((float)route_lat * 1e-7f * 3.14159265f / 180.0f);
}

static void polys_rebase(void)
{
    int32_t route_lat, route_lon;
    if (!have_polys || !frame_now(&route_lat, &route_lon) || (polys_lat == route_lat && polys_lon == route_lon)) return;
    float dx, dy;
    frame_shift(polys_lat, polys_lon, route_lat, route_lon, &dx, &dy);
    nav_poly_t *p = polys_shown[polys_idx];
    for (uint16_t i = 0; i < polys_n; i++)
        for (int k = 0; k < p[i].n; k++) { p[i].x[k] = clamp16(p[i].x[k] + dx); p[i].y[k] = clamp16(p[i].y[k] + dy); }
    polys_lat = route_lat;
    polys_lon = route_lon;
    nav_ui_set_polys(p, polys_n);
}

/* Shift roads_shown[roads_idx] from its own frame into the route's frame (flat-earth, as the phone does). */
static void roads_rebase(void)
{
    int32_t route_lat, route_lon;
    if (!have_roads || !frame_now(&route_lat, &route_lon) || (roads_lat == route_lat && roads_lon == route_lon)) return;
    float dx, dy;
    frame_shift(roads_lat, roads_lon, route_lat, route_lon, &dx, &dy);
    nav_seg_t *s = roads_shown[roads_idx];
    for (uint16_t i = 0; i < roads_n; i++) {
        s[i].x1 = clamp16(s[i].x1 + dx); s[i].x2 = clamp16(s[i].x2 + dx);
        s[i].y1 = clamp16(s[i].y1 + dy); s[i].y2 = clamp16(s[i].y2 + dy);
    }
    roads_lat = route_lat;
    roads_lon = route_lon;
    nav_ui_set_roads(s, roads_n);
}

void nav_link_init(void)
{
    if (!rrx) {
        rrx = nav_link_alloc ? nav_link_alloc(sizeof *rrx) : malloc(sizeof *rrx);
        arx = nav_link_alloc ? nav_link_alloc(sizeof *arx) : malloc(sizeof *arx);
        polys_shown = nav_link_alloc ? nav_link_alloc(2 * sizeof *polys_shown) : malloc(2 * sizeof *polys_shown);
        roads_shown = nav_link_alloc ? nav_link_alloc(2 * sizeof *roads_shown) : malloc(2 * sizeof *roads_shown);
    }
    memset(&stats, 0, sizeof stats);
    nav_route_rx_reset(&rx);
    if (rrx) nav_roads_rx_reset(rrx);
    if (arx) nav_areas_rx_reset(arx);
    have_polys = 0;
    polys_n = 0;
    shown_idx = 0;
    have_roads = 0;
    roads_n = 0;
}

const nav_link_stats_t *nav_link_stats(void) { return &stats; }

const nav_roundabout_t *nav_link_roundabout(uint32_t *seq)
{
    if (seq) *seq = roundabout_seq;
    return roundabout_seq ? &phone_roundabout : NULL;
}

int nav_link_motion_requested(uint32_t *seq)
{
    if (seq) *seq = motion_seq;
    return motion_req;
}

int nav_link_tpms_enabled(uint32_t *seq)
{
    if (seq) *seq = tpms_seq;
    return tpms_enabled > 0;
}

const nav_layout_t *nav_link_layout(uint32_t *seq)
{
    if (seq) *seq = layout_seq;
    return layout_seq ? &phone_layout : NULL;
}

const nav_time_t *nav_link_time(uint32_t *seq)
{
    if (seq) *seq = time_seq;
    return time_seq ? &phone_time : NULL;
}

const nav_settings_t *nav_link_settings(uint32_t *seq)
{
    if (seq) *seq = settings_seq;
    return settings_seq ? &settings : NULL;
}

void nav_link_write(nav_channel_t ch, const uint8_t *d, size_t n)
{
    switch (ch) {
    case NAV_CH_STATUS: {
        nav_status_t st;
        if (n < sizeof st || d[0] != NAV_PROTO_VERSION) { stats.rejected++; return; }
        memcpy(&st, d, sizeof st);
        stats.status++;
        stats.last_speed_kmh_x10 = st.speed_kmh_x10;
        stats.last_flags = st.flags;
        nav_ui_update(&st);
        roads_rebase();          /* a first fix (or a re-centre) can change the frame */
        polys_rebase();
        break;
    }
    case NAV_CH_WEATHER: {
        nav_weather_t w;
        if (n < sizeof w || d[0] != NAV_PROTO_VERSION) { stats.rejected++; return; }
        memcpy(&w, d, sizeof w);
        stats.weather++;
        nav_ui_set_weather(&w);
        break;
    }
    case NAV_CH_ROUTE: {
        nav_rx_result_t r = nav_route_rx_feed(&rx, d, n);
        if (r == NAV_RX_ERROR) {
            stats.rejected++;
        } else if (r == NAV_RX_READY) {
            int next = shown_idx ^ 1;
            memcpy(shown[next], rx.pts, (size_t)rx.received * sizeof(nav_pt_t));
            shown_idx = next;
            stats.route_ok++;
            stats.last_route_points = rx.received;
            nav_ui_set_route(rx.origin_lat_e7, rx.origin_lon_e7, shown[next], rx.received);
            roads_rebase();
            polys_rebase();
        }
        break;
    }
    case NAV_CH_ROADS: {
        if (!rrx || !roads_shown) { stats.rejected++; break; }
        nav_rx_result_t r = nav_roads_rx_feed(rrx, d, n);
        if (r == NAV_RX_ERROR) {
            stats.rejected++;
        } else if (r == NAV_RX_READY) {
            int next = roads_idx ^ 1;
            memcpy(roads_shown[next], rrx->segs, (size_t)rrx->received * sizeof(nav_seg_t));
            roads_idx = next;
            roads_n = rrx->received;
            roads_lat = rrx->origin_lat_e7;
            roads_lon = rrx->origin_lon_e7;
            have_roads = 1;
            stats.roads_ok++;
            stats.last_road_segments = rrx->received;
            nav_ui_set_roads(roads_shown[next], roads_n);
            roads_rebase();
        }
        break;
    }
    case NAV_CH_SETTINGS: {
        nav_settings_t s = { .voice_on = 1, .volume = 75, .touch = 2 };           /* a phone app that sends the old 3-byte form leaves the voice on */
        if (n < 3 || d[0] != NAV_PROTO_VERSION) { stats.rejected++; break; }
        memcpy(&s, d, n < sizeof s ? n : sizeof s);          /* an older, shorter packet leaves the rest at its defaults */
        s.voice_on = s.voice_on ? 1 : 0;
        s.metric = s.metric ? 1 : 0;
        s.clock24 = s.clock24 ? 1 : 0;
        s.style = s.style <= 8 ? s.style : 0;
        if (s.volume > 100) s.volume = 100;
        if (s.touch > 2) s.touch = 2;
        if (s.day_pct > 100 || s.night_pct > 100) { stats.rejected++; break; }
        if (s.day_pct < 5) s.day_pct = 5;              /* never let the screen go fully dark by accident */
        if (s.night_pct < 5) s.night_pct = 5;
        settings = s;
        settings_seq++;
        break;
    }
    case NAV_CH_TIME: {
        nav_time_t t;
        if (n < sizeof t || d[0] != NAV_PROTO_VERSION) { stats.rejected++; break; }
        memcpy(&t, d, sizeof t);
        if (t.unix_utc < 1700000000u || t.tz_offset_s < -14 * 3600 || t.tz_offset_s > 14 * 3600) { stats.rejected++; break; }
        phone_time = t;
        time_seq++;
        break;
    }
    case NAV_CH_TELEMETRY: {
        nav_telemetry_t t;
        if (n < sizeof t || d[0] != NAV_PROTO_VERSION) { stats.rejected++; break; }
        memcpy(&t, d, sizeof t);
        stats.telemetry++;
        stats.last_altitude_m = t.altitude_m;
        nav_ui_set_altitude((float)t.altitude_m, (float)t.vspeed_dm_s * 0.1f, t.valid ? 1 : 0);
        break;
    }
    case NAV_CH_ROUNDABOUT: {
        nav_roundabout_t t;
        if (n < sizeof t || d[0] != NAV_PROTO_VERSION) { stats.rejected++; break; }
        memcpy(&t, d, sizeof t);
        if (t.count > NAV_RB_MAX_EXITS || (t.taken != 0xFF && t.taken >= t.count)) { stats.rejected++; break; }
        phone_roundabout = t;
        roundabout_seq++;
        nav_ui_set_roundabout(&t);
        break;
    }
    case NAV_CH_TPMS: {
        nav_tpms_t t;
        if (n != sizeof t || d[0] != NAV_PROTO_VERSION) { stats.rejected++; break; }
        memcpy(&t, d, sizeof t);
        if (t.flags & ~0x7Fu) { stats.rejected++; break; }
        stats.tpms++;
        { const int on = (t.flags & NAV_TPMS_ENABLED) != 0; if (on != tpms_enabled) { tpms_enabled = on; tpms_seq++; } }
        nav_ui_set_tpms(&t);
        break;
    }
    case NAV_CH_MOTION:
        if (n != 1 || d[0] > NAV_MOTION_START) { stats.rejected++; break; }
        motion_req = d[0];
        motion_seq++;
        break;
    case NAV_CH_TRAFFIC: {
        if (n < 1) { stats.rejected++; break; }
        if (d[0] == NAV_TRAFFIC_SPANS) {
            if (n < 2 || d[1] > NAV_MAX_TRAFFIC_SPANS || n != 2u + (size_t)d[1] * sizeof(nav_traffic_span_t)) { stats.rejected++; break; }
            nav_traffic_span_t sp[NAV_MAX_TRAFFIC_SPANS];
            int ok = 1;
            for (int i = 0; i < d[1]; i++) {
                memcpy(&sp[i], d + 2 + i * sizeof(nav_traffic_span_t), sizeof(nav_traffic_span_t));
                if (sp[i].first > sp[i].last || sp[i].level < NAV_TRAFFIC_SLOW || sp[i].level > NAV_TRAFFIC_STOPPED) ok = 0;
            }
            if (!ok) { stats.rejected++; break; }
            stats.traffic_spans++;
            nav_ui_set_traffic(sp, d[1]);
        } else if (d[0] == NAV_TRAFFIC_ALERT) {
            if (n != 6 || d[1] > NAV_TRAFFIC_STOPPED) { stats.rejected++; break; }
            stats.traffic_alerts++;
            nav_ui_traffic_alert(d[1], (uint16_t)(d[2] | (d[3] << 8)), (uint16_t)(d[4] | (d[5] << 8)));
        } else {
            stats.rejected++;
        }
        break;
    }
    case NAV_CH_LAYOUT: {
        nav_layout_t t;
        if (n < NAV_LAYOUT_V1_BYTES || d[0] != NAV_PROTO_VERSION) { stats.rejected++; break; }
        memset(&t, 0, sizeof t);
        memcpy(&t, d, n < sizeof t ? n : sizeof t);
        if (n < sizeof t && t.count == NAV_PAGE_COUNT - 1) {   /* a list from before the tire page: it goes last */
            t.count = NAV_PAGE_COUNT;
            t.order[NAV_PAGE_COUNT - 1] = NAV_PAGE_TPMS;
        }
        uint8_t seen = 0; int ok = t.count == NAV_PAGE_COUNT;
        for (int k = 0; ok && k < NAV_PAGE_COUNT; k++) {
            if (t.order[k] >= NAV_PAGE_COUNT || (seen & (1u << t.order[k]))) ok = 0; else seen |= (uint8_t)(1u << t.order[k]);
        }
        if (!ok) { stats.rejected++; break; }                /* not a permutation of the screens */
        t.off_mask &= (uint8_t)((1u << NAV_PAGE_COUNT) - 1);   /* only real screens */
        phone_layout = t;
        layout_seq++;
        break;
    }
    case NAV_CH_AREAS: {
        if (!arx || !polys_shown) { stats.rejected++; break; }
        nav_rx_result_t r = nav_areas_rx_feed(arx, d, n);
        if (r == NAV_RX_ERROR) {
            stats.rejected++;
        } else if (r == NAV_RX_READY) {
            int next = polys_idx ^ 1;
            memcpy(polys_shown[next], arx->polys, (size_t)arx->received * sizeof(nav_poly_t));
            polys_idx = next;
            polys_n = arx->received;
            polys_lat = arx->origin_lat_e7;
            polys_lon = arx->origin_lon_e7;
            have_polys = 1;
            stats.areas_ok++;
            stats.last_area_polys = arx->received;
            nav_ui_set_polys(polys_shown[next], polys_n);
            polys_rebase();
        }
        break;
    }
    }
}
