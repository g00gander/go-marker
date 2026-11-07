#include "feed.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define NV 8
static const nav_pt_t VERT[NV] = {
    {0, 0},     {0, 350},   {420, 350}, {700, 520},
    {700, 900}, {300, 900}, {300, 1250}, {900, 1250},
};
/* street name of segment i (VERT[i] -> VERT[i+1]) */
static const char *STREET[NV - 1] = {
    "Market Street", "Ring Road", "Bay Street", "Harbor Avenue",
    "Mission Street", "Pine Street", "Embarcadero",
};

static float seg_len[NV - 1], cum[NV];
static float total;
static double s_pos;   /* meters travelled along the route */

#define MAX_ROADS 128
static nav_seg_t roads[MAX_ROADS];
static uint16_t nroads;
static const nav_area_t areas[] = {
    {60, 60, 240, 250, NAV_AREA_PARK},
    {350, 600, 650, 800, NAV_AREA_PARK},
    {-400, 500, -100, 900, NAV_AREA_PARK},
    {400, 950, 650, 1200, NAV_AREA_PARK},
    {950, 100, 1400, 1500, NAV_AREA_WATER},
};

static float heading_of(int i)
{
    float dx = VERT[i + 1].dx - VERT[i].dx, dy = VERT[i + 1].dy - VERT[i].dy;
    float h = atan2f(dx, dy) * 180.0f / 3.14159265f;
    return h < 0 ? h + 360.0f : h;
}

static int classify(int i)   /* turn at vertex i+1, between segment i and i+1 */
{
    float t = heading_of(i + 1) - heading_of(i);
    while (t > 180) t -= 360;
    while (t < -180) t += 360;
    float a = fabsf(t);
    int right = t > 0;
    if (a < 20) return NAV_MAN_STRAIGHT;
    if (a < 55) return right ? NAV_MAN_SLIGHT_RIGHT : NAV_MAN_SLIGHT_LEFT;
    if (a < 125) return right ? NAV_MAN_RIGHT : NAV_MAN_LEFT;
    if (a < 165) return right ? NAV_MAN_SHARP_RIGHT : NAV_MAN_SHARP_LEFT;
    return NAV_MAN_UTURN;
}

int feed_theme_night = 0;   /* simulator: set the STATUS night flag (dark theme) */

static void add_road(int x1, int y1, int x2, int y2, int major)
{
    if (nroads < MAX_ROADS)
        roads[nroads++] = (nav_seg_t){x1, y1, x2, y2, (uint8_t)major};
}

void feed_init(void)
{
    total = 0;
    for (int i = 0; i < NV - 1; i++) {
        seg_len[i] = hypotf(VERT[i + 1].dx - VERT[i].dx, VERT[i + 1].dy - VERT[i].dy);
        cum[i] = total;
        total += seg_len[i];
    }
    cum[NV - 1] = total;
    s_pos = 0;

    nroads = 0;
    for (int x = -500; x <= 1300; x += 100)
        add_road(x, -300, x, 1600, x % 300 == 0);
    for (int y = -300; y <= 1600; y += 100)
        add_road(-500, y, 1300, y, y % 300 == 0);
    for (int i = 0; i < NV - 1; i++)   /* the route itself is always a road */
        add_road(VERT[i].dx, VERT[i].dy, VERT[i + 1].dx, VERT[i + 1].dy, 1);
}

float feed_cruise_ms = 14.0f;

