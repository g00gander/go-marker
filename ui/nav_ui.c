#include "nav_ui.h"
#include "nav_trip.h"
#include "nav_trip_store.h"
#include "nav_raster.h"
#include "src/widgets/tileview/lv_tileview_private.h"       /* a tile's swipe directions: they change when a screen joins or leaves the strip */

/* Fonts: designed for 412 px; smaller panels use the nearest scaled size. The bold ones are generated into ui/fonts with lv_font_conv. */
#if NAV_UI_SIZE == 412
LV_FONT_DECLARE(font_montserrat_bold_22);
LV_FONT_DECLARE(font_montserrat_bold_26);
LV_FONT_DECLARE(font_montserrat_bold_44);    /* N E S W only: the compass cardinals */
LV_FONT_DECLARE(font_montserrat_bold_120);   /* digits and colon only: the clock page */
LV_FONT_DECLARE(font_montserrat_bold_80);    /* digits, minus and point only: the tire pressures */
#define FT14  (&lv_font_montserrat_14)
#define FT20  (&lv_font_montserrat_20)
#define FT28  (&lv_font_montserrat_28)
#define FT48  (&lv_font_montserrat_48)
#define FB22  (&font_montserrat_bold_22)
#define FB26  (&font_montserrat_bold_26)
#define FB44  (&font_montserrat_bold_44)
#define FB120 (&font_montserrat_bold_120)
#define FBN   (&font_montserrat_bold_80)
#elif NAV_UI_SIZE == 360
LV_FONT_DECLARE(font_montserrat_bold_19);
LV_FONT_DECLARE(font_montserrat_bold_23);
LV_FONT_DECLARE(font_montserrat_bold_38);
LV_FONT_DECLARE(font_montserrat_bold_105);
LV_FONT_DECLARE(font_montserrat_bold_70);
#define FT14  (&lv_font_montserrat_12)
#define FT20  (&lv_font_montserrat_18)
#define FT28  (&lv_font_montserrat_24)
#define FT48  (&lv_font_montserrat_42)
#define FB22  (&font_montserrat_bold_19)
#define FB26  (&font_montserrat_bold_23)
#define FB44  (&font_montserrat_bold_38)
#define FB120 (&font_montserrat_bold_105)
#define FBN   (&font_montserrat_bold_70)
#else
#error "NAV_UI_SIZE: no font set for this size (add one next to the 412 and 360 sets above)"
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Where the map's drawing time goes (microseconds since the last report, printed in the firmware's frame log); only the draw callback, not LVGL's later blending. */
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#define PERF_NOW() ((uint32_t)esp_timer_get_time())
#else
#define PERF_NOW() 0u
#endif
float nav_zoom_stats[4];            /* max zoom rate, max acceleration, sum of squared accelerations, ticks: reset by whoever reports them */
void (*nav_ui_zoom_hook)(float scale, float scale_tgt, float dt) = NULL;       /* tests: called every tick with the zoom */
uint32_t nav_timer_max_us[2];                  /* longest frame tick and label-cache tick since the last report (microseconds) */
uint32_t nav_perf_us[13];       /* 0 whole map callback, 1 areas+polygons, 2 streets, 3 route points, 4 route masks, 5 strips drawn */

/* ---- palette: two themes, switched at runtime (night = dark, day = light for sunlight) ---------- */
typedef struct {
    uint32_t bg, map, road, road_maj, park, water, accent, route_out, past, text, sub, dim, north, div, edge, scale;
    uint32_t slow, stopped;   /* the route line over slow and nearly stopped traffic: each must stand out from this look's accent */
    uint32_t logo;            /* the logo on the start-up screen (it sits on this look's background) */
    uint8_t light;            /* a light background: weather icons are darkened to stay visible */
} theme_t;
static const theme_t THEME_NIGHT = {
    .bg = 0x0A0E14, .map = 0x0F1622, .road = 0x566D92, .road_maj = 0x9AB2D8, .park = 0x256B40, .water = 0x226CB0,
    .accent = 0x3B82F6, .route_out = 0xFFFFFF, .past = 0x2B4A7A, .text = 0xFFFFFF, .sub = 0x8B96A8, .dim = 0x5A667A,
    .north = 0xEF4444, .div = 0x2A3444, .edge = 0x3A4558, .scale = 0xD6DCE8, .slow = 0xF5A300, .stopped = 0xE0352B, .logo = 0x3B82F6,
};
static const theme_t THEME_DAY = {
    .bg = 0xF2F5F9, .map = 0xE6EBF2, .road = 0xB7C2D2, .road_maj = 0x8798B2, .park = 0xB9E0BF, .water = 0x9CC9F0,
    .accent = 0x1D63E0, .route_out = 0xFFFFFF, .past = 0x9DB2D6, .text = 0x0B1220, .sub = 0x475569, .dim = 0x6B7688,
    .north = 0xDC2626, .div = 0xC5CEDA, .edge = 0x9AA6B8, .scale = 0x1E293B, .light = 1, .slow = 0xF59E0B, .stopped = 0xDC1F1F, .logo = 0x1D63E0,
};
/* Vintage: the green face, white numerals, and red needle of a 1970s Honda CL350 speedometer; day is a cream dial. */
static const theme_t THEME_VINTAGE_NIGHT = {
    .bg = 0x0A3B30, .map = 0x0D4637, .road = 0x6FA08C, .road_maj = 0xE8E2CC, .park = 0x3C7A3A, .water = 0x2F6F8A,
    .accent = 0xE0402F, .route_out = 0xF5F0DF, .past = 0x2C6B58, .text = 0xF7F3E6, .sub = 0xB5C9BC, .dim = 0x7C9C8D,
    .north = 0xE0402F, .div = 0x1F5B4A, .edge = 0x3F7A66, .scale = 0xF0EBDA, .slow = 0xF5B700, .stopped = 0x5A0D12, .logo = 0xF7F3E6,
};
static const theme_t THEME_VINTAGE_DAY = {
    .bg = 0xEFE9D6, .map = 0xE6DFC8, .road = 0xC7BE9E, .road_maj = 0x8C8466, .park = 0xB5CDA0, .water = 0x9DBDC4,
    .accent = 0xC8321F, .route_out = 0xFFFFFF, .past = 0xD2C9A8, .text = 0x0B2E26, .sub = 0x3F5A50, .dim = 0x6F8279,
    .north = 0xC8321F, .div = 0xC7BE9E, .edge = 0x9A9276, .scale = 0x12372E, .light = 1, .slow = 0xF2A900, .stopped = 0x4A0A0E, .logo = 0x0B2E26,
};
/* Tripper: the dash of a Royal Enfield Bear 650. Black face by night with the gold loop and green neutral light, pale grey face by day. */
static const theme_t THEME_TRIPPER_NIGHT = {
    .bg = 0x050607, .map = 0x0C0E11, .road = 0x585D66, .road_maj = 0xB4B9C0, .park = 0x24402C, .water = 0x244560,
    .accent = 0xE9B949, .route_out = 0xFFFFFF, .past = 0x4D4426, .text = 0xFFFFFF, .sub = 0xB9BEC6, .dim = 0x7A8088,
    .north = 0x3DBE4C, .div = 0x2A2E33, .edge = 0x4A4F57, .scale = 0xE6E8EB, .slow = 0xF2701D, .stopped = 0xE0352B, .logo = 0xE9B949,
};
static const theme_t THEME_TRIPPER_DAY = {
    .bg = 0xD8DDE5, .map = 0xE3E7EC, .road = 0xB2B8C2, .road_maj = 0x858C98, .park = 0xBCD3BB, .water = 0xA6C2DA,
    .accent = 0x8F6F12, .route_out = 0xFFFFFF, .past = 0xC8BD92, .text = 0x0A0A0A, .sub = 0x3E434B, .dim = 0x6B717B,
    .north = 0x1F8A35, .div = 0xB2B8C2, .edge = 0x8D939D, .scale = 0x1A1C20, .light = 1, .slow = 0xE8620F, .stopped = 0xD01F1F, .logo = 0x8F6F12,
};
static const theme_t *TH = &THEME_NIGHT;
#define C_BG        (TH->bg)
#define C_MAP       (TH->map)
#define C_ROAD      (TH->road)
#define C_ROAD_MAJ  (TH->road_maj)
#define C_PARK      (TH->park)
#define C_WATER     (TH->water)
#define C_ACCENT    (TH->accent)
#define C_ROUTE_OUT (TH->route_out)
#define C_PAST      (TH->past)
#define C_TEXT      (TH->text)
#define C_SUB       (TH->sub)
#define C_NORTH     (TH->north)

/* Widgets whose colors depend on the theme, so a theme change can restyle them. */
enum { ROLE_TEXT, ROLE_SUB, ROLE_DIM, ROLE_DIV };
static struct { lv_obj_t *o; uint8_t role; } themed[80];
static int nthemed;
static void theme_reg(lv_obj_t *o, int role) { if (nthemed < 80) { themed[nthemed].o = o; themed[nthemed].role = (uint8_t)role; nthemed++; } }
static uint32_t theme_role_color(int role)
{
    switch (role) { case ROLE_SUB: return TH->sub; case ROLE_DIM: return TH->dim; case ROLE_DIV: return TH->div; default: return TH->text; }
}

/* ---- layout: positions are on a 412 x 412 design grid; S() and SF() scale design pixels to the panel, pos()/wid()/plain() apply it to widgets ---- */
#define DSZ         412
#define SZ          NAV_UI_SIZE
#define S(v)        ((((v) * SZ) + DSZ / 2) / DSZ)
#define SF(v)       ((v) * (float)SZ / (float)DSZ)
#define ARROW_X     58
#define ARROW_Y     50
#define ARROW_S     84
#define DIST_X      152
#define DIST_Y      38
#define STREET_X    154
#define STREET_Y    92
#define STREET_W    190
#define MAP_Y       0
#define MAP_H       DSZ
#define MARK_Y      246            /* position marker while navigating, relative to map top */
#define MARK_Y_FREE 206            /* not navigating: dead centre of the round display */
#define MARKER_Y()  S(U.free_mode ? MARK_Y_FREE : MARK_Y)         /* real pixels */
#define MAP_BIG     1.21f          /* the map (streets), route line and position marker are drawn this much larger */
#define MB          (MAP_BIG * (float)SZ / (float)DSZ)    /* map zoom and line/marker scale: bigger by MAP_BIG, and for the panel size */
#define MORE        1.25f          /* a further 25% for the marker, route line, and streets in every view (and the free-ride zoom) */
#define MBX         (MB * MORE)    /* line widths and marker size */
#define BIGPX(v)    ((int32_t)((v) * MBX + 0.5f))
#define STAT_Y      348
#define NPAGES      6
#define NAV_UI_MAX_ROUTE 640      /* route points kept (the link delivers at most 600) */

#define PI_F 3.14159265f
#define DEG2RAD (PI_F / 180.0f)

static void tile_changed_cb(lv_event_t *e);
static void refresh_weather(void);
static void tpms_refresh(void);
static void tpms_tick(float dt, uint32_t now);

int nav_ui_cull_enabled = 1;   /* map off-screen culling; tests switch it off to A/B it */

/* ---- state ------------------------------------------------------------ */
static struct {
    lv_obj_t *round, *tv, *tile_nav, *tile_wx, *tile_trip, *tile_cmp, *tile_clk, *tile_tpms;   /* the screens (movable containers) */
    lv_obj_t *slot[NPAGES];              /* the swipe strip's tiles: slot i holds the screen whose id is order[i] */
    uint8_t order[NPAGES];               /* the order the phone chose (all the screens) */
    uint8_t strip[NPAGES];               /* what is in the slots: the same, minus the screens turned off and the tire screen while monitoring is off */
    uint8_t off_mask;                    /* the screens the phone turned off (bit 1 << id) */
    int nstrip;                          /* screens in the swipe strip */
    int tires_enabled;
    lv_obj_t *cmp_obj, *cmp_card, *cmp_deg, *cmp_elev, *cmp_vs, *cmp_hold;      /* compass page */
    float alt_m, vs_mps;                 /* altitude above sea level and climb rate, from the phone */
    int alt_valid;
    float alt_age;                       /* seconds since the last altitude update */
    float cmp_elev_w;                    /* pixel width of the elevation text, to place the climb marker beside it */
    float cmp_h;                         /* the compass card's own, more heavily smoothed heading */
    lv_obj_t *cmp_ic[4];                 /* NE SE SW NW: real text, always upright, carried around the ring */
    lv_obj_t *cmp_cd[4];                 /* N E S W, the same way, larger */
    uint32_t cmp_tick;
    lv_obj_t *clk_time, *clk_ampm, *clk_date;
    lv_obj_t *bat_pill, *bat_icon, *bat_nub, *bat_fill, *bat_bolt, *bat_txt;     /* battery indicator at the top of the map page; hidden until a percentage arrives */
    int bat_pct;                                          /* -1 = unknown */
    int bat_shown;                                        /* whether the indicator is on screen (see nav_ui_set_battery) */
    char clk_text[24];
    int32_t tz_offset_s;     /* seconds east of UTC, from the phone (or the host in the simulator) */
    uint32_t clk_tick;
    lv_obj_t *arrow, *dist, *street;
    lv_obj_t *map, *bar_lbl, *rem_val, *eta_val;
    lv_obj_t *dot[NPAGES];               /* the page indicator */
    lv_obj_t *pw_icon, *pw_temp, *pw_cond, *pw_row, *pw_wind, *pw_wind_val, *pw_wind_sub, *pw_rain, *pw_rain_cap;
    lv_obj_t *hr_time[4], *hr_icon[4], *hr_temp[4], *hr_pop[4];
    lv_obj_t *trip_val[6];
    nav_weather_t wx;
    int have_wx;

    nav_status_t st;
    int have_status;

    const nav_pt_t *route;   uint16_t nroute;
    const nav_seg_t *roads;  uint16_t nroads;
    const nav_area_t *areas; uint16_t nareas;
    int32_t olat, olon;
    int have_origin;         /* olat/olon is meaningful (set by a route, or by the first fix) */
    lv_obj_t *splash;
    lv_obj_t *stat_div;      /* divider between the two bottom values */
    lv_obj_t *cap_l, *cap_r; /* free-ride captions under the speed and temperature */
    int free_mode;           /* 1 = phone connected but not navigating: map at the current position */

    float px, py;            /* estimated position, meters from origin */
    float hdg, hdg_tgt;      /* displayed / target heading, degrees */
    float hsin, hcos;        /* sin/cos of the displayed heading, refreshed once per frame */
    float speed_ms;
    const nav_poly_t *polys; uint16_t npolys;
    float status_age;        /* seconds since the last STATUS packet from the phone */
    float scale, scale_tgt;  /* px per meter */
    float zoom_v;            /* smoothed speed (m/s) used to pick the zoom */
    float along_corr;        /* the part of the phone's latest position correction (m along the route) not yet applied; it glides in */
    float corr_x, corr_y;    /* the same off the route (m) */
    float zoom_dist;         /* distance to the next turn (m), worked out between the phone's updates from the speed */
    int zoom_dist_valid;
    float zoom_log, zoom_vel; /* the zoom as a spring: ln(px per meter) and its rate of change (nepers a second) */
    uint32_t zoom_t;         /* tick of the last zoom speed update */
    uint32_t last_tick;

    int route_seg;           /* segment the position projects onto */
    float proj_x, proj_y;    /* projected point on the route */
    float proj_along;        /* meters along the route at that point */
    float cross_m;           /* distance from the raw position to the route, meters */
    float cum[NAV_UI_MAX_ROUTE];   /* route length up to each point, meters */
    float along;             /* where the marker is on the route while it is locked to it */
    int snapped;             /* 1 = the marker follows the road instead of the raw position estimate */

    int metric;              /* 0 = feet / miles / mph / F (default), 1 = m / km / km/h / C */
    int clock24;             /* 0 = 12-hour clock with AM/PM (default), 1 = 24-hour */
    int bar_idx;             /* scale bar choice */
    float bar_px;

    nav_trip_t trip;
    float trip_refresh;
    float save_timer;        /* seconds since the last trip save */
    float stopped_s;         /* how long the vehicle has been standing still */
    int trip_dirty;          /* trip changed since the last save */
} U;

/* ---- small helpers ---------------------------------------------------- */
static void pos(lv_obj_t *o, int x, int y) { lv_obj_set_pos(o, S(x), S(y)); }     /* design pixels */
static void wid(lv_obj_t *o, int w) { lv_obj_set_width(o, S(w)); }

static lv_obj_t *plain(lv_obj_t *parent, int x, int y, int w, int h)           /* design pixels */
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, S(x), S(y));
    lv_obj_set_size(o, S(w), S(h));
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void fl_register(lv_obj_t *l);

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    fl_register(l);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    if (color == THEME_NIGHT.text || color == 0xE5EAF3 || color == 0xD6DCE8) theme_reg(l, ROLE_TEXT);
    else if (color == THEME_NIGHT.sub) theme_reg(l, ROLE_SUB);
    else if (color == THEME_NIGHT.dim) theme_reg(l, ROLE_DIM);
    return l;
}

