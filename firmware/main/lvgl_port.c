#include "lvgl_port.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include <string.h>
#include <math.h>
#include "nav_ui.h"
#include "src/draw/sw/lv_draw_sw_utils.h"   /* lv_draw_sw_rgb565_swap */

#include "board_display.h"     /* the board's panel and touch drivers */

static const char *TAG = "lvgl_port";
volatile uint32_t nav_touch_max_us;                 /* the longest touch read since the last report (microseconds) */
extern volatile uint32_t nav_batt_max_us;           /* the longest battery-gauge read (board_battery.c) */
volatile uint32_t nav_lcd_timeouts;                 /* sends whose "finished" signal never came (each cost a 200 ms wait) */
extern uint32_t nav_loop_max_us[3];
extern uint32_t nav_timer_max_us[2];
extern uint32_t nav_write_worst_us, nav_write_worst_ch, nav_write_worst_len;
extern const char *nav_build_tag;   /* app_main.c */

/* Two partial draw buffers (1/10 frame) in internal RAM: alpha blending is far slower through PSRAM. */
#if BOARD_COLOR_BITS == 18
/* 18-bit color: LVGL renders 24-bit pixels and flush_cb sends them with a little dither; the buffers are 1/14 frame. */
#define LVGL_BYTES_PER_PX 3
#define LVGL_BUF_PIXELS   (EXAMPLE_LCD_WIDTH * EXAMPLE_LCD_HEIGHT / 10)
#else
#define LVGL_BYTES_PER_PX 2
#define LVGL_BUF_PIXELS   (EXAMPLE_LCD_WIDTH * EXAMPLE_LCD_HEIGHT / 10)
#endif
#define LVGL_TICK_PERIOD_MS 2

/* ---- frame timing (logged every 2 s; set NAV_PERF_LOG to 0 to silence) --------- */
#define NAV_PERF_LOG 1
/* Per-object draw timing (prof_*) costs LVGL memory: turn on only when hunting slow objects. */
#define NAV_PERF_OBJECTS 0
#if NAV_PERF_LOG
static int64_t perf_render_start, perf_last_report;
static int64_t perf_acc_frame, perf_acc_swap, perf_acc_send, perf_acc_wait, perf_wait_start, perf_acc_gap, perf_last_ready;
static uint32_t perf_frames, perf_flushes;
static uint64_t perf_pixels;

#if NAV_PERF_OBJECTS
static void prof_attach(lv_obj_t *o);
static void prof_report(double frames);
#endif

/* frame pacing: start-to-start time of consecutive frames (a gap over 200 ms is a pause) */
static int64_t pace_last, pace_prev;
static double pace_sum, pace_sumsq, pace_max;
static uint32_t pace_n, pace_late;
static double pace_worst_render;
static double mot_sum, mot_sumsq, mot_max, mot_prev, mot_maxjump;
static float ring_tr[8], ring_tu[8], ring_zo[8], ring_dt[8];       /* the last 8 frames: what moved, and the time since the frame before */
static int ring_i, jump_reports;
static uint32_t mot_n, mot_jumps;

