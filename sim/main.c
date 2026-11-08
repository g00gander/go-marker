/*
 * Desktop simulator for the round nav display.
 *
 *   ./nav_sim                       run interactively (drag to swipe pages)
 *   ./nav_sim --hz 1                phone update rate (default 1 = realistic)
 *   ./nav_sim --shot out.bmp --at 60   simulate 60 s, save a screenshot, exit
 */
#include <SDL.h>
#include <math.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "feed.h"
#include "lvgl.h"
#include "nav_link.h"
#define ARROW_S_PUBLIC 84   /* must match ARROW_S in ui/nav_ui.c */
#include "nav_trip_store.h"
#include "nav_ui.h"

/* Trip persistence in the simulator: one file per slot in ~/.nav_sim/ */
static void slot_path(int slot, char *out, size_t n)
{
    const char *home = getenv("HOME");
    snprintf(out, n, "%s/.nav_sim/trip%d.bin", home ? home : ".", slot);
}
static int file_read(int slot, uint8_t *buf, size_t n)
{
    char p[512];
    slot_path(slot, p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    size_t got = fread(buf, 1, n, f);
    fclose(f);
    return (int)got;
}
static int file_write(int slot, const uint8_t *buf, size_t n)
{
    char p[512], dir[512];
    snprintf(dir, sizeof dir, "%s/.nav_sim", getenv("HOME") ? getenv("HOME") : ".");
    mkdir(dir, 0755);
    slot_path(slot, p, sizeof p);
    FILE *f = fopen(p, "wb");
    if (!f) return -1;
    size_t w = fwrite(buf, 1, n, f);
    fclose(f);
    return w == n ? 0 : -1;
}
static const nav_store_ops_t file_store = {file_read, file_write};

static volatile sig_atomic_t g_quit;
static void on_signal(int sig) { (void)sig; g_quit = 1; }

/* Screenshot runs use a fake clock that only advances when the loop says so, so the same
 * arguments always give the same pixels (the UI's smoothing and dead-reckoning read this clock). */
static const char *zoom_trace_path;
static FILE *zoom_trace_file;
static double zoom_trace_t;
static void zoom_trace_cb(float scale, float tgt, float dt);
static int glyph_cull_check;
static int glyph_cache_check;
static int g_fake_clock;
static uint32_t g_fake_ms;
static uint32_t tick_cb(void) { return g_fake_clock ? g_fake_ms : SDL_GetTicks(); }

static void save_bmp(const char *path)
{
    lv_draw_buf_t *buf = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_ARGB8888);
    if (!buf) { fprintf(stderr, "snapshot failed\n"); return; }
    int w = buf->header.w, h = buf->header.h;
    uint32_t stride = buf->header.stride;
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    uint32_t size = 54 + w * h * 4;
    uint8_t hdr[54] = {'B', 'M'};
    memcpy(hdr + 2, &size, 4);
    uint32_t off = 54, dib = 40;
    int32_t hh = -h;                     /* top-down */
    uint16_t planes = 1, bpp = 32;
    memcpy(hdr + 10, &off, 4);
    memcpy(hdr + 14, &dib, 4);
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &hh, 4);
    memcpy(hdr + 26, &planes, 2);
    memcpy(hdr + 28, &bpp, 2);
    fwrite(hdr, 1, 54, f);
    for (int y = 0; y < h; y++) {
        const uint8_t *row = buf->data + y * stride;
        for (int x = 0; x < w; x++) {
            uint8_t px[4] = {row[x * 4], row[x * 4 + 1], row[x * 4 + 2], 255};
            fwrite(px, 1, 4, f);
        }
    }
    fclose(f);
    lv_draw_buf_destroy(buf);
    printf("saved %s\n", path);
}

static nav_status_t g_status;
static double g_since_update = 0;
static double g_period = 1.0;      /* seconds between phone STATUS packets */
static uint32_t g_last_tick;

/* ---- live mode: packets from the phone (or the Bluetooth bridge) arrive over UDP ---------
 * Datagram = 1 byte channel (0 STATUS, 1 ROUTE, 2 WEATHER) + the characteristic's bytes. */
static int g_udp = -1;

static int udp_open(int port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) { close(fd); return -1; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    return fd;
}

static void udp_poll_cb(lv_timer_t *t)
{
    (void)t;
    uint8_t buf[1024];
    ssize_t n;
    while ((n = recvfrom(g_udp, buf, sizeof buf, 0, NULL, NULL)) > 0) {
        if (n >= 2) nav_link_write((nav_channel_t)buf[0], buf + 1, (size_t)n - 1);
    }
}


