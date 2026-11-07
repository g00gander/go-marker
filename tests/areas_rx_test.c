/* Unit test for the AREAS receiver.
 *   cc -I../ui -I../proto -o /tmp/areas_rx_test areas_rx_test.c ../ui/nav_areas_rx.c ../ui/nav_route_rx.c && /tmp/areas_rx_test */
#include <stdio.h>
#include <string.h>
#include "nav_areas_rx.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static void put16(uint8_t *p, int v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, int32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)((uint32_t)v >> (8 * i)); }

static nav_areas_rx_t rx;

/* n polygons, each nv vertices; put `per` polygons in every chunk */
static nav_rx_result_t send_set(int n, int nv, int per, int corrupt, int split_poly)
{
    uint8_t rec[NAV_AREAS_MAX_RAW]; size_t rl = 0;
    size_t one = 2 + (size_t)nv * 4;
    for (int i = 0; i < n; i++) {
        uint8_t *r = rec + rl;
        r[0] = (uint8_t)(i & 1); r[1] = (uint8_t)nv;
        for (int k = 0; k < nv; k++) { put16(r + 2 + 4 * k, i * 10 + k); put16(r + 4 + 4 * k, -(i * 10 + k)); }
        rl += one;
    }
    uint8_t pkt[200];
    pkt[0] = NAV_AREAS_HEADER; put16(pkt + 1, n); put32(pkt + 3, 470000000); put32(pkt + 7, -1229000000);
    nav_rx_result_t r = nav_areas_rx_feed(&rx, pkt, 11);
    if (r == NAV_RX_ERROR) return r;
    for (int i = 0; i < n; i += per) {
        int k = n - i < per ? n - i : per;
        pkt[0] = NAV_AREAS_POLYS; put16(pkt + 1, i);
        size_t len = (size_t)k * one;
        memcpy(pkt + 3, rec + i * one, len);
        if (split_poly) len -= 3;                       /* cut a polygon in half */
        r = nav_areas_rx_feed(&rx, pkt, 3 + len);
        if (r == NAV_RX_ERROR) return r;
    }
    pkt[0] = NAV_AREAS_END; put16(pkt + 1, nav_crc16(rec, rl) ^ (corrupt ? 1 : 0));
    return nav_areas_rx_feed(&rx, pkt, 3);
}

int main(void)
{
    CHECK(send_set(10, 8, 3, 0, 0) == NAV_RX_READY);
    CHECK(rx.received == 10 && rx.polys[3].kind == 1 && rx.polys[3].n == 8 && rx.polys[3].x[2] == 32 && rx.polys[3].y[2] == -32);
    CHECK(rx.origin_lat_e7 == 470000000);
    CHECK(send_set(NAV_MAX_POLYS, NAV_POLY_MAXV, 1, 0, 0) == NAV_RX_READY);
    CHECK(send_set(0, 8, 3, 0, 0) == NAV_RX_READY);
    CHECK(send_set(5, 8, 2, 1, 0) == NAV_RX_ERROR);          /* bad CRC */
    CHECK(send_set(5, 8, 2, 0, 1) == NAV_RX_ERROR);          /* truncated polygon */
    uint8_t h[11] = {NAV_AREAS_HEADER}; put16(h + 1, NAV_MAX_POLYS + 1);
    CHECK(nav_areas_rx_feed(&rx, h, 11) == NAV_RX_ERROR);
    CHECK(send_set(6, 4, 6, 0, 0) == NAV_RX_READY);          /* recovers */
    printf(failures ? "areas_rx_test: %d FAILED\n" : "areas_rx_test: all passed\n", failures);
    return failures != 0;
}