static float wrap180(float a)
{
    while (a > 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

static void fmt_dist(char *out, size_t n, uint32_t m)
{
    if (U.metric) {
        if (m >= 1000) {
            if (m >= 10000) snprintf(out, n, "%lu km", (unsigned long)((m + 500) / 1000));
            else            snprintf(out, n, "%.1f km", m / 1000.0);
        } else if (m >= 100) {
            snprintf(out, n, "%lu m", (unsigned long)((m + 5) / 10 * 10));
        } else {
            snprintf(out, n, "%lu m", (unsigned long)((m + 2) / 5 * 5));
        }
        return;
    }
    float ft = m * 3.28084f;
    if (ft < 1000.0f) {
        int v = ft >= 200.0f ? (int)((ft + 25.0f) / 50.0f) * 50 : (int)((ft + 5.0f) / 10.0f) * 10;
        snprintf(out, n, "%d ft", v);
    } else {
        float mi = m / 1609.344f;
        if (mi >= 10.0f) snprintf(out, n, "%.0f mi", mi);
        else             snprintf(out, n, "%.1f mi", mi);
    }
}

static float speed_disp(float kmh) { return U.metric ? kmh : kmh * 0.621371f; }
static const char *speed_unit(void) { return U.metric ? "km/h" : "mph"; }

static const char *cardinal(float h)
{
    static const char *n[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return n[((int)((h + 22.5f) / 45.0f)) & 7];
}

/* ---- maneuver glyphs ----
 * Each glyph is one shape, a union of primitives evaluated as a signed distance per pixel, so edges are anti-aliased and seams consistent.
 * Rendered once per maneuver into an ARGB image (96 x 96 grid, scaled to GL_N pixels) and blitted each frame. */
#define GL_N   S(ARROW_S)
#define GL_SW  15.0f     /* stroke width, grid units */
#define GL_HW  40.0f     /* arrowhead width */
#define GL_HL  30.0f     /* arrowhead length */
#define GL_MAXP 16
#define GL_OV  1.5f     /* pieces overlap their neighbour by this much: shapes that merely touch leave a half-covered seam */

typedef struct { float x, y; } v2;
typedef enum { GK_BOX, GK_TRI, GK_ARC, GK_DISC, GK_RING } gk_t;
typedef struct { gk_t k; int layer; float a[9]; } gprim_t;     /* layer 0 = glyph, 1 = faint branch, 2 = white hole */

static lv_color32_t gl_buf[GL_N * GL_N];
static lv_image_dsc_t gl_img;
static gprim_t gp[GL_MAXP];
static int gnp;
static int gl_man = -1;
static nav_roundabout_t rb;                     /* the exits of the next roundabout, from the phone (count 0 = none known) */

/* signed distances in grid units, negative inside */
static float sd_box(v2 p, const float *a)      /* a: cx cy ux uy halflen halfwidth (u = unit direction of the length) */
{
    float dx = p.x - a[0], dy = p.y - a[1];
    float al = fabsf(dx * a[2] + dy * a[3]) - a[4];
    float ac = fabsf(-dx * a[3] + dy * a[2]) - a[5];
    float ox = fmaxf(al, 0), oy = fmaxf(ac, 0);
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(al, ac), 0);
}

static float sd_tri(v2 p, const float *a)      /* a: x0 y0 x1 y1 x2 y2, any winding */
{
    float d = -1e9f;
    for (int i = 0; i < 3; i++) {
        float x0 = a[i * 2], y0 = a[i * 2 + 1];
        float x1 = a[((i + 1) % 3) * 2], y1 = a[((i + 1) % 3) * 2 + 1];
        float xo = a[((i + 2) % 3) * 2], yo = a[((i + 2) % 3) * 2 + 1];
        float ex = x1 - x0, ey = y1 - y0, len = hypotf(ex, ey);
        float nx = ey / len, ny = -ex / len;                       /* a normal of this edge... */
        if ((xo - x0) * nx + (yo - y0) * ny > 0) { nx = -nx; ny = -ny; }   /* ...pointing away from the third vertex */
        float di = (p.x - x0) * nx + (p.y - y0) * ny;
        if (di > d) d = di;
    }
    return d;
}

static float sd_arc(v2 p, const float *a)      /* a: cx cy radius width startdeg sweepdeg (clockwise, 0 = 3 o'clock, sweep <= 180) */
{
    float dx = p.x - a[0], dy = p.y - a[1];
    float ring = fabsf(hypotf(dx, dy) - a[2]) - a[3] / 2;
    float a0 = a[4] * DEG2RAD, a1 = (a[4] + a[5]) * DEG2RAD;
    float h0 = -dx * sinf(a0) + dy * cosf(a0);                    /* >= 0 on the swept side of the start edge */
    float h1 = dx * sinf(a1) - dy * cosf(a1);                     /* >= 0 on the swept side of the end edge */
    float cut = a[5] >= 179.9f ? h0 : fminf(h0, h1);
    return fmaxf(ring, -cut);
}

static float sd_prim(const gprim_t *g, v2 p)
{
    switch (g->k) {
    case GK_BOX:  return sd_box(p, g->a);
    case GK_TRI:  return sd_tri(p, g->a);
    case GK_ARC:  return sd_arc(p, g->a);
    case GK_DISC: return hypotf(p.x - g->a[0], p.y - g->a[1]) - g->a[2];
    default:      return fabsf(hypotf(p.x - g->a[0], p.y - g->a[1]) - g->a[2]) - g->a[3] / 2;   /* full ring */
    }
}

static gprim_t *gadd(gk_t k, int layer)
{
    gprim_t *g = &gp[gnp++ < GL_MAXP - 1 ? gnp - 1 : GL_MAXP - 1];
    g->k = k; g->layer = layer;
    return g;
}

static void gadd_box(int layer, float ax, float ay, float bx, float by)     /* stroke-wide box from a to b */
{
    float dx = bx - ax, dy = by - ay, len = hypotf(dx, dy);
    if (len < 0.01f) return;
    gprim_t *g = gadd(GK_BOX, layer);
    g->a[0] = (ax + bx) / 2; g->a[1] = (ay + by) / 2; g->a[2] = dx / len; g->a[3] = dy / len;
    g->a[4] = len / 2; g->a[5] = GL_SW / 2;
}

static void gadd_thin(int layer, float ax, float ay, float bx, float by, float width)       /* a thinner stroke, for the road you are leaving */
{
    gadd_box(layer, ax, ay, bx, by);
    gp[gnp - 1].a[5] = width / 2;
}

static void gadd_head(int layer, float ex, float ey, float ux, float uy)
{
    float px = -uy, py = ux;
    gprim_t *g = gadd(GK_TRI, layer);
    g->a[0] = ex + px * GL_HW / 2; g->a[1] = ey + py * GL_HW / 2;
    g->a[2] = ex - px * GL_HW / 2; g->a[3] = ey - py * GL_HW / 2;
    g->a[4] = ex + ux * GL_HL;     g->a[5] = ey + uy * GL_HL;
}

/* A bent path starting at the bend (0,0) with the stem below: bend `theta` degrees (right positive, 180 = U-turn) on radius `r`, straight run `lo`, optional head. */
typedef struct { float minx, maxx, miny, maxy, ex, ey, ux, uy, ax, ay; } gl_path_t;

static void gl_expand(gl_path_t *b, float x, float y, float pad)
{
    if (x - pad < b->minx) b->minx = x - pad;
    if (x + pad > b->maxx) b->maxx = x + pad;
    if (y - pad < b->miny) b->miny = y - pad;
    if (y + pad > b->maxy) b->maxy = y + pad;
}

static void gl_path_geom(gl_path_t *b, float theta, float r, float lo, int head)
{
    float s = theta >= 0 ? 1.0f : -1.0f, a = fabsf(theta);
    b->minx = -GL_SW / 2; b->maxx = GL_SW / 2; b->miny = 0; b->maxy = 0;
    float px = 0, py = 0;
    for (float al = 0; al <= a + 0.01f; al += 5.0f) {
        float ang = (s > 0 ? 180.0f + al : -al) * DEG2RAD;
        px = s * r + r * cosf(ang);
        py = r * sinf(ang);
        gl_expand(b, px, py, GL_SW / 2);
    }
    float h = (-90.0f + s * a) * DEG2RAD;
    b->ux = cosf(h); b->uy = sinf(h);
    b->ax = px; b->ay = py;
    b->ex = px + b->ux * lo; b->ey = py + b->uy * lo;
    gl_expand(b, b->ex, b->ey, GL_SW / 2);
    if (head) {
        float qx = -b->uy, qy = b->ux;
        gl_expand(b, b->ex + b->ux * GL_HL, b->ey + b->uy * GL_HL, 0);
        gl_expand(b, b->ex + qx * GL_HW / 2, b->ey + qy * GL_HW / 2, 0);
        gl_expand(b, b->ex - qx * GL_HW / 2, b->ey - qy * GL_HW / 2, 0);
    }
}

static void gl_path_build(float theta, float r, float lo, int head, int layer, float xs, float yc)
{
    gl_path_t b;
    gl_path_geom(&b, theta, r, lo, head);
    float s = theta >= 0 ? 1.0f : -1.0f, a = fabsf(theta);
    gadd_box(layer, xs, 92, xs, yc - GL_OV);                                    /* stem, reaching into the bend / head */
    if (a > 0.5f) {
        gprim_t *g = gadd(GK_ARC, layer);
        g->a[0] = xs + s * r; g->a[1] = yc; g->a[2] = r; g->a[3] = GL_SW;
        g->a[4] = s > 0 ? 180.0f : 360.0f - a; g->a[5] = a;
    }
    if (lo > 0.5f)                                                              /* straight run, overlapping bend and head */
        gadd_box(layer, xs + b.ax - b.ux * GL_OV, yc + b.ay - b.uy * GL_OV, xs + b.ex + b.ux * GL_OV, yc + b.ey + b.uy * GL_OV);
    if (head) gadd_head(layer, xs + b.ex, yc + b.ey, b.ux, b.uy);
}

static void gl_place(float th1, float r1, float lo1, int head1, int two, float th2, float *xs, float *yc)
{
    gl_path_t a, b;
    gl_path_geom(&a, th1, r1, lo1, head1);
    if (two) {
        gl_path_geom(&b, th2, r1, lo1, 0);
        if (b.minx < a.minx) a.minx = b.minx;
        if (b.maxx > a.maxx) a.maxx = b.maxx;
        if (b.miny < a.miny) a.miny = b.miny;
    }
    *xs = 48.0f - (a.minx + a.maxx) / 2;          /* centered horizontally */
    *yc = 8.0f - a.miny;                          /* topmost point lands at y = 8 */
    if (*yc > 82.0f) *yc = 82.0f;
}

static void gl_build(int man)
{
    gnp = 0;
    float xs, yc;
    if (man == NAV_MAN_ARRIVE) {
        /* destination pin: a teardrop (disc + triangle to the tip) with a white center */
        const float cx = 48, cy = 38, r = 27, tipy = 90;
        float dy = tipy - cy, tl = sqrtf(dy * dy - r * r), al = asinf(r / dy);
        float ax = sinf(al), ay = -cosf(al);                          /* unit vector from the tip along one tangent */
        gprim_t *d = gadd(GK_DISC, 0); d->a[0] = cx; d->a[1] = cy; d->a[2] = r;
        gprim_t *t = gadd(GK_TRI, 0);
        t->a[0] = cx + ax * tl; t->a[1] = tipy + ay * tl;
        t->a[2] = cx - ax * tl; t->a[3] = tipy + ay * tl;
        t->a[4] = cx;           t->a[5] = tipy;
        gprim_t *h = gadd(GK_DISC, 2); h->a[0] = cx; h->a[1] = cy; h->a[2] = 11;
        return;
    }
    if (man == NAV_MAN_ROUNDABOUT) {
        /* Roundabout: a thin ring, the entry road at the bottom, existing exits as faint stubs, and the route in bold (in, counter-clockwise round the ring, out by the exit to take, with the arrowhead).
         * With no exit data (count 0) it is just the ring and the entry road. */
        const float cx = 48, cy = 46, R = 17, RW = 6, PW = 10;
        gprim_t *ring = gadd(GK_RING, 1); ring->a[0] = cx; ring->a[1] = cy; ring->a[2] = R; ring->a[3] = RW;
        gadd_thin(0, cx, 92, cx, cy + R, PW);                                               /* the road in, ending on the ring's centre line... */
        { gprim_t *j = gadd(GK_DISC, 0); j->a[0] = cx; j->a[1] = cy + R; j->a[2] = PW / 2; }       /* ...with a round joint: no square corners or steps */
        int n = rb.count > NAV_RB_MAX_EXITS ? NAV_RB_MAX_EXITS : rb.count;
        int taken = (n > 0 && rb.taken < n) ? rb.taken : -1;
        for (int i = 0; i < n; i++) {
            if (i == taken) continue;
            float al = rb.dir2[i] * 2.0f * DEG2RAD, ux = sinf(al), uy = -cosf(al);          /* clockwise from straight ahead (up) */
            gadd_thin(1, cx + ux * R, cy + uy * R, cx + ux * (R + 19), cy + uy * (R + 19), 8);   /* an exit that exists, not taken */
        }
        if (taken >= 0) {
            float deg = rb.dir2[taken] * 2.0f, al = deg * DEG2RAD, ux = sinf(al), uy = -cosf(al);
            float sweep = 180.0f - deg;                                                    /* from the exit round to the entry at the bottom */
            while (sweep <= 0) sweep += 360.0f;
            while (sweep > 340.0f) sweep -= 20.0f;                                          /* a U-turn exit: nearly all the way round */
            float start = fmodf(deg - 90.0f + 720.0f, 360.0f);                              /* screen angle of the exit (0 = 3 o'clock, clockwise) */
            float left = sweep;
            while (left > 0.1f) {
                float part = left > 170.0f ? 170.0f : left;                                  /* pieces under 180 so each keeps both of its cut edges */
                gprim_t *a = gadd(GK_ARC, 0); a->a[0] = cx; a->a[1] = cy; a->a[2] = R; a->a[3] = PW;
                a->a[4] = fmodf(start + (sweep - left), 360.0f); a->a[5] = part + (left > part ? 5.0f : 0.0f);   /* overlap the next piece: no hairline seam */
                left -= part;
            }
            gadd_thin(0, cx + ux * R, cy + uy * R, cx + ux * (R + 12 + GL_OV), cy + uy * (R + 12 + GL_OV), PW);   /* the exit, from the ring's centre line... */
            { gprim_t *j = gadd(GK_DISC, 0); j->a[0] = cx + ux * R; j->a[1] = cy + uy * R; j->a[2] = PW / 2; }     /* ...with a round joint */
            float px = -uy, py = ux, ex = cx + ux * (R + 12), ey = cy + uy * (R + 12);
            gprim_t *h = gadd(GK_TRI, 0);                                                   /* arrowhead, pointing out of the ring */
            h->a[0] = ex + px * 12; h->a[1] = ey + py * 12;
            h->a[2] = ex - px * 12; h->a[3] = ey - py * 12;
            h->a[4] = ex + ux * 15; h->a[5] = ey + uy * 15;
        }
        return;
    }
    if (man == NAV_MAN_MERGE || man == NAV_MAN_MERGE_LEFT || man == NAV_MAN_MERGE_RIGHT) {
        /* Merge: the main road runs straight up (faint below the join, bold above); your lane slides in beside it, with the arrowhead on the road you merge onto.
         * An unknown side (NAV_MAN_MERGE) is drawn as a left merge. */
        const float sgn = man == NAV_MAN_MERGE_RIGHT ? 1.0f : -1.0f;
        const float PW = 11, mainx = 48 + sgn * 13, laneX = 48 - sgn * 13;                 /* the main road and your lane, 26 apart */
        gadd_thin(1, mainx, 92, mainx, 40, 8);                                               /* the main road below the join, faint, running up to meet the bold path */
        gadd_thin(0, laneX, 92, laneX, 64, PW);                                              /* your lane in */
        { gprim_t *j = gadd(GK_DISC, 0); j->a[0] = laneX; j->a[1] = 64; j->a[2] = PW / 2; }
        gadd_thin(0, laneX, 64, mainx, 40, PW);                                              /* the slide across */
        { gprim_t *j = gadd(GK_DISC, 0); j->a[0] = mainx; j->a[1] = 40; j->a[2] = PW / 2; }
        gadd_thin(0, mainx, 40, mainx, 26, PW);                                              /* on up the main road */
        gprim_t *h = gadd(GK_TRI, 0);                                                         /* arrowhead */
        h->a[0] = mainx - 14; h->a[1] = 28; h->a[2] = mainx + 14; h->a[3] = 28; h->a[4] = mainx; h->a[5] = 6;
        return;
    }
    float theta = 0, r = 26, lo = 0;
    int fork = 0, exitg = 0;
    switch (man) {
    case NAV_MAN_LEFT:         theta = -90;  r = 20; lo = 12; break;
    case NAV_MAN_RIGHT:        theta = 90;   r = 20; lo = 12; break;
    case NAV_MAN_SLIGHT_LEFT:  theta = -45;  r = 30; lo = 20; break;
    case NAV_MAN_SLIGHT_RIGHT: theta = 45;   r = 30; lo = 20; break;
    case NAV_MAN_SHARP_LEFT:   theta = -135; r = 17; lo = 6;  break;
    case NAV_MAN_SHARP_RIGHT:  theta = 135;  r = 17; lo = 6;  break;
    case NAV_MAN_UTURN:        theta = 180;  r = 17; lo = 10; break;
    case NAV_MAN_EXIT_LEFT:    theta = -32;  r = 32; lo = 24; exitg = 1; break;       /* the ramp peels off gently; the road keeps going straight, faint */
    case NAV_MAN_EXIT_RIGHT:   theta = 32;   r = 32; lo = 24; exitg = 1; break;
    case NAV_MAN_FORK_LEFT:    theta = -35;  r = 22; lo = 20; fork = 1; break;
    case NAV_MAN_FORK_RIGHT:   theta = 35;   r = 22; lo = 20; fork = 1; break;
    default: break;                                          /* straight / keep */
    }
    gl_place(theta, r, lo, 1, fork, -theta, &xs, &yc);
    if (fork) gl_path_build(-theta, r, lo, 0, 1, xs, yc);    /* the branch you are NOT taking, drawn faint */
    if (exitg) gadd_thin(1, xs, yc + GL_OV, xs, 10.0f, 10.0f);   /* an exit: the road you are leaving continues straight on, thin and faint */
    gl_path_build(theta, r, lo, 1, 0, xs, yc);
}

static float gl_cov(float sd_grid, float k)                   /* pixel coverage from a signed distance */
{
    float c = 0.5f - sd_grid * k;
    return c < 0 ? 0 : (c > 1 ? 1 : c);
}

/* Rasterize maneuver `man` into gl_buf: one anti-aliased image, accent-colored, white where a glyph has a hole. */
static void gl_render_impl(int man);
uint32_t nav_gl_render_us;                 /* the slowest turn-glyph render since the last report (microseconds) */
static void gl_render(int man)
{
    const uint32_t t0 = PERF_NOW();
    gl_render_impl(man);
    const uint32_t us = PERF_NOW() - t0;
    if (us > nav_gl_render_us) nav_gl_render_us = us;
}

/* Renders rows y0 .. y1-1 of the glyph (the primitives from gl_build) into dst, a GL_N x GL_N image. */
int nav_gl_cull = 1;                    /* 1 (default): skip shapes too far from a pixel to touch it; 0 tests every shape for every pixel (tests: same image) */

static void gl_render_rows(int y0, int y1, lv_color32_t *dst)
{
    const float k = (float)GL_N / 96.0f;
    const uint8_t cr = (C_ACCENT >> 16) & 0xFF, cg = (C_ACCENT >> 8) & 0xFF, cb = C_ACCENT & 0xFF;
    /* A shape more than half a pixel (0.5 / k grid units) away gives no coverage, so each shape (except triangles) gets a bounding circle and pixels beyond it skip the shape; same image, faster. */
    float cx[GL_MAXP] = {0}, cy[GL_MAXP] = {0}, lim2[GL_MAXP] = {0};      /* (zeroed: a triangle sets none of them, and a stale value here could make a pixel skip it) */
    for (int i = 0; i < gnp && i < GL_MAXP; i++) {
        float r = -1;
        const float *a = gp[i].a;
        switch (gp[i].k) {
        case GK_BOX:  cx[i] = a[0]; cy[i] = a[1]; r = hypotf(a[4], a[5]); break;
        case GK_DISC: cx[i] = a[0]; cy[i] = a[1]; r = a[2]; break;
        case GK_ARC:  cx[i] = a[0]; cy[i] = a[1]; r = a[2] + a[3] / 2; break;
        case GK_RING: cx[i] = a[0]; cy[i] = a[1]; r = a[2] + a[3] / 2; break;
        default: break;
        }
        const float lim = r + 1.0f / k;
        lim2[i] = (r < 0 || !nav_gl_cull) ? 1e30f : lim * lim;
    }
    for (int y = y0; y < y1; y++) {
        for (int x = 0; x < GL_N; x++) {
            v2 p = {(x + 0.5f) / k, (y + 0.5f) / k};
            float d0 = 1e9f, d1 = 1e9f, d2 = 1e9f;
            for (int i = 0; i < gnp && i < GL_MAXP; i++) {
                const float ddx = p.x - cx[i], ddy = p.y - cy[i];
                if (ddx * ddx + ddy * ddy > lim2[i]) continue;
                float d = sd_prim(&gp[i], p);
                if (gp[i].layer == 0) { if (d < d0) d0 = d; }
                else if (gp[i].layer == 1) { if (d < d1) d1 = d; }
                else if (d < d2) d2 = d;
            }
            float a0 = gl_cov(d0, k), a1 = 0.35f * gl_cov(d1, k), hole = gl_cov(d2, k);
            float alpha = a0 > a1 ? a0 : a1;
            lv_color32_t *px = &dst[y * GL_N + x];
            px->red   = (uint8_t)lroundf(cr + (255 - cr) * hole);
            px->green = (uint8_t)lroundf(cg + (255 - cg) * hole);
            px->blue  = (uint8_t)lroundf(cb + (255 - cb) * hole);
            px->alpha = (uint8_t)lroundf(alpha * 255.0f);
        }
    }
}

static void gl_publish(int man)
{
    gl_img.header.magic = LV_IMAGE_HEADER_MAGIC;
    gl_img.header.cf = LV_COLOR_FORMAT_ARGB8888;
    gl_img.header.w = GL_N;
    gl_img.header.h = GL_N;
    gl_img.header.stride = GL_N * 4;
    gl_img.data = (const uint8_t *)gl_buf;
    gl_img.data_size = sizeof(gl_buf);
    gl_man = man;
}

static void gl_render_impl(int man)
{
    gl_build(man);
    gl_render_rows(0, GL_N, gl_buf);
    gl_publish(man);
}

/* The same render spread over many frames (in one go it took 45 to 87 ms and froze the screen at every corner): a few ms of rows per tick into a PSRAM scratch image, swapped in when complete.
 * The arrow is hidden meanwhile if the maneuver changed; a recolor or roundabout update restarts the job. */
static lv_color32_t *gl_work;
static int gl_job_man = -1, gl_job_row, gl_job_bake;      /* gl_job_bake: the job only fills the cache below and shows nothing */
static uint32_t gl_job_t0;
uint32_t nav_gl_job_ms;                     /* the longest wait from a new maneuver to its arrow being ready, since the last report */
static uint32_t gl_job_accent;

/* Finished arrows are kept in PSRAM (one per maneuver type, ~21 KB each at 360 px; roundabouts are never kept) so a maneuver seen before shows at once.
 * Arrows not yet kept are drawn in the background, commonest first, when nothing else is drawing; a theme change redraws them. */
#define GL_MAN_COUNT 18
static lv_color32_t *gl_cache[GL_MAN_COUNT];
static uint32_t gl_cache_accent[GL_MAN_COUNT];
uint32_t nav_gl_cache_stats[3];             /* arrows shown from the cache, maneuver changes that had to be drawn, arrows drawn into the cache */
static int gl_bake_off;                     /* no memory for the scratch image: no background drawing */
static const uint8_t gl_bake_order[] = { NAV_MAN_LEFT, NAV_MAN_RIGHT, NAV_MAN_STRAIGHT, NAV_MAN_SLIGHT_LEFT, NAV_MAN_SLIGHT_RIGHT, NAV_MAN_KEEP,
                                         NAV_MAN_SHARP_LEFT, NAV_MAN_SHARP_RIGHT, NAV_MAN_UTURN, NAV_MAN_FORK_LEFT, NAV_MAN_FORK_RIGHT, NAV_MAN_EXIT_LEFT,
                                         NAV_MAN_EXIT_RIGHT, NAV_MAN_MERGE_LEFT, NAV_MAN_MERGE_RIGHT, NAV_MAN_MERGE, NAV_MAN_ARRIVE };

static int gl_cacheable(int man) { return man >= 0 && man < GL_MAN_COUNT && man != NAV_MAN_ROUNDABOUT; }
static int gl_cached_ok(int man) { return gl_cacheable(man) && gl_cache[man] && gl_cache_accent[man] == C_ACCENT; }

static void gl_cache_store(int man, const lv_color32_t *img, uint32_t accent)
{
    if (!gl_cacheable(man)) return;
    if (!gl_cache[man]) gl_cache[man] = (lv_color32_t *)(nav_ui_img_alloc ? nav_ui_img_alloc(sizeof(gl_buf)) : malloc(sizeof(gl_buf)));
    if (!gl_cache[man]) return;
    memcpy(gl_cache[man], img, sizeof(gl_buf));
    gl_cache_accent[man] = accent;
    nav_gl_cache_stats[2]++;
}

static void gl_show(const lv_color32_t *img, int man)             /* swap a finished image in and show the arrow */
{
    memcpy(gl_buf, img, sizeof(gl_buf));
    gl_publish(man);
    if (U.arrow) { if (!U.free_mode) lv_obj_remove_flag(U.arrow, LV_OBJ_FLAG_HIDDEN); lv_obj_invalidate(U.arrow); }
}

static void gl_start_job(int man, int hide)
{
    if (hide && gl_cached_ok(man)) {                                 /* seen before in this color: no wait at all */
        gl_job_man = -1; gl_job_bake = 0;
        gl_show(gl_cache[man], man);
        nav_gl_cache_stats[0]++;
        return;
    }
    if (hide) nav_gl_cache_stats[1]++;
    gl_job_man = man; gl_job_row = 0; gl_job_bake = 0; gl_job_accent = C_ACCENT; gl_job_t0 = lv_tick_get();
    if (hide && U.arrow && !U.free_mode) lv_obj_add_flag(U.arrow, LV_OBJ_FLAG_HIDDEN);
}

static void gl_job_step(void)
{
    if (gl_job_man < 0) {                                            /* nothing wanted: draw the next arrow that is not kept yet, in the background */
        if (gl_bake_off) return;
        int next = -1;
        for (unsigned i = 0; i < sizeof gl_bake_order; i++) if (!gl_cached_ok(gl_bake_order[i])) { next = gl_bake_order[i]; break; }
        if (next < 0) return;
        gl_job_man = next; gl_job_row = 0; gl_job_bake = 1; gl_job_accent = C_ACCENT; gl_job_t0 = lv_tick_get();
    }
    if (!gl_work) gl_work = (lv_color32_t *)(nav_ui_img_alloc ? nav_ui_img_alloc(sizeof(gl_buf)) : malloc(sizeof(gl_buf)));
    if (!gl_work) {                                                  /* no memory for the scratch image: the old way, all at once (and no background drawing) */
        const int man = gl_job_man, bake = gl_job_bake;
        gl_job_man = -1; gl_job_bake = 0; gl_bake_off = 1;
        if (!bake) {
            gl_render_impl(man);
            if (U.arrow) { if (!U.free_mode) lv_obj_remove_flag(U.arrow, LV_OBJ_FLAG_HIDDEN); lv_obj_invalidate(U.arrow); }
        }
        return;
    }
    if (gl_job_accent != C_ACCENT) { gl_job_accent = C_ACCENT; gl_job_row = 0; }       /* the theme changed in the middle: start again in the new color */
    const uint32_t t0 = PERF_NOW();
    gl_build(gl_job_man);
    const uint32_t tick0 = lv_tick_get();
    /* a few milliseconds a tick; background drawing while the bike is moving takes only 2 so the frames hardly notice it */
    const uint32_t budget = (gl_job_bake && U.speed_ms >= 1.0f) ? 2 : 6;
    do { gl_render_rows(gl_job_row, gl_job_row + 1, gl_work); gl_job_row++; } while (gl_job_row < GL_N && lv_tick_get() - tick0 < budget);
    if (gl_job_row >= GL_N) {
        gl_cache_store(gl_job_man, gl_work, gl_job_accent);
        if (!gl_job_bake) {
            gl_show(gl_work, gl_job_man);
            if (lv_tick_get() - gl_job_t0 > nav_gl_job_ms) nav_gl_job_ms = lv_tick_get() - gl_job_t0;
        }
        gl_job_man = -1; gl_job_bake = 0;
    }
    const uint32_t us = PERF_NOW() - t0;
    if (us > nav_gl_render_us) nav_gl_render_us = us;
}

/* A maneuver change from the phone: show the kept arrow or start drawing it. (A background job for the same maneuver does not count: it shows nothing.) */
static void gl_request(int man)
{
    if (gl_job_man >= 0 && !gl_job_bake) {                           /* a real job is pending (and the arrow may be hidden meanwhile) */
        if (gl_job_man == man) return;                               /* already on its way */
        if (man == gl_man) {                                         /* back to the maneuver that is shown: forget the pending one */
            gl_job_man = -1;
            if (U.arrow) { if (!U.free_mode) lv_obj_remove_flag(U.arrow, LV_OBJ_FLAG_HIDDEN); lv_obj_invalidate(U.arrow); }
            return;
        }
    } else if (man == gl_man) return;                                /* already shown (a background job does not matter) */
    gl_start_job(man, 1);
}

#ifndef ESP_PLATFORM       /* (simulator only: the reference image below would take 21 KB of the board's last few KB of internal RAM) */
/* Test hook: checks the cache against a fresh render for every kept-able maneuver; returns how many differ (0 = all good). */
int nav_ui_debug_glyph_cache_check(void)
{
    static lv_color32_t ref[GL_N * GL_N];
    int bad = 0;
    for (int man = 0; man < GL_MAN_COUNT; man++) {
        if (!gl_cacheable(man)) continue;
        gl_render(man); memcpy(ref, gl_buf, sizeof ref);                 /* the one-go render is the reference */
        gl_cache_accent[man] = 0;
        gl_start_job(man, 1);                                            /* a miss: drawn in slices */
        for (int guard = 0; guard < 100000 && gl_job_man >= 0; guard++) gl_job_step();
        memset(gl_buf, 0, sizeof gl_buf);
        gl_man = -1;
        gl_start_job(man, 1);                                            /* now a hit: copied from the cache */
        if (gl_man != man || memcmp(gl_buf, ref, sizeof ref)) {
            int nd = 0; for (int i = 0; i < GL_N * GL_N; i++) if (memcmp(&gl_buf[i], &ref[i], sizeof gl_buf[0])) nd++;
            bad++; printf("glyph %d: the kept arrow differs from a fresh render (shown man %d, %d pixels differ)\n", man, gl_man, nd);
        }
    }
    return bad;
}
#endif

void nav_ui_set_roundabout(const nav_roundabout_t *r)
{
    rb = *r;
    if (gl_man == NAV_MAN_ROUNDABOUT || gl_job_man == NAV_MAN_ROUNDABOUT) gl_start_job(NAV_MAN_ROUNDABOUT, 0);     /* it is on screen now: draw it again (in the background) */
}

/* Test hook: the rendered glyph for `man` (GL_N x GL_N, ARGB). */
const lv_color32_t *nav_ui_debug_glyph(int man) { if (man != gl_man) gl_render(man); return gl_buf; }

static void arrow_draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    d.src = &gl_img;
    lv_area_t dst = {a.x1, a.y1, a.x1 + GL_N - 1, a.y1 + GL_N - 1};
    lv_draw_image(lv_event_get_layer(e), &d, &dst);
}


/* ---- position marker ----
 * It never rotates (the map turns under it), so it is rasterized once into an ARGB image (disc, ring, shadow, chevron), re-rendered on a theme change, and placed each frame. */
#define MK_MAX 96
static lv_color32_t *mk_buf;                      /* allocated once, in PSRAM on the board (nav_ui_img_alloc): 37 KB of static internal RAM starved the Bluetooth controller */
void *(*nav_ui_img_alloc)(size_t bytes) = NULL;
static lv_image_dsc_t mk_img;
static int mk_n;

static float mk_seg(float px, float py, float ax, float ay, float bx, float by)      /* distance from p to segment a-b */
{
    float vx = bx - ax, vy = by - ay, t = ((px - ax) * vx + (py - ay) * vy) / (vx * vx + vy * vy);
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return hypotf(px - (ax + t * vx), py - (ay + t * vy));
}

/* The map is redrawn on a tick only if the picture moved, turned, or zoomed (measured against the last drawing, so slow drift adds up) or `map_dirty` was set; a parked bike is not repainted. */
static int map_dirty = 1;
static struct { float px, py, hdg, scale; } map_seen;

static float mk_cov(float sd) { float c = 0.5f - sd; return c < 0 ? 0 : (c > 1 ? 1 : c); }      /* pixel coverage from a signed distance (1 px = 1 unit) */

static void marker_render(void)
{
    const float R = 17.0f * MBX;                       /* disc radius, including the ring */
    const float ring = (float)S(3);                    /* white ring width */
    const float shadow = (float)S(10);                 /* shadow reaches this far beyond the disc */
    const float ch = 7.0f * MBX, cl = 6.0f * MBX, hw = (float)BIGPX(4) * 0.5f;      /* chevron: half width and half height; stroke half width */
    int n = 2 * (int)ceilf(R + shadow) + 2;
    if (n > MK_MAX) n = MK_MAX;
    if (!mk_buf) {
        size_t bytes = (size_t)MK_MAX * MK_MAX * sizeof(lv_color32_t);        /* room for the biggest size: the marker is redrawn when the mode changes */
        mk_buf = nav_ui_img_alloc ? nav_ui_img_alloc(bytes) : malloc(bytes);
        if (!mk_buf) { mk_n = 0; return; }           /* no memory: the marker is simply not drawn */
    }
    mk_n = n;
    map_dirty = 1;
    const float c = n / 2.0f;
    const float ar = ((C_ACCENT >> 16) & 255) / 255.0f, ag = ((C_ACCENT >> 8) & 255) / 255.0f, ab = (C_ACCENT & 255) / 255.0f;
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            float px = x + 0.5f - c, py = y + 0.5f - c;
            float r = hypotf(px, py);
            /* shadow: black, soft, only outside the disc */
            float sh = r > R ? expf(-powf((r - R) / (shadow * 0.35f), 2.0f)) * 0.20f : 0.20f;
            float oa = sh, or_ = 0, og = 0, ob = 0;                 /* the "over" accumulator: alpha and premultiplied color */
            #define OVER(cov, cr, cg, cb) do { float a_ = (cov); or_ = (cr) * a_ + or_ * (1 - a_); og = (cg) * a_ + og * (1 - a_); ob = (cb) * a_ + ob * (1 - a_); oa = a_ + oa * (1 - a_); } while (0)
            OVER(mk_cov(r - R), 1.0f, 1.0f, 1.0f);                  /* white disc (its outer part is the ring) */
            OVER(mk_cov(r - (R - ring)), ar, ag, ab);               /* accent inside the ring ... */
            float dch = fminf(mk_seg(px, py, -ch, cl, 0, -ch), mk_seg(px, py, 0, -ch, ch, cl)) - hw;
            OVER(mk_cov(dch), 1.0f, 1.0f, 1.0f);                    /* ... and the white chevron on top */
            #undef OVER
            lv_color32_t *q = &mk_buf[y * mk_n + x];
            float inv = oa > 0.001f ? 1.0f / oa : 0;
            q->red = (uint8_t)lroundf(fminf(1, or_ * inv * 1.0f) * 255); q->green = (uint8_t)lroundf(fminf(1, og * inv) * 255);
            q->blue = (uint8_t)lroundf(fminf(1, ob * inv) * 255);
            q->alpha = (uint8_t)lroundf(fminf(1, oa) * 255);
        }
    }
    mk_img.header.magic = LV_IMAGE_HEADER_MAGIC;
    mk_img.header.cf = LV_COLOR_FORMAT_ARGB8888;
    mk_img.header.w = n; mk_img.header.h = n; mk_img.header.stride = n * 4;
    mk_img.data = (const uint8_t *)mk_buf;
    mk_img.data_size = (uint32_t)(n * n * 4);
}

/* ---- map -------------------------------------------------------------- */
static void to_screen(float wx, float wy, float ox, float oy, float *sx, float *sy)
{
    float dx = wx - U.px, dy = wy - U.py;
    float fwd = dx * U.hsin + dy * U.hcos;
    float rgt = dx * U.hcos - dy * U.hsin;
    *sx = ox + rgt * U.scale;
    *sy = oy - fwd * U.scale;
}

/* Cheap trivial-reject tests against the region LVGL is rendering (one horizontal band in partial rendering): off-screen and other-band segments are skipped; output identical. */
static int seg_outside(const lv_area_t *c, float x1, float y1, float x2, float y2, float pad)
{
    if (!nav_ui_cull_enabled) return 0;
    if (x1 < c->x1 - pad && x2 < c->x1 - pad) return 1;
    if (x1 > c->x2 + pad && x2 > c->x2 + pad) return 1;
    if (y1 < c->y1 - pad && y2 < c->y1 - pad) return 1;
    if (y1 > c->y2 + pad && y2 > c->y2 + pad) return 1;
    return 0;
}

int nav_ui_area_scanline = 1;   /* 1 = fast span fill (default); 0 = LVGL triangles, kept for A/B tests */

/* Fill a convex quad with horizontal spans: LVGL's triangle fill (per-pixel edge mask) cost >100 ms for a screen-sized area.
 * Rows with identical spans are merged; edges are not anti-aliased. */
