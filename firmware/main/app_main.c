/* Display firmware entry point. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "I2C_Driver.h"
#include "TCA9554PWR.h"
#include "PWR_Key.h"

#include "lvgl_port.h"
#include "nav_ui.h"
#include "nav_trip_store.h"
#include "feed.h"
#include "ble_server.h"
#include "board_motion.h"
#include "nav_nvs_store.h"
#include "board_time.h"
#include "board_sound.h"
#include "board_battery.h"
#include "nav_link.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "board_display.h"

#define NAV_PAGE_CYCLE_TEST 0       /* 1: flip through the pages every 7 s (slide timing) */
uint32_t nav_loop_max_us[3];     /* longest time in each part of the main loop since the last report (microseconds), see lvgl_port.c */
static const char *TAG = "app_main";

/* Touch on/off (off for rain and wet hands, BOOT is then the only input); saved across power cycles. */
static void touch_apply(nav_saved_settings_t *cfg, int on, int say)
{
    on = on ? 1 : 0;
    if (cfg->touch_on == on && !say) { lvgl_port_set_touch(on); return; }
    cfg->touch_on = (uint8_t)on;
    lvgl_port_set_touch(on);
    if (say) { nav_ui_toast(on ? "Touch on" : "Touch off"); nav_nvs_save_settings(cfg); }
}

/* BOOT button (GPIO 0, active low, debounced): short press = next screen, hold 1.25 s = touch on/off. */
#ifdef NAV_BOARD_1_85B      /* the 1.46 keeps its button as it was: this is for the 1.85B only */
static void boot_button_init(void)
{
    gpio_config_t c = { .pin_bit_mask = 1ULL << GPIO_NUM_0, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&c);
}
static void boot_button_poll(nav_saved_settings_t *cfg)
{
    static int stable = 1, long_done;
    static uint32_t changed_at, pressed_at;
    const int level = gpio_get_level(GPIO_NUM_0);
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (level != stable && now - changed_at > 40) {
        stable = level;
        changed_at = now;
        if (!level) { pressed_at = now; long_done = 0; }
        else if (!long_done) nav_ui_next_page();                       /* a short press, on release */
    }
    if (!stable && !long_done && now - pressed_at >= 1250) {         /* held: toggle the touch screen */
        long_done = 1;
        touch_apply(cfg, !cfg->touch_on, 1);
    }
}
#else
static void boot_button_init(void) {}
static void boot_button_poll(nav_saved_settings_t *cfg) { (void)cfg; }
#endif

/* Build stamp: printed in the frame-timing log to match a measurement to its firmware. */
#ifndef NAV_DEMO_RIDE
#define NAV_DEMO_RIDE 0     /* 1 = built-in fake ride when no phone is connected (bench testing only) */
#endif
static void *psram_alloc(size_t bytes) { return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM); }   /* big buffers live in PSRAM */
const char *nav_build_tag = "NAVBUILD " __DATE__ " " __TIME__;