void feed_step(double dt, nav_status_t *out)
{
    /* slow down for turns, cruise on straights */
    int seg = 0;
    while (seg < NV - 2 && s_pos >= cum[seg + 1]) seg++;
    float to_turn = cum[seg + 1] - (float)s_pos;
    float k = to_turn / (120.0f * feed_cruise_ms / 14.0f);
    float v = 5.0f + (feed_cruise_ms - 5.0f) * (k > 1 ? 1 : k);      /* m/s */

    s_pos += v * dt;
    if (s_pos >= total) s_pos = 0;                 /* loop the demo */

    seg = 0;
    while (seg < NV - 2 && s_pos >= cum[seg + 1]) seg++;
    float along = (float)s_pos - cum[seg];
    float f = along / seg_len[seg];
    float x = VERT[seg].dx + f * (VERT[seg + 1].dx - VERT[seg].dx);
    float y = VERT[seg].dy + f * (VERT[seg + 1].dy - VERT[seg].dy);

    memset(out, 0, sizeof(*out));
    out->version = NAV_PROTO_VERSION;
    out->flags = NAV_FLAG_NAVIGATING | (feed_theme_night ? NAV_FLAG_NIGHT : 0);
    out->lat_e7 = FEED_ORIGIN_LAT_E7 + (int32_t)(y / 111320.0 * 1e7);
    out->lon_e7 = FEED_ORIGIN_LON_E7 +
                  (int32_t)(x / (111320.0 * cos(FEED_ORIGIN_LAT_E7 * 1e-7 * M_PI / 180.0)) * 1e7);
    out->heading_x10 = (uint16_t)(heading_of(seg) * 10.0f);
    out->dist_turn_m = (uint16_t)(cum[seg + 1] - s_pos);
    out->speed_kmh_x10 = (uint16_t)(v * 3.6f * 10.0f);
    out->remaining_m = (uint32_t)(total - s_pos);
    out->eta_min = (uint16_t)ceilf(out->remaining_m / 10.0f / 60.0f);

    int last = (seg == NV - 2);
    out->maneuver = last ? NAV_MAN_ARRIVE : classify(seg);
    if (last) {
        out->next_maneuver = NAV_MAN_STRAIGHT;
    } else if (seg + 1 == NV - 2) {
        out->next_maneuver = NAV_MAN_ARRIVE;
        out->dist_next_m = (uint16_t)seg_len[seg + 1];
    } else {
        out->next_maneuver = classify(seg + 1);
        out->dist_next_m = (uint16_t)seg_len[seg + 1];
    }
    /* the street you are on now is where the next turn puts you after it, so
     * show the street you will turn onto, like a real nav banner */
    const char *name = last ? "Destination" : STREET[seg + 1];
    snprintf(out->street, NAV_STREET_LEN, "%s", name);
}

void feed_weather(nav_weather_t *out, int cond, int night)
{
    memset(out, 0, sizeof(*out));
    out->version = 1;
    out->condition = cond >= 0 ? (uint8_t)cond : NAV_WX_PARTLY;
    out->flags = night ? NAV_WX_FLAG_NIGHT : 0;
    out->hour0 = 15;
    out->temp_x10 = 190;      /* 19.0 C = 66 F */
    out->feels_x10 = 175;
    out->hi_c = 22;
    out->lo_c = 12;
    out->wind_kmh = 19;       /* ~12 mph */
    out->gust_kmh = 32;
    out->wind_from_deg = 300; /* from the WNW */
    out->precip_pct = (out->condition >= NAV_WX_DRIZZLE) ? 80 : 20;

    if (cond < 0) {   /* default: a dry spell, then rain building over the next hours */
        static const uint8_t c4[4] = {NAV_WX_PARTLY, NAV_WX_CLOUDY, NAV_WX_DRIZZLE, NAV_WX_RAIN};
        static const int8_t t4[4] = {19, 18, 16, 15};
        static const uint8_t p4[4] = {20, 35, 60, 80};
        for (int i = 0; i < 4; i++) {
            out->hourly[i].condition = c4[i];
            out->hourly[i].temp_c = t4[i];
            out->hourly[i].precip_pct = p4[i];
        }
    } else {
        for (int i = 0; i < 4; i++) {
            out->hourly[i].condition = out->condition;
            out->hourly[i].temp_c = (int8_t)(19 - i);
            out->hourly[i].precip_pct = out->precip_pct;
        }
    }
}

const nav_pt_t *feed_route(uint16_t *n) { *n = NV; return VERT; }
const nav_seg_t *feed_roads(uint16_t *n) { *n = nroads; return roads; }
const nav_area_t *feed_areas(uint16_t *n) { *n = sizeof(areas) / sizeof(areas[0]); return areas; }
