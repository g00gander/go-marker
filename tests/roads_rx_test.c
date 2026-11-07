/* Unit test for the ROADS receiver (chunk reassembly, order and CRC checks).
 *   cc -I../ui -I../proto -o /tmp/roads_rx_test roads_rx_test.c ../ui/nav_roads_rx.c ../ui/nav_route_rx.c && /tmp/roads_rx_test */
#include <stdio.h>
#include <string.h>

#include "nav_roads_rx.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static void put16(uint8_t *p, int v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, int32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)((uint32_t)v >> (8 * i)); }

static nav_roads_rx_t rx;          /* large: keep off the stack */
static uint8_t rec[NAV_MAX_ROADS * NAV_ROADS_REC];

static size_t make_records(int n)
{
    for (int i = 0; i < n; i++) {
        uint8_t *r = rec + i * NAV_ROADS_REC;
        put16(r, i - 100); put16(r + 2, -i); put16(r + 4, i * 3); put16(r + 6, 500 - i);
        r[8] = (i % 5 == 0);
    }
    return (size_t)n * NAV_ROADS_REC;
}

static nav_rx_result_t send_set(int n, int per_chunk, int corrupt_crc, int skip_chunk)
{
    uint8_t pkt[200];
    size_t bytes = make_records(n);
    pkt[0] = NAV_ROADS_HEADER; put16(pkt + 1, n); put32(pkt + 3, 470000000); put32(pkt + 7, -1229000000);
    nav_rx_result_t r = nav_roads_rx_feed(&rx, pkt, 11);
    if (r == NAV_RX_ERROR) return r;
    int chunk = 0;
    for (int i = 0; i < n; i += per_chunk, chunk++) {
        int k = n - i < per_chunk ? n - i : per_chunk;
        if (chunk == skip_chunk) continue;
        pkt[0] = NAV_ROADS_SEGS; put16(pkt + 1, i);
        memcpy(pkt + 3, rec + i * NAV_ROADS_REC, (size_t)k * NAV_ROADS_REC);
        r = nav_roads_rx_feed(&rx, pkt, 3 + (size_t)k * NAV_ROADS_REC);
        if (r == NAV_RX_ERROR) return r;
    }
    pkt[0] = NAV_ROADS_END; put16(pkt + 1, nav_crc16(rec, bytes) ^ (corrupt_crc ? 0x1 : 0));
    return nav_roads_rx_feed(&rx, pkt, 3);
}

int main(void)
{
    /* a normal set, several chunks */
    CHECK(send_set(300, 19, 0, -1) == NAV_RX_READY);
    CHECK(rx.received == 300 && rx.expected == 300);
    CHECK(rx.origin_lat_e7 == 470000000 && rx.origin_lon_e7 == -1229000000);
    CHECK(rx.segs[0].x1 == -100 && rx.segs[0].y1 == 0 && rx.segs[0].x2 == 0 && rx.segs[0].y2 == 500 && rx.segs[0].major == 1);
    CHECK(rx.segs[7].x1 == -93 && rx.segs[7].y1 == -7 && rx.segs[7].x2 == 21 && rx.segs[7].y2 == 493 && rx.segs[7].major == 0);
    CHECK(rx.segs[299].x2 == 897 && rx.segs[299].major == 0);

    /* max size */
    CHECK(send_set(NAV_MAX_ROADS, 19, 0, -1) == NAV_RX_READY);
    CHECK(rx.received == NAV_MAX_ROADS);

    /* empty set is allowed (an area with no roads) */
    CHECK(send_set(0, 19, 0, -1) == NAV_RX_READY);

    /* bad CRC, missing chunk, oversize header */
    CHECK(send_set(120, 19, 1, -1) == NAV_RX_ERROR);
    CHECK(send_set(120, 19, 0, 2) == NAV_RX_ERROR);
    uint8_t h[11] = {NAV_ROADS_HEADER}; put16(h + 1, NAV_MAX_ROADS + 1);
    CHECK(nav_roads_rx_feed(&rx, h, 11) == NAV_RX_ERROR);

    /* a record chunk with no header is refused; a new set works after all that */
    nav_roads_rx_reset(&rx);
    uint8_t c[3 + 9] = {NAV_ROADS_SEGS};
    CHECK(nav_roads_rx_feed(&rx, c, sizeof c) == NAV_RX_ERROR);
    CHECK(send_set(50, 19, 0, -1) == NAV_RX_READY);

    printf(failures ? "roads_rx_test: %d FAILED\n" : "roads_rx_test: all passed\n", failures);
    return failures != 0;
}