static void perf_render_start_cb(lv_event_t *e)
{
    (void)e;
    perf_render_start = esp_timer_get_time();
    if (pace_last && perf_render_start - pace_last < 200000) {
        const double ms = (perf_render_start - pace_last) / 1000.0;
        pace_sum += ms; pace_sumsq += ms * ms; pace_n++;
        if (ms > pace_max) pace_max = ms;
        if (ms > 55.0) pace_late++;
    }
    pace_last = perf_render_start;
    {   /* how far the picture moved since the last frame (px): a stutter is a jump in this number */
        const double st = nav_ui_pose_step();
        ring_tr[ring_i & 7] = nav_pose_parts[0]; ring_tu[ring_i & 7] = nav_pose_parts[1]; ring_zo[ring_i & 7] = nav_pose_parts[2];
        ring_dt[ring_i & 7] = pace_n ? (float)((perf_render_start - pace_prev) / 1000.0) : 0;
        ring_i++;
        pace_prev = perf_render_start;
        if (mot_n) {
            const double j = fabs(st - mot_prev);
            if (j > mot_maxjump) mot_maxjump = j;
            if (j > 3.0) mot_jumps++;
            if (j > 5.0 && jump_reports < 12) {          /* a jump: print the last eight frames, oldest first */
                jump_reports++;
                char line[400]; int n = 0;
                for (int k = 8; k >= 1; k--) {
                    const int idx = (ring_i - k) & 7;
                    n += snprintf(line + n, sizeof line - n, " [%.1f/%.1f/%.1f %.0fms]", ring_tr[idx], ring_tu[idx], ring_zo[idx], ring_dt[idx]);
                }
                ESP_LOGI(TAG, "perf JUMP of %.1f px; last frames (translate/turn/zoom px, ms):%s", j, line);
            }
        }
        mot_prev = st; mot_sum += st; mot_sumsq += st * st; mot_n++;
        if (st > mot_max) mot_max = st;
    }
#if NAV_PERF_OBJECTS
    { static int attached; if (!attached && perf_render_start > 8000000) { attached = 1; prof_attach(lv_screen_active()); } }
#endif
    if (perf_last_ready) perf_acc_gap += perf_render_start - perf_last_ready;
}

static void perf_wait_start_cb(lv_event_t *e) { (void)e; perf_wait_start = esp_timer_get_time(); }
static void perf_wait_finish_cb(lv_event_t *e) { (void)e; perf_acc_wait += esp_timer_get_time() - perf_wait_start; }

/* page slides: every frame drawn while the pages slide, and what the slide cost, printed when it ends */
static int sw_active, sw_from, sw_n, sw_page = -1;
static float sw_dur[40];
static int64_t sw_t0;

