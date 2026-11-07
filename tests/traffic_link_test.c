/* Unit test for the TRAFFIC characteristic as nav_link receives it (the UI functions are stubs that record what they are given).
 *   cc -DLV_CONF_INCLUDE_SIMPLE -I../ui -I../proto -I../sim -I../lvgl -o /tmp/traffic_link_test traffic_link_test.c ../ui/nav_link.c ../ui/nav_areas_rx.c ../ui/nav_roads_rx.c ../ui/nav_route_rx.c && /tmp/traffic_link_test */
#include <stdio.h>
#include <string.h>
#include "nav_link.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static nav_traffic_span_t got_span[NAV_MAX_TRAFFIC_SPANS];
static int got_n = -1, alert_calls, alert_level = -1, alert_dist = -1, alert_len = -1;

void nav_ui_set_traffic(const nav_traffic_span_t *s, int n) { got_n = n; if (n) memcpy(got_span, s, (size_t)n * sizeof *s); }
void nav_ui_traffic_alert(int level, uint16_t dist, uint16_t len) { alert_calls++; alert_level = level; alert_dist = dist; alert_len = len; }
void nav_ui_frame_origin(int32_t a, int32_t b) { (void)a; (void)b; }
void nav_ui_set_altitude(float a, float b, int c) { (void)a; (void)b; (void)c; }
void nav_ui_set_polys(const nav_poly_t *p, uint16_t n) { (void)p; (void)n; }
void nav_ui_set_roads(const nav_seg_t *p, uint16_t n) { (void)p; (void)n; }
void nav_ui_set_areas(const nav_area_t *p, uint16_t n) { (void)p; (void)n; }
void nav_ui_set_roundabout(const nav_roundabout_t *r) { (void)r; }
void nav_ui_set_route(int32_t a, int32_t b, const nav_pt_t *p, uint16_t n) { (void)a; (void)b; (void)p; (void)n; }
void nav_ui_set_weather(const nav_weather_t *w) { (void)w; }
void nav_ui_update(const nav_status_t *s) { (void)s; }
void nav_ui_set_tpms(const nav_tpms_t *t) { (void)t; }

static void spans(const uint16_t *v, int count, int *bytes_out, uint8_t *pkt)
{
    pkt[0] = NAV_TRAFFIC_SPANS; pkt[1] = (uint8_t)count;
    for (int i = 0; i < count; i++) {
        pkt[2 + 5 * i] = (uint8_t)v[3 * i];       pkt[3 + 5 * i] = (uint8_t)(v[3 * i] >> 8);
        pkt[4 + 5 * i] = (uint8_t)v[3 * i + 1];   pkt[5 + 5 * i] = (uint8_t)(v[3 * i + 1] >> 8);
        pkt[6 + 5 * i] = (uint8_t)v[3 * i + 2];
    }
    *bytes_out = 2 + 5 * count;
}

int main(void)
{
    nav_link_init();
    uint8_t pkt[128]; int len;

    /* two spans arrive intact */
    const uint16_t two[] = { 10, 20, NAV_TRAFFIC_SLOW, 300, 410, NAV_TRAFFIC_STOPPED };
    spans(two, 2, &len, pkt);
    nav_link_write(NAV_CH_TRAFFIC, pkt, (size_t)len);
    CHECK(got_n == 2);
    CHECK(got_span[0].first == 10 && got_span[0].last == 20 && got_span[0].level == NAV_TRAFFIC_SLOW);
    CHECK(got_span[1].first == 300 && got_span[1].last == 410 && got_span[1].level == NAV_TRAFFIC_STOPPED);

    /* an empty list clears */
    spans(two, 0, &len, pkt);
    nav_link_write(NAV_CH_TRAFFIC, pkt, (size_t)len);
    CHECK(got_n == 0);

    /* bad ones are refused and leave the last good state alone */
    got_n = 99;
    const uint16_t backwards[] = { 20, 10, NAV_TRAFFIC_SLOW };
    spans(backwards, 1, &len, pkt); nav_link_write(NAV_CH_TRAFFIC, pkt, (size_t)len); CHECK(got_n == 99);
    const uint16_t badlevel[] = { 1, 2, 3 };
    spans(badlevel, 1, &len, pkt); nav_link_write(NAV_CH_TRAFFIC, pkt, (size_t)len); CHECK(got_n == 99);
    const uint16_t zerolevel[] = { 1, 2, 0 };
    spans(zerolevel, 1, &len, pkt); nav_link_write(NAV_CH_TRAFFIC, pkt, (size_t)len); CHECK(got_n == 99);
    spans(two, 2, &len, pkt); nav_link_write(NAV_CH_TRAFFIC, pkt, (size_t)len - 1); CHECK(got_n == 99);          /* truncated */
    spans(two, 2, &len, pkt); nav_link_write(NAV_CH_TRAFFIC, pkt, (size_t)len + 1); CHECK(got_n == 99);          /* extra byte */
    pkt[0] = NAV_TRAFFIC_SPANS; pkt[1] = NAV_MAX_TRAFFIC_SPANS + 1;
    nav_link_write(NAV_CH_TRAFFIC, pkt, 2 + 5 * (NAV_MAX_TRAFFIC_SPANS + 1)); CHECK(got_n == 99);                /* too many */
    pkt[0] = 9; nav_link_write(NAV_CH_TRAFFIC, pkt, 6); CHECK(got_n == 99 && alert_calls == 0);                    /* unknown type */
    nav_link_write(NAV_CH_TRAFFIC, pkt, 0);

    /* an alert */
    uint8_t a[6] = { NAV_TRAFFIC_ALERT, NAV_TRAFFIC_STOPPED, 0x58, 0x02, 0xA4, 0x01 };      /* 0x0258 = 600 m, 0x01A4 = 420 m */
    nav_link_write(NAV_CH_TRAFFIC, a, 6);
    CHECK(alert_calls == 1 && alert_level == NAV_TRAFFIC_STOPPED && alert_dist == 600 && alert_len == 420);
    a[1] = 0; nav_link_write(NAV_CH_TRAFFIC, a, 6);
    CHECK(alert_calls == 2 && alert_level == 0);
    a[1] = 3; nav_link_write(NAV_CH_TRAFFIC, a, 6); CHECK(alert_calls == 2);                 /* level out of range */
    nav_link_write(NAV_CH_TRAFFIC, a, 5); CHECK(alert_calls == 2);                           /* short */

    printf(failures ? "%d FAILED\n" : "traffic_link_test: all passed\n", failures);
    return failures != 0;
}