static void fill_quad_spans(lv_layer_t *layer, const lv_area_t *clip,
                            const float cx[4], const float cy[4], lv_color_t color)
{
    float miny = cy[0], maxy = cy[0];
    for (int k = 1; k < 4; k++) {
        if (cy[k] < miny) miny = cy[k];
        if (cy[k] > maxy) maxy = cy[k];
    }
    int y0 = (int)ceilf(miny - 0.5f), y1 = (int)floorf(maxy - 0.5f);
    if (y0 < clip->y1) y0 = clip->y1;
    if (y1 > clip->y2) y1 = clip->y2;
    if (y0 > y1) return;

    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = color;
    rd.bg_opa = LV_OPA_COVER;

    int have = 0, rx1 = 0, rx2 = 0, rystart = 0;
    for (int y = y0; y <= y1 + 1; y++) {                 /* one extra pass flushes the last run */
        int x1i = 1, x2i = 0, ok = 0;
        if (y <= y1) {
            float yc = y + 0.5f, xmin = 1e9f, xmax = -1e9f;
            for (int e = 0; e < 4; e++) {
                int f = (e + 1) & 3;
                float ya = cy[e], yb = cy[f];
                if ((yc < ya && yc < yb) || (yc >= ya && yc >= yb)) continue;   /* half-open: no double count */
                float xi = cx[e] + (yc - ya) * (cx[f] - cx[e]) / (yb - ya);
                if (xi < xmin) xmin = xi;
                if (xi > xmax) xmax = xi;
            }
            if (xmax >= xmin) {
                x1i = (int)ceilf(xmin - 0.5f);
                x2i = (int)floorf(xmax - 0.5f);
                if (x1i < clip->x1) x1i = clip->x1;
                if (x2i > clip->x2) x2i = clip->x2;
                ok = x2i >= x1i;
            }
        }
        if (ok && have && x1i == rx1 && x2i == rx2) continue;            /* same span: extend the run */
        if (have) {
            lv_area_t r = {rx1, rystart, rx2, y - 1};
            lv_draw_rect(layer, &rd, &r);
            have = 0;
        }
        if (ok) { rx1 = x1i; rx2 = x2i; rystart = y; have = 1; }
    }
}

int nav_ui_line_chunk = 1;   /* 1 = draw long slanted lines in short pieces (default); 0 = whole lines, for A/B tests */

/* ---- the route line with exact edges ----
 * Two 8-bit coverage masks (white casing, colored core) are filled for the piece being drawn and blended tinted, giving even width with round ends and corners. The masks live in PSRAM. */
#define ROUTE_AA_MAX 800
static uint8_t *rt_mask[6];                      /* 0 route casing, 1 route core, 2 minor roads, 3 major roads, 4 slow traffic, 5 stopped traffic: separate, because drawing is queued and runs later */
static float *rt_x, *rt_y;                      /* the route's points in screen coordinates (ROUTE_AA_MAX each) */
static lv_image_dsc_t rt_dsc[6];

static int route_masks_ready(void)
{
    if (rt_mask[0]) return 1;
    /* everything here is allocated from the image allocator (PSRAM on the board): internal RAM is needed by the Bluetooth controller */
    size_t bytes = (size_t)SZ * SZ;
    uint8_t *m[6];
    for (int i = 0; i < 6; i++) m[i] = nav_ui_img_alloc ? nav_ui_img_alloc(bytes) : malloc(bytes);
    float *fx = nav_ui_img_alloc ? nav_ui_img_alloc(ROUTE_AA_MAX * sizeof(float)) : malloc(ROUTE_AA_MAX * sizeof(float));
    float *fy = nav_ui_img_alloc ? nav_ui_img_alloc(ROUTE_AA_MAX * sizeof(float)) : malloc(ROUTE_AA_MAX * sizeof(float));
    if (!m[0] || !m[1] || !m[2] || !m[3] || !m[4] || !m[5] || !fx || !fy) return 0;                          /* (a failed attempt leaks what it got; it is only tried again next frame) */
    for (int i = 0; i < 6; i++) rt_mask[i] = m[i];
    rt_x = fx; rt_y = fy;
    return 1;
}

/* Blends an 8-bit coverage mask (w x h, `stride` bytes a row) over the layer's pixels at (x0, y0) in `col`, writing the pixels directly: LVGL's own mask blend walked every pixel of the box.
 * Empty runs skip four at a time, solid pixels are plain stores, only soft edges blend. Returns 0 for a pixel format it does not handle (LVGL then does it). */
static inline uint32_t div255(uint32_t v) { v += 128; return (v + (v >> 8)) >> 8; }

static int mask_blit(lv_layer_t *layer, const uint8_t *mask, int w, int h, int stride, int32_t x0, int32_t y0, lv_color_t col)
{
    const lv_draw_buf_t *db = layer->draw_buf;
    if (!db || w <= 0 || h <= 0) return 0;
    const lv_color_format_t cf = db->header.cf;
    int bpp;
    if (cf == LV_COLOR_FORMAT_RGB888) bpp = 3;
    else if (cf == LV_COLOR_FORMAT_RGB565) bpp = 2;
    else if (cf == LV_COLOR_FORMAT_XRGB8888 || cf == LV_COLOR_FORMAT_ARGB8888) bpp = 4;
    else return 0;
    if (x0 < layer->buf_area.x1 || y0 < layer->buf_area.y1 || x0 + w - 1 > layer->buf_area.x2 || y0 + h - 1 > layer->buf_area.y2) return 0;
    uint8_t *base = (uint8_t *)lv_draw_layer_go_to_xy(layer, x0 - layer->buf_area.x1, y0 - layer->buf_area.y1);
    if (!base) return 0;
    const uint32_t dstride = db->header.stride;
    const uint32_t cr = col.red, cg = col.green, cb = col.blue;
    const uint16_t c565 = (uint16_t)(((cr & 0xF8) << 8) | ((cg & 0xFC) << 3) | (cb >> 3));
    for (int y = 0; y < h; y++) {
        const uint8_t *m = mask + (size_t)y * stride;
        uint8_t *d = base + (size_t)y * dstride;
        int x = 0;
        while (x < w) {
            if (x + 4 <= w && (((uintptr_t)(m + x)) & 3) == 0) {
                const uint32_t q = *(const uint32_t *)(const void *)(m + x);
                if (q == 0) { x += 4; continue; }
            }
            const uint32_t a = m[x];
            if (a) {
                uint8_t *px = d + (size_t)x * bpp;
                if (bpp == 3) {
                    if (a == 255) { px[0] = (uint8_t)cb; px[1] = (uint8_t)cg; px[2] = (uint8_t)cr; }
                    else { const uint32_t ia = 255 - a;
                           px[0] = (uint8_t)div255(px[0] * ia + cb * a); px[1] = (uint8_t)div255(px[1] * ia + cg * a); px[2] = (uint8_t)div255(px[2] * ia + cr * a); }
                } else if (bpp == 4) {
                    if (a == 255) { px[0] = (uint8_t)cb; px[1] = (uint8_t)cg; px[2] = (uint8_t)cr; px[3] = 255; }
                    else { const uint32_t ia = 255 - a;
                           px[0] = (uint8_t)div255(px[0] * ia + cb * a); px[1] = (uint8_t)div255(px[1] * ia + cg * a); px[2] = (uint8_t)div255(px[2] * ia + cr * a); }
                } else {
                    uint16_t *p16 = (uint16_t *)(void *)px;
                    if (a == 255) *p16 = c565;
                    else {
                        const uint32_t v = *p16, ia = 255 - a;
                        const uint32_t r = ((v >> 11) & 31) * 255 / 31, g = ((v >> 5) & 63) * 255 / 63, b = (v & 31) * 255 / 31;
                        const uint32_t nr = div255(r * ia + cr * a), ng = div255(g * ia + cg * a), nb = div255(b * ia + cb * a);
                        *p16 = (uint16_t)(((nr & 0xF8) << 8) | ((ng & 0xFC) << 3) | (nb >> 3));
                    }
                }
            }
            x++;
        }
    }
    return 1;
}

/* One pixel of a straight-alpha blend, any of the formats mask_blit handles (`px` is the destination pixel, bpp its size in bytes). */
static inline void blend_px(uint8_t *px, int bpp, uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    if (!a) return;
    if (bpp != 2) {
        if (a == 255) { px[0] = (uint8_t)b; px[1] = (uint8_t)g; px[2] = (uint8_t)r; if (bpp == 4) px[3] = 255; return; }
        const uint32_t ia = 255 - a;
        px[0] = (uint8_t)div255(px[0] * ia + b * a); px[1] = (uint8_t)div255(px[1] * ia + g * a); px[2] = (uint8_t)div255(px[2] * ia + r * a);
    } else {
        uint16_t *p16 = (uint16_t *)(void *)px;
        if (a == 255) { *p16 = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); return; }
        const uint32_t v = *p16, ia = 255 - a;
        const uint32_t pr = ((v >> 11) & 31) * 255 / 31, pg = ((v >> 5) & 63) * 255 / 63, pb = (v & 31) * 255 / 31;
        const uint32_t nr = div255(pr * ia + r * a), ng = div255(pg * ia + g * a), nb = div255(pb * ia + b * a);
        *p16 = (uint16_t)(((nr & 0xF8) << 8) | ((ng & 0xFC) << 3) | (nb >> 3));
    }
}

/* The bytes per pixel of a layer this file can write into directly (3, 2 or 4), or 0. Also checks that x1..x2, y1..y2 lie inside the layer's buffer. */
static int layer_bpp(const lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    const lv_draw_buf_t *db = layer->draw_buf;
    if (!db) return 0;
    const lv_color_format_t cf = db->header.cf;
    int bpp = 0;
    if (cf == LV_COLOR_FORMAT_RGB888) bpp = 3;
    else if (cf == LV_COLOR_FORMAT_RGB565) bpp = 2;
    else if (cf == LV_COLOR_FORMAT_XRGB8888 || cf == LV_COLOR_FORMAT_ARGB8888) bpp = 4;
    if (!bpp || x1 < layer->buf_area.x1 || y1 < layer->buf_area.y1 || x2 > layer->buf_area.x2 || y2 > layer->buf_area.y2) return 0;
    return bpp;
}

/* Blends a straight-alpha ARGB8888 picture (w x h) over the layer at (x0, y0), clipped to what is being drawn; for the position marker, where LVGL's image blend was slow. */
static int image_blit_argb(lv_layer_t *layer, const lv_color32_t *src, int w, int h, int32_t x0, int32_t y0)
{
    const lv_area_t *c = &layer->_clip_area;
    const int32_t cx1 = x0 > c->x1 ? x0 : c->x1, cx2 = x0 + w - 1 < c->x2 ? x0 + w - 1 : c->x2;
    const int32_t cy1 = y0 > c->y1 ? y0 : c->y1, cy2 = y0 + h - 1 < c->y2 ? y0 + h - 1 : c->y2;
    if (cx1 > cx2 || cy1 > cy2) return 1;                          /* nothing of it in this strip */
    const int bpp = layer_bpp(layer, cx1, cy1, cx2, cy2);
    if (!bpp) return 0;
    const uint32_t dstride = layer->draw_buf->header.stride;
    uint8_t *base = (uint8_t *)lv_draw_layer_go_to_xy(layer, cx1 - layer->buf_area.x1, cy1 - layer->buf_area.y1);
    if (!base) return 0;
    for (int32_t y = cy1; y <= cy2; y++) {
        const lv_color32_t *s = src + (size_t)(y - y0) * w + (cx1 - x0);
        uint8_t *d = base + (size_t)(y - cy1) * dstride;
        for (int32_t x = cx1; x <= cx2; x++, s++, d += bpp) blend_px(d, bpp, s->red, s->green, s->blue, s->alpha);
    }
    return 1;
}

/* Blends one color over rows y1..y2 of the object spanning x1..x2 with its own opacity per row (`alpha[row - y1]`), clipped to what is being drawn; for the top and bottom fades. */
static int rows_blend(lv_layer_t *layer, int32_t x1, int32_t x2, int32_t y1, int32_t y2, lv_color_t col, const uint8_t *alpha)
{
    const lv_area_t *c = &layer->_clip_area;
    const int32_t cx1 = x1 > c->x1 ? x1 : c->x1, cx2 = x2 < c->x2 ? x2 : c->x2;
    const int32_t cy1 = y1 > c->y1 ? y1 : c->y1, cy2 = y2 < c->y2 ? y2 : c->y2;
    if (cx1 > cx2 || cy1 > cy2) return 1;
    const int bpp = layer_bpp(layer, cx1, cy1, cx2, cy2);
    if (!bpp) return 0;
    const uint32_t dstride = layer->draw_buf->header.stride;
    uint8_t *base = (uint8_t *)lv_draw_layer_go_to_xy(layer, cx1 - layer->buf_area.x1, cy1 - layer->buf_area.y1);
    if (!base) return 0;
    const int n = cx2 - cx1 + 1;
    for (int32_t y = cy1; y <= cy2; y++) {
        const uint32_t a = alpha[y - y1];
        if (!a) continue;
        uint8_t *d = base + (size_t)(y - cy1) * dstride;
        int k = 0;
        if (bpp == 3 && (((uintptr_t)d) & 3) == 0) k = nav_row_blend_bgr(d, n, col.red, col.green, col.blue, a);
        for (uint8_t *px = d + (size_t)k * bpp; k < n; k++, px += bpp) blend_px(px, bpp, col.red, col.green, col.blue, a);
    }
    return 1;
}

/* ---- cached labels ----
 * LVGL's text drawing costs a lot per frame, so each label is drawn once into an ARGB snapshot in PSRAM, LVGL's own text is switched off (opacity 0), and each frame blends the image.
 * It is remade from a timer when the text, font, color, size, or alignment changes; labels with a background or border, or that scroll, are left alone. */
#define FL_MAX 80
typedef struct {
    lv_obj_t *o;
    lv_image_dsc_t img;                  /* the same bytes as an A8 image, for LVGL to draw when the direct blend cannot */
    uint8_t *data;
    size_t cap;
    uint32_t hash, color;
    int32_t w, h;                        /* the labels size when the image was made (part of the key) */
    int32_t iw, ih, ix, iy;              /* the image's own size and where it starts relative to the label (LVGL adds a margin for some labels) */
    const void *font;
    int8_t align, space;
    uint8_t state;                       /* 0 = LVGL draws the text as usual, 1 = the cached image is drawn, 2 = never cached */
} fl_t;
static fl_t *fl;                         /* FL_MAX entries, allocated on first use (PSRAM on the board: internal RAM is nearly all spoken for) */
static int fl_n;
static uint8_t *fl_scratch;              /* the one image every label is made in before its coverage is kept */
static size_t fl_scratch_cap;
uint32_t nav_fl_stats[10];                /* images made, snapshots that failed, which label failed last, which label was made last */
static int fl_busy;                      /* an image is being made: the draw callback stays out of it */
int nav_ui_label_cache = 1;
int nav_ui_label_count(void) { return fl_n; }       /* how many labels the cache looks after (the table holds FL_MAX) */              /* 0 turns the cache off (the simulator's A/B switch) */

static void fl_draw_cb(lv_event_t *e)
{
    fl_t *f = (fl_t *)lv_event_get_user_data(e);
    if (f->state != 1 || fl_busy) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(f->o, &a);
    const lv_color_t col = lv_color_hex(f->color);
    const int32_t x0 = a.x1 + f->ix, y0 = a.y1 + f->iy;
    if (layer->parent == NULL) {                                    /* the screen's own layer: blend the coverage straight in, clipped to what is being drawn */
        const lv_area_t *c = &layer->_clip_area;
        const int32_t cx1 = x0 > c->x1 ? x0 : c->x1, cx2 = x0 + f->iw - 1 < c->x2 ? x0 + f->iw - 1 : c->x2;
        const int32_t cy1 = y0 > c->y1 ? y0 : c->y1, cy2 = y0 + f->ih - 1 < c->y2 ? y0 + f->ih - 1 : c->y2;
        if (cx1 > cx2 || cy1 > cy2) return;
        if (mask_blit(layer, f->data + (size_t)(cy1 - y0) * f->iw + (cx1 - x0), cx2 - cx1 + 1, cy2 - cy1 + 1, f->iw, cx1, cy1, col)) return;
    }
    lv_draw_image_dsc_t d;                                          /* not the screen's own layer, or a pixel format the direct blend does not know */
    lv_draw_image_dsc_init(&d);
    d.src = &f->img;
    d.recolor = col;
    d.recolor_opa = LV_OPA_COVER;
    lv_area_t dst = { x0, y0, x0 + f->iw - 1, y0 + f->ih - 1 };
    lv_draw_image(layer, &d, &dst);
}

static void fl_register(lv_obj_t *l)
{
    if (!fl) {
        fl = (fl_t *)(nav_ui_img_alloc ? nav_ui_img_alloc(sizeof(fl_t) * FL_MAX) : malloc(sizeof(fl_t) * FL_MAX));
        if (!fl) return;
        memset(fl, 0, sizeof(fl_t) * FL_MAX);
    }
    if (fl_n >= FL_MAX) return;
    fl[fl_n].o = l;
    lv_obj_add_event_cb(l, fl_draw_cb, LV_EVENT_DRAW_MAIN, &fl[fl_n]);
    fl_n++;
}

static uint32_t fl_hash(const char *t)
{
    uint32_t h = 2166136261u;
    for (; *t; t++) h = (h ^ (uint8_t)*t) * 16777619u;
    return h;
}

static void fl_uncache(fl_t *f, int state)
{
    if (f->state == 1) lv_obj_set_style_text_opa(f->o, LV_OPA_COVER, 0);      /* LVGL draws it again */
    f->state = (uint8_t)state;
}

/* Made again when anything that shapes the text changes. Returns 1 if it made an image (the caller makes one a turn, to spread the cost). */
static int fl_update_one(fl_t *f, const lv_area_t *screen)
{
    lv_obj_t *o = f->o;
    if (f->state == 2) return 0;
    if (!nav_ui_label_cache) { fl_uncache(f, 0); return 0; }
    if (!lv_obj_is_visible(o)) return 0;
    lv_obj_update_layout(o);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    if (a.x2 < screen->x1 || a.x1 > screen->x2 || a.y2 < screen->y1 || a.y1 > screen->y2) return 0;      /* not on the screen: leave it for when it is */
    if (lv_label_get_long_mode(o) == LV_LABEL_LONG_MODE_SCROLL || lv_label_get_long_mode(o) == LV_LABEL_LONG_MODE_SCROLL_CIRCULAR
        || lv_obj_get_style_bg_opa(o, LV_PART_MAIN) > 0 || lv_obj_get_style_border_width(o, LV_PART_MAIN) > 0
        || lv_obj_get_style_outline_width(o, LV_PART_MAIN) > 0 || lv_obj_get_style_shadow_width(o, LV_PART_MAIN) > 0
        || lv_obj_get_style_radius(o, LV_PART_MAIN) > 0) { fl_uncache(f, 2); return 0; }
    const char *text = lv_label_get_text(o);
    const int32_t w = lv_area_get_width(&a), h = lv_area_get_height(&a);
    if (!text || !text[0] || w <= 0 || h <= 0) { fl_uncache(f, 0); return 0; }       /* nothing to draw */
    const uint32_t hash = fl_hash(text), color = lv_color_to_u32(lv_obj_get_style_text_color(o, LV_PART_MAIN));
    const void *font = lv_obj_get_style_text_font(o, LV_PART_MAIN);
    const int8_t align = (int8_t)lv_obj_get_style_text_align(o, LV_PART_MAIN), space = (int8_t)lv_obj_get_style_text_letter_space(o, LV_PART_MAIN);
    if (f->state == 1 && hash == f->hash && color == f->color && font == f->font && align == f->align && space == f->space && w == f->w && h == f->h) return 0;

    /* room for the label and a margin of up to 24 pixels all round, which LVGL adds to the image of some objects */
    const int32_t mw = w + 48, mh = h + 48;
    const uint32_t mstride = lv_draw_buf_width_to_stride((uint32_t)mw, LV_COLOR_FORMAT_ARGB8888);
    const size_t need = (size_t)mstride * (size_t)mh;
    /* The snapshot is made in one shared scratch image and only its coverage (alpha) is kept, packed to one byte a pixel in a buffer of exactly the label's size (the text is one color); this saved about 3 MB of PSRAM across 59 labels. */
    if (need > fl_scratch_cap) {
        free(fl_scratch);
        const size_t cap = need + need / 4;
        fl_scratch = (uint8_t *)(nav_ui_img_alloc ? nav_ui_img_alloc(cap) : malloc(cap));
        fl_scratch_cap = fl_scratch ? cap : 0;
        if (!fl_scratch) { fl_uncache(f, 2); return 0; }
    }
    lv_draw_buf_t buf;
    if (lv_draw_buf_init(&buf, (uint32_t)mw, (uint32_t)mh, LV_COLOR_FORMAT_ARGB8888, mstride, fl_scratch, (uint32_t)fl_scratch_cap) != LV_RESULT_OK) { fl_uncache(f, 2); return 0; }
    fl_busy = 1;
    lv_obj_set_style_text_opa(o, LV_OPA_COVER, 0);                   /* the image is made with the text drawn for real */
    const lv_result_t res = lv_snapshot_take_to_draw_buf(o, LV_COLOR_FORMAT_ARGB8888, &buf);   /* reshapes buf to the object's real size */
    fl_busy = 0;
    if (res != LV_RESULT_OK) { nav_fl_stats[1]++; nav_fl_stats[2] = (uint32_t)(f - fl); nav_fl_stats[4] = (uint32_t)w; nav_fl_stats[5] = (uint32_t)h; nav_fl_stats[7] = (uint32_t)fl_scratch_cap; f->state = 2; lv_obj_set_style_text_opa(o, LV_OPA_COVER, 0); return 1; }
    nav_fl_stats[0]++; nav_fl_stats[3] = (uint32_t)(f - fl);
    const int32_t iw = (int32_t)buf.header.w, ih = (int32_t)buf.header.h;
    if (buf.header.stride != (uint32_t)iw * 4 || iw < w || ih < h) { fl_uncache(f, 2); return 1; }    /* the conversion below reads the image with no row padding */
    const size_t bytes = (size_t)iw * (size_t)ih;
    if (!f->data || bytes > f->cap) {                                /* a buffer of exactly the image's size (with a little room in case the label grows a bit) */
        free(f->data);
        const size_t cap = bytes + bytes / 8 + 16;
        f->data = (uint8_t *)(nav_ui_img_alloc ? nav_ui_img_alloc(cap) : malloc(cap));
        f->cap = f->data ? cap : 0;
        if (!f->data) { fl_uncache(f, 2); return 1; }
    }
    f->iw = iw; f->ih = ih;
    f->ix = -(f->iw - w) / 2; f->iy = -(f->ih - h) / 2;
    {
        const uint8_t *src = fl_scratch + 3;
        uint8_t *dst = f->data;
        for (size_t i = 0; i < bytes; i++, src += 4) dst[i] = *src;
        memset(&f->img, 0, sizeof f->img);
        f->img.header.magic = LV_IMAGE_HEADER_MAGIC;
        f->img.header.cf = LV_COLOR_FORMAT_A8;
        f->img.header.w = (uint32_t)f->iw; f->img.header.h = (uint32_t)f->ih; f->img.header.stride = (uint32_t)f->iw;
        f->img.data = f->data; f->img.data_size = (uint32_t)bytes;
    }
    lv_obj_set_style_text_opa(o, LV_OPA_TRANSP, 0);                   /* from now on the image is drawn, not the text */
    f->hash = hash; f->color = color; f->font = font; f->align = align; f->space = space; f->w = w; f->h = h;
    f->state = 1;
    lv_obj_invalidate(o);
    return 1;
}

static void fl_timer_cb_body(lv_timer_t *t);
static void fl_timer_cb(lv_timer_t *t)
{
    const uint32_t t0 = PERF_NOW();
    fl_timer_cb_body(t);
    const uint32_t us = PERF_NOW() - t0;
    if (us > nav_timer_max_us[1]) nav_timer_max_us[1] = us;
}

static void fl_timer_cb_body(lv_timer_t *t)
{
    (void)t;
    static int next;
    const lv_area_t screen = { 0, 0, SZ - 1, SZ - 1 };
    if (!fl) return;
    for (int k = 0; k < fl_n; k++) {                                // starting after the last label that was made, so every label gets its turn
        const int i = (next + k) % fl_n;
        if (fl_update_one(&fl[i], &screen)) { next = i + 1; return; }
    }
}

/* Streets drawn the same way: one coverage mask per kind (minor, major), so meeting streets join cleanly. Returns 0 if there was no memory (the caller then draws plain lines). */
static int roads_aa_draw(lv_layer_t *layer, const lv_area_t *clip, float ox, float oy, int major, float half, lv_color_t col)
{
    if (!route_masks_ready()) return 0;
    uint8_t *mask = rt_mask[2 + major];
    const int w = clip->x2 - clip->x1 + 1, h = clip->y2 - clip->y1 + 1;
    if (w <= 0 || h <= 0 || (size_t)w * h > (size_t)SZ * SZ) return 0;
    const uint32_t perf_m0 = PERF_NOW();
    memset(mask, 0, (size_t)w * h);
    nav_perf_us[6] += PERF_NOW() - perf_m0;
    nav_perf_us[7] += (uint32_t)(w * h);
    const float pad = half + 1.5f;
    int any = 0;
    for (uint16_t i = 0; i < U.nroads; i++) {
        const nav_seg_t *sg = &U.roads[i];
        if ((sg->major ? 1 : 0) != major) continue;
        float ax, ay, bx, by;
        to_screen(sg->x1, sg->y1, ox, oy, &ax, &ay);
        to_screen(sg->x2, sg->y2, ox, oy, &bx, &by);
        int32_t x1 = (int32_t)floorf((ax < bx ? ax : bx) - pad), x2 = (int32_t)ceilf((ax > bx ? ax : bx) + pad);
        int32_t y1 = (int32_t)floorf((ay < by ? ay : by) - pad), y2 = (int32_t)ceilf((ay > by ? ay : by) + pad);
        if (x1 < clip->x1) x1 = clip->x1;
        if (x2 > clip->x2) x2 = clip->x2;
        if (y1 < clip->y1) y1 = clip->y1;
        if (y2 > clip->y2) y2 = clip->y2;
        if (x1 > x2 || y1 > y2) continue;
        nav_seg_cover(mask, NULL, w, clip->x1, clip->y1, x1, x2, y1, y2, ax, ay, bx, by, half, 0);
        any = 1;
    }
    if (!major && U.nroute >= 2) {
        /* The road behind the marker as an ordinary street: the route already driven, back as far as the screen reaches (at most 48 points). */
        const int seg = U.route_seg;
        const float reach = (float)SZ / U.scale;                             /* meters: about a screen's worth, measured from the marker */
        for (int i = seg, count = 0; i >= 0 && count < 48; i--, count++) {
            float ax, ay, bx, by;
            to_screen(U.route[i].dx, U.route[i].dy, ox, oy, &ax, &ay);
            if (i == seg) to_screen(U.proj_x, U.proj_y, ox, oy, &bx, &by);
            else to_screen(U.route[i + 1].dx, U.route[i + 1].dy, ox, oy, &bx, &by);
            int32_t x1 = (int32_t)floorf((ax < bx ? ax : bx) - pad), x2 = (int32_t)ceilf((ax > bx ? ax : bx) + pad);
            int32_t y1 = (int32_t)floorf((ay < by ? ay : by) - pad), y2 = (int32_t)ceilf((ay > by ? ay : by) + pad);
            if (x1 < clip->x1) x1 = clip->x1;
            if (x2 > clip->x2) x2 = clip->x2;
            if (y1 < clip->y1) y1 = clip->y1;
            if (y2 > clip->y2) y2 = clip->y2;
            if (x1 <= x2 && y1 <= y2) { nav_seg_cover(mask, NULL, w, clip->x1, clip->y1, x1, x2, y1, y2, ax, ay, bx, by, half, 0); any = 1; }
            if (hypotf(U.route[i].dx - U.proj_x, U.route[i].dy - U.proj_y) > reach) break;     /* this piece started beyond the screen: that is far enough */
        }
    }
    if (!any) return 1;
    lv_image_dsc_t *dsc = &rt_dsc[2 + major];
    memset(dsc, 0, sizeof *dsc);
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_A8;
    dsc->header.w = (uint32_t)w;
    dsc->header.h = (uint32_t)h;
    dsc->header.stride = (uint32_t)w;
    dsc->data_size = (uint32_t)w * h;
    dsc->data = mask;
    lv_draw_image_dsc_t id;
    lv_draw_image_dsc_init(&id);
    id.src = dsc;
    id.recolor = col;
    id.recolor_opa = LV_OPA_COVER;
    if (!mask_blit(layer, mask, w, h, w, clip->x1, clip->y1, col)) lv_draw_image(layer, &id, clip);
    return 1;
}