static void perf_render_ready_cb(lv_event_t *e)
{
    (void)e;
    int64_t now = esp_timer_get_time();
    {
        const double dur = (now - perf_render_start) / 1000.0;
        if (nav_ui_scrolling()) {
            if (!sw_active) { sw_active = 1; sw_from = sw_page; sw_n = 0; sw_t0 = perf_render_start; }
            if (sw_n < 40) sw_dur[sw_n] = (float)dur;
            sw_n++;
        } else {
            if (sw_active) {
                char line[400]; int n = 0; float worst = 0, sum = 0;
                for (int i = 0; i < sw_n && i < 40; i++) { if (sw_dur[i] > worst) worst = sw_dur[i]; sum += sw_dur[i]; if (i < 24) n += snprintf(line + n, sizeof line - n, " %.0f", sw_dur[i]); }
                ESP_LOGI(TAG, "perf page slide %d -> %d: %d frames in %.0f ms (avg %.0f ms to draw, slowest %.0f); each frame (ms):%s", sw_from, nav_ui_active_page(), sw_n,
                         (now - sw_t0) / 1000.0, sw_n ? sum / (sw_n < 40 ? sw_n : 40) : 0, worst, line);
                sw_active = 0;
            }
            sw_page = nav_ui_active_page();
        }
    }
    if ((now - perf_render_start) / 1000.0 > pace_worst_render) pace_worst_render = (now - perf_render_start) / 1000.0;
    perf_last_ready = now;
    perf_acc_frame += now - perf_render_start;
    perf_frames++;
    if (perf_last_report == 0) perf_last_report = now;
    if (now - perf_last_report >= 2000000 && perf_frames) {
        double secs = (now - perf_last_report) / 1e6;
        double f = (double)perf_frames;
        double frame_ms = perf_acc_frame / 1000.0 / f;
        double swap_ms = perf_acc_swap / 1000.0 / f;
        double send_ms = perf_acc_send / 1000.0 / f;
        ESP_LOGI(TAG, "perf: %.1f fps | per frame: total %.1f ms = draw %.1f + swap %.1f + send %.1f ms | "
                      "%.1f flushes, %.0f%% of screen | %s",
                 f / secs, frame_ms, frame_ms - swap_ms - send_ms, swap_ms, send_ms,
                 perf_flushes / f,
                 100.0 * (double)perf_pixels / f / (EXAMPLE_LCD_WIDTH * EXAMPLE_LCD_HEIGHT),
                 nav_build_tag);
        ESP_LOGI(TAG, "perf stages per frame (ms): map callback %.1f = areas %.1f + streets %.1f + route points %.1f + route masks %.1f | "
                      "waiting for a free buffer %.1f | gap between frames %.1f | %.1f strips",
                 nav_perf_us[0] / 1000.0 / f, nav_perf_us[1] / 1000.0 / f, nav_perf_us[2] / 1000.0 / f, nav_perf_us[3] / 1000.0 / f,
                 nav_perf_us[4] / 1000.0 / f, perf_acc_wait / 1000.0 / f, perf_acc_gap / 1000.0 / f, nav_perf_us[5] / f);
        ESP_LOGI(TAG, "perf map extras per frame (ms): marker %.1f, north badge + scale bar %.1f (badge circle %.1f, needle %.1f)", nav_perf_us[9] / 1000.0 / f, nav_perf_us[8] / 1000.0 / f, nav_perf_us[10] / 1000.0 / f, nav_perf_us[11] / 1000.0 / f);
        { lv_mem_monitor_t mon; lv_mem_monitor(&mon);
          ESP_LOGI(TAG, "perf label cache: %u images made, %u failed (last failed label %u, last made %u) | LVGL pool: %u KB free of %u, biggest %u KB",
                   (unsigned)nav_fl_stats[0], (unsigned)nav_fl_stats[1], (unsigned)nav_fl_stats[2], (unsigned)nav_fl_stats[3],
                   (unsigned)(mon.free_size / 1024), (unsigned)((mon.total_size) / 1024), (unsigned)(mon.free_biggest_size / 1024)); }
        ESP_LOGI(TAG, "perf main loop, longest turn since the last report: Bluetooth data %.1f ms, LVGL %.1f ms, the rest %.1f ms",
                 nav_loop_max_us[0] / 1000.0, nav_loop_max_us[1] / 1000.0, nav_loop_max_us[2] / 1000.0);
        nav_loop_max_us[0] = nav_loop_max_us[1] = nav_loop_max_us[2] = 0;
        ESP_LOGI(TAG, "perf display: %u sends have timed out waiting for the panel so far (each stalls a strip 200 ms); LVGL timers, longest: frame tick %.1f ms, label cache %.1f ms",
                 (unsigned)nav_lcd_timeouts, nav_timer_max_us[0] / 1000.0, nav_timer_max_us[1] / 1000.0);
        ESP_LOGI(TAG, "perf bus: longest touch read %.1f ms, longest battery-gauge read %.1f ms", nav_touch_max_us / 1000.0, nav_batt_max_us / 1000.0);
        nav_touch_max_us = 0; nav_batt_max_us = 0;
        nav_timer_max_us[0] = nav_timer_max_us[1] = 0;
        ESP_LOGI(TAG, "perf slowest data handler: %.1f ms (channel %u, %u bytes) | slowest turn-glyph render step: %.1f ms | new arrow ready after up to %u ms | kept arrows: %u shown from the cache, %u drawn on demand, %u drawn into the cache",
                 nav_write_worst_us / 1000.0, (unsigned)nav_write_worst_ch, (unsigned)nav_write_worst_len, nav_gl_render_us / 1000.0, (unsigned)nav_gl_job_ms,
                 (unsigned)nav_gl_cache_stats[0], (unsigned)nav_gl_cache_stats[1], (unsigned)nav_gl_cache_stats[2]);
        nav_write_worst_us = 0; nav_gl_render_us = 0; nav_gl_job_ms = 0;
        if (mot_n > 2) {
            const double mean = mot_sum / mot_n, sd = sqrt(fmax(0.0, mot_sumsq / mot_n - mean * mean));
            ESP_LOGI(TAG, "perf motion: the picture moves %.2f px a frame (sd %.2f, largest %.1f); biggest change from one frame to the next %.1f px; %u changes over 3 px",
                     mean, sd, mot_max, mot_maxjump, (unsigned)mot_jumps);
        }
        mot_sum = mot_sumsq = mot_max = mot_maxjump = 0; mot_n = mot_jumps = 0;
        if (pace_n > 2) {
            const double mean = pace_sum / pace_n, sd = sqrt(fmax(0.0, pace_sumsq / pace_n - mean * mean));
            ESP_LOGI(TAG, "perf pacing: %u frames, time between frames avg %.1f sd %.1f worst %.1f ms, %u over 55 ms; slowest frame to draw %.1f ms", (unsigned)pace_n, mean, sd, pace_max, (unsigned)pace_late, pace_worst_render);
        }
        pace_sum = pace_sumsq = pace_max = pace_worst_render = 0; pace_n = pace_late = 0;
        ESP_LOGI(TAG, "perf zoom: fastest %.2f nep/s, sharpest acceleration %.2f, rms acceleration %.2f (nepers: 0.69 = one octave)",
                 nav_zoom_stats[0], nav_zoom_stats[1], nav_zoom_stats[3] > 0 ? sqrtf(nav_zoom_stats[2] / nav_zoom_stats[3]) : 0.0f);
        nav_zoom_stats[0] = nav_zoom_stats[1] = nav_zoom_stats[2] = nav_zoom_stats[3] = 0;
        ESP_LOGI(TAG, "perf memory: street mask clearing %.1f ms and %.0f KB per frame | free internal RAM %u KB (largest block %u KB), PSRAM %u KB",
                 nav_perf_us[6] / 1000.0 / f, nav_perf_us[7] / 1024.0 / f,
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024), (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
#if NAV_PERF_OBJECTS
        prof_report(f);
#endif
        memset(nav_perf_us, 0, sizeof nav_perf_us);
        perf_acc_wait = perf_acc_gap = 0;
        perf_last_report = now;
        perf_acc_frame = perf_acc_swap = perf_acc_send = 0;
        perf_frames = perf_flushes = 0;
        perf_pixels = 0;
    }
}
#endif

#if NAV_PERF_LOG && NAV_PERF_OBJECTS
/* Times each object's own drawing (not its children); the slowest are listed with the frame log. Attached once, shortly after start. */
#define PROF_MAX 96
static struct { lv_obj_t *o; int64_t us; int64_t t0; } prof[PROF_MAX];
static int prof_n;
static void prof_cb(lv_event_t *e)
{
    const lv_event_code_t c = lv_event_get_code(e);
    if (c != LV_EVENT_DRAW_MAIN_BEGIN && c != LV_EVENT_DRAW_MAIN_END && c != LV_EVENT_DRAW_POST_BEGIN && c != LV_EVENT_DRAW_POST_END) return;
    lv_obj_t *o = lv_event_get_target_obj(e);
    for (int i = 0; i < prof_n; i++) {
        if (prof[i].o != o) continue;
        if (c == LV_EVENT_DRAW_MAIN_BEGIN || c == LV_EVENT_DRAW_POST_BEGIN) prof[i].t0 = esp_timer_get_time();
        else prof[i].us += esp_timer_get_time() - prof[i].t0;
        return;
    }
}
static void prof_attach(lv_obj_t *o)
{
    if (prof_n < PROF_MAX) { prof[prof_n].o = o; prof_n++; lv_obj_add_event_cb(o, prof_cb, LV_EVENT_ALL, NULL); }
    const uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++) prof_attach(lv_obj_get_child(o, (int32_t)i));
}
static void prof_report(double frames)
{
    int order[PROF_MAX];
    for (int i = 0; i < prof_n; i++) order[i] = i;
    for (int a = 0; a < prof_n; a++) for (int b = a + 1; b < prof_n; b++) if (prof[order[b]].us > prof[order[a]].us) { const int t = order[a]; order[a] = order[b]; order[b] = t; }
    for (int k = 0; k < 8 && k < prof_n; k++) {
        const int i = order[k];
        if (prof[i].us < 300 * frames) break;                    /* under 0.3 ms a frame: not worth a line */
        lv_area_t a;
        lv_obj_get_coords(prof[i].o, &a);
        ESP_LOGI(TAG, "perf object %d (page %d): %.1f ms/frame  %s at %d,%d size %dx%d", k + 1, nav_ui_active_page(), prof[i].us / 1000.0 / frames,
                 lv_obj_check_type(prof[i].o, &lv_label_class) ? "label" : "object", (int)a.x1, (int)a.y1, (int)lv_area_get_width(&a), (int)lv_area_get_height(&a));
    }
    for (int i = 0; i < prof_n; i++) prof[i].us = 0;
}
#endif