void app_main(void)
{
    nav_nvs_init();
    PWR_Init();     /* latches board power on if the physical switch is held at boot */
    I2C_Init();
    if (EXIO_Init() != ESP_OK) {
        ESP_LOGW(TAG, "EXIO_Init failed - IO expander (TCA9554, drives the LCD reset line) not responding");
    }

    lv_obj_t *screen = lvgl_port_init();

    nav_store_init(&nav_nvs_ops);   /* trip stats survive power-off */
    nav_ui_img_alloc = psram_alloc;                    /* the marker image goes to PSRAM: internal RAM is needed by the Bluetooth controller */
    nav_ui_init(screen);
    { uint8_t order[6]; if (nav_nvs_load_layout(order)) nav_ui_set_page_order(order, 6); }       /* the screen order chosen on the phone */
    uint32_t layout_seen = 0;
    nav_ui_set_tires_enabled(nav_nvs_load_tires_on());     /* the tire screen is only in the strip while the phone has monitoring on */
    uint32_t tires_seen = 0;
    board_time_init();                                     /* restore the clock if the chip kept it, and the time zone */
    board_sound_init();
    board_battery_init();
    board_motion_init();                                   /* the motion sensor (1.85B): idle until the phone asks for its stream */
    boot_button_init();
    uint32_t time_seen = 0;

    nav_saved_settings_t cfg;
    nav_nvs_load_settings(&cfg);
    board_sound_set_enabled(cfg.voice_on);
    board_sound_play(SOUND_BOOT);                           /* silent if the voice is off */
    int applied_pct = cfg.last_night ? cfg.night_pct : cfg.day_pct;
#ifndef NAV_BOARD_1_85B
    Set_Backlight((uint8_t)applied_pct);                 /* the level of the last session, not the vendor default */
#endif                                                   /* (the 1.85B keeps it off until the logo is drawn: see the main loop) */
    uint32_t settings_seen = 0;
    nav_ui_set_metric(cfg.metric);       /* units and clock format: as last chosen on the phone (default imperial, 12-hour) */
    nav_ui_set_clock24(cfg.clock24);
    board_sound_set_volume(cfg.volume);
    lvgl_port_set_touch(cfg.touch_on);
    nav_ui_set_night(cfg.last_night);     /* the day or night the display was in last time: automatic themes start from it */
    nav_ui_set_style(cfg.style);          /* standard or vintage colors, as last chosen on the phone */
#ifdef NAV_BOARD_1_85B
    nav_ui_splash_hold(1);                /* the logo's pop starts when the backlight comes on (below) */
#endif
    nav_ui_splash_show(1);                /* the start-up screen, in the theme that was last chosen */

    nav_link_alloc = psram_alloc;
    nav_link_init();
#if NAV_DEMO_RIDE
    nav_status_t status;
    feed_init();
    uint16_t n;
    nav_ui_set_route(FEED_ORIGIN_LAT_E7, FEED_ORIGIN_LON_E7, feed_route(&n), n);
    nav_ui_set_roads(feed_roads(&n), n);
    nav_ui_set_areas(feed_areas(&n), n);
    feed_step(0, &status);
    nav_ui_update(&status);
    ESP_LOGI(TAG, "up: running the demo ride on real hardware");
#else
    ESP_LOGI(TAG, "up: waiting for the phone");
#endif

    ble_server_start();
    int phone_seen = 0;   /* once the phone writes anything, the demo ride stops */

    TickType_t last_status = xTaskGetTickCount(), last_stats = last_status;
    while (1) {
        const int64_t lt0 = esp_timer_get_time();
        PWR_Loop();
        if (ble_server_poll() && !phone_seen) {
            phone_seen = 1;
            ESP_LOGI(TAG, "phone data arriving: demo ride off");
        }
        const int64_t lt1 = esp_timer_get_time();
        const uint32_t lv_wait_ms = lvgl_port_task_handler();      /* how long until LVGL has something to do */
        const int64_t lt2 = esp_timer_get_time();
        if (lt1 - lt0 > nav_loop_max_us[0]) nav_loop_max_us[0] = (uint32_t)(lt1 - lt0);      /* longest: power key + Bluetooth data handling */
        if (lt2 - lt1 > nav_loop_max_us[1]) nav_loop_max_us[1] = (uint32_t)(lt2 - lt1);      /* longest: LVGL (its timers and a frame) */
#ifdef NAV_BOARD_1_85B
        {   /* backlight after the first frame, so the panel never shows what its memory held at power-up */
            static uint32_t first_frame_ms;
            static int backlight_on;
            if (!backlight_on) {
                const uint32_t t = (uint32_t)(esp_timer_get_time() / 1000);
                if (!first_frame_ms) first_frame_ms = t;
                else if (t - first_frame_ms >= 300) { Set_Backlight((uint8_t)applied_pct); backlight_on = 1; nav_ui_splash_pop(); }
            }
        }
#endif
        boot_button_poll(&cfg);
#if NAV_PAGE_CYCLE_TEST
        {   /* test only: slide to the next page every 7 seconds, so the slides can be timed without anyone swiping */
            static uint32_t cycle_ms;
            const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
            if (now_ms - cycle_ms > 7000 && now_ms > 20000) { cycle_ms = now_ms; nav_ui_next_page(); }
        }
#endif
        {   /* the phone starts and stops the motion stream; the link dropping stops it too */
            static uint32_t motion_seen;
            uint32_t mseq;
            const int mwant = nav_link_motion_requested(&mseq);
            if (mseq != motion_seen) { motion_seen = mseq; board_motion_enable(mwant); }
            if (!ble_server_connected()) board_motion_enable(0);
        }
        {   /* a phone is pairing: its code goes on the screen until it is typed in (or the attempt ends) */
            static uint32_t shown_code;
            const uint32_t pc = ble_server_pairing_code();
            if (pc != shown_code) {
                const int was_showing = shown_code != 0;
                shown_code = pc;
                nav_ui_pairing_code(pc);
                if (was_showing && !pc && ble_server_secure()) nav_ui_toast("Paired");
            }
        }
        {   /* battery level for the clock page: from the gauge on boards that have one, hidden otherwise */
            static uint32_t bat_at;
            uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
            if (now_ms - bat_at > 2000) {
                bat_at = now_ms;
                board_battery_t b = board_battery_get();
                nav_ui_set_battery(b.valid ? b.percent : -1, b.valid && b.current_ma > 20);
            }
        }

        TickType_t now = xTaskGetTickCount();
        if (now - last_stats >= pdMS_TO_TICKS(10000)) {
            last_stats = now;
            const nav_link_stats_t *ls = nav_link_stats();
            ESP_LOGI(TAG, "link: %s | status %lu (last speed %.1f mph), weather %lu, routes %lu, road sets %lu (%lu segs), altitude %lu pkts (%ld m), rejected %lu, traffic %lu spans / %lu alerts",
                     ble_server_connected() ? "connected" : "NOT connected", (unsigned long)ls->status, ls->last_speed_kmh_x10 / 10.0 * 0.621371,
                     (unsigned long)ls->weather, (unsigned long)ls->route_ok, (unsigned long)ls->roads_ok,
                     (unsigned long)ls->last_road_segments, (unsigned long)ls->telemetry, (long)ls->last_altitude_m,
                     (unsigned long)ls->rejected, (unsigned long)ls->traffic_spans, (unsigned long)ls->traffic_alerts);

        }
#if NAV_DEMO_RIDE
        if (!phone_seen && now - last_status >= pdMS_TO_TICKS(1000)) {
            last_status = now;
            feed_step(1.0, &status);
            nav_ui_update(&status);
        }
#endif
        {   /* the phone sets the clock on connection and every ~10 minutes */
            uint32_t tseq;
            const nav_time_t *nt = nav_link_time(&tseq);
            if (nt && tseq != time_seen) { time_seen = tseq; board_time_set(nt->unix_utc, nt->tz_offset_s); }
        }

        /* Backlight follows the phone's day/night levels; saved across power cycles. */
        {
            uint32_t seq;
            const nav_settings_t *ns = nav_link_settings(&seq);
            int dirty = 0;
            if (ns && seq != settings_seen) { settings_seen = seq; cfg.day_pct = ns->day_pct; cfg.night_pct = ns->night_pct; cfg.voice_on = ns->voice_on; board_sound_set_enabled(cfg.voice_on);
                cfg.metric = ns->metric; cfg.clock24 = ns->clock24; cfg.style = ns->style; { uint8_t was = cfg.volume; cfg.volume = ns->volume; board_sound_set_volume(cfg.volume); if (ns->touch != 2 && ns->touch != cfg.touch_on) touch_apply(&cfg, ns->touch, 1); if (cfg.volume != was) board_sound_play(SOUND_LETS_GO); }   /* say something so the new volume can be heard */ nav_ui_set_metric(cfg.metric); nav_ui_set_clock24(cfg.clock24); nav_ui_set_style(cfg.style); dirty = 1; }
            int night = nav_ui_is_night();
            if (night != cfg.last_night && nav_link_stats()->status) { cfg.last_night = (uint8_t)night; dirty = 1; }
            int want = cfg.last_night ? cfg.night_pct : cfg.day_pct;
            if (want != applied_pct) { applied_pct = want; Set_Backlight((uint8_t)want); }
            if (dirty) nav_nvs_save_settings(&cfg);
        }

        {   /* a new screen order from the phone: apply it and remember it */
            uint32_t lseq;
            const nav_layout_t *nl = nav_link_layout(&lseq);
            if (nl && lseq != layout_seen) {
                layout_seen = lseq;
                if (nav_ui_set_page_order(nl->order, NAV_PAGE_COUNT)) {
                    nav_nvs_save_layout(nl->order);
                    ESP_LOGI(TAG, "screen order from the phone: %d %d %d %d %d", nl->order[0], nl->order[1], nl->order[2], nl->order[3], nl->order[4]);
                }
            }
        }
        {   /* the phone turned tire monitoring on or off: remember it (the screen itself follows the packet) */
            uint32_t tseq;
            const int on = nav_link_tpms_enabled(&tseq);
            if (tseq != tires_seen) { tires_seen = tseq; nav_nvs_save_tires_on(on); ESP_LOGI(TAG, "tire monitoring %s", on ? "on" : "off"); }
        }
        ble_server_set_page((uint8_t)nav_ui_active_page());                  /* the phone sends the fast compass data only for the compass page */

        /* Logo until the phone has connected and sent a position, then the map. Follows the Bluetooth link, with a 4 s grace on dropouts. */
        {
            static int map_on = 0, prev_conn = 0;
            static uint32_t status_at_conn = 0;
            static TickType_t lost_at = 0;
            int conn = ble_server_connected();
            uint32_t sc = nav_link_stats()->status;
            static int idle_seen = 0;                                 /* saw "not navigating" on this connection */
            if (conn && !prev_conn) { status_at_conn = sc; idle_seen = 0; board_sound_play(SOUND_CONNECT); }   /* count statuses from this connection on */
            if (!conn && prev_conn) { lost_at = now; board_sound_play(SOUND_DISCONNECT); }
            prev_conn = conn;
            if (conn && sc != status_at_conn) {                       /* a status has arrived on this connection */
                int navigating = (nav_link_stats()->last_flags & NAV_FLAG_NAVIGATING) != 0;
                if (!navigating) idle_seen = 1;
                else if (idle_seen) { idle_seen = 0; board_sound_play(SOUND_LETS_GO); }   /* a route started: not on a reconnect mid-route */
            }
            int gone = !conn && (now - lost_at) > pdMS_TO_TICKS(4000);
            int want = map_on;
            if (map_on && gone) want = 0;
            if (!map_on && conn && sc != status_at_conn && now > pdMS_TO_TICKS(2500)) want = 1;   /* logo for at least 2.5 s at power-on */
            if (want != map_on) { map_on = want; nav_ui_splash_show(!map_on); }
        }
        /* Sleep as long as LVGL can wait (1 to 4 ms) so the touch reading is not held up a full tick. */
        { const int64_t lt3 = esp_timer_get_time(); if (lt3 - lt2 > nav_loop_max_us[2]) nav_loop_max_us[2] = (uint32_t)(lt3 - lt2); }   /* longest: everything else in the loop */
        vTaskDelay(pdMS_TO_TICKS(lv_wait_ms > 4 ? 4 : (lv_wait_ms < 1 ? 1 : lv_wait_ms)));
    }
}