static int route_aa_draw(lv_layer_t *layer, const lv_area_t *clip, const float *px, const float *py, int n,
                         float half_o, float half_c, lv_color_t col_o, lv_color_t col_c)
{
    if (n < 2 || !route_masks_ready()) return n < 2;
    float minx = px[0], maxx = px[0], miny = py[0], maxy = py[0];
    for (int i = 1; i < n; i++) {
        if (px[i] < minx) minx = px[i];
        if (px[i] > maxx) maxx = px[i];
        if (py[i] < miny) miny = py[i];
        if (py[i] > maxy) maxy = py[i];
    }
    const float pad = half_o + 2.0f;
    int32_t x1 = (int32_t)floorf(minx - pad), x2 = (int32_t)ceilf(maxx + pad), y1 = (int32_t)floorf(miny - pad), y2 = (int32_t)ceilf(maxy + pad);
    if (x1 < clip->x1) x1 = clip->x1;
    if (x2 > clip->x2) x2 = clip->x2;
    if (y1 < clip->y1) y1 = clip->y1;
    if (y2 > clip->y2) y2 = clip->y2;
    if (x1 > x2 || y1 > y2) return 1;                        /* nothing of it in this piece of the screen */
    const int w = x2 - x1 + 1, h = y2 - y1 + 1;
    if ((size_t)w * h > (size_t)SZ * SZ) return 0;
    memset(rt_mask[0], 0, (size_t)w * h);
    memset(rt_mask[1], 0, (size_t)w * h);
    int any = 0;
    for (int i = 0; i + 1 < n; i++) {
        const float ax = px[i], ay = py[i], bx = px[i + 1], by = py[i + 1];
        int32_t sx1 = (int32_t)floorf((ax < bx ? ax : bx) - pad), sx2 = (int32_t)ceilf((ax > bx ? ax : bx) + pad);
        int32_t sy1 = (int32_t)floorf((ay < by ? ay : by) - pad), sy2 = (int32_t)ceilf((ay > by ? ay : by) + pad);
        if (sx1 < x1) sx1 = x1;
        if (sx2 > x2) sx2 = x2;
        if (sy1 < y1) sy1 = y1;
        if (sy2 > y2) sy2 = y2;
        if (sx1 > sx2 || sy1 > sy2) continue;
        nav_seg_cover(rt_mask[0], rt_mask[1], w, x1, y1, sx1, sx2, sy1, sy2, ax, ay, bx, by, half_o, half_c);
        any = 1;
    }
    if (!any) return 1;
    const lv_area_t dst = { x1, y1, x2, y2 };
    for (int k = 0; k < 2; k++) {
        memset(&rt_dsc[k], 0, sizeof rt_dsc[k]);
        rt_dsc[k].header.magic = LV_IMAGE_HEADER_MAGIC;
        rt_dsc[k].header.cf = LV_COLOR_FORMAT_A8;
        rt_dsc[k].header.w = (uint32_t)w;
        rt_dsc[k].header.h = (uint32_t)h;
        rt_dsc[k].header.stride = (uint32_t)w;
        rt_dsc[k].data_size = (uint32_t)w * h;
        rt_dsc[k].data = rt_mask[k];
        lv_draw_image_dsc_t id;
        lv_draw_image_dsc_init(&id);
        id.src = &rt_dsc[k];
        id.recolor = k ? col_c : col_o;
        id.recolor_opa = LV_OPA_COVER;
        if (!mask_blit(layer, rt_mask[k], w, h, w, x1, y1, k ? col_c : col_o)) lv_draw_image(layer, &id, &dst);
    }
    return 1;
}

/* ---- slow stretches of the route ----
 * The phone's traffic spans (point ranges of the route) are drawn over the route's core: amber for slow, red for nearly stopped, each with its own coverage mask made like the route's.
 * `rx`/`ry` are the route's screen points from the rider's position on (rx[0] is the position; rx[k] is route point seg + k). */
static nav_traffic_span_t tf_span[NAV_MAX_TRAFFIC_SPANS];
static int tf_n;
static int tf_alert_level;

static void traffic_overlay(lv_layer_t *layer, const lv_area_t *clip, const float *rx, const float *ry, int rn, int seg, float half_c)
{
    for (int level = NAV_TRAFFIC_SLOW; level <= NAV_TRAFFIC_STOPPED; level++) {
        uint8_t *mask = rt_mask[3 + level];
        float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
        int k0s[NAV_MAX_TRAFFIC_SPANS], k1s[NAV_MAX_TRAFFIC_SPANS], ns = 0;
        for (int s = 0; s < tf_n; s++) {
            if (tf_span[s].level != level) continue;
            int k0 = (int)tf_span[s].first - seg, k1 = (int)tf_span[s].last - seg;
            if (k0 < 0) k0 = 0;
            if (k1 > rn - 1) k1 = rn - 1;
            if (k1 <= k0) continue;
            k0s[ns] = k0; k1s[ns] = k1; ns++;
            for (int k = k0; k <= k1; k++) {
                if (rx[k] < minx) minx = rx[k];
                if (rx[k] > maxx) maxx = rx[k];
                if (ry[k] < miny) miny = ry[k];
                if (ry[k] > maxy) maxy = ry[k];
            }
        }
        if (!ns) continue;
        const float pad = half_c + 2.0f;
        int32_t x1 = (int32_t)floorf(minx - pad), x2 = (int32_t)ceilf(maxx + pad), y1 = (int32_t)floorf(miny - pad), y2 = (int32_t)ceilf(maxy + pad);
        if (x1 < clip->x1) x1 = clip->x1;
        if (x2 > clip->x2) x2 = clip->x2;
        if (y1 < clip->y1) y1 = clip->y1;
        if (y2 > clip->y2) y2 = clip->y2;
        if (x1 > x2 || y1 > y2) continue;
        const int w = x2 - x1 + 1, h = y2 - y1 + 1;
        if ((size_t)w * h > (size_t)SZ * SZ) continue;
        memset(mask, 0, (size_t)w * h);
        int any = 0;
        for (int s = 0; s < ns; s++) {
            for (int k = k0s[s]; k < k1s[s]; k++) {
                const float ax = rx[k], ay = ry[k], bx = rx[k + 1], by = ry[k + 1];
                int32_t sx1 = (int32_t)floorf((ax < bx ? ax : bx) - pad), sx2 = (int32_t)ceilf((ax > bx ? ax : bx) + pad);
                int32_t sy1 = (int32_t)floorf((ay < by ? ay : by) - pad), sy2 = (int32_t)ceilf((ay > by ? ay : by) + pad);
                if (sx1 < x1) sx1 = x1;
                if (sx2 > x2) sx2 = x2;
                if (sy1 < y1) sy1 = y1;
                if (sy2 > y2) sy2 = y2;
                if (sx1 > sx2 || sy1 > sy2) continue;
                nav_seg_cover(mask, NULL, w, x1, y1, sx1, sx2, sy1, sy2, ax, ay, bx, by, half_c, 0);
                any = 1;
            }
        }
        if (!any) continue;
        lv_image_dsc_t *dsc = &rt_dsc[3 + level];
        memset(dsc, 0, sizeof *dsc);
        dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
        dsc->header.cf = LV_COLOR_FORMAT_A8;
        dsc->header.w = (uint32_t)w;
        dsc->header.h = (uint32_t)h;
        dsc->header.stride = (uint32_t)w;
        dsc->data_size = (uint32_t)w * h;
        dsc->data = mask;
        const lv_area_t dst = { x1, y1, x2, y2 };
        lv_draw_image_dsc_t id;
        lv_draw_image_dsc_init(&id);
        id.src = dsc;
        id.recolor = lv_color_hex(level == NAV_TRAFFIC_STOPPED ? TH->stopped : TH->slow);
        id.recolor_opa = LV_OPA_COVER;
        if (!mask_blit(layer, mask, w, h, w, x1, y1, id.recolor)) lv_draw_image(layer, &id, &dst);
    }
}

/* Draw a map line. LVGL runs its edge masks over a slanted line's whole bounding box, so long diagonals cost as much as filling the band (~4 fps with the map rotated).
 * Cutting a slanted line into short pieces gives each a small box, and pieces outside the band are skipped; ends are rounded to whole pixels and overlap by one so no gap shows. */
static void map_line(lv_layer_t *l, lv_draw_line_dsc_t *d,
                     float x1, float y1, float x2, float y2)
{
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    float m = len > 0 ? fminf(fabsf(dx), fabsf(dy)) / len : 0;      /* sine of the angle to the nearest axis */
    if (!nav_ui_line_chunk || len * m < 10.0f || len < 40.0f) {      /* near-axis or short: cheap already */
        d->p1.x = x1; d->p1.y = y1;
        d->p2.x = x2; d->p2.y = y2;
        lv_draw_line(l, d);
        return;
    }
    /* piece length: long enough that each piece spans a few pixels across (so integer ends do not flatten it), short enough for a small bounding box */
    float piece = 6.0f / m;
    if (piece < 72.0f) piece = 72.0f;      /* long pieces: the +-0.5 px end rounding bends the line by < 0.5 deg */
    if (piece > 240.0f) piece = 240.0f;
    int n = (int)ceilf(len / piece);
    float ux = dx / len, uy = dy / len;
    const lv_area_t *clip = &l->_clip_area;
    float pad = d->width * 0.5f + 2.0f;
    for (int i = 0; i < n; i++) {
        float t0 = len * i / n, t1 = len * (i + 1) / n;
        float ax = x1 + ux * t0, ay = y1 + uy * t0;
        float bx = x1 + ux * t1, by = y1 + uy * t1;
        if (i + 1 < n) { bx += ux; by += uy; }                       /* one pixel of overlap with the next piece */
        if (seg_outside(clip, ax, ay, bx, by, pad)) continue;
        d->p1.x = lroundf(ax); d->p1.y = lroundf(ay);
        d->p2.x = lroundf(bx); d->p2.y = lroundf(by);
        lv_draw_line(l, d);
    }
}

/* Fill a simple polygon (even-odd) with opaque horizontal spans; like fill_quad_spans, but rows within one pixel of a run's first row are merged, so a curved edge can overshoot by a pixel. Edges are not anti-aliased. */
#define POLY_RUNS 8
static void fill_poly_spans(lv_layer_t *layer, const lv_area_t *clip, const float *px, const float *py,
                            int n, lv_color_t color)
{
    if (n < 3) return;
    float miny = py[0], maxy = py[0];
    for (int k = 1; k < n; k++) {
        if (py[k] < miny) miny = py[k];
        if (py[k] > maxy) maxy = py[k];
    }
    int y0 = (int)ceilf(miny - 0.5f), y1 = (int)floorf(maxy - 0.5f);
    if (y0 < clip->y1) y0 = clip->y1;
    if (y1 > clip->y2) y1 = clip->y2;
    if (y0 > y1) return;

    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = color;
    rd.bg_opa = LV_OPA_COVER;

    struct { int x1, x2, ax1, ax2, ys, last; } run[POLY_RUNS];     /* a* = the run's first row: rows merge only against it */
    int nrun = 0;
    for (int y = y0; y <= y1 + 1; y++) {
        float xs[NAV_POLY_MAXV];
        int nx = 0;
        if (y <= y1) {
            float yc = y + 0.5f;
            for (int e = 0; e < n; e++) {
                int f = (e + 1 == n) ? 0 : e + 1;
                float ya = py[e], yb = py[f];
                if ((yc < ya && yc < yb) || (yc >= ya && yc >= yb)) continue;
                float xi = px[e] + (yc - ya) * (px[f] - px[e]) / (yb - ya);
                int j = nx++;
                while (j > 0 && xs[j - 1] > xi) { xs[j] = xs[j - 1]; j--; }
                xs[j] = xi;
            }
        }
        int matched[POLY_RUNS] = {0};
        for (int k = 0; k + 1 < nx; k += 2) {
            int a = (int)ceilf(xs[k] - 0.5f), b = (int)floorf(xs[k + 1] - 0.5f);
            if (a < clip->x1) a = clip->x1;
            if (b > clip->x2) b = clip->x2;
            if (b < a) continue;
            int r;
            for (r = 0; r < nrun; r++)
                if (!matched[r] && run[r].last == y - 1 && abs(run[r].ax1 - a) <= 1 && abs(run[r].ax2 - b) <= 1) break;
            if (r < nrun) {
                if (a < run[r].x1) run[r].x1 = a;
                if (b > run[r].x2) run[r].x2 = b;
                run[r].last = y;
                matched[r] = 1;
            } else if (nrun < POLY_RUNS) {
                run[nrun].x1 = run[nrun].ax1 = a; run[nrun].x2 = run[nrun].ax2 = b; run[nrun].ys = y; run[nrun].last = y;
                matched[nrun++] = 1;
            } else {
                lv_area_t one = {a, y, b, y};
                lv_draw_rect(layer, &rd, &one);
            }
        }
        for (int r = 0; r < nrun;) {                       /* flush runs that did not continue */
            if (run[r].last == y) { r++; continue; }
            lv_area_t rc = {run[r].x1, run[r].ys, run[r].x2, run[r].last};
            lv_draw_rect(layer, &rd, &rc);
            run[r] = run[--nrun];
            matched[r] = matched[nrun];
        }
    }
}

/* Parks, water, and background roads for the current view, with the pivot at (ox, oy) in the layer's coordinates. */
static void draw_background(lv_layer_t *layer, float ox, float oy)
{
    float sx1, sy1, sx2, sy2;
    const lv_area_t clip = layer->_clip_area;

    const uint32_t perf_a0 = PERF_NOW();
    /* parks / water */
    for (uint16_t i = 0; i < U.nareas; i++) {
        const nav_area_t *a = &U.areas[i];
        float cx[4], cy[4];
        to_screen(a->x1, a->y1, ox, oy, &cx[0], &cy[0]);
        to_screen(a->x2, a->y1, ox, oy, &cx[1], &cy[1]);
        to_screen(a->x2, a->y2, ox, oy, &cx[2], &cy[2]);
        to_screen(a->x1, a->y2, ox, oy, &cx[3], &cy[3]);
        {
            float minx = cx[0], maxx = cx[0], miny = cy[0], maxy = cy[0];
            for (int k = 1; k < 4; k++) {
                if (cx[k] < minx) minx = cx[k];
                if (cx[k] > maxx) maxx = cx[k];
                if (cy[k] < miny) miny = cy[k];
                if (cy[k] > maxy) maxy = cy[k];
            }
            if (nav_ui_cull_enabled && (maxx < clip.x1 || minx > clip.x2 || maxy < clip.y1 || miny > clip.y2)) continue;
        }
        if (nav_ui_area_scanline) {
            fill_quad_spans(layer, &clip, cx, cy, lv_color_hex(a->kind == NAV_AREA_WATER ? C_WATER : C_PARK));
            continue;
        }
        lv_draw_triangle_dsc_t t;
        lv_draw_triangle_dsc_init(&t);
        t.color = lv_color_hex(a->kind == NAV_AREA_WATER ? C_WATER : C_PARK);
        t.opa = LV_OPA_COVER;
        static const int idx[2][3] = {{0, 1, 2}, {0, 2, 3}};
        for (int q = 0; q < 2; q++) {
            for (int p = 0; p < 3; p++) {
                t.p[p].x = cx[idx[q][p]];
                t.p[p].y = cy[idx[q][p]];
            }
            lv_draw_triangle(layer, &t);
        }
    }

    /* park / water polygons */
    for (uint16_t i = 0; i < U.npolys; i++) {
        const nav_poly_t *p = &U.polys[i];
        if (p->n < 3 || p->n > NAV_POLY_MAXV) continue;          /* the receiver already insists on this; the drawing code does not rely on it */
        float px[NAV_POLY_MAXV], py[NAV_POLY_MAXV];
        float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
        for (int k = 0; k < p->n; k++) {
            to_screen(p->x[k], p->y[k], ox, oy, &px[k], &py[k]);
            if (px[k] < minx) minx = px[k];
            if (px[k] > maxx) maxx = px[k];
            if (py[k] < miny) miny = py[k];
            if (py[k] > maxy) maxy = py[k];
        }
        if (nav_ui_cull_enabled && (maxx < clip.x1 || minx > clip.x2 || maxy < clip.y1 || miny > clip.y2)) continue;
        fill_poly_spans(layer, &clip, px, py, p->n, lv_color_hex(p->kind == NAV_AREA_WATER ? C_WATER : C_PARK));
    }

    const uint32_t perf_a1 = PERF_NOW();
    nav_perf_us[1] += perf_a1 - perf_a0;
    /* background roads */
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.round_start = d.round_end = 0;     /* (only used if the streets cannot be drawn with exact edges) */
    for (int pass = 0; pass < 2; pass++) {
        d.color = lv_color_hex(pass ? C_ROAD_MAJ : C_ROAD);
        d.width = pass ? BIGPX(6) : BIGPX(4);
        const uint32_t perf_s0 = PERF_NOW();
        if (roads_aa_draw(layer, &clip, ox, oy, pass, d.width * 0.5f, d.color)) { nav_perf_us[2] += PERF_NOW() - perf_s0; continue; }      /* exact edges; plain lines only if there is no memory for it */
        for (uint16_t i = 0; i < U.nroads; i++) {
            const nav_seg_t *s = &U.roads[i];
            if ((s->major ? 1 : 0) != pass) continue;
            to_screen(s->x1, s->y1, ox, oy, &sx1, &sy1);
            to_screen(s->x2, s->y2, ox, oy, &sx2, &sy2);
            if (seg_outside(&clip, sx1, sy1, sx2, sy2, d.width * 0.5f + 2.0f)) continue;
            map_line(layer, &d, sx1, sy1, sx2, sy2);
        }
    }
}


/* What the route's cached screen points (rt_x, rt_y; see map_draw_cb) were made from. memcmp'd, so every field is a float or an int, and the struct has no padding. */
static struct rt_key_t { float ox, oy, px, py, hsin, hcos, scale, projx, projy; int seg, nroute; uint32_t rev; } rt_cache_key;
static int rt_cache_n = -1;
static uint32_t rt_rev;                 /* bumped when the route's points change */

static void map_draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t ar;
    lv_obj_get_coords(o, &ar);
    float ox = ar.x1 + SZ / 2.0f, oy = ar.y1 + MARKER_Y();
    const lv_area_t clip = layer->_clip_area;    /* what is being rendered right now */
    const uint32_t perf0 = PERF_NOW();
    nav_perf_us[5]++;

    draw_background(layer, ox, oy);

    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    /* route: ahead as white casing + accent core */
    if (U.nroute >= 2) {
        d.round_start = d.round_end = 1;     /* the route keeps its rounded ends */
        int seg = U.route_seg;
        /* (The road behind the marker is not drawn here: it is part of the streets, see roads_aa_draw.) */

        /* The route ahead is drawn with exact edges: each pixel's distance to the line sets its casing and core coverage, giving an even outline with round joins (lv_draw_line pieces left a wobbly edge). */
        {
            float *rx = rt_x, *ry = rt_y;
            int rn = 0;
            const uint32_t perf_r0 = PERF_NOW();
            if (!route_masks_ready()) rx = ry = NULL;
            /* The route's screen points are the same for every strip of a frame, so they are worked out for the first strip and kept until the position, heading, zoom, offset, or route changes. */
            const struct rt_key_t key = { ox, oy, U.px, U.py, U.hsin, U.hcos, U.scale, U.proj_x, U.proj_y, seg, U.nroute, rt_rev };
            const int cached = rx && rt_cache_n >= 0 && memcmp(&rt_cache_key, &key, sizeof key) == 0;
            float ahead = 0;
            if (cached) rn = rt_cache_n;
            for (int i = seg; !cached && rx && i < U.nroute && rn < ROUTE_AA_MAX; i++) {
                float wx = (i == seg) ? U.proj_x : U.route[i].dx, wy = (i == seg) ? U.proj_y : U.route[i].dy;
                if (i > seg) ahead += hypotf(wx - U.route[i - 1].dx, wy - U.route[i - 1].dy);
                if (ahead > 3000) break;
                to_screen(wx, wy, ox, oy, &rx[rn], &ry[rn]);
                rn++;
            }
            /* The phone sends route points in whole meters, so curves wobble a pixel or two at this zoom. Two passes of a 1-2-1 average take most of it out.
             * Only points with close neighbors on both sides are averaged (a corner, or a point beside a long straight, is a real place on the road); the ends stay put. */
            const float near_px = 15.0f * U.scale;
            for (int pass = 0; !cached && pass < 2 && rn > 2; pass++) {
                float px_ = rx[0], py_ = ry[0];
                for (int i = 1; i + 1 < rn; i++) {
                    const float cx = rx[i], cy = ry[i];
                    if (hypotf(cx - px_, cy - py_) <= near_px && hypotf(rx[i + 1] - cx, ry[i + 1] - cy) <= near_px) {
                        rx[i] = 0.25f * px_ + 0.5f * cx + 0.25f * rx[i + 1];
                        ry[i] = 0.25f * py_ + 0.5f * cy + 0.25f * ry[i + 1];
                    }
                    px_ = cx; py_ = cy;
                }
            }
            if (!cached && rx) { rt_cache_key = key; rt_cache_n = rn; }
            const uint32_t perf_r1 = PERF_NOW();
            nav_perf_us[3] += perf_r1 - perf_r0;
            if (rx && tf_n && rn >= 2 && route_aa_draw(layer, &clip, rx, ry, rn, BIGPX(12) * 0.5f, BIGPX(7) * 0.5f, lv_color_hex(C_ROUTE_OUT), lv_color_hex(C_ACCENT))) {
                traffic_overlay(layer, &clip, rx, ry, rn, seg, BIGPX(7) * 0.5f);
            } else
            if (rx && !route_aa_draw(layer, &clip, rx, ry, rn, BIGPX(12) * 0.5f, BIGPX(7) * 0.5f, lv_color_hex(C_ROUTE_OUT), lv_color_hex(C_ACCENT))) {
                /* no memory for the masks: the old way */
                for (int pass = 0; pass < 2; pass++) {
                    d.color = lv_color_hex(pass ? C_ACCENT : C_ROUTE_OUT);
                    d.width = pass ? BIGPX(7) : BIGPX(12);
                    for (int i = 0; i + 1 < rn; i++) {
                        if (seg_outside(&clip, rx[i], ry[i], rx[i + 1], ry[i + 1], d.width * 0.5f + 2.0f)) continue;
                        map_line(layer, &d, rx[i], ry[i], rx[i + 1], ry[i + 1]);
                    }
                }
            }
            nav_perf_us[4] += PERF_NOW() - perf_r1;
        }
    }

    /* position marker: the pre-rendered image, centred on the pivot */
    const uint32_t perf_k0 = PERF_NOW();
    if (mk_n) {
        lv_draw_image_dsc_t md;
        lv_draw_image_dsc_init(&md);
        md.src = &mk_img;
        int32_t mx = (int32_t)(ox - mk_n / 2.0f + 0.5f), my = (int32_t)(oy - mk_n / 2.0f + 0.5f);
        lv_area_t dst = {mx, my, mx + mk_n - 1, my + mk_n - 1};
        if (!image_blit_argb(layer, mk_buf, mk_n, mk_n, mx, my)) lv_draw_image(layer, &md, &dst);
    }

    nav_perf_us[0] += PERF_NOW() - perf0;
    nav_perf_us[9] += PERF_NOW() - perf_k0;        /* the marker */
    const uint32_t perf_t0 = PERF_NOW();

    /* north badge */
    {
        float bx = ar.x1 + SF(50), by = ar.y1 + SF(236);
        lv_draw_rect_dsc_t rd;
        lv_draw_rect_dsc_init(&rd);
        rd.radius = LV_RADIUS_CIRCLE;
        rd.bg_color = lv_color_hex(C_BG);
        rd.bg_opa = 230;
        rd.border_color = lv_color_hex(TH->edge);
        rd.border_width = 2;
        rd.border_opa = LV_OPA_COVER;
        lv_area_t m = {(int32_t)bx - S(21), (int32_t)by - S(21), (int32_t)bx + S(21), (int32_t)by + S(21)};
        const uint32_t perf_b0 = PERF_NOW();
        lv_draw_rect(layer, &rd, &m);
        nav_perf_us[10] += PERF_NOW() - perf_b0;
        const uint32_t perf_n0 = PERF_NOW();

        float h = U.hdg * DEG2RAD;
        float dx = -sinf(h), dy = -cosf(h);      /* north on screen */
        float px_ = -dy, py_ = dx;
        lv_draw_triangle_dsc_t t;
        lv_draw_triangle_dsc_init(&t);
        t.opa = LV_OPA_COVER;
        t.color = lv_color_hex(C_NORTH);
        const float n15 = SF(15), n2 = SF(2), n8 = SF(8);
        t.p[0].x = bx + dx * n15;        t.p[0].y = by + dy * n15;
        t.p[1].x = bx - dx * n2 + px_ * n8; t.p[1].y = by - dy * n2 + py_ * n8;
        t.p[2].x = bx - dx * n2 - px_ * n8; t.p[2].y = by - dy * n2 - py_ * n8;
        lv_draw_triangle(layer, &t);
        t.color = lv_color_hex(TH->sub);
        t.p[0].x = bx - dx * n15;        t.p[0].y = by - dy * n15;
        lv_draw_triangle(layer, &t);
        nav_perf_us[11] += PERF_NOW() - perf_n0;
    }

    /* scale bar */
    {
        float x2 = ar.x2 - SF(60), x1 = x2 - U.bar_px, y = ar.y1 + SF(270);
        lv_draw_line_dsc_t sd;
        lv_draw_line_dsc_init(&sd);
        sd.color = lv_color_hex(TH->scale);
        sd.width = 2;
        map_line(layer, &sd, x1, y, x2, y);
        map_line(layer, &sd, x1, y - SF(5), x1, y + SF(5));
        map_line(layer, &sd, x2, y - SF(5), x2, y + SF(5));
    }
    nav_perf_us[8] += PERF_NOW() - perf_t0;       /* north badge and scale bar */
}


/* ---- weather icons ---------------------------------------------------- */
/* Weather icons are light greys for the dark theme; on the light theme grey-ish colors are darkened (saturated sun and rain colors work on both). */
static uint32_t wxc(uint32_t c)
{
    if (!TH->light || c == TH->bg) return c;
    int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    if (mx - mn > 48) return c;
    r = (r * 38 + 0x47 * 62) / 100; g = (g * 38 + 0x55 * 62) / 100; b = (b * 38 + 0x69 * 62) / 100;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

typedef struct { lv_layer_t *l; float x0, y0, k; } wx_t;

static void wx_circle(wx_t *w, float cx, float cy, float r, uint32_t col)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = lv_color_hex(wxc(col));
    d.bg_opa = LV_OPA_COVER;
    lv_area_t a = {(int32_t)(w->x0 + (cx - r) * w->k), (int32_t)(w->y0 + (cy - r) * w->k),
                   (int32_t)(w->x0 + (cx + r) * w->k), (int32_t)(w->y0 + (cy + r) * w->k)};
    lv_draw_rect(w->l, &d, &a);
}