static void flush_to_panel(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
#if NAV_PERF_LOG
    int64_t t0 = esp_timer_get_time();
#endif
#if BOARD_COLOR_BITS == 18
    /* LVGL's 24-bit pixels are B, G, R and go out as 18-bit (top 6 bits of each byte). A 4 x 4 ordered dither, added 4 pixels (3 words) at a time, hides the lost steps. */
    {
        /* dither offsets for the 4 columns of each of the 4 row types, as the bytes of 4 pixels = 3 words */
        static const uint8_t bayer[4][4] = { {0, 2, 0, 2}, {3, 1, 3, 1}, {0, 2, 0, 2}, {3, 1, 3, 1} };
        const int w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
        if ((w & 3) == 0 && (((uintptr_t)px_map) & 3) == 0) {
            for (int row = 0; row < h; row++) {
                const uint8_t *d = bayer[(area->y1 + row) & 3];
                const uint32_t d0 = d[0], d1 = d[1], d2 = d[2], d3 = d[3];
                const uint32_t dw[3] = { d0 * 0x00010101u | d1 << 24,                /* B0 G0 R0 B1 */
                                         d1 * 0x00000101u | d2 * 0x00010000u | d2 << 24,   /* G1 R1 B2 G2 */
                                         d2 | d3 * 0x01010100u };                        /* R2 B3 G3 R3 */
                uint32_t *q = (uint32_t *)(void *)(px_map + (size_t)row * w * 3);
                for (int blk = 0; blk < w / 4; blk++, q += 3) {
                    for (int k = 0; k < 3; k++) {
                        const uint32_t a = q[k], b = dw[k];
                        uint32_t sum = (a & 0x7F7F7F7Fu) + (b & 0x7F7F7F7Fu);      /* bytewise add; the top bit of each byte is handled apart */
                        const uint32_t over = ((a & b) | ((a | b) & sum)) & 0x80808080u;      /* bytes that wrapped past 255 */
                        sum ^= (a ^ b) & 0x80808080u;
                        q[k] = sum | ((over >> 7) * 0xFFu);                         /* ... stay at 255 */
                    }
                }
            }
        } else {
            uint8_t *p = px_map;
            for (int row = 0; row < h; row++) {
                const uint8_t *dr = bayer[(area->y1 + row) & 3];
                for (int col = 0; col < w; col++, p += 3) {
                    const int d = dr[(area->x1 + col) & 3];
                    for (int c = 0; c < 3; c++) { int v = p[c] + d; p[c] = (uint8_t)(v > 255 ? 255 : v); }
                }
            }
        }
    }
#else
    lv_draw_sw_rgb565_swap(px_map, lv_area_get_size(area));
#endif
#if NAV_PERF_LOG
    int64_t t1 = esp_timer_get_time();
#endif

    /* Synchronous: esp_lcd_panel_draw_bitmap() blocks until the transfer completes, so "ready" can be signalled at once. */
#if BOARD_COLOR_BITS == 18
    LCD_ColorBegin();
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    if (!LCD_ColorWait()) nav_lcd_timeouts++;          /* this panel's driver queues the send and returns: wait until the buffer has really been read */
#else
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
#endif
#if NAV_PERF_LOG
    int64_t t2 = esp_timer_get_time();
    perf_acc_swap += t1 - t0;
    perf_acc_send += t2 - t1;
    perf_flushes++;
    perf_pixels += lv_area_get_size(area);
#endif
    lv_display_flush_ready(disp);
}

