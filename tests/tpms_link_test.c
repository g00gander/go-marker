/* Unit test for the TPMS and LAYOUT characteristics as nav_link receives them (the UI functions are stubs that record what they are given).
 *   cc -DLV_CONF_INCLUDE_SIMPLE -I../ui -I../proto -I../sim -I../lvgl -o /tmp/tpms_link_test tpms_link_test.c ../ui/nav_link.c ../ui/nav_areas_rx.c ../ui/nav_roads_rx.c ../ui/nav_route_rx.c && /tmp/tpms_link_test */
#include <stdio.h>
#include <string.h>
#include "nav_link.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static nav_tpms_t got;
static int calls;

void nav_ui_set_tpms(const nav_tpms_t *t) { got = *t; calls++; }
void nav_ui_set_traffic(const nav_traffic_span_t *s, int n) { (void)s; (void)n; }
void nav_ui_traffic_alert(int level, uint16_t dist, uint16_t len) { (void)level; (void)dist; (void)len; }
void nav_ui_frame_origin(int32_t a, int32_t b) { (void)a; (void)b; }
void nav_ui_set_altitude(float a, float b, int c) { (void)a; (void)b; (void)c; }
void nav_ui_set_polys(const nav_poly_t *p, uint16_t n) { (void)p; (void)n; }
void nav_ui_set_roads(const nav_seg_t *p, uint16_t n) { (void)p; (void)n; }
void nav_ui_set_areas(const nav_area_t *p, uint16_t n) { (void)p; (void)n; }
void nav_ui_set_roundabout(const nav_roundabout_t *r) { (void)r; }
void nav_ui_set_route(int32_t a, int32_t b, const nav_pt_t *p, uint16_t n) { (void)a; (void)b; (void)p; (void)n; }
void nav_ui_set_weather(const nav_weather_t *w) { (void)w; }
void nav_ui_update(const nav_status_t *s) { (void)s; }

int main(void)
{
    nav_link_init();

    /* a good packet arrives intact (little-endian, 16 bytes) */
    const uint8_t good[16] = { NAV_PROTO_VERSION, NAV_TPMS_ENABLED | NAV_TPMS_FRONT_OK | NAV_TPMS_REAR_OK | NAV_TPMS_REAR_LOW,
                               0x6D, 0x01,            /* 365 = 36.5 psi */
                               0xF0, 0x00,            /* 240 = 24.0 psi */
                               0x2C, 0x01, 0x2C, 0x01, /* 300 = 30 psi limits */
                               72, 0xFF,
                               0x68, 0x01, 0x90, 0x01 }; /* 360 = 36 psi, 400 = 40 psi set pressures */
    nav_link_write(NAV_CH_TPMS, good, sizeof good);
    CHECK(calls == 1 && nav_link_stats()->tpms == 1);
    CHECK(got.flags == (NAV_TPMS_ENABLED | NAV_TPMS_FRONT_OK | NAV_TPMS_REAR_OK | NAV_TPMS_REAR_LOW));
    CHECK(got.front_x10 == 365 && got.rear_x10 == 240 && got.front_low_x10 == 300 && got.rear_low_x10 == 300);
    CHECK(got.front_temp_f == 72 && got.rear_temp_f == 0xFF);
    CHECK(got.front_set_x10 == 360 && got.rear_set_x10 == 400);

    /* the enabled flag: seq changes only when it does, the first packet always counts */
    uint32_t tseq0;
    CHECK(nav_link_tpms_enabled(&tseq0) == 1 && tseq0 == 1);
    nav_link_write(NAV_CH_TPMS, good, sizeof good);
    uint32_t tseq1; nav_link_tpms_enabled(&tseq1); CHECK(tseq1 == 1);
    uint8_t off[16]; memcpy(off, good, sizeof off); off[1] = 0;
    nav_link_write(NAV_CH_TPMS, off, sizeof off);
    CHECK(nav_link_tpms_enabled(&tseq1) == 0 && tseq1 == 2);
    nav_link_write(NAV_CH_TPMS, good, sizeof good);
    CHECK(nav_link_tpms_enabled(&tseq1) == 1 && tseq1 == 3);
    calls = 1;

    /* bad ones are refused */
    uint8_t bad[17];
    memcpy(bad, good, sizeof good);
    nav_link_write(NAV_CH_TPMS, good, 12); CHECK(calls == 1);                       /* the old 12-byte packet */
    nav_link_write(NAV_CH_TPMS, good, 15); CHECK(calls == 1);                       /* short */
    bad[16] = 0; nav_link_write(NAV_CH_TPMS, bad, 17); CHECK(calls == 1);           /* long */
    memcpy(bad, good, sizeof good); bad[0] = 9; nav_link_write(NAV_CH_TPMS, bad, 16); CHECK(calls == 1);      /* wrong version */
    memcpy(bad, good, sizeof good); bad[1] = 0x80; nav_link_write(NAV_CH_TPMS, bad, 16); CHECK(calls == 1);   /* unknown flag */
    nav_link_write(NAV_CH_TPMS, good, 0); CHECK(calls == 1);

    /* the screen order: six screens, in any permutation */
    uint32_t seq = 0;
    CHECK(nav_link_layout(&seq) == NULL);
    uint8_t lay[8] = { NAV_PROTO_VERSION, 6, 5, 4, 3, 2, 1, 0 };
    nav_link_write(NAV_CH_LAYOUT, lay, 8);
    const nav_layout_t *l = nav_link_layout(&seq);
    CHECK(l && seq == 1 && l->count == 6 && l->order[0] == 5 && l->order[5] == 0);

    /* a list from an older phone app (five screens, 7 bytes) gets the tire page last */
    const uint8_t old5[7] = { NAV_PROTO_VERSION, 5, 4, 0, 1, 2, 3 };
    nav_link_write(NAV_CH_LAYOUT, old5, 7);
    l = nav_link_layout(&seq);
    CHECK(l && seq == 2 && l->count == 6 && l->order[0] == 4 && l->order[4] == 3 && l->order[5] == NAV_PAGE_TPMS);

    /* not a permutation: refused, the last good one stays */
    uint8_t dup[8] = { NAV_PROTO_VERSION, 6, 0, 0, 1, 2, 3, 4 };
    nav_link_write(NAV_CH_LAYOUT, dup, 8); CHECK(seq == 2);
    const uint8_t old_dup[7] = { NAV_PROTO_VERSION, 5, 0, 0, 1, 2, 3 };
    nav_link_write(NAV_CH_LAYOUT, old_dup, 7); CHECK(seq == 2);
    const uint8_t old_bad[7] = { NAV_PROTO_VERSION, 5, 0, 1, 2, 3, 5 };               /* the tire id inside a five-screen list */
    nav_link_write(NAV_CH_LAYOUT, old_bad, 7); CHECK(seq == 2);
    nav_link_write(NAV_CH_LAYOUT, lay, 6); CHECK(seq == 2);                          /* short */

    printf(failures ? "%d FAILED\n" : "tpms_link_test: all passed\n", failures);
    return failures != 0;
}
