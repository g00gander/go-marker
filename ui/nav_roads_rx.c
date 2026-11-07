#include "nav_roads_rx.h"

#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int16_t rds16(const uint8_t *p) { return (int16_t)rd16(p); }
static int32_t rd32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

void nav_roads_rx_reset(nav_roads_rx_t *rx)
{
    rx->expected = 0;
    rx->received = 0;
    rx->active = 0;
}

nav_rx_result_t nav_roads_rx_feed(nav_roads_rx_t *rx, const uint8_t *d, size_t n)
{
    if (n < 1) return NAV_RX_ERROR;

    switch (d[0]) {
    case NAV_ROADS_HEADER:
        if (n < 11) return NAV_RX_ERROR;
        nav_roads_rx_reset(rx);
        rx->expected = rd16(d + 1);
        rx->origin_lat_e7 = rd32(d + 3);
        rx->origin_lon_e7 = rd32(d + 7);
        if (rx->expected > NAV_MAX_ROADS) return NAV_RX_ERROR;
        rx->active = 1;
        return NAV_RX_PENDING;

    case NAV_ROADS_SEGS: {
        if (!rx->active || n < 3) return NAV_RX_ERROR;
        uint16_t start = rd16(d + 1);
        size_t count = (n - 3) / NAV_ROADS_REC;
        if (start != rx->received || (size_t)rx->received + count > rx->expected) {
            rx->active = 0;
            return NAV_RX_ERROR;
        }
        for (size_t i = 0; i < count; i++) {
            const uint8_t *r = d + 3 + i * NAV_ROADS_REC;
            nav_seg_t *s = &rx->segs[rx->received + i];
            s->x1 = rds16(r);     s->y1 = rds16(r + 2);
            s->x2 = rds16(r + 4); s->y2 = rds16(r + 6);
            s->major = r[8] ? 1 : 0;
        }
        memcpy(rx->raw + (size_t)rx->received * NAV_ROADS_REC, d + 3, count * NAV_ROADS_REC);
        rx->received = (uint16_t)(rx->received + count);
        return NAV_RX_PENDING;
    }

    case NAV_ROADS_END: {
        if (!rx->active || n < 3) return NAV_RX_ERROR;
        rx->active = 0;
        if (rx->received != rx->expected) return NAV_RX_ERROR;
        uint16_t want = rd16(d + 1);
        uint16_t got = nav_crc16(rx->raw, (size_t)rx->received * NAV_ROADS_REC);
        return want == got ? NAV_RX_READY : NAV_RX_ERROR;
    }

    default:
        return NAV_RX_ERROR;
    }
}