#if BOARD_COLOR_BITS == 18
/* The 18-bit conversion and send run on the second core while LVGL renders into the other buffer; lv_display_flush_ready is called when the send is done. */
typedef struct { lv_display_t *disp; lv_area_t area; uint8_t *px; } flush_job_t;
static QueueHandle_t flush_queue;

static void flush_task(void *arg)
{
    (void)arg;
    flush_job_t j;
    for (;;) {
        if (xQueueReceive(flush_queue, &j, portMAX_DELAY) == pdTRUE) flush_to_panel(j.disp, &j.area, j.px);
    }
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    const flush_job_t j = { disp, *area, px_map };
    xQueueSend(flush_queue, &j, portMAX_DELAY);
}
#else
#define flush_cb flush_to_panel
#endif

/* The SPD2010's QSPI writes pack 4 pixels, so partial-redraw areas must start and end on a 4-pixel boundary. */
static void rounder_cb(lv_event_t *e)
{
    lv_area_t *area = lv_event_get_invalidated_area(e);
    area->x1 = (area->x1 >> 2) << 2;
    area->x2 = ((area->x2 >> 2) << 2) + 3;
}

static volatile int touch_enabled = 1;
void lvgl_port_set_touch(int on) { touch_enabled = on ? 1 : 0; }
int lvgl_port_touch_enabled(void) { return touch_enabled; }

