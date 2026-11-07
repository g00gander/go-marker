#include "nav_route_rx.h"

#include <string.h>

/* nav_pt_t is two little-endian int16, so on the little-endian targets we run on the struct memory is the wire bytes. */
_Static_assert(sizeof(nav_pt_t) == 4, "nav_pt_t must be 4 bytes");

uint16_t nav_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int32_t rd32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

void nav_route_rx_reset(nav_route_rx_t *rx)
{
    rx->expected = 0;
    rx->received = 0;
    rx->active = 0;
}

nav_rx_result_t nav_route_rx_feed(nav_route_rx_t *rx, const uint8_t *d, size_t n)
{
    if (n < 1) return NAV_RX_ERROR;

    switch (d[0]) {
    case NAV_ROUTE_HEADER:
        if (n < 11) return NAV_RX_ERROR;
        nav_route_rx_reset(rx);
        rx->expected = rd16(d + 1);
        rx->origin_lat_e7 = rd32(d + 3);
        rx->origin_lon_e7 = rd32(d + 7);
        if (rx->expected < 2 || rx->expected > NAV_RX_MAX_POINTS) return NAV_RX_ERROR;
        rx->active = 1;
        return NAV_RX_PENDING;

    case NAV_ROUTE_POINTS: {
        if (!rx->active || n < 3) return NAV_RX_ERROR;
        uint16_t start = rd16(d + 1);
        size_t count = (n - 3) / 4;
        if (start != rx->received || (size_t)rx->received + count > rx->expected) {
            rx->active = 0;                        /* out of order or too many: drop this route */
            return NAV_RX_ERROR;
        }
        memcpy(&rx->pts[rx->received], d + 3, count * 4);
        rx->received = (uint16_t)(rx->received + count);
        return NAV_RX_PENDING;
    }

    case NAV_ROUTE_END: {
        if (!rx->active || n < 3) return NAV_RX_ERROR;
        rx->active = 0;
        if (rx->received != rx->expected) return NAV_RX_ERROR;
        uint16_t want = rd16(d + 1);
        uint16_t got = nav_crc16((const uint8_t *)rx->pts, (size_t)rx->received * 4);
        return want == got ? NAV_RX_READY : NAV_RX_ERROR;
    }

    default:
        return NAV_RX_ERROR;
    }
}