static void wx_rrect(wx_t *w, float x1, float y1, float x2, float y2, float rad, uint32_t col)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = (int32_t)(rad * w->k);
    d.bg_color = lv_color_hex(wxc(col));
    d.bg_opa = LV_OPA_COVER;
    lv_area_t a = {(int32_t)(w->x0 + x1 * w->k), (int32_t)(w->y0 + y1 * w->k),
                   (int32_t)(w->x0 + x2 * w->k), (int32_t)(w->y0 + y2 * w->k)};
    lv_draw_rect(w->l, &d, &a);
}

static void wx_line(wx_t *w, float ax, float ay, float bx, float by, float width, uint32_t col)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = lv_color_hex(wxc(col));
    d.width = (int32_t)(width * w->k + 0.5f);
    if (d.width < 1) d.width = 1;
    d.round_start = d.round_end = 1;
    d.p1.x = w->x0 + ax * w->k; d.p1.y = w->y0 + ay * w->k;
    d.p2.x = w->x0 + bx * w->k; d.p2.y = w->y0 + by * w->k;
    lv_draw_line(w->l, &d);
}

static void wx_cloud(wx_t *w, float dx, float dy, uint32_t col)
{
    wx_circle(w, 32 + dx, 48 + dy, 14, col);
    wx_circle(w, 50 + dx, 38 + dy, 20, col);
    wx_circle(w, 70 + dx, 46 + dy, 16, col);
    wx_rrect(w, 18 + dx, 46 + dy, 84 + dx, 64 + dy, 9, col);
}

static void wx_sun(wx_t *w, float cx, float cy, float r)
{
    for (int i = 0; i < 8; i++) {
        float a = i * 45.0f * DEG2RAD, sn = sinf(a), cs = cosf(a);
        wx_line(w, cx + sn * (r + 8), cy - cs * (r + 8), cx + sn * (r + 16), cy - cs * (r + 16), 5, 0xFBBF24);
    }
    wx_circle(w, cx, cy, r, 0xFBBF24);
}

static void wx_moon(wx_t *w, float cx, float cy, float r)
{
    wx_circle(w, cx, cy, r, 0xE5E7EB);
    wx_circle(w, cx + r * 0.6f, cy - r * 0.4f, r * 0.85f, C_BG);   /* cut a crescent */
}

/* Draw weather condition `c` in a size x size box (design grid 100). */
static void draw_weather(lv_layer_t *l, float x0, float y0, float size, int c, int night)
{
    wx_t w = {l, x0, y0, size / 100.0f};
    switch (c) {
    case NAV_WX_CLEAR:
        if (night) wx_moon(&w, 50, 50, 26); else wx_sun(&w, 50, 50, 22);
        break;
    case NAV_WX_PARTLY:
        if (night) wx_moon(&w, 36, 34, 16); else wx_sun(&w, 36, 34, 14);
        wx_cloud(&w, 8, 12, 0xE2E8F0);
        break;
    case NAV_WX_CLOUDY:
        wx_cloud(&w, 0, 8, 0xC3CDDB);
        break;
    case NAV_WX_FOG:
        wx_cloud(&w, 0, -6, 0xAAB4C3);
        wx_line(&w, 24, 72, 76, 72, 5, 0x94A0B4);
        wx_line(&w, 34, 84, 86, 84, 5, 0x94A0B4);
        wx_line(&w, 20, 96, 66, 96, 5, 0x94A0B4);
        break;
    case NAV_WX_DRIZZLE:
        wx_cloud(&w, 0, -8, 0x8B99AE);
        for (int i = 0; i < 3; i++)
            wx_line(&w, 34 + i * 16, 72, 31 + i * 16, 80, 4, 0x60A5FA);
        break;
    case NAV_WX_RAIN:
        wx_cloud(&w, 0, -8, 0x8B99AE);
        for (int i = 0; i < 3; i++)
            wx_line(&w, 36 + i * 16, 68, 30 + i * 16, 90, 5, 0x60A5FA);
        break;
    case NAV_WX_THUNDER:
        wx_cloud(&w, 0, -8, 0x7C8AA0);
        wx_line(&w, 56, 60, 44, 78, 6, 0xFBBF24);
        wx_line(&w, 44, 78, 58, 78, 6, 0xFBBF24);
        wx_line(&w, 58, 78, 46, 98, 6, 0xFBBF24);
        break;
    case NAV_WX_SNOW:
        wx_cloud(&w, 0, -8, 0xB4C0D0);
        wx_circle(&w, 34, 78, 5, 0xFFFFFF);
        wx_circle(&w, 52, 90, 5, 0xFFFFFF);
        wx_circle(&w, 66, 76, 5, 0xFFFFFF);
        break;
    }
}

static float temp_disp(float c) { return U.metric ? c : c * 1.8f + 32.0f; }

static int hour_is_night(int h) { return h < 6 || h >= 20; }

static void wx_icon_draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    intptr_t which = (intptr_t)lv_event_get_user_data(e);
    if (!U.have_wx) return;

    if (which == 2) {                       /* current condition */
        draw_weather(layer, a.x1, a.y1, lv_area_get_width(&a), U.wx.condition,
                     U.wx.flags & NAV_WX_FLAG_NIGHT);
        return;
    }
    if (which >= 10) {                      /* hourly forecast icon */
        int i = (int)which - 10;
        draw_weather(layer, a.x1, a.y1, lv_area_get_width(&a), U.wx.hourly[i].condition,
                     hour_is_night((U.wx.hour0 + i) % 24));
        return;
    }
    /* wind arrow, relative to the direction you are riding: up = headwind */
    float to = U.wx.wind_from_deg + 180.0f - U.hdg;
    float ang = to * DEG2RAD, sn = sinf(ang), cs = cosf(ang);
    float cx = (a.x1 + a.x2) / 2.0f, cy = (a.y1 + a.y2) / 2.0f;
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = lv_color_hex(0x7DD3FC);
    d.width = S(4);
    d.round_start = d.round_end = 1;
    const float w11 = SF(11), w9 = SF(9);
    float tx = cx - sn * w11, ty = cy + cs * w11, hx = cx + sn * w11, hy = cy - cs * w11;
    map_line(layer, &d, tx, ty, hx, hy);
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        float wa = ang + PI_F + sgn * 0.7f;
        map_line(layer, &d, hx, hy, hx + sinf(wa) * w9, hy - cosf(wa) * w9);
    }
}

/* Translucent panels over the map: a solid part at the screen edge fading into the map. user_data 0 = top (turn info), 1 = bottom (remaining / ETA). */
static void fade_draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    int bottom = (int)(intptr_t)lv_event_get_user_data(e);
    const int solid_h = bottom ? S(70) : S(82), fade_h = S(54), top_opa = 215;
    {   /* The fast way: the opacity of every row of the object, then one pass over the rows (see rows_blend). Falls back to LVGL's rectangles below. */
        const int rows = (int)lv_area_get_height(&a);
        uint8_t alpha[200];
        const int opa5[5] = { top_opa, top_opa * 844 / 1000, top_opa * 500 / 1000, top_opa * 156 / 1000, 0 };
        if (rows <= (int)sizeof alpha) {
            for (int r = 0; r < rows; r++) {
                const int dist = bottom ? (rows - 1 - r) : r;              /* rows from the screen edge */
                int v;
                if (dist < solid_h) v = top_opa;
                else if (dist >= solid_h + fade_h) v = 0;
                else {
                    const int f = dist - solid_h;                          /* 0 .. fade_h-1 into the fade, four linear pieces */
                    const int piece = f * 4 / fade_h, n0 = fade_h * piece / 4, n1 = fade_h * (piece + 1) / 4;
                    const int span = n1 - n0 > 1 ? n1 - n0 - 1 : 1;               /* the piece's first row has its start opacity, its last row its end opacity */
                    v = opa5[piece] + (opa5[piece + 1] - opa5[piece]) * (f - n0) / span;
                }
                alpha[r] = (uint8_t)v;
            }
            if (rows_blend(layer, a.x1, a.x2, a.y1, a.y2, lv_color_hex(C_BG), alpha)) return;
        }
    }
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(C_BG);
    d.bg_opa = top_opa;
    if (bottom) {
        lv_area_t r = {a.x1, a.y2 - solid_h + 1, a.x2, a.y2};
        lv_draw_rect(layer, &d, &r);
    } else {
        lv_area_t r = {a.x1, a.y1, a.x2, a.y1 + solid_h - 1};
        lv_draw_rect(layer, &d, &r);
    }
    /* The fade is four linear ramps along an S-curve (smoothstep), so there are no bands or hard edges; opa[] is the opacity at the boundaries, from the solid part outward. */
    const int opa[5] = { top_opa, top_opa * 844 / 1000, top_opa * 500 / 1000, top_opa * 156 / 1000, 0 };
    d.bg_opa = LV_OPA_COVER;
    d.bg_grad.dir = LV_GRAD_DIR_VER;
    d.bg_grad.stops_count = 2;
    d.bg_grad.stops[0].color = d.bg_grad.stops[1].color = lv_color_hex(C_BG);
    d.bg_grad.stops[0].frac = 0;
    d.bg_grad.stops[1].frac = 255;
    for (int i = 0; i < 4; i++) {
        int n0 = fade_h * i / 4, n1 = fade_h * (i + 1) / 4;          /* distance from the solid part, near edge and far edge */
        lv_area_t f;
        if (bottom) {
            f = (lv_area_t){a.x1, a.y2 - solid_h - n1 + 1, a.x2, a.y2 - solid_h - n0};
            d.bg_grad.stops[0].opa = (lv_opa_t)opa[i + 1];             /* the strip's top is the far edge */
            d.bg_grad.stops[1].opa = (lv_opa_t)opa[i];
        } else {
            f = (lv_area_t){a.x1, a.y1 + solid_h + n0, a.x2, a.y1 + solid_h + n1 - 1};
            d.bg_grad.stops[0].opa = (lv_opa_t)opa[i];
            d.bg_grad.stops[1].opa = (lv_opa_t)opa[i + 1];
        }
        lv_draw_rect(layer, &d, &f);
    }
}

/* Test hook: where the map thinks the vehicle is (meters from the route origin) and the map heading. */
void nav_ui_debug_position(float *x, float *y, float *heading_deg) { *x = U.px; *y = U.py; *heading_deg = U.hdg; }

/* How far the picture moved on screen since the last call (px, for a point 120 px from the marker; translation, turning, and zoom together). Called per drawn frame: a jump in it is what the eye sees as a stutter. */
float nav_pose_parts[3];                 /* the last step's three parts: translation, turning, zoom (px) */
float nav_ui_pose_step(void)
{
    static float lx, ly, lh, ls;
    static int have;
    float step = 0;
    if (have) {
        const float tr = hypotf(U.px - lx, U.py - ly) * U.scale;
        const float tu = fabsf(wrap180(U.hdg - lh)) * DEG2RAD * 120.0f;
        const float zo = ls > 0 ? fabsf(U.scale - ls) / U.scale * 120.0f : 0;
        step = sqrtf(tr * tr + tu * tu + zo * zo);
        nav_pose_parts[0] = tr; nav_pose_parts[1] = tu; nav_pose_parts[2] = zo;
    }
    lx = U.px; ly = U.py; lh = U.hdg; ls = U.scale; have = 1;
    return step;
}

/* ---- per-frame + update ---------------------------------------------- */
float nav_ui_cross_px;   /* how far the marker is from the route, in screen pixels (for tests) */

static void project_on_route(void)
{
    if (U.nroute < 2) return;
    float best = 1e30f;
    for (int i = 0; i + 1 < U.nroute; i++) {
        float ax = U.route[i].dx, ay = U.route[i].dy;
        float bx = U.route[i + 1].dx, by = U.route[i + 1].dy;
        float vx = bx - ax, vy = by - ay;
        float len2 = vx * vx + vy * vy;
        float t = len2 > 0 ? ((U.px - ax) * vx + (U.py - ay) * vy) / len2 : 0;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        float qx = ax + t * vx, qy = ay + t * vy;
        float d2 = (U.px - qx) * (U.px - qx) + (U.py - qy) * (U.py - qy);
        if (d2 < best) {
            best = d2;
            U.route_seg = i;
            U.proj_x = qx;
            U.proj_y = qy;
            U.proj_along = U.cum[i] + t * sqrtf(len2);
        }
    }
    U.cross_m = sqrtf(best);
    nav_ui_cross_px = U.cross_m * U.scale;
}

typedef struct { float m; const char *txt; } bar_opt_t;

static void update_scale_bar(void)
{
    static const bar_opt_t imp[] = {
        {30.48f, "100 ft"}, {60.96f, "200 ft"}, {152.4f, "500 ft"}, {304.8f, "1000 ft"},
        {402.3f, "0.25 mi"}, {804.7f, "0.5 mi"}, {1609.3f, "1 mi"}, {3218.7f, "2 mi"},
    };
    static const bar_opt_t met[] = {
        {20, "20 m"}, {50, "50 m"}, {100, "100 m"}, {200, "200 m"},
        {500, "500 m"}, {1000, "1 km"}, {2000, "2 km"},
    };
    const bar_opt_t *tab = U.metric ? met : imp;
    int cnt = U.metric ? (int)(sizeof(met) / sizeof(met[0])) : (int)(sizeof(imp) / sizeof(imp[0]));
    int pick = 0;
    for (int i = 0; i < cnt; i++)
        if (tab[i].m * U.scale <= SF(72.0f)) pick = i;
    U.bar_px = tab[pick].m * U.scale;
    int key = pick + (U.metric ? 100 : 0);
    if (key != U.bar_idx) {
        U.bar_idx = key;
        lv_label_set_text(U.bar_lbl, tab[pick].txt);
    }
}

/* Point on the route `along` meters from its start (clamped to the route). */
static void route_point_at(float along, float *x, float *y)
{
    int n = U.nroute;
    if (n < 2) { *x = U.px; *y = U.py; return; }
    float total = U.cum[n - 1];
    if (along <= 0) { *x = U.route[0].dx; *y = U.route[0].dy; return; }
    if (along >= total) { *x = U.route[n - 1].dx; *y = U.route[n - 1].dy; return; }
    int lo = 0, hi = n - 1;                              /* binary search for the segment */
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        if (U.cum[mid] <= along) lo = mid; else hi = mid;
    }
    float seg = U.cum[hi] - U.cum[lo];
    float t = seg > 0 ? (along - U.cum[lo]) / seg : 0;
    *x = U.route[lo].dx + (U.route[hi].dx - U.route[lo].dx) * t;
    *y = U.route[lo].dy + (U.route[hi].dy - U.route[lo].dy) * t;
}

/* Direction of the road at `along`, degrees clockwise from north, averaged over +-6 m so the map turns smoothly into a corner. */
static float route_bearing(float along)
{
    float x1, y1, x2, y2;
    route_point_at(along - 6.0f, &x1, &y1);
    route_point_at(along + 6.0f, &x2, &y2);
    float dx = x2 - x1, dy = y2 - y1;
    if (dx * dx + dy * dy < 0.01f) return U.hdg_tgt;
    float b = atan2f(dx, dy) * 180.0f / PI_F;
    return b < 0 ? b + 360.0f : b;
}

/* While locked to the route the marker sits on the road and the map turns to the road's direction. */
static void place_on_route(void)
{
    route_point_at(U.along, &U.px, &U.py);
    U.hdg_tgt = route_bearing(U.along);
}

/* Lock the marker to the route when the raw position is close (with hysteresis), so GPS error and drift cannot pull the arrow off the road. */
static void snap_to_route(void)
{
    if (U.nroute < 2) { U.snapped = 0; return; }
    project_on_route();
    if (!U.snapped && U.cross_m < 25.0f) { U.snapped = 1; U.along = U.proj_along; U.along_corr = 0; }
    else if (U.snapped && U.cross_m > 50.0f) U.snapped = 0;
    if (U.snapped) {
        if (fabsf(U.proj_along - U.along) > 40.0f) { U.along = U.proj_along; U.along_corr = 0; }      /* jumped: take the new spot */
        else U.along_corr = (U.proj_along - U.along) * 0.5f;                    /* otherwise half the difference, glided in over the next half second (nav_ui_advance) */
        place_on_route();
    }
}

static void fmt_hms(char *out, size_t n, double sec)
{
    unsigned t = (unsigned)sec;
    snprintf(out, n, "%u:%02u:%02u", t / 3600, (t / 60) % 60, t % 60);
}

static void refresh_trip_labels(void)
{
    char b[24];
    fmt_dist(b, sizeof(b), (uint32_t)U.trip.dist_m);
    lv_label_set_text(U.trip_val[0], b);
    fmt_hms(b, sizeof(b), U.trip.moving_s);
    lv_label_set_text(U.trip_val[1], b);
    float avg = U.trip.moving_s > 1 ? (float)(U.trip.dist_m / U.trip.moving_s * 3.6) : 0;
    snprintf(b, sizeof(b), "%.0f %s", speed_disp(avg), speed_unit());
    lv_label_set_text(U.trip_val[2], b);
    snprintf(b, sizeof(b), "%.0f %s", speed_disp(U.trip.max_kmh), speed_unit());
    lv_label_set_text(U.trip_val[3], b);
    fmt_hms(b, sizeof(b), U.trip.total_s);
    lv_label_set_text(U.trip_val[4], b);
    fmt_hms(b, sizeof(b), U.trip.total_s - U.trip.moving_s);
    lv_label_set_text(U.trip_val[5], b);
}

/* Everything that changes with time; called from the frame timer and from the simulator's screenshot mode. */
/* The zoom (how much road to show) from the distance to the next turn and the speed (see nav_ui_update): wide on a highway, closer in town, closing in on a corner; the turn sits ~110 px up the screen. `dist` is meters to the turn. */
static float zoom_target(float dist)
{
    const float LOOK_S = 12.0f, MIN_V = 5.0f, MIN_D = 38.0f;
    const float look = (U.zoom_v > MIN_V ? U.zoom_v : MIN_V) * LOOK_S;
    float d = dist < look ? dist : look;
    if (d < MIN_D) d = MIN_D;
    const float sc = 90.0f / d;
    return (sc < 0.16f * MORE ? 0.16f * MORE : (sc > 2.4f * MORE ? 2.4f * MORE : sc)) * MB;      /* the turn stays put; the zoom limits move in by MORE */
}

/* The zoom follows its target like a critically damped spring on ln(scale): gentle start and stop, equal time for equal zoom in and out, speed capped. */
static void zoom_follow(float dt)
{
    const float W = 2.6f;                          /* rad/s: about 1.5 s to settle after a jump */
    const float VMAX = 0.9f;                       /* nepers a second (1.3 octaves): the fastest zoom */
    const float AMAX = 2.5f;                       /* nepers a second, each second: how fast it may speed up or slow down */
    const float tgt = logf(U.scale_tgt);
    const int n = dt > 0.05f ? (int)ceilf(dt / 0.04f) : 1;
    const float h = dt / (float)n;
    for (int i = 0; i < n; i++) {
        float acc = W * W * (tgt - U.zoom_log) - 2.0f * W * U.zoom_vel;
        if (acc > AMAX) acc = AMAX;                 /* the push is limited too, so a big jump of the target starts gradually */
        if (acc < -AMAX) acc = -AMAX;
        U.zoom_vel += acc * h;
        if (U.zoom_vel > VMAX) U.zoom_vel = VMAX;
        if (U.zoom_vel < -VMAX) U.zoom_vel = -VMAX;
        U.zoom_log += U.zoom_vel * h;
    }
    U.scale = expf(U.zoom_log);
}

void nav_ui_advance(float dt)
{
    gl_job_step();                                  /* a few milliseconds of the next turn glyph, if one is being made */
    if (dt > 0.25f) dt = 0.25f;

    /* Phone went quiet (app stopped, link dropped): stop coasting and counting trip time; the map holds still until fresh data. */
    U.status_age += dt;
    U.alt_age += dt;
    if (U.alt_age > 15.0f) U.alt_valid = 0;            /* no altitude update for 15 s: show -- rather than a stale value */
    if (U.status_age > 3.0f) U.speed_ms = 0;

    if (U.have_status) {
        {   /* the phone's position correction glides in (time constant 0.6 s); applying it at once jumped the map 7 to 11 px at corners */
            const float f = fminf(1.0f, dt / 0.6f);
            if (U.along_corr != 0) { U.along += U.along_corr * f; U.along_corr -= U.along_corr * f; }
            if (U.corr_x != 0 || U.corr_y != 0) { U.px += U.corr_x * f; U.py += U.corr_y * f; U.corr_x -= U.corr_x * f; U.corr_y -= U.corr_y * f; }
        }
        if (U.snapped) {
            U.along += U.speed_ms * dt;
            float total = U.cum[U.nroute - 1];
            if (U.along > total) U.along = total;           /* stop at the destination, never coast past it */
            place_on_route();
        } else {
            float h = U.hdg_tgt * DEG2RAD;
            U.px += U.speed_ms * dt * sinf(h);
            U.py += U.speed_ms * dt * cosf(h);
        }
    }
    U.hdg += wrap180(U.hdg_tgt - U.hdg) * fminf(1.0f, dt * 6.0f);
    if (U.hdg < 0) U.hdg += 360.0f;
    if (U.hdg >= 360) U.hdg -= 360.0f;
    U.cmp_h += wrap180(U.hdg - U.cmp_h) * fminf(1.0f, dt * 7.0f);      /* the compass card trails the map heading a little: steady, not steppy */
    if (U.cmp_h < 0) U.cmp_h += 360.0f;
    if (U.cmp_h >= 360) U.cmp_h -= 360.0f;
    U.hsin = sinf(U.hdg * DEG2RAD);
    U.hcos = cosf(U.hdg * DEG2RAD);
    if (!U.free_mode && U.have_status) {
        /* between the phone's updates the distance to the turn runs down at the speed, so the zoom target moves smoothly; it aims 0.6 s ahead to cancel most of the spring's lag */
        U.zoom_dist -= U.speed_ms * dt;
        if (U.zoom_dist < 0) U.zoom_dist = 0;
        U.scale_tgt = zoom_target(U.zoom_dist - U.speed_ms * 0.6f);
    }
    zoom_follow(dt);
    {   /* zoom smoothness for the frame log: fastest rate and acceleration since the last report */
        static float prev_log, prev_rate;
        static int started;
        if (!started) { prev_log = U.zoom_log; started = 1; }          /* (the first tick has nothing to be compared with) */
        const float rate = dt > 0 ? (U.zoom_log - prev_log) / dt : 0, acc = dt > 0 ? (rate - prev_rate) / dt : 0;
        prev_log = U.zoom_log; prev_rate = rate;
        if (fabsf(rate) > nav_zoom_stats[0]) nav_zoom_stats[0] = fabsf(rate);
        if (fabsf(acc) > nav_zoom_stats[1]) nav_zoom_stats[1] = fabsf(acc);
        nav_zoom_stats[2] += acc * acc;
        nav_zoom_stats[3] += 1.0f;
    }
    if (nav_ui_zoom_hook) nav_ui_zoom_hook(U.scale, U.scale_tgt, dt);

    if (U.have_status) {
        nav_trip_tick(&U.trip, dt, U.speed_ms);
        if (U.speed_ms > 1.0f) U.trip_dirty = 1;
    }
    /* Save the trip so it survives power loss (ignition off): every 10 s while changing, and once the vehicle has stood still for 2 s. Two alternating slots keep the last good copy. */
    U.save_timer += dt;
    if (U.have_status && U.speed_ms <= 1.0f) U.stopped_s += dt; else U.stopped_s = 0;
    if (U.trip_dirty && (U.save_timer >= 10.0f || (U.stopped_s >= 2.0f && U.save_timer >= 2.0f))) {
        nav_trip_save(&U.trip);
        U.trip_dirty = 0;
        U.save_timer = 0;
    }

    project_on_route();
    update_scale_bar();

    U.trip_refresh += dt;
    if (U.trip_refresh >= 0.5f) {
        U.trip_refresh = 0;
        if (nav_ui_active_page() == 2) refresh_trip_labels();
    }
}

/* ---- compass page ----------------------------------------------------------------------------------------- */
/* The compass dial's hairline rings and tick marks, drawn with the same exact-edge coverage masks as the map (LVGL's arc and line drawing took 12 to 30 ms a frame).
 * Rings come from each pixel's distance to the center; ticks are capsules from nav_seg_cover, one mask per color. Returns 0 if the masks are not available. */
static int cmp_dial_masks(lv_layer_t *layer, const lv_area_t *clip, float cx, float cy, uint32_t ink)
{
    if (!route_masks_ready()) return 0;
    const int w = clip->x2 - clip->x1 + 1, h = clip->y2 - clip->y1 + 1;
    if (w <= 0 || h <= 0 || (size_t)w * h > (size_t)SZ * SZ) return 0;
    uint8_t *m_ring = rt_mask[0], *m_minor = rt_mask[1], *m_major = rt_mask[2];
    memset(m_ring, 0, (size_t)w * h);
    memset(m_minor, 0, (size_t)w * h);
    memset(m_major, 0, (size_t)w * h);

    const float radii[2] = { (float)S(198), (float)S(122) };
    for (int k = 0; k < 2; k++) {
        const float half = 1.0f, R = radii[k] - half;                          /* ring width 2: LVGL's arc radius is the outer edge, so the stroke is centered one pixel in */
        const float ro = R + half + 1.0f, ri = R - half - 1.0f;
        for (int y = clip->y1; y <= clip->y2; y++) {
            const float dy = (y + 0.5f) - cy;
            if (fabsf(dy) > ro) continue;
            const float xo = sqrtf(ro * ro - dy * dy), xi = fabsf(dy) < ri ? sqrtf(ri * ri - dy * dy) : 0.0f;
            uint8_t *row = m_ring + (size_t)(y - clip->y1) * w - clip->x1;
            for (int side = 0; side < 2; side++) {
                float xa = side ? cx + xi : cx - xo, xb = side ? cx + xo : cx - xi;
                int32_t x1 = (int32_t)floorf(xa) - 1, x2 = (int32_t)ceilf(xb) + 1;
                if (x1 < clip->x1) x1 = clip->x1;
                if (x2 > clip->x2) x2 = clip->x2;
                for (int32_t x = x1; x <= x2; x++) {
                    const float dx = (x + 0.5f) - cx, dd = dx * dx + dy * dy - R * R;       /* d^2 - R^2 = (d - R)(d + R) ~ 2R (d - R) */
                    float c = half + 0.5f - fabsf(dd) / (2.0f * R);
                    if (c <= 0) continue;
                    const uint8_t v = (uint8_t)((c > 1 ? 1 : c) * 255.0f + 0.5f);
                    if (v > row[x]) row[x] = v;
                }
            }
        }
    }

    /* tick marks every 5 degrees; the card turns so the heading sits under the pointer at the top */
    for (int deg = 0; deg < 360; deg += 5) {
        if (deg % 90 == 0) continue;                                          /* the letters take these spots */
        const float rel = fmodf((float)deg - U.cmp_h + 720.0f, 360.0f);
        if (rel > 166.0f && rel < 194.0f) continue;                           /* keep the page dots at the bottom clear */
        const int major = deg % 30 == 0, mid = deg % 10 == 0;
        const float th = (deg - U.cmp_h) * DEG2RAD, sn = sinf(th), cs = cosf(th);
        const float r1 = SF(196.0f), r2 = r1 - SF(major ? 15.0f : (mid ? 10.0f : 6.0f));
        const float half = (float)S(major ? 4 : (mid ? 3 : 2)) * 0.5f, pad = half + 1.5f;
        const float ax = cx + sn * r1, ay = cy - cs * r1, bx = cx + sn * r2, by = cy - cs * r2;
        int32_t x1 = (int32_t)floorf((ax < bx ? ax : bx) - pad), x2 = (int32_t)ceilf((ax > bx ? ax : bx) + pad);
        int32_t y1 = (int32_t)floorf((ay < by ? ay : by) - pad), y2 = (int32_t)ceilf((ay > by ? ay : by) + pad);
        if (x1 < clip->x1) x1 = clip->x1;
        if (x2 > clip->x2) x2 = clip->x2;
        if (y1 < clip->y1) y1 = clip->y1;
        if (y2 > clip->y2) y2 = clip->y2;
        if (x1 > x2 || y1 > y2) continue;
        nav_seg_cover(major ? m_major : m_minor, NULL, w, clip->x1, clip->y1, x1, x2, y1, y2, ax, ay, bx, by, half, 0);
    }
    mask_blit(layer, m_ring, w, h, w, clip->x1, clip->y1, lv_color_hex(TH->div));
    mask_blit(layer, m_minor, w, h, w, clip->x1, clip->y1, lv_color_hex(TH->edge));
    mask_blit(layer, m_major, w, h, w, clip->x1, clip->y1, lv_color_hex(ink));
    return 1;
}