static void touchpad_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (!touch_enabled) { data->state = LV_INDEV_STATE_RELEASED; return; }      /* touch off (wet screen): the BOOT button is the only input */
    uint16_t x[5] = {0}, y[5] = {0};
    uint8_t count = 0;
    const int64_t tt0 = esp_timer_get_time();
    bool pressed = Touch_Get_xy(x, y, NULL, &count, CONFIG_ESP_LCD_TOUCH_MAX_POINTS);
    { const uint32_t us = (uint32_t)(esp_timer_get_time() - tt0); if (us > nav_touch_max_us) nav_touch_max_us = us; }
    if (pressed && count > 0) {
        data->point.x = x[0];
        data->point.y = y[0];
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

struct _lv_obj_t *lvgl_port_init(void)
{
    LCD_Init();   /* vendor: panel + backlight + touch bring-up (board/LCD_Driver, board/Touch_Driver) */

    lv_init();

    size_t buf_bytes = LVGL_BUF_PIXELS * LVGL_BYTES_PER_PX;
    void *buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    void *buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!buf1 || !buf2) {
        ESP_LOGW(TAG, "internal RAM too tight for 2x%u-byte draw buffers, falling back to PSRAM (slower)",
                 (unsigned)buf_bytes);
        heap_caps_free(buf1);
        heap_caps_free(buf2);
        buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
        buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
    }
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "failed to allocate %u-byte LVGL draw buffers", (unsigned)buf_bytes);
        abort();
    }
    ESP_LOGI(TAG, "draw buffers: 2 x %u bytes, %s", (unsigned)buf_bytes,
             esp_ptr_internal(buf1) ? "internal RAM" : "PSRAM");

#if BOARD_COLOR_BITS == 18
    flush_queue = xQueueCreate(2, sizeof(flush_job_t));
    xTaskCreatePinnedToCore(flush_task, "flush", 4096, NULL, 5, NULL, 1);
#endif
    lv_display_t *disp = lv_display_create(EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT);
#if BOARD_COLOR_BITS == 18
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB888);   /* converted for the panel in flush_cb */
#else
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);   /* swap is done in flush_cb */
#endif
    lv_display_set_buffers(disp, buf1, buf2, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
#if NAV_PERF_LOG
    lv_display_add_event_cb(disp, perf_render_start_cb, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(disp, perf_render_ready_cb, LV_EVENT_RENDER_READY, NULL);
    lv_display_add_event_cb(disp, perf_wait_start_cb, LV_EVENT_FLUSH_WAIT_START, NULL);
    lv_display_add_event_cb(disp, perf_wait_finish_cb, LV_EVENT_FLUSH_WAIT_FINISH, NULL);
#endif

    /* No mirror call: the panel's init-table orientation is the right one on this unit. */
    esp_lcd_panel_invert_color(panel_handle, BOARD_LCD_INVERT_COLOR);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read_cb);
    lv_indev_set_display(indev, disp);

    const esp_timer_create_args_t tick_args = {.callback = tick_cb, .name = "lvgl_tick"};
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    return lv_screen_active();
}

uint32_t lvgl_port_task_handler(void)
{
    return lv_timer_handler();
}