/* Simulated altitude for the compass page: a hill climb and descent around 600 ft. */
static void altitude_demo(double t)
{
    double alt = 185.0 + 60.0 * sin(2 * 3.14159265 * t / 90.0), vs = 60.0 * 2 * 3.14159265 / 90.0 * cos(2 * 3.14159265 * t / 90.0);
    nav_ui_set_altitude((float)alt, (float)vs, 1);
}
static void altitude_timer_cb(lv_timer_t *t) { (void)t; altitude_demo(lv_tick_get() / 1000.0); }

static void print_stats_cb(lv_timer_t *t)
{
    (void)t;
    const nav_link_stats_t *s = nav_link_stats();
    printf("link: %u status, %u weather, %u routes (%u pts), %u road sets (%u segs), %u rejected\n", s->status, s->weather,
           s->route_ok, s->last_route_points, s->roads_ok, s->last_road_segments, s->rejected);
    fflush(stdout);
}

/* --rb "90,0,-90:1": the exits of a roundabout (degrees, 0 = straight on) and the index to take; ":r" at the end = only the route's exit is known. */
static void apply_rb_arg(const char *rb_arg)
{
    if (!rb_arg) return;
    nav_roundabout_t rbd; memset(&rbd, 0, sizeof rbd);
    rbd.proto_version = NAV_PROTO_VERSION; rbd.taken = 0xFF; rbd.flags = 1;
    char buf[128]; snprintf(buf, sizeof buf, "%s", rb_arg);
    char *colon = strchr(buf, ':'); if (colon) *colon = 0;
    for (char *t = strtok(buf, ","); t && rbd.count < NAV_RB_MAX_EXITS; t = strtok(NULL, ",")) rbd.dir2[rbd.count++] = (int8_t)(atoi(t) / 2);
    if (colon) { rbd.taken = (uint8_t)atoi(colon + 1); if (strchr(colon + 1, 'r')) rbd.flags = 0; }
    nav_ui_set_roundabout(&rbd);
}

/* --tpms "36.5,34,30,30[,72,78[,36,40]]": front and rear pressure (psi, "-" = no sensor), the two alert limits, then the two temperatures (F) and the two set pressures.
 * A tire at or under its limit raises the low flag, as the phone would. In a live run the packet is sent again every 2 seconds, like the phone does. */
static const char *g_tpms_arg;
static void apply_tpms_arg(const char *arg)
{
    if (!arg) return;
    char buf[128]; snprintf(buf, sizeof buf, "%s", arg);
    double v[8] = { -1, -1, 30, 30, 0xFF, 0xFF, 0, 0 };
    int k = 0;
    for (char *t = strtok(buf, ","); t && k < 8; t = strtok(NULL, ","), k++) v[k] = (t[0] == '-' && !t[1]) ? -1 : atof(t);
    nav_tpms_t p; memset(&p, 0, sizeof p);
    p.proto_version = NAV_PROTO_VERSION;
    p.flags = NAV_TPMS_ENABLED;
    p.front_x10 = v[0] < 0 ? NAV_TPMS_NONE : (uint16_t)(v[0] * 10 + 0.5); p.rear_x10 = v[1] < 0 ? NAV_TPMS_NONE : (uint16_t)(v[1] * 10 + 0.5);
    p.front_low_x10 = (uint16_t)(v[2] * 10 + 0.5); p.rear_low_x10 = (uint16_t)(v[3] * 10 + 0.5);
    p.front_temp_f = (uint8_t)v[4]; p.rear_temp_f = (uint8_t)v[5];
    p.front_set_x10 = (uint16_t)(v[6] * 10 + 0.5); p.rear_set_x10 = (uint16_t)(v[7] * 10 + 0.5);
    if (v[0] >= 0) p.flags |= NAV_TPMS_FRONT_OK | (p.front_x10 <= p.front_low_x10 ? NAV_TPMS_FRONT_LOW : 0);
    if (v[1] >= 0) p.flags |= NAV_TPMS_REAR_OK | (p.rear_x10 <= p.rear_low_x10 ? NAV_TPMS_REAR_LOW : 0);
    nav_link_write(NAV_CH_TPMS, (const uint8_t *)&p, sizeof p);
}
static void tpms_timer_cb(lv_timer_t *t) { (void)t; apply_tpms_arg(g_tpms_arg); }