static void cmp_draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    float cx = a.x1 + SZ / 2.0f, cy = a.y1 + SZ / 2.0f;
    int moving = U.speed_ms > 0.9f || !U.have_status || (U.st.flags & NAV_FLAG_COMPASS_LIVE);
    uint32_t ink = moving ? TH->text : TH->sub;

    const lv_area_t cclip = layer->_clip_area;
    const int dial_done = cmp_dial_masks(layer, &cclip, cx, cy, ink);
    /* outer hairline ring, and an inner one that frames the readouts */
    lv_draw_arc_dsc_t ad;
    lv_draw_arc_dsc_init(&ad);
    ad.start_angle = 0; ad.end_angle = 360; ad.center.x = (int32_t)cx; ad.center.y = (int32_t)cy; ad.opa = LV_OPA_COVER;
    if (!dial_done) {
    ad.radius = S(198); ad.width = 2; ad.color = lv_color_hex(TH->div); lv_draw_arc(layer, &ad);
    ad.radius = S(122); ad.width = 2; ad.color = lv_color_hex(TH->div); lv_draw_arc(layer, &ad);
    }

    /* tick marks every 5 degrees; the card turns so the heading sits under the pointer at the top */
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.round_start = ld.round_end = 1;
    for (int deg = 0; deg < 360 && !dial_done; deg += 5) {
        float th = (deg - U.cmp_h) * DEG2RAD, sn = sinf(th), cs = cosf(th);
        int major = deg % 30 == 0, mid = deg % 10 == 0;
        float r1 = SF(196.0f), r2 = r1 - SF(major ? 15.0f : (mid ? 10.0f : 6.0f));
        ld.width = S(major ? 4 : (mid ? 3 : 2));
        ld.color = lv_color_hex(deg == 0 ? TH->north : (major ? ink : TH->edge));
        if (deg % 90 == 0) continue;                                   /* the letters take these spots */
        float rel = fmodf((float)deg - U.cmp_h + 720.0f, 360.0f);
        if (rel > 166.0f && rel < 194.0f) continue;                    /* keep the page dots at the bottom clear */
        ld.p1.x = cx + sn * r1; ld.p1.y = cy - cs * r1;
        ld.p2.x = cx + sn * r2; ld.p2.y = cy - cs * r2;
        lv_draw_line(layer, &ld);
    }

    /* the fixed pointer at the top: a bright wedge pointing at the card */
    lv_draw_triangle_dsc_t t;
    lv_draw_triangle_dsc_init(&t);
    t.color = lv_color_hex(TH->accent); t.opa = LV_OPA_COVER;
    t.p[0].x = cx;          t.p[0].y = cy - SF(170.0f);
    t.p[1].x = cx - SF(13); t.p[1].y = cy - SF(203.0f);
    t.p[2].x = cx + SF(13); t.p[2].y = cy - SF(203.0f);
    lv_draw_triangle(layer, &t);

    /* climb / descent marker next to the elevation value */
    if (U.alt_valid && fabsf(U.vs_mps) > 0.4f) {
        int up = U.vs_mps > 0;
        t.color = lv_color_hex(up ? 0x34C759 : 0xFF9F0A);
        float ax = cx - U.cmp_elev_w / 2 - SF(17), ay = cy + SF(50);      /* right beside the number, centred on its digits */
        const float c10 = SF(10), c8 = SF(8);
        t.p[0].x = ax;        t.p[0].y = ay + (up ? -c10 : c10);
        t.p[1].x = ax - c10;  t.p[1].y = ay + (up ? c8 : -c8);
        t.p[2].x = ax + c10;  t.p[2].y = ay + (up ? c8 : -c8);
        lv_draw_triangle(layer, &t);
    }
}

static const char *const CARDINAL16[16] = {"N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"};