/* The ride and the IMU run continuously; the phone only sends STATUS at ~1 Hz. */
static int g_free;                              /* --free: show the free-ride view (a phone connected, no route running) */
/* The demo feed always rides its route; --free turns it into what the phone sends when nothing is navigating. */
static void sim_feed_step(double dt, nav_status_t *out)
{
    feed_step(dt, out);
    if (!g_free) return;
    out->flags &= (uint8_t)~NAV_FLAG_NAVIGATING;
    out->maneuver = NAV_MAN_STRAIGHT; out->next_maneuver = NAV_MAN_STRAIGHT;
    out->dist_turn_m = 0; out->dist_next_m = 0;
    memset(out->street, 0, sizeof out->street);
    memcpy(out->street, "Tumwater, WA", 12);
}

static int g_force_man = -1;                    /* --maneuver in the live window: hold this glyph */
static void feed_timer_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t now = lv_tick_get();
    double dt = (now - g_last_tick) / 1000.0;
    g_last_tick = now;
    if (dt > 0.25) dt = 0.25;

    sim_feed_step(dt, &g_status);
    if (g_force_man >= 0) { g_status.maneuver = (uint8_t)g_force_man; g_status.dist_turn_m = 240; }

    g_since_update += dt;
    if (g_since_update >= g_period) {
        g_since_update = 0;
        nav_ui_update(&g_status);
    }
}

static void zoom_trace_cb(float scale, float tgt, float dt)
{
    zoom_trace_t += dt;
    if (zoom_trace_file) {
        float x, y, hd;
        nav_ui_debug_position(&x, &y, &hd);
        fprintf(zoom_trace_file, "%.3f,%.5f,%.5f,%u,%.2f,%.3f,%.3f,%.3f\n", zoom_trace_t, scale, tgt, (unsigned)g_status.dist_turn_m, g_status.speed_kmh_x10 / 36.0, x, y, hd);
    }
}

int main(int argc, char **argv)
{
    const char *shot = NULL;
    double at = 30.0, hz = 1.0;
    int splash = 0;
    const char *order_arg = NULL;
    const char *off_arg = NULL;
    const char *battery_arg = NULL, *toast_arg = NULL;
    int page = 0, metric = 0, clock24 = 0, style = 0, wx = -1, night = 0, life_size = 0, live = 0, port = 47800;
    const char *rb_arg = NULL, *tpms_arg = NULL;
    uint32_t pairing = 0;  /* --pairing CODE: show the pairing code screen, e.g. --pairing 123456 */
    int traffic = 0;       /* --traffic: slow and stopped stretches on the demo route, with the alert message */
    int persist = 0, fresh = 0, cull_check = 0, track_check = 0, maneuver_override = -1, glyph_check = 0;
    double gps_noise = 0;   /* --gps-noise M: random position error of up to M meters in --track-check */
    double wait_s = 3.0;
    int wait_given = 0;
    float ppi = 254.0f;    /* physical pixels per inch of this screen (MacBook Pro 14") */
    float mag = 1.0f;      /* --mag N: N times life size */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--at") && i + 1 < argc) at = atof(argv[++i]);
        else if (!strcmp(argv[i], "--page") && i + 1 < argc) page = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--life-size")) life_size = 1;
        else if (!strcmp(argv[i], "--ppi") && i + 1 < argc) ppi = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--mag") && i + 1 < argc) mag = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--wx") && i + 1 < argc) wx = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--night")) { night = 1; feed_theme_night = 1; }
        else if (!strcmp(argv[i], "--splash")) splash = 1;
        else if (!strcmp(argv[i], "--off") && i + 1 < argc) off_arg = argv[++i];           /* screens to turn off, e.g. --off 1,4 */
        else if (!strcmp(argv[i], "--order") && i + 1 < argc) order_arg = argv[++i];        /* e.g. --order 4,0,1,2,3 */
        else if (!strcmp(argv[i], "--tpms") && i + 1 < argc) tpms_arg = argv[++i];       /* tire pressures: "36.5,34,30,30" (front, rear, front limit, rear limit; psi) */
        else if (!strcmp(argv[i], "--free")) g_free = 1;           /* the free-ride view, no route */
        else if (!strcmp(argv[i], "--metric")) metric = 1;          /* km, m, km/h, C */
        else if (!strcmp(argv[i], "--vintage") || !strcmp(argv[i], "--vintage-night")) style = 1;   /* the green CL350 theme */
        else if (!strcmp(argv[i], "--vintage-day")) style = 7;      /* the cream daylight twin */
        else if (!strcmp(argv[i], "--vintage-auto")) style = 8;     /* whichever the phone says (--night for the green one) */
        else if (!strcmp(argv[i], "--tripper")) style = 2;          /* the Bear 650 dash theme, day face */
        else if (!strcmp(argv[i], "--tripper-night")) style = 3;    /* ... and its night face */
        else if (!strcmp(argv[i], "--standard-day")) style = 6;     /* the standard light theme, always */
        else if (!strcmp(argv[i], "--standard-night")) style = 5;   /* the standard dark theme, always */
        else if (!strcmp(argv[i], "--tripper-auto")) style = 4;     /* ... or whichever the phone says (--night for the dark one) */
        else if (!strcmp(argv[i], "--pairing") && i + 1 < argc) pairing = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--traffic")) traffic = 1;
        else if (!strcmp(argv[i], "--traffic-quiet")) traffic = 2;       /* the same, without the alert message */
        else if (!strcmp(argv[i], "--toast") && i + 1 < argc) toast_arg = argv[++i];        /* show a message, e.g. --toast "Touch off" */
        else if (!strcmp(argv[i], "--battery") && i + 1 < argc) battery_arg = argv[++i];     /* e.g. 82 or 82:c (charging) */
        else if (!strcmp(argv[i], "--clock24")) clock24 = 1;        /* 24-hour clock */
        else if (!strcmp(argv[i], "--rb") && i + 1 < argc) rb_arg = argv[++i];        /* roundabout exits: "90,0,-90:1" = exits (degrees) : index to take; ":r" at the end = route only */
        else if (!strcmp(argv[i], "--mph") && i + 1 < argc) feed_cruise_ms = (float)atof(argv[++i]) * 0.44704f;      /* demo cruising speed */
        else if (!strcmp(argv[i], "--udp")) live = 1;
        else if (!strcmp(argv[i], "--cull-check")) { cull_check = 1; shot = "unused"; }
        else if (!strcmp(argv[i], "--area-check")) { cull_check = 2; shot = "unused"; }   /* spans vs triangles */
        else if (!strcmp(argv[i], "--line-check")) { cull_check = 3; shot = "unused"; }   /* pieces vs whole lines */
        else if (!strcmp(argv[i], "--track-check")) { track_check = 1; shot = "unused"; }
        else if (!strcmp(argv[i], "--glyph-check")) { glyph_check = 1; shot = "unused"; }
        else if (!strcmp(argv[i], "--gps-noise") && i + 1 < argc) gps_noise = atof(argv[++i]);
        else if (!strcmp(argv[i], "--maneuver") && i + 1 < argc) maneuver_override = atoi(argv[++i]);   /* force a glyph in --shot runs */
        else if (!strcmp(argv[i], "--persist")) persist = 1;      /* keep trip stats even in --shot runs */
        else if (!strcmp(argv[i], "--fresh")) fresh = 1;          /* forget saved trip stats first */
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--wait") && i + 1 < argc) { wait_s = atof(argv[++i]); wait_given = 1; }
        else if (!strcmp(argv[i], "--zoom-trace") && i + 1 < argc) zoom_trace_path = argv[++i];      /* write the zoom every 33 ms of the ride to a CSV file (use with --shot x --at 150) */
        else if (!strcmp(argv[i], "--glyph-cache-check")) glyph_cache_check = 1;     /* the kept arrows against fresh renders: must be identical */
        else if (!strcmp(argv[i], "--glyph-cull-check")) glyph_cull_check = 1;     /* every maneuver glyph with and without the renderer's shortcut: must be identical */
        else if (!strcmp(argv[i], "--no-label-cache")) nav_ui_label_cache = 0;      /* A/B: LVGL draws every label itself */
        else if (!strcmp(argv[i], "--hz") && i + 1 < argc) hz = atof(argv[++i]);
    }
    g_period = 1.0 / hz;
    g_fake_clock = shot != NULL;

    signal(SIGTERM, on_signal);   /* quit cleanly (exit 0) instead of dying on a signal */
    signal(SIGINT, on_signal);

    lv_init();
    lv_tick_set_cb(tick_cb);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");   /* smooth when shrunk to life size */
    lv_display_t *disp = lv_sdl_window_create(NAV_UI_SIZE, NAV_UI_SIZE);
    lv_sdl_mouse_create();

    if (fresh) {
        for (int s = 0; s < 2; s++) { char p[512]; slot_path(s, p, sizeof p); remove(p); }
        printf("saved trip cleared\n");
    }
    /* Trip stats persist across runs (like the display across ignition cycles), except in
     * screenshot runs, which stay repeatable unless asked with --persist. */
    nav_store_init((!shot || persist) ? &file_store : NULL);

    nav_ui_init(lv_screen_active());
    { time_t now_ = time(NULL); struct tm lt; localtime_r(&now_, &lt); nav_ui_set_clock_offset((int32_t)lt.tm_gmtoff); }   /* the host's zone */
    if (splash) nav_ui_splash_show(1);
    if (order_arg) {
        uint8_t ids[6]; int n = 0;
        for (const char *c = order_arg; *c && n < 6; c++) if (*c >= '0' && *c <= '9') ids[n++] = (uint8_t)(*c - '0');
        if (!nav_ui_set_page_order(ids, n)) fprintf(stderr, "--order: not a permutation of 0..5\n");
    }

    if (off_arg) {
        uint8_t mask = 0;
        for (const char *c = off_arg; *c; c++) if (*c >= '0' && *c <= '5') mask |= (uint8_t)(1u << (*c - '0'));
        nav_ui_set_pages_off(mask);
    }

    if (life_size && !shot) {
        /* Size the window so the 1.46" round LCD is its real physical size on this screen. */
        int out_w = 0, out_h = 0, win_w = 0, win_h = 0;
        SDL_Window *win = lv_sdl_window_get_window(disp);
        SDL_GetWindowSize(win, &win_w, &win_h);
        SDL_GetRendererOutputSize(lv_sdl_window_get_renderer(disp), &out_w, &out_h);
        float backing = win_w > 0 ? (float)out_w / (float)win_w : 1.0f;   /* 2 on Retina */
        float pts_per_in = ppi / backing;
        float target_pts = 1.46f * pts_per_in * mag;
        lv_sdl_window_set_zoom(disp, target_pts / NAV_UI_SIZE);
        printf("life-size: %.0f ppi screen, backing scale %.0f -> window %.0f pt = %.2f in (%.1f mm) x%.1f\n",
               ppi, backing, target_pts, 1.46f * mag, 1.46f * mag * 25.4f, mag);
        fflush(stdout);
    }
    nav_ui_set_metric(metric);
    nav_ui_set_clock24(clock24);
    nav_ui_set_style(style);
    if (battery_arg) nav_ui_set_battery(atoi(battery_arg), strchr(battery_arg, 'c') != NULL);
    if (toast_arg) nav_ui_toast(toast_arg);
    if (pairing) nav_ui_pairing_code(pairing);
    nav_link_init();
    printf("label cache table: %d labels registered\n", nav_ui_label_count());

    if (glyph_cache_check) {
        const int bad = nav_ui_debug_glyph_cache_check();
        printf("glyph-cache-check: %d differ\n", bad);
        return bad != 0;
    }

    if (glyph_cull_check) {
        static lv_color32_t a[NAV_UI_SIZE * NAV_UI_SIZE];
        int bad = 0, n = 0;
        for (int man = 0; man < 24; man++) {
            nav_gl_cull = 1;  nav_ui_debug_glyph(-1); memcpy(a, nav_ui_debug_glyph(man), sizeof a);
            nav_gl_cull = 0;  nav_ui_debug_glyph(-1); const lv_color32_t *b = nav_ui_debug_glyph(man);
            n++;
            if (memcmp(a, b, sizeof a)) { bad++; printf("glyph %d differs\n", man); }
        }
        nav_gl_cull = 1;
        printf("glyph-cull-check: %d maneuvers compared, %d differ\n", n, bad);
        return bad != 0;
    }

    if (live) {
        /* Live mode: no fake ride. The display shows whatever the phone sends. */
        g_udp = udp_open(port);
        if (g_udp < 0) { fprintf(stderr, "cannot listen on UDP %d\n", port); return 1; }
        printf("live mode: waiting for the phone on UDP 127.0.0.1:%d\n", port);
        fflush(stdout);
        lv_timer_create(udp_poll_cb, 10, NULL);
        lv_timer_create(print_stats_cb, 5000, NULL);
        if (shot) {
            nav_ui_show_page(page);
            uint32_t until = lv_tick_get() + (uint32_t)(wait_s * 1000);
            while ((int32_t)(until - lv_tick_get()) > 0) { if (g_fake_clock) g_fake_ms += 5; lv_timer_handler(); usleep(5000); }
            print_stats_cb(NULL);
            save_bmp(shot);
            return 0;
        }
        while (!g_quit) {
            if (!lv_display_get_default()) break;
            uint32_t d = lv_timer_handler();
            usleep(d > 5 ? d * 1000 : 5000);
        }
        return 0;
    }

    nav_weather_t weather;
    feed_weather(&weather, wx, night);
    nav_ui_set_weather(&weather);

    feed_init();
    uint16_t n;
    const nav_pt_t *route = feed_route(&n);
    nav_ui_set_route(FEED_ORIGIN_LAT_E7, FEED_ORIGIN_LON_E7, route, n);
    if (traffic) {      /* the east leg is slow, the north leg after the diagonal is nearly stopped (route point numbers) */
        const nav_traffic_span_t sp[] = { {1, 2, NAV_TRAFFIC_SLOW}, {3, 4, NAV_TRAFFIC_STOPPED} };
        nav_ui_set_traffic(sp, 2);
        if (traffic == 1) nav_ui_traffic_alert(NAV_TRAFFIC_SLOW, 600, 420);
    }
    const nav_seg_t *roads = feed_roads(&n);
    nav_ui_set_roads(roads, n);
    const nav_area_t *areas = feed_areas(&n);
    nav_ui_set_areas(areas, n);

    if (glyph_check) {
        const int N = ARROW_S_PUBLIC;
        int bad = 0;
        /* 1. left/right pairs must be exact mirror images of each other */
        static const int pairs[][2] = {{1, 2}, {3, 4}, {7, 6}, {10, 11}};
        static uint8_t keep[ARROW_S_PUBLIC * ARROW_S_PUBLIC * 4];
        for (unsigned q = 0; q < sizeof pairs / sizeof pairs[0]; q++) {
            const lv_color32_t *a = nav_ui_debug_glyph(pairs[q][0]);
            memcpy(keep, a, (size_t)N * N * 4);
            const lv_color32_t *b = nav_ui_debug_glyph(pairs[q][1]);
            int maxd = 0;
            for (int y = 0; y < N; y++)
                for (int x = 0; x < N; x++) {
                    const uint8_t *pa = keep + ((size_t)y * N + x) * 4;
                    const uint8_t *pb = (const uint8_t *)&b[y * N + (N - 1 - x)];
                    for (int c = 0; c < 4; c++) { int d = abs((int)pa[c] - (int)pb[c]); if (d > maxd) maxd = d; }
                }
            printf("mirror pair %2d/%-2d: max channel difference %d/255%s\n", pairs[q][0], pairs[q][1], maxd, maxd > 2 ? "   <-- not symmetric" : "");
            if (maxd > 2) bad++;
        }
        /* 2. the straight stem of every glyph: center and width must not wander from row to row */
        static const int stems[] = {0, 1, 2, 3, 4, 6, 7, 8, 9};
        for (unsigned q = 0; q < sizeof stems / sizeof stems[0]; q++) {
            const lv_color32_t *g = nav_ui_debug_glyph(stems[q]);
            float cmin = 1e9f, cmax = -1e9f, wmin = 1e9f, wmax = -1e9f;
            for (int y = (int)(N * 0.86f); y < (int)(N * 0.92f); y++) {          /* the lowest rows: pure stem */
                float sum = 0, mom = 0;
                for (int x = 0; x < N; x++) { float a = g[y * N + x].alpha / 255.0f; sum += a; mom += a * (x + 0.5f); }
                if (sum < 1) continue;
                float c = mom / sum;
                if (c < cmin) cmin = c; if (c > cmax) cmax = c; if (sum < wmin) wmin = sum; if (sum > wmax) wmax = sum;
            }
            printf("glyph %2d stem: center %.3f..%.3f px (spread %.3f), width %.3f..%.3f px%s\n", stems[q], cmin, cmax, cmax - cmin, wmin, wmax,
                   (cmax - cmin > 0.05f || wmax - wmin > 0.05f) ? "   <-- wanders" : "");
            if (cmax - cmin > 0.05f || wmax - wmin > 0.05f) bad++;
        }
        /* 3. seams: a partly covered pixel between two fully covered ones means two shapes met badly */
        for (int man = 0; man <= 12; man++) {
            const lv_color32_t *g = nav_ui_debug_glyph(man);
            int seams = 0;
            for (int y = 1; y < N - 1; y++)
                for (int x = 1; x < N - 1; x++) {
                    int a = g[y * N + x].alpha;
                    if (a >= 245 || a <= 10) continue;
                    int h = g[y * N + x - 1].alpha >= 250 && g[y * N + x + 1].alpha >= 250;
                    int v = g[(y - 1) * N + x].alpha >= 250 && g[(y + 1) * N + x].alpha >= 250;
                    if (h || v) { seams++; if (getenv("GLYPH_VERBOSE")) printf("   glyph %d seam pixel at (%d,%d) alpha %d\n", man, x, y, a); }
                }
            if (seams > 4) { printf("glyph %2d: %d seam pixels\n", man, seams); bad++; }   /* 1-2 px = inner-corner edge pixels, fine */
        }
        printf("glyph-check: %s\n", bad ? "PROBLEMS" : "all clean");
        fflush(stdout);
        return bad ? 1 : 0;
    }

    if (track_check) {
        /* How far is the marker from the route while riding? Real-time behavior: the phone sends a
         * STATUS once a second, the UI dead-reckons in between and redraws every 33 ms. */
        const double dt = 0.033;
        static float cross[20000], step_err[20000], turn[20000];
        int n = 0;
        float lx = 0, ly = 0, lh = 0; int have_last = 0;
        double since = 1.0;
        sim_feed_step(0, &g_status);
        nav_ui_update(&g_status);
        for (double t = 0; t < 280.0 && n < 20000; t += dt) {
            sim_feed_step(dt, &g_status);
            since += dt;
            if (since >= 1.0) {
                since = 0;
                nav_status_t noisy = g_status;
                if (gps_noise > 0) {                 /* deterministic pseudo-random GPS error */
                    static unsigned rng = 12345;
                    rng = rng * 1103515245u + 12345u; double ex = ((rng >> 16) & 0x7fff) / 32767.0 * 2 - 1;
                    rng = rng * 1103515245u + 12345u; double ey = ((rng >> 16) & 0x7fff) / 32767.0 * 2 - 1;
                    noisy.lat_e7 += (int32_t)(ey * gps_noise / 111320.0 * 1e7);
                    noisy.lon_e7 += (int32_t)(ex * gps_noise / 111320.0 * 1e7 / 0.79);
                }
                nav_ui_update(&noisy);
            }
            nav_ui_advance((float)dt);
            {
                float x, y, hd; nav_ui_debug_position(&x, &y, &hd);
                float sp = g_status.speed_kmh_x10 / 36.0f;
                if (have_last) {
                    float st = hypotf(x - lx, y - ly);
                    step_err[n] = fabsf(st - sp * (float)dt);            /* meters the marker moved beyond/short of speed*dt */
                    float dh = hd - lh; while (dh > 180) dh -= 360; while (dh < -180) dh += 360;
                    turn[n] = fabsf(dh);                                  /* map rotation this frame, degrees */
                } else { step_err[n] = 0; turn[n] = 0; }
                lx = x; ly = y; lh = hd; have_last = 1;
            }
            cross[n++] = nav_ui_cross_px;
        }
        float mx = 0, sum = 0; int over1 = 0, over05 = 0, worst_i = 0;
        for (int i = 0; i < n; i++) {
            sum += cross[i];
            if (cross[i] > mx) { mx = cross[i]; worst_i = i; }
            if (cross[i] > 1.0f) over1++;
            if (cross[i] > 0.5f) over05++;
        }
        {   /* list the worst moments (one per 2 s window) so the cause can be read off the ride */
            int shown = 0;
            for (int i = 0; i < n && shown < 6; i++) {
                if (cross[i] > 1.0f) {
                    printf("  off-route %.1f px at t=%.1fs\n", cross[i], i * dt);
                    shown++;
                    i += (int)(2.0 / dt);
                }
            }
        }
        {
            float se = 0, sum_se = 0, mt = 0;
            for (int i = 0; i < n; i++) { if (step_err[i] > se) se = step_err[i]; sum_se += step_err[i]; if (turn[i] > mt) mt = turn[i]; }
            printf("  smoothness: marker step error vs speed*dt: mean %.3f m, max %.2f m | fastest map rotation %.1f deg/frame\n", sum_se / n, se, mt);
        }
        printf("track-check: %d frames | marker-to-route distance: mean %.2f px, max %.2f px (at t=%.0fs) | "
               ">0.5 px in %.1f%% of frames, >1 px in %.1f%%\n",
               n, sum / n, mx, worst_i * dt, 100.0 * over05 / n, 100.0 * over1 / n);
        fflush(stdout);
        return 0;
    }

    if (cull_check) {
        /* Render the same frame twice from identical state - once with culling, once without -
         * and count differing pixels. lv_refr_now() draws without advancing the clock, so the
         * state cannot change between the two renders. */
        const double step = 0.1;
        int every = (int)(g_period / step + 0.5);
        if (every < 1) every = 1;
        int i = 0, bad = 0, checked = 0;
        double t = 0;
        int *sw = (cull_check == 2) ? &nav_ui_area_scanline : (cull_check == 3) ? &nav_ui_line_chunk : &nav_ui_cull_enabled;
        long total_diff = 0, max_diff = 0;
        for (int mark = 5; mark <= 295; mark += 5) {
            for (; t < mark; t += step, i++) {
                sim_feed_step(step, &g_status);
                if (i % every == 0) nav_ui_update(&g_status);
                nav_ui_advance((float)step);
            }
            nav_ui_update(&g_status);
            for (int pg = 0; pg <= 2; pg += 2) {          /* map page and trip page */
                if (pg != 0 && mark % 50) continue;
                nav_ui_show_page(pg);
                for (int k = 0; k < 4; k++) { g_fake_ms += 40; lv_timer_handler(); }
                *sw = 1;
                lv_obj_invalidate(lv_screen_active());
                lv_refr_now(NULL);
                lv_draw_buf_t *a = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_ARGB8888);
                *sw = 0;
                lv_obj_invalidate(lv_screen_active());
                lv_refr_now(NULL);
                lv_draw_buf_t *b = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_ARGB8888);
                *sw = 1;
                long diff = 0;
                if (a && b) {
                    for (int y = 0; y < a->header.h; y++) {
                        const uint8_t *ra = a->data + y * a->header.stride, *rb = b->data + y * b->header.stride;
                        for (int x = 0; x < a->header.w; x++)
                            if (memcmp(ra + x * 4, rb + x * 4, 3)) diff++;
                    }
                }
                if (a) lv_draw_buf_destroy(a);
                if (b) lv_draw_buf_destroy(b);
                checked++;
                total_diff += diff; if (diff > max_diff) max_diff = diff;
                if (diff) { bad++; if (cull_check == 1) printf("t=%3ds page %d: %ld pixels differ\n", mark, pg, diff); }
            }
        }
        if (cull_check >= 2)
            printf("%s: %d frames compared; differing pixels per frame: mean %ld, max %ld (of %d)\n",
                   cull_check == 2 ? "area-check" : "line-check",
                   checked, total_diff / (checked ? checked : 1), max_diff, NAV_UI_SIZE * NAV_UI_SIZE);
        else if (0)
            printf("area-check: %d frames compared; differing pixels per frame: mean %ld, max %ld (of %d)\n",
                   checked, total_diff / (checked ? checked : 1), max_diff, NAV_UI_SIZE * NAV_UI_SIZE);
        else
            printf("cull-check: %d frames compared, %d differ\n", checked, bad);
        fflush(stdout);
        return (cull_check == 1 && bad) ? 1 : 0;
    }

    if (shot) {
        /* fast-forward the ride without rendering, then let the UI settle */
        const double step = zoom_trace_path ? 1.0 / 30.0 : 0.1;
        if (zoom_trace_path) { zoom_trace_file = fopen(zoom_trace_path, "w"); if (zoom_trace_file) { fprintf(zoom_trace_file, "t,scale,target,dist_turn_m,speed_ms,x,y,heading\n"); nav_ui_zoom_hook = zoom_trace_cb; } }
        int every = (int)(g_period / step + 0.5);
        if (every < 1) every = 1;
        int i = 0;
        for (double t = 0; t < at; t += step, i++) {
            sim_feed_step(step, &g_status);
                    if (i % every == 0) nav_ui_update(&g_status);
            nav_ui_advance((float)step);
        }
        if (zoom_trace_file) { nav_ui_zoom_hook = NULL; fclose(zoom_trace_file); zoom_trace_file = NULL; }
        apply_rb_arg(rb_arg);
        if (maneuver_override >= 0) { g_status.maneuver = (uint8_t)maneuver_override; g_status.dist_turn_m = 240; }
        nav_ui_update(&g_status);
        apply_tpms_arg(tpms_arg);          /* (the tire screen is only in the strip once there is a TPMS packet) */
        nav_ui_show_page(page);
        altitude_demo(at);
        if (wait_given) {          /* --wait S: run the UI for S more seconds before the picture (animations, such as the logo pop, show their frames) */
            const uint32_t until = lv_tick_get() + (uint32_t)(wait_s * 1000);
            while ((int32_t)(until - lv_tick_get()) > 0) { if (g_fake_clock) g_fake_ms += 1; lv_timer_handler(); usleep(1000); }
        } else {
            for (int k = 0; k < 80; k++) { g_fake_ms += 10; lv_timer_handler(); usleep(1000); }
        }
        save_bmp(shot);
        return 0;
    }

    g_last_tick = lv_tick_get();
    g_force_man = maneuver_override;
    apply_rb_arg(rb_arg);
    g_tpms_arg = tpms_arg;
    if (tpms_arg) { apply_tpms_arg(tpms_arg); lv_timer_create(tpms_timer_cb, 2000, NULL); }
    lv_timer_create(feed_timer_cb, 33, NULL);
    lv_timer_create(altitude_timer_cb, 200, NULL);
    sim_feed_step(0, &g_status);
    nav_ui_update(&g_status);
    while (!g_quit) {
        if (!lv_display_get_default()) break;      /* window was closed */
        uint32_t d = lv_timer_handler();
        usleep(d > 5 ? d * 1000 : 5000);
    }
    return 0;
}