static void cmp_refresh(void)
{
    char b[40];
    int deg = ((int)lroundf(U.cmp_h) % 360 + 360) % 360;
    snprintf(b, sizeof b, "%03d\xC2\xB0", deg);
    lv_label_set_text(U.cmp_deg, b);
    lv_label_set_text(U.cmp_card, CARDINAL16[(int)((U.cmp_h + 11.25f) / 22.5f) % 16]);
    if (U.alt_valid) {
        long ft = lroundf(U.metric ? U.alt_m : U.alt_m * 3.28084f);           /* (named ft, but meters when metric) */
        char num[24];
        snprintf(num, sizeof num, "%ld", labs(ft));
        char out[32]; int n = (int)strlen(num), o = 0;                    /* thousands separator */
        if (ft < 0) out[o++] = '-';
        for (int i = 0; i < n; i++) { out[o++] = num[i]; if ((n - i - 1) % 3 == 0 && i < n - 1) out[o++] = ','; }
        out[o] = 0;
        snprintf(b, sizeof b, "%s %s", out, U.metric ? "m" : "ft");
        lv_label_set_text(U.cmp_elev, b);
        lv_point_t sz;
        lv_text_get_size(&sz, b, FT28, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        U.cmp_elev_w = (float)sz.x;
        float fpm = U.vs_mps * (U.metric ? 60.0f : 196.85f);                    /* climb rate: m/min or ft/min */
        if (fabsf(U.vs_mps) > 0.4f) snprintf(b, sizeof b, "%s%.0f %s", fpm > 0 ? "+" : "-", fabsf(fpm), U.metric ? "m/min" : "ft/min");
        else snprintf(b, sizeof b, "level");
        lv_label_set_text(U.cmp_vs, b);
    } else {
        lv_label_set_text(U.cmp_elev, "--");
        lv_label_set_text(U.cmp_vs, "");
    }
    {   /* N E S W (large) and NE SE SW NW (small) ride the ring with the card but stay upright */
        int live = U.speed_ms > 0.9f || !U.have_status || (U.st.flags & NAV_FLAG_COMPASS_LIVE);
        for (int i = 0; i < 4; i++) {
            float th = (i * 90 - U.cmp_h) * DEG2RAD;
            lv_obj_update_layout(U.cmp_cd[i]);
            int w = lv_obj_get_width(U.cmp_cd[i]), h = lv_obj_get_height(U.cmp_cd[i]);
            lv_obj_set_pos(U.cmp_cd[i], (int)(SZ / 2.0f + sinf(th) * SF(151.0f) - w / 2.0f), (int)(SZ / 2.0f - cosf(th) * SF(151.0f) - h / 2.0f));
            lv_obj_set_style_text_color(U.cmp_cd[i], lv_color_hex(i == 0 ? TH->north : (live ? TH->text : TH->sub)), 0);
        }
    }
    {

        int live = U.speed_ms > 0.9f || !U.have_status || (U.st.flags & NAV_FLAG_COMPASS_LIVE);
        for (int i = 0; i < 4; i++) {
            float th = (45 + i * 90 - U.cmp_h) * DEG2RAD;
            lv_obj_update_layout(U.cmp_ic[i]);
            int w = lv_obj_get_width(U.cmp_ic[i]), h = lv_obj_get_height(U.cmp_ic[i]);
            lv_obj_set_pos(U.cmp_ic[i], (int)(SZ / 2.0f + sinf(th) * SF(156.0f) - w / 2.0f), (int)(SZ / 2.0f - cosf(th) * SF(156.0f) - h / 2.0f));
            lv_obj_set_style_text_color(U.cmp_ic[i], lv_color_hex(live ? TH->sub : TH->dim), 0);   /* quieter than the cardinals */
        }
    }
    int moving = U.speed_ms > 0.9f || !U.have_status || (U.st.flags & NAV_FLAG_COMPASS_LIVE);
    if (moving) lv_obj_add_flag(U.cmp_hold, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(U.cmp_hold, LV_OBJ_FLAG_HIDDEN);
}

void nav_ui_set_altitude(float altitude_m, float vspeed_mps, int valid)
{
    U.alt_m = altitude_m; U.vs_mps = vspeed_mps; U.alt_valid = valid; U.alt_age = 0;
}

static void cmp_tick(uint32_t now)
{
    if (nav_ui_active_page() != 3) return;                             /* only while the page is on screen */
    if (now - U.cmp_tick < 45) return;
    U.cmp_tick = now;
    cmp_refresh();
    lv_obj_invalidate(U.cmp_obj);
}

/* ---- clock page ------------------------------------------------------------------------------------------ */
static const char *const DOW[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *const MON[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

/* Local time = UTC + the phone's offset (so daylight saving is right). Unknown until the clock has been set. */
static void clock_refresh(void)
{
    time_t utc = time(NULL);
    char t[8] = "--:--", ap[4] = "", d[24] = "";
    if (utc > 1700000000) {                                   /* after Nov 2023: the clock has been set */
        time_t loc = utc + U.tz_offset_s;
        struct tm tmv;
        gmtime_r(&loc, &tmv);
        if (U.clock24) {
            snprintf(t, sizeof t, "%02d:%02d", tmv.tm_hour, tmv.tm_min);          /* 14:05; no AM/PM */
        } else {
            snprintf(t, sizeof t, "%d:%02d", tmv.tm_hour % 12 == 0 ? 12 : tmv.tm_hour % 12, tmv.tm_min);
            snprintf(ap, sizeof ap, "%s", tmv.tm_hour < 12 ? "AM" : "PM");
        }
        snprintf(d, sizeof d, "%s, %s %d", DOW[tmv.tm_wday % 7], MON[tmv.tm_mon % 12], tmv.tm_mday);
    }
    char key[24];
    snprintf(key, sizeof key, "%s|%s|%s", t, ap, d);
    if (strcmp(key, U.clk_text) == 0) return;
    snprintf(U.clk_text, sizeof U.clk_text, "%s", key);
    lv_label_set_text(U.clk_time, t);
    lv_label_set_text(U.clk_ampm, ap);
    lv_label_set_text(U.clk_date, d);
}

void nav_ui_set_clock_offset(int32_t seconds_east_of_utc)
{
    U.tz_offset_s = seconds_east_of_utc;
    clock_refresh();
}


static void frame_cb_body(lv_timer_t *t);
static void frame_cb(lv_timer_t *t)
{
    const uint32_t t0 = PERF_NOW();
    frame_cb_body(t);
    const uint32_t us = PERF_NOW() - t0;
    if (us > nav_timer_max_us[0]) nav_timer_max_us[0] = us;
}

static void frame_cb_body(lv_timer_t *t)
{
    (void)t;
    uint32_t now = lv_tick_get();
    float dt = (now - U.last_tick) / 1000.0f;
    U.last_tick = now;
    nav_ui_advance(dt);
    if (now - U.clk_tick >= 500) { U.clk_tick = now; if (nav_ui_active_page() == 4) clock_refresh(); }
    cmp_tick(now);
    tpms_tick(dt, now);

    /* Only the page on screen is redrawn or refreshed; the others are left alone until they are swiped to. */
    int pg = nav_ui_active_page();
    int logo = U.splash && !lv_obj_has_flag(U.splash, LV_OBJ_FLAG_HIDDEN);
    if (pg == 0 && !logo) {
        const float reach = SF(200.0f);          /* pixels from the marker to the far edge: what a turn or a zoom moves the most */
        const float moved = hypotf(U.px - map_seen.px, U.py - map_seen.py) * U.scale;
        const float turned = fabsf(wrap180(U.hdg - map_seen.hdg)) * DEG2RAD * reach;
        const float zoomed = map_seen.scale > 0 ? fabsf(U.scale - map_seen.scale) / map_seen.scale * reach : reach;
        if (map_dirty || moved > 0.05f || turned > 0.05f || zoomed > 0.05f) {
            map_dirty = 0;
            map_seen.px = U.px; map_seen.py = U.py; map_seen.hdg = U.hdg; map_seen.scale = U.scale;
            lv_obj_invalidate(U.map);
        }
    }
    if (pg == 1) lv_obj_invalidate(U.pw_wind);
}

static int night_mode = 1;
static int style_mode;                 /* look id: see nav_ui.h */

static const theme_t *theme_pick(void)
{
    if (style_mode == 1) return &THEME_VINTAGE_NIGHT;           /* vintage night: always the green face */
    if (style_mode == 7) return &THEME_VINTAGE_DAY;
    if (style_mode == 8) return night_mode ? &THEME_VINTAGE_NIGHT : &THEME_VINTAGE_DAY;      /* vintage auto */
    if (style_mode == 2) return &THEME_TRIPPER_DAY;          /* the two tripper faces are separate choices, not following day/night */
    if (style_mode == 3) return &THEME_TRIPPER_NIGHT;
    if (style_mode == 5) return &THEME_NIGHT;                 /* standard night: always dark */
    if (style_mode == 6) return &THEME_DAY;                   /* standard day: always light */
    if (style_mode == 4) return night_mode ? &THEME_TRIPPER_NIGHT : &THEME_TRIPPER_DAY;      /* auto: follows the phone's day/night */
    return night_mode ? &THEME_NIGHT : &THEME_DAY;
}

static void splash_restyle(void);

static void theme_apply(void)
{
    lv_obj_set_style_bg_color(U.round, lv_color_hex(TH->bg), 0);
    lv_obj_set_style_bg_color(U.map, lv_color_hex(TH->map), 0);
    if (U.bat_icon) {
        lv_obj_set_style_bg_color(U.bat_pill, lv_color_hex(TH->bg), 0);
        lv_obj_set_style_border_color(U.bat_icon, lv_color_hex(TH->sub), 0);
        lv_obj_set_style_bg_color(U.bat_nub, lv_color_hex(TH->sub), 0);
    }
    for (int i = 0; i < nthemed; i++) {
        if (themed[i].role == ROLE_DIV) lv_obj_set_style_bg_color(themed[i].o, lv_color_hex(TH->div), 0);
        else lv_obj_set_style_text_color(themed[i].o, lv_color_hex(theme_role_color(themed[i].role)), 0);
    }
    if (gl_man >= 0) gl_start_job((gl_job_man >= 0 && !gl_job_bake) ? gl_job_man : gl_man, 0);            /* the turn glyph is rasterized in the accent color (in the background) */
    marker_render();                               /* ... and so is the position marker */
    if (U.have_wx) refresh_weather();
    tpms_refresh();
    tile_changed_cb(NULL);
    splash_restyle();                              /* the start-up screen follows the theme too */
    lv_obj_invalidate(lv_screen_active());
}

/* Night = dark theme (default), day = light; the phone decides (sunrise/sunset or manual) and sends it in the STATUS night flag. */
void nav_ui_set_night(int night)
{
    night = night ? 1 : 0;
    if (night == night_mode) return;
    night_mode = night;
    const theme_t *want = theme_pick();
    if (want == TH) return;                        /* the vintage palette is the same day and night */
    TH = want;
    theme_apply();
}

/* Look id: see nav_ui.h. */
void nav_ui_set_style(int style)
{
    style = (style >= 1 && style <= 8) ? style : 0;
    if (style == style_mode) return;
    style_mode = style;
    TH = theme_pick();
    theme_apply();
}
int nav_ui_get_style(void) { return style_mode; }
int nav_ui_is_night(void) { return night_mode; }

/* Free-ride bottom row, right side: the current temperature from the phone's weather ("--" until it arrives). */
static void free_temp_label(void)
{
    char b[16];
    if (U.have_wx) snprintf(b, sizeof(b), "%.0f\xC2\xB0", temp_disp(U.wx.temp_x10 / 10.0f));
    else           snprintf(b, sizeof(b), "--");
    lv_label_set_text(U.eta_val, b);
}

void nav_ui_update(const nav_status_t *st)
{
    const int had_status = U.have_status;
    const float old_px = U.px, old_py = U.py;         /* where the map is now, before the phone's position is applied */
    U.st = *st;
    U.have_status = 1;
    nav_ui_set_night((st->flags & NAV_FLAG_NIGHT) != 0);
    if (st->maneuver != gl_man || gl_job_man >= 0) gl_request(st->maneuver);      /* a new maneuver: the kept arrow, or drawn in the background (see gl_job_step) */
    int navigating = (st->flags & NAV_FLAG_NAVIGATING) != 0;
    if (!navigating && U.nroute) {                       /* navigation ended: drop the old route, keep the frame */
        U.nroute = 0;
        U.snapped = 0;
        map_dirty = 1;
    }
    /* No route: the map frame's origin stays where the first fix was so the shifted roads and areas stay valid; re-centre only after driving far from it. */
    if (!U.nroute && (!U.have_origin || fabsf(U.px) > 12000.0f || fabsf(U.py) > 12000.0f)) {
        U.olat = st->lat_e7;
        U.olon = st->lon_e7;
        U.have_origin = 1;
    }

    double cos_lat = cos(U.olat * 1e-7 * (double)DEG2RAD);
    U.px = (float)((st->lon_e7 - U.olon) * 1e-7 * 111320.0 * cos_lat);
    U.py = (float)((st->lat_e7 - U.olat) * 1e-7 * 111320.0);
    U.hdg_tgt = st->heading_x10 / 10.0f;
    U.speed_ms = st->speed_kmh_x10 / 36.0f;
    U.status_age = 0;
    snap_to_route();       /* overrides position + heading when the raw fix is on the route */
    if (!U.snapped) {      /* off the route the correction glides in too (a big jump, a new frame, or the first fix is taken at once) */
        const float dx = U.px - old_px, dy = U.py - old_py;
        if (had_status && hypotf(dx, dy) < 40.0f) { U.corr_x = dx; U.corr_y = dy; U.px = old_px; U.py = old_py; }
        else U.corr_x = U.corr_y = 0;
    } else { U.corr_x = U.corr_y = 0; }

    /* Zoom shows about LOOK_S seconds of road ahead at your speed, never more than the distance to the next turn or stop; the turn sits ~110 px up the screen.
     * Speed is smoothed (~3 s), and below 5 m/s (11 mph) the look-ahead is taken as 5 m/s so a stop holds a close zoom. */
    float v_now = st->speed_kmh_x10 / 36.0f;
    uint32_t tnow = lv_tick_get();
    float dts = U.zoom_t ? (tnow - U.zoom_t) / 1000.0f : 99.0f;
    U.zoom_t = tnow;
    if (dts > 5.0f) U.zoom_v = v_now;                                   /* first update, or a long gap: start from the current speed */
    else U.zoom_v += (v_now - U.zoom_v) * (1.0f - expf(-dts / 3.0f));   /* ~3 s time constant, whatever the update rate */
    {   /* the distance to the turn: keep the running estimate and pull it toward the phone's figure unless it jumped (a new turn or route) */
        const float meas = (float)st->dist_turn_m;
        if (!U.zoom_dist_valid || fabsf(meas - U.zoom_dist) > 60.0f) U.zoom_dist = meas;
        else U.zoom_dist += (meas - U.zoom_dist) * 0.6f;
        U.zoom_dist_valid = 1;
    }
    U.scale_tgt = zoom_target(U.zoom_dist - U.speed_ms * 0.6f);

    if (!navigating) {                                   /* free ride: where you are, no turn instructions */
        U.free_mode = 1;
        U.scale_tgt = 0.30f * MB * MORE;
        lv_obj_add_flag(U.arrow, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(U.dist, "");
        char place[NAV_STREET_LEN + 1];
        memcpy(place, st->street, NAV_STREET_LEN);
        place[NAV_STREET_LEN] = 0;
        pos(U.street, (DSZ - 272) / 2, 52);           /* no arrow: centre the place name on the display */
        wid(U.street, 272);
        lv_obj_set_style_text_font(U.street, FB26, 0);      /* bigger without the arrow beside it */
        lv_obj_set_style_text_align(U.street, LV_TEXT_ALIGN_CENTER, 0);
        for (char *c = place; *c; c++) if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);      /* ALL CAPS: TUMWATER, WA */
        lv_label_set_text(U.street, place);
        char fb[24];
        snprintf(fb, sizeof(fb), "%.0f", speed_disp(st->speed_kmh_x10 / 10.0f));
        lv_label_set_text(U.rem_val, fb);
        /* bottom row: two centred columns (number over caption), symmetric and well inside the round edge (the lowest text is at y=372, still 240 px wide) */
        const int col_dx = 62, vy = 296, cy = 350;
        lv_obj_set_style_text_font(U.rem_val, FT48, 0);
        lv_obj_set_style_text_font(U.eta_val, FT48, 0);
        wid(U.rem_val, 100);
        pos(U.rem_val, 206 - col_dx - 50, vy);
        lv_obj_set_style_text_align(U.rem_val, LV_TEXT_ALIGN_CENTER, 0);
        wid(U.eta_val, 100);
        pos(U.eta_val, 206 + col_dx - 50, vy);
        lv_obj_set_style_text_align(U.eta_val, LV_TEXT_ALIGN_CENTER, 0);
        pos(U.cap_l, 206 - col_dx - 50, cy);
        pos(U.cap_r, 206 + col_dx - 50, cy);
        lv_obj_remove_flag(U.cap_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(U.cap_r, LV_OBJ_FLAG_HIDDEN);
        pos(U.stat_div, 206, vy + 16);
        lv_obj_add_flag(U.stat_div, LV_OBJ_FLAG_HIDDEN);       /* no divider between the speed and the temperature */
        free_temp_label();
        lv_obj_invalidate(U.map);
        return;
    }
    if (U.free_mode) {
        U.free_mode = 0;
        lv_obj_remove_flag(U.arrow, LV_OBJ_FLAG_HIDDEN);
        pos(U.street, STREET_X, STREET_Y);            /* back beside the turn arrow */
        wid(U.street, STREET_W);
        lv_obj_set_style_text_align(U.street, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_font(U.street, FB22, 0);
        pos(U.rem_val, 86, STAT_Y);                   /* bottom row back to its navigating layout */
        pos(U.eta_val, 209, STAT_Y);
        wid(U.rem_val, 118);
        wid(U.eta_val, 118);
        lv_obj_set_style_text_font(U.rem_val, FT28, 0);
        lv_obj_set_style_text_font(U.eta_val, FT28, 0);
        lv_obj_add_flag(U.cap_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(U.cap_r, LV_OBJ_FLAG_HIDDEN);
        pos(U.stat_div, 206, STAT_Y + 3);
        lv_obj_remove_flag(U.stat_div, LV_OBJ_FLAG_HIDDEN);    /* back for the distance and the time */
    }

    char b[48];
    fmt_dist(b, sizeof(b), st->dist_turn_m);
    lv_label_set_text(U.dist, st->maneuver == NAV_MAN_ARRIVE && st->dist_turn_m < 15 ? "Arrived" : b);

    char street[NAV_STREET_LEN + 1];
    memcpy(street, st->street, NAV_STREET_LEN);
    street[NAV_STREET_LEN] = 0;
    lv_label_set_text(U.street, street);

    fmt_dist(b, sizeof(b), st->remaining_m);
    lv_label_set_text(U.rem_val, b);
    if (st->eta_min >= 60) snprintf(b, sizeof(b), "%u h %02u", st->eta_min / 60, st->eta_min % 60);
    else                   snprintf(b, sizeof(b), "%u min", st->eta_min);
    lv_label_set_text(U.eta_val, b);

    lv_obj_invalidate(U.arrow);
}

void nav_ui_set_route(int32_t olat, int32_t olon, const nav_pt_t *pts, uint16_t n)
{
    if (n > NAV_UI_MAX_ROUTE) n = NAV_UI_MAX_ROUTE;
    U.olat = olat;
    U.olon = olon;
    U.have_origin = 1;
    U.route = pts;
    U.nroute = n;
    U.route_seg = 0;
    map_dirty = 1;
    rt_rev++; rt_cache_n = -1;
    tf_n = 0;                           /* the point numbers belong to the old route */
    U.cum[0] = 0;
    for (int i = 1; i < n; i++)
        U.cum[i] = U.cum[i - 1] + hypotf((float)(pts[i].dx - pts[i - 1].dx), (float)(pts[i].dy - pts[i - 1].dy));
    U.snapped = 0;                      /* the coordinate frame changed; re-lock on the next position */
    U.along_corr = 0; U.corr_x = U.corr_y = 0;       /* a pending correction belonged to the old frame */
    if (U.have_status) {                /* meanwhile express the last known position in the new frame */
        double cos_lat = cos(U.olat * 1e-7 * (double)DEG2RAD);
        U.px = (float)((U.st.lon_e7 - U.olon) * 1e-7 * 111320.0 * cos_lat);
        U.py = (float)((U.st.lat_e7 - U.olat) * 1e-7 * 111320.0);
    }
}

static void refresh_weather(void)
{
    if (!U.have_wx) return;
    char b[40];
    float wind = speed_disp((float)U.wx.wind_kmh);
    static const char *WX_NAME[] = {"Clear", "Partly cloudy", "Cloudy", "Fog", "Drizzle", "Rain", "Thunderstorm", "Snow"};
    snprintf(b, sizeof(b), "%.0f\xC2\xB0%s", temp_disp(U.wx.temp_x10 / 10.0f), U.metric ? "C" : "F");
    lv_label_set_text(U.pw_temp, b);
    lv_label_set_text(U.pw_cond, WX_NAME[U.wx.condition < 8 ? U.wx.condition : 0]);

    /* rain heads-up if it is dry now but likely within the next few hours */
    int rain_in = -1;
    if (U.wx.precip_pct < 50)
        for (int i = 0; i < 4; i++)
            if (U.wx.hourly[i].precip_pct >= 50) { rain_in = i + 1; break; }
    if (rain_in > 0) {
        snprintf(b, sizeof(b), "Rain likely in %d h", rain_in);
        lv_obj_set_style_text_color(U.pw_row, lv_color_hex(0xF59E0B), 0);
    } else {
        snprintf(b, sizeof(b), "Feels %.0f\xC2\xB0  H %.0f\xC2\xB0  L %.0f\xC2\xB0",
                 temp_disp(U.wx.feels_x10 / 10.0f), temp_disp(U.wx.hi_c), temp_disp(U.wx.lo_c));
        lv_obj_set_style_text_color(U.pw_row, lv_color_hex(C_SUB), 0);
    }
    lv_label_set_text(U.pw_row, b);

    snprintf(b, sizeof(b), "%.0f %s", wind, speed_unit());
    lv_label_set_text(U.pw_wind_val, b);
    snprintf(b, sizeof(b), "%s  gust %.0f", cardinal((float)U.wx.wind_from_deg), speed_disp((float)U.wx.gust_kmh));
    lv_label_set_text(U.pw_wind_sub, b);
    snprintf(b, sizeof(b), "%u%%", U.wx.precip_pct);
    lv_label_set_text(U.pw_rain, b);

    for (int i = 0; i < 4; i++) {
        int h = (U.wx.hour0 + i) % 24;
        if (U.clock24) snprintf(b, sizeof(b), "%02d:00", h);
        else          snprintf(b, sizeof(b), "%d %s", h % 12 == 0 ? 12 : h % 12, h < 12 ? "AM" : "PM");
        lv_label_set_text(U.hr_time[i], b);
        snprintf(b, sizeof(b), "%.0f\xC2\xB0", temp_disp(U.wx.hourly[i].temp_c));
        lv_label_set_text(U.hr_temp[i], b);
        snprintf(b, sizeof(b), "%u%%", U.wx.hourly[i].precip_pct);
        lv_label_set_text(U.hr_pop[i], b);
        lv_obj_set_style_text_color(U.hr_pop[i],
            lv_color_hex(U.wx.hourly[i].precip_pct >= 50 ? 0x60A5FA : TH->dim), 0);
        lv_obj_invalidate(U.hr_icon[i]);
    }
    lv_obj_invalidate(U.pw_icon);
}

void nav_ui_set_weather(const nav_weather_t *wx)
{
    U.wx = *wx;
    U.have_wx = 1;
    if (nav_ui_active_page() == 1) refresh_weather();   /* the weather page is only rebuilt while it is on screen... */
    if (U.free_mode) free_temp_label();                /* ...the map's temperature readout is cheap and always current */
}

void nav_ui_set_metric(int metric)
{
    U.metric = metric ? 1 : 0;
    map_dirty = 1;                        /* the scale bar changes length */
    U.bar_idx = -1;                       /* force the scale bar label to refresh */
    if (U.cap_l) lv_label_set_text(U.cap_l, speed_unit());          /* the free-ride speed caption: mph or km/h */
    refresh_trip_labels();
    refresh_weather();
    cmp_refresh();                        /* elevation and climb rate */
    if (U.have_status) nav_ui_update(&U.st);
}

/* Battery for the top of the map page: percent 0..100 (-1 = no battery or gauge) and charging; shown only while charging or at 30% and below. */
void nav_ui_set_battery(int percent, int charging)
{
    if (!U.bat_icon) return;
    U.bat_pct = percent;
    /* Shown only while charging or at 30% or less; hides again at 35% so a reading near the limit does not flicker. */
    if (percent < 0) U.bat_shown = 0;
    else if (charging || percent <= 30) U.bat_shown = 1;
    else if (percent >= 35) U.bat_shown = 0;
    const int show = U.bat_shown;
    if (show) lv_obj_remove_flag(U.bat_pill, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(U.bat_pill, LV_OBJ_FLAG_HIDDEN);
    if (!show) return;
    if (percent > 100) percent = 100;
    lv_obj_set_width(U.bat_fill, percent * S(30) / 100 > 1 ? percent * S(30) / 100 : 2);
    lv_obj_set_style_bg_color(U.bat_fill, lv_color_hex(percent <= 15 ? 0xEF4444 : (percent <= 30 ? 0xF59E0B : 0x34C759)), 0);
    char t[24];
    snprintf(t, sizeof t, "%d%%", percent);
    lv_label_set_text(U.bat_txt, t);
    if (charging) lv_obj_remove_flag(U.bat_bolt, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(U.bat_bolt, LV_OBJ_FLAG_HIDDEN);
}

void nav_ui_set_clock24(int clock24)
{
    U.clock24 = clock24 ? 1 : 0;
    if (U.clk_time) {
        /* 24-hour has no AM/PM line under the time, so the block sits a little lower to stay centred */
        pos(U.clk_date, 46, U.clock24 ? 118 : 106);
        pos(U.clk_time, 10, U.clock24 ? 172 : 153);
    }
    U.clk_text[0] = 0;                    /* force the clock page to redraw */
    if (U.clk_time) clock_refresh();
    refresh_weather();                    /* the hour labels */
}

void nav_ui_trip_reset(void)
{
    nav_trip_reset(&U.trip);
    nav_trip_save(&U.trip);          /* a reset must stick across a power cycle too */
    U.trip_dirty = 0;
    refresh_trip_labels();
}

static lv_obj_t *page_obj(int id)
{
    switch (id) {
    case 0: return U.tile_nav; case 1: return U.tile_wx; case 2: return U.tile_trip; case 3: return U.tile_cmp;
    case 4: return U.tile_clk;
    default: return U.tile_tpms;
    }
}

/* The id of the screen on the display now (ids never change; only their order does). */
int nav_ui_scrolling(void) { return U.tv && lv_obj_is_scrolling(U.tv); }      /* a page slide is in progress (a finger drag or the slide after it) */

int nav_ui_active_page(void)
{
    lv_obj_t *act = lv_tileview_get_tile_active(U.tv);
    for (int i = 0; i < U.nstrip; i++) if (U.slot[i] == act) return U.strip[i];
    return 0;
}

/* A short message in the middle of the screen ("Touch off"), gone after about a second and a half. */
static void toast_show(const char *text, uint32_t ms);

static lv_obj_t *pair_obj, *pair_code_lbl;

void nav_ui_pairing_code(uint32_t code)
{
    if (!U.round) return;
    if (code == 0) {
        if (pair_obj) lv_obj_add_flag(pair_obj, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (!pair_obj) {          /* a card with the title in the regular bold font and the code in the big numerals (that font has digits only) */
        pair_obj = lv_obj_create(lv_screen_active());
        lv_obj_set_size(pair_obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(pair_obj, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(pair_obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_radius(pair_obj, S(36), 0);
        lv_obj_set_style_bg_opa(pair_obj, 245, 0);
        lv_obj_set_style_pad_hor(pair_obj, S(30), 0);
        lv_obj_set_style_pad_ver(pair_obj, S(16), 0);
        lv_obj_set_style_pad_row(pair_obj, S(6), 0);
        lv_obj_set_style_border_width(pair_obj, 3, 0);
        lv_obj_remove_flag(pair_obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(pair_obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *title = lv_label_create(pair_obj);
        lv_obj_set_style_text_font(title, FB26, 0);
        lv_label_set_text(title, "Pairing code");
        pair_code_lbl = lv_label_create(pair_obj);
        lv_obj_set_style_text_font(pair_code_lbl, FT48, 0);
        lv_obj_set_style_text_letter_space(pair_code_lbl, S(5), 0);
    }
    lv_obj_set_style_bg_color(pair_obj, lv_color_hex(TH->bg), 0);
    lv_obj_set_style_border_color(pair_obj, lv_color_hex(TH->accent), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(pair_obj, 0), lv_color_hex(TH->sub), 0);
    lv_obj_set_style_text_color(pair_code_lbl, lv_color_hex(TH->text), 0);
    char b[16];
    snprintf(b, sizeof b, "%06u", (unsigned)(code % 1000000));
    lv_label_set_text(pair_code_lbl, b);
    lv_obj_remove_flag(pair_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(pair_obj, LV_ALIGN_CENTER, 0, 0);
    lv_obj_move_foreground(pair_obj);
}

void nav_ui_set_traffic(const nav_traffic_span_t *spans, int n)
{
    if (n < 0) n = 0;
    if (n > NAV_MAX_TRAFFIC_SPANS) n = NAV_MAX_TRAFFIC_SPANS;
    if (n && spans) memcpy(tf_span, spans, (size_t)n * sizeof tf_span[0]);
    tf_n = n;
    map_dirty = 1;
    lv_obj_invalidate(lv_screen_active());
}

void nav_ui_traffic_alert(int level, uint16_t dist_m, uint16_t length_m)
{
    (void)length_m;
    if (level <= 0) { tf_alert_level = 0; return; }
    if (level <= tf_alert_level) return;                 /* already told about a stretch this bad */
    tf_alert_level = level;
    char d[24], msg[48];
    const char *what = level >= NAV_TRAFFIC_STOPPED ? "Stopped traffic" : "Slow traffic";
    if (dist_m == 0xFFFF || dist_m < 150) snprintf(msg, sizeof msg, "%s\nahead", what);
    else { fmt_dist(d, sizeof d, dist_m); snprintf(msg, sizeof msg, "%s\nin %s", what, d); }
    toast_show(msg, 5000);                               /* long enough to notice with a glance at the road in between */
}

static lv_obj_t *toast_obj;
static lv_timer_t *toast_timer;
static void toast_hide_cb(lv_timer_t *t) { (void)t; if (toast_obj) lv_obj_add_flag(toast_obj, LV_OBJ_FLAG_HIDDEN); toast_timer = NULL; }

static void toast_show(const char *text, uint32_t ms);
void nav_ui_toast(const char *text) { toast_show(text, 1500); }

static void toast_show(const char *text, uint32_t ms)
{
    if (!U.round) return;
    if (!toast_obj) {
        toast_obj = lv_label_create(lv_screen_active());          /* a child of the screen, above everything else (also visible in simulator snapshots) */
        lv_obj_set_style_text_font(toast_obj, FB26, 0);
        lv_obj_set_style_radius(toast_obj, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(toast_obj, 235, 0);
        lv_obj_set_style_pad_hor(toast_obj, S(22), 0);
        lv_obj_set_style_pad_ver(toast_obj, S(10), 0);
        lv_obj_remove_flag(toast_obj, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_set_style_bg_color(toast_obj, lv_color_hex(TH->bg), 0);
    lv_obj_set_style_text_color(toast_obj, lv_color_hex(TH->text), 0);
    lv_obj_set_style_border_width(toast_obj, 2, 0);
    lv_obj_set_style_border_color(toast_obj, lv_color_hex(TH->edge), 0);
    lv_label_set_text(toast_obj, text);
    lv_obj_remove_flag(toast_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(toast_obj, LV_ALIGN_CENTER, 0, 0);
    if (toast_timer) lv_timer_delete(toast_timer);
    toast_timer = lv_timer_create(toast_hide_cb, ms, NULL);
    lv_timer_set_repeat_count(toast_timer, 1);
}

/* The next screen in the strip: it slides over like a swipe. After the last one it jumps back to the first (no long slide back). */
void nav_ui_next_page(void)
{
    lv_obj_t *act = lv_tileview_get_tile_active(U.tv);
    int cur = 0;
    for (int i = 0; i < NPAGES; i++) if (U.slot[i] == act) cur = i;
    int next = (cur + 1) % U.nstrip;
    lv_tileview_set_tile(U.tv, U.slot[next], next == 0 ? LV_ANIM_OFF : LV_ANIM_ON);
    if (next == 0) tile_changed_cb(NULL);          /* no scroll-end event without the slide */
}

void nav_ui_show_page(int page)
{
    if (page < 0 || page >= NPAGES) page = 0;
    lv_obj_update_layout(U.tv);
    int found = 0;
    for (int i = 0; i < U.nstrip; i++)
        if (U.strip[i] == page) { lv_tileview_set_tile(U.tv, U.slot[i], LV_ANIM_OFF); found = 1; break; }
    if (!found) lv_tileview_set_tile(U.tv, U.slot[0], LV_ANIM_OFF);               /* a screen that is not in the strip (tires, while monitoring is off): the first one */
    tile_changed_cb(NULL);
}

/* Whether a screen is in the swipe strip: not turned off on the phone, and for the tires also monitoring on. */
static int page_in_strip(uint8_t id)
{
    if (U.off_mask & (1u << id)) return 0;
    return id != NAV_PAGE_TPMS || U.tires_enabled;
}

/* The page indicator: one round dot per screen, spaced evenly along an arc near the bottom edge so the row follows the display's circle. The first screen
 * is leftmost. */
#define DOT_R     193.0f           /* design px from the center to the middle of the dots */
#define DOT_SIZE  7                /* design px */
#define DOT_PITCH 14.0f            /* along the arc, design px */
static void dot_place(int i, int n)
{
    const float th = ((float)i - (float)(n - 1) / 2.0f) * DOT_PITCH / DOT_R;
    const float x = SF(206.0f + DOT_R * sinf(th) - DOT_SIZE / 2.0f), y = SF(206.0f + DOT_R * cosf(th) - DOT_SIZE / 2.0f);
    lv_obj_set_pos(U.dot[i], (lv_coord_t)lroundf(x), (lv_coord_t)lroundf(y));
}

/* Puts the screens in the slots: the phone's order, minus screens turned off and the tire screen while monitoring is off (they wait, hidden, after the last slot).
 * The first screen can only be swiped left and the last only right; the dots follow. The showing screen stays on (or the first is shown if the tire screen just went away). */
static void strip_apply(void)
{
    const int showing = U.nstrip ? nav_ui_active_page() : 0;
    int n = 0;
    for (int i = 0; i < NPAGES; i++) if (page_in_strip(U.order[i])) U.strip[n++] = U.order[i];
    if (n == 0) U.strip[n++] = NAV_PAGE_MAP;                                    /* never an empty strip */
    U.nstrip = n;
    for (int i = n; i < NPAGES; i++) {                                          /* the parked ones: every screen not in the strip */
        for (int id = 0; id < NPAGES; id++) {
            int used = 0;
            for (int k = 0; k < i; k++) if (U.strip[k] == id) used = 1;
            if (!used) { U.strip[i] = (uint8_t)id; break; }
        }
    }
    for (int i = 0; i < NPAGES; i++) {
        lv_obj_t *pg = page_obj(U.strip[i]);
        lv_obj_set_parent(pg, U.slot[i]);
        lv_obj_set_pos(pg, 0, 0);
        lv_tileview_tile_t *tile = (lv_tileview_tile_t *)U.slot[i];
        if (i >= n) { tile->dir = LV_DIR_NONE; lv_obj_add_flag(U.slot[i], LV_OBJ_FLAG_HIDDEN); }
        else {
            lv_obj_remove_flag(U.slot[i], LV_OBJ_FLAG_HIDDEN);
            tile->dir = n == 1 ? LV_DIR_NONE : i == 0 ? LV_DIR_RIGHT : i == n - 1 ? LV_DIR_LEFT : LV_DIR_HOR;
        }
    }
    for (int i = 0; i < NPAGES; i++) {
        if (i < n) { lv_obj_remove_flag(U.dot[i], LV_OBJ_FLAG_HIDDEN); dot_place(i, n); }
        else lv_obj_add_flag(U.dot[i], LV_OBJ_FLAG_HIDDEN);
    }
    nav_ui_show_page(showing);
}

void nav_ui_set_tires_enabled(int enabled)
{
    enabled = enabled ? 1 : 0;
    if (enabled == U.tires_enabled) return;
    U.tires_enabled = enabled;
    if (U.tv) strip_apply();
}

void nav_ui_set_pages_off(uint8_t mask)
{
    mask &= (uint8_t)((1u << NPAGES) - 1);
    if (mask == U.off_mask) return;
    U.off_mask = mask;
    if (U.tv) strip_apply();
}

/* Reorder the screens: ids[i] is the id of the i-th screen in the strip; must be a permutation of 0..5, otherwise ignored. The showing screen stays on. */
int nav_ui_set_page_order(const uint8_t *ids, int n)
{
    if (n != NPAGES) return 0;
    uint8_t seen = 0;
    for (int i = 0; i < NPAGES; i++) { if (ids[i] >= NPAGES || (seen & (1u << ids[i]))) return 0; seen |= (uint8_t)(1u << ids[i]); }
    for (int i = 0; i < NPAGES; i++) U.order[i] = ids[i];
    strip_apply();
    return 1;
}

void nav_ui_get_page_order(uint8_t out[NPAGES]) { for (int i = 0; i < NPAGES; i++) out[i] = U.order[i]; }

int nav_ui_frame_origin(int32_t *lat, int32_t *lon)
{
    if (!U.have_origin) return 0;
    *lat = U.olat;
    *lon = U.olon;
    return 1;
}

void nav_ui_set_roads(const nav_seg_t *s, uint16_t n) { U.roads = s; U.nroads = n; map_dirty = 1; }
void nav_ui_set_areas(const nav_area_t *a, uint16_t n) { U.areas = a; U.nareas = n; map_dirty = 1; }
void nav_ui_set_polys(const nav_poly_t *p, uint16_t n) { U.polys = p; U.npolys = n; map_dirty = 1; }

static void tile_changed_cb(lv_event_t *e)
{
    (void)e;
    clock_refresh();                 /* the page just swiped to may have missed updates while it was off screen */
    refresh_trip_labels();
    cmp_refresh();
    refresh_weather();               /* (does nothing until the first weather packet has arrived) */
    lv_obj_t *act = lv_tileview_get_tile_active(U.tv);
    for (int i = 0; i < U.nstrip; i++)
        lv_obj_set_style_bg_color(U.dot[i], lv_color_hex(act == U.slot[i] ? TH->text : TH->edge), 0);
}

#ifdef NAV_UI_ROUND_MASK
static void mask_draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.color = lv_color_hex(0x000000);
    d.opa = LV_OPA_COVER;
    d.start_angle = 0;
    d.end_angle = 360;
    d.center.x = a.x1 + SZ / 2;
    d.center.y = a.y1 + SZ / 2;
    d.width = 110;
    d.radius = SZ / 2 + 110;
    lv_draw_arc(lv_event_get_layer(e), &d);
}
#endif

static void trip_reset_cb(lv_event_t *e)
{
    (void)e;
    nav_ui_trip_reset();
}

/* ---- build ------------------------------------------------------------ */
/* ---- splash --------------------------------------------------------------------------------- */
extern const lv_image_dsc_t nav_splash_logo;          /* the logo's shape as an 8-bit mask (assets/make_splash_mask.py) */
extern const int nav_splash_logo_x, nav_splash_logo_y;

static lv_obj_t *splash_img;
static int splash_held;

#define SPLASH_START_SCALE 24          /* 256 = full size: the pop starts from a speck */
#define SPLASH_POP_MS 600
static void splash_scale_cb(void *obj, int32_t v) { lv_image_set_scale((lv_obj_t *)obj, (uint32_t)v); }

/* The start-up screen is the theme's background with the logo in the theme's logo color. */
static void splash_restyle(void)
{
    if (!U.splash || !splash_img) return;
    lv_obj_set_style_bg_color(U.splash, lv_color_hex(TH->bg), 0);
    lv_obj_set_style_image_recolor(splash_img, lv_color_hex(TH->logo), 0);
    lv_obj_set_style_image_recolor_opa(splash_img, LV_OPA_COVER, 0);
}

/* Grows the logo from the centre with a little overshoot using the image's own scale (a style transform would need a full-screen layer this board cannot afford); only the logo's rectangle is redrawn each frame. */
static void splash_start_pop(void)
{
    if (!splash_img) return;
    lv_anim_delete(splash_img, splash_scale_cb);
    lv_image_set_scale(splash_img, SPLASH_START_SCALE);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, splash_img);
    lv_anim_set_exec_cb(&a, splash_scale_cb);
    lv_anim_set_values(&a, SPLASH_START_SCALE, 256);
    lv_anim_set_duration(&a, SPLASH_POP_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_start(&a);
}

void nav_ui_splash_hold(int hold) { splash_held = hold; }
void nav_ui_splash_pop(void) { splash_held = 0; splash_start_pop(); }

/* Full-screen start-up screen on top of everything, shown while the phone is not connected. No corner clipping, opacity, or fading: they make LVGL allocate a 339 KB layer that does not fit the 64 KB pool and hangs the draw loop. */
void nav_ui_splash_show(int show)
{
    if (!U.splash) {
        if (!show) return;
        lv_obj_t *screen = lv_obj_get_screen(U.round);
        lv_obj_t *sp = lv_obj_create(screen);
        lv_obj_remove_style_all(sp);
        lv_obj_set_size(sp, SZ, SZ);
        lv_obj_set_pos(sp, 0, 0);
        lv_obj_set_style_bg_opa(sp, LV_OPA_COVER, 0);
        lv_obj_add_flag(sp, LV_OBJ_FLAG_CLICKABLE);      /* swallow touches while it is up */
        lv_obj_t *im = lv_image_create(sp);
        lv_image_set_src(im, &nav_splash_logo);
        lv_obj_set_size(im, nav_splash_logo.header.w, nav_splash_logo.header.h);       /* a fixed size, so scaling grows the logo around its centre */
        lv_image_set_pivot(im, nav_splash_logo.header.w / 2, nav_splash_logo.header.h / 2);
        lv_image_set_antialias(im, true);                 /* smooth edges while the logo is scaled */
        lv_obj_set_pos(im, nav_splash_logo_x, nav_splash_logo_y + S(5));      /* nudged down 5 px (design) to sit centred in the bezel */
        U.splash = sp;
        splash_img = im;
        splash_restyle();
        if (splash_held) lv_image_set_scale(im, SPLASH_START_SCALE);       /* waits, as a speck, for nav_ui_splash_pop() */
        else splash_start_pop();
        return;
    }
    if (show) {
        splash_restyle();
        lv_obj_remove_flag(U.splash, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(U.splash);
        if (!splash_held) splash_start_pop();               /* the logo pops out each time it comes back */
    }
    else lv_obj_add_flag(U.splash, LV_OBJ_FLAG_HIDDEN);
}

/* ---- tires (TPMS) ----
 * The tire page shows the phone's front and rear pressure (nav_tpms_t). While either low flag is set a thick red ring pulses around whatever screen is up:
 * one panel-sized object above the pages that draws nothing unless pulsing, blending a pre-made coverage mask at the pulse opacity and invalidating only the ring's own stretch (about twenty rectangles) each step. */
#define RING_W      40                 /* design px */
#define RING_SOFT   9                  /* design px of fade on the inner edge */
#define RING_MAXRECT 28
#define TM_MAX_X10  500                /* a meter runs from 0 to the pressure the tire should have (the SET value); 50 psi if none is set */
#define TM_BLUE     0x60A5FA           /* the pressure fill: the blue of the rain on the weather screen, in every theme (the accent is red or gold in some) */
#define TM_W        60                 /* a meter's width, design px */
#define TM_TOP      152                /* top of the meters and their height */
#define TM_H        104
static const int tm_cx[2] = { 136, 276 };          /* the centers of the front and rear meters */
static struct {
    nav_tpms_t t;
    int have;                          /* a packet has arrived and is not stale */
    float age;                         /* seconds since the last packet */
    lv_obj_t *cap[2], *val[2], *temp[2], *sub[2];     /* 0 front, 1 rear */
    lv_obj_t *psi;                                      /* the unit, centered between the two meters, underneath them */
    lv_obj_t *track[2], *fill[2];                       /* the vertical meters: the empty tube and the pressure */
    lv_obj_t *lim_tick[2][2], *lim_lbl[2];              /* the red line at the alert pressure, and its psi */
    char lim_s[2][8];
    char cap_s[2][16], val_s[2][12], temp_s[2][12], sub_s[2][44];
    lv_obj_t *ring;
    uint8_t *mask;                     /* SZ x SZ coverage of the ring */
    uint16_t *span;                    /* per row: first run start, end, second run start, end (start > end = no run) */
    lv_area_t rect[RING_MAXRECT];      /* the ring's stretch of the screen, as rectangles relative to the ring object */
    int nrect;
    int alpha;                         /* the pulse's opacity now, 0 = nothing drawn */
} TP;

static uint32_t alarm_red(void) { return TH->light ? 0xD01212 : 0xFF3B30; }

static void tpms_set_text(lv_obj_t *l, char *cache, size_t cap, const char *txt)
{
    if (strcmp(cache, txt) == 0) return;
    size_t n = strlen(txt);
    if (n >= cap) n = cap - 1;                    /* (the text is always shorter; this only keeps the compiler and a future edit honest) */
    memcpy(cache, txt, n);
    cache[n] = 0;
    lv_label_set_text(l, txt);
}

static void tpms_refresh(void)
{
    if (!TP.cap[0]) return;
    for (int i = 0; i < 2; i++) {
        const unsigned x10 = i ? TP.t.rear_x10 : TP.t.front_x10, lim = i ? TP.t.rear_low_x10 : TP.t.front_low_x10, set = i ? TP.t.rear_set_x10 : TP.t.front_set_x10;
        const unsigned temp = i ? TP.t.rear_temp_f : TP.t.front_temp_f;
        const int valid = TP.have && (TP.t.flags & (i ? NAV_TPMS_REAR_OK : NAV_TPMS_FRONT_OK)) && x10 != NAV_TPMS_NONE;
        const int low = TP.have && (TP.t.flags & (i ? NAV_TPMS_REAR_LOW : NAV_TPMS_FRONT_LOW));
        const int batt = TP.have && (TP.t.flags & (i ? NAV_TPMS_REAR_BATT : NAV_TPMS_FRONT_BATT));
        const char *name = i ? "REAR" : "FRONT";
        char b[48];
        if (low) snprintf(b, sizeof b, "%s LOW", name);
        else snprintf(b, sizeof b, "%s", name);
        tpms_set_text(TP.cap[i], TP.cap_s[i], sizeof TP.cap_s[i], b);
        if (valid && temp != 0xFF) snprintf(b, sizeof b, "%u\xC2\xB0""F", temp); else b[0] = 0;
        tpms_set_text(TP.temp[i], TP.temp_s[i], sizeof TP.temp_s[i], b);
        if (valid) snprintf(b, sizeof b, "%u.%u", x10 / 10, x10 % 10); else snprintf(b, sizeof b, "--");
        tpms_set_text(TP.val[i], TP.val_s[i], sizeof TP.val_s[i], b);
        if (batt) snprintf(b, sizeof b, "BATTERY LOW");
        else if (!valid) snprintf(b, sizeof b, "NO DATA");
        else if (set) snprintf(b, sizeof b, "SET %u", (set + 5) / 10);
        else b[0] = 0;
        tpms_set_text(TP.sub[i], TP.sub_s[i], sizeof TP.sub_s[i], b);
        const uint32_t red = alarm_red();
        /* the meter: the fill is the pressure, scaled so the tire's set pressure fills it; the red line is the alert pressure */
        const unsigned full = set ? set : TM_MAX_X10;                             /* the pressure that fills the meter */
        const unsigned shown = valid ? (x10 > full ? full : x10) : 0;
        const int fh = (int)((shown * (unsigned)TM_H + full / 2) / full);
        lv_obj_set_style_bg_color(TP.track[i], lv_color_hex(TH->div), 0);
        if (fh > 0) {
            lv_obj_remove_flag(TP.fill[i], LV_OBJ_FLAG_HIDDEN);
            pos(TP.fill[i], tm_cx[i] - TM_W / 2, TM_TOP + TM_H - fh);
            lv_obj_set_height(TP.fill[i], S(fh));
            lv_obj_set_style_bg_color(TP.fill[i], lv_color_hex(low ? red : TM_BLUE), 0);
        } else lv_obj_add_flag(TP.fill[i], LV_OBJ_FLAG_HIDDEN);
        if (lim) {                                                                  /* the alert pressure: a red line across the meter and its psi beside it */
            const unsigned lv = lim > full ? full : lim;
            const int ly = TM_TOP + TM_H - (int)((lv * (unsigned)TM_H + full / 2) / full);
            for (int side = 0; side < 2; side++) {
                lv_obj_remove_flag(TP.lim_tick[i][side], LV_OBJ_FLAG_HIDDEN);
                pos(TP.lim_tick[i][side], side ? tm_cx[i] : tm_cx[i] - TM_W / 2, ly - 1);
                lv_obj_set_style_bg_color(TP.lim_tick[i][side], lv_color_hex(red), 0);
            }
            if (lim % 10) snprintf(b, sizeof b, "%u.%u", lim / 10, lim % 10); else snprintf(b, sizeof b, "%u", lim / 10);
            tpms_set_text(TP.lim_lbl[i], TP.lim_s[i], sizeof TP.lim_s[i], b);
            lv_obj_remove_flag(TP.lim_lbl[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_y(TP.lim_lbl[i], S(ly - 12));
            lv_obj_set_style_text_color(TP.lim_lbl[i], lv_color_hex(red), 0);
        } else {
            for (int side = 0; side < 2; side++) lv_obj_add_flag(TP.lim_tick[i][side], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(TP.lim_lbl[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_style_text_color(TP.cap[i], lv_color_hex(low ? red : TH->sub), 0);
        lv_obj_set_style_text_color(TP.val[i], lv_color_hex(low ? red : valid ? TH->text : TH->dim), 0);
        lv_obj_set_style_text_color(TP.sub[i], lv_color_hex(batt ? red : TH->sub), 0);
        lv_obj_set_style_text_color(TP.temp[i], lv_color_hex(TH->sub), 0);
        lv_obj_set_style_text_color(TP.psi, lv_color_hex(TH->sub), 0);
    }
}

/* The ring's coverage mask and the row runs and rectangles that go with it. Built once, in PSRAM (the mask is a panel-sized A8 picture). */
static void ring_build(void)
{
    const size_t mbytes = (size_t)SZ * SZ, sbytes = (size_t)SZ * 4 * sizeof(uint16_t);
    uint8_t *mem = (uint8_t *)(nav_ui_img_alloc ? nav_ui_img_alloc(mbytes + sbytes) : malloc(mbytes + sbytes));
    if (!mem) return;
    TP.mask = mem;
    TP.span = (uint16_t *)(void *)(mem + mbytes);
    const float c = SZ * 0.5f, rout = SZ * 0.5f + 1.5f, rin = SZ * 0.5f - (float)S(RING_W), soft = (float)S(RING_SOFT);
    for (int y = 0; y < SZ; y++) {
        uint8_t *row = TP.mask + (size_t)y * SZ;
        const float dy = y + 0.5f - c;
        int runs = 0, in = 0;
        uint16_t *sp = TP.span + (size_t)y * 4;
        sp[0] = sp[2] = 1; sp[1] = sp[3] = 0;
        for (int x = 0; x < SZ; x++) {
            const float dx = x + 0.5f - c, d = sqrtf(dx * dx + dy * dy);
            float cin = (d - rin) / soft, cout = rout - d + 0.5f;
            cin = cin < 0 ? 0 : cin > 1 ? 1 : cin;
            cout = cout < 0 ? 0 : cout > 1 ? 1 : cout;
            const float cv = cin * cin * (3.0f - 2.0f * cin) * cout;           /* smoothstep: the inner edge fades like a glow */
            const uint8_t m = (uint8_t)(cv * 255.0f + 0.5f);
            row[x] = m;
            if (m && !in) { in = 1; if (runs < 2) sp[runs * 2] = (uint16_t)x; }
            if (m) { if (runs < 2) sp[runs * 2 + 1] = (uint16_t)x; }
            if (!m && in) { in = 0; runs++; }
        }
    }
    /* Rectangles: the rows that have a single run (top and bottom) are one rectangle each; the rest are cut into bands, each with a left and a right piece. */
    int top = 0;
    while (top < SZ && TP.span[(size_t)top * 4 + 2] > TP.span[(size_t)top * 4 + 3]) top++;       /* rows before the first one with two runs */
    int n = 0;
    if (top > 0 && top < SZ / 2) {
        int32_t x1 = SZ, x2 = 0;
        for (int y = 0; y < top; y++) { const uint16_t *sp = TP.span + (size_t)y * 4; if (sp[0] < x1) x1 = sp[0]; if (sp[1] > x2) x2 = sp[1]; }
        TP.rect[n++] = (lv_area_t){ x1, 0, x2, top - 1 };
        x1 = SZ; x2 = 0;
        for (int y = SZ - top; y < SZ; y++) { const uint16_t *sp = TP.span + (size_t)y * 4; if (sp[0] < x1) x1 = sp[0]; if (sp[1] > x2) x2 = sp[1]; }
        TP.rect[n++] = (lv_area_t){ x1, SZ - top, x2, SZ - 1 };
    }
    const int mid0 = top, mid1 = SZ - 1 - top, bands = 10;
    for (int b = 0; b < bands && mid1 > mid0; b++) {
        const int y1 = mid0 + (mid1 - mid0 + 1) * b / bands, y2 = mid0 + (mid1 - mid0 + 1) * (b + 1) / bands - 1;
        int32_t lx1 = SZ, lx2 = 0, rx1 = SZ, rx2 = 0;
        for (int y = y1; y <= y2; y++) {
            const uint16_t *sp = TP.span + (size_t)y * 4;
            if (sp[0] <= sp[1]) { if (sp[0] < lx1) lx1 = sp[0]; if (sp[1] > lx2) lx2 = sp[1]; }
            if (sp[2] <= sp[3]) { if (sp[2] < rx1) rx1 = sp[2]; if (sp[3] > rx2) rx2 = sp[3]; }
        }
        if (lx1 <= lx2 && n < RING_MAXRECT) TP.rect[n++] = (lv_area_t){ lx1, y1, lx2, y2 };
        if (rx1 <= rx2 && n < RING_MAXRECT) TP.rect[n++] = (lv_area_t){ rx1, y1, rx2, y2 };
    }
    TP.nrect = n;
}

static void ring_invalidate(void)
{
    if (!TP.ring) return;
    lv_area_t oa;
    lv_obj_get_coords(TP.ring, &oa);
    for (int i = 0; i < TP.nrect; i++) {
        lv_area_t a = { oa.x1 + TP.rect[i].x1, oa.y1 + TP.rect[i].y1, oa.x1 + TP.rect[i].x2, oa.y1 + TP.rect[i].y2 };
        lv_obj_invalidate_area(TP.ring, &a);
    }
}

static void ring_draw_cb(lv_event_t *e)
{
    if (!TP.alpha || !TP.mask) return;
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t oa;
    lv_obj_get_coords(o, &oa);
    const lv_area_t *c = &layer->_clip_area;
    const int32_t x1 = c->x1 > oa.x1 ? c->x1 : oa.x1, x2 = c->x2 < oa.x2 ? c->x2 : oa.x2;
    const int32_t y1 = c->y1 > oa.y1 ? c->y1 : oa.y1, y2 = c->y2 < oa.y2 ? c->y2 : oa.y2;
    if (x1 > x2 || y1 > y2) return;
    const int bpp = layer_bpp(layer, x1, y1, x2, y2);
    if (!bpp) return;
    const uint32_t dstride = layer->draw_buf->header.stride;
    uint8_t *base = (uint8_t *)lv_draw_layer_go_to_xy(layer, x1 - layer->buf_area.x1, y1 - layer->buf_area.y1);
    if (!base) return;
    const lv_color_t col = lv_color_hex(alarm_red());
    const uint32_t alpha = (uint32_t)TP.alpha;
    const int32_t lo = x1 - oa.x1, hi = x2 - oa.x1;
    for (int32_t y = y1; y <= y2; y++) {
        const int32_t row = y - oa.y1;
        const uint16_t *sp = TP.span + (size_t)row * 4;
        const uint8_t *m = TP.mask + (size_t)row * SZ;
        uint8_t *d = base + (size_t)(y - y1) * dstride - (size_t)lo * bpp;
        for (int run = 0; run < 2; run++) {
            int32_t a = sp[run * 2], b = sp[run * 2 + 1];
            if (a > b) continue;
            if (a < lo) a = lo;
            if (b > hi) b = hi;
            for (int32_t x = a; x <= b; x++) blend_px(d + (size_t)x * bpp, bpp, col.red, col.green, col.blue, div255(m[x] * alpha));
        }
    }
}

/* Called every frame: ages the data, and steps the pulse (about 1 a second, steps of 1/16 of the opacity so the ring is only redrawn when it visibly changes). */
static void tpms_tick(float dt, uint32_t now)
{
    if (TP.have) {
        TP.age += dt;
        if (TP.age > NAV_TPMS_STALE_S) { TP.have = 0; tpms_refresh(); }
    }
    int target = 0;
    if (TP.have && U.tires_enabled && (TP.t.flags & (NAV_TPMS_FRONT_LOW | NAV_TPMS_REAR_LOW)) && TP.mask) {
        const float ph = (float)(now % 1100u) / 1100.0f;
        const float k = 0.5f - 0.5f * cosf(ph * 2.0f * PI_F);            /* 0 .. 1 .. 0 */
        target = (int)(48.0f + 207.0f * k) & ~15;
        if (target < 32) target = 32;
    }
    if (target != TP.alpha) {
        TP.alpha = target;
        ring_invalidate();
    }
}

void nav_ui_set_tpms(const nav_tpms_t *t)
{
    nav_ui_set_tires_enabled((t->flags & NAV_TPMS_ENABLED) != 0);
    TP.t = *t;
    TP.have = 1;
    TP.age = 0;
    tpms_refresh();
}

void nav_ui_init(lv_obj_t *screen)
{
    memset(&U, 0, sizeof(U));
    memset(&TP, 0, sizeof(TP));
    U.bat_pct = -1;
    U.scale = U.scale_tgt = 0.4f * MB;
    U.zoom_log = logf(U.scale);
    U.bar_idx = -1;
    gl_render(NAV_MAN_STRAIGHT);
    marker_render();
    U.hsin = 0.0f;
    U.hcos = 1.0f;
    nav_trip_load(&U.trip);          /* pick up where the last ride left off (if storage is set up) */
    U.last_tick = lv_tick_get();

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* full-panel container; the real LCD is physically round */
    U.round = plain(screen, 0, 0, DSZ, DSZ);
    lv_obj_set_style_bg_color(U.round, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(U.round, LV_OPA_COVER, 0);

    U.tv = lv_tileview_create(U.round);
    lv_obj_set_size(U.tv, SZ, SZ);
    lv_obj_set_pos(U.tv, 0, 0);
    lv_obj_set_style_bg_opa(U.tv, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(U.tv, 0, 0);
    lv_obj_set_style_pad_all(U.tv, 0, 0);
    lv_obj_set_scrollbar_mode(U.tv, LV_SCROLLBAR_MODE_OFF);
    /* The swipe strip is a row of fixed slots; each screen is a container in one of them, and reordering just moves containers between slots. */
    const lv_dir_t dirs[NPAGES] = {LV_DIR_RIGHT, LV_DIR_HOR, LV_DIR_HOR, LV_DIR_HOR, LV_DIR_HOR, LV_DIR_LEFT};
    for (int i = 0; i < NPAGES; i++) {
        U.slot[i] = lv_tileview_add_tile(U.tv, i, 0, dirs[i]);
        U.order[i] = U.strip[i] = (uint8_t)i;
        lv_obj_set_style_bg_opa(U.slot[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(U.slot[i], 0, 0);
        lv_obj_set_style_pad_all(U.slot[i], 0, 0);
    }
    U.tile_nav = plain(U.slot[0], 0, 0, DSZ, DSZ);
    U.tile_wx = plain(U.slot[1], 0, 0, DSZ, DSZ);
    U.tile_trip = plain(U.slot[2], 0, 0, DSZ, DSZ);
    U.tile_cmp = plain(U.slot[3], 0, 0, DSZ, DSZ);
    U.tile_clk = plain(U.slot[4], 0, 0, DSZ, DSZ);
    U.tile_tpms = plain(U.slot[5], 0, 0, DSZ, DSZ);
    U.nstrip = NPAGES;

    /* ---- navigation page ---- */
    lv_obj_t *p = U.tile_nav;

    U.arrow = plain(p, ARROW_X, ARROW_Y, ARROW_S, ARROW_S);
    lv_obj_add_event_cb(U.arrow, arrow_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    U.dist = label(p, FT48, C_TEXT);
    pos(U.dist, DIST_X, DIST_Y);
    lv_label_set_text(U.dist, "--");

    U.street = label(p, FB22, 0xE5EAF3);
    pos(U.street, STREET_X, STREET_Y);
    wid(U.street, STREET_W);
    lv_label_set_long_mode(U.street, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(U.street, "");

    U.map = plain(p, 0, MAP_Y, DSZ, MAP_H);
    lv_obj_set_style_bg_color(U.map, lv_color_hex(C_MAP), 0);
    lv_obj_set_style_bg_opa(U.map, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(U.map, map_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    U.bar_lbl = label(U.map, FT14, 0xD6DCE8);
    pos(U.bar_lbl, DSZ - 58 - 62 + 6, 270 - 22);
    lv_label_set_text(U.bar_lbl, "");

    /* the turn info floats over the map on a translucent panel */
    lv_obj_t *fade = plain(p, 0, 0, DSZ, 140);
    lv_obj_add_event_cb(fade, fade_draw_cb, LV_EVENT_DRAW_MAIN, (void *)(intptr_t)0);
    lv_obj_move_foreground(fade);
    lv_obj_move_foreground(U.arrow);
    lv_obj_move_foreground(U.dist);
    lv_obj_move_foreground(U.street);

    /* battery level, top centre of the map, on a small translucent pill so it stays readable over streets; hidden until a percentage arrives */
    U.bat_pill = plain(p, 143, 6, 126, 26);
    lv_obj_set_style_radius(U.bat_pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(U.bat_pill, 215, 0);
    lv_obj_set_style_bg_color(U.bat_pill, lv_color_hex(TH->bg), 0);
    U.bat_icon = plain(U.bat_pill, 12, 4, 38, 18);
    lv_obj_set_style_radius(U.bat_icon, S(4), 0);
    lv_obj_set_style_border_width(U.bat_icon, 2, 0);
    lv_obj_set_style_border_color(U.bat_icon, lv_color_hex(TH->sub), 0);
    lv_obj_set_style_border_opa(U.bat_icon, LV_OPA_COVER, 0);
    U.bat_nub = plain(U.bat_pill, 51, 10, 4, 6);
    lv_obj_set_style_radius(U.bat_nub, 1, 0);
    lv_obj_set_style_bg_opa(U.bat_nub, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(U.bat_nub, lv_color_hex(TH->sub), 0);
    U.bat_fill = plain(U.bat_pill, 16, 8, 30, 10);
    lv_obj_set_style_radius(U.bat_fill, S(2), 0);
    lv_obj_set_style_bg_opa(U.bat_fill, LV_OPA_COVER, 0);
    U.bat_bolt = label(U.bat_pill, FT14, C_TEXT);        /* the charging bolt, drawn through the battery */
    wid(U.bat_bolt, 38);
    pos(U.bat_bolt, 12, 5);
    lv_obj_set_style_text_align(U.bat_bolt, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(U.bat_bolt, LV_SYMBOL_CHARGE);
    lv_obj_add_flag(U.bat_bolt, LV_OBJ_FLAG_HIDDEN);
    U.bat_txt = label(U.bat_pill, FT20, C_TEXT);
    wid(U.bat_txt, 62);
    pos(U.bat_txt, 62, 2);
    lv_label_set_text(U.bat_txt, "");
    lv_obj_add_flag(U.bat_pill, LV_OBJ_FLAG_HIDDEN);

    /* ... and so do remaining distance / ETA, at the bottom */
    lv_obj_t *fade_b = plain(p, 0, DSZ - 130, DSZ, 130);
    lv_obj_add_event_cb(fade_b, fade_draw_cb, LV_EVENT_DRAW_MAIN, (void *)(intptr_t)1);

    lv_obj_t *div = plain(p, 206, STAT_Y + 3, 1, 26);
    U.stat_div = div;
    lv_obj_set_style_bg_color(div, lv_color_hex(TH->div), 0);
    theme_reg(div, ROLE_DIV);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);

    U.rem_val = label(p, FT28, C_TEXT);
    wid(U.rem_val, 118);
    pos(U.rem_val, 86, STAT_Y);
    lv_obj_set_style_text_align(U.rem_val, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(U.rem_val, "--");

    U.eta_val = label(p, FT28, C_TEXT);
    wid(U.eta_val, 118);
    pos(U.eta_val, 209, STAT_Y);
    lv_obj_set_style_text_align(U.eta_val, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(U.eta_val, "--");

    /* free-ride bottom row: a big number over a small caption in each of two columns (hidden while navigating) */
    U.cap_l = label(p, FT20, C_SUB);
    U.cap_r = label(p, FT20, C_SUB);
    lv_obj_t *caps[2] = {U.cap_l, U.cap_r};
    for (int i = 0; i < 2; i++) {
        wid(caps[i], 100);
        lv_obj_set_style_text_align(caps[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_add_flag(caps[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(U.cap_l, "mph");
    lv_label_set_text(U.cap_r, "temp");

    /* ---- weather page ---- */
    lv_obj_t *wp = U.tile_wx;
    U.pw_icon = plain(wp, 74, 54, 92, 92);
    lv_obj_add_event_cb(U.pw_icon, wx_icon_draw_cb, LV_EVENT_DRAW_MAIN, (void *)(intptr_t)2);
    U.pw_temp = label(wp, FT48, C_TEXT);
    pos(U.pw_temp, 180, 62);
    lv_label_set_text(U.pw_temp, "--");
    U.pw_cond = label(wp, FT20, C_SUB);
    pos(U.pw_cond, 182, 116);
    wid(U.pw_cond, 190);
    lv_label_set_long_mode(U.pw_cond, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(U.pw_cond, "");
    U.pw_row = label(wp, FT20, C_SUB);
    wid(U.pw_row, 320);
    pos(U.pw_row, 46, 152);
    lv_obj_set_style_text_align(U.pw_row, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(U.pw_row, "");

    U.pw_wind = plain(wp, 84, 190, 30, 30);
    lv_obj_add_event_cb(U.pw_wind, wx_icon_draw_cb, LV_EVENT_DRAW_MAIN, (void *)(intptr_t)3);
    U.pw_wind_val = label(wp, FT28, C_TEXT);
    pos(U.pw_wind_val, 122, 186);
    lv_label_set_text(U.pw_wind_val, "");
    U.pw_wind_sub = label(wp, FT14, C_SUB);
    pos(U.pw_wind_sub, 84, 222);
    lv_label_set_text(U.pw_wind_sub, "");
    U.pw_rain = label(wp, FT28, C_TEXT);
    pos(U.pw_rain, 276, 186);
    lv_label_set_text(U.pw_rain, "");
    U.pw_rain_cap = label(wp, FT14, C_SUB);
    pos(U.pw_rain_cap, 276, 222);
    lv_label_set_text(U.pw_rain_cap, "rain, next hour");

    lv_obj_t *wdiv = plain(wp, 70, 250, 272, 1);
    lv_obj_set_style_bg_color(wdiv, lv_color_hex(TH->div), 0);
    theme_reg(wdiv, ROLE_DIV);
    lv_obj_set_style_bg_opa(wdiv, LV_OPA_COVER, 0);

    static const int hx[4] = {98, 170, 242, 314};
    for (int i = 0; i < 4; i++) {
        U.hr_time[i] = label(wp, FT14, C_SUB);
        wid(U.hr_time[i], 68);
        pos(U.hr_time[i], hx[i] - 34, 258);
        lv_obj_set_style_text_align(U.hr_time[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(U.hr_time[i], "");
        U.hr_icon[i] = plain(wp, hx[i] - 19, 278, 38, 38);
        lv_obj_add_event_cb(U.hr_icon[i], wx_icon_draw_cb, LV_EVENT_DRAW_MAIN, (void *)(intptr_t)(10 + i));
        U.hr_temp[i] = label(wp, FT20, C_TEXT);
        wid(U.hr_temp[i], 68);
        pos(U.hr_temp[i], hx[i] - 34, 320);
        lv_obj_set_style_text_align(U.hr_temp[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(U.hr_temp[i], "");
        U.hr_pop[i] = label(wp, FT14, 0x5A667A);
        wid(U.hr_pop[i], 68);
        pos(U.hr_pop[i], hx[i] - 34, 346);
        lv_obj_set_style_text_align(U.hr_pop[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(U.hr_pop[i], "");
    }

    /* ---- trip page ---- */
    lv_obj_t *tp = U.tile_trip;
    lv_obj_t *tt_ = label(tp, FT20, C_SUB);
    wid(tt_, 200);
    pos(tt_, 106, 62);
    lv_obj_set_style_text_align(tt_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(tt_, "TRIP");
    static const char *cap[6] = {"Distance", "Moving time", "Avg speed", "Max speed", "Total time", "Stopped"};
    for (int i = 0; i < 6; i++) {
        int col = i % 2, row = i / 2;
        int x = col ? 212 : 80, y = 104 + row * 76;
        lv_obj_t *c = label(tp, FT14, C_SUB);
        wid(c, 120);
        pos(c, x, y);
        lv_obj_set_style_text_align(c, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(c, cap[i]);
        U.trip_val[i] = label(tp, FT28, C_TEXT);
        wid(U.trip_val[i], 130);
        pos(U.trip_val[i], x - 5, y + 20);
        lv_obj_set_style_text_align(U.trip_val[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(U.trip_val[i], "--");
    }
    lv_obj_t *hint = label(tp, FT14, 0x5A667A);
    wid(hint, 200);
    pos(hint, 106, 342);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(hint, "hold to reset");
    lv_obj_add_event_cb(tp, trip_reset_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_flag(tp, LV_OBJ_FLAG_CLICKABLE);

    /* ---- compass page ---- */
    {
        lv_obj_t *cp = U.tile_cmp;
        U.cmp_obj = plain(cp, 0, 0, DSZ, DSZ);
        lv_obj_add_event_cb(U.cmp_obj, cmp_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
        static const char *const cd_text[4] = {"N", "E", "S", "W"};
        for (int i = 0; i < 4; i++) {
            U.cmp_cd[i] = label(cp, FB44, C_TEXT);
            lv_label_set_text(U.cmp_cd[i], cd_text[i]);
        }
        static const char *const ic_text[4] = {"NE", "SE", "SW", "NW"};
        for (int i = 0; i < 4; i++) {
            U.cmp_ic[i] = label(cp, FB22, C_TEXT);
            lv_label_set_text(U.cmp_ic[i], ic_text[i]);
        }
        U.cmp_card = label(cp, FB26, C_SUB);
        wid(U.cmp_card, 160); pos(U.cmp_card, 126, 104);
        lv_obj_set_style_text_align(U.cmp_card, LV_TEXT_ALIGN_CENTER, 0);
        U.cmp_deg = label(cp, FT48, C_TEXT);
        wid(U.cmp_deg, 220); pos(U.cmp_deg, 96, 136);
        lv_obj_set_style_text_align(U.cmp_deg, LV_TEXT_ALIGN_CENTER, 0);
        U.cmp_hold = label(cp, FT14, C_SUB);
        wid(U.cmp_hold, 200); pos(U.cmp_hold, 106, 194);
        lv_obj_set_style_text_align(U.cmp_hold, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(U.cmp_hold, "HOLD  -  MOVE FOR HEADING");
        lv_obj_t *elev_cap = label(cp, FT14, C_SUB);
        wid(elev_cap, 160); pos(elev_cap, 126, 224);
        lv_obj_set_style_text_align(elev_cap, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(elev_cap, "ELEVATION");
        U.cmp_elev = label(cp, FT28, C_TEXT);
        wid(U.cmp_elev, 200); pos(U.cmp_elev, 106, 240);
        lv_obj_set_style_text_align(U.cmp_elev, LV_TEXT_ALIGN_CENTER, 0);
        U.cmp_vs = label(cp, FT14, C_SUB);
        wid(U.cmp_vs, 160); pos(U.cmp_vs, 126, 276);
        lv_obj_set_style_text_align(U.cmp_vs, LV_TEXT_ALIGN_CENTER, 0);
        cmp_refresh();
    }

    /* ---- clock page ---- */
    {
        lv_obj_t *cp = U.tile_clk;
        U.clk_date = label(cp, FT28, C_SUB);
        wid(U.clk_date, 320);
        pos(U.clk_date, 46, 106);
        lv_obj_set_style_text_align(U.clk_date, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(U.clk_date, "");
        U.clk_time = label(cp, FB120, C_TEXT);
        wid(U.clk_time, 392);
        pos(U.clk_time, 10, 153);      /* centred by advance width, so it stays centred as the digits change */
        lv_obj_set_style_text_align(U.clk_time, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(U.clk_time, "--:--");
        U.clk_ampm = label(cp, FT48, C_SUB);
        wid(U.clk_ampm, 200);
        pos(U.clk_ampm, 110, 258);
        lv_obj_set_style_text_align(U.clk_ampm, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(U.clk_ampm, "");

    }


    /* ---- tire page ---- */
    {
        lv_obj_t *tp = U.tile_tpms;
        TP.psi = label(tp, FT20, THEME_NIGHT.sub);
        wid(TP.psi, 60); pos(TP.psi, 176, TM_TOP + TM_H + 67);
        lv_label_set_text(TP.psi, "PSI");
        lv_obj_set_style_text_align(TP.psi, LV_TEXT_ALIGN_CENTER, 0);
        for (int i = 0; i < 2; i++) {
            /* Everything stays inside the ring (inner radius about 166 of 206 design px): the temperature, the pressure, the meter, then the name and the set pressure. */
            const int cx = tm_cx[i];
            TP.val[i] = label(tp, FT48, 0x010101);
            wid(TP.val[i], 120); pos(TP.val[i], cx - 60, 94);
            lv_obj_set_style_text_align(TP.val[i], LV_TEXT_ALIGN_CENTER, 0);
            TP.temp[i] = label(tp, FB22, 0x010101);
            wid(TP.temp[i], 120); pos(TP.temp[i], cx - 60, 72);
            lv_obj_set_style_text_align(TP.temp[i], LV_TEXT_ALIGN_CENTER, 0);
            TP.track[i] = plain(tp, cx - TM_W / 2, TM_TOP, TM_W, TM_H);
            lv_obj_set_style_radius(TP.track[i], S(10), 0);
            lv_obj_set_style_bg_opa(TP.track[i], LV_OPA_COVER, 0);
            TP.fill[i] = plain(tp, cx - TM_W / 2, TM_TOP, TM_W, 1);
            lv_obj_set_style_radius(TP.fill[i], S(10), 0);
            lv_obj_set_style_bg_opa(TP.fill[i], LV_OPA_COVER, 0);
            for (int side = 0; side < 2; side++) {                                  /* the alert line: half a meter wide on each side of the middle */
                TP.lim_tick[i][side] = plain(tp, cx, TM_TOP, TM_W / 2, 3);
                lv_obj_set_style_bg_opa(TP.lim_tick[i][side], LV_OPA_COVER, 0);
                lv_obj_add_flag(TP.lim_tick[i][side], LV_OBJ_FLAG_HIDDEN);
            }
            TP.lim_lbl[i] = label(tp, FT20, 0x010101);                              /* its psi, outside the meter: left of the front, right of the rear */
            wid(TP.lim_lbl[i], 40);
            pos(TP.lim_lbl[i], i ? cx + TM_W / 2 + 6 : cx - TM_W / 2 - 46, TM_TOP);
            lv_obj_set_style_text_align(TP.lim_lbl[i], i ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_add_flag(TP.lim_lbl[i], LV_OBJ_FLAG_HIDDEN);
            TP.cap[i] = label(tp, FB22, 0x010101);
            wid(TP.cap[i], 170); pos(TP.cap[i], cx - 85, TM_TOP + TM_H + 6);
            lv_obj_set_style_text_align(TP.cap[i], LV_TEXT_ALIGN_CENTER, 0);
            TP.sub[i] = label(tp, FT14, THEME_NIGHT.sub);
            wid(TP.sub[i], 150); pos(TP.sub[i], cx - 75, TM_TOP + TM_H + 34);
            lv_obj_set_style_text_align(TP.sub[i], LV_TEXT_ALIGN_CENTER, 0);
        }
        tpms_refresh();
    }

    /* page dots (on the round layer so they stay put while swiping) */
    for (int i = 0; i < NPAGES; i++) {
        U.dot[i] = plain(U.round, 0, 0, DOT_SIZE, DOT_SIZE);
        lv_obj_set_style_radius(U.dot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(U.dot[i], LV_OPA_COVER, 0);
        dot_place(i, NPAGES);
    }
    lv_obj_add_event_cb(U.tv, tile_changed_cb, LV_EVENT_SCROLL_END, NULL);

    /* the low-pressure ring: over every page and the dots, nothing to touch */
    TP.ring = plain(U.round, 0, 0, DSZ, DSZ);
    lv_obj_remove_flag(TP.ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(TP.ring, ring_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    ring_build();

    if (style_mode) theme_apply();
    tile_changed_cb(NULL);
    if (!U.tires_enabled) strip_apply();               /* the tire screen stays out of the strip until the phone turns monitoring on */

#ifdef NAV_UI_ROUND_MASK
    /* simulator only: black ring that hides the square corners */
    lv_obj_t *mask = plain(screen, 0, 0, DSZ, DSZ);
    lv_obj_remove_flag(mask, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(mask, mask_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
#endif

    refresh_trip_labels();
    lv_timer_create(frame_cb, 33, NULL);
    lv_timer_create(fl_timer_cb, 8, NULL);          /* keeps the cached label images up to date */
}
